#include "assistant_telemetry.h"
#include "assistant_storage.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <coreinit/time.h>

#define TELEMETRY_SCHEMA_VERSION 1
#define TELEMETRY_KEEP_EVENTS 256
#define TELEMETRY_SCAN_EVENTS 1024
#define TELEMETRY_EVENT_BYTES 4096
#define TELEMETRY_RESPONSE_BYTES (256 * 1024)

static unsigned int telemetry_sequence = 0;

static void copy_text(char *destination, size_t destination_size, const char *source)
{
    if (!destination || destination_size == 0)
        return;

    if (!source)
        source = "";

    size_t index = 0;
    for (; index + 1 < destination_size && source[index]; index++)
        destination[index] = source[index];
    destination[index] = '\0';
}

static bool join_path(char *destination, size_t destination_size, const char *left, const char *right)
{
    if (!destination || destination_size == 0 || !left || !right)
        return false;

    size_t left_length = strlen(left);
    bool separator = left_length > 0 && left[left_length - 1] != '/';
    int length = snprintf(destination, destination_size, "%s%s%s", left, separator ? "/" : "", right);
    return length >= 0 && (size_t)length < destination_size;
}

static bool path_is_directory(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool path_is_regular_file(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool ends_with_ignore_case(const char *value, const char *suffix)
{
    if (!value || !suffix)
        return false;

    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length
        && strcasecmp(value + value_length - suffix_length, suffix) == 0;
}

static bool mkdir_recursive(const char *path)
{
    if (!path || !path[0])
        return false;

    char current[ASSISTANT_TELEMETRY_PATH];
    copy_text(current, sizeof(current), path);
    char *cursor = strstr(current, ":/");
    cursor = cursor ? cursor + 2 : current;
    if (*cursor == '/')
        cursor++;

    for (; *cursor; cursor++)
    {
        if (*cursor != '/')
            continue;

        *cursor = '\0';
        if (current[0] && mkdir(current, 0777) != 0 && errno != EEXIST)
        {
            *cursor = '/';
            return false;
        }
        *cursor = '/';
    }

    return mkdir(current, 0777) == 0 || errno == EEXIST;
}

static void json_escape(const char *source, char *destination, size_t destination_size)
{
    if (!destination || destination_size == 0)
        return;

    if (!source)
        source = "";

    size_t output = 0;
    for (size_t input = 0; source[input] && output + 1 < destination_size; input++)
    {
        unsigned char value = (unsigned char)source[input];
        const char *escape = NULL;
        switch (value)
        {
            case '\\': escape = "\\\\"; break;
            case '"': escape = "\\\""; break;
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            default: break;
        }

        if (escape)
        {
            size_t length = strlen(escape);
            if (output + length >= destination_size)
                break;
            memcpy(destination + output, escape, length);
            output += length;
        }
        else if (value >= 0x20)
        {
            destination[output++] = (char)value;
        }
    }
    destination[output] = '\0';
}

static bool valid_title_id(const char *title_id)
{
    if (!title_id || strlen(title_id) != 16)
        return false;

    for (int index = 0; index < 16; index++)
    {
        if (!isxdigit((unsigned char)title_id[index]))
            return false;
    }
    return true;
}

static bool read_file_text(const char *path, size_t maximum_bytes, char **content)
{
    *content = NULL;
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    long length = ftell(file);
    if (length < 0 || (size_t)length > maximum_bytes || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }

    char *buffer = (char *)malloc((size_t)length + 1);
    if (!buffer)
    {
        fclose(file);
        return false;
    }

    size_t read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    buffer[read] = '\0';
    *content = buffer;
    return true;
}

static void read_xml_value(const char *xml, const char *tag, char *value, size_t value_size)
{
    if (!xml || !tag || !value || value_size == 0)
        return;

    char start_tag[96];
    char end_tag[96];
    snprintf(start_tag, sizeof(start_tag), "<%s>", tag);
    snprintf(end_tag, sizeof(end_tag), "</%s>", tag);
    const char *start = strstr(xml, start_tag);
    if (!start)
        return;

    start += strlen(start_tag);
    const char *end = strstr(start, end_tag);
    if (!end || end <= start)
        return;

    size_t length = (size_t)(end - start);
    if (length >= value_size)
        length = value_size - 1;
    memcpy(value, start, length);
    value[length] = '\0';
}

static bool hash_file_sha256(const char *path, char output[65])
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts_ret(&sha, 0) != 0)
    {
        mbedtls_sha256_free(&sha);
        fclose(file);
        return false;
    }

    unsigned char buffer[32768];
    bool success = true;
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (mbedtls_sha256_update_ret(&sha, buffer, read) != 0)
        {
            success = false;
            break;
        }
    }
    if (ferror(file))
        success = false;
    fclose(file);

    unsigned char digest[32];
    if (success && mbedtls_sha256_finish_ret(&sha, digest) != 0)
        success = false;
    mbedtls_sha256_free(&sha);
    if (!success)
        return false;

    for (int index = 0; index < 32; index++)
        snprintf(output + (index * 2), 3, "%02X", digest[index]);
    output[64] = '\0';
    return true;
}

static bool inspect_title_at(
    const char *storage_root,
    const char *storage_target,
    const char *title_id,
    AssistantTelemetryTitle *title)
{
    char normalized[17];
    for (int index = 0; index < 16; index++)
        normalized[index] = (char)tolower((unsigned char)title_id[index]);
    normalized[16] = '\0';

    char high[9];
    char low[9];
    memcpy(high, normalized, 8);
    high[8] = '\0';
    memcpy(low, normalized + 8, 8);
    low[8] = '\0';

    char high_path[ASSISTANT_TELEMETRY_PATH];
    char title_path[ASSISTANT_TELEMETRY_PATH];
    if (!join_path(high_path, sizeof(high_path), storage_root, high)
        || !join_path(title_path, sizeof(title_path), high_path, low)
        || !path_is_directory(title_path))
    {
        return false;
    }

    memset(title, 0, sizeof(*title));
    for (int index = 0; index < 16; index++)
        title->title_id[index] = (char)toupper((unsigned char)title_id[index]);
    title->title_id[16] = '\0';
    copy_text(title->game_name, sizeof(title->game_name), title->title_id);
    copy_text(title->storage_target, sizeof(title->storage_target), storage_target);
    copy_text(title->installed_path, sizeof(title->installed_path), title_path);

    char meta_path[ASSISTANT_TELEMETRY_PATH];
    if (join_path(meta_path, sizeof(meta_path), title_path, "meta/meta.xml"))
    {
        char *xml = NULL;
        if (read_file_text(meta_path, 1024 * 1024, &xml))
        {
            read_xml_value(xml, "longname_en", title->game_name, sizeof(title->game_name));
            if (strcmp(title->game_name, title->title_id) == 0)
                read_xml_value(xml, "shortname_en", title->game_name, sizeof(title->game_name));
            free(xml);
        }
    }

    char code_path[ASSISTANT_TELEMETRY_PATH];
    if (!join_path(code_path, sizeof(code_path), title_path, "code"))
        return true;

    DIR *code = opendir(code_path);
    if (!code)
        return true;

    struct dirent *entry;
    while ((entry = readdir(code)) != NULL)
    {
        if (entry->d_name[0] == '.' || !ends_with_ignore_case(entry->d_name, ".rpx"))
            continue;

        char runtime_path[ASSISTANT_TELEMETRY_PATH];
        if (!join_path(runtime_path, sizeof(runtime_path), code_path, entry->d_name)
            || !path_is_regular_file(runtime_path))
        {
            continue;
        }

        copy_text(title->runtime_path, sizeof(title->runtime_path), runtime_path);
        hash_file_sha256(runtime_path, title->runtime_sha256);
        break;
    }
    closedir(code);
    return true;
}

bool assistant_telemetry_find_title(const char *title_id, AssistantTelemetryTitle *title)
{
    if (!valid_title_id(title_id) || !title)
        return false;

    if (inspect_title_at(
               "fs:/vol/storage_mlc01/usr/title",
               "NAND / MLC",
               title_id,
               title)
        || inspect_title_at(
               "fs:/vol/storage_usb01/usr/title",
               "USB",
               title_id,
               title))
        return true;

    AssistantTitleLocation location;
    if (!assistant_find_catalog_title(title_id, &location))
        return false;
    memset(title, 0, sizeof(*title));
    copy_text(title->title_id, sizeof(title->title_id), location.title_id);
    copy_text(title->game_name, sizeof(title->game_name), location.title_id);
    copy_text(title->storage_target, sizeof(title->storage_target), location.usb ? "USB" : "NAND / MLC");
    copy_text(title->installed_path, sizeof(title->installed_path), location.path);
    return true;
}

static int compare_names_ascending(const void *left, const void *right)
{
    const char *const *left_name = (const char *const *)left;
    const char *const *right_name = (const char *const *)right;
    return strcmp(*left_name, *right_name);
}

static unsigned int collect_event_names(const char *root, char **names, unsigned int maximum, bool *overflow)
{
    if (overflow)
        *overflow = false;

    DIR *directory = opendir(root);
    if (!directory)
        return 0;

    unsigned int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (entry->d_name[0] == '.' || !ends_with_ignore_case(entry->d_name, ".json"))
            continue;

        if (count >= maximum)
        {
            if (overflow)
                *overflow = true;
            continue;
        }

        size_t length = strlen(entry->d_name);
        names[count] = (char *)malloc(length + 1);
        if (!names[count])
            continue;
        memcpy(names[count], entry->d_name, length + 1);
        count++;
    }
    closedir(directory);
    qsort(names, count, sizeof(char *), compare_names_ascending);
    return count;
}

static void free_event_names(char **names, unsigned int count)
{
    for (unsigned int index = 0; index < count; index++)
        free(names[index]);
}

static void prune_events(const char *root)
{
    char *names[TELEMETRY_SCAN_EVENTS];
    bool overflow = false;
    unsigned int count = collect_event_names(root, names, TELEMETRY_SCAN_EVENTS, &overflow);
    unsigned int remove_count = count > TELEMETRY_KEEP_EVENTS ? count - TELEMETRY_KEEP_EVENTS : 0;
    if (overflow && remove_count == 0)
        remove_count = 1;

    for (unsigned int index = 0; index < remove_count; index++)
    {
        char path[ASSISTANT_TELEMETRY_PATH];
        if (join_path(path, sizeof(path), root, names[index]))
            unlink(path);
    }
    free_event_names(names, count);
}

bool assistant_telemetry_write_event(
    const char *sd_root,
    const AssistantTelemetryEvent *event,
    char event_id[64])
{
    if (!sd_root || !sd_root[0] || !event || !event->event_type || !event->event_type[0])
        return false;

    char root[ASSISTANT_TELEMETRY_PATH];
    if (!join_path(root, sizeof(root), sd_root, "uinjectforge/telemetry/events") || !mkdir_recursive(root))
        return false;

    uint64_t ticks = OSGetSystemTime();
    uint64_t observed_ms = OSTicksToMilliseconds(ticks);
    char id[64];
    char path[ASSISTANT_TELEMETRY_PATH];
    for (unsigned int attempt = 0; attempt < 1000; attempt++)
    {
        unsigned int sequence = ++telemetry_sequence;
        snprintf(id, sizeof(id), "%016llX-%08X", (unsigned long long)ticks, sequence);
        char file_name[80];
        snprintf(file_name, sizeof(file_name), "%s.json", id);
        if (!join_path(path, sizeof(path), root, file_name))
            return false;
        if (!path_is_regular_file(path))
            break;
    }

    char event_type[ASSISTANT_TELEMETRY_TEXT * 2];
    char source[ASSISTANT_TELEMETRY_TEXT * 2];
    char title_id[64];
    char game_name[ASSISTANT_TELEMETRY_TEXT * 2];
    char storage_target[128];
    char installed_path[ASSISTANT_TELEMETRY_PATH * 2];
    char runtime_path[ASSISTANT_TELEMETRY_PATH * 2];
    char runtime_sha256[160];
    char result[ASSISTANT_TELEMETRY_TEXT * 2];
    char message[ASSISTANT_TELEMETRY_TEXT * 2];
    char correlation_id[160];
    json_escape(event->event_type, event_type, sizeof(event_type));
    json_escape(event->source, source, sizeof(source));
    json_escape(event->title_id, title_id, sizeof(title_id));
    json_escape(event->game_name, game_name, sizeof(game_name));
    json_escape(event->storage_target, storage_target, sizeof(storage_target));
    json_escape(event->installed_path, installed_path, sizeof(installed_path));
    json_escape(event->runtime_path, runtime_path, sizeof(runtime_path));
    json_escape(event->runtime_sha256, runtime_sha256, sizeof(runtime_sha256));
    json_escape(event->result, result, sizeof(result));
    json_escape(event->message, message, sizeof(message));
    json_escape(event->correlation_id && event->correlation_id[0] ? event->correlation_id : id, correlation_id, sizeof(correlation_id));

    char temporary_path[ASSISTANT_TELEMETRY_PATH];
    int temporary_length = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path);
    if (temporary_length < 0 || (size_t)temporary_length >= sizeof(temporary_path))
        return false;

    FILE *file = fopen(temporary_path, "wb");
    if (!file)
        return false;

    int written = fprintf(
        file,
        "{\"schemaVersion\":%d,\"eventId\":\"%s\",\"eventType\":\"%s\",\"source\":\"%s\",\"observedAtConsoleMs\":%llu,\"titleId\":\"%s\",\"gameName\":\"%s\",\"storageTarget\":\"%s\",\"installedPath\":\"%s\",\"runtimePath\":\"%s\",\"runtimeSha256\":\"%s\",\"result\":\"%s\",\"message\":\"%s\",\"correlationId\":\"%s\"}\n",
        TELEMETRY_SCHEMA_VERSION,
        id,
        event_type,
        source,
        (unsigned long long)observed_ms,
        title_id,
        game_name,
        storage_target,
        installed_path,
        runtime_path,
        runtime_sha256,
        result,
        message,
        correlation_id);
    bool success = written > 0 && fflush(file) == 0 && fclose(file) == 0;
    if (!success || rename(temporary_path, path) != 0)
    {
        unlink(temporary_path);
        return false;
    }

    if (event_id)
        copy_text(event_id, 64, id);
    prune_events(root);
    return true;
}

char *assistant_telemetry_read_events_json(
    const char *sd_root,
    unsigned int maximum_events,
    unsigned int *event_count,
    bool *truncated)
{
    if (event_count)
        *event_count = 0;
    if (truncated)
        *truncated = false;

    char *json = (char *)malloc(TELEMETRY_RESPONSE_BYTES);
    if (!json)
        return NULL;
    json[0] = '[';
    json[1] = '\0';
    size_t offset = 1;

    char root[ASSISTANT_TELEMETRY_PATH];
    if (!sd_root || !join_path(root, sizeof(root), sd_root, "uinjectforge/telemetry/events"))
    {
        json[offset++] = ']';
        json[offset] = '\0';
        return json;
    }

    char *names[TELEMETRY_SCAN_EVENTS];
    bool overflow = false;
    unsigned int count = collect_event_names(root, names, TELEMETRY_SCAN_EVENTS, &overflow);
    if (maximum_events == 0 || maximum_events > 64)
        maximum_events = 64;
    if (truncated)
        *truncated = overflow || count > maximum_events;

    unsigned int appended = 0;
    for (unsigned int reverse = count; reverse > 0 && appended < maximum_events; reverse--)
    {
        char path[ASSISTANT_TELEMETRY_PATH];
        if (!join_path(path, sizeof(path), root, names[reverse - 1]))
            continue;

        char *content = NULL;
        if (!read_file_text(path, TELEMETRY_EVENT_BYTES, &content))
        {
            if (truncated)
                *truncated = true;
            continue;
        }

        size_t length = strlen(content);
        while (length > 0 && isspace((unsigned char)content[length - 1]))
            content[--length] = '\0';
        char *start = content;
        while (*start && isspace((unsigned char)*start))
            start++;
        length = strlen(start);
        if (length < 2 || start[0] != '{' || start[length - 1] != '}')
        {
            free(content);
            if (truncated)
                *truncated = true;
            continue;
        }

        size_t required = length + (appended > 0 ? 1 : 0) + 2;
        if (required >= TELEMETRY_RESPONSE_BYTES - offset)
        {
            free(content);
            if (truncated)
                *truncated = true;
            break;
        }
        if (appended > 0)
            json[offset++] = ',';
        memcpy(json + offset, start, length);
        offset += length;
        json[offset] = '\0';
        appended++;
        free(content);
    }

    json[offset++] = ']';
    json[offset] = '\0';
    free_event_names(names, count);
    if (event_count)
        *event_count = appended;
    return json;
}

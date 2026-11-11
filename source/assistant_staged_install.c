#include "assistant_staged_install.h"
#include "assistant_json.h"
#include "assistant_policy.h"
#include <mbedtls/sha256.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool hex_text(const char *text, size_t length)
{
    if (strlen(text) != length) return false;
    for (size_t i = 0; i < length; ++i) if (!isxdigit((unsigned char)text[i])) return false;
    return true;
}

static void upper_hex(char *text)
{
    for (; *text; ++text) *text = (char)toupper((unsigned char)*text);
}

bool assistant_staged_install_parse(const char *json, AssistantStagedInstallRequest *request)
{
    if (!json || !request) return false;
    memset(request, 0, sizeof(*request));
    AssistantJsonField fields[5];
    size_t count = 0;
    if (!assistant_json_object(json, fields, 5, &count) || count != 5) return false;
    char target[8] = {0};
    for (size_t i = 0; i < count; ++i) {
        char *output; size_t capacity;
        if (!strcmp(fields[i].name, "requestId")) { output = request->request_id; capacity = sizeof(request->request_id); }
        else if (!strcmp(fields[i].name, "packageName")) { output = request->package_name; capacity = sizeof(request->package_name); }
        else if (!strcmp(fields[i].name, "titleId")) { output = request->title_id; capacity = sizeof(request->title_id); }
        else if (!strcmp(fields[i].name, "tmdSha256")) { output = request->tmd_sha256; capacity = sizeof(request->tmd_sha256); }
        else if (!strcmp(fields[i].name, "storageTarget")) { output = target; capacity = sizeof(target); }
        else return false;
        if (!assistant_json_decode_string(fields[i].value, output, capacity)) return false;
    }
    const char *name = request->package_name;
    if (strcmp(target, "usb") || !name[0] || name[0] == '.' || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) if (*p < 32 || *p == 127) return false;
    if (!hex_text(request->request_id, 32) || !hex_text(request->title_id, 16) || !hex_text(request->tmd_sha256, 64)) return false;
    // Remote installation is for full game/homebrew packages, never updates,
    // DLC or system titles. Existing manual installation remains unchanged.
    upper_hex(request->request_id); upper_hex(request->title_id); upper_hex(request->tmd_sha256);
    return !strncmp(request->title_id, "00050000", 8) || !strncmp(request->title_id, "00050002", 8);
}

static bool regular_file(const char *root, const char *name, unsigned long long expected, bool exact)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s", root, name);
    struct stat info;
    return length > 0 && (size_t)length < sizeof(path) && lstat(path, &info) == 0
        && S_ISREG(info.st_mode) && info.st_size > 0
        && (!exact || (unsigned long long)info.st_size == expected);
}

static unsigned long long big_endian(const unsigned char *bytes, unsigned count)
{
    unsigned long long value = 0;
    while (count--) value = (value << 8) | *bytes++;
    return value;
}

bool assistant_staged_install_identity(const char *root, char title_id[17], char digest[65])
{
    title_id[0] = digest[0] = 0;
    struct stat directory;
    if (!root || lstat(root, &directory) || !S_ISDIR(directory.st_mode)
        || assistant_policy_read(root) != ASSISTANT_POLICY_ANY
        || !regular_file(root, "title.tik", 0, false) || !regular_file(root, "title.cert", 0, false)) return false;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/title.tmd", root);
    struct stat info;
    if (length < 0 || (size_t)length >= sizeof(path) || lstat(path, &info) || !S_ISREG(info.st_mode)
        || info.st_size < 0xB04 || info.st_size > 0xB04 + 1024 * 48) return false;
    size_t size = (size_t)info.st_size;
    unsigned char *tmd = malloc(size);
    if (!tmd) return false;
    FILE *file = fopen(path, "rb");
    bool ok = file && fread(tmd, 1, size, file) == size && !ferror(file);
    if (file && fclose(file)) ok = false;
    if (!ok) { free(tmd); return false; }
    unsigned count = (unsigned)big_endian(tmd + 0x1DE, 2);
    unsigned high = (unsigned)big_endian(tmd + 0x18C, 4);
    ok = big_endian(tmd, 4) == 0x00010004 && (high == 0x00050000 || high == 0x00050002)
        && count > 0 && count <= 1024 && size == 0xB04 + count * 48;
    for (unsigned i = 0; ok && i < count; ++i) {
        const unsigned char *record = tmd + 0xB04 + i * 48;
        unsigned id = (unsigned)big_endian(record, 4);
        unsigned type = (unsigned)big_endian(record + 6, 2);
        unsigned long long bytes = big_endian(record + 8, 8);
        char name[32]; snprintf(name, sizeof(name), "%08x.app", id);
        ok = bytes > 0 && regular_file(root, name, bytes, true);
        if (ok && (type & 2)) {
            snprintf(name, sizeof(name), "%08x.h3", id);
            unsigned long long h3_bytes = (bytes / 0x10000000ULL + (bytes % 0x10000000ULL != 0)) * 20;
            ok = regular_file(root, name, h3_bytes, true);
        }
        for (unsigned j = 0; ok && j < i; ++j)
            if (!memcmp(record, tmd + 0xB04 + j * 48, 4)) ok = false;
    }
    unsigned char hash[32];
    if (ok) ok = mbedtls_sha256_ret(tmd, size, hash, 0) == 0;
    if (ok) {
        snprintf(title_id, 17, "%016llX", big_endian(tmd + 0x18C, 8));
        for (unsigned i = 0; i < 32; ++i) snprintf(digest + i * 2, 3, "%02X", hash[i]);
    }
    free(tmd);
    return ok;
}

static bool claim_file(const char *root, const char *kind, const char *key, const AssistantStagedInstallRequest *request)
{
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s-%s.claim", root, kind, key);
    if (length < 0 || (size_t)length >= sizeof(path)) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); return false; }
    bool ok = fprintf(file, "UIFSDINSTALL1\n%s\n%s\n%s\nUSB\n", request->request_id, request->title_id, request->tmd_sha256) > 0;
    if (fflush(file)) ok = false;
    if (fsync(fileno(file))) ok = false;
    if (fclose(file)) ok = false;
    // Never remove a partial claim: after power loss or a lost response the
    // owner must inspect the console, not automatically replay installation.
    return ok;
}

bool assistant_staged_install_claim(const char *root, const AssistantStagedInstallRequest *request)
{
    return root && request && hex_text(request->request_id, 32) && hex_text(request->tmd_sha256, 64)
        && claim_file(root, "request", request->request_id, request)
        && claim_file(root, "package", request->tmd_sha256, request);
}

static bool read_journal_file(const char *root, const char *kind, const char *key,
    const char *extension, char data[256], size_t *size)
{
    char path[1024]; struct stat directory, info;
    if (!root || lstat(root, &directory) || !S_ISDIR(directory.st_mode)) return false;
    int length = snprintf(path, sizeof(path), "%s/%s-%s.%s", root, kind, key, extension);
    if (length < 0 || (size_t)length >= sizeof(path) || lstat(path, &info)
        || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size >= 256) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    *size = fread(data, 1, 255, file);
    bool ok = *size == (size_t)info.st_size && !ferror(file) && feof(file)
        && !memchr(data, 0, *size);
    if (fclose(file)) ok = false;
    data[*size] = 0;
    return ok;
}

static bool read_matching_claims(const char *root, const char *id, char claim[256], size_t *size)
{
    if (!id || !hex_text(id, 32)) return false;
    char normalized[33]; memcpy(normalized, id, 33); upper_hex(normalized);
    if (!read_journal_file(root, "request", normalized, "claim", claim, size)) return false;
    AssistantStagedInstallRequest identity = {0}; int consumed = 0;
    if (sscanf(claim, "UIFSDINSTALL1\n%32[0-9A-F]\n%16[0-9A-F]\n%64[0-9A-F]\nUSB\n%n",
        identity.request_id, identity.title_id, identity.tmd_sha256, &consumed) != 3
        || consumed != (int)*size || strcmp(identity.request_id, normalized)
        || !hex_text(identity.title_id, 16) || !hex_text(identity.tmd_sha256, 64)
        || (strncmp(identity.title_id, "00050000", 8) && strncmp(identity.title_id, "00050002", 8))) return false;
    char expected[256];
    int count = snprintf(expected, sizeof(expected), "UIFSDINSTALL1\n%s\n%s\n%s\nUSB\n",
        identity.request_id, identity.title_id, identity.tmd_sha256);
    if (count != (int)*size || memcmp(claim, expected, *size)) return false;
    char package[256]; size_t package_size = 0;
    return read_journal_file(root, "package", identity.tmd_sha256, "claim", package, &package_size)
        && package_size == *size && !memcmp(package, claim, *size);
}

bool assistant_staged_install_record_result(const char *root, const char *id, bool success)
{
    char claim[256]; size_t size;
    if (!read_matching_claims(root, id, claim, &size)) return false;
    char normalized[33]; memcpy(normalized, id, 33); upper_hex(normalized);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/result-%s.receipt", root, normalized);
    if (length < 0 || (size_t)length >= sizeof(path)) return false;
    // Immutable, bounded terminal records. A partial record stays unknown, not replayable.
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); return false; }
    bool ok = fwrite(claim, 1, size, file) == size
        && fprintf(file, "RESULT\n%s\n", success ? "installed" : "install-failed") > 0;
    if (fflush(file)) ok = false;
    if (fsync(fileno(file))) ok = false;
    if (fclose(file)) ok = false;
    return ok;
}

bool assistant_staged_install_read_result(const char *root, const char *id, bool *success)
{
    if (!success) return false;
    *success = false;
    char claim[256]; size_t claim_size;
    if (!read_matching_claims(root, id, claim, &claim_size)) return false;
    char normalized[33]; memcpy(normalized, id, 33); upper_hex(normalized);
    char data[256]; size_t size;
    if (!read_journal_file(root, "result", normalized, "receipt", data, &size)
        || size <= claim_size || memcmp(claim, data, claim_size)) return false;
    if (!strcmp(data + claim_size, "RESULT\ninstalled\n")) { *success = true; return true; }
    return !strcmp(data + claim_size, "RESULT\ninstall-failed\n");
}

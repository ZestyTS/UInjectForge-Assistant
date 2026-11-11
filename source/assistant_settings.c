#include "assistant_settings.h"
#include "assistant_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

AssistantSettings assistant_settings_defaults(void)
{
    return (AssistantSettings){8573, 8573, true, false, true, true, true, false, false, 100, false};
}
bool assistant_settings_valid(const AssistantSettings *s)
{
    return s && s->http_port >= 1 && s->http_port <= 65535 && s->broadcast_port >= 1
        && s->broadcast_port <= 65535 && s->ui_scale_percent >= 45 && s->ui_scale_percent <= 100;
}
bool assistant_settings_network_changed(const AssistantSettings *a, const AssistantSettings *b)
{
    return a->http_port != b->http_port || a->broadcast_port != b->broadcast_port
        || a->broadcast_enabled != b->broadcast_enabled || a->passive_broadcast_enabled != b->passive_broadcast_enabled;
}
static bool bool_value(const AssistantJsonField *f, bool *out)
{
    if (f->value_length == 4 && !memcmp(f->value, "true", 4)) { *out = true; return true; }
    if (f->value_length == 5 && !memcmp(f->value, "false", 5)) { *out = false; return true; }
    return false;
}
static bool number_value(const AssistantJsonField *f, double *out)
{
    char *end;
    errno = 0;
    *out = strtod(f->value, &end);
    return !errno && isfinite(*out) && end == f->value + f->value_length;
}
static const char *names[] = {"httpPort", "broadcastPort", "broadcastEnabled", "passiveBroadcastEnabled",
    "simpleUi", "installEnabled", "manualInstallEnabled", "autoInstallEnabled", "allowUiScale", "uiScale", "uiMode", "settingsSchemaVersion"};
static int known_field(const char *name)
{
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(name, names[i])) return (int)i;
    return -1;
}
bool assistant_settings_parse(const char *json, AssistantSettings *settings)
{
    if (!settings) return false;
    AssistantJsonField *fields = calloc(64, sizeof(*fields));
    size_t count = 0;
    AssistantSettings s = assistant_settings_defaults();
    bool ok = fields && assistant_json_object(json, fields, 64, &count);
    bool has_udp = false, has_mode = false, simple_mode = true;
    for (size_t i = 0; ok && i < count; i++) {
        const AssistantJsonField *f = &fields[i];
        int id = known_field(f->name);
        double number;
        bool *booleans[] = {&s.broadcast_enabled, &s.passive_broadcast_enabled, &s.simple_ui,
            &s.install_enabled, &s.manual_install_enabled, &s.auto_install_enabled, &s.allow_ui_scale};
        if (id == 0 || id == 1) {
            ok = number_value(f, &number) && number >= 1 && number <= 65535 && number == (int)number;
            if (ok) { if (id == 0) s.http_port = (int)number; else { s.broadcast_port = (int)number; has_udp = true; } }
        } else if (id >= 2 && id <= 8) ok = bool_value(f, booleans[id - 2]);
        else if (id == 9) {
            ok = number_value(f, &number) && number >= .45 && number <= 1;
            if (ok) s.ui_scale_percent = (int)(number * 100 + .5);
        } else if (id == 10) {
            char mode[64];
            ok = assistant_json_decode_string(f->value, mode, sizeof(mode));
            if (ok) {
                simple_mode = !strcasecmp(mode, "simple") || !strcasecmp(mode, "fallback");
                ok = simple_mode || !strcasecmp(mode, "full") || !strcasecmp(mode, "dashboard") || !strcasecmp(mode, "advanced");
                has_mode = true;
            }
        } else if (id == 11) {
            ok = number_value(f, &number) && number == 1;
            s.managed = ok;
        }
    }
    free(fields);
    if (!has_udp) s.broadcast_port = s.http_port;
    if (has_mode) s.simple_ui = simple_mode;
    if (!ok || !assistant_settings_valid(&s)) return false;
    *settings = s;
    return true;
}
static bool root_path(const char *root, const char *relative, char *path, size_t size)
{
    if (!root || !*root) return false;
    const char *separator = root[strlen(root) - 1] == '/' ? "" : "/";
    int n = snprintf(path, size, "%s%s%s", root, separator, relative);
    return n > 0 && n < (int)size;
}
static bool config_path(const char *root, char *path, size_t size)
{
    return root_path(root, "wiiu/apps/uinjectforge-assistant/config.json", path, size);
}
static bool ensure_config_directory(const char *root)
{
    const char *suffixes[] = {"wiiu", "wiiu/apps", "wiiu/apps/uinjectforge-assistant"};
    char path[1024];
    for (unsigned i = 0; i < 3; i++) {
        if (!root_path(root, suffixes[i], path, sizeof(path))) return false;
        if (mkdir(path, 0777) != 0 && errno != EEXIST) return false;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    }
    return true;
}
static bool read_json(const char *path, char *json, bool *exists)
{
    FILE *file = fopen(path, "rb");
    *exists = file != NULL;
    if (!file) { strcpy(json, "{}"); return errno == ENOENT; }
    size_t n = fread(json, 1, 8191, file);
    bool ok = !ferror(file) && fgetc(file) == EOF && !memchr(json, 0, n);
    if (fclose(file) != 0) ok = false;
    json[n] = 0;
    return ok;
}
static bool read_config(const char *path, char *json, bool *exists, bool *recovered)
{
    *recovered = false;
    if (!read_json(path, json, exists)) return false;
    if (*exists) return true;
    char backup[1032];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    /* A shutdown between the two FAT renames must not lose the last settings. */
    return read_json(backup, json, recovered);
}
bool assistant_settings_load(const char *root, AssistantSettings *settings, char *error, size_t size)
{
    char path[1024], json[8192];
    bool exists, recovered;
    if (!config_path(root, path, sizeof(path)) || !read_config(path, json, &exists, &recovered) || !assistant_settings_parse(json, settings)) {
        snprintf(error, size, "Settings could not be read. Existing configuration was not changed.");
        return false;
    }
    snprintf(error, size, "%s", recovered ? "Recovered previous settings from config.json.bak." : "");
    return true;
}
static bool save_config(const char *root, const AssistantSettings *s, const unsigned int *pairing_code, char *error, size_t size)
{
    char path[1024], temporary[1032], backup[1032], json[8192];
    bool exists = false, recovered;
    AssistantSettings old;
    if (!assistant_settings_valid(s) || !config_path(root, path, sizeof(path))
        || !read_config(path, json, &exists, &recovered) || !assistant_settings_parse(json, &old)) {
        snprintf(error, size, "Invalid or unreadable settings. Existing configuration was not changed.");
        return false;
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    AssistantJsonField *fields = calloc(64, sizeof(*fields));
    size_t count = 0;
    if (!fields || !assistant_json_object(json, fields, 64, &count)) {
        free(fields); snprintf(error, size, "Could not prepare settings. No changes applied."); return false;
    }
    if (!ensure_config_directory(root)) {
        free(fields); snprintf(error, size, "Cannot create the settings folder on SD. No changes applied."); return false;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) { free(fields); snprintf(error, size, "Cannot write settings to SD. No changes applied."); return false; }
    bool ok = fputs("{\n", file) >= 0;
    for (size_t i = 0; ok && i < count; i++) if (known_field(fields[i].name) < 0
        && (!pairing_code || strcmp(fields[i].name, "pairingCode"))) {
        ok = fwrite(fields[i].start, 1, fields[i].length, file) == fields[i].length && fputs(",\n", file) >= 0;
    }
    free(fields);
    if (pairing_code) ok = ok && fprintf(file, "\"pairingCode\":\"%06u\",\n", *pairing_code) > 0;
    const char *yes = "true", *no = "false";
    ok = ok && fprintf(file,
        "\"httpPort\":%d,\n\"broadcastPort\":%d,\n\"broadcastEnabled\":%s,\n\"passiveBroadcastEnabled\":%s,\n"
        "\"simpleUi\":%s,\n\"uiMode\":\"%s\",\n\"installEnabled\":%s,\n\"manualInstallEnabled\":%s,\n"
        "\"autoInstallEnabled\":%s,\n\"allowUiScale\":%s,\n\"uiScale\":%.2f,\n\"settingsSchemaVersion\":1\n}\n",
        s->http_port, s->broadcast_port, s->broadcast_enabled ? yes : no, s->passive_broadcast_enabled ? yes : no,
        s->simple_ui ? yes : no, s->simple_ui ? "simple" : "full", s->install_enabled ? yes : no,
        s->manual_install_enabled ? yes : no, s->auto_install_enabled ? yes : no, s->allow_ui_scale ? yes : no,
        s->ui_scale_percent / 100.0) > 0;
    ok = ok && fflush(file) == 0;
    if (fclose(file) != 0) ok = false;
    bool temporary_exists;
    AssistantSettings check;
    ok = ok && read_json(temporary, json, &temporary_exists) && assistant_settings_parse(json, &check);
    if (!ok) { unlink(temporary); snprintf(error, size, "Could not finish saving settings. No changes applied."); return false; }
    /* FAT rename cannot overwrite. Keep the prior file as a recoverable backup. */
    if (exists && ((access(backup, F_OK) == 0 && unlink(backup) != 0) || rename(path, backup) != 0)) {
        unlink(temporary); snprintf(error, size, "Cannot back up settings. No changes applied."); return false;
    }
    if (rename(temporary, path) != 0) {
        if (exists) rename(backup, path);
        unlink(temporary); snprintf(error, size, "Cannot publish settings. Previous settings retained in config.json.bak."); return false;
    }
    return true;
}

bool assistant_settings_save(const char *root, const AssistantSettings *s, char *error, size_t size)
{
    return save_config(root, s, NULL, error, size);
}

bool assistant_settings_reset_pairing_code(const char *root, const AssistantSettings *s,
    unsigned int code, char *error, size_t size)
{
    if (code > 999999) {
        snprintf(error, size, "Pairing code must contain six digits.");
        return false;
    }
    return save_config(root, s, &code, error, size);
}

bool assistant_settings_pairing_code(const char *root, const AssistantSettings *s,
    unsigned int generated, unsigned int *code, char *error, size_t size)
{
    char path[1024], json[8192];
    bool exists, recovered;
    AssistantSettings check;
    if (!code || !config_path(root, path, sizeof(path)) || !read_config(path, json, &exists, &recovered)
        || !assistant_settings_parse(json, &check)) {
        snprintf(error, size, "Cannot read pairing configuration. Existing configuration was not changed.");
        return false;
    }
    AssistantJsonField *fields = calloc(64, sizeof(*fields));
    size_t count = 0;
    bool ok = fields && assistant_json_object(json, fields, 64, &count), found = false;
    unsigned int saved = 0;
    for (size_t i = 0; ok && i < count; i++) if (!strcmp(fields[i].name, "pairingCode")) {
        char digits[16];
        found = true;
        ok = assistant_json_decode_string(fields[i].value, digits, sizeof(digits)) && strlen(digits) == 6;
        for (size_t d = 0; ok && d < 6; d++) {
            ok = digits[d] >= '0' && digits[d] <= '9';
            if (ok) saved = saved * 10 + (unsigned)(digits[d] - '0');
        }
    }
    free(fields);
    if (!ok) {
        snprintf(error, size, "Invalid pairing configuration. Existing configuration was not changed.");
        return false;
    }
    /* Publish recovered backups before accepting their code as persistent. */
    if ((!found || recovered) && !assistant_settings_reset_pairing_code(root, s,
        found ? saved : generated, error, size)) return false;
    *code = found ? saved : generated;
    snprintf(error, size, "%s", "");
    return true;
}

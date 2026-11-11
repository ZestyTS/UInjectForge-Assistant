#include "assistant_test_temp.h"
#include "assistant_settings.h"
#include "assistant_json.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(fclose(f) == 0);
}
static void read_text(const char *path, char *text, size_t capacity)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(text, 1, capacity - 1, f);
    text[n] = 0;
    assert(!ferror(f) && fgetc(f) == EOF && fclose(f) == 0);
}
int main(void)
{
    AssistantSettings s = assistant_settings_defaults(), original = s;
    assert(assistant_settings_valid(&s));
    assert(s.http_port == 8573 && s.broadcast_port == 8573 && !s.auto_install_enabled);
    assert(assistant_settings_parse("{\"httpPort\":2345,\"uiMode\":\"FULL\",\"simpleUi\":true}", &s));
    assert(s.http_port == 2345 && s.broadcast_port == 2345 && !s.simple_ui);
    assert(assistant_settings_parse("{\"httpPort\":65535,\"broadcastPort\":1,\"uiScale\":0.45}", &s));
    assert(s.broadcast_port == 1 && s.ui_scale_percent == 45);
    assert(assistant_settings_parse("{\"uiMode\":\"fallback\"}", &s) && s.simple_ui);
    assert(assistant_settings_parse("{\"uiMode\":\"advanced\"}", &s) && !s.simple_ui);
    assert(assistant_settings_parse("{\"uiMode\":\"dashboard\"}", &s) && !s.simple_ui);
    assert(assistant_settings_parse("{\"future\":{\"httpPort\":4,\"list\":[null,1.25e-3,{},\"\\\"}\"]}}", &s));
    assert(s.http_port == 8573);
    const char *bad[] = {"", "[]", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{}x", "{\"a\":[1,]}",
        "{\"httpPort\":0}", "{\"httpPort\":65536}", "{\"httpPort\":1.5}", "{\"httpPort\":\"80\"}",
        "{\"httpPort\":01}", "{\"httpPort\":+1}", "{\"httpPort\":1e999}", "{\"httpPort\":1.}",
        "{\"broadcastPort\":-1}", "{\"uiScale\":0.44}", "{\"uiScale\":1.01}", "{\"uiScale\":null}",
        "{\"installEnabled\":1}", "{\"autoInstallEnabled\":\"true\"}", "{\"uiMode\":\"bad\"}",
        "{\"httpPort\":4,\"http\\u0050ort\":5}", "{\"settingsSchemaVersion\":2}", "{\"a\":tru}",
        "{\"a\":\"\\uD800\"}", "{\"a\":\"\\q\"}", "{\"a\":falsex}", "{\"a\":\"unterminated}"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        s = original;
        assert(!assistant_settings_parse(bad[i], &s));
        assert(s.http_port == original.http_port && s.install_enabled == original.install_enabled);
    }
    /* Every truncated prefix must remain bounded, including escapes and containers. */
    const char *complete = "{\"future\":[{\"a\":\"\\uD83D\\uDE00\"},true,null,-1.2e+3],\"httpPort\":8000}";
    char prefix[256];
    for (size_t n = 0; n < strlen(complete); n++) {
        memcpy(prefix, complete, n); prefix[n] = 0;
        assert(!assistant_settings_parse(prefix, &s));
    }
    assert(assistant_settings_parse(complete, &s));
    char deep[256] = "{\"nested\":";
    for (int i = 0; i < 20; i++) strcat(deep, "[");
    strcat(deep, "0");
    for (int i = 0; i < 20; i++) strcat(deep, "]");
    strcat(deep, "}");
    assert(!assistant_settings_parse(deep, &s));
    char oversized[8193]; memset(oversized, ' ', sizeof(oversized)); oversized[8192] = 0;
    assert(!assistant_settings_parse(oversized, &s));

    char root[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-settings"));
    char path[1024], backup[2048], temporary[2048], error[256] = {0}, text[8192], unchanged[8192];
    snprintf(path, sizeof(path), "%s/wiiu/apps/uinjectforge-assistant/config.json", root);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    assert(assistant_settings_load(root, &s, error, sizeof(error)) && !s.managed);
    unsigned int code = 999999;
    assert(assistant_settings_pairing_code(root, &s, 123456, &code, error, sizeof(error)) && code == 123456);
    read_text(path, text, sizeof(text)); assert(strstr(text, "\"pairingCode\":\"123456\""));
    assert(assistant_settings_pairing_code(root, &s, 654321, &code, error, sizeof(error)) && code == 123456);
    assert(assistant_settings_reset_pairing_code(root, &s, 42, error, sizeof(error)));
    assert(assistant_settings_pairing_code(root, &s, 654321, &code, error, sizeof(error)) && code == 42);
    read_text(path, text, sizeof(text)); assert(strstr(text, "\"pairingCode\":\"000042\""));
    assert(!assistant_settings_reset_pairing_code(root, &s, 1000000, error, sizeof(error)));
    const char *bad_codes[] = {"123456", "null", "true", "\"12345\"", "\"1234567\"", "\"12345x\"", "\" 12345\"", "\"-12345\""};
    for (size_t i = 0; i < sizeof(bad_codes) / sizeof(bad_codes[0]); i++) {
        snprintf(text, sizeof(text), "{\"pairingCode\":%s,\"future\":{\"keep\":true}}", bad_codes[i]);
        write_text(path, text);
        code = 42;
        assert(!assistant_settings_pairing_code(root, &s, 123456, &code, error, sizeof(error)) && code == 42);
        read_text(path, unchanged, sizeof(unchanged)); assert(!strcmp(text, unchanged));
    }
    write_text(path, "{\"pairingCode\":\"000042\",\"future\":{\"keep\":true}}");
    s.auto_install_enabled = true;
    s.http_port = 9000; s.broadcast_port = 9001; s.broadcast_enabled = false;
    s.passive_broadcast_enabled = true; s.simple_ui = false;
    s.install_enabled = false; s.manual_install_enabled = false;
    s.allow_ui_scale = true; s.ui_scale_percent = 73;
    assert(assistant_settings_save(root, &s, error, sizeof(error)));
    assert(assistant_settings_pairing_code(root, &s, 654321, &code, error, sizeof(error)) && code == 42);
    read_text(path, text, sizeof(text)); assert(strstr(text, "\"future\":{\"keep\":true}"));
    assert(unlink(path) == 0);
    assert(assistant_settings_pairing_code(root, &s, 654321, &code, error, sizeof(error)) && code == 42);
    assert(access(path, F_OK) == 0);
    AssistantSettings loaded;
    assert(assistant_settings_load(root, &loaded, error, sizeof(error)));
    assert(loaded.managed && loaded.auto_install_enabled && !loaded.install_enabled && !loaded.manual_install_enabled);
    assert(loaded.http_port == 9000 && loaded.broadcast_port == 9001 && !loaded.broadcast_enabled && loaded.passive_broadcast_enabled);
    assert(!loaded.simple_ui && loaded.allow_ui_scale && loaded.ui_scale_percent == 73);
    char root_with_slash[1024]; snprintf(root_with_slash, sizeof(root_with_slash), "%s/", root);
    assert(assistant_settings_load(root_with_slash, &loaded, error, sizeof(error)) && loaded.http_port == 9000);
    assert(!assistant_settings_save("", &s, error, sizeof(error)));
    assert(!assistant_settings_network_changed(&s, &loaded));
    loaded.ui_scale_percent = 80;
    assert(!assistant_settings_network_changed(&s, &loaded));
    loaded.broadcast_enabled = true;
    assert(assistant_settings_network_changed(&s, &loaded));
    const char *old = "{\"future\":{\"array\":[1,true,null],\"text\":\"brace } and \\\"\"},\"httpPort\":1234}";
    write_text(path, old);
    assert(assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(backup, text, sizeof(text)); assert(!strcmp(text, old));
    read_text(path, text, sizeof(text)); assert(strstr(text, "\"future\":{\"array\":[1,true,null],\"text\":\"brace } and \\\"\"}"));
    assert(access(temporary, F_OK) != 0);
    assert(unlink(path) == 0);
    assert(assistant_settings_load(root, &loaded, error, sizeof(error)) && loaded.http_port == 1234);
    assert(strstr(error, "Recovered"));
    assert(assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(path, text, sizeof(text)); assert(strstr(text, "\"future\""));
    read_text(backup, unchanged, sizeof(unchanged)); assert(!strcmp(unchanged, old));
    s.http_port = 0;
    assert(!assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(path, unchanged, sizeof(unchanged)); assert(!strcmp(text, unchanged));
    s.http_port = 9000;
    write_text(path, "{invalid}");
    assert(!assistant_settings_load(root, &loaded, error, sizeof(error)));
    assert(!assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(path, text, sizeof(text)); assert(!strcmp(text, "{invalid}"));
    write_text(path, "{}");
    assert(mkdir(temporary, 0700) == 0);
    code = 42;
    assert(!assistant_settings_pairing_code(root, &s, 654321, &code, error, sizeof(error)) && code == 42);
    assert(!assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(path, text, sizeof(text)); assert(!strcmp(text, "{}"));
    assert(rmdir(temporary) == 0);
    assert(unlink(backup) == 0 && mkdir(backup, 0700) == 0);
    assert(!assistant_settings_save(root, &s, error, sizeof(error)));
    read_text(path, text, sizeof(text)); assert(!strcmp(text, "{}"));
    assert(access(temporary, F_OK) != 0);
    assert(rmdir(backup) == 0);
    FILE *f = fopen(path, "wb"); assert(f);
    const char with_null[] = "{}\0garbage";
    assert(fwrite(with_null, 1, sizeof(with_null), f) == sizeof(with_null) && fclose(f) == 0);
    assert(!assistant_settings_load(root, &loaded, error, sizeof(error)));
    assert(unlink(path) == 0);
    char dir[1024]; snprintf(dir, sizeof(dir), "%s/wiiu/apps/uinjectforge-assistant", root); assert(rmdir(dir) == 0);
    snprintf(dir, sizeof(dir), "%s/wiiu/apps", root); assert(rmdir(dir) == 0);
    snprintf(dir, sizeof(dir), "%s/wiiu", root); assert(rmdir(dir) == 0);
    assert(rmdir(root) == 0);
    puts("Assistant settings parsing, preservation, persistence and failure tests passed.");
    return 0;
}

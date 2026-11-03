#ifndef ASSISTANT_SETTINGS_H
#define ASSISTANT_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int http_port, broadcast_port;
    bool broadcast_enabled, passive_broadcast_enabled, simple_ui;
    bool install_enabled, manual_install_enabled, auto_install_enabled;
    bool allow_ui_scale;
    int ui_scale_percent;
    bool managed;
} AssistantSettings;

#ifdef __cplusplus
extern "C" {
#endif
AssistantSettings assistant_settings_defaults(void);
bool assistant_settings_valid(const AssistantSettings *settings);
bool assistant_settings_network_changed(const AssistantSettings *a, const AssistantSettings *b);
bool assistant_settings_parse(const char *json, AssistantSettings *settings);
bool assistant_settings_load(const char *sd_root, AssistantSettings *settings, char *error, size_t error_size);
bool assistant_settings_save(const char *sd_root, const AssistantSettings *settings, char *error, size_t error_size);
bool assistant_settings_pairing_code(const char *sd_root, const AssistantSettings *settings,
    unsigned int generated_code, unsigned int *code, char *error, size_t error_size);
bool assistant_settings_reset_pairing_code(const char *sd_root, const AssistantSettings *settings,
    unsigned int code, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif

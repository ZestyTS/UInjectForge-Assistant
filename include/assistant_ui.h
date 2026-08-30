#ifndef ASSISTANT_UI_H
#define ASSISTANT_UI_H

#include <stdbool.h>

#define ASSISTANT_UI_ACTIVITY_LINES 8
#define ASSISTANT_UI_LOG_LINES 10
#define ASSISTANT_UI_TEXT 256

typedef struct
{
    char connection_state[64];
    char ip_address[64];
    char component_version[64];
    char build_id[64];
    char sd_root[256];
    char status[ASSISTANT_UI_TEXT];
    char last_http_request[128];
    char last_http_error[ASSISTANT_UI_TEXT];
    char last_discovery_peer[128];
    char controller_summary[ASSISTANT_UI_TEXT];
    char log_path[256];
    char activity_log[ASSISTANT_UI_ACTIVITY_LINES][ASSISTANT_UI_TEXT];
    char log_tail[ASSISTANT_UI_LOG_LINES][ASSISTANT_UI_TEXT];
    unsigned int activity_count;
    unsigned int log_tail_count;
    unsigned int pairing_code;
    unsigned int http_request_count;
    unsigned int http_error_count;
    unsigned int pairing_attempt_count;
    unsigned int pairing_failure_count;
    unsigned int discovery_probe_count;
    unsigned int discovery_reply_count;
    unsigned int discovery_broadcast_count;
    unsigned int loop_heartbeat_count;
    int http_port;
    int broadcast_port;
    bool paired;
    bool transfer_active;
    bool broadcast_enabled;
    bool passive_broadcast_enabled;
    bool broadcast_socket_ready;
    bool simple_ui;
    bool install_enabled;
    bool manual_install_enabled;
    bool auto_install_enabled;
    bool install_active;
    long long install_done_bytes;
    long long install_total_bytes;
    bool sd_space_known;
    long long sd_free_bytes;
    long long sd_total_bytes;
    bool transfer_fits_sd;
    float ui_scale;
    long long transfer_required_bytes;
    char package_display_name[ASSISTANT_UI_TEXT];
    char package_kind[64];
    char transfer_id[64];
    char target_root_kind[64];
    char target_root[256];
    char post_transfer_action[64];
    int file_count;
    int uploaded_files;
    long long total_bytes;
    long long uploaded_bytes;
    bool last_transfer_has_result;
    bool last_transfer_success;
    bool last_transfer_canceled;
    char last_transfer_status[32];
    char last_transfer_id[64];
    char last_transfer_package_display_name[ASSISTANT_UI_TEXT];
    char last_transfer_package_kind[64];
    char last_transfer_target_root[256];
    char last_transfer_post_transfer_action[64];
    char last_transfer_message[ASSISTANT_UI_TEXT];
    char last_transfer_failed_file[256];
    int last_transfer_file_count;
    int last_transfer_uploaded_files;
    long long last_transfer_total_bytes;
    long long last_transfer_uploaded_bytes;
} AssistantUiModel;

typedef enum
{
    ASSISTANT_UI_ACTION_NONE = 0,
    ASSISTANT_UI_ACTION_INSTALL_TO_NAND,
    ASSISTANT_UI_ACTION_INSTALL_TO_USB,
    ASSISTANT_UI_ACTION_LAUNCH_TITLE,
} AssistantUiActionType;

typedef struct
{
    AssistantUiActionType type;
    char package_name[256];
    char title_id[17];
    char title_name[256];
} AssistantUiAction;

#ifdef __cplusplus
extern "C" {
#endif

bool assistant_ui_init(void);
void assistant_ui_shutdown(void);
void assistant_ui_render(const AssistantUiModel *model);
bool assistant_ui_poll_input(void);
bool assistant_ui_take_refresh_request(void);
bool assistant_ui_take_storage_refresh_request(void);
bool assistant_ui_take_action(AssistantUiAction *action);
void assistant_ui_require_system_memory(const char *package_name);
void assistant_ui_set_install_active(bool active);
void assistant_ui_invalidate_storage(void);
bool assistant_ui_needs_log_tail(void);

#ifdef __cplusplus
}
#endif

#endif

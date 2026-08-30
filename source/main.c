#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mbedtls/sha256.h>
#include <netinet/in.h>
#include <nn/ac/ac_c.h>
#include <nn/result.h>
#include <nsysnet/_socket.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/screen.h>
#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <padscore/kpad.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include "assistant_ui.h"
#include "assistant_security.h"
#include "assistant_telemetry.h"
#include "assistant_storage.h"

#define UIF_ASSISTANT_PROTOCOL "uif-assistant-transfer-v1"
#ifndef UIF_COMPONENT_VERSION
#define UIF_COMPONENT_VERSION "1.0.0-alpha"
#endif
#ifndef UIF_COMPONENT_BUILD_ID
#define UIF_COMPONENT_BUILD_ID "dev"
#endif
#define UIF_ASSISTANT_CAPABILITIES_JSON "[\"pairing\",\"transfer-v1\",\"manual-install\",\"auto-install\",\"installed-titles\",\"console-events\",\"title-launch\",\"setup-check\"]"
#define UIF_ASSISTANT_PORT 8573
#define UIF_MAX_FILES 768
#define UIF_MAX_PATH 768
#define UIF_MAX_TEXT 256
#define UIF_MAX_HEADER 65536
#define UIF_MAX_MANIFEST (1024 * 1024)
#define UIF_IO_BUFFER 32768
#define UIF_LOG_TAIL_BYTES 16384
#define UIF_ACTIVITY_LINES 8
#define UIF_MAX_SETUP_PACKAGES 512
#define UIF_MCP_COMMAND_INSTALL_ASYNC 0x81
#define UIF_MCP_INSTALL_PATH_LENGTH 0x27F
#define UIF_INSTALL_STALL_TIMEOUT_MS (15U * 60U * 1000U)
#define UIF_INSTALL_MAX_RUNTIME_MS (90U * 60U * 1000U)
#define UIF_UPLOAD_IDLE_TIMEOUT_MS (30U * 1000U)
#define UIF_UPLOAD_RENDER_INTERVAL_MS 500U
#define UIF_STATUS_JSON_BUFFER 32768U
#define UIF_COLOR_BACKGROUND 0x090A0F00
#define UIF_COLOR_PANEL 0x16162200
#define UIF_COLOR_PANEL_ALT 0x20202900
#define UIF_COLOR_BORDER 0x5B3A8F00
#define UIF_COLOR_ACCENT 0x7C4DFF00
#define UIF_COLOR_READY 0x1E8E4D00
#define UIF_COLOR_WARN 0xB7791F00
#define UIF_COLOR_DANGER 0x8B1E3F00
#define UIF_COLOR_MUTED 0x3A3A4600
#define UIF_ASSISTANT_DEFAULT_UI_SCALE 1.00f

static volatile bool g_procui_exit_requested = false;
static volatile bool g_install_completed = false;
static volatile IOSError g_install_error = 0;
static unsigned int g_log_revision = 0;
static unsigned int g_storage_revision = 0;

static void install_callback(IOSError error_code, void *context)
{
    (void)context;
    g_install_error = error_code;
    __sync_synchronize();
    g_install_completed = true;
}

typedef struct
{
    char relative_path[UIF_MAX_PATH];
    char target_path[UIF_MAX_PATH];
    char sha256[65];
    long long size_bytes;
    bool uploaded;
    bool had_existing_target;
    bool committed;
    char staged_path[UIF_MAX_PATH];
} AssistantFile;

typedef struct
{
    bool active;
    bool manifest_loaded;
    char transfer_id[64];
    char package_display_name[UIF_MAX_TEXT];
    char package_kind[64];
    char target_root_kind[64];
    char target_root[UIF_MAX_PATH];
    char post_transfer_action[64];
    char install_target[16];
    bool system_memory_install_required;
    long long total_bytes;
    int file_count;
    int uploaded_files;
    long long uploaded_bytes;
    char incoming_root[UIF_MAX_PATH];
    AssistantFile files[UIF_MAX_FILES];
} AssistantTransfer;

typedef struct
{
    bool has_result;
    bool success;
    bool canceled;
    char status[32];
    char transfer_id[64];
    char package_display_name[UIF_MAX_TEXT];
    char package_kind[64];
    char target_root[UIF_MAX_PATH];
    char post_transfer_action[64];
    char message[UIF_MAX_TEXT];
    char failed_file[UIF_MAX_PATH];
    int file_count;
    int uploaded_files;
    long long total_bytes;
    long long uploaded_bytes;
    uint64_t completed_ticks;
} AssistantTransferResult;

typedef struct
{
    char method[12];
    char path[UIF_MAX_PATH];
    char authorization[UIF_MAX_TEXT];
    char x_sha256[65];
    long long x_size;
    long long content_length;
    bool expect_continue;
    char body_prefix[UIF_MAX_HEADER];
    size_t body_prefix_len;
} HttpRequest;

typedef struct
{
    unsigned int pairing_code;
    char access_token[96];
    bool paired;
    char sd_root[UIF_MAX_PATH];
    char ip_address[64];
    char status[UIF_MAX_TEXT];
    int http_port;
    bool broadcast_enabled;
    bool passive_broadcast_enabled;
    bool simple_ui;
    bool install_enabled;
    bool manual_install_enabled;
    bool auto_install_enabled;
    float ui_scale;
    int broadcast_port;
    int server_fd;
    int broadcast_fd;
    uint64_t last_broadcast_ticks;
    unsigned int discovery_probe_count;
    unsigned int discovery_reply_count;
    unsigned int discovery_broadcast_count;
    unsigned int loop_heartbeat_count;
    char last_discovery_peer[64];
    unsigned int http_request_count;
    unsigned int http_error_count;
    unsigned int pairing_attempt_count;
    unsigned int pairing_failure_count;
    unsigned int pairing_failure_streak;
    uint64_t pairing_retry_after_ticks;
    char last_http_request[128];
    char last_http_error[UIF_MAX_TEXT];
    char activity_log[UIF_ACTIVITY_LINES][UIF_MAX_TEXT];
    unsigned int activity_count;
    AssistantTransfer transfer;
    AssistantTransferResult last_transfer_result;
    bool receiving_file;
    char receiving_file_path[UIF_MAX_PATH];
    long long receiving_file_bytes;
    long long receiving_file_total_bytes;
    bool auto_install_pending;
    bool install_active;
    long long install_done_bytes;
    long long install_total_bytes;
    AssistantUiAction auto_install_action;
    bool title_launch_pending;
    uint64_t pending_title_id;
    char pending_title_name[UIF_MAX_TEXT];
    char pending_launch_event_id[64];
} AssistantState;

typedef struct
{
    bool sd_accessible;
    bool sd_space_known;
    long long sd_free_bytes;
    bool assistant_installed;
    bool aroma_bridge_installed;
    bool ftp_plugin_installed;
    bool nintendont_installed;
    bool noods_installed;
    bool ppsspp_runtime_installed;
    bool ppsspp_assets_complete;
    bool wiistation_installed;
    bool not64_installed;
    bool genesis_plus_gx_installed;
    bool controller_config_installed;
    bool loadiine_folder_present;
    bool install_folder_present;
    int valid_install_packages;
    int invalid_install_packages;
    bool install_scan_truncated;
} AssistantSetupReadiness;

static void render_assistant_screen(const AssistantState *state);
static void join_path(char *destination, size_t destination_size, const char *root, const char *relative);
static void build_controller_summary(char *summary, size_t summary_size);
static bool mkdir_recursive(const char *path);
static void append_persistent_activity_log(const AssistantState *state, const char *message);
static unsigned int read_persistent_log_tail(const AssistantState *state, char lines[][ASSISTANT_UI_TEXT], unsigned int max_lines);
static bool get_sd_space(const AssistantState *state, long long *free_bytes, long long *total_bytes);
static bool get_cached_sd_space(const AssistantState *state, long long *free_bytes, long long *total_bytes);
static AssistantSetupReadiness inspect_setup_readiness(const AssistantState *state);
static void build_setup_json(const AssistantState *state, char *json, size_t json_size);
static void handle_discovery_probe(AssistantState *state);
static void send_discovery_broadcast(AssistantState *state);

static void copy_text(char *destination, size_t destination_size, const char *source)
{
    if (!destination || destination_size == 0)
        return;

    if (!source)
        source = "";

    size_t i = 0;
    for (; i + 1 < destination_size && source[i]; i++)
        destination[i] = source[i];
    destination[i] = '\0';
}

static void copy_display_name(char *destination, size_t destination_size, const char *source)
{
    if (!destination || destination_size == 0)
        return;

    copy_text(destination, destination_size, source);
    size_t length = strlen(destination);
    if (length >= destination_size - 1 && destination_size > 4)
    {
        destination[destination_size - 4] = '.';
        destination[destination_size - 3] = '.';
        destination[destination_size - 2] = '.';
        destination[destination_size - 1] = '\0';
    }
}

static void append_activity(AssistantState *state, const char *message)
{
    if (!state || !message || !message[0])
        return;

    append_persistent_activity_log(state, message);

    if (state->activity_count < UIF_ACTIVITY_LINES)
    {
        copy_text(state->activity_log[state->activity_count], sizeof(state->activity_log[state->activity_count]), message);
        state->activity_count++;
        return;
    }

    for (int i = 1; i < UIF_ACTIVITY_LINES; i++)
        copy_text(state->activity_log[i - 1], sizeof(state->activity_log[i - 1]), state->activity_log[i]);
    copy_text(state->activity_log[UIF_ACTIVITY_LINES - 1], sizeof(state->activity_log[UIF_ACTIVITY_LINES - 1]), message);
}

static void set_status(AssistantState *state, const char *message)
{
    copy_text(state->status, sizeof(state->status), message);
    append_activity(state, state->status);
    WHBLogPrintf("%s", state->status);
    render_assistant_screen(state);
}

static void set_http_error(AssistantState *state, const char *message)
{
    state->http_error_count++;
    copy_text(state->last_http_error, sizeof(state->last_http_error), message);
    set_status(state, message);
}

static unsigned int g_pairing_generation = 0;

static bool create_pairing_digest(const AssistantState *state, const char *purpose, unsigned char digest[32])
{
    volatile uint32_t stack_marker = 0x55494641u;
    struct
    {
        uint64_t time;
        uint64_t system_time;
        uintptr_t state_address;
        uintptr_t thread_address;
        uintptr_t stack_address;
        uint32_t generation;
        unsigned int http_requests;
        unsigned int pairing_attempts;
    } entropy = {
        .time = OSGetTime(),
        .system_time = OSGetSystemTime(),
        .state_address = (uintptr_t)state,
        .thread_address = (uintptr_t)OSGetCurrentThread(),
        .stack_address = (uintptr_t)&stack_marker,
        .generation = ++g_pairing_generation,
        .http_requests = state ? state->http_request_count : 0,
        .pairing_attempts = state ? state->pairing_attempt_count : 0,
    };

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool success = mbedtls_sha256_starts_ret(&sha, 0) == 0
        && mbedtls_sha256_update_ret(&sha, (const unsigned char *)&entropy, sizeof(entropy)) == 0
        && mbedtls_sha256_update_ret(&sha, (const unsigned char *)purpose, strlen(purpose)) == 0;
    if (success && state && state->ip_address[0])
        success = mbedtls_sha256_update_ret(&sha, (const unsigned char *)state->ip_address, strlen(state->ip_address)) == 0;
    if (success)
        success = mbedtls_sha256_finish_ret(&sha, digest) == 0;
    mbedtls_sha256_free(&sha);
    return success;
}

static unsigned int create_pairing_code(const AssistantState *state)
{
    unsigned char digest[32];
    uint32_t value = (uint32_t)(OSGetTime() & 0xFFFFFFFFu);
    if (create_pairing_digest(state, "pairing-code", digest))
        memcpy(&value, digest, sizeof(value));
    return (value % 900000U) + 100000U;
}

static void create_access_token(AssistantState *state)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    if (!create_pairing_digest(state, "access-token", digest))
    {
        snprintf(state->access_token, sizeof(state->access_token), "%08X%08X%08X%08X",
            state->pairing_code,
            (unsigned int)(OSGetTime() & 0xFFFFFFFFu),
            state->http_request_count,
            state->pairing_attempt_count);
        return;
    }

    for (int i = 0; i < 16; i++)
    {
        state->access_token[i * 2] = hex[(digest[i] >> 4) & 0x0F];
        state->access_token[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    state->access_token[32] = '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool is_hex_sha256(const char *value)
{
    if (!value || strlen(value) != 64)
        return false;

    for (int i = 0; i < 64; i++)
    {
        if (!isxdigit((unsigned char)value[i]))
            return false;
    }

    return true;
}

static bool equals_ignore_case(const char *left, const char *right)
{
    if (!left || !right)
        return false;

    while (*left && *right)
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static void json_escape(const char *source, char *destination, size_t destination_size)
{
    if (!destination || destination_size == 0)
        return;

    size_t out = 0;
    for (size_t i = 0; source && source[i] && out + 1 < destination_size; i++)
    {
        unsigned char c = (unsigned char)source[i];
        const char *escape = NULL;
        if (c == '"')
            escape = "\\\"";
        else if (c == '\\')
            escape = "\\\\";
        else if (c == '\n')
            escape = "\\n";
        else if (c == '\r')
            escape = "\\r";
        else if (c == '\t')
            escape = "\\t";

        if (escape)
        {
            for (size_t e = 0; escape[e] && out + 1 < destination_size; e++)
                destination[out++] = escape[e];
        }
        else if (c >= 0x20)
        {
            destination[out++] = (char)c;
        }
    }

    destination[out] = '\0';
}

static bool starts_with_ignore_case(const char *value, const char *prefix)
{
    if (!value || !prefix)
        return false;

    while (*prefix)
    {
        if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return false;
        value++;
        prefix++;
    }

    return true;
}

static bool ends_with_ignore_case(const char *value, const char *suffix)
{
    if (!value || !suffix)
        return false;

    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length)
        return false;

    return strcasecmp(value + value_length - suffix_length, suffix) == 0;
}

static void append_format(char *destination, size_t destination_size, size_t *offset, const char *format, ...)
{
    if (!destination || !offset || *offset >= destination_size)
        return;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(destination + *offset, destination_size - *offset, format, args);
    va_end(args);

    if (written < 0)
        return;

    size_t remaining = destination_size - *offset;
    *offset += (size_t)written < remaining ? (size_t)written : remaining - 1;
}

static bool assistant_screen_init(void)
{
    return assistant_ui_init();
}

static void assistant_screen_shutdown(void)
{
    assistant_ui_shutdown();
}

static const char *connection_state_text(const AssistantState *state)
{
    if (!state)
        return "Starting";
    if (state->transfer.active)
        return "Transfer Active";
    if (state->paired)
        return "Paired";
    if (state->server_fd >= 0)
        return "Ready";
    return "Starting";
}

static void record_transfer_result(
    AssistantState *state,
    const char *status,
    bool success,
    bool canceled,
    const char *message,
    const char *failed_file)
{
    if (!state)
        return;

    AssistantTransferResult result;
    memset(&result, 0, sizeof(result));
    AssistantTransfer *transfer = &state->transfer;
    g_storage_revision++;
    result.has_result = true;
    result.success = success;
    result.canceled = canceled;
    result.completed_ticks = OSGetTime();
    copy_text(result.status, sizeof(result.status), status);
    copy_text(result.transfer_id, sizeof(result.transfer_id), transfer->transfer_id);
    copy_text(result.package_display_name, sizeof(result.package_display_name), transfer->package_display_name);
    copy_text(result.package_kind, sizeof(result.package_kind), transfer->package_kind);
    copy_text(result.target_root, sizeof(result.target_root), transfer->target_root);
    copy_text(result.post_transfer_action, sizeof(result.post_transfer_action), transfer->post_transfer_action);
    copy_text(result.message, sizeof(result.message), message);
    copy_text(result.failed_file, sizeof(result.failed_file), failed_file);
    result.file_count = transfer->file_count;
    result.uploaded_files = transfer->uploaded_files;
    result.total_bytes = transfer->total_bytes;
    result.uploaded_bytes = transfer->uploaded_bytes;
    state->last_transfer_result = result;

    AssistantTelemetryEvent event = {
        .event_type = "transfer-result",
        .source = "assistant",
        .game_name = transfer->package_display_name,
        .installed_path = transfer->target_root,
        .result = status,
        .message = message,
        .correlation_id = transfer->transfer_id,
    };
    assistant_telemetry_write_event(state->sd_root, &event, NULL);
}

static void render_assistant_screen(const AssistantState *state)
{
    if (!state)
        return;

    if (assistant_ui_take_storage_refresh_request())
    {
        g_storage_revision++;
        g_log_revision++;
    }

    AssistantUiModel model;
    memset(&model, 0, sizeof(model));
    copy_text(model.connection_state, sizeof(model.connection_state), connection_state_text(state));
    copy_text(model.ip_address, sizeof(model.ip_address), state->ip_address);
    copy_text(model.component_version, sizeof(model.component_version), UIF_COMPONENT_VERSION);
    copy_text(model.build_id, sizeof(model.build_id), UIF_COMPONENT_BUILD_ID);
    copy_text(model.sd_root, sizeof(model.sd_root), state->sd_root);
    copy_text(model.status, sizeof(model.status), state->status);
    copy_text(model.last_http_request, sizeof(model.last_http_request), state->last_http_request);
    copy_text(model.last_http_error, sizeof(model.last_http_error), state->last_http_error);
    copy_text(model.last_discovery_peer, sizeof(model.last_discovery_peer), state->last_discovery_peer);
    join_path(model.log_path, sizeof(model.log_path), state->sd_root, "uinjectforge/assistant/assistant.log");
    build_controller_summary(model.controller_summary, sizeof(model.controller_summary));
    model.simple_ui = state->simple_ui;
    model.install_enabled = state->install_enabled;
    model.manual_install_enabled = state->manual_install_enabled;
    model.auto_install_enabled = state->auto_install_enabled;
    model.ui_scale = state->ui_scale;
    model.install_active = state->install_active;
    model.install_done_bytes = state->install_done_bytes;
    model.install_total_bytes = state->install_total_bytes;
    model.sd_space_known = get_cached_sd_space(state, &model.sd_free_bytes, &model.sd_total_bytes);
    model.transfer_required_bytes = state->transfer.active
        ? state->transfer.total_bytes + (16LL * 1024LL * 1024LL)
        : 0;
    model.transfer_fits_sd = !state->transfer.active
        || !model.sd_space_known
        || model.sd_free_bytes >= model.transfer_required_bytes;
    model.activity_count = state->activity_count > ASSISTANT_UI_ACTIVITY_LINES
        ? ASSISTANT_UI_ACTIVITY_LINES
        : state->activity_count;
    for (unsigned int i = 0; i < model.activity_count; i++)
        copy_text(model.activity_log[i], sizeof(model.activity_log[i]), state->activity_log[i]);
    if (assistant_ui_needs_log_tail())
    {
        static char log_tail[ASSISTANT_UI_LOG_LINES][ASSISTANT_UI_TEXT];
        static unsigned int log_count = 0;
        static unsigned int log_revision = UINT_MAX;
        if (log_revision != g_log_revision)
        {
            log_count = read_persistent_log_tail(state, log_tail, ASSISTANT_UI_LOG_LINES);
            log_revision = g_log_revision;
        }
        model.log_tail_count = log_count;
        memcpy(model.log_tail, log_tail, sizeof(log_tail));
    }

    model.pairing_code = state->pairing_code;
    model.http_request_count = state->http_request_count;
    model.http_error_count = state->http_error_count;
    model.pairing_attempt_count = state->pairing_attempt_count;
    model.pairing_failure_count = state->pairing_failure_count;
    model.discovery_probe_count = state->discovery_probe_count;
    model.discovery_reply_count = state->discovery_reply_count;
    model.discovery_broadcast_count = state->discovery_broadcast_count;
    model.loop_heartbeat_count = state->loop_heartbeat_count;
    model.http_port = state->http_port;
    model.broadcast_port = state->broadcast_port;
    model.paired = state->paired;
    model.transfer_active = state->transfer.active;
    model.broadcast_enabled = state->broadcast_enabled;
    model.passive_broadcast_enabled = state->passive_broadcast_enabled;
    model.broadcast_socket_ready = state->broadcast_fd >= 0;
    copy_text(model.package_display_name, sizeof(model.package_display_name), state->transfer.package_display_name);
    copy_text(model.package_kind, sizeof(model.package_kind), state->transfer.package_kind);
    copy_text(model.transfer_id, sizeof(model.transfer_id), state->transfer.transfer_id);
    copy_text(model.target_root_kind, sizeof(model.target_root_kind), state->transfer.target_root_kind);
    copy_text(model.target_root, sizeof(model.target_root), state->transfer.target_root);
    copy_text(model.post_transfer_action, sizeof(model.post_transfer_action), state->transfer.post_transfer_action);
    model.file_count = state->transfer.file_count;
    model.uploaded_files = state->transfer.uploaded_files;
    model.total_bytes = state->transfer.total_bytes;
    model.uploaded_bytes = state->transfer.uploaded_bytes
        + (state->receiving_file ? state->receiving_file_bytes : 0);
    model.last_transfer_has_result = state->last_transfer_result.has_result;
    model.last_transfer_success = state->last_transfer_result.success;
    model.last_transfer_canceled = state->last_transfer_result.canceled;
    copy_text(model.last_transfer_status, sizeof(model.last_transfer_status), state->last_transfer_result.status);
    copy_text(model.last_transfer_id, sizeof(model.last_transfer_id), state->last_transfer_result.transfer_id);
    copy_text(model.last_transfer_package_display_name, sizeof(model.last_transfer_package_display_name), state->last_transfer_result.package_display_name);
    copy_text(model.last_transfer_package_kind, sizeof(model.last_transfer_package_kind), state->last_transfer_result.package_kind);
    copy_text(model.last_transfer_target_root, sizeof(model.last_transfer_target_root), state->last_transfer_result.target_root);
    copy_text(model.last_transfer_post_transfer_action, sizeof(model.last_transfer_post_transfer_action), state->last_transfer_result.post_transfer_action);
    copy_text(model.last_transfer_message, sizeof(model.last_transfer_message), state->last_transfer_result.message);
    copy_text(model.last_transfer_failed_file, sizeof(model.last_transfer_failed_file), state->last_transfer_result.failed_file);
    model.last_transfer_file_count = state->last_transfer_result.file_count;
    model.last_transfer_uploaded_files = state->last_transfer_result.uploaded_files;
    model.last_transfer_total_bytes = state->last_transfer_result.total_bytes;
    model.last_transfer_uploaded_bytes = state->last_transfer_result.uploaded_bytes;

    assistant_ui_render(&model);
}

static const char *skip_spaces(const char *value)
{
    while (value && *value && isspace((unsigned char)*value))
        value++;
    return value;
}

static void trim_trailing(char *value)
{
    size_t length = strlen(value);
    while (length > 0 && isspace((unsigned char)value[length - 1]))
    {
        value[length - 1] = '\0';
        length--;
    }
}

static bool safe_relative_path(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':') || strchr(path, '\\'))
        return false;

    char segment[UIF_MAX_TEXT];
    size_t segment_length = 0;

    for (const char *cursor = path;; cursor++)
    {
        char c = *cursor;
        if (c == '/' || c == '\0')
        {
            if (segment_length == 0)
                return false;

            segment[segment_length] = '\0';
            if (strcmp(segment, ".") == 0 || strcmp(segment, "..") == 0)
                return false;

            segment_length = 0;
            if (c == '\0')
                break;
            continue;
        }

        if ((unsigned char)c < 0x20 || segment_length + 1 >= sizeof(segment))
            return false;

        segment[segment_length++] = c;
    }

    return true;
}

static bool path_is_below(const char *path, const char *root)
{
    if (!path || !root || !path[0] || !root[0])
        return false;

    size_t root_length = strlen(root);
    size_t path_length = strlen(path);
    if (path_length <= root_length)
        return false;

    return strncasecmp(path, root, root_length) == 0
           && path[root_length] == '/'
           && path[root_length + 1] != '\0';
}

static bool path_has_allowed_sd_prefix(const char *path)
{
    static const char *allowed_prefixes[] = {
        "apps",
        "controllers",
        "games",
        "install",
        "noods",
        "ppsspp",
        "retroarch",
        "saves",
        "uinjectforge",
        "wiiu"
    };

    if (equals_ignore_case(path, "controller.ini"))
        return true;

    size_t path_length = strlen(path);
    for (size_t i = 0; i < sizeof(allowed_prefixes) / sizeof(allowed_prefixes[0]); i++)
    {
        const char *prefix = allowed_prefixes[i];
        size_t prefix_length = strlen(prefix);
        if (path_length > prefix_length
            && strncasecmp(path, prefix, prefix_length) == 0
            && path[prefix_length] == '/')
            return true;
    }

    return false;
}

static bool path_is_assistant_transaction_storage(const char *path)
{
    static const char *protected_roots[] = {
        "uinjectforge/assistant/backups",
        "uinjectforge/assistant/incoming"
    };

    for (size_t i = 0; i < sizeof(protected_roots) / sizeof(protected_roots[0]); i++)
        if (equals_ignore_case(path, protected_roots[i]) || path_is_below(path, protected_roots[i]))
            return true;
    return false;
}

static bool validate_transfer_destination_header(
    const AssistantTransfer *transfer,
    char *error,
    size_t error_size)
{
    if (!transfer)
    {
        snprintf(error, error_size, "Manifest transfer data is missing.");
        return false;
    }

    if (equals_ignore_case(transfer->package_kind, "installable-wup"))
    {
        if (!equals_ignore_case(transfer->target_root_kind, "sd-install-folder")
            || !path_is_below(transfer->target_root, "install"))
        {
            snprintf(error, error_size, "Installable manifests must target a folder under sd:/install.");
            return false;
        }
    }
    else if (equals_ignore_case(transfer->package_kind, "loadiine"))
    {
        if (!equals_ignore_case(transfer->target_root_kind, "sd-loadiine-games-folder")
            || !path_is_below(transfer->target_root, "wiiu/games"))
        {
            snprintf(error, error_size, "Loadiine manifests must target a folder under sd:/wiiu/games.");
            return false;
        }
    }
    else if (equals_ignore_case(transfer->package_kind, "sd-package"))
    {
        if (!equals_ignore_case(transfer->target_root_kind, "sd-root") || transfer->target_root[0])
        {
            snprintf(error, error_size, "SD-package manifests must target the SD root.");
            return false;
        }
    }
    else
    {
        snprintf(error, error_size, "Manifest package kind is unsupported.");
        return false;
    }

    if (!equals_ignore_case(transfer->post_transfer_action, "copy-only")
        && !equals_ignore_case(transfer->post_transfer_action, "prompt-install-on-wiiu")
        && !equals_ignore_case(transfer->post_transfer_action, "auto-install-after-validation"))
    {
        snprintf(error, error_size, "Manifest post-transfer action is unsupported.");
        return false;
    }

    if (!equals_ignore_case(transfer->install_target, "usb")
        && !equals_ignore_case(transfer->install_target, "nand"))
    {
        snprintf(error, error_size, "Manifest install target is unsupported.");
        return false;
    }

    if (!equals_ignore_case(transfer->post_transfer_action, "copy-only")
        && !equals_ignore_case(transfer->package_kind, "installable-wup"))
    {
        snprintf(error, error_size, "Install actions require an installable WUP package.");
        return false;
    }

    return true;
}

static bool validate_transfer_file_destination(
    const AssistantTransfer *transfer,
    const AssistantFile *file,
    char *error,
    size_t error_size)
{
    if (equals_ignore_case(transfer->package_kind, "sd-package"))
    {
        if (strcmp(file->target_path, file->relative_path) != 0
            || !path_has_allowed_sd_prefix(file->target_path)
            || path_is_assistant_transaction_storage(file->target_path))
        {
            snprintf(error, error_size, "SD-package file destination is outside UIF-managed SD folders.");
            return false;
        }
    }
    else if (!path_is_below(file->target_path, transfer->target_root))
    {
        snprintf(error, error_size, "Manifest file destination is outside its declared target root.");
        return false;
    }

    return true;
}

static void join_path(char *destination, size_t destination_size, const char *root, const char *relative)
{
    if (!relative || !relative[0])
    {
        snprintf(destination, destination_size, "%s", root ? root : "");
        return;
    }

    size_t root_length = root ? strlen(root) : 0;
    const char *separator = "";
    if (root_length > 0 && root[root_length - 1] != '/' && root[root_length - 1] != ':')
        separator = "/";
    else if (root_length > 0 && root[root_length - 1] == ':')
        separator = "/";

    snprintf(destination, destination_size, "%s%s%s", root ? root : "", separator, relative);
}

static bool join_path_checked(char *destination, size_t destination_size, const char *root, const char *relative)
{
    if (!destination || destination_size == 0)
        return false;

    if (!relative || !relative[0])
    {
        int written = snprintf(destination, destination_size, "%s", root ? root : "");
        return written >= 0 && (size_t)written < destination_size;
    }

    size_t root_length = root ? strlen(root) : 0;
    const char *separator = "";
    if (root_length > 0 && root[root_length - 1] != '/' && root[root_length - 1] != ':')
        separator = "/";
    else if (root_length > 0 && root[root_length - 1] == ':')
        separator = "/";

    int written = snprintf(destination, destination_size, "%s%s%s", root ? root : "", separator, relative);
    if (written < 0 || (size_t)written >= destination_size)
    {
        destination[0] = '\0';
        return false;
    }

    return true;
}

static void parent_path(char *destination, size_t destination_size, const char *path)
{
    copy_text(destination, destination_size, path);
    char *slash = strrchr(destination, '/');
    if (slash)
        *slash = '\0';
}

static bool read_text_file_limited(const char *path, char *buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0)
        return false;

    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    size_t read = fread(buffer, 1, buffer_size - 1, file);
    fclose(file);
    buffer[read] = '\0';
    return read > 0;
}

static bool path_exists(const char *path)
{
    struct stat info;
    return path && path[0] && stat(path, &info) == 0;
}

static bool path_is_regular_file(const char *path)
{
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool path_is_directory(const char *path)
{
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_hex_component_8(const char *value)
{
    if (!value || strlen(value) != 8)
        return false;

    for (size_t i = 0; i < 8; i++)
    {
        if (!isxdigit((unsigned char)value[i]))
            return false;
    }

    return true;
}

static void xml_unescape_copy(char *destination, size_t destination_size, const char *start, size_t length)
{
    if (!destination || destination_size == 0)
        return;

    size_t out = 0;
    for (size_t i = 0; start && i < length && out + 1 < destination_size; i++)
    {
        if (start[i] == '&')
        {
            const char *replacement = NULL;
            size_t consumed = 0;
            if (i + 5 <= length && strncmp(start + i, "&amp;", 5) == 0)
            {
                replacement = "&";
                consumed = 5;
            }
            else if (i + 6 <= length && strncmp(start + i, "&quot;", 6) == 0)
            {
                replacement = "\"";
                consumed = 6;
            }
            else if (i + 6 <= length && strncmp(start + i, "&apos;", 6) == 0)
            {
                replacement = "'";
                consumed = 6;
            }
            else if (i + 4 <= length && strncmp(start + i, "&lt;", 4) == 0)
            {
                replacement = "<";
                consumed = 4;
            }
            else if (i + 4 <= length && strncmp(start + i, "&gt;", 4) == 0)
            {
                replacement = ">";
                consumed = 4;
            }

            if (replacement)
            {
                for (size_t r = 0; replacement[r] && out + 1 < destination_size; r++)
                    destination[out++] = replacement[r];
                i += consumed - 1;
                continue;
            }
        }

        unsigned char c = (unsigned char)start[i];
        if (c >= 0x20 || c == '\t')
            destination[out++] = (char)c;
    }

    destination[out] = '\0';
    trim_trailing(destination);
}

static bool extract_xml_tag_value(const char *xml, const char *tag, char *output, size_t output_size)
{
    if (!xml || !tag || !output || output_size == 0)
        return false;

    char open_tag[UIF_MAX_TEXT];
    char close_tag[UIF_MAX_TEXT];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strstr(xml, open_tag);
    if (!start)
        return false;

    start += strlen(open_tag);
    const char *end = strstr(start, close_tag);
    if (!end || end <= start)
        return false;

    xml_unescape_copy(output, output_size, start, (size_t)(end - start));
    return output[0] != '\0';
}

static void read_installed_title_metadata(
    const char *title_path,
    const char *fallback_title_id,
    char *display_name,
    size_t display_name_size,
    char *version,
    size_t version_size,
    char *product_code,
    size_t product_code_size)
{
    copy_text(display_name, display_name_size, fallback_title_id);
    copy_text(version, version_size, "");
    copy_text(product_code, product_code_size, "");

    char meta_path[UIF_MAX_PATH];
    join_path(meta_path, sizeof(meta_path), title_path, "meta/meta.xml");

    char xml[65536];
    if (!read_text_file_limited(meta_path, xml, sizeof(xml)))
        return;

    if (!extract_xml_tag_value(xml, "longname_en", display_name, display_name_size)
        && !extract_xml_tag_value(xml, "shortname_en", display_name, display_name_size)
        && !extract_xml_tag_value(xml, "longname_ja", display_name, display_name_size))
    {
        copy_text(display_name, display_name_size, fallback_title_id);
    }

    extract_xml_tag_value(xml, "title_version", version, version_size);
    extract_xml_tag_value(xml, "product_code", product_code, product_code_size);
}

static bool mkdir_recursive(const char *path)
{
    if (!path || !path[0])
        return false;

    char current[UIF_MAX_PATH];
    copy_text(current, sizeof(current), path);

    char *cursor = current;
    char *device_separator = strstr(current, ":/");
    if (device_separator)
        cursor = device_separator + 2;
    else if (*cursor == '/')
        cursor++;

    for (; *cursor; cursor++)
    {
        if (*cursor == '/')
        {
            *cursor = '\0';
            if (strlen(current) > 0 && mkdir(current, 0777) != 0 && errno != EEXIST)
            {
                *cursor = '/';
                return false;
            }
            *cursor = '/';
        }
    }

    return mkdir(current, 0777) == 0 || errno == EEXIST;
}

static void append_persistent_activity_log(const AssistantState *state, const char *message)
{
    if (!state || !message || !message[0] || !state->sd_root[0])
        return;

    char log_dir[UIF_MAX_PATH];
    join_path(log_dir, sizeof(log_dir), state->sd_root, "uinjectforge/assistant");
    if (!mkdir_recursive(log_dir))
        return;

    char log_path[UIF_MAX_PATH];
    join_path(log_path, sizeof(log_path), log_dir, "assistant.log");
    FILE *file = fopen(log_path, "ab");
    if (!file)
        return;

    uint64_t timestamp_ms = OSTicksToMilliseconds(OSGetTime());
    fprintf(file, "[%llu] %s\n", (unsigned long long)timestamp_ms, message);
    fclose(file);
    g_log_revision++;
}

static unsigned int read_persistent_log_tail(
    const AssistantState *state,
    char lines[][ASSISTANT_UI_TEXT],
    unsigned int max_lines)
{
    if (!state || !state->sd_root[0] || !lines || max_lines == 0)
        return 0;

    char log_path[UIF_MAX_PATH];
    join_path(log_path, sizeof(log_path), state->sd_root, "uinjectforge/assistant/assistant.log");
    FILE *file = fopen(log_path, "rb");
    if (!file)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    long file_size = ftell(file);
    if (file_size <= 0)
    {
        fclose(file);
        return 0;
    }

    long tail_offset = file_size > UIF_LOG_TAIL_BYTES ? file_size - UIF_LOG_TAIL_BYTES : 0;
    if (fseek(file, tail_offset, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    size_t max_read = (size_t)(file_size - tail_offset);
    char *buffer = (char *)malloc(max_read + 1);
    if (!buffer)
    {
        fclose(file);
        return 0;
    }

    size_t bytes_read = fread(buffer, 1, max_read, file);
    fclose(file);
    buffer[bytes_read] = '\0';

    char *cursor = buffer;
    if (tail_offset > 0)
    {
        char *first_newline = strchr(cursor, '\n');
        if (first_newline)
            cursor = first_newline + 1;
    }

    unsigned int count = 0;
    while (*cursor)
    {
        char *line_start = cursor;
        char *line_end = strchr(line_start, '\n');
        if (line_end)
        {
            *line_end = '\0';
            cursor = line_end + 1;
        }
        else
        {
            cursor = line_start + strlen(line_start);
        }

        trim_trailing(line_start);
        if (!line_start[0])
            continue;

        if (count < max_lines)
        {
            copy_text(lines[count], ASSISTANT_UI_TEXT, line_start);
            count++;
        }
        else
        {
            for (unsigned int i = 1; i < max_lines; i++)
                copy_text(lines[i - 1], ASSISTANT_UI_TEXT, lines[i]);
            copy_text(lines[max_lines - 1], ASSISTANT_UI_TEXT, line_start);
        }
    }

    free(buffer);
    return count;
}

static bool ensure_parent_directory(const char *path)
{
    char parent[UIF_MAX_PATH];
    parent_path(parent, sizeof(parent), path);
    if (!parent[0])
        return true;
    return mkdir_recursive(parent);
}

static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        remove(path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child[UIF_MAX_PATH];
        join_path(child, sizeof(child), path, entry->d_name);
        remove_tree(child);
    }

    closedir(dir);
    rmdir(path);
}

static int count_directory_children(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            count++;
    }
    closedir(dir);
    return count;
}

static void recover_assistant_storage(AssistantState *state)
{
    char incoming_root[UIF_MAX_PATH];
    char backup_root[UIF_MAX_PATH];
    join_path(incoming_root, sizeof(incoming_root), state->sd_root, "uinjectforge/assistant/incoming");
    join_path(backup_root, sizeof(backup_root), state->sd_root, "uinjectforge/assistant/backups");

    int stale_transfer_count = count_directory_children(incoming_root);
    if (path_exists(incoming_root))
        remove_tree(incoming_root);

    if (path_exists(incoming_root))
    {
        set_status(state, "Could not clean interrupted transfer staging. Check sd:/uinjectforge/assistant/incoming.");
    }
    else if (stale_transfer_count > 0)
    {
        char message[UIF_MAX_TEXT];
        snprintf(message, sizeof(message), "Cleaned %d interrupted transfer staging folder(s).", stale_transfer_count);
        set_status(state, message);
    }

    int recovery_count = count_directory_children(backup_root);
    if (recovery_count > 0)
    {
        char message[UIF_MAX_TEXT];
        snprintf(message, sizeof(message), "%d transfer recovery folder(s) remain under sd:/uinjectforge/assistant/backups.", recovery_count);
        set_status(state, message);
    }
}

static bool get_sd_space(const AssistantState *state, long long *free_bytes, long long *total_bytes)
{
    if (free_bytes)
        *free_bytes = 0;
    if (total_bytes)
        *total_bytes = 0;

    if (!state || !state->sd_root[0])
        return false;

    struct statvfs info;
    if (statvfs(state->sd_root, &info) != 0)
        return false;

    unsigned long long free_value = (unsigned long long)info.f_bavail * (unsigned long long)info.f_frsize;
    unsigned long long total_value = (unsigned long long)info.f_blocks * (unsigned long long)info.f_frsize;
    if (free_bytes)
        *free_bytes = (long long)free_value;
    if (total_bytes)
        *total_bytes = (long long)total_value;
    return true;
}

static bool get_cached_sd_space(const AssistantState *state, long long *free_bytes, long long *total_bytes)
{
    static bool loaded = false;
    static bool known = false;
    static long long cached_free = 0, cached_total = 0;
    static uint64_t checked_at = 0;
    static unsigned int revision = UINT_MAX;
    uint64_t now = OSGetTime();
    if (!loaded || (!state->install_active && !state->receiving_file
        && (revision != g_storage_revision || now - checked_at >= OSMillisecondsToTicks(30000))))
    {
        known = get_sd_space(state, &cached_free, &cached_total);
        checked_at = now;
        revision = g_storage_revision;
        loaded = true;
    }
    *free_bytes = cached_free;
    *total_bytes = cached_total;
    return known;
}

static bool has_sd_space_for_transfer(const AssistantState *state, long long transfer_bytes)
{
    if (transfer_bytes < 0)
        return false;

    long long free_bytes = 0;
    long long total_bytes = 0;
    if (!get_sd_space(state, &free_bytes, &total_bytes))
        return true;

    const unsigned long long safety_margin = 16ULL * 1024ULL * 1024ULL;
    unsigned long long required = (unsigned long long)transfer_bytes + safety_margin;
    return (unsigned long long)free_bytes >= required;
}

static void digest_to_hex(const unsigned char digest[32], char output[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        output[i * 2] = hex[(digest[i] >> 4) & 0x0F];
        output[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    output[64] = '\0';
}

static bool hash_file_sha256(const char *path, char output[65], long long *size_bytes)
{
    if (size_bytes)
        *size_bytes = 0;

    FILE *file = fopen(path, "rb");
    if (!file)
        return false;

    unsigned char *buffer = (unsigned char *)malloc(UIF_IO_BUFFER);
    if (!buffer)
    {
        fclose(file);
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts_ret(&sha, 0) != 0)
    {
        mbedtls_sha256_free(&sha);
        free(buffer);
        fclose(file);
        return false;
    }

    bool success = true;
    long long total = 0;
    for (;;)
    {
        size_t read = fread(buffer, 1, UIF_IO_BUFFER, file);
        if (read > 0)
        {
            if (mbedtls_sha256_update_ret(&sha, buffer, read) != 0)
            {
                success = false;
                break;
            }
            total += (long long)read;
        }

        if (read < UIF_IO_BUFFER)
        {
            if (ferror(file))
                success = false;
            break;
        }
    }

    unsigned char digest[32];
    if (success && mbedtls_sha256_finish_ret(&sha, digest) != 0)
        success = false;

    mbedtls_sha256_free(&sha);
    free(buffer);
    if (fclose(file) != 0)
        success = false;

    if (!success)
        return false;

    digest_to_hex(digest, output);
    if (size_bytes)
        *size_bytes = total;
    return true;
}

static bool url_decode(const char *source, char *destination, size_t destination_size)
{
    size_t out = 0;
    for (size_t i = 0; source && source[i]; i++)
    {
        if (out + 1 >= destination_size)
            return false;

        if (source[i] == '%' && source[i + 1] && source[i + 2])
        {
            int hi = hex_value(source[i + 1]);
            int lo = hex_value(source[i + 2]);
            if (hi < 0 || lo < 0)
                return false;
            destination[out++] = (char)((hi << 4) | lo);
            i += 2;
        }
        else
        {
            destination[out++] = source[i] == '+' ? ' ' : source[i];
        }
    }

    destination[out] = '\0';
    return true;
}

static ssize_t send_all(int fd, const void *buffer, size_t length)
{
    const char *cursor = (const char *)buffer;
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t result = send(fd, cursor + sent, length - sent, 0);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return result;
        sent += (size_t)result;
    }

    return (ssize_t)sent;
}

static void send_json(int fd, int status_code, const char *status_text, const char *json)
{
    char header[512];
    size_t body_length = strlen(json);
    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status_code,
        status_text,
        (unsigned int)body_length);

    send_all(fd, header, strlen(header));
    send_all(fd, json, body_length);
}

static void send_html(int fd, int status_code, const char *status_text, const char *html)
{
    char header[512];
    size_t body_length = strlen(html);
    snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status_code,
        status_text,
        (unsigned int)body_length);

    send_all(fd, header, strlen(header));
    send_all(fd, html, body_length);
}

static void send_message(int fd, int status_code, const char *status_text, const char *message)
{
    char json[768];
    char escaped[512];
    json_escape(message ? message : "Request failed.", escaped, sizeof(escaped));
    snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"%s\"}", escaped);
    send_json(fd, status_code, status_text, json);
}

static int find_header_end(const char *buffer, int used)
{
    for (int i = 3; i < used; i++)
    {
        if (buffer[i - 3] == '\r' && buffer[i - 2] == '\n' && buffer[i - 1] == '\r' && buffer[i] == '\n')
            return i + 1;
    }

    return -1;
}

static bool parse_http_request(int fd, HttpRequest *request)
{
    memset(request, 0, sizeof(*request));
    request->content_length = 0;
    request->x_size = -1;

    char header[UIF_MAX_HEADER + 1];
    int used = 0;
    int header_end = -1;

    while (used < UIF_MAX_HEADER)
    {
        ssize_t read_bytes = recv(fd, header + used, UIF_MAX_HEADER - used, 0);
        if (read_bytes < 0 && errno == EINTR)
            continue;
        if (read_bytes <= 0)
            return false;

        used += (int)read_bytes;
        header_end = find_header_end(header, used);
        if (header_end >= 0)
            break;
    }

    if (header_end < 0)
        return false;

    header[used] = '\0';
    int header_length = header_end;
    int body_prefix_length = used - header_length;
    if (body_prefix_length > 0)
    {
        memcpy(request->body_prefix, header + header_length, (size_t)body_prefix_length);
        request->body_prefix_len = (size_t)body_prefix_length;
    }

    header[header_length] = '\0';
    char *line = strtok(header, "\r\n");
    if (!line || sscanf(line, "%11s %767s", request->method, request->path) != 2)
        return false;

    char *query = strchr(request->path, '?');
    if (query)
        *query = '\0';

    while ((line = strtok(NULL, "\r\n")) != NULL)
    {
        char *colon = strchr(line, ':');
        if (!colon)
            continue;

        *colon = '\0';
        char *name = line;
        char *value = (char *)skip_spaces(colon + 1);
        trim_trailing(value);

        if (equals_ignore_case(name, "Content-Length"))
            request->content_length = strtoll(value, NULL, 10);
        else if (equals_ignore_case(name, "Authorization"))
            copy_text(request->authorization, sizeof(request->authorization), value);
        else if (equals_ignore_case(name, "Expect") && equals_ignore_case(value, "100-continue"))
            request->expect_continue = true;
        else if (equals_ignore_case(name, "X-UIF-SHA256"))
            copy_text(request->x_sha256, sizeof(request->x_sha256), value);
        else if (equals_ignore_case(name, "X-UIF-Size"))
            request->x_size = strtoll(value, NULL, 10);
    }

    if (request->expect_continue)
    {
        const char *response = "HTTP/1.1 100 Continue\r\n\r\n";
        send_all(fd, response, strlen(response));
    }

    return true;
}

static bool read_body_to_memory(int fd, const HttpRequest *request, char **body_out)
{
    *body_out = NULL;
    if (request->content_length < 0 || request->content_length > UIF_MAX_MANIFEST)
        return false;

    char *body = (char *)malloc((size_t)request->content_length + 1);
    if (!body)
        return false;

    long long copied = 0;
    if (request->body_prefix_len > 0)
    {
        size_t take = request->body_prefix_len;
        if ((long long)take > request->content_length)
            take = (size_t)request->content_length;
        memcpy(body, request->body_prefix, take);
        copied += (long long)take;
    }

    while (copied < request->content_length)
    {
        ssize_t read_bytes = recv(fd, body + copied, (size_t)(request->content_length - copied), 0);
        if (read_bytes < 0 && errno == EINTR)
            continue;
        if (read_bytes <= 0)
        {
            free(body);
            return false;
        }
        copied += read_bytes;
    }

    body[request->content_length] = '\0';
    *body_out = body;
    return true;
}

static const char *find_json_key(const char *json, const char *key)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *position = strstr(json, pattern);
    if (!position)
        return NULL;

    position += strlen(pattern);
    position = skip_spaces(position);
    if (*position != ':')
        return NULL;

    return skip_spaces(position + 1);
}

static bool json_get_string(const char *json, const char *key, char *output, size_t output_size)
{
    const char *position = find_json_key(json, key);
    if (!position || *position != '"')
        return false;

    position++;
    size_t out = 0;
    while (*position && *position != '"')
    {
        char c = *position++;
        if (c == '\\')
        {
            c = *position++;
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c == 'u')
            {
                for (int i = 0; i < 4 && isxdigit((unsigned char)*position); i++)
                    position++;
                c = '?';
            }
        }

        if (out + 1 >= output_size)
            return false;
        output[out++] = c;
    }

    if (*position != '"')
        return false;

    output[out] = '\0';
    return true;
}

static bool json_get_long(const char *json, const char *key, long long *output)
{
    const char *position = find_json_key(json, key);
    if (!position)
        return false;

    char *end = NULL;
    long long value = strtoll(position, &end, 10);
    if (end == position)
        return false;

    *output = value;
    return true;
}

static bool json_get_bool(const char *json, const char *key, bool *output)
{
    const char *position = find_json_key(json, key);
    if (!position)
        return false;

    if (starts_with_ignore_case(position, "true"))
    {
        *output = true;
        return true;
    }

    if (starts_with_ignore_case(position, "false"))
    {
        *output = false;
        return true;
    }

    return false;
}

static bool normalize_pairing_code(const char *input, char *output, size_t output_size)
{
    if (!input || !output || output_size == 0)
        return false;

    size_t out = 0;
    for (size_t i = 0; input[i]; i++)
    {
        unsigned char c = (unsigned char)input[i];
        if (isdigit(c))
        {
            if (out + 1 >= output_size)
                return false;
            output[out++] = (char)c;
        }
        else if (!isspace(c) && c != '"' && c != '\'' && c != '-' && c != '_' && c != '=' && c != '&')
        {
            continue;
        }
    }

    output[out] = '\0';
    return out > 0;
}

static bool extract_pairing_code_from_body(const char *body, char *output, size_t output_size)
{
    char value[64];
    if ((json_get_string(body, "pairingCode", value, sizeof(value))
            || json_get_string(body, "PairingCode", value, sizeof(value))
            || json_get_string(body, "code", value, sizeof(value)))
        && normalize_pairing_code(value, output, output_size))
    {
        return true;
    }

    long long numeric_code = 0;
    if ((json_get_long(body, "pairingCode", &numeric_code)
            || json_get_long(body, "PairingCode", &numeric_code)
            || json_get_long(body, "code", &numeric_code))
        && numeric_code >= 0)
    {
        snprintf(value, sizeof(value), "%06lld", numeric_code);
        return normalize_pairing_code(value, output, output_size);
    }

    const char *form = strstr(body, "pairingCode=");
    if (!form)
        form = strstr(body, "PairingCode=");
    if (!form)
        form = strstr(body, "code=");
    if (form)
    {
        form = strchr(form, '=');
        if (form)
        {
            form++;
            char encoded[64];
            size_t i = 0;
            while (form[i] && form[i] != '&' && i + 1 < sizeof(encoded))
            {
                encoded[i] = form[i];
                i++;
            }
            encoded[i] = '\0';

            char decoded[64];
            if (url_decode(encoded, decoded, sizeof(decoded)) && normalize_pairing_code(decoded, output, output_size))
                return true;
        }
    }

    return normalize_pairing_code(body, output, output_size);
}

static void load_assistant_config(AssistantState *state)
{
    state->http_port = UIF_ASSISTANT_PORT;
    state->broadcast_enabled = true;
    state->passive_broadcast_enabled = false;
    state->simple_ui = true;
    state->install_enabled = true;
    state->manual_install_enabled = true;
    state->auto_install_enabled = false;
    state->ui_scale = UIF_ASSISTANT_DEFAULT_UI_SCALE;
    state->broadcast_port = UIF_ASSISTANT_PORT;

    char config_path[UIF_MAX_PATH];
    join_path(config_path, sizeof(config_path), state->sd_root, "wiiu/apps/uinjectforge-assistant/config.json");

    FILE *file = fopen(config_path, "rb");
    if (!file)
        return;

    char json[8192];
    size_t read = fread(json, 1, sizeof(json) - 1, file);
    fclose(file);
    json[read] = '\0';

    long long port = 0;
    if (json_get_long(json, "httpPort", &port) && port >= 1 && port <= 65535)
        state->http_port = (int)port;

    long long broadcast_port = 0;
    if (json_get_long(json, "broadcastPort", &broadcast_port) && broadcast_port >= 1 && broadcast_port <= 65535)
        state->broadcast_port = (int)broadcast_port;
    else
        state->broadcast_port = state->http_port;

    bool broadcast_enabled = true;
    if (json_get_bool(json, "broadcastEnabled", &broadcast_enabled))
        state->broadcast_enabled = broadcast_enabled;

    bool passive_broadcast_enabled = false;
    if (json_get_bool(json, "passiveBroadcastEnabled", &passive_broadcast_enabled))
        state->passive_broadcast_enabled = passive_broadcast_enabled;

    bool simple_ui = state->simple_ui;
    if (json_get_bool(json, "simpleUi", &simple_ui))
        state->simple_ui = simple_ui;

    bool install_enabled = true;
    bool manual_install_enabled = true;
    bool auto_install_enabled = false;
    bool has_install_enabled = json_get_bool(json, "installEnabled", &install_enabled);
    bool has_manual_install_enabled = json_get_bool(json, "manualInstallEnabled", &manual_install_enabled);
    bool has_auto_install_enabled = json_get_bool(json, "autoInstallEnabled", &auto_install_enabled);
    AssistantInstallPermissions install_permissions = assistant_resolve_install_permissions(
        has_install_enabled,
        install_enabled,
        has_manual_install_enabled,
        manual_install_enabled,
        has_auto_install_enabled,
        auto_install_enabled);
    state->install_enabled = install_permissions.global_enabled;
    state->manual_install_enabled = install_permissions.manual_enabled;
    state->auto_install_enabled = install_permissions.automatic_enabled;

    char ui_mode[64];
    if (json_get_string(json, "uiMode", ui_mode, sizeof(ui_mode)))
    {
        if (equals_ignore_case(ui_mode, "simple") || equals_ignore_case(ui_mode, "fallback"))
        {
            state->simple_ui = true;
        }
        else if (equals_ignore_case(ui_mode, "full") || equals_ignore_case(ui_mode, "dashboard") || equals_ignore_case(ui_mode, "advanced"))
        {
            state->simple_ui = false;
        }
    }

}

static const char *find_matching_brace(const char *start)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (const char *cursor = start; *cursor; cursor++)
    {
        char c = *cursor;
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
            continue;
        }

        if (c == '"')
            in_string = true;
        else if (c == '{')
            depth++;
        else if (c == '}')
        {
            depth--;
            if (depth == 0)
                return cursor;
        }
    }

    return NULL;
}

static AssistantFile *find_file(AssistantTransfer *transfer, const char *relative_path)
{
    for (int i = 0; i < transfer->file_count; i++)
    {
        if (strcmp(transfer->files[i].relative_path, relative_path) == 0)
            return &transfer->files[i];
    }

    return NULL;
}

static bool parse_manifest(AssistantState *state, const char *json, char *error, size_t error_size)
{
    static AssistantTransfer transfer;
    memset(&transfer, 0, sizeof(transfer));

    char protocol[UIF_MAX_TEXT];
    if (!json_get_string(json, "protocolVersion", protocol, sizeof(protocol)) || strcmp(protocol, UIF_ASSISTANT_PROTOCOL) != 0)
    {
        snprintf(error, error_size, "Unsupported or missing protocol version.");
        return false;
    }

    if (!json_get_string(json, "packageDisplayName", transfer.package_display_name, sizeof(transfer.package_display_name)))
        copy_text(transfer.package_display_name, sizeof(transfer.package_display_name), "UInjectForge Package");

    if (!json_get_string(json, "packageKind", transfer.package_kind, sizeof(transfer.package_kind))
        || !json_get_string(json, "targetRootKind", transfer.target_root_kind, sizeof(transfer.target_root_kind))
        || !json_get_string(json, "targetRoot", transfer.target_root, sizeof(transfer.target_root))
        || !json_get_long(json, "totalBytes", &transfer.total_bytes))
    {
        snprintf(error, error_size, "Manifest is missing required package fields.");
        return false;
    }

    if (transfer.total_bytes < 0)
    {
        snprintf(error, error_size, "Manifest total bytes is invalid.");
        return false;
    }

    if (!json_get_string(json, "postTransferAction", transfer.post_transfer_action, sizeof(transfer.post_transfer_action)))
        copy_text(transfer.post_transfer_action, sizeof(transfer.post_transfer_action), "copy-only");

    if (!json_get_string(json, "installTarget", transfer.install_target, sizeof(transfer.install_target)))
        copy_text(transfer.install_target, sizeof(transfer.install_target), "usb");

    bool system_memory_install_required = false;
    if (json_get_bool(json, "systemMemoryInstallRequired", &system_memory_install_required))
        transfer.system_memory_install_required = system_memory_install_required;
    if (transfer.system_memory_install_required)
        copy_text(transfer.install_target, sizeof(transfer.install_target), "nand");

    long long file_count_long = 0;
    if (!json_get_long(json, "fileCount", &file_count_long)
        || file_count_long <= 0
        || file_count_long > UIF_MAX_FILES)
    {
        snprintf(error, error_size, "Manifest file count is invalid.");
        return false;
    }

    if (transfer.target_root[0] && !safe_relative_path(transfer.target_root))
    {
        snprintf(error, error_size, "Manifest target root is unsafe.");
        return false;
    }

    if (!validate_transfer_destination_header(&transfer, error, error_size))
        return false;

    const char *files_key = strstr(json, "\"files\"");
    if (!files_key)
    {
        snprintf(error, error_size, "Manifest has no files array.");
        return false;
    }

    const char *array = strchr(files_key, '[');
    if (!array)
    {
        snprintf(error, error_size, "Manifest files array is invalid.");
        return false;
    }

    int parsed = 0;
    long long total = 0;
    const char *cursor = array + 1;
    while (*cursor && *cursor != ']')
    {
        const char *object_start = strchr(cursor, '{');
        if (!object_start)
            break;

        const char *object_end = find_matching_brace(object_start);
        if (!object_end)
        {
            snprintf(error, error_size, "Manifest file entry is malformed.");
            return false;
        }

        size_t object_length = (size_t)(object_end - object_start + 1);
        char *object_json = (char *)malloc(object_length + 1);
        if (!object_json)
        {
            snprintf(error, error_size, "Out of memory while parsing manifest.");
            return false;
        }

        memcpy(object_json, object_start, object_length);
        object_json[object_length] = '\0';

        if (parsed >= UIF_MAX_FILES)
        {
            free(object_json);
            snprintf(error, error_size, "Manifest has too many files.");
            return false;
        }

        AssistantFile *file = &transfer.files[parsed];
        long long size = 0;
        bool ok = json_get_string(object_json, "relativePath", file->relative_path, sizeof(file->relative_path))
                  && json_get_string(object_json, "targetPath", file->target_path, sizeof(file->target_path))
                  && json_get_long(object_json, "sizeBytes", &size)
                  && json_get_string(object_json, "sha256", file->sha256, sizeof(file->sha256));
        free(object_json);

        if (!ok || size < 0)
        {
            snprintf(error, error_size, "Manifest file entry is missing required fields.");
            return false;
        }

        file->size_bytes = size;
        if (!safe_relative_path(file->relative_path)
            || !safe_relative_path(file->target_path)
            || !is_hex_sha256(file->sha256))
        {
            snprintf(error, error_size, "Manifest file entry is unsafe or missing SHA-256.");
            return false;
        }

        if (!validate_transfer_file_destination(&transfer, file, error, error_size))
            return false;

        for (int i = 0; i < parsed; i++)
        {
            if (strcasecmp(transfer.files[i].target_path, file->target_path) == 0
                || strcasecmp(transfer.files[i].relative_path, file->relative_path) == 0
                || path_is_below(transfer.files[i].target_path, file->target_path)
                || path_is_below(file->target_path, transfer.files[i].target_path)
                || path_is_below(transfer.files[i].relative_path, file->relative_path)
                || path_is_below(file->relative_path, transfer.files[i].relative_path))
            {
                snprintf(error, error_size, "Manifest contains duplicate file paths.");
                return false;
            }
        }

        if (size > LLONG_MAX - total)
        {
            snprintf(error, error_size, "Manifest total bytes overflowed.");
            return false;
        }

        total += size;
        parsed++;
        cursor = object_end + 1;
    }

    if (parsed != file_count_long)
    {
        snprintf(error, error_size, "Manifest file count does not match file entries.");
        return false;
    }

    if (total != transfer.total_bytes)
    {
        snprintf(error, error_size, "Manifest total bytes do not match file entries.");
        return false;
    }

    if (state->transfer.active)
    {
        snprintf(error, error_size, "Another Assistant transfer is already active. Finish or cancel it before starting a new transfer.");
        return false;
    }

    if (!has_sd_space_for_transfer(state, transfer.total_bytes))
    {
        snprintf(error, error_size, "SD card does not have enough free space for this transfer.");
        return false;
    }

    snprintf(
        transfer.transfer_id,
        sizeof(transfer.transfer_id),
        "%08X%08X",
        (unsigned int)(OSGetTime() & 0xFFFFFFFF),
        (unsigned int)((OSGetTime() >> 32) & 0xFFFFFFFF));
    char incoming_base[UIF_MAX_PATH];
    if (!join_path_checked(incoming_base, sizeof(incoming_base), state->sd_root, "uinjectforge/assistant/incoming")
        || !join_path_checked(transfer.incoming_root, sizeof(transfer.incoming_root), incoming_base, transfer.transfer_id))
    {
        snprintf(error, error_size, "Transfer staging path is too long.");
        return false;
    }

    transfer.file_count = parsed;
    transfer.active = true;
    transfer.manifest_loaded = true;

    if (!mkdir_recursive(transfer.incoming_root))
    {
        int create_error = errno;
        snprintf(
            error,
            error_size,
            "Could not create transfer staging folder at %s (errno %d).",
            transfer.incoming_root,
            create_error);
        return false;
    }

    state->transfer = transfer;
    return true;
}

static bool authorization_ok(const AssistantState *state, const HttpRequest *request)
{
    if (!state->paired || !state->access_token[0])
        return false;

    const char *prefix = "Bearer ";
    if (!starts_with_ignore_case(request->authorization, prefix))
        return false;

    const char *token = skip_spaces(request->authorization + strlen(prefix));
    return assistant_constant_time_equal(token, state->access_token);
}

static void clear_receiving_file_state(AssistantState *state)
{
    if (!state)
        return;

    state->receiving_file = false;
    state->receiving_file_path[0] = '\0';
    state->receiving_file_bytes = 0;
    state->receiving_file_total_bytes = 0;
}

static bool receive_file_body(
    int fd,
    AssistantState *state,
    const HttpRequest *request,
    AssistantFile *file,
    char *error,
    size_t error_size)
{
    if (request->content_length != file->size_bytes || request->x_size != file->size_bytes)
    {
        snprintf(error, error_size, "Uploaded file size does not match manifest.");
        return false;
    }

    if (!equals_ignore_case(request->x_sha256, file->sha256))
    {
        snprintf(error, error_size, "Upload SHA-256 header does not match manifest.");
        return false;
    }

    if (!ensure_parent_directory(file->staged_path))
    {
        snprintf(error, error_size, "Could not create staging folder.");
        return false;
    }

    FILE *output = fopen(file->staged_path, "wb");
    if (!output)
    {
        snprintf(error, error_size, "Could not open staging file.");
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts_ret(&sha, 0) != 0)
    {
        mbedtls_sha256_free(&sha);
        fclose(output);
        remove(file->staged_path);
        snprintf(error, error_size, "Could not initialize upload verification.");
        return false;
    }

    long long written = 0;
    bool success = true;
    if (request->body_prefix_len > 0)
    {
        size_t take = request->body_prefix_len;
        if ((long long)take > request->content_length)
            take = (size_t)request->content_length;

        if (take > 0)
        {
            if (mbedtls_sha256_update_ret(&sha, (const unsigned char *)request->body_prefix, take) != 0
                || fwrite(request->body_prefix, 1, take, output) != take)
            {
                snprintf(error, error_size, "Could not persist uploaded file to the SD card.");
                success = false;
            }
            written += (long long)take;
        }
    }

    char *buffer = (char *)malloc(UIF_IO_BUFFER);
    if (!buffer)
    {
        mbedtls_sha256_free(&sha);
        fclose(output);
        remove(file->staged_path);
        snprintf(error, error_size, "Out of memory receiving file.");
        return false;
    }

    state->receiving_file = true;
    copy_text(state->receiving_file_path, sizeof(state->receiving_file_path), file->relative_path);
    state->receiving_file_bytes = written;
    state->receiving_file_total_bytes = request->content_length;
    uint64_t last_data_ticks = OSGetTime();
    uint64_t last_render_ticks = 0;

    while (success && written < request->content_length)
    {
        if (g_procui_exit_requested || !WHBProcIsRunning())
        {
            snprintf(error, error_size, "Upload canceled because Assistant is exiting.");
            success = false;
            break;
        }

        assistant_ui_poll_input();
        handle_discovery_probe(state);
        send_discovery_broadcast(state);

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        struct timeval wait_time = {.tv_sec = 0, .tv_usec = 100000};
        int ready = select(fd + 1, &read_set, NULL, NULL, &wait_time);
        uint64_t now = OSGetTime();
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            snprintf(error, error_size, "Socket failed while receiving the uploaded file.");
            success = false;
            break;
        }

        if (ready == 0)
        {
            if (now - last_data_ticks >= OSMillisecondsToTicks(UIF_UPLOAD_IDLE_TIMEOUT_MS))
            {
                snprintf(error, error_size, "Upload stopped after 30 seconds without receiving data.");
                success = false;
                break;
            }
        }
        else
        {
            long long remaining = request->content_length - written;
            size_t to_read = remaining > UIF_IO_BUFFER ? UIF_IO_BUFFER : (size_t)remaining;
            ssize_t read_bytes = recv(fd, buffer, to_read, 0);
            if (read_bytes <= 0)
            {
                snprintf(error, error_size, "Connection closed during file upload.");
                success = false;
                break;
            }

            if (mbedtls_sha256_update_ret(&sha, (const unsigned char *)buffer, (size_t)read_bytes) != 0
                || fwrite(buffer, 1, (size_t)read_bytes, output) != (size_t)read_bytes)
            {
                snprintf(error, error_size, "Could not persist uploaded file to the SD card.");
                success = false;
                break;
            }

            written += read_bytes;
            state->receiving_file_bytes = written;
            last_data_ticks = now;
        }

        if (now - last_render_ticks >= OSMillisecondsToTicks(UIF_UPLOAD_RENDER_INTERVAL_MS))
        {
            int percent = request->content_length > 0
                ? (int)((written * 100LL) / request->content_length)
                : 100;
            char short_path[96];
            copy_display_name(short_path, sizeof(short_path), file->relative_path);
            snprintf(state->status, sizeof(state->status), "Receiving %s: %d%%.", short_path, percent);
            render_assistant_screen(state);
            last_render_ticks = now;
        }
    }

    unsigned char digest[32];
    if (success && mbedtls_sha256_finish_ret(&sha, digest) != 0)
    {
        snprintf(error, error_size, "Could not finalize upload verification.");
        success = false;
    }
    mbedtls_sha256_free(&sha);
    free(buffer);

    bool finalize_failed = fflush(output) != 0 || ferror(output);
    if (fclose(output) != 0)
        finalize_failed = true;
    if (finalize_failed && success)
    {
        snprintf(error, error_size, "Could not finalize uploaded file on the SD card.");
        success = false;
    }

    if (success)
    {
        char actual_sha[65];
        digest_to_hex(digest, actual_sha);
        struct stat persisted;
        if (stat(file->staged_path, &persisted) != 0
            || (long long)persisted.st_size != file->size_bytes
            || !equals_ignore_case(actual_sha, file->sha256))
        {
            snprintf(error, error_size, "Uploaded file verification did not match the manifest.");
            success = false;
        }
    }

    clear_receiving_file_state(state);
    if (!success)
    {
        remove(file->staged_path);
        return false;
    }

    file->uploaded = true;
    return true;
}

static void build_file_backup_path(
    char *destination,
    size_t destination_size,
    const char *backup_root,
    int file_index)
{
    char backup_name[32];
    snprintf(backup_name, sizeof(backup_name), "%03d.bak", file_index);
    join_path(destination, destination_size, backup_root, backup_name);
}

static bool rollback_sd_root_commit(
    AssistantState *state,
    AssistantTransfer *transfer,
    const char *backup_root)
{
    bool restored = true;

    for (int i = transfer->file_count - 1; i >= 0; i--)
    {
        AssistantFile *file = &transfer->files[i];
        if (!file->committed)
            continue;

        char final_path[UIF_MAX_PATH];
        join_path(final_path, sizeof(final_path), state->sd_root, file->target_path);
        if (path_exists(final_path) && remove(final_path) != 0)
            restored = false;
        file->committed = false;
    }

    for (int i = transfer->file_count - 1; i >= 0; i--)
    {
        AssistantFile *file = &transfer->files[i];
        if (!file->had_existing_target)
            continue;

        char final_path[UIF_MAX_PATH];
        char backup_path[UIF_MAX_PATH];
        join_path(final_path, sizeof(final_path), state->sd_root, file->target_path);
        build_file_backup_path(backup_path, sizeof(backup_path), backup_root, i);
        if (!ensure_parent_directory(final_path) || rename(backup_path, final_path) != 0)
            restored = false;
        else
            file->had_existing_target = false;
    }

    if (restored)
        remove_tree(backup_root);

    return restored;
}

static bool commit_transfer(AssistantState *state, char *error, size_t error_size)
{
    AssistantTransfer *transfer = &state->transfer;
    if (!transfer->active || !transfer->manifest_loaded)
    {
        snprintf(error, error_size, "No active transfer to commit.");
        return false;
    }

    if (transfer->uploaded_files != transfer->file_count || transfer->uploaded_bytes != transfer->total_bytes)
    {
        snprintf(error, error_size, "Transfer is missing one or more uploaded files.");
        return false;
    }

    if (!validate_transfer_destination_header(transfer, error, error_size))
        return false;

    for (int i = 0; i < transfer->file_count; i++)
    {
        AssistantFile *file = &transfer->files[i];
        char actual_sha[65];
        long long actual_size = 0;
        if (!file->uploaded
            || !validate_transfer_file_destination(transfer, file, error, error_size)
            || !hash_file_sha256(file->staged_path, actual_sha, &actual_size)
            || actual_size != file->size_bytes
            || !equals_ignore_case(actual_sha, file->sha256))
        {
            snprintf(error, error_size, "Staged file verification failed before commit: %s.", file->relative_path);
            return false;
        }
    }

    if (transfer->target_root[0])
    {
        char staged_root[UIF_MAX_PATH];
        char final_root[UIF_MAX_PATH];
        char backup_root[UIF_MAX_PATH];
        char backup_parent[UIF_MAX_PATH];
        join_path(staged_root, sizeof(staged_root), transfer->incoming_root, transfer->target_root);
        join_path(final_root, sizeof(final_root), state->sd_root, transfer->target_root);
        join_path(backup_parent, sizeof(backup_parent), state->sd_root, "uinjectforge/assistant/backups");
        join_path(backup_root, sizeof(backup_root), backup_parent, transfer->transfer_id);

        if (!path_exists(staged_root) || !ensure_parent_directory(final_root) || !mkdir_recursive(backup_parent))
        {
            snprintf(error, error_size, "Could not prepare the final SD destination.");
            return false;
        }

        if (path_exists(backup_root))
        {
            snprintf(
                error,
                error_size,
                "A prior recovery folder still exists at sd:/uinjectforge/assistant/backups/%s. Restore or move it before retrying.",
                transfer->transfer_id);
            return false;
        }

        bool had_existing_root = path_exists(final_root);
        if (had_existing_root && rename(final_root, backup_root) != 0)
        {
            snprintf(error, error_size, "Could not preserve the existing SD destination.");
            return false;
        }

        if (rename(staged_root, final_root) != 0)
        {
            if (had_existing_root && rename(backup_root, final_root) != 0)
            {
                snprintf(
                    error,
                    error_size,
                    "Could not commit or restore the prior SD package. Recovery remains at sd:/uinjectforge/assistant/backups/%s.",
                    transfer->transfer_id);
            }
            else if (had_existing_root)
            {
                snprintf(
                    error,
                    error_size,
                    "Could not commit the validated package; the prior SD package was restored.");
            }
            else
            {
                snprintf(error, error_size, "Could not move the validated package into its final SD destination.");
            }
            return false;
        }

        if (had_existing_root)
            remove_tree(backup_root);
    }
    else
    {
        char backup_parent[UIF_MAX_PATH];
        char backup_name[96];
        char backup_root[UIF_MAX_PATH];
        join_path(backup_parent, sizeof(backup_parent), state->sd_root, "uinjectforge/assistant/backups");
        snprintf(backup_name, sizeof(backup_name), "%s-files", transfer->transfer_id);
        join_path(backup_root, sizeof(backup_root), backup_parent, backup_name);
        if (path_exists(backup_root))
        {
            snprintf(
                error,
                error_size,
                "A prior recovery folder still exists at sd:/uinjectforge/assistant/backups/%s. Restore or move it before retrying.",
                backup_name);
            return false;
        }

        if (!mkdir_recursive(backup_root))
        {
            snprintf(error, error_size, "Could not prepare SD file rollback storage.");
            return false;
        }

        for (int i = 0; i < transfer->file_count; i++)
        {
            AssistantFile *file = &transfer->files[i];
            file->had_existing_target = false;
            file->committed = false;
        }

        for (int i = 0; i < transfer->file_count; i++)
        {
            AssistantFile *file = &transfer->files[i];
            char final_path[UIF_MAX_PATH];
            char backup_path[UIF_MAX_PATH];
            join_path(final_path, sizeof(final_path), state->sd_root, file->target_path);
            build_file_backup_path(backup_path, sizeof(backup_path), backup_root, i);
            if (!ensure_parent_directory(final_path))
            {
                bool restored = rollback_sd_root_commit(state, transfer, backup_root);
                if (restored)
                    snprintf(error, error_size, "Could not create a final SD folder; prior files were restored.");
                else
                    snprintf(
                        error,
                        error_size,
                        "Could not create a final SD folder or fully restore prior files. Recovery copies remain in sd:/uinjectforge/assistant/backups/%s.",
                        backup_name);
                return false;
            }

            if (path_exists(final_path))
            {
                if (rename(final_path, backup_path) != 0)
                {
                    bool restored = rollback_sd_root_commit(state, transfer, backup_root);
                    if (restored)
                        snprintf(error, error_size, "Could not preserve an existing SD file; prior files were restored.");
                    else
                        snprintf(
                            error,
                            error_size,
                            "Could not preserve an existing SD file or fully restore prior files. Recovery copies remain in sd:/uinjectforge/assistant/backups/%s.",
                            backup_name);
                    return false;
                }
                file->had_existing_target = true;
            }
        }

        for (int i = 0; i < transfer->file_count; i++)
        {
            AssistantFile *file = &transfer->files[i];
            char final_path[UIF_MAX_PATH];
            join_path(final_path, sizeof(final_path), state->sd_root, file->target_path);
            if (rename(file->staged_path, final_path) != 0)
            {
                bool restored = rollback_sd_root_commit(state, transfer, backup_root);
                if (restored)
                    snprintf(error, error_size, "Could not commit the validated SD package; prior files were restored.");
                else
                    snprintf(
                        error,
                        error_size,
                        "Could not commit the validated SD package or fully restore prior files. Recovery copies remain in sd:/uinjectforge/assistant/backups/%s.",
                        backup_name);
                return false;
            }
            file->committed = true;
        }

        remove_tree(backup_root);
    }

    remove_tree(transfer->incoming_root);
    transfer->active = false;
    return true;
}

static const char *controller_extension_name(WPADExtensionType extension_type)
{
    switch (extension_type)
    {
        case WPAD_EXT_CORE:
            return "Wii Remote";
        case WPAD_EXT_NUNCHUK:
            return "Wii Remote + Nunchuk";
        case WPAD_EXT_CLASSIC:
            return "Classic Controller";
        case WPAD_EXT_MPLUS:
            return "Wii Remote + MotionPlus";
        case WPAD_EXT_MPLUS_NUNCHUK:
            return "Wii Remote + MotionPlus/Nunchuk";
        case WPAD_EXT_MPLUS_CLASSIC:
            return "Classic Controller + MotionPlus";
        case WPAD_EXT_PRO_CONTROLLER:
            return "Wii U Pro Controller";
        default:
            return "unknown extension";
    }
}

static void build_controller_summary(char *summary, size_t summary_size)
{
    if (!summary || summary_size == 0)
        return;

    static char cached[512];
    static uint64_t checked_at = 0;
    uint64_t now = OSGetTime();
    if (cached[0] && now - checked_at < OSMillisecondsToTicks(2000))
    {
        copy_text(summary, summary_size, cached);
        return;
    }

    size_t offset = 0;
    int connected = 0;
    for (int channel = WPAD_CHAN_0; channel <= WPAD_CHAN_3; channel++)
    {
        WPADExtensionType extension_type = WPAD_EXT_CORE;
        if (WPADProbe((WPADChan)channel, &extension_type) != 0)
            continue;

        append_format(
            summary,
            summary_size,
            &offset,
            "%sP%d %s",
            connected == 0 ? "" : "; ",
            channel + 1,
            controller_extension_name(extension_type));
        connected++;
    }

    if (connected == 0)
        copy_text(summary, summary_size, "No Wii Remote, Classic Controller, or Wii U Pro Controller detected.");
    copy_text(cached, sizeof(cached), summary);
    checked_at = now;
}

static void handle_status(int fd, AssistantState *state)
{
    char controller_summary[512];
    char escaped_controller_summary[768];
    build_controller_summary(controller_summary, sizeof(controller_summary));
    json_escape(controller_summary, escaped_controller_summary, sizeof(escaped_controller_summary));

    char *json = (char *)malloc(UIF_STATUS_JSON_BUFFER);
    if (!json)
    {
        send_message(fd, 500, "Internal Server Error", "Assistant could not allocate its status response.");
        return;
    }
    char status[512];
    char ip_address[128];
    char sd_root[UIF_MAX_PATH + 64];
    char log_path_raw[UIF_MAX_PATH];
    char log_path[UIF_MAX_PATH + 64];
    char last_discovery_peer[128];
    char last_http_request[256];
    char last_http_error[512];
    char receiving_file_path[UIF_MAX_PATH * 2];
    json_escape(state->status, status, sizeof(status));
    json_escape(state->ip_address, ip_address, sizeof(ip_address));
    json_escape(state->sd_root, sd_root, sizeof(sd_root));
    join_path(log_path_raw, sizeof(log_path_raw), state->sd_root, "uinjectforge/assistant/assistant.log");
    json_escape(log_path_raw, log_path, sizeof(log_path));
    json_escape(state->last_discovery_peer, last_discovery_peer, sizeof(last_discovery_peer));
    json_escape(state->last_http_request, last_http_request, sizeof(last_http_request));
    json_escape(state->last_http_error, last_http_error, sizeof(last_http_error));
    json_escape(state->receiving_file_path, receiving_file_path, sizeof(receiving_file_path));
    long long sd_free_bytes = 0;
    long long sd_total_bytes = 0;
    bool sd_space_known = get_cached_sd_space(state, &sd_free_bytes, &sd_total_bytes);
    long long transfer_required_bytes = state->transfer.active
        ? state->transfer.total_bytes + (16LL * 1024LL * 1024LL)
        : 0;
    bool transfer_fits_sd = !state->transfer.active
        || !sd_space_known
        || sd_free_bytes >= transfer_required_bytes;

    char last_transfer_json[8192];
    if (state->last_transfer_result.has_result)
    {
        char result_status[96];
        char result_transfer_id[128];
        char result_package_name[UIF_MAX_TEXT * 2];
        char result_package_kind[128];
        char result_target_root[UIF_MAX_PATH * 2];
        char result_action[128];
        char result_message[UIF_MAX_TEXT * 2];
        char result_failed_file[UIF_MAX_PATH * 2];
        json_escape(state->last_transfer_result.status, result_status, sizeof(result_status));
        json_escape(state->last_transfer_result.transfer_id, result_transfer_id, sizeof(result_transfer_id));
        json_escape(state->last_transfer_result.package_display_name, result_package_name, sizeof(result_package_name));
        json_escape(state->last_transfer_result.package_kind, result_package_kind, sizeof(result_package_kind));
        json_escape(state->last_transfer_result.target_root, result_target_root, sizeof(result_target_root));
        json_escape(state->last_transfer_result.post_transfer_action, result_action, sizeof(result_action));
        json_escape(state->last_transfer_result.message, result_message, sizeof(result_message));
        json_escape(state->last_transfer_result.failed_file, result_failed_file, sizeof(result_failed_file));
        snprintf(
            last_transfer_json,
            sizeof(last_transfer_json),
            "{\"hasResult\":true,\"success\":%s,\"canceled\":%s,\"status\":\"%s\",\"transferId\":\"%s\",\"packageDisplayName\":\"%s\",\"packageKind\":\"%s\",\"targetRoot\":\"%s\",\"postTransferAction\":\"%s\",\"message\":\"%s\",\"failedFile\":\"%s\",\"fileCount\":%d,\"uploadedFiles\":%d,\"totalBytes\":%lld,\"uploadedBytes\":%lld}",
            state->last_transfer_result.success ? "true" : "false",
            state->last_transfer_result.canceled ? "true" : "false",
            result_status,
            result_transfer_id,
            result_package_name,
            result_package_kind,
            result_target_root,
            result_action,
            result_message,
            result_failed_file,
            state->last_transfer_result.file_count,
            state->last_transfer_result.uploaded_files,
            state->last_transfer_result.total_bytes,
            state->last_transfer_result.uploaded_bytes);
    }
    else
    {
        copy_text(last_transfer_json, sizeof(last_transfer_json), "{\"hasResult\":false}");
    }

    char setup_json[2048];
    build_setup_json(state, setup_json, sizeof(setup_json));

    int json_length = snprintf(
        json,
        UIF_STATUS_JSON_BUFFER,
        "{\"message\":\"%s\",\"deviceName\":\"UInjectForge Assistant\",\"protocolVersion\":\"%s\",\"componentVersion\":\"" UIF_COMPONENT_VERSION "\",\"buildId\":\"" UIF_COMPONENT_BUILD_ID "\",\"capabilities\":" UIF_ASSISTANT_CAPABILITIES_JSON ",\"ipAddress\":\"%s\",\"port\":%d,\"broadcastEnabled\":%s,\"passiveBroadcastEnabled\":%s,\"broadcastPort\":%d,\"broadcastSocketReady\":%s,\"paired\":%s,\"activeTransfer\":%s,\"receivingFile\":%s,\"receivingFilePath\":\"%s\",\"receivingFileBytes\":%lld,\"receivingFileTotalBytes\":%lld,\"simpleUi\":%s,\"installEnabled\":%s,\"manualInstallEnabled\":%s,\"autoInstallEnabled\":%s,\"autoInstallSupported\":true,\"sdRoot\":\"%s\",\"sdSpaceKnown\":%s,\"sdFreeBytes\":%lld,\"sdTotalBytes\":%lld,\"transferRequiredBytes\":%lld,\"transferFitsSd\":%s,\"logPath\":\"%s\",\"controllerSummary\":\"%s\",\"discoveryProbeCount\":%u,\"discoveryReplyCount\":%u,\"discoveryBroadcastCount\":%u,\"loopHeartbeatCount\":%u,\"lastDiscoveryPeer\":\"%s\",\"httpRequestCount\":%u,\"httpErrorCount\":%u,\"pairingAttemptCount\":%u,\"pairingFailureCount\":%u,\"lastHttpRequest\":\"%s\",\"lastHttpError\":\"%s\",\"lastTransfer\":%s,\"setup\":%s}",
        status,
        UIF_ASSISTANT_PROTOCOL,
        ip_address,
        state->http_port,
        state->broadcast_enabled ? "true" : "false",
        state->passive_broadcast_enabled ? "true" : "false",
        state->broadcast_port,
        state->broadcast_fd >= 0 ? "true" : "false",
        state->paired ? "true" : "false",
        state->transfer.active ? "true" : "false",
        state->receiving_file ? "true" : "false",
        receiving_file_path,
        state->receiving_file_bytes,
        state->receiving_file_total_bytes,
        state->simple_ui ? "true" : "false",
        state->install_enabled ? "true" : "false",
        state->manual_install_enabled ? "true" : "false",
        state->auto_install_enabled ? "true" : "false",
        sd_root,
        sd_space_known ? "true" : "false",
        sd_free_bytes,
        sd_total_bytes,
        transfer_required_bytes,
        transfer_fits_sd ? "true" : "false",
        log_path,
        escaped_controller_summary,
        state->discovery_probe_count,
        state->discovery_reply_count,
        state->discovery_broadcast_count,
        state->loop_heartbeat_count,
        last_discovery_peer,
        state->http_request_count,
        state->http_error_count,
        state->pairing_attempt_count,
        state->pairing_failure_count,
        last_http_request,
        last_http_error,
        last_transfer_json,
        setup_json);
    if (json_length < 0 || (size_t)json_length >= UIF_STATUS_JSON_BUFFER)
    {
        free(json);
        send_message(fd, 500, "Internal Server Error", "Assistant status exceeded its response limit.");
        return;
    }

    send_json(fd, 200, "OK", json);
    free(json);
}

static void handle_home(int fd, AssistantState *state)
{
    char status[512];
    char sd_root[UIF_MAX_PATH + 64];
    char last_http_error[512];
    char html[8192];
    json_escape(state->status, status, sizeof(status));
    json_escape(state->sd_root, sd_root, sizeof(sd_root));
    json_escape(state->last_http_error, last_http_error, sizeof(last_http_error));

    snprintf(
        html,
        sizeof(html),
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>UInjectForge Assistant</title>"
        "<style>"
        "body{margin:0;background:#090a0f;color:#f4f6ff;font-family:system-ui,-apple-system,Segoe UI,sans-serif;line-height:1.45}"
        "main{max-width:820px;margin:0 auto;padding:28px}"
        "h1{margin:0 0 8px;font-size:32px}"
        ".card{background:#171722;border:1px solid #3c3452;border-radius:10px;padding:18px;margin:16px 0}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}"
        ".metric{background:#26263a;border-radius:8px;padding:12px}"
        ".label{color:#bcc0d2;font-size:13px;text-transform:uppercase;letter-spacing:.04em}"
        ".value{font-size:24px;font-weight:700;word-break:break-word}"
        "a{color:#8d63e0}"
        "code{background:#26263a;padding:2px 5px;border-radius:4px}"
        "</style></head><body><main>"
        "<h1>UInjectForge Assistant</h1>"
        "<p>Receiver for UIF Desktop and mobile transfers.</p>"
        "<section class=\"card\"><div class=\"grid\">"
        "<div class=\"metric\"><div class=\"label\">IP</div><div class=\"value\">%s</div></div>"
        "<div class=\"metric\"><div class=\"label\">Port</div><div class=\"value\">%d</div></div>"
        "<div class=\"metric\"><div class=\"label\">State</div><div class=\"value\">%s</div></div>"
        "</div></section>"
        "<section class=\"card\"><p><strong>Status:</strong> %s</p><p><strong>SD root:</strong> %s</p>"
        "<p><strong>Last HTTP error:</strong> %s</p></section>"
        "<section class=\"card\"><p>Use UIF's Assistant panel with this IP and port. "
        "The pairing code is shown only on the Wii U screen. "
        "For raw checks, open <a href=\"/api/v1/status\"><code>/api/v1/status</code></a>.</p></section>"
        "</main></body></html>",
        state->ip_address[0] ? state->ip_address : "waiting",
        state->http_port,
        state->paired ? "Paired" : "Waiting",
        status,
        sd_root,
        last_http_error[0] ? last_http_error : "None");

    send_html(fd, 200, "OK", html);
}

static void handle_pair(int fd, AssistantState *state, const HttpRequest *request)
{
    uint64_t now = OSGetTime();
    if (state->pairing_retry_after_ticks > now)
    {
        unsigned int remaining_ms = (unsigned int)OSTicksToMilliseconds(state->pairing_retry_after_ticks - now);
        char message[UIF_MAX_TEXT];
        snprintf(message, sizeof(message), "Pairing is temporarily paused after failed attempts. Retry in %u ms.", remaining_ms);
        send_message(fd, 429, "Too Many Requests", message);
        return;
    }

    state->pairing_attempt_count++;
    char *body = NULL;
    if (!read_body_to_memory(fd, request, &body))
    {
        state->pairing_failure_count++;
        set_http_error(state, "Pairing failed: could not read request body.");
        send_message(fd, 400, "Bad Request", "Could not read pairing request.");
        return;
    }

    char pairing_code[32];
    bool has_code = extract_pairing_code_from_body(body, pairing_code, sizeof(pairing_code));
    free(body);

    char expected[16];
    snprintf(expected, sizeof(expected), "%06u", state->pairing_code);
    if (!has_code || !assistant_constant_time_equal(pairing_code, expected))
    {
        state->pairing_failure_count++;
        state->pairing_failure_streak++;
        unsigned int backoff_ms = assistant_pairing_backoff_ms(state->pairing_failure_streak);
        state->pairing_retry_after_ticks = OSGetTime() + OSMillisecondsToTicks(backoff_ms);
        set_http_error(state, "Pairing failed: the submitted code did not match.");
        if (state->pairing_failure_streak >= 5)
        {
            state->pairing_code = create_pairing_code(state);
            state->pairing_failure_streak = 0;
        }
        send_json(fd, 200, "OK", "{\"success\":false,\"message\":\"Pairing code did not match. Recheck the six digits shown on the Wii U screen.\"}");
        return;
    }

    state->paired = true;
    state->pairing_failure_streak = 0;
    state->pairing_retry_after_ticks = 0;
    create_access_token(state);
    copy_text(state->last_http_error, sizeof(state->last_http_error), "");
    set_status(state, "Paired with UIF sender.");

    char json[256];
    snprintf(
        json,
        sizeof(json),
        "{\"success\":true,\"message\":\"Pairing succeeded.\",\"accessToken\":\"%s\"}",
        state->access_token);
    send_json(fd, 200, "OK", json);
}

static void handle_start_transfer(int fd, AssistantState *state, const HttpRequest *request)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before starting a transfer.");
        return;
    }

    char *body = NULL;
    if (!read_body_to_memory(fd, request, &body))
    {
        send_message(fd, 400, "Bad Request", "Could not read transfer manifest.");
        return;
    }

    char error[UIF_MAX_PATH + UIF_MAX_TEXT];
    bool ok = parse_manifest(state, body, error, sizeof(error));
    free(body);

    if (!ok)
    {
        set_status(state, error);
        send_message(fd, 400, "Bad Request", error);
        return;
    }

    char status[UIF_MAX_TEXT];
    snprintf(status, sizeof(status), "Transfer %s ready: %d files.", state->transfer.transfer_id, state->transfer.file_count);
    set_status(state, status);

    char json[256];
    snprintf(
        json,
        sizeof(json),
        "{\"transferId\":\"%s\",\"message\":\"Transfer created.\"}",
        state->transfer.transfer_id);
    send_json(fd, 200, "OK", json);
}

static void handle_upload(int fd, AssistantState *state, const HttpRequest *request, const char *transfer_id, const char *relative_path_encoded)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before uploading files.");
        return;
    }

    AssistantTransfer *transfer = &state->transfer;
    if (!transfer->active || strcmp(transfer->transfer_id, transfer_id) != 0)
    {
        send_message(fd, 404, "Not Found", "Transfer ID is not active.");
        return;
    }

    char relative_path[UIF_MAX_PATH];
    if (!url_decode(relative_path_encoded, relative_path, sizeof(relative_path)) || !safe_relative_path(relative_path))
    {
        send_message(fd, 400, "Bad Request", "Upload path is unsafe.");
        return;
    }

    AssistantFile *file = find_file(transfer, relative_path);
    if (!file)
    {
        send_message(fd, 404, "Not Found", "Uploaded file is not listed in the manifest.");
        return;
    }

    join_path(file->staged_path, sizeof(file->staged_path), transfer->incoming_root, file->target_path);
    bool was_uploaded = file->uploaded;

    char error[UIF_MAX_TEXT];
    if (!receive_file_body(fd, state, request, file, error, sizeof(error)))
    {
        record_transfer_result(state, "failed", false, false, error, file->relative_path);
        set_status(state, error);
        send_message(fd, 400, "Bad Request", error);
        return;
    }

    if (!file->uploaded)
    {
        send_message(fd, 400, "Bad Request", "File upload did not complete.");
        return;
    }

    if (!was_uploaded)
    {
        transfer->uploaded_files++;
        transfer->uploaded_bytes += file->size_bytes;
    }

    char status[UIF_MAX_TEXT];
    char short_path[96];
    copy_text(short_path, sizeof(short_path), file->relative_path);
    snprintf(status, sizeof(status), "Uploaded %d/%d: %s", transfer->uploaded_files, transfer->file_count, short_path);
    set_status(state, status);
    send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"File uploaded.\"}");
}

static bool send_finalized_commit_response(int fd, const AssistantState *state, const char *transfer_id)
{
    if (!state || !transfer_id || !transfer_id[0])
        return false;

    const AssistantTransferResult *result = &state->last_transfer_result;
    if (!result->has_result
        || strcmp(result->transfer_id, transfer_id) != 0
        || !(equals_ignore_case(result->status, "complete")
            || equals_ignore_case(result->status, "ready-to-install")
            || equals_ignore_case(result->status, "install-queued")
            || equals_ignore_case(result->status, "installed")
            || equals_ignore_case(result->status, "install-failed")))
    {
        return false;
    }

    const char *performed_action = "copy-only";
    bool install_supported = false;
    if (equals_ignore_case(result->status, "ready-to-install"))
    {
        performed_action = "prompt-install-on-wiiu";
        install_supported = true;
    }
    else if (equals_ignore_case(result->status, "install-queued")
        || equals_ignore_case(result->status, "installed")
        || equals_ignore_case(result->status, "install-failed"))
    {
        performed_action = "auto-install-queued";
        install_supported = true;
    }

    char escaped_message[UIF_MAX_TEXT * 2];
    json_escape(result->message, escaped_message, sizeof(escaped_message));
    char json[1024];
    snprintf(
        json,
        sizeof(json),
        "{\"success\":true,\"message\":\"%s\",\"installSupported\":%s,\"performedAction\":\"%s\",\"replayed\":true}",
        escaped_message[0] ? escaped_message : "Transfer was already committed.",
        install_supported ? "true" : "false",
        performed_action);
    send_json(fd, 200, "OK", json);
    return true;
}

static void handle_commit(int fd, AssistantState *state, const HttpRequest *request, const char *transfer_id)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before committing a transfer.");
        return;
    }

    if (!state->transfer.active || strcmp(state->transfer.transfer_id, transfer_id) != 0)
    {
        if (send_finalized_commit_response(fd, state, transfer_id))
            return;

        send_message(fd, 404, "Not Found", "Transfer ID is not active.");
        return;
    }

    char error[UIF_MAX_TEXT];
    if (!commit_transfer(state, error, sizeof(error)))
    {
        record_transfer_result(state, "failed", false, false, error, "");
        set_status(state, error);
        send_message(fd, 400, "Bad Request", error);
        return;
    }

    const char *package_name = strrchr(state->transfer.target_root, '/');
    package_name = package_name ? package_name + 1 : state->transfer.target_root;
    if (state->transfer.system_memory_install_required)
        assistant_ui_require_system_memory(package_name);

    if (equals_ignore_case(state->transfer.post_transfer_action, "copy-only"))
    {
        record_transfer_result(state, "complete", true, false, "Transfer committed to final SD destination.", "");
        set_status(state, "Transfer committed to final SD destination.");
        send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed.\",\"installSupported\":false,\"performedAction\":\"copy-only\"}");
        return;
    }

    if (!state->install_enabled)
    {
        record_transfer_result(state, "complete", true, false, "Transfer committed. Install assistance is disabled on this Wii U.", "");
        set_status(state, "Transfer committed. Install assistance is disabled on this Wii U.");
        send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed; install assistance is disabled on the Wii U.\",\"installSupported\":false,\"performedAction\":\"copy-only\"}");
        return;
    }

    if (equals_ignore_case(state->transfer.post_transfer_action, "prompt-install-on-wiiu"))
    {
        if (!state->manual_install_enabled)
        {
            record_transfer_result(state, "complete", true, false, "Transfer committed. Manual installation is disabled on this Wii U.", "");
            set_status(state, "Transfer committed. Manual installation is disabled.");
            send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed; manual installation is disabled on the Wii U.\",\"installSupported\":false,\"performedAction\":\"copy-only\"}");
            return;
        }

        record_transfer_result(state, "ready-to-install", true, false, "Transfer committed. Open Packages on the Wii U to choose an install target.", "");
        set_status(state, "Transfer ready. Open Packages to install it.");
        send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed and is ready on the Packages page.\",\"installSupported\":true,\"performedAction\":\"prompt-install-on-wiiu\"}");
        return;
    }

    if (!state->auto_install_enabled)
    {
        if (state->manual_install_enabled)
        {
            record_transfer_result(state, "ready-to-install", true, false, "Transfer committed. Automatic installation is disabled; open Packages to install it.", "");
            set_status(state, "Transfer ready. Automatic install is disabled; use Packages.");
            send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed; automatic installation is disabled, so the package is ready on the Packages page.\",\"installSupported\":true,\"performedAction\":\"prompt-install-on-wiiu\"}");
        }
        else
        {
            record_transfer_result(state, "complete", true, false, "Transfer committed. Automatic and manual installation are disabled on this Wii U.", "");
            set_status(state, "Transfer committed. Installation is disabled.");
            send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed; installation is disabled on the Wii U.\",\"installSupported\":false,\"performedAction\":\"copy-only\"}");
        }
        return;
    }

    memset(&state->auto_install_action, 0, sizeof(state->auto_install_action));
    state->auto_install_action.type = state->transfer.system_memory_install_required
        || equals_ignore_case(state->transfer.install_target, "nand")
        ? ASSISTANT_UI_ACTION_INSTALL_TO_NAND
        : ASSISTANT_UI_ACTION_INSTALL_TO_USB;
    copy_text(
        state->auto_install_action.package_name,
        sizeof(state->auto_install_action.package_name),
        package_name);
    state->auto_install_pending = true;
    record_transfer_result(state, "install-queued", true, false, "Transfer committed. Automatic installation is queued.", "");
    set_status(state, "Transfer committed. Automatic installation queued.");
    send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer committed; automatic installation queued.\",\"installSupported\":true,\"performedAction\":\"auto-install-queued\"}");
}

static void handle_transfer_status(int fd, AssistantState *state, const HttpRequest *request, const char *transfer_id)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before reading transfer status.");
        return;
    }

    AssistantTransfer *transfer = &state->transfer;
    if (!transfer->active || strcmp(transfer->transfer_id, transfer_id) != 0)
    {
        send_message(fd, 404, "Not Found", "Transfer ID is not active.");
        return;
    }

    char package_display_name[384];
    char package_kind[128];
    char target_root_kind[128];
    char target_root[UIF_MAX_PATH * 2];
    char post_transfer_action[128];
    json_escape(transfer->package_display_name, package_display_name, sizeof(package_display_name));
    json_escape(transfer->package_kind, package_kind, sizeof(package_kind));
    json_escape(transfer->target_root_kind, target_root_kind, sizeof(target_root_kind));
    json_escape(transfer->target_root, target_root, sizeof(target_root));
    json_escape(transfer->post_transfer_action, post_transfer_action, sizeof(post_transfer_action));

    size_t json_capacity = 262144;
    char *json = (char *)malloc(json_capacity);
    if (!json)
    {
        send_message(fd, 500, "Internal Server Error", "Transfer status response allocation failed.");
        return;
    }

    size_t offset = 0;
    append_format(
        json,
        json_capacity,
        &offset,
        "{\"success\":true,\"transferId\":\"%s\",\"packageDisplayName\":\"%s\",\"packageKind\":\"%s\",\"targetRootKind\":\"%s\",\"targetRoot\":\"%s\",\"postTransferAction\":\"%s\",\"fileCount\":%d,\"uploadedFiles\":%d,\"totalBytes\":%lld,\"uploadedBytes\":%lld,\"files\":[",
        transfer->transfer_id,
        package_display_name,
        package_kind,
        target_root_kind,
        target_root,
        post_transfer_action,
        transfer->file_count,
        transfer->uploaded_files,
        transfer->total_bytes,
        transfer->uploaded_bytes);

    bool first = true;
    bool truncated = false;
    for (int i = 0; i < transfer->file_count; i++)
    {
        if (offset + (UIF_MAX_PATH * 2) + 256 >= json_capacity)
        {
            truncated = true;
            break;
        }

        char relative_path[UIF_MAX_PATH * 2];
        char target_path[UIF_MAX_PATH * 2];
        json_escape(transfer->files[i].relative_path, relative_path, sizeof(relative_path));
        json_escape(transfer->files[i].target_path, target_path, sizeof(target_path));
        append_format(
            json,
            json_capacity,
            &offset,
            "%s{\"relativePath\":\"%s\",\"targetPath\":\"%s\",\"sizeBytes\":%lld,\"uploaded\":%s}",
            first ? "" : ",",
            relative_path,
            target_path,
            transfer->files[i].size_bytes,
            transfer->files[i].uploaded ? "true" : "false");
        first = false;
    }

    append_format(json, json_capacity, &offset, "],\"fileStatusTruncated\":%s}", truncated ? "true" : "false");
    send_json(fd, 200, "OK", json);
    free(json);
}

static void handle_cancel_transfer(int fd, AssistantState *state, const HttpRequest *request, const char *transfer_id)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before canceling a transfer.");
        return;
    }

    if (!state->transfer.active || strcmp(state->transfer.transfer_id, transfer_id) != 0)
    {
        if (state->last_transfer_result.has_result
            && strcmp(state->last_transfer_result.transfer_id, transfer_id) == 0)
        {
            send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer was already finalized; no staging data remained to cancel.\"}");
            return;
        }

        send_message(fd, 404, "Not Found", "Transfer ID is not active.");
        return;
    }

    record_transfer_result(state, "canceled", false, true, "Transfer canceled and staging folder removed.", "");
    remove_tree(state->transfer.incoming_root);
    memset(&state->transfer, 0, sizeof(state->transfer));
    set_status(state, "Transfer canceled and staging folder removed.");
    send_json(fd, 200, "OK", "{\"success\":true,\"message\":\"Transfer canceled.\"}");
}

static int append_installed_titles_from_storage(
    char *json,
    size_t json_size,
    size_t *offset,
    const char *storage_target,
    const char *root_path,
    bool *first,
    bool *readable,
    bool *truncated)
{
    if (readable)
        *readable = false;

    DIR *root = opendir(root_path);
    if (!root)
        return 0;

    if (readable)
        *readable = true;

    int count = 0;
    struct dirent *high_entry;
    while ((high_entry = readdir(root)) != NULL)
    {
        if (!is_hex_component_8(high_entry->d_name))
            continue;

        char high_path[UIF_MAX_PATH];
        if (!join_path_checked(high_path, sizeof(high_path), root_path, high_entry->d_name)
            || !path_is_directory(high_path))
        {
            continue;
        }

        DIR *high = opendir(high_path);
        if (!high)
            continue;

        struct dirent *low_entry;
        while ((low_entry = readdir(high)) != NULL)
        {
            if (!is_hex_component_8(low_entry->d_name))
                continue;

            char title_path[UIF_MAX_PATH];
            if (!join_path_checked(title_path, sizeof(title_path), high_path, low_entry->d_name)
                || !path_is_directory(title_path))
            {
                continue;
            }

            char title_id[17];
            snprintf(title_id, sizeof(title_id), "%s%s", high_entry->d_name, low_entry->d_name);

            char path_hint[UIF_MAX_PATH];
            int path_length = snprintf(path_hint, sizeof(path_hint), "%s/", title_path);
            if (path_length < 0 || (size_t)path_length >= sizeof(path_hint))
            {
                if (truncated)
                    *truncated = true;
                break;
            }

            char display_name[UIF_MAX_TEXT];
            char version[64];
            char product_code[64];
            read_installed_title_metadata(
                path_hint,
                title_id,
                display_name,
                sizeof(display_name),
                version,
                sizeof(version),
                product_code,
                sizeof(product_code));

            char escaped_storage[64];
            char escaped_title_id[64];
            char escaped_name[UIF_MAX_TEXT * 2];
            char escaped_version[128];
            char escaped_product_code[128];
            char escaped_path[UIF_MAX_PATH * 2];
            json_escape(storage_target, escaped_storage, sizeof(escaped_storage));
            json_escape(title_id, escaped_title_id, sizeof(escaped_title_id));
            json_escape(display_name, escaped_name, sizeof(escaped_name));
            json_escape(version, escaped_version, sizeof(escaped_version));
            json_escape(product_code, escaped_product_code, sizeof(escaped_product_code));
            json_escape(path_hint, escaped_path, sizeof(escaped_path));

            char entry_json[4096];
            int entry_length = snprintf(
                entry_json,
                sizeof(entry_json),
                "{\"titleId\":\"%s\",\"name\":\"%s\",\"version\":\"%s\",\"productCode\":\"%s\",\"storageTarget\":\"%s\",\"pathHint\":\"%s\"}",
                escaped_title_id,
                escaped_name,
                escaped_version,
                escaped_product_code,
                escaped_storage,
                escaped_path);
            size_t separator_length = *first ? 0U : 1U;
            if (entry_length < 0
                || (size_t)entry_length >= sizeof(entry_json)
                || *offset >= json_size
                || separator_length + (size_t)entry_length + 512U > json_size - *offset)
            {
                if (truncated)
                    *truncated = true;
                break;
            }

            if (!*first)
                json[(*offset)++] = ',';
            memcpy(json + *offset, entry_json, (size_t)entry_length);
            *offset += (size_t)entry_length;
            json[*offset] = '\0';
            *first = false;
            count++;
        }

        closedir(high);
        if (truncated && *truncated)
            break;
    }

    closedir(root);
    return count;
}

typedef struct
{
    char *json;
    size_t capacity;
    size_t *offset;
    bool *first;
    bool nand_readable;
    bool usb_readable;
    int count;
} CatalogJsonContext;

static bool append_catalog_json(const AssistantTitleLocation *title, void *context)
{
    CatalogJsonContext *output = context;
    if (title->usb ? output->usb_readable : output->nand_readable)
        return true;
    char path[512];
    json_escape(title->path, path, sizeof(path));
    char entry[1024];
    int length = snprintf(entry, sizeof(entry),
        "%s{\"titleId\":\"%s\",\"name\":\"%s\",\"version\":\"%u\",\"productCode\":\"\",\"storageTarget\":\"%s\",\"pathHint\":\"%s/\"}",
        *output->first ? "" : ",", title->title_id, title->title_id, title->version,
        title->usb ? "USB" : "NAND / MLC", path);
    if (length < 0 || (size_t)length >= sizeof(entry) || *output->offset + (size_t)length + 512 >= output->capacity)
        return false;
    memcpy(output->json + *output->offset, entry, (size_t)length + 1);
    *output->offset += (size_t)length;
    *output->first = false;
    output->count++;
    return true;
}

static void handle_installed_titles(int fd, AssistantState *state, const HttpRequest *request)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before reading installed titles.");
        return;
    }

    const size_t json_capacity = 262144;
    char *json = (char *)malloc(json_capacity);
    if (!json)
    {
        send_message(fd, 500, "Internal Server Error", "Installed-title response allocation failed.");
        return;
    }

    size_t offset = 0;
    bool first = true;
    bool nand_readable = false;
    bool usb_readable = false;
    bool truncated = false;
    append_format(json, json_capacity, &offset, "{\"success\":true,\"titles\":[");
    int count = 0;
    count += append_installed_titles_from_storage(
        json,
        json_capacity,
        &offset,
        "NAND / MLC",
        "fs:/vol/storage_mlc01/usr/title",
        &first,
        &nand_readable,
        &truncated);
    count += append_installed_titles_from_storage(
        json,
        json_capacity,
        &offset,
        "USB",
        "fs:/vol/storage_usb01/usr/title",
        &first,
        &usb_readable,
        &truncated);

    bool catalog_available = false;
    if ((!nand_readable || !usb_readable) && !truncated)
    {
        CatalogJsonContext catalog = {json, json_capacity, &offset, &first, nand_readable, usb_readable, 0};
        catalog_available = assistant_visit_title_catalog(append_catalog_json, &catalog, &truncated);
        count += catalog.count;
    }
    const char *message = "Installed title locations loaded.";
    if (catalog_available)
        message = "System title locations loaded. Some names require folder access; installation is separate.";
    else if (!nand_readable && !usb_readable)
        message = "Title browsing is restricted. This does not mean installation is unavailable.";
    else if (truncated)
        message = "Installed title locations loaded partially; narrow the installed library before retrying.";
    else if (!nand_readable)
        message = "USB titles loaded; the NAND title root was unavailable.";
    else if (!usb_readable)
        message = "NAND titles loaded; the USB title root was unavailable or no USB storage is attached.";

    append_format(
        json,
        json_capacity,
        &offset,
        "],\"count\":%d,\"nandReadable\":%s,\"usbReadable\":%s,\"truncated\":%s,\"catalogAvailable\":%s,\"message\":\"%s\"}",
        count,
        nand_readable ? "true" : "false",
        usb_readable ? "true" : "false",
        truncated ? "true" : "false",
        catalog_available ? "true" : "false",
        message);
    send_json(fd, 200, "OK", json);
    free(json);
}
static bool queue_title_launch(
    AssistantState *state,
    const char *title_id,
    char *error,
    size_t error_size)
{
    if (!state || !title_id)
    {
        snprintf(error, error_size, "Title launch request is invalid.");
        return false;
    }

    AssistantTelemetryTitle title;
    if (!assistant_telemetry_find_title(title_id, &title))
    {
        snprintf(error, error_size, "Installed title %s was not found on NAND or USB.", title_id);
        return false;
    }

    char *end = NULL;
    uint64_t parsed_title_id = strtoull(title.title_id, &end, 16);
    if (!end || *end != '\0' || !SYSCheckTitleExists(parsed_title_id))
    {
        snprintf(error, error_size, "Wii U no longer reports title %s as installed.", title.title_id);
        return false;
    }

    char message[UIF_MAX_TEXT];
    snprintf(
        message,
        sizeof(message),
        "Launch requested for %.120s. Runtime SHA-256: %.64s",
        title.game_name,
        title.runtime_sha256[0] ? title.runtime_sha256 : "not available");

    AssistantTelemetryEvent event = {
        .event_type = "launch-requested",
        .source = "assistant",
        .title_id = title.title_id,
        .game_name = title.game_name,
        .storage_target = title.storage_target,
        .installed_path = title.installed_path,
        .runtime_path = title.runtime_path,
        .runtime_sha256 = title.runtime_sha256,
        .result = "queued",
        .message = message,
        .correlation_id = NULL,
    };
    char event_id[64];
    if (!assistant_telemetry_write_event(state->sd_root, &event, event_id))
    {
        snprintf(error, error_size, "Launch was not queued because the SD telemetry event could not be written.");
        return false;
    }

    state->title_launch_pending = true;
    state->pending_title_id = parsed_title_id;
    copy_text(state->pending_title_name, sizeof(state->pending_title_name), title.game_name);
    copy_text(state->pending_launch_event_id, sizeof(state->pending_launch_event_id), event_id);
    snprintf(state->status, sizeof(state->status), "Launching %.120s.", title.game_name);
    append_activity(state, state->status);
    return true;
}

static void dispatch_pending_title_launch(AssistantState *state)
{
    if (!state || !state->title_launch_pending)
        return;

    uint64_t title_id = state->pending_title_id;
    char title_id_text[17];
    snprintf(title_id_text, sizeof(title_id_text), "%016llX", (unsigned long long)title_id);

    AssistantTelemetryTitle title;
    memset(&title, 0, sizeof(title));
    assistant_telemetry_find_title(title_id_text, &title);
    AssistantTelemetryEvent event = {
        .event_type = "launch-dispatched",
        .source = "assistant",
        .title_id = title_id_text,
        .game_name = title.game_name[0] ? title.game_name : state->pending_title_name,
        .storage_target = title.storage_target,
        .installed_path = title.installed_path,
        .runtime_path = title.runtime_path,
        .runtime_sha256 = title.runtime_sha256,
        .result = "dispatched",
        .message = "Assistant handed the title to the Wii U launcher. This does not prove runtime startup.",
        .correlation_id = state->pending_launch_event_id,
    };
    assistant_telemetry_write_event(state->sd_root, &event, NULL);

    state->title_launch_pending = false;
    append_persistent_activity_log(state, "Title launch handed to the Wii U launcher.");
    SYSLaunchTitle(title_id);
}

static void handle_events(int fd, AssistantState *state, const HttpRequest *request)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before reading console results.");
        return;
    }

    unsigned int count = 0;
    bool truncated = false;
    char *events = assistant_telemetry_read_events_json(state->sd_root, 64, &count, &truncated);
    if (!events)
    {
        send_message(fd, 500, "Internal Server Error", "Console result response allocation failed.");
        return;
    }

    char event_root_raw[UIF_MAX_PATH];
    char event_root[UIF_MAX_PATH * 2];
    join_path(event_root_raw, sizeof(event_root_raw), state->sd_root, "uinjectforge/telemetry/events");
    json_escape(event_root_raw, event_root, sizeof(event_root));

    size_t capacity = strlen(events) + 2048;
    char *json = (char *)malloc(capacity);
    if (!json)
    {
        free(events);
        send_message(fd, 500, "Internal Server Error", "Console result JSON allocation failed.");
        return;
    }

    snprintf(
        json,
        capacity,
        "{\"success\":true,\"message\":\"Loaded %u console result(s).\",\"eventRoot\":\"%s\",\"events\":%s,\"truncated\":%s}",
        count,
        event_root,
        events,
        truncated ? "true" : "false");
    free(events);
    send_json(fd, 200, "OK", json);
    free(json);
}

static void handle_title_launch(
    int fd,
    AssistantState *state,
    const HttpRequest *request,
    const char *title_id)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before launching an installed title.");
        return;
    }

    if (state->transfer.active || state->auto_install_pending)
    {
        send_message(fd, 409, "Conflict", "Finish the active transfer or install before launching a title.");
        return;
    }

    char error[UIF_MAX_TEXT];
    if (!queue_title_launch(state, title_id, error, sizeof(error)))
    {
        send_message(fd, 400, "Bad Request", error);
        return;
    }

    char json[512];
    snprintf(
        json,
        sizeof(json),
        "{\"success\":true,\"message\":\"Title launch accepted. The Assistant recorded the request before leaving.\",\"titleId\":\"%016llX\",\"eventId\":\"%s\",\"correlationId\":\"%s\"}",
        (unsigned long long)state->pending_title_id,
        state->pending_launch_event_id,
        state->pending_launch_event_id);
    send_json(fd, 202, "Accepted", json);
}
static int count_package_files(const char *package_path, const char *suffix)
{
    int count = 0;
    DIR *dir = opendir(package_path);
    if (!dir)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.' || !ends_with_ignore_case(entry->d_name, suffix))
            continue;

        char candidate[UIF_MAX_PATH];
        join_path(candidate, sizeof(candidate), package_path, entry->d_name);
        if (path_is_regular_file(candidate))
            count++;
    }

    closedir(dir);
    return count;
}

static bool setup_relative_path_exists(const AssistantState *state, const char *relative_path)
{
    char path[UIF_MAX_PATH];
    join_path(path, sizeof(path), state->sd_root, relative_path);
    return path_is_regular_file(path);
}

static const char *PpssppRequiredAssetPaths[] = {
    "flash0/font/ltn0.pgf",
    "flash0/font/jpn0.pgf",
    "flash0/font/kr0.pgf",
    "flash0/font/ltn1.pgf",
    "flash0/font/ltn2.pgf",
    "flash0/font/ltn3.pgf",
    "flash0/font/ltn4.pgf",
    "flash0/font/ltn5.pgf",
    "flash0/font/ltn6.pgf",
    "flash0/font/ltn7.pgf",
    "flash0/font/ltn8.pgf",
    "flash0/font/ltn9.pgf",
    "flash0/font/ltn10.pgf",
    "flash0/font/ltn11.pgf",
    "flash0/font/ltn12.pgf",
    "flash0/font/ltn13.pgf",
    "flash0/font/ltn14.pgf",
    "flash0/font/ltn15.pgf",
    "ui_atlas.zim",
    "ui_atlas.meta",
    "ppge_atlas.zim",
    "ppge_atlas.meta",
    "Roboto-Condensed.ttf",
    "compat.ini",
    "langregion.ini",
    "lang/en_US.ini",
    "shaders/defaultshaders.ini"
};

static bool setup_ppsspp_assets_complete(const AssistantState *state, const char *assets_root)
{
    for (size_t i = 0; i < sizeof(PpssppRequiredAssetPaths) / sizeof(PpssppRequiredAssetPaths[0]); i++)
    {
        char relative_path[UIF_MAX_PATH];
        join_path(relative_path, sizeof(relative_path), assets_root, PpssppRequiredAssetPaths[i]);
        if (!setup_relative_path_exists(state, relative_path))
            return false;
    }

    return true;
}
static bool setup_relative_directory_exists(const AssistantState *state, const char *relative_path)
{
    char path[UIF_MAX_PATH];
    join_path(path, sizeof(path), state->sd_root, relative_path);
    DIR *dir = opendir(path);
    if (!dir)
        return false;

    closedir(dir);
    return true;
}

static bool setup_directory_has_ini(const AssistantState *state, const char *relative_path)
{
    char path[UIF_MAX_PATH];
    join_path(path, sizeof(path), state->sd_root, relative_path);
    DIR *dir = opendir(path);
    if (!dir)
        return false;

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.' && ends_with_ignore_case(entry->d_name, ".ini"))
        {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

static void inspect_setup_install_packages(const AssistantState *state, AssistantSetupReadiness *result)
{
    char install_root[UIF_MAX_PATH];
    join_path(install_root, sizeof(install_root), state->sd_root, "install");
    DIR *root = opendir(install_root);
    if (!root)
        return;

    result->install_folder_present = true;
    int inspected = 0;
    struct dirent *entry;
    while ((entry = readdir(root)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char package_path[UIF_MAX_PATH];
        join_path(package_path, sizeof(package_path), install_root, entry->d_name);
        DIR *package_dir = opendir(package_path);
        if (!package_dir)
            continue;
        closedir(package_dir);

        if (inspected >= UIF_MAX_SETUP_PACKAGES)
        {
            result->install_scan_truncated = true;
            break;
        }

        inspected++;
        char ticket_path[UIF_MAX_PATH];
        char tmd_path[UIF_MAX_PATH];
        char cert_path[UIF_MAX_PATH];
        join_path(ticket_path, sizeof(ticket_path), package_path, "title.tik");
        join_path(tmd_path, sizeof(tmd_path), package_path, "title.tmd");
        join_path(cert_path, sizeof(cert_path), package_path, "title.cert");
        bool valid = path_exists(ticket_path)
            && path_exists(tmd_path)
            && path_exists(cert_path)
            && count_package_files(package_path, ".app") > 0;
        if (valid)
            result->valid_install_packages++;
        else
            result->invalid_install_packages++;
    }

    closedir(root);
}

static AssistantSetupReadiness inspect_setup_readiness(const AssistantState *state)
{
    AssistantSetupReadiness result;
    memset(&result, 0, sizeof(result));
    if (!state || !state->sd_root[0])
        return result;

    result.sd_accessible = setup_relative_directory_exists(state, "");
    if (!result.sd_accessible)
        return result;

    long long total_bytes = 0;
    result.sd_space_known = get_sd_space(state, &result.sd_free_bytes, &total_bytes);
    result.assistant_installed =
        setup_relative_path_exists(state, "wiiu/apps/uinjectforge-assistant/uinjectforge-assistant.rpx") ||
        setup_relative_path_exists(state, "wiiu/apps/uinjectforge-assistant/uinjectforge-assistant.wuhb");
    result.aroma_bridge_installed =
        setup_relative_path_exists(state, "wiiu/environments/aroma/plugins/uinjectforge-aroma-bridge.wps");
    result.ftp_plugin_installed =
        setup_relative_path_exists(state, "wiiu/environments/aroma/plugins/ftpiiu_plugin.wps") ||
        setup_relative_path_exists(state, "wiiu/environments/aroma/plugins/ftpiiu.wps") ||
        setup_relative_path_exists(state, "wiiu/environments/aroma/plugins/ftpiiu_everywhere.wps");
    result.nintendont_installed =
        setup_relative_path_exists(state, "apps/nintendont/boot.dol") ||
        setup_relative_path_exists(state, "apps/Nintendont/boot.dol");
    result.noods_installed =
        setup_relative_path_exists(state, "wiiu/apps/noods/noods.rpx") ||
        setup_relative_path_exists(state, "wiiu/apps/noods/noods.wuhb") ||
        setup_relative_path_exists(state, "wiiu/apps/NooDS/noods.rpx") ||
        setup_relative_path_exists(state, "wiiu/apps/NooDS/noods.wuhb");
    result.ppsspp_runtime_installed =
        setup_relative_path_exists(state, "wiiu/apps/ppsspp/PPSSPP.rpx") ||
        setup_relative_path_exists(state, "wiiu/apps/ppsspp/ppsspp.rpx") ||
        setup_relative_path_exists(state, "ppsspp/PPSSPP.rpx") ||
        setup_relative_path_exists(state, "ppsspp/ppsspp.rpx");
    result.ppsspp_assets_complete =
        setup_ppsspp_assets_complete(state, "wiiu/apps/ppsspp/assets") ||
        setup_ppsspp_assets_complete(state, "ppsspp/assets");
    result.wiistation_installed =
        setup_relative_path_exists(state, "apps/WiiStation/boot.dol") ||
        setup_relative_path_exists(state, "apps/wiistation/boot.dol");
    result.not64_installed =
        ((setup_relative_path_exists(state, "apps/not64/boot.dol") &&
          setup_relative_path_exists(state, "apps/not64/data.iso")) ||
         (setup_relative_path_exists(state, "apps/Not64/boot.dol") &&
          setup_relative_path_exists(state, "apps/Not64/data.iso")));
    result.genesis_plus_gx_installed =
        setup_relative_path_exists(state, "apps/genplus-gx/boot.dol") ||
        setup_relative_path_exists(state, "apps/genplusgx/boot.dol");
    result.controller_config_installed =
        setup_relative_path_exists(state, "controller.ini") ||
        setup_relative_path_exists(state, "apps/nintendont/controller.ini") ||
        setup_relative_path_exists(state, "apps/Nintendont/controller.ini") ||
        setup_directory_has_ini(state, "controllers");
    result.loadiine_folder_present = setup_relative_directory_exists(state, "wiiu/games");
    inspect_setup_install_packages(state, &result);
    return result;
}

static void build_setup_summary(const AssistantSetupReadiness *setup, char *summary, size_t summary_size)
{
    if (!setup->sd_accessible)
    {
        copy_text(summary, summary_size, "SD not accessible");
        return;
    }

    snprintf(
        summary,
        summary_size,
        "SD ready; Assistant %s; FTP %s; Nintendont %s; NooDS %s; PPSSPP %s; vWii presets %d/3; %d valid and %d incomplete install package(s)",
        setup->assistant_installed ? "found" : "missing",
        setup->ftp_plugin_installed ? "found" : "missing",
        setup->nintendont_installed ? "found" : "missing",
        setup->noods_installed ? "found" : "missing",
        setup->ppsspp_runtime_installed && setup->ppsspp_assets_complete ? "ready" : setup->ppsspp_runtime_installed ? "assets incomplete" : "missing",
        (setup->wiistation_installed ? 1 : 0) + (setup->not64_installed ? 1 : 0) + (setup->genesis_plus_gx_installed ? 1 : 0),
        setup->valid_install_packages,
        setup->invalid_install_packages);
}

static void build_setup_json(const AssistantState *state, char *json, size_t json_size)
{
    static AssistantSetupReadiness setup;
    static bool loaded = false;
    static uint64_t checked_at = 0;
    static unsigned int revision = UINT_MAX;
    uint64_t now = OSGetTime();
    if (!loaded || (!state->install_active && !state->transfer.active
        && (revision != g_storage_revision || now - checked_at >= OSMillisecondsToTicks(30000))))
    {
        setup = inspect_setup_readiness(state);
        checked_at = now;
        revision = g_storage_revision;
        loaded = true;
    }
    char summary[512];
    char escaped_summary[768];
    build_setup_summary(&setup, summary, sizeof(summary));
    json_escape(summary, escaped_summary, sizeof(escaped_summary));
    snprintf(
        json,
        json_size,
        "{\"schemaVersion\":1,\"sdAccessible\":%s,\"sdSpaceKnown\":%s,\"sdFreeBytes\":%lld,\"assistantInstalled\":%s,\"aromaBridgeInstalled\":%s,\"ftpPluginInstalled\":%s,\"nintendontInstalled\":%s,\"noodsInstalled\":%s,\"ppssppRuntimeInstalled\":%s,\"ppssppAssetsComplete\":%s,\"wiiStationInstalled\":%s,\"not64Installed\":%s,\"genesisPlusGxInstalled\":%s,\"controllerConfigInstalled\":%s,\"loadiineFolderPresent\":%s,\"installFolderPresent\":%s,\"validInstallPackages\":%d,\"invalidInstallPackages\":%d,\"installScanTruncated\":%s,\"summary\":\"%s\"}",
        setup.sd_accessible ? "true" : "false",
        setup.sd_space_known ? "true" : "false",
        setup.sd_free_bytes,
        setup.assistant_installed ? "true" : "false",
        setup.aroma_bridge_installed ? "true" : "false",
        setup.ftp_plugin_installed ? "true" : "false",
        setup.nintendont_installed ? "true" : "false",
        setup.noods_installed ? "true" : "false",
        setup.ppsspp_runtime_installed ? "true" : "false",
        setup.ppsspp_assets_complete ? "true" : "false",
        setup.wiistation_installed ? "true" : "false",
        setup.not64_installed ? "true" : "false",
        setup.genesis_plus_gx_installed ? "true" : "false",
        setup.controller_config_installed ? "true" : "false",
        setup.loadiine_folder_present ? "true" : "false",
        setup.install_folder_present ? "true" : "false",
        setup.valid_install_packages,
        setup.invalid_install_packages,
        setup.install_scan_truncated ? "true" : "false",
        escaped_summary);
}

static void format_byte_count(long long bytes, char *buffer, size_t buffer_size)
{
    const char *unit = "B";
    double amount = (double)bytes;
    if (bytes >= 1024LL * 1024LL * 1024LL)
    {
        amount /= 1024.0 * 1024.0 * 1024.0;
        unit = "GiB";
    }
    else if (bytes >= 1024LL * 1024LL)
    {
        amount /= 1024.0 * 1024.0;
        unit = "MiB";
    }
    else if (bytes >= 1024LL)
    {
        amount /= 1024.0;
        unit = "KiB";
    }

    if (strcmp(unit, "B") == 0)
        snprintf(buffer, buffer_size, "%lld B", bytes);
    else
        snprintf(buffer, buffer_size, "%.1f %s", amount, unit);
}

static bool is_safe_install_package_name(const char *name)
{
    if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;

    for (const char *cursor = name; *cursor; cursor++)
    {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            return false;
    }

    return true;
}

static bool validate_install_package_for_install(
    const AssistantState *state,
    const char *package_name,
    char *fs_package_path,
    size_t fs_package_path_size,
    char *mcp_package_path,
    size_t mcp_package_path_size,
    char *error,
    size_t error_size)
{
    if (!is_safe_install_package_name(package_name))
    {
        snprintf(error, error_size, "Install blocked: invalid package name.");
        return false;
    }

    char install_root[UIF_MAX_PATH];
    join_path(install_root, sizeof(install_root), state->sd_root, "install");
    join_path(fs_package_path, fs_package_path_size, install_root, package_name);

    DIR *package_dir = opendir(fs_package_path);
    if (!package_dir)
    {
        snprintf(error, error_size, "Install blocked: package folder not found.");
        return false;
    }
    closedir(package_dir);

    char ticket_path[UIF_MAX_PATH];
    char tmd_path[UIF_MAX_PATH];
    char cert_path[UIF_MAX_PATH];
    join_path(ticket_path, sizeof(ticket_path), fs_package_path, "title.tik");
    join_path(tmd_path, sizeof(tmd_path), fs_package_path, "title.tmd");
    join_path(cert_path, sizeof(cert_path), fs_package_path, "title.cert");

    int app_count = count_package_files(fs_package_path, ".app");
    if (!path_is_regular_file(ticket_path) || !path_is_regular_file(tmd_path) || !path_is_regular_file(cert_path) || app_count == 0)
    {
        snprintf(error, error_size, "Install blocked: WUP package is incomplete.");
        return false;
    }

    snprintf(mcp_package_path, mcp_package_path_size, "/vol/app_sd/install/%s/", package_name);
    if (strlen(mcp_package_path) >= UIF_MCP_INSTALL_PATH_LENGTH)
    {
        snprintf(error, error_size, "Install blocked: package path is too long.");
        return false;
    }

    return true;
}

static bool title_type_can_install(uint32_t title_id_high)
{
    return title_id_high == 0x00050000
        || title_id_high == 0x00050002
        || title_id_high == 0x0005000C
        || title_id_high == 0x0005000E;
}

static const char *install_error_hint(IOSError error)
{
    switch ((uint32_t)error)
    {
        case 0xFFFCFFE9:
            return "USB storage may be disconnected.";
        case 0xFFFBF446:
        case 0xFFFBF43F:
            return "Possible missing or bad title.tik.";
        case 0xFFFBF441:
            return "Ticket may not match this console.";
        case 0xFFFCFFE4:
            return "Target storage may be full.";
        case 0xFFFFF825:
            return "Possible SD card read/write problem.";
        default:
            break;
    }

    if (((uint32_t)error & 0xFFFF0000u) == 0xFFFB0000u)
        return "Verify WUP files and sigpatches.";

    return "Check package files and target storage.";
}

static void record_install_telemetry(
    AssistantState *state,
    const char *event_type,
    const AssistantUiAction *action,
    const char *target_text,
    const char *result,
    const char *message,
    const char *correlation_id,
    uint64_t title_id,
    char event_id[64])
{
    if (!state || !action)
        return;

    char title_id_text[17] = {0};
    AssistantTelemetryTitle title;
    memset(&title, 0, sizeof(title));
    if (title_id != 0)
    {
        snprintf(title_id_text, sizeof(title_id_text), "%016llX", (unsigned long long)title_id);
        assistant_telemetry_find_title(title_id_text, &title);
    }

    char package_display_name[80];
    copy_display_name(package_display_name, sizeof(package_display_name), action->package_name);
    AssistantTelemetryEvent event = {
        .event_type = event_type,
        .source = "assistant",
        .title_id = title_id_text,
        .game_name = title.game_name[0] ? title.game_name : package_display_name,
        .storage_target = title.storage_target[0] ? title.storage_target : target_text,
        .installed_path = title.installed_path,
        .runtime_path = title.runtime_path,
        .runtime_sha256 = title.runtime_sha256,
        .result = result,
        .message = message,
        .correlation_id = correlation_id,
    };
    assistant_telemetry_write_event(state->sd_root, &event, event_id);
}
static bool perform_install_action(AssistantState *state, const AssistantUiAction *action, bool automatic)
{
    if (!state || !action || action->type == ASSISTANT_UI_ACTION_NONE)
        return false;

    if (!state->install_enabled
        || (automatic && !state->auto_install_enabled)
        || (!automatic && !state->manual_install_enabled))
    {
        set_status(
            state,
            automatic
                ? "Automatic install is disabled in config.json. The package remains on SD."
                : "Manual install is disabled in config.json.");
        return false;
    }

    bool to_usb = action->type == ASSISTANT_UI_ACTION_INSTALL_TO_USB;
    const char *target_text = to_usb ? "USB" : "NAND";
    char package_display_name[80];
    copy_display_name(package_display_name, sizeof(package_display_name), action->package_name);
    char fs_package_path[UIF_MAX_PATH];
    char mcp_package_path[UIF_MAX_PATH];
    char error[UIF_MAX_TEXT];
    char status[UIF_MAX_TEXT];
    char install_event_id[64] = {0};
    snprintf(status, sizeof(status), "Installing %s to %s.", package_display_name, target_text);
    record_install_telemetry(
        state,
        "install-start",
        action,
        target_text,
        "started",
        status,
        NULL,
        0,
        install_event_id);

    if (!validate_install_package_for_install(
            state,
            action->package_name,
            fs_package_path,
            sizeof(fs_package_path),
            mcp_package_path,
            sizeof(mcp_package_path),
            error,
            sizeof(error)))
    {
        set_status(state, error);
        record_install_telemetry(
            state,
            "install-failed",
            action,
            target_text,
            "failed",
            error,
            install_event_id,
            0,
            NULL);
        return false;
    }

    bool home_was_enabled = OSIsHomeButtonMenuEnabled();
    OSEnableHomeButtonMenu(false);
    state->install_active = true;
    state->install_done_bytes = 0;
    state->install_total_bytes = 0;
    assistant_ui_set_install_active(true);
    set_status(state, status);
    render_assistant_screen(state);

    bool install_succeeded = false;
    bool async_submitted = false;
    bool abort_requested = false;
    uint64_t observed_title_id = 0;
    MCPError handle = MCP_Open();
    if (handle < 0)
    {
        snprintf(status, sizeof(status), "Install failed: MCP open returned %d.", handle);
        set_status(state, status);
        record_install_telemetry(
            state,
            "install-failed",
            action,
            target_text,
            "failed",
            status,
            install_event_id,
            0,
            NULL);
        state->install_active = false;
        assistant_ui_set_install_active(false);
        OSEnableHomeButtonMenu(home_was_enabled);
        return false;
    }

    MCPInstallInfo *install_info = (MCPInstallInfo *)OSAllocFromSystem(sizeof(MCPInstallInfo), 0x40);
    MCPInstallProgress *progress = (MCPInstallProgress *)OSAllocFromSystem(sizeof(MCPInstallProgress), 0x40);
    IOSVec *path_vector = (IOSVec *)OSAllocFromSystem(sizeof(IOSVec), 0x40);
    char *install_path = (char *)OSAllocFromSystem(UIF_MCP_INSTALL_PATH_LENGTH, 0x40);
    uint32_t *callback_context = (uint32_t *)OSAllocFromSystem(0x24, 0x40);

    if (!install_info || !progress || !path_vector || !install_path || !callback_context)
    {
        set_status(state, "Install failed: could not allocate MCP buffers.");
        goto cleanup;
    }

    memset(install_info, 0, sizeof(MCPInstallInfo));
    MCPError result = MCP_InstallGetInfo(handle, mcp_package_path, install_info);
    if (result != 0)
    {
        snprintf(status, sizeof(status), "Install failed: MCP_InstallGetInfo %d.", result);
        set_status(state, status);
        goto cleanup;
    }

    uint32_t *install_words = (uint32_t *)install_info;
    uint32_t title_id_high = install_words[0];
    if (!title_type_can_install(title_id_high))
    {
        snprintf(status, sizeof(status), "Install blocked: unsupported title type %08X.", title_id_high);
        set_status(state, status);
        goto cleanup;
    }

    MCPInstallTarget target = to_usb ? MCP_INSTALL_TARGET_USB : MCP_INSTALL_TARGET_MLC;
    result = MCP_InstallSetTargetDevice(handle, target);
    if (result != 0)
    {
        snprintf(status, sizeof(status), "Install failed: target device %d.", result);
        set_status(state, status);
        goto cleanup;
    }

    result = MCP_InstallSetTargetUsb(handle, target);
    if (result != 0)
    {
        snprintf(status, sizeof(status), "Install failed: target USB %d.", result);
        set_status(state, status);
        goto cleanup;
    }

    memset(callback_context, 0, 0x24);
    callback_context[2] = UIF_MCP_COMMAND_INSTALL_ASYNC;
    callback_context[3] = (uint32_t)path_vector;
    callback_context[4] = 1;
    callback_context[5] = 0;

    memset(install_path, 0, UIF_MCP_INSTALL_PATH_LENGTH);
    copy_text(install_path, UIF_MCP_INSTALL_PATH_LENGTH, mcp_package_path);
    memset(path_vector, 0, sizeof(IOSVec));
    path_vector->vaddr = install_path;
    path_vector->len = UIF_MCP_INSTALL_PATH_LENGTH;

    g_install_completed = false;
    g_install_error = 0;
    result = IOS_IoctlvAsync(handle, UIF_MCP_COMMAND_INSTALL_ASYNC, 1, 0, path_vector, install_callback, callback_context);
    if (result != 0)
    {
        snprintf(status, sizeof(status), "Install failed: IOS_IoctlvAsync %d.", result);
        set_status(state, status);
        goto cleanup;
    }
    async_submitted = true;

    int last_percent = -1;
    uint64_t install_started_ticks = OSGetTime();
    uint64_t last_progress_ticks = install_started_ticks;
    uint64_t last_progress_read_ticks = install_started_ticks;
    uint64_t last_render_ticks = 0;
    uint64_t last_progress_bytes = 0;
    while (!g_install_completed && !g_procui_exit_requested && WHBProcIsRunning())
    {
        memset(progress, 0, sizeof(MCPInstallProgress));
        MCPError progress_result = MCP_InstallGetProgress(handle, progress);
        uint64_t now = OSGetTime();
        if (progress_result == 0)
        {
            state->install_done_bytes = (long long)progress->sizeProgress;
            state->install_total_bytes = (long long)progress->sizeTotal;
            if (progress->tid != 0)
                observed_title_id = progress->tid;
            last_progress_read_ticks = now;
            if (progress->sizeProgress > last_progress_bytes)
            {
                last_progress_bytes = progress->sizeProgress;
                last_progress_ticks = now;
            }
        }

        if (now - install_started_ticks >= OSMillisecondsToTicks(UIF_INSTALL_MAX_RUNTIME_MS))
        {
            MCP_InstallTitleAbort(handle);
            abort_requested = true;
            set_status(state, "Install stopped after 90 minutes. The package remains on SD for a manual retry.");
            goto cleanup;
        }

        if (now - last_progress_ticks >= OSMillisecondsToTicks(UIF_INSTALL_STALL_TIMEOUT_MS)
            || now - last_progress_read_ticks >= OSMillisecondsToTicks(UIF_INSTALL_STALL_TIMEOUT_MS))
        {
            MCP_InstallTitleAbort(handle);
            abort_requested = true;
            set_status(state, "Install stopped after 15 minutes without readable progress. Check storage, package files, and free space.");
            goto cleanup;
        }

        int percent = 0;
        if (progress_result == 0 && progress->sizeTotal > 0)
            percent = (int)((progress->sizeProgress * 100ULL) / progress->sizeTotal);
        if (percent > 100)
            percent = 100;

        if (percent != last_percent || now - last_render_ticks >= OSMillisecondsToTicks(1000))
        {
            if (progress_result == 0)
            {
                char done[32];
                char total[32];
                format_byte_count((long long)progress->sizeProgress, done, sizeof(done));
                format_byte_count((long long)progress->sizeTotal, total, sizeof(total));
                snprintf(status, sizeof(status), "Installing %s to %s: %d%% (%s / %s).", package_display_name, target_text, percent, done, total);
            }
            else
            {
                snprintf(status, sizeof(status), "Installing %s to %s; waiting for progress (%d).", package_display_name, target_text, progress_result);
            }
            copy_text(state->status, sizeof(state->status), status);
            WHBLogPrintf("%s", state->status);
            render_assistant_screen(state);
            last_percent = percent;
            last_render_ticks = now;
        }

        assistant_ui_poll_input();
        OSSleepTicks(OSMillisecondsToTicks(250));
    }

    if (g_procui_exit_requested || !WHBProcIsRunning())
    {
        g_procui_exit_requested = true;
        MCP_InstallTitleAbort(handle);
        abort_requested = true;
        set_status(state, "Install canceled: app is exiting.");
        goto cleanup;
    }

    __sync_synchronize();
    if (g_install_error != 0)
    {
        snprintf(
            status,
            sizeof(status),
            "Install failed: 0x%08X. %s",
            (uint32_t)g_install_error,
            install_error_hint(g_install_error));
        set_status(state, status);
        goto cleanup;
    }

    snprintf(status, sizeof(status), "Installed %s to %s.", package_display_name, target_text);
    set_status(state, status);
    install_succeeded = true;

cleanup:
    copy_text(status, sizeof(status), state->status);
    if (async_submitted && !g_install_completed)
    {
        if (!abort_requested)
            MCP_InstallTitleAbort(handle);
        uint64_t wait_started = OSGetTime();
        uint64_t last_wait_render = 0;
        // Abort requests are asynchronous too. The IOS callback still owns the
        // request buffers until it completes, even after the HOME exit callback.
        while (!g_install_completed)
        {
            if (!g_procui_exit_requested && !WHBProcIsRunning())
                g_procui_exit_requested = true;
            uint64_t now = OSGetTime();
            if (g_procui_exit_requested)
            {
                if (now - wait_started >= OSMillisecondsToTicks(5000))
                    break;
            }
            else
            {
                assistant_ui_poll_input();
                if (now - last_wait_render >= OSMillisecondsToTicks(1000))
                {
                    copy_text(state->status, sizeof(state->status), "Canceling installation. Keep the console on.");
                    render_assistant_screen(state);
                    last_wait_render = now;
                }
            }
            OSSleepTicks(OSMillisecondsToTicks(50));
        }
    }

    bool buffers_can_release = !async_submitted || g_install_completed;
    if (!buffers_can_release)
    {
        // Only an external process-exit request can take this path. Leave the
        // outstanding IOS buffers and handle alive for process reclamation.
        return false;
    }
    __sync_synchronize();
    if (!g_procui_exit_requested)
    {
        copy_text(state->status, sizeof(state->status), status);
        record_install_telemetry(
            state,
            install_succeeded ? "install-complete" : "install-failed",
            action,
            target_text,
            install_succeeded ? "installed" : "failed",
            state->status,
            install_event_id,
            observed_title_id,
            NULL);
    }

    if (callback_context)
        OSFreeToSystem(callback_context);
    if (install_path)
        OSFreeToSystem(install_path);
    if (path_vector)
        OSFreeToSystem(path_vector);
    if (progress)
        OSFreeToSystem(progress);
    if (install_info)
        OSFreeToSystem(install_info);
    MCP_Close(handle);
    state->install_active = false;
    g_storage_revision++;
    assistant_ui_set_install_active(false);
    if (!g_procui_exit_requested)
        OSEnableHomeButtonMenu(home_was_enabled);
    return install_succeeded;
}

static int append_install_packages(char *json, size_t json_size, size_t *offset, const AssistantState *state)
{
    char install_root[UIF_MAX_PATH];
    join_path(install_root, sizeof(install_root), state->sd_root, "install");

    DIR *root = opendir(install_root);
    if (!root)
        return 0;

    int count = 0;
    bool first = true;
    struct dirent *entry;
    while ((entry = readdir(root)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        char package_path[UIF_MAX_PATH];
        join_path(package_path, sizeof(package_path), install_root, entry->d_name);
        DIR *package_dir = opendir(package_path);
        if (!package_dir)
            continue;
        closedir(package_dir);

        char ticket_path[UIF_MAX_PATH];
        char tmd_path[UIF_MAX_PATH];
        char cert_path[UIF_MAX_PATH];
        join_path(ticket_path, sizeof(ticket_path), package_path, "title.tik");
        join_path(tmd_path, sizeof(tmd_path), package_path, "title.tmd");
        join_path(cert_path, sizeof(cert_path), package_path, "title.cert");

        int app_count = count_package_files(package_path, ".app");
        int h3_count = count_package_files(package_path, ".h3");
        bool has_ticket = path_is_regular_file(ticket_path);
        bool has_tmd = path_is_regular_file(tmd_path);
        bool has_cert = path_is_regular_file(cert_path);
        bool looks_installable = has_ticket && has_tmd && has_cert && app_count > 0;
        char missing_required[128];
        size_t missing_offset = 0;
        int missing_count = 0;
        if (!has_ticket)
        {
            append_format(missing_required, sizeof(missing_required), &missing_offset, "%stitle.tik", missing_count == 0 ? "" : ", ");
            missing_count++;
        }
        if (!has_tmd)
        {
            append_format(missing_required, sizeof(missing_required), &missing_offset, "%stitle.tmd", missing_count == 0 ? "" : ", ");
            missing_count++;
        }
        if (!has_cert)
        {
            append_format(missing_required, sizeof(missing_required), &missing_offset, "%stitle.cert", missing_count == 0 ? "" : ", ");
            missing_count++;
        }
        if (app_count == 0)
        {
            append_format(missing_required, sizeof(missing_required), &missing_offset, "%s.app content files", missing_count == 0 ? "" : ", ");
            missing_count++;
        }
        if (missing_count == 0)
            copy_text(missing_required, sizeof(missing_required), "");

        char validation_summary[UIF_MAX_TEXT];
        if (looks_installable)
        {
            snprintf(
                validation_summary,
                sizeof(validation_summary),
                "Ready: title.tik, title.tmd, title.cert, %d app file(s), %d h3 file(s).",
                app_count,
                h3_count);
        }
        else
        {
            snprintf(
                validation_summary,
                sizeof(validation_summary),
                "Incomplete: missing %s.",
                missing_required[0] ? missing_required : "required install files");
        }

        char escaped_name[UIF_MAX_TEXT * 2];
        char sd_path[UIF_MAX_PATH + 64];
        char escaped_path[(UIF_MAX_PATH + 64) * 2];
        char escaped_missing[256];
        char escaped_validation_summary[UIF_MAX_TEXT * 2];
        json_escape(entry->d_name, escaped_name, sizeof(escaped_name));
        snprintf(sd_path, sizeof(sd_path), "sd:/install/%s", entry->d_name);
        json_escape(sd_path, escaped_path, sizeof(escaped_path));
        json_escape(missing_required, escaped_missing, sizeof(escaped_missing));
        json_escape(validation_summary, escaped_validation_summary, sizeof(escaped_validation_summary));

        append_format(
            json,
            json_size,
            offset,
            "%s{\"name\":\"%s\",\"path\":\"%s\",\"hasTicket\":%s,\"hasTmd\":%s,\"hasCert\":%s,\"appCount\":%d,\"h3Count\":%d,\"looksInstallable\":%s,\"validationStatus\":\"%s\",\"requiredMissingCount\":%d,\"missingRequired\":\"%s\",\"validationSummary\":\"%s\"}",
            first ? "" : ",",
            escaped_name,
            escaped_path,
            has_ticket ? "true" : "false",
            has_tmd ? "true" : "false",
            has_cert ? "true" : "false",
            app_count,
            h3_count,
            looks_installable ? "true" : "false",
            looks_installable ? "ready" : "incomplete",
            missing_count,
            escaped_missing,
            escaped_validation_summary);
        first = false;
        count++;

        if (*offset + 512 >= json_size)
            break;
    }

    closedir(root);
    return count;
}

static void handle_install_packages(int fd, AssistantState *state, const HttpRequest *request)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before reading SD install packages.");
        return;
    }

    char install_root[UIF_MAX_PATH];
    char escaped_root[UIF_MAX_PATH * 2];
    join_path(install_root, sizeof(install_root), state->sd_root, "install");
    json_escape(install_root, escaped_root, sizeof(escaped_root));

    char json[32768];
    size_t offset = 0;
    append_format(json, sizeof(json), &offset, "{\"success\":true,\"message\":\"SD install package list loaded.\",\"installRoot\":\"%s\",\"packages\":[", escaped_root);
    int count = append_install_packages(json, sizeof(json), &offset, state);
    append_format(json, sizeof(json), &offset, "],\"count\":%d}", count);
    send_json(fd, 200, "OK", json);
}

static void handle_log(int fd, AssistantState *state, const HttpRequest *request)
{
    if (!authorization_ok(state, request))
    {
        send_message(fd, 401, "Unauthorized", "Assistant pairing is required before reading the Assistant log.");
        return;
    }

    char log_path_raw[UIF_MAX_PATH];
    char log_path[UIF_MAX_PATH * 2];
    join_path(log_path_raw, sizeof(log_path_raw), state->sd_root, "uinjectforge/assistant/assistant.log");
    json_escape(log_path_raw, log_path, sizeof(log_path));

    FILE *file = fopen(log_path_raw, "rb");
    if (!file)
    {
        char json[2048];
        snprintf(
            json,
            sizeof(json),
            "{\"success\":false,\"message\":\"Assistant log was not found.\",\"path\":\"%s\",\"exists\":false,\"bytesRead\":0,\"truncated\":false,\"content\":\"\"}",
            log_path);
        send_json(fd, 200, "OK", json);
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        send_message(fd, 500, "Internal Server Error", "Assistant log could not be read.");
        return;
    }

    long file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        send_message(fd, 500, "Internal Server Error", "Assistant log size could not be read.");
        return;
    }

    long tail_offset = file_size > UIF_LOG_TAIL_BYTES ? file_size - UIF_LOG_TAIL_BYTES : 0;
    if (fseek(file, tail_offset, SEEK_SET) != 0)
    {
        fclose(file);
        send_message(fd, 500, "Internal Server Error", "Assistant log tail could not be read.");
        return;
    }

    size_t max_read = (size_t)(file_size - tail_offset);
    char *content = (char *)malloc(max_read + 1);
    if (!content)
    {
        fclose(file);
        send_message(fd, 500, "Internal Server Error", "Assistant log buffer allocation failed.");
        return;
    }

    size_t bytes_read = fread(content, 1, max_read, file);
    fclose(file);
    content[bytes_read] = '\0';

    size_t escaped_capacity = (bytes_read * 2) + 1;
    if (escaped_capacity < 1)
        escaped_capacity = 1;

    char *escaped_content = (char *)malloc(escaped_capacity);
    if (!escaped_content)
    {
        free(content);
        send_message(fd, 500, "Internal Server Error", "Assistant log JSON buffer allocation failed.");
        return;
    }

    json_escape(content, escaped_content, escaped_capacity);
    free(content);

    size_t json_capacity = escaped_capacity + 2048;
    char *json = (char *)malloc(json_capacity);
    if (!json)
    {
        free(escaped_content);
        send_message(fd, 500, "Internal Server Error", "Assistant log response allocation failed.");
        return;
    }

    snprintf(
        json,
        json_capacity,
        "{\"success\":true,\"message\":\"Assistant log tail loaded.\",\"path\":\"%s\",\"exists\":true,\"bytesRead\":%u,\"truncated\":%s,\"content\":\"%s\"}",
        log_path,
        (unsigned int)bytes_read,
        tail_offset > 0 ? "true" : "false",
        escaped_content);
    free(escaped_content);

    send_json(fd, 200, "OK", json);
    free(json);
}

static void handle_request(int fd, AssistantState *state)
{
    HttpRequest request;
    if (!parse_http_request(fd, &request))
    {
        state->http_request_count++;
        copy_text(state->last_http_request, sizeof(state->last_http_request), "unparsed request");
        set_http_error(state, "HTTP request failed: could not parse request headers.");
        send_message(fd, 400, "Bad Request", "Could not parse HTTP request.");
        return;
    }

    state->http_request_count++;
    snprintf(
        state->last_http_request,
        sizeof(state->last_http_request),
        "%.11s %.100s",
        request.method,
        request.path);

    if (strcmp(request.method, "GET") == 0 && (strcmp(request.path, "/") == 0 || strcmp(request.path, "/index.html") == 0))
    {
        handle_home(fd, state);
        return;
    }

    if (strcmp(request.method, "GET") == 0 && strcmp(request.path, "/api/v1/status") == 0)
    {
        handle_status(fd, state);
        return;
    }

    if (strcmp(request.method, "POST") == 0 && strcmp(request.path, "/api/v1/pair") == 0)
    {
        handle_pair(fd, state, &request);
        return;
    }

    if (strcmp(request.method, "POST") == 0 && strcmp(request.path, "/api/v1/transfers") == 0)
    {
        handle_start_transfer(fd, state, &request);
        return;
    }

    if (strcmp(request.method, "GET") == 0 && strcmp(request.path, "/api/v1/titles") == 0)
    {
        handle_installed_titles(fd, state, &request);
        return;
    }

    if (strcmp(request.method, "GET") == 0 && strcmp(request.path, "/api/v1/events") == 0)
    {
        handle_events(fd, state, &request);
        return;
    }

    const char *title_prefix = "/api/v1/titles/";
    size_t title_prefix_length = strlen(title_prefix);
    if (strcmp(request.method, "POST") == 0
        && strncmp(request.path, title_prefix, title_prefix_length) == 0)
    {
        const char *title_request = request.path + title_prefix_length;
        if (strlen(title_request) == 23 && strcmp(title_request + 16, "/launch") == 0)
        {
            char title_id[17];
            memcpy(title_id, title_request, 16);
            title_id[16] = '\0';
            handle_title_launch(fd, state, &request, title_id);
            return;
        }
    }
    if (strcmp(request.method, "GET") == 0 && strcmp(request.path, "/api/v1/install-packages") == 0)
    {
        handle_install_packages(fd, state, &request);
        return;
    }

    if (strcmp(request.method, "GET") == 0 && strcmp(request.path, "/api/v1/log") == 0)
    {
        handle_log(fd, state, &request);
        return;
    }

    const char *prefix = "/api/v1/transfers/";
    size_t prefix_length = strlen(prefix);
    if (strncmp(request.path, prefix, prefix_length) == 0)
    {
        char *rest = request.path + prefix_length;
        char *files = strstr(rest, "/files/");
        char *commit = strstr(rest, "/commit");
        if (files && strcmp(request.method, "PUT") == 0)
        {
            *files = '\0';
            handle_upload(fd, state, &request, rest, files + strlen("/files/"));
            return;
        }

        if (commit && strcmp(request.method, "POST") == 0 && strcmp(commit, "/commit") == 0)
        {
            *commit = '\0';
            handle_commit(fd, state, &request, rest);
            return;
        }

        if (!strchr(rest, '/') && strcmp(request.method, "GET") == 0)
        {
            handle_transfer_status(fd, state, &request, rest);
            return;
        }

        if (!strchr(rest, '/') && strcmp(request.method, "DELETE") == 0)
        {
            handle_cancel_transfer(fd, state, &request, rest);
            return;
        }
    }

    send_message(fd, 404, "Not Found", "Endpoint not found.");
}

static int start_server_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        close(fd);
        return -1;
    }

    if (listen(fd, 2) < 0)
    {
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

static void set_socket_timeouts(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

#if defined(SO_RCVTIMEO) && defined(SO_SNDTIMEO)
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#else
    (void)fd;
#endif
}

static bool wait_for_client(int server_fd)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(server_fd, &set);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    return select(server_fd + 1, &set, NULL, NULL, &timeout) > 0 && FD_ISSET(server_fd, &set);
}

static int start_broadcast_socket(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
        return -1;

    int enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

static void build_discovery_json(const AssistantState *state, char *json, size_t json_size)
{
    char ip_address[128];
    char setup_json[2048];
    json_escape(state->ip_address, ip_address, sizeof(ip_address));
    build_setup_json(state, setup_json, sizeof(setup_json));
    snprintf(
        json,
        json_size,
        "{\"type\":\"uinjectforge-assistant\",\"deviceName\":\"UInjectForge Assistant\",\"protocolVersion\":\"%s\",\"componentVersion\":\"" UIF_COMPONENT_VERSION "\",\"buildId\":\"" UIF_COMPONENT_BUILD_ID "\",\"capabilities\":" UIF_ASSISTANT_CAPABILITIES_JSON ",\"ipAddress\":\"%s\",\"port\":%d,\"paired\":%s,\"activeTransfer\":%s,\"passiveBroadcastEnabled\":%s,\"simpleUi\":%s,\"installEnabled\":%s,\"manualInstallEnabled\":%s,\"autoInstallEnabled\":%s,\"autoInstallSupported\":true,\"broadcastPort\":%d,\"broadcastSocketReady\":%s,\"setup\":%s}",
        UIF_ASSISTANT_PROTOCOL,
        ip_address,
        state->http_port,
        state->paired ? "true" : "false",
        state->transfer.active ? "true" : "false",
        state->passive_broadcast_enabled ? "true" : "false",
        state->simple_ui ? "true" : "false",
        state->install_enabled ? "true" : "false",
        state->manual_install_enabled ? "true" : "false",
        state->auto_install_enabled ? "true" : "false",
        state->broadcast_port,
        state->broadcast_fd >= 0 ? "true" : "false",
        setup_json);
}

static bool send_discovery_to(AssistantState *state, const struct sockaddr_in *address)
{
    char json[4096];
    build_discovery_json(state, json, sizeof(json));
    return sendto(state->broadcast_fd, json, strlen(json), 0, (const struct sockaddr *)address, sizeof(*address)) >= 0;
}

static void send_discovery_broadcast(AssistantState *state)
{
    if (!state->broadcast_enabled || !state->passive_broadcast_enabled || state->broadcast_fd < 0)
        return;

    uint64_t now = OSGetTime();
    if (state->last_broadcast_ticks != 0
        && now - state->last_broadcast_ticks < OSMillisecondsToTicks(2000))
    {
        return;
    }

    state->last_broadcast_ticks = now;

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    address.sin_port = htons((uint16_t)state->broadcast_port);
    if (send_discovery_to(state, &address))
        state->discovery_broadcast_count++;

    struct in_addr assigned_address;
    if (inet_aton(state->ip_address, &assigned_address))
    {
        uint32_t ip = ntohl(assigned_address.s_addr);
        address.sin_addr.s_addr = htonl((ip & 0xFFFFFF00u) | 0x000000FFu);
        if (send_discovery_to(state, &address))
            state->discovery_broadcast_count++;
    }
}

static bool udp_has_message(int fd)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    return select(fd + 1, &set, NULL, NULL, &timeout) > 0 && FD_ISSET(fd, &set);
}

static void handle_discovery_probe(AssistantState *state)
{
    if (!state->broadcast_enabled || state->broadcast_fd < 0 || !udp_has_message(state->broadcast_fd))
        return;

    char buffer[512];
    struct sockaddr_in remote;
    socklen_t remote_length = sizeof(remote);
    ssize_t received = recvfrom(state->broadcast_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&remote, &remote_length);
    if (received <= 0)
        return;

    buffer[received] = '\0';
    if (!strstr(buffer, "uinjectforge-assistant-discovery-request"))
        return;

    state->discovery_probe_count++;
    copy_text(state->last_discovery_peer, sizeof(state->last_discovery_peer), inet_ntoa(remote.sin_addr));
    if (send_discovery_to(state, &remote))
        state->discovery_reply_count++;

    if (ntohs(remote.sin_port) != (uint16_t)state->broadcast_port)
    {
        remote.sin_port = htons((uint16_t)state->broadcast_port);
        if (send_discovery_to(state, &remote))
            state->discovery_reply_count++;
    }
}

static void resolve_ip_address(AssistantState *state)
{
    uint32_t ip = 0;
    if (NNResult_IsSuccess(ACGetAssignedAddress(&ip)) && ip != 0)
    {
        struct in_addr address;
        address.s_addr = htonl(ip);
        copy_text(state->ip_address, sizeof(state->ip_address), inet_ntoa(address));
    }
    else
    {
        copy_text(state->ip_address, sizeof(state->ip_address), "unknown");
    }
}

static bool initialize_services(AssistantState *state)
{
    memset(state, 0, sizeof(*state));
    state->server_fd = -1;
    state->broadcast_fd = -1;
    state->pairing_code = create_pairing_code(state);
    copy_text(state->status, sizeof(state->status), "Starting receiver.");

    set_status(state, "Mounting SD card.");
    if (!WHBMountSdCard())
    {
        set_status(state, "SD card mount failed.");
        return false;
    }

    copy_text(state->sd_root, sizeof(state->sd_root), WHBGetSdCardMountPath());
    set_status(state, "Loading assistant config.");
    load_assistant_config(state);
    set_status(state, "Checking interrupted transfers.");
    recover_assistant_storage(state);

    set_status(state, "Initializing network.");
    ACInitialize();
    set_status(state, "Connecting network.");
    ACConnect();
    set_status(state, "Resolving IP address.");
    resolve_ip_address(state);
    set_status(state, "Initializing sockets.");
    socket_lib_init();

    set_status(state, "Opening HTTP listener.");
    state->server_fd = start_server_socket(state->http_port);
    if (state->server_fd < 0)
    {
        set_status(state, "HTTP server could not listen on configured port.");
        return false;
    }

    if (state->broadcast_enabled)
        state->broadcast_fd = start_broadcast_socket(state->broadcast_port);

    set_status(state, "Receiver ready.");
    return true;
}

static void shutdown_services(AssistantState *state)
{
    if (state->server_fd >= 0)
    {
        close(state->server_fd);
        state->server_fd = -1;
    }

    if (state->broadcast_fd >= 0)
    {
        close(state->broadcast_fd);
        state->broadcast_fd = -1;
    }
}

static void print_startup_screen(const AssistantState *state)
{
    WHBLogPrintf("UInjectForge Assistant");
    WHBLogPrintf("Version: %s (%s)", UIF_COMPONENT_VERSION, UIF_COMPONENT_BUILD_ID);
    WHBLogPrintf("Protocol: %s", UIF_ASSISTANT_PROTOCOL);
    WHBLogPrintf("IP: %s", state->ip_address);
    WHBLogPrintf("Port: %d", state->http_port);
    WHBLogPrintf("Discovery probe replies: %s on UDP %d", state->broadcast_enabled ? "on" : "off", state->broadcast_port);
    WHBLogPrintf("Passive UDP broadcast: %s", state->passive_broadcast_enabled ? "on" : "off");
    WHBLogPrintf("Pairing code: %06u", state->pairing_code);
    WHBLogPrintf("SD root: %s", state->sd_root);
    WHBLogPrintf("");
    WHBLogPrintf("Open the UIF Assistant panel.");
    WHBLogPrintf("Enter this IP, port, and code.");
    WHBLogPrintf("Use HOME to leave.");
    WHBLogPrintf("Wii Remote/Pro input enabled.");
    WHBLogPrintf("Alive messages appear every 5 seconds.");
    WHBLogPrintf("");
    WHBLogPrintf("This app receives, validates, and can install opted-in packages.");
    WHBLogPrintf("Automatic install runs only after an explicit UIF request.");
    WHBLogPrintf("");
    render_assistant_screen(state);
}

static unsigned int handle_procui_exit(void *context)
{
    (void)context;
    g_procui_exit_requested = true;
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    WHBProcInit();
    ProcUIRegisterCallback(PROCUI_CALLBACK_EXIT, handle_procui_exit, NULL, 100);
    OSEnableHomeButtonMenu(1);
    assistant_screen_init();
    WHBLogPrintf("UInjectForge Assistant %s (%s) starting.", UIF_COMPONENT_VERSION, UIF_COMPONENT_BUILD_ID);
    WHBLogPrintf("If this screen stops changing, note the last startup step shown.");
    VPADInit();
    KPADInit();
    WPADEnableWiiRemote(1);
    WPADEnableURCC(1);

    static AssistantState state;
    bool initialized = initialize_services(&state);
    if (initialized)
    {
        AssistantTelemetryEvent startup_event = {
            .event_type = "assistant-start",
            .source = "assistant",
            .result = "ready",
            .message = "UInjectForge Assistant started and mounted the SD telemetry folder.",
        };
        assistant_telemetry_write_event(state.sd_root, &startup_event, NULL);
    }
    print_startup_screen(&state);
    uint64_t last_heartbeat_ticks = OSGetTime();

    while (!g_procui_exit_requested)
    {
        if (!WHBProcIsRunning())
        {
            g_procui_exit_requested = true;
            break;
        }

        assistant_ui_poll_input();

        if (initialized)
        {
            AssistantUiAction action;
            if (assistant_ui_take_action(&action))
            {
                if (action.type == ASSISTANT_UI_ACTION_LAUNCH_TITLE)
                {
                    char error[UIF_MAX_TEXT];
                    if (!queue_title_launch(&state, action.title_id, error, sizeof(error)))
                        set_status(&state, error);
                }
                else
                {
                    perform_install_action(&state, &action, false);
                }
                if (g_procui_exit_requested)
                    break;
                render_assistant_screen(&state);
            }
        }

        if (initialized && state.auto_install_pending)
        {
            AssistantUiAction action = state.auto_install_action;
            state.auto_install_pending = false;
            memset(&state.auto_install_action, 0, sizeof(state.auto_install_action));
            bool install_succeeded = perform_install_action(&state, &action, true);
            if (g_procui_exit_requested)
                break;
            if (state.last_transfer_result.has_result
                && equals_ignore_case(state.last_transfer_result.post_transfer_action, "auto-install-after-validation"))
            {
                state.last_transfer_result.success = install_succeeded;
                copy_text(
                    state.last_transfer_result.status,
                    sizeof(state.last_transfer_result.status),
                    install_succeeded ? "installed" : "install-failed");
                copy_text(
                    state.last_transfer_result.message,
                    sizeof(state.last_transfer_result.message),
                    state.status);
            }
            render_assistant_screen(&state);
        }

        if (assistant_ui_take_refresh_request())
            render_assistant_screen(&state);

        if (initialized)
        {
            handle_discovery_probe(&state);
            send_discovery_broadcast(&state);
        }

        if (initialized && wait_for_client(state.server_fd))
        {
            struct sockaddr_in client_address;
            socklen_t client_length = sizeof(client_address);
            int client_fd = accept(state.server_fd, (struct sockaddr *)&client_address, &client_length);
            if (client_fd >= 0)
            {
                set_socket_timeouts(client_fd);
                handle_request(client_fd, &state);
                close(client_fd);
                if (g_procui_exit_requested)
                    break;
                render_assistant_screen(&state);
            }
        }

        if (initialized && state.title_launch_pending)
            dispatch_pending_title_launch(&state);

        uint64_t now = OSGetTime();
        if (now - last_heartbeat_ticks >= OSMillisecondsToTicks(5000))
        {
            state.loop_heartbeat_count++;
            WHBLogPrintf(
                "Receiver alive: heartbeat %u, HTTP %u, errors %u, probes %u, replies %u.",
                state.loop_heartbeat_count,
                state.http_request_count,
                state.http_error_count,
                state.discovery_probe_count,
                state.discovery_reply_count);
            last_heartbeat_ticks = now;
            render_assistant_screen(&state);
        }

        OSSleepTicks(OSMillisecondsToTicks(25));
    }

    /*
     * HOME -> Close Software is a ProcUI-owned exit. Releasing sockets,
     * controllers, or SDL here has repeatedly left real consoles frozen on
     * the Wii U Menu screen. Let the system reclaim those process resources,
     * matching the lifecycle that passed the earlier hardware close test.
     * Keep explicit cleanup only for a future non-ProcUI exit path.
     */
    if (!g_procui_exit_requested)
    {
        WHBLogPrintf("Exiting UInjectForge Assistant.");
        copy_text(state.status, sizeof(state.status), "Exiting UInjectForge Assistant.");

        if (initialized)
            shutdown_services(&state);

        KPADShutdown();
        VPADShutdown();
        assistant_screen_shutdown();
    }

    WHBProcShutdown();
    return 0;
}

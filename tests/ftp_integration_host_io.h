#include <assert.h>

static bool g_procui_exit_requested, proc_running = true;
static uint64_t fake_ticks;
static unsigned int install_ticks, controls, polls, renders, telemetry_events;
static unsigned int verification_controls, http_body_calls;
static unsigned int g_storage_revision;
static int response_status;
static char response_body[2048];
static struct { AssistantUiAction action; bool automatic; AssistantTransferResult transfer_result; } g_install;

/* Forward declarations retain production function ordering. */
static void join_path(char *, size_t, const char *, const char *);
static bool mkdir_recursive(const char *);
static void stop_ftp_transfer(AssistantState *);
static uint64_t OSGetTime(void) { return fake_ticks += 100; }
static uint64_t OSTicksToMilliseconds(uint64_t value) { return value; }
static uint64_t OSMillisecondsToTicks(uint64_t value) { return value; }
static bool WHBProcIsRunning(void) { return proc_running; }
static void WHBLogPrintf(const char *format, ...) { (void)format; }
static void update_power_guard(const AssistantState *state, bool exiting) { (void)state; (void)exiting; }
static bool has_sd_space_for_transfer(const AssistantState *state, long long bytes) { (void)state; return bytes >= 0; }
static void set_status(AssistantState *state, const char *text) { snprintf(state->status, sizeof(state->status), "%s", text); }
static void append_activity(AssistantState *state, const char *message) { (void)state; (void)message; }
static void render_assistant_screen(const AssistantState *state) { (void)state; renders++; }
bool assistant_ui_poll_input(void) { polls++; return false; }
void assistant_ui_require_system_memory(const char *name) { (void)name; }
static void tick_install(AssistantState *state)
{
    assert(state->verifying_files);
    install_ticks++;
    if (state->install_active) {
        assert(!strcmp(g_install.action.package_name, "OldOwnedPackage"));
        assert(!strcmp(g_install.transfer_result.transfer_id, "OLD_INSTALL"));
    }
}
static void service_install_status_request(AssistantState *state)
{
    controls++;
    assert(state->verifying_files && !state->ftp_server && !state->ftp_file && !state->ftp_entry);
    assert(assistant_request_allowed_during_install("GET", "/api/v1/status"));
    assert(!assistant_request_allowed_during_install("DELETE", "/api/v1/transfers/current"));
    assert(!assistant_request_allowed_during_install("POST", "/api/v1/transfers/current/commit"));
    verification_controls++;
}
bool assistant_telemetry_write_event(const char *root, const AssistantTelemetryEvent *event, char event_id[64])
{ (void)root; (void)event_id; assert(!strcmp(event->event_type, "transfer-result")); telemetry_events++; return true; }
static void send_json(int fd, int status, const char *reason, const char *body)
{ (void)fd; (void)reason; response_status = status; assert(strlen(body) < sizeof(response_body)); strcpy(response_body, body); }
static void send_message(int fd, int status, const char *reason, const char *body) { send_json(fd, status, reason, body); }
static bool receive_file_body(int fd, AssistantState *state, const HttpRequest *request,
    AssistantFile *file, char *error, size_t size)
{
    (void)fd; (void)state; (void)request; (void)file; (void)error; (void)size;
    http_body_calls++; return false;
}

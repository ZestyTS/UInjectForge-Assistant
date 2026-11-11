#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool g_procui_exit_requested, proc_running = true;
static uint64_t fake_ticks;
static uint64_t fake_tick_step = 1;
static unsigned int polls, install_ticks, controls, discovery, broadcasts, renders;
static char timing_message[UIF_MAX_TEXT];

static uint64_t OSGetTime(void) { fake_ticks += fake_tick_step; return fake_ticks; }
static uint64_t OSTicksToMilliseconds(uint64_t ticks) { return ticks; }
static uint64_t OSMillisecondsToTicks(uint64_t ms) { return ms; }
static bool WHBProcIsRunning(void) { return proc_running; }
static void WHBLogPrintf(const char *format, ...) { (void)format; }
static void append_activity(AssistantState *state, const char *message)
{
    (void)state;
    snprintf(timing_message, sizeof(timing_message), "%s", message);
}
static bool ensure_parent_directory(const char *path) { return path && *path; }
bool assistant_ui_poll_input(void) { polls++; return false; }
static void tick_install(AssistantState *state) { (void)state; install_ticks++; }
static void service_install_status_request(AssistantState *state) { (void)state; controls++; }
static void handle_discovery_probe(AssistantState *state) { (void)state; discovery++; }
static void send_discovery_broadcast(AssistantState *state) { (void)state; broadcasts++; }
static void render_assistant_screen(const AssistantState *state) { (void)state; renders++; }

/* This header is emitted before the actual upload functions. The runner undefines
   fopen before including the test body, so fixture setup/readback use real stdio. */
static const char *upload_failure_path;
static unsigned int upload_failure_open_count;
static int upload_failure_write_fd = -1;

static FILE *assistant_test_upload_fopen(const char *path, const char *mode)
{
    if (!upload_failure_path || strcmp(path, upload_failure_path) != 0
        || strcmp(mode, "wb") != 0)
        return fopen(path, mode);

    /* Leave a real staging entry for the receiver's failed-upload removal oracle. */
    FILE *placeholder = fopen(path, mode);
    if (!placeholder) return NULL;
    if (fclose(placeholder) != 0) return NULL;

    int descriptors[2];
    if (pipe(descriptors) != 0) return NULL;
    if (close(descriptors[0]) != 0)
    {
        int saved_errno = errno;
        close(descriptors[1]);
        errno = saved_errno;
        return NULL;
    }
    /* Independently establish a real kernel write failure before stdio buffering.
       main() ignores SIGPIPE; the production fwrite/fflush/fclose remain real. */
    errno = 0;
    ssize_t written = write(descriptors[1], "x", 1);
    int write_error = errno;
    assert(written == -1 && write_error == EPIPE);
    FILE *stream = fdopen(descriptors[1], mode);
    if (!stream)
    {
        int saved_errno = errno;
        close(descriptors[1]);
        errno = saved_errno;
        return NULL;
    }
    upload_failure_open_count++;
    upload_failure_write_fd = descriptors[1];
    return stream;
}

#define fopen assistant_test_upload_fopen

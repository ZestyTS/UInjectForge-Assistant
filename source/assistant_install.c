#include "assistant_install.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

void assistant_install_begin(AssistantInstallLifecycle *job, uint64_t now_ms)
{
    *job = (AssistantInstallLifecycle){.started_ms = now_ms, .last_progress_ms = now_ms,
        .last_read_ms = now_ms, .last_poll_ms = now_ms};
}
void assistant_install_progress(AssistantInstallLifecycle *job, uint64_t now_ms, uint64_t bytes)
{
    job->last_read_ms = now_ms;
    if (bytes > job->bytes) {
        job->bytes = bytes;
        job->last_progress_ms = now_ms;
    }
}
AssistantInstallStep assistant_install_step(AssistantInstallLifecycle *job, uint64_t now_ms, bool completed, bool exiting)
{
    // Even an aborted request retains its IOS buffers until the callback completes.
    if (completed) return ASSISTANT_INSTALL_FINISH;
    if (job->abort_requested) return ASSISTANT_INSTALL_WAIT;
    if (exiting || now_ms - job->started_ms >= UIF_INSTALL_MAX_RUNTIME_MS
        || now_ms - job->last_progress_ms >= UIF_INSTALL_STALL_TIMEOUT_MS
        || now_ms - job->last_read_ms >= UIF_INSTALL_STALL_TIMEOUT_MS) {
        job->abort_requested = true;
        return ASSISTANT_INSTALL_ABORT;
    }
    if (now_ms - job->last_poll_ms < 250) return ASSISTANT_INSTALL_WAIT;
    job->last_poll_ms = now_ms;
    return ASSISTANT_INSTALL_POLL;
}
static bool same_name(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return *a == *b;
}
bool assistant_title_launch_allowed(bool transfer_active, bool install_active, bool install_pending)
{
    return !transfer_active && !install_active && !install_pending;
}
bool assistant_install_destination_allowed(const char *active_package, const char *pending_package, const char *kind, const char *destination)
{
    if ((!active_package || !*active_package) && (!pending_package || !*pending_package)) return true;
    // Only isolated install packages can be published while MCP owns a package.
    if (!kind || strcmp(kind, "sd-install-folder") != 0 || !destination) return false;
    if (strlen(destination) < 8) return false;
    const char *prefix = "install/";
    for (size_t i = 0; i < 8; i++)
        if (tolower((unsigned char)destination[i]) != prefix[i]) return false;
    const char *name = destination + 8;
    // Reject nested paths and ambiguous FAT aliases while an install owns its input.
    size_t length = strlen(name);
    if (!length || strchr(name, '/') || strchr(name, '\\') || strchr(name, '~')
        || name[length - 1] == '.' || name[length - 1] == ' ') return false;
    return *name && !same_name(name, active_package) && !same_name(name, pending_package);
}

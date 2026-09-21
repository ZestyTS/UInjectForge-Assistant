#ifndef UIF_ASSISTANT_INSTALL_H
#define UIF_ASSISTANT_INSTALL_H
#include <stdbool.h>
#include <stdint.h>
#define UIF_INSTALL_STALL_TIMEOUT_MS (15ULL * 60 * 1000)
#define UIF_INSTALL_MAX_RUNTIME_MS (90ULL * 60 * 1000)

typedef struct {
    uint64_t started_ms, last_progress_ms, last_read_ms, last_poll_ms, bytes;
    bool abort_requested;
} AssistantInstallLifecycle;
typedef enum {
    ASSISTANT_INSTALL_WAIT, ASSISTANT_INSTALL_POLL, ASSISTANT_INSTALL_ABORT,
    ASSISTANT_INSTALL_FINISH
} AssistantInstallStep;

void assistant_install_begin(AssistantInstallLifecycle *job, uint64_t now_ms);
void assistant_install_progress(AssistantInstallLifecycle *job, uint64_t now_ms, uint64_t bytes);
AssistantInstallStep assistant_install_step(AssistantInstallLifecycle *job, uint64_t now_ms, bool completed, bool exiting);
bool assistant_install_destination_allowed(const char *active_package, const char *pending_package, const char *kind, const char *destination);
bool assistant_title_launch_allowed(bool transfer_active, bool install_active, bool install_pending);
#endif

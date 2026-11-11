#include "assistant_power.h"

AssistantPowerResult assistant_power_update(AssistantPowerGuard *guard, bool busy,
    uint64_t now_ms, const AssistantPowerIo *io, void *context)
{
    if (busy) {
        if (guard->busy_period) return ASSISTANT_POWER_UNCHANGED;
        guard->busy_period = true;
        /* A failed restoration still belongs to this lease; retain it if the
           next transfer starts before restoration succeeds. */
        if (guard->owns_disable) return ASSISTANT_POWER_UNCHANGED;
        uint32_t enabled = 0;
        if (io->read_enabled(context, &enabled) < 0 || enabled > 1)
            return ASSISTANT_POWER_READ_FAILED;
        if (!enabled) return ASSISTANT_POWER_UNCHANGED;
        if (io->write_enabled(context, 0) < 0)
            return ASSISTANT_POWER_DISABLE_FAILED;
        guard->owns_disable = true;
        guard->restore_retry_ms = 0;
        return ASSISTANT_POWER_SUSPENDED;
    }

    guard->busy_period = false;
    if (!guard->owns_disable || now_ms < guard->restore_retry_ms)
        return ASSISTANT_POWER_UNCHANGED;
    if (io->write_enabled(context, 1) < 0) {
        guard->restore_retry_ms = now_ms > UINT64_MAX - 5000
            ? UINT64_MAX : now_ms + 5000;
        return ASSISTANT_POWER_RESTORE_FAILED;
    }
    guard->owns_disable = false;
    guard->restore_retry_ms = 0;
    return ASSISTANT_POWER_RESTORED;
}

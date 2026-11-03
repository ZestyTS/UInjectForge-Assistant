#ifndef ASSISTANT_POWER_H
#define ASSISTANT_POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool busy_period;
    bool owns_disable;
    uint64_t restore_retry_ms;
} AssistantPowerGuard;

typedef struct {
    int (*read_enabled)(void *context, uint32_t *enabled);
    int (*write_enabled)(void *context, uint32_t enabled);
} AssistantPowerIo;

typedef enum {
    ASSISTANT_POWER_UNCHANGED,
    ASSISTANT_POWER_SUSPENDED,
    ASSISTANT_POWER_RESTORED,
    ASSISTANT_POWER_READ_FAILED,
    ASSISTANT_POWER_DISABLE_FAILED,
    ASSISTANT_POWER_RESTORE_FAILED
} AssistantPowerResult;

/* The callbacks address only the runtime automatic-power-down enable bit. */
AssistantPowerResult assistant_power_update(AssistantPowerGuard *guard, bool busy,
    uint64_t now_ms, const AssistantPowerIo *io, void *context);

#endif

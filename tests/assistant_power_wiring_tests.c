#include "assistant_power.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    struct { bool active; } transfer;
    bool receiving_file, install_active, auto_install_pending;
} AssistantState;

enum { IM_PARAMETER_APD_ENABLED = 3 };
static uint32_t apd;
static unsigned reads, writes, logs;
static AssistantPowerGuard g_power_guard;

static int IM_GetRuntimeParameter(int parameter, uint32_t *enabled)
{
    assert(parameter == IM_PARAMETER_APD_ENABLED);
    reads++;
    *enabled = apd;
    return 0;
}

static int IM_SetRuntimeParameter(int parameter, uint32_t enabled)
{
    assert(parameter == IM_PARAMETER_APD_ENABLED && enabled <= 1);
    writes++;
    apd = enabled;
    return 0;
}

static uint64_t OSGetTime(void) { return 1000; }
static uint64_t OSTicksToMilliseconds(uint64_t ticks) { return ticks; }
static void WHBLogPrintf(const char *message) { assert(message && *message); logs++; }

#include "power-wiring.inc"

int main(void)
{
    for (unsigned flags = 0; flags < 16; flags++) {
        for (unsigned initially_enabled = 0; initially_enabled < 2; initially_enabled++) {
            AssistantState state = {.transfer = {.active = flags & 1},
                .receiving_file = flags & 2, .install_active = flags & 4,
                .auto_install_pending = flags & 8};
            g_power_guard = (AssistantPowerGuard){0};
            reads = writes = logs = 0;
            apd = initially_enabled;
            update_power_guard(&state, false);
            assert(apd == (flags ? 0 : initially_enabled));
            assert(reads == (flags ? 1U : 0U));
            assert(writes == (flags && initially_enabled ? 1U : 0U));
            update_power_guard(&state, false);
            assert(reads == (flags ? 1U : 0U));
            /* Covers active-transfer HOME exit as well as an idle shutdown. */
            update_power_guard(&state, true);
            assert(apd == initially_enabled);
            assert(writes == (flags && initially_enabled ? 2U : 0U));
            assert(logs == writes);
        }
    }

    /* Upload -> queued install -> installation must be one continuous lease. */
    AssistantState state = {.transfer = {.active = true}};
    g_power_guard = (AssistantPowerGuard){0};
    reads = writes = 0;
    apd = 1;
    update_power_guard(&state, false);
    state.transfer.active = false;
    state.auto_install_pending = true;
    update_power_guard(&state, false);
    state.auto_install_pending = false;
    state.install_active = true;
    update_power_guard(&state, false);
    state.transfer.active = true; /* overlapping second upload */
    update_power_guard(&state, false);
    state.install_active = false;
    update_power_guard(&state, false);
    assert(apd == 0 && reads == 1 && writes == 1);
    state.transfer.active = false; /* completed, canceled, or rejected copy */
    update_power_guard(&state, false);
    assert(apd == 1 && writes == 2);
    puts("Assistant production power adapters and all busy/exit combinations passed.");
    return 0;
}

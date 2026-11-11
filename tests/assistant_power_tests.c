#include "assistant_power.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t enabled;
    unsigned reads, writes;
    int read_error, write_error;
} PowerFixture;

static int read_enabled(void *context, uint32_t *enabled)
{
    PowerFixture *f = context;
    f->reads++;
    *enabled = f->enabled;
    return f->read_error;
}

static int write_enabled(void *context, uint32_t enabled)
{
    PowerFixture *f = context;
    f->writes++;
    if (f->write_error < 0) return f->write_error;
    f->enabled = enabled;
    return 0;
}

static const AssistantPowerIo io = {read_enabled, write_enabled};

int main(void)
{
    AssistantPowerGuard guard = {0};
    PowerFixture f = {.enabled = 1};
    assert(assistant_power_update(&guard, false, 0, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.reads == 0 && f.writes == 0);
    assert(assistant_power_update(&guard, true, 1, &io, &f) == ASSISTANT_POWER_SUSPENDED);
    assert(f.enabled == 0 && guard.owns_disable);
    /* No controller input or repeated IOS calls are needed during a long copy. */
    for (unsigned i = 0; i < 10000; i++)
        assert(assistant_power_update(&guard, true, 2 + i * 1000, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.reads == 1 && f.writes == 1);
    assert(assistant_power_update(&guard, false, 10000002, &io, &f) == ASSISTANT_POWER_RESTORED);
    assert(f.enabled == 1 && !guard.owns_disable && !guard.busy_period);
    assert(assistant_power_update(&guard, false, 10000003, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.writes == 2);

    /* A user's already disabled setting must never be enabled by Assistant. */
    guard = (AssistantPowerGuard){0};
    f = (PowerFixture){0};
    assert(assistant_power_update(&guard, true, 0, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(assistant_power_update(&guard, false, 1, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.enabled == 0 && f.reads == 1 && f.writes == 0);
    f.enabled = 1;
    assert(assistant_power_update(&guard, true, 2, &io, &f) == ASSISTANT_POWER_SUSPENDED);
    assert(assistant_power_update(&guard, false, 3, &io, &f) == ASSISTANT_POWER_RESTORED);

    for (unsigned failure = 0; failure < 3; failure++) {
        guard = (AssistantPowerGuard){0};
        f = (PowerFixture){.enabled = failure == 2 ? 7 : 1,
            .read_error = failure == 0 ? -1 : 0,
            .write_error = failure == 1 ? -1 : 0};
        AssistantPowerResult expected = failure == 1
            ? ASSISTANT_POWER_DISABLE_FAILED : ASSISTANT_POWER_READ_FAILED;
        assert(assistant_power_update(&guard, true, 0, &io, &f) == expected);
        assert(!guard.owns_disable);
        assert(assistant_power_update(&guard, true, 60000, &io, &f) == ASSISTANT_POWER_UNCHANGED);
        assert(assistant_power_update(&guard, false, 60001, &io, &f) == ASSISTANT_POWER_UNCHANGED);
        assert(f.reads == 1 && f.writes == (failure == 1 ? 1U : 0U));
        f.read_error = f.write_error = 0;
        f.enabled = 1;
        assert(assistant_power_update(&guard, true, 60002, &io, &f) == ASSISTANT_POWER_SUSPENDED);
        assert(assistant_power_update(&guard, false, 60003, &io, &f) == ASSISTANT_POWER_RESTORED);
    }

    guard = (AssistantPowerGuard){0};
    f = (PowerFixture){.enabled = 1};
    assert(assistant_power_update(&guard, true, 0, &io, &f) == ASSISTANT_POWER_SUSPENDED);
    f.write_error = -1;
    assert(assistant_power_update(&guard, false, 100, &io, &f) == ASSISTANT_POWER_RESTORE_FAILED);
    assert(guard.owns_disable && guard.restore_retry_ms == 5100);
    for (unsigned i = 101; i < 5100; i++)
        assert(assistant_power_update(&guard, false, i, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.writes == 2);
    assert(assistant_power_update(&guard, false, 5100, &io, &f) == ASSISTANT_POWER_RESTORE_FAILED);
    /* A new busy period retains ownership of the original setting. */
    assert(assistant_power_update(&guard, true, 5101, &io, &f) == ASSISTANT_POWER_UNCHANGED);
    assert(f.reads == 1 && f.writes == 3);
    f.write_error = 0;
    assert(assistant_power_update(&guard, false, 10100, &io, &f) == ASSISTANT_POWER_RESTORED);
    assert(f.enabled == 1 && !guard.owns_disable);

    assert(assistant_power_update(&guard, true, 10101, &io, &f) == ASSISTANT_POWER_SUSPENDED);
    f.write_error = -1;
    assert(assistant_power_update(&guard, false, UINT64_MAX - 1, &io, &f) == ASSISTANT_POWER_RESTORE_FAILED);
    assert(guard.restore_retry_ms == UINT64_MAX);
    f.write_error = 0;
    /* Exit makes a final restore attempt even when idle retry is not yet due. */
    assert(assistant_power_update(&guard, false, UINT64_MAX, &io, &f) == ASSISTANT_POWER_RESTORED);
    puts("Assistant runtime power lease, failures, retry and exit tests passed.");
    return 0;
}

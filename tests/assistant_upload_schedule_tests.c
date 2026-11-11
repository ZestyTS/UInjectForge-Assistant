#include "assistant_upload.h"
#include <assert.h>
#include <stdio.h>

static void owner_cadence(void)
{
    AssistantUploadPump pump = {0};
    /* Startup/idle service must never inherit a prior FTP deadline. */
    for (unsigned ms = 0; ms < 10; ms++)
        assert(assistant_upload_owner_control_due(&pump, false, ms));
    assert(assistant_upload_owner_control_due(&pump, true, 10));
    assistant_upload_owner_control_complete(&pump, true, 50);
    for (unsigned ms = 50; ms < 75; ms++)
        assert(!assistant_upload_owner_control_due(&pump, true, ms));
    assert(assistant_upload_owner_control_due(&pump, true, 75));
    /* Cancel/commit restores idle work immediately; a new transfer services
       ProcUI before its first data pump, even at the same clock value. */
    assert(assistant_upload_owner_control_due(&pump, false, 75));
    assert(assistant_upload_owner_control_due(&pump, true, 75));
    assistant_upload_owner_control_complete(&pump, false, 75);
    assert(assistant_upload_owner_control_due(&pump, true, 75));

    pump = (AssistantUploadPump){0};
    unsigned calls = 0;
    uint64_t last_finished = 0;
    for (uint64_t ms = 0; ms < 60000; ms++) {
        if (!assistant_upload_owner_control_due(&pump, true, ms)) continue;
        assert(!calls || ms - last_finished == 25);
        /* Account for time actually spent inside slow platform service. */
        ms += 7;
        last_finished = ms;
        assistant_upload_owner_control_complete(&pump, true, ms);
        calls++;
    }
    assert(calls == 1875); /* not a tight loop after slow platform service */
    assert(assistant_upload_owner_control_due(&pump, false, 60000));
    assert(assistant_upload_owner_control_due(&pump, false, 60000));
}

int main(void)
{
    owner_cadence();
    AssistantUploadPump pump = {0};
    assert(assistant_upload_control_due(&pump, 0));
    for (unsigned i = 0; i < 25; i++)
        assert(!assistant_upload_control_due(&pump, i));
    assert(assistant_upload_control_due(&pump, 25));
    assert(!assistant_upload_control_due(&pump, 25));
    assistant_upload_control_complete(&pump, 65); /* expensive platform service */
    assert(!assistant_upload_control_due(&pump, 66));
    assert(!assistant_upload_control_due(&pump, 89));
    assert(assistant_upload_control_due(&pump, 90));
    assert(assistant_upload_control_due(&pump, 125)); /* a stalled socket */
    assert(!assistant_upload_control_due(&pump, 126));
    assert(assistant_upload_control_due(&pump, 1)); /* clock reset */
    assert(!assistant_upload_control_due(&pump, 2));
    pump = (AssistantUploadPump){0};
    unsigned calls = 0;
    for (unsigned ms = 0; ms < 60000; ms++)
        calls += assistant_upload_control_due(&pump, ms);
    assert(calls == 2400); /* independent of the amount of incoming data */
    assert(assistant_upload_control_due(&pump, UINT64_MAX));
    assert(!assistant_upload_control_due(&pump, UINT64_MAX));
    puts("Assistant upload cadence, FTP owner transitions, slow service, stalled socket and clock tests passed.");
    return 0;
}

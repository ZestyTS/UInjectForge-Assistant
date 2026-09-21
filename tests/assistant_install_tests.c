#include "assistant_install.h"
#include "assistant_security.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    AssistantInstallLifecycle job;
    for (unsigned int busy = 0; busy < 8; busy++)
        assert(assistant_title_launch_allowed(busy & 1, busy & 2, busy & 4) == (busy == 0));
    assistant_install_begin(&job, 1000);
    assert(assistant_install_step(&job, 1100, false, false) == ASSISTANT_INSTALL_WAIT);
    assert(assistant_install_step(&job, 1250, false, false) == ASSISTANT_INSTALL_POLL);
    // Simulate upload chunks while an asynchronous install is progressing.
    for (unsigned i = 1; i <= 10000; i++) {
        uint64_t now = 1250 + i * 100;
        AssistantInstallStep step = assistant_install_step(&job, now, false, false);
        assert(step == ASSISTANT_INSTALL_POLL || step == ASSISTANT_INSTALL_WAIT);
        if (step == ASSISTANT_INSTALL_POLL) assistant_install_progress(&job, now, i * 1024);
        assert(assistant_request_allowed_with_install("PUT", "/api/v1/transfers/next/files/00000000.app"));
        assert(assistant_request_allowed_during_install("GET", "/api/v1/status"));
        assert(!assistant_request_allowed_during_install("DELETE", "/api/v1/transfers/next"));
    }
    assert(assistant_install_step(&job, 1001300, true, false) == ASSISTANT_INSTALL_FINISH);
    assistant_install_begin(&job, 0);
    assert(assistant_install_step(&job, 900000, false, false) == ASSISTANT_INSTALL_ABORT);
    assert(assistant_install_step(&job, 900001, false, false) == ASSISTANT_INSTALL_WAIT);
    assert(assistant_install_step(&job, 900002, true, false) == ASSISTANT_INSTALL_FINISH);
    assert(job.abort_requested);
    assistant_install_begin(&job, 50);
    assert(assistant_install_step(&job, 51, false, true) == ASSISTANT_INSTALL_ABORT);
    assert(assistant_install_step(&job, 6000, false, true) == ASSISTANT_INSTALL_WAIT);
    assert(assistant_install_step(&job, 6001, true, true) == ASSISTANT_INSTALL_FINISH);
    assistant_install_begin(&job, 0);
    assistant_install_progress(&job, 5400000, 1000);
    assert(assistant_install_step(&job, 5400000, false, false) == ASSISTANT_INSTALL_ABORT);
    assistant_install_begin(&job, 0);
    assistant_install_progress(&job, 899999, 0); // readable but stalled
    assert(assistant_install_step(&job, 900000, false, false) == ASSISTANT_INSTALL_ABORT);
    assert(assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/Second"));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/fIrSt"));
    assert(!assistant_install_destination_allowed(NULL, "Second", "sd-install-folder", "install/Second"));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-root", ""));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/First/Other"));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/First."));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/FIRST~1"));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-install-folder", "install/"));
    assert(!assistant_install_destination_allowed("First", NULL, "sd-loadiine-games-folder", "wiiu/games/Second"));
    assert(assistant_install_destination_allowed(NULL, NULL, "sd-root", ""));
    assert(assistant_request_allowed_with_install("POST", "/api/v1/transfers/next/commit"));
    assert(assistant_request_allowed_with_install("DELETE", "/api/v1/transfers/next"));
    assert(!assistant_request_allowed_with_install("POST", "/api/v1/titles/0005000011111111/launch"));
    assert(!assistant_request_allowed_with_install("GET", "/api/v1/install-packages"));
    assert(!assistant_request_allowed_with_install("POST", "/api/v1/transfers//commit"));
    puts("Assistant install lifecycle, overlap and destination ownership tests passed.");
    return 0;
}

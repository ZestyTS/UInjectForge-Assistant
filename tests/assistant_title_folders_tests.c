#include "assistant_title_folders.h"
#include "title_folders_host_platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int init_error, fsa_error, open_error, unlock_error, close_error, deinit_error;
static int init_calls, deinit_calls, open_calls, unlock_calls, close_calls, stat_calls;
static int fault_call, fault_error, file_call;
static bool second_usb, existing_mlc, existing_usb, existing_usb2;
static bool prepared_context;
static char last_target[128];
int32_t Mocha_InitLibrary(void) { prepared_context = false; init_calls++; return init_error; }
int32_t Mocha_DeInitLibrary(void) { prepared_context = false; deinit_calls++; return deinit_error; }
int32_t FSAInit(void) { return fsa_error; }
FSAClientHandle FSAAddClient(void *p) { assert(!p); open_calls++; return open_error ? open_error : 17; }
int32_t Mocha_UnlockFSClientEx(int32_t c) { assert(c == 17); unlock_calls++; return unlock_error; }
int32_t FSADelClient(FSAClientHandle c) { assert(c == 17); close_calls++; return close_error; }
int32_t FSAGetStat(FSAClientHandle c, const char *path, FSAStat *stat)
{
    assert(c == 17 && unlock_calls == 1 && !unlock_error);
    stat_calls++;
    if (stat_calls == fault_call) return fault_error;
    bool mlc = !strncmp(path, "/vol/storage_mlc01/usr/title", 28);
    bool usb = !strncmp(path, "/vol/storage_usb01/usr/title", 28);
    bool usb2 = !strncmp(path, "/vol/storage_usb02/usr/title", 28);
    assert(mlc || usb || usb2);
    bool root = strlen(path) == strlen("/vol/storage_mlc01/usr/title");
    if (root) {
        if (usb2 && !second_usb) return FS_ERROR_NOT_FOUND;
        stat->flags = stat_calls == file_call ? 0 : FS_STAT_DIRECTORY;
        return FS_ERROR_OK;
    }
    assert(strstr(path, "/usr/title/00050002/") && strlen(path) == 46);
    snprintf(last_target, sizeof(last_target), "%s", path);
    if ((mlc && existing_mlc) || (usb && existing_usb) || (usb2 && existing_usb2)) return FS_ERROR_OK;
    return FS_ERROR_NOT_FOUND;
}
static void reset(void)
{
    init_error = fsa_error = open_error = unlock_error = close_error = deinit_error = 0;
    init_calls = deinit_calls = open_calls = unlock_calls = close_calls = stat_calls = 0;
    fault_call = file_call = -1; fault_error = 0;
    second_usb = existing_mlc = existing_usb = existing_usb2 = false;
    prepared_context = false;
    memset(last_target, 0, sizeof(last_target));
}
static AssistantTitleFolderResult check(void)
{
    AssistantTitleFolderResult result = assistant_check_title_folders("00050002F49B7A5E");
    assert(init_calls == 1);
    assert(deinit_calls == (init_error ? 0 : 1));
    assert(close_calls == ((!init_error && !fsa_error && !open_error) ? 1 : 0));
    return result;
}
int main(void)
{
    const char *invalid[] = {NULL, "", "00050000F49B7A5E", "00050002F49B7A5", "00050002F49B7A5EE",
        "00050002F49B7A5/", "00050002../other", "00050002F49B7A5G", " 0050002F49B7A5E"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        reset(); assert(!assistant_title_folder_id_valid(invalid[i]));
        assert(assistant_check_title_folders(invalid[i]).state == ASSISTANT_TITLE_FOLDERS_UNKNOWN);
        assert(init_calls == 0 && open_calls == 0 && stat_calls == 0);
    }
    assert(assistant_title_folder_id_valid("00050002f49b7a5e"));
    reset(); AssistantTitleFolderResult result = check();
    assert(result.state == ASSISTANT_TITLE_FOLDERS_ABSENT && result.error == 0 && result.checked_locations == 2 && stat_calls == 8);
    assert(!strcmp(last_target, "/vol/storage_usb01/usr/title/00050002/f49b7a5e"));
    reset(); second_usb = true; result = check();
    assert(result.state == ASSISTANT_TITLE_FOLDERS_ABSENT && result.checked_locations == 3 && stat_calls == 9);
    bool *existing[] = {&existing_mlc, &existing_usb, &existing_usb2};
    for (unsigned i = 0; i < 3; ++i) {
        reset(); second_usb = true; *existing[i] = true;
        assert(check().state == ASSISTANT_TITLE_FOLDERS_PRESENT);
    }
    int *failures[] = {&init_error, &fsa_error, &open_error, &unlock_error, &close_error, &deinit_error};
    for (unsigned i = 0; i < 6; ++i) {
        reset(); *failures[i] = -55;
        assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN);
    }
    const int errors[] = {-6, -0x30019, -0x3001a, -0x3001b, -0x30021, -99};
    for (unsigned e = 0; e < sizeof(errors) / sizeof(*errors); ++e) {
        for (int call = 1; call <= 8; ++call) {
            reset(); fault_call = call; fault_error = errors[e];
            result = check(); assert(result.state == ASSISTANT_TITLE_FOLDERS_UNKNOWN && result.error == errors[e]);
        }
    }
    const int required_roots[] = {1, 2, 6, 7};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); fault_call = required_roots[i]; fault_error = FS_ERROR_NOT_FOUND;
        assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN);
        reset(); file_call = required_roots[i];
        assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN);
    }
    reset(); fault_call = 8; fault_error = FS_ERROR_OK;
    assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN); // USB2 appeared mid-check.
    reset(); second_usb = true; fault_call = 9; fault_error = FS_ERROR_NOT_FOUND;
    assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN); // USB2 disappeared.
    reset(); existing_usb = true; close_error = -9;
    assert(check().state == ASSISTANT_TITLE_FOLDERS_PRESENT); // Cleanup cannot turn a collision into absence.
    reset(); existing_usb = true; deinit_error = -9;
    assert(check().state == ASSISTANT_TITLE_FOLDERS_UNKNOWN && stat_calls == 0);
    reset();
    AssistantTitleFolderSession session = {-1, false};
    assert(assistant_open_title_folder_session(&session, &result) && session.ready && deinit_calls == 1);
    assert(!assistant_open_title_folder_session(&session, &result) && session.ready && init_calls == 1);
    prepared_context = true;
    assert(assistant_check_title_folders_in_session("00050002F49B7A5E", &session).state == ASSISTANT_TITLE_FOLDERS_ABSENT);
    assert(prepared_context && init_calls == 1 && deinit_calls == 1 && close_calls == 0);
    existing_usb = true;
    assert(assistant_check_title_folders_in_session("00050002F49B7A5E", &session).state == ASSISTANT_TITLE_FOLDERS_PRESENT);
    assert(prepared_context && init_calls == 1 && deinit_calls == 1 && close_calls == 0);
    assert(assistant_close_title_folder_session(&session) == 0 && !session.ready && close_calls == 1 && prepared_context);
    assert(assistant_close_title_folder_session(&session) == 0 && close_calls == 1);
    assert(assistant_check_title_folders_in_session("00050002F49B7A5E", &session).state == ASSISTANT_TITLE_FOLDERS_UNKNOWN);
    puts("Title-folder metadata checker passed: bounded paths, both required stores, optional USB2, errors, device changes and cleanup.");
    return 0;
}

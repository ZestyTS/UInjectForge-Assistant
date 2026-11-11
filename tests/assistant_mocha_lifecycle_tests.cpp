#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {
#include "assistant_title_folders.h"
#include "title_folders_host_platform.h"
}

typedef int32_t MochaUtilsStatus;
enum IOSOpenMode { IOS_OPEN_READ };
constexpr int32_t MOCHA_RESULT_SUCCESS = 0;
constexpr int32_t MOCHA_RESULT_UNSUPPORTED_COMMAND = -1;
constexpr int32_t INVALID_HANDLE = -0x30025;
#define DEBUG_FUNCTION_LINE_ERR(...) ((void)0)

enum HandleKind { FREE, MOCHA, FSA, MCP };
static HandleKind handles[16] = {};
static unsigned opens, closes, unlocks;

static int allocate(HandleKind kind)
{
    for (int i = 3; i < 16; ++i) {
        if (handles[i] == FREE) { handles[i] = kind; return i; }
    }
    assert(false);
    return -1;
}

static int IOS_Open(const char *path, IOSOpenMode)
{
    assert(!std::strcmp(path, "/dev/iosuhax"));
    ++opens;
    return allocate(MOCHA);
}

static int IOS_Close(int handle)
{
    assert(handle >= 3 && handle < 16 && handles[handle] != FREE);
    handles[handle] = FREE;
    ++closes;
    return 0;
}

static MochaUtilsStatus Mocha_CheckAPIVersion(uint32_t *version)
{
    int client = allocate(MCP);
    *version = 1;
    return IOS_Close(client);
}

#include "mocha-lifecycle.inc"

int32_t FSAInit(void) { return FS_ERROR_OK; }
FSAClientHandle FSAAddClient(void *argument)
{
    assert(!argument);
    return allocate(FSA);
}
int32_t Mocha_UnlockFSClientEx(int32_t client)
{
    assert(client >= 3 && client < 16 && handles[client] == FSA);
    assert(mochaInitDone && mochaApiVersion == 1);
    ++unlocks;
    return 0;
}
int32_t FSADelClient(FSAClientHandle client)
{
    if (client < 3 || client >= 16 || handles[client] != FSA) return INVALID_HANDLE;
    handles[client] = FREE;
    return FS_ERROR_OK;
}
int32_t FSAGetStat(FSAClientHandle client, const char *path, FSAStat *stat)
{
    if (client < 3 || client >= 16 || handles[client] != FSA) return INVALID_HANDLE;
    if (std::strstr(path, "storage_usb02")) return FS_ERROR_NOT_FOUND;
    if (std::strstr(path, "/00050002/")) {
        if (!std::strstr(path, "/f635cb3c")) return FS_ERROR_NOT_FOUND;
    }
    stat->flags = FS_STAT_DIRECTORY;
    return FS_ERROR_OK;
}

int main()
{
    for (unsigned i = 0; i < 1000; ++i) {
        auto present = assistant_check_title_folders("00050002F635CB3C");
        assert(present.state == ASSISTANT_TITLE_FOLDERS_PRESENT && !present.error);
        auto absent = assistant_check_title_folders("00050002FEFFEFBE");
        assert(absent.state == ASSISTANT_TITLE_FOLDERS_ABSENT && !absent.error);
        assert(absent.checked_locations == 2);
        assert(iosuhaxHandle == -1 && !mochaInitDone && !mochaApiVersion);
        for (auto kind : handles) assert(kind == FREE);
    }
    AssistantTitleFolderSession session = {-1, false};
    AssistantTitleFolderResult result = {};
    assert(assistant_open_title_folder_session(&session, &result));
    unsigned initialized_opens = opens, initialized_closes = closes;
    // Late collision checks must not reopen or close any Mocha/MCP handles.
    for (unsigned i = 0; i < 100; ++i) {
        result = assistant_check_title_folders_in_session("00050002FEFFEFBE", &session);
        assert(result.state == ASSISTANT_TITLE_FOLDERS_ABSENT && !result.error);
    }
    assert(opens == initialized_opens && closes == initialized_closes);
    assert(!assistant_close_title_folder_session(&session));
    assert(!assistant_close_title_folder_session(&session));
    for (auto kind : handles) assert(kind == FREE);
    assert(opens == 2001 && unlocks == 2001);
    std::puts("Mocha lifecycle: 2000 repeated checks, 100 retained-session checks, no leaked/closed reused handles.");
}

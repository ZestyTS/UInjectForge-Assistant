#include "assistant_paths.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define ASSISTANT_TELEMETRY_PATH 1024
static char calls[16][ASSISTANT_TELEMETRY_PATH];
static unsigned int call_count;
static const char *denied;
static const char *existing;
static bool mount_exists = true;
static void copy_text(char *out, size_t size, const char *text)
{
    snprintf(out, size, "%s", text);
}
static bool path_is_directory(const char *path)
{
    return mount_exists && (!strcmp(path, "/vol/external01")
        || !strcmp(path, "fs:/vol/external01"));
}
static int fake_mkdir(const char *path, unsigned mode)
{
    (void)mode;
    assert(call_count < 16);
    snprintf(calls[call_count++], ASSISTANT_TELEMETRY_PATH, "%s", path);
    if (!strcmp(path, "/vol") || !strcmp(path, "/vol/external01")
        || !strcmp(path, "fs:/vol") || !strcmp(path, "fs:/vol/external01")
        || !strcmp(path, "sd:")) {
        errno = ENOENT;
        return -1;
    }
    if (denied && !strcmp(path, denied)) {errno = EACCES; return -1;}
    if (existing && !strcmp(path, existing)) {errno = EEXIST; return -1;}
    return 0;
}
#define mkdir fake_mkdir
#include UIF_TELEMETRY_DIRECTORY_FUNCTION
#undef mkdir
static void reset(void)
{
    call_count = 0; denied = NULL; existing = NULL; mount_exists = true;
    memset(calls, 0, sizeof(calls));
}
int main(void)
{
    reset();
    assert(path_is_directory("/vol/external01"));
    assert(!path_is_directory("/vol"));
    assert(mkdir_recursive("/vol/external01/uinjectforge/telemetry/events"));
    assert(call_count == 3);
    assert(!strcmp(calls[0], "/vol/external01/uinjectforge"));
    assert(!strcmp(calls[2], "/vol/external01/uinjectforge/telemetry/events"));
    reset();
    assert(mkdir_recursive("fs:/vol/external01/uinjectforge/telemetry/events"));
    assert(call_count == 3 && !strcmp(calls[0], "fs:/vol/external01/uinjectforge"));
    reset();
    assert(mkdir_recursive("sd:/uinjectforge/telemetry/events"));
    assert(call_count == 3 && !strcmp(calls[0], "sd:/uinjectforge"));
    reset();
    assert(mkdir_recursive("/vol/external01") && call_count == 0);
    assert(mkdir_recursive("fs:/vol/external01") && call_count == 0);
    mount_exists = false;
    assert(!mkdir_recursive("/vol/external01") && call_count == 0);
    reset(); existing = "/vol/external01/uinjectforge";
    assert(mkdir_recursive("/vol/external01/uinjectforge/telemetry/events"));
    assert(call_count == 3);
    reset(); denied = "/vol/external01/uinjectforge";
    assert(!mkdir_recursive("/vol/external01/uinjectforge/telemetry/events"));
    assert(call_count == 1);
    reset();
    assert(mkdir_recursive("relative/events") && call_count == 2);
    reset();
    assert(!mkdir_recursive(NULL) && !mkdir_recursive("") && call_count == 0);
    puts("Production telemetry mount-root directory tests passed.");
    return 0;
}

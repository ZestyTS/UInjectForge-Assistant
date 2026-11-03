#include "assistant_storage.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <malloc.h>
#ifdef UIF_ASSISTANT_HOST_TEST
#include "assistant_test_platform.h"
#else
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#endif

static const char *catalog_stage = "not-requested";
static int32_t catalog_error;
static AssistantCatalogDiagnostics catalog_diagnostics = {-1, -1, -1, 0, 0};
const char *assistant_catalog_stage(void) { return catalog_stage; }
int32_t assistant_catalog_error(void) { return catalog_error; }
AssistantCatalogDiagnostics assistant_catalog_diagnostics(void) { return catalog_diagnostics; }

static void release_catalog(MCPTitleListType *titles, bool system_allocation)
{
    if (system_allocation) OSFreeToSystem(titles); else free(titles);
}

bool assistant_visit_title_catalog(AssistantTitleVisitor visitor, void *context, bool *truncated)
{
    catalog_stage = "invalid-visitor";
    catalog_error = 0;
    catalog_diagnostics = (AssistantCatalogDiagnostics){-1, -1, -1, 0, 0};
    if (truncated)
        *truncated = false;
    if (!visitor)
        return false;

    MCPError handle = MCP_Open();
    if (handle < 0)
    {
        catalog_stage = "open";
        catalog_error = handle;
        return false;
    }

    MCPTitleListType *titles = NULL;
    bool system_allocation = false;
    uint32_t actual = 0;
    bool stable = false;
    // Count and list are separate IPC calls. Retry a bounded number of times
    // when the library changes, but never publish a partial snapshot as complete.
    for (uint32_t attempt = 1; attempt <= 3; ++attempt)
    {
        catalog_diagnostics = (AssistantCatalogDiagnostics){-1, -1, -1, 0, attempt};
        MCPError count = MCP_TitleCount(handle);
        catalog_diagnostics.expected_count = count;
        if (count < 0 || count > 4096)
        {
            catalog_stage = "count";
            catalog_error = count;
            break;
        }
        size_t bytes = (size_t)count * sizeof(MCPTitleListType);
        catalog_diagnostics.capacity_bytes = (uint32_t)bytes;
        actual = 0;
        MCPError result = 0;
        if (count > 0)
        {
            titles = OSAllocFromSystem(bytes, 0x40);
            system_allocation = titles != NULL;
            if (!titles) titles = memalign(0x40, bytes);
            if (!titles) { catalog_stage = "allocation"; break; }
            memset(titles, 0, bytes);
            result = MCP_TitleList(handle, &actual, titles, (uint32_t)bytes);
        }
        catalog_diagnostics.returned_count = actual <= INT32_MAX ? (int32_t)actual : -1;
        if (result != 0)
        {
            catalog_stage = "list";
            catalog_error = result;
            break;
        }
        MCPError observed = MCP_TitleCount(handle);
        catalog_diagnostics.observed_count = observed;
        if (observed < 0 || observed > 4096)
        {
            catalog_stage = "count-after";
            catalog_error = observed;
            break;
        }
        if (actual == (uint32_t)count && observed == count)
        {
            stable = true;
            break;
        }
        catalog_stage = "count-changed";
        if (titles) release_catalog(titles, system_allocation);
        titles = NULL;
    }
    MCP_Close(handle);
    if (!stable)
    {
        if (titles) release_catalog(titles, system_allocation);
        if (truncated) *truncated = true;
        return false;
    }
    for (uint32_t i = 0; i < actual; ++i)
    {
        if (!titles[i].titleId)
        {
            catalog_stage = "invalid-record";
            if (truncated) *truncated = true;
            release_catalog(titles, system_allocation);
            return false;
        }
    }

    catalog_stage = "complete";
    bool complete = true;
    for (uint32_t i = 0; i < actual; ++i)
    {
        const MCPTitleListType *item = &titles[i];
        uint32_t high = (uint32_t)(item->titleId >> 32);
        if (high != 0x00050000 && high != 0x00050002
            && high != 0x0005000E && high != 0x0005000C)
            continue;

        // MCP knows installed titles even when direct NAND/USB directory reads
        // are denied. Only expose locations explicitly reported by that catalog.
        if (!memchr(item->path, '\0', sizeof(item->path)))
        {
            complete = false;
            continue;
        }
        bool usb = strncmp(item->path, "/vol/storage_usb01/usr/title/", 29) == 0;
        bool nand = strncmp(item->path, "/vol/storage_mlc01/usr/title/", 29) == 0;
        if (!usb && !nand)
        {
            if (strncmp(item->path, "/vol/storage_odd", 16) != 0) complete = false;
            continue;
        }
        AssistantTitleLocation title = {0};
        snprintf(title.title_id, sizeof(title.title_id), "%016llX", (unsigned long long)item->titleId);
        snprintf(title.path, sizeof(title.path), "fs:%.*s", (int)sizeof(item->path), item->path);
        title.version = item->titleVersion;
        title.usb = usb;
        if (!visitor(&title, context))
        {
            if (truncated)
                *truncated = true;
            break;
        }
    }
    if (titles) release_catalog(titles, system_allocation);
    if (!complete)
    {
        catalog_stage = "unrecognized-location";
        if (truncated) *truncated = true;
    }
    return complete;
}

typedef struct
{
    const char *title_id;
    AssistantTitleLocation *title;
    bool found;
} CatalogLookup;

static bool match_catalog_title(const AssistantTitleLocation *title, void *context)
{
    CatalogLookup *lookup = context;
    if (strcasecmp(title->title_id, lookup->title_id) != 0)
        return true;
    *lookup->title = *title;
    lookup->found = true;
    return false;
}

bool assistant_find_catalog_title(const char *title_id, AssistantTitleLocation *title)
{
    if (!title || !title_id || strlen(title_id) != 16)
        return false;
    CatalogLookup lookup = {title_id, title, false};
    return assistant_visit_title_catalog(match_catalog_title, &lookup, NULL) && lookup.found;
}

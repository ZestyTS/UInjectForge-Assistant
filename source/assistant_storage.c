#include "assistant_storage.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifdef UIF_ASSISTANT_HOST_TEST
#include "assistant_test_platform.h"
#else
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#endif

bool assistant_visit_title_catalog(AssistantTitleVisitor visitor, void *context, bool *truncated)
{
    if (truncated)
        *truncated = false;
    if (!visitor)
        return false;

    MCPError handle = MCP_Open();
    if (handle < 0)
        return false;

    MCPError count = MCP_TitleCount(handle);
    if (count < 0 || count > 4096)
    {
        MCP_Close(handle);
        return false;
    }
    if (count == 0)
    {
        MCP_Close(handle);
        return true;
    }

    size_t bytes = (size_t)count * sizeof(MCPTitleListType);
    MCPTitleListType *titles = OSAllocFromSystem(bytes, 0x40);
    if (!titles)
    {
        MCP_Close(handle);
        return false;
    }
    memset(titles, 0, bytes);
    uint32_t actual = 0;
    MCPError result = MCP_TitleList(handle, &actual, titles, (uint32_t)bytes);
    MCP_Close(handle);
    if (result < 0 || actual > (uint32_t)count)
    {
        OSFreeToSystem(titles);
        return false;
    }

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
            continue;
        bool usb = strncmp(item->path, "/vol/storage_usb01/usr/title/", 29) == 0;
        bool nand = strncmp(item->path, "/vol/storage_mlc01/usr/title/", 29) == 0;
        if (!usb && !nand)
            continue;
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
    OSFreeToSystem(titles);
    return true;
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

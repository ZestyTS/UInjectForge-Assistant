#include "assistant_catalog_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#ifdef UIF_ASSISTANT_HOST_TEST
#include "assistant_test_platform.h"
#else
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
/* The pinned WUT import library exports this API, but its older header omits it.
   The documented device selector has a 32-bit ABI; only values 2..4 are admitted. */
MCPError MCP_TitleListByDeviceType(int32_t handle, uint32_t device_type,
    uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
MCPError MCP_TitleListByDevice(int32_t handle, const char *device_name,
    uint32_t *count, MCPTitleListType *titles, uint32_t bytes);
#endif
_Static_assert(sizeof(AssistantCatalogProbeDevice) == sizeof(uint32_t), "Device selector ABI must be 32-bit.");

bool assistant_probe_title_catalog(AssistantCatalogProbeKind kind, uint32_t filter_value,
    AssistantCatalogProbeVisitor visitor, void *context, AssistantCatalogProbeResult *result)
{
    if (!result) return false;
    *result = (AssistantCatalogProbeResult){"invalid-request", 0, -1, -1, -1, 0, 0, true};
    if (!visitor || kind < ASSISTANT_PROBE_COUNT_SIZED || kind > ASSISTANT_PROBE_SYSTEM_DEVICE) return false;
    if (kind == ASSISTANT_PROBE_DEVICE_TYPE && filter_value != ASSISTANT_PROBE_DEVICE_ODD
        && filter_value != ASSISTANT_PROBE_DEVICE_MLC && filter_value != ASSISTANT_PROBE_DEVICE_USB) return false;
    if (kind == ASSISTANT_PROBE_SYSTEM_DEVICE && filter_value > 1) return false;
    MCPError handle = MCP_Open();
    if (handle < 0) { result->stage = "open"; result->error = handle; return false; }
    bool success = false;
    bool system_allocation = false;
    MCPTitleListType *titles = NULL;
    MCPError count = MCP_TitleCount(handle);
    result->expected_count = count;
    if (count < 0 || count > 4096) { result->stage = "count"; result->error = count; goto finish; }
    uint32_t capacity = kind == ASSISTANT_PROBE_COUNT_SIZED ? (uint32_t)count : 4096;
    result->capacity_bytes = capacity * sizeof(MCPTitleListType);
    uint32_t actual = 0;
    MCPError error = 0;
    if (capacity) {
        titles = OSAllocFromSystem(result->capacity_bytes, 0x40);
        system_allocation = titles != NULL;
        if (!titles) titles = memalign(0x40, result->capacity_bytes);
        if (!titles) { result->stage = "allocation"; goto finish; }
        memset(titles, 0, result->capacity_bytes);
        if (kind == ASSISTANT_PROBE_APP_TYPE)
            error = MCP_TitleListByAppType(handle, (MCPAppType)filter_value, &actual, titles, result->capacity_bytes);
        else if (kind == ASSISTANT_PROBE_DEVICE_TYPE)
            error = MCP_TitleListByDeviceType(handle, filter_value, &actual, titles, result->capacity_bytes);
        else if (kind == ASSISTANT_PROBE_SYSTEM_DEVICE)
            error = MCP_TitleListByDevice(handle, filter_value == 0 ? "slc" : "slccmpt", &actual, titles, result->capacity_bytes);
        else
            error = MCP_TitleList(handle, &actual, titles, result->capacity_bytes);
    }
    result->returned_count = actual <= INT32_MAX ? (int32_t)actual : -1;
    if (error != 0) { result->stage = "list"; result->error = error; goto finish; }
    if (actual > capacity) { result->stage = "invalid-returned-count"; goto finish; }
    MCPError observed = MCP_TitleCount(handle);
    result->observed_count = observed;
    if (observed < 0 || observed > 4096) { result->stage = "count-after"; result->error = observed; goto finish; }
    for (uint32_t i = 0; i < actual; i++) {
        AssistantCatalogProbeRecord record = {0};
        const MCPTitleListType *item = &titles[i];
        snprintf(record.title_id, sizeof(record.title_id), "%016llX", (unsigned long long)item->titleId);
        memcpy(record.path, item->path, sizeof(item->path));
        memcpy(record.indexed_device, item->indexedDevice, sizeof(item->indexedDevice));
        record.app_type = (uint32_t)item->appType;
        record.version = item->titleVersion;
        record.valid = item->titleId != 0 && memchr(item->path, '\0', sizeof(item->path))
            && memchr(item->indexedDevice, '\0', sizeof(item->indexedDevice));
        char *texts[] = {record.path, record.indexed_device};
        for (unsigned text = 0; text < 2; text++) {
            for (size_t j = 0; texts[text][j]; j++) {
                unsigned char value = (unsigned char)texts[text][j];
                if (value < 0x20 || value > 0x7e) { texts[text][j] = '?'; record.valid = false; }
            }
        }
        result->records_valid = result->records_valid && record.valid;
        if (!visitor(&record, context)) { result->stage = "response-capacity"; goto finish; }
        result->exported_count++;
    }
    result->stage = "captured";
    success = true;
finish:
    if (titles) {
        if (system_allocation) OSFreeToSystem(titles); else free(titles);
    }
    MCP_Close(handle);
    return success;
}

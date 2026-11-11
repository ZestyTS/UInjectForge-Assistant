#include "assistant_catalog_probe.h"
#include "assistant_test_platform.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

_Static_assert(sizeof(MCPTitleListType) == 97, "Host catalog fixture must match the packed Wii U ABI.");
static MCPTitleListType titles[407];
static int before, after, handle, list_error, closed, calls, allocations, visited;
static uint32_t returned, received_bytes, received_app, received_device;
static bool use_expanded, invalid_record, fallback;
static int stop_after;
static const char *received_device_name;
void *OSAllocFromSystem(size_t bytes, int alignment)
{
    if (fallback) return NULL;
    size_t rounded = (bytes + (size_t)alignment - 1) / (size_t)alignment * (size_t)alignment;
    void *p = aligned_alloc((size_t)alignment, rounded);
    if (p) allocations++;
    return p;
}
void OSFreeToSystem(void *p) { allocations--; free(p); }
MCPError MCP_Open(void) { return handle; }
MCPError MCP_Close(int h) { assert(h == handle); closed++; return 0; }
MCPError MCP_TitleCount(int h) { assert(h == handle); return calls++ ? after : before; }
MCPError MCP_TitleList(int h, uint32_t *count, MCPTitleListType *output, uint32_t bytes)
{
    assert(h == handle && *count == 0);
    received_bytes = bytes;
    if (list_error) return list_error;
    *count = use_expanded && bytes > 407 * sizeof(*output) ? 407 : returned;
    uint32_t copied = *count <= 407 && *count <= bytes / sizeof(*output) ? *count : 0;
    memcpy(output, titles, copied * sizeof(*output));
    if (invalid_record && copied) { output[0].titleId = 0; memset(output[0].path, 'X', sizeof(output[0].path)); }
    return 0;
}
MCPError MCP_TitleListByAppType(int h, MCPAppType app, uint32_t *count, MCPTitleListType *output, uint32_t bytes)
{
    received_app = app;
    return MCP_TitleList(h, count, output, bytes);
}
MCPError MCP_TitleListByDeviceType(int h, MCPDeviceType device, uint32_t *count, MCPTitleListType *output, uint32_t bytes)
{
    received_device = device;
    return MCP_TitleList(h, count, output, bytes);
}
MCPError MCP_TitleListByDevice(int h, const char *device, uint32_t *count, MCPTitleListType *output, uint32_t bytes)
{
    received_device_name = device;
    return MCP_TitleList(h, count, output, bytes);
}
static bool collect(const AssistantCatalogProbeRecord *record, void *context)
{
    (void)context;
    assert(strlen(record->title_id) == 16 && strlen(record->path) <= 56 && strlen(record->indexed_device) <= 10);
    if (visited == stop_after) return false;
    visited++;
    return true;
}
static void reset(void)
{
    before = after = 407; handle = 1; list_error = closed = calls = visited = 0;
    returned = 402; received_bytes = received_app = received_device = 0;
    use_expanded = invalid_record = fallback = false; stop_after = -1;
    received_device_name = NULL;
    assert(allocations == 0);
    memset(titles, 0, sizeof(titles));
    for (unsigned i = 0; i < 407; i++) {
        titles[i].titleId = 0x0005000010000000ULL + i;
        strcpy(titles[i].indexedDevice, "usb");
        snprintf(titles[i].path, sizeof(titles[i].path), "/vol/storage_usb01/usr/title/00050000/%08X", 0x10000000 + i);
    }
}
static bool probe(AssistantCatalogProbeKind kind, AssistantCatalogProbeResult *result)
{
    bool ok = assistant_probe_title_catalog(kind, 0x80000000, collect, NULL, result);
    assert(allocations == 0);
    return ok;
}
int main(void)
{
    AssistantCatalogProbeResult result;
    reset(); assert(probe(ASSISTANT_PROBE_COUNT_SIZED, &result));
    assert(result.expected_count == 407 && result.returned_count == 402 && result.observed_count == 407);
    assert(result.capacity_bytes == 39479 && result.exported_count == 402 && result.records_valid && closed == 1);
    reset(); use_expanded = true; assert(probe(ASSISTANT_PROBE_EXPANDED, &result));
    assert(result.returned_count == 407 && visited == 407 && received_bytes == 4096 * 97);
    reset(); assert(probe(ASSISTANT_PROBE_APP_TYPE, &result));
    assert(received_app == 0x80000000 && received_bytes == 4096 * 97);
    for (uint32_t device = 2; device <= 4; device++) {
        reset();
        assert(assistant_probe_title_catalog(ASSISTANT_PROBE_DEVICE_TYPE, device, collect, NULL, &result));
        assert(received_device == device && received_app == 0 && received_bytes == 4096 * 97);
        assert(result.returned_count == 402 && result.expected_count == 407 && closed == 1 && allocations == 0);
    }
    const uint32_t invalid_devices[] = {0, 1, 5, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_devices) / sizeof(*invalid_devices); i++) {
        reset();
        assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_DEVICE_TYPE, invalid_devices[i], collect, NULL, &result));
        assert(closed == 0 && calls == 0 && allocations == 0);
    }
    for (uint32_t device = 0; device <= 1; device++) {
        reset(); returned = 5;
        assert(assistant_probe_title_catalog(ASSISTANT_PROBE_SYSTEM_DEVICE, device, collect, NULL, &result));
        assert(strcmp(received_device_name, device == 0 ? "slc" : "slccmpt") == 0);
        assert(result.returned_count == 5 && visited == 5 && received_bytes == 4096 * 97 && allocations == 0);
    }
    reset();
    assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_SYSTEM_DEVICE, 2, collect, NULL, &result));
    assert(closed == 0 && calls == 0 && received_device_name == NULL);
    reset(); list_error = -15;
    assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_SYSTEM_DEVICE, 0, collect, NULL, &result));
    assert(result.error == -15 && visited == 0 && closed == 1 && allocations == 0);
    reset(); returned = 0;
    assert(assistant_probe_title_catalog(ASSISTANT_PROBE_SYSTEM_DEVICE, 0, collect, NULL, &result));
    assert(result.returned_count == 0 && visited == 0 && closed == 1 && allocations == 0);
    reset(); list_error = -15;
    assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_DEVICE_TYPE, 4, collect, NULL, &result));
    assert(result.error == -15 && visited == 0 && closed == 1 && allocations == 0);
    reset(); fallback = true; assert(probe(ASSISTANT_PROBE_EXPANDED, &result) && closed == 1);
    reset(); invalid_record = true; assert(probe(ASSISTANT_PROBE_EXPANDED, &result) && !result.records_valid);
    reset(); after = 408; assert(probe(ASSISTANT_PROBE_EXPANDED, &result) && result.observed_count == 408);
    reset(); returned = 5000; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && visited == 0 && closed == 1);
    reset(); list_error = -15; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && result.error == -15 && visited == 0);
    reset(); before = -5; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && visited == 0 && closed == 1);
    reset(); before = 4097; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && visited == 0 && closed == 1);
    reset(); after = -4; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && visited == 0 && closed == 1);
    reset(); stop_after = 10; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && result.exported_count == 10);
    assert(strcmp(result.stage, "response-capacity") == 0);
    reset(); before = after = returned = 0; assert(probe(ASSISTANT_PROBE_COUNT_SIZED, &result) && visited == 0);
    reset(); handle = -3; assert(!probe(ASSISTANT_PROBE_EXPANDED, &result) && closed == 0);
    reset(); assert(!assistant_probe_title_catalog((AssistantCatalogProbeKind)99, 0, collect, NULL, &result) && closed == 0);
    assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_EXPANDED, 0, NULL, NULL, &result));
    assert(!assistant_probe_title_catalog(ASSISTANT_PROBE_EXPANDED, 0, collect, NULL, NULL));
    puts("Catalog diagnostic probe: 30 scenarios passed; packed ABI, bounded device filters and failure cleanup verified.");
    return 0;
}

#ifndef ASSISTANT_CATALOG_PROBE_H
#define ASSISTANT_CATALOG_PROBE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ASSISTANT_PROBE_COUNT_SIZED,
    ASSISTANT_PROBE_EXPANDED,
    ASSISTANT_PROBE_APP_TYPE,
    ASSISTANT_PROBE_DEVICE_TYPE,
    ASSISTANT_PROBE_SYSTEM_DEVICE
} AssistantCatalogProbeKind;

typedef enum {
    ASSISTANT_PROBE_DEVICE_ODD = 2,
    ASSISTANT_PROBE_DEVICE_MLC = 3,
    ASSISTANT_PROBE_DEVICE_USB = 4
} AssistantCatalogProbeDevice;

typedef struct {
    char title_id[17];
    char path[57];
    char indexed_device[11];
    uint32_t app_type;
    uint16_t version;
    bool valid;
} AssistantCatalogProbeRecord;

typedef struct {
    const char *stage;
    int32_t error;
    int32_t expected_count;
    int32_t returned_count;
    int32_t observed_count;
    uint32_t capacity_bytes;
    uint32_t exported_count;
    bool records_valid;
} AssistantCatalogProbeResult;

typedef bool (*AssistantCatalogProbeVisitor)(const AssistantCatalogProbeRecord *, void *);

/* Diagnostic records never constitute an installation-safe catalog. */
bool assistant_probe_title_catalog(AssistantCatalogProbeKind kind, uint32_t filter_value,
    AssistantCatalogProbeVisitor visitor, void *context, AssistantCatalogProbeResult *result);

#endif

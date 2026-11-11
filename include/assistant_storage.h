#ifndef ASSISTANT_STORAGE_H
#define ASSISTANT_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char title_id[17];
    char path[160];
    uint16_t version;
    bool usb;
} AssistantTitleLocation;

typedef bool (*AssistantTitleVisitor)(const AssistantTitleLocation *title, void *context);

typedef struct
{
    int32_t expected_count;
    int32_t returned_count;
    int32_t observed_count;
    uint32_t capacity_bytes;
    uint32_t attempts;
} AssistantCatalogDiagnostics;

#ifdef __cplusplus
extern "C" {
#endif

bool assistant_visit_title_catalog(AssistantTitleVisitor visitor, void *context, bool *truncated);
bool assistant_find_catalog_title(const char *title_id, AssistantTitleLocation *title);
const char *assistant_catalog_stage(void);
int32_t assistant_catalog_error(void);
AssistantCatalogDiagnostics assistant_catalog_diagnostics(void);

#ifdef __cplusplus
}
#endif
#endif

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

#ifdef __cplusplus
extern "C" {
#endif

bool assistant_visit_title_catalog(AssistantTitleVisitor visitor, void *context, bool *truncated);
bool assistant_find_catalog_title(const char *title_id, AssistantTitleLocation *title);

#ifdef __cplusplus
}
#endif
#endif

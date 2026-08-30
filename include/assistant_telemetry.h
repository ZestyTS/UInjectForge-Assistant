#ifndef ASSISTANT_TELEMETRY_H
#define ASSISTANT_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASSISTANT_TELEMETRY_PATH 768
#define ASSISTANT_TELEMETRY_TEXT 256

typedef struct
{
    char title_id[17];
    char game_name[ASSISTANT_TELEMETRY_TEXT];
    char storage_target[64];
    char installed_path[ASSISTANT_TELEMETRY_PATH];
    char runtime_path[ASSISTANT_TELEMETRY_PATH];
    char runtime_sha256[65];
} AssistantTelemetryTitle;

typedef struct
{
    const char *event_type;
    const char *source;
    const char *title_id;
    const char *game_name;
    const char *storage_target;
    const char *installed_path;
    const char *runtime_path;
    const char *runtime_sha256;
    const char *result;
    const char *message;
    const char *correlation_id;
} AssistantTelemetryEvent;

bool assistant_telemetry_find_title(const char *title_id, AssistantTelemetryTitle *title);

bool assistant_telemetry_write_event(
    const char *sd_root,
    const AssistantTelemetryEvent *event,
    char event_id[64]);

char *assistant_telemetry_read_events_json(
    const char *sd_root,
    unsigned int maximum_events,
    unsigned int *event_count,
    bool *truncated);

#endif

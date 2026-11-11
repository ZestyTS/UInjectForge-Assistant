#ifndef ASSISTANT_UPLOAD_H
#define ASSISTANT_UPLOAD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool started;
    uint64_t last_control_ms;
} AssistantUploadPump;

typedef struct {
    uint64_t receive_ticks, hash_ticks, write_ticks, service_ticks, total_ticks;
    unsigned int reads, pumps;
} AssistantUploadTiming;

bool assistant_upload_control_due(AssistantUploadPump *pump, uint64_t now_ms);
void assistant_upload_control_complete(AssistantUploadPump *pump, uint64_t now_ms);

/* The outer owner loop still pumps FTP data and polls HTTP on every iteration.
   Only expensive platform/UI/lifecycle service is paced while a FTP transaction
   exists. Leaving FTP resets the cadence and preserves ordinary owner behavior. */
bool assistant_upload_owner_control_due(AssistantUploadPump *pump, bool ftp_active, uint64_t now_ms);
void assistant_upload_owner_control_complete(AssistantUploadPump *pump, bool ftp_active, uint64_t now_ms);

#endif

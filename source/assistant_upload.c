#include "assistant_upload.h"

bool assistant_upload_control_due(AssistantUploadPump *pump, uint64_t now_ms)
{
    if (!pump->started || now_ms < pump->last_control_ms
        || now_ms - pump->last_control_ms >= 25) {
        pump->started = true;
        pump->last_control_ms = now_ms;
        return true;
    }
    return false;
}

void assistant_upload_control_complete(AssistantUploadPump *pump, uint64_t now_ms)
{
    pump->last_control_ms = now_ms;
}

bool assistant_upload_owner_control_due(AssistantUploadPump *pump, bool ftp_active, uint64_t now_ms)
{
    if (!ftp_active) {
        *pump = (AssistantUploadPump){0};
        return true;
    }
    return assistant_upload_control_due(pump, now_ms);
}

void assistant_upload_owner_control_complete(AssistantUploadPump *pump, bool ftp_active, uint64_t now_ms)
{
    if (ftp_active) assistant_upload_control_complete(pump, now_ms);
    else *pump = (AssistantUploadPump){0};
}

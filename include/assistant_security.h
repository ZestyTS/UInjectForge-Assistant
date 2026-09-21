#ifndef ASSISTANT_SECURITY_H
#define ASSISTANT_SECURITY_H

#include <stdbool.h>

typedef struct
{
    bool global_enabled;
    bool manual_enabled;
    bool automatic_enabled;
} AssistantInstallPermissions;

#ifdef __cplusplus
extern "C" {
#endif

bool assistant_constant_time_equal(const char *left, const char *right);
bool assistant_request_allowed_during_install(const char *method, const char *path);
bool assistant_request_allowed_with_install(const char *method, const char *path);
unsigned int assistant_pairing_backoff_ms(unsigned int failure_streak);
AssistantInstallPermissions assistant_resolve_install_permissions(
    bool legacy_present,
    bool legacy_enabled,
    bool manual_present,
    bool manual_enabled,
    bool automatic_present,
    bool automatic_enabled);

#ifdef __cplusplus
}
#endif

#endif

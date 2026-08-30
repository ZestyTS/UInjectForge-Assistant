#include "assistant_security.h"

#include <stddef.h>
#include <string.h>

bool assistant_constant_time_equal(const char *left, const char *right)
{
    if (!left || !right)
        return false;

    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    size_t length = left_length > right_length ? left_length : right_length;
    unsigned int difference = (unsigned int)(left_length ^ right_length);

    for (size_t i = 0; i < length; i++)
    {
        unsigned char left_value = i < left_length ? (unsigned char)left[i] : 0;
        unsigned char right_value = i < right_length ? (unsigned char)right[i] : 0;
        difference |= (unsigned int)(left_value ^ right_value);
    }

    return difference == 0;
}

unsigned int assistant_pairing_backoff_ms(unsigned int failure_streak)
{
    if (failure_streak == 0)
        return 0;

    unsigned int shift = failure_streak - 1;
    if (shift > 4)
        shift = 4;
    return 250U << shift;
}

AssistantInstallPermissions assistant_resolve_install_permissions(
    bool legacy_present,
    bool legacy_enabled,
    bool manual_present,
    bool manual_enabled,
    bool automatic_present,
    bool automatic_enabled)
{
    AssistantInstallPermissions result = {
        .global_enabled = legacy_present ? legacy_enabled : true,
        .manual_enabled = manual_present ? manual_enabled : true,
        .automatic_enabled = automatic_present ? automatic_enabled : false,
    };

    if (!result.global_enabled)
    {
        result.manual_enabled = false;
        result.automatic_enabled = false;
    }

    return result;
}

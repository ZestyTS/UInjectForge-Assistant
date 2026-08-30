#include "assistant_security.h"

#include <assert.h>
#include <stdio.h>

static void test_constant_time_compare(void)
{
    assert(assistant_constant_time_equal("token", "token"));
    assert(!assistant_constant_time_equal("token", "Token"));
    assert(!assistant_constant_time_equal("token", "token-longer"));
    assert(!assistant_constant_time_equal(NULL, "token"));
}

static void test_pairing_backoff(void)
{
    assert(assistant_pairing_backoff_ms(0) == 0);
    assert(assistant_pairing_backoff_ms(1) == 250);
    assert(assistant_pairing_backoff_ms(2) == 500);
    assert(assistant_pairing_backoff_ms(3) == 1000);
    assert(assistant_pairing_backoff_ms(4) == 2000);
    assert(assistant_pairing_backoff_ms(5) == 4000);
    assert(assistant_pairing_backoff_ms(100) == 4000);
}

static void test_install_permissions(void)
{
    AssistantInstallPermissions defaults = assistant_resolve_install_permissions(false, false, false, false, false, false);
    assert(defaults.global_enabled);
    assert(defaults.manual_enabled);
    assert(!defaults.automatic_enabled);

    AssistantInstallPermissions opted_in = assistant_resolve_install_permissions(true, true, true, true, true, true);
    assert(opted_in.global_enabled);
    assert(opted_in.manual_enabled);
    assert(opted_in.automatic_enabled);

    AssistantInstallPermissions killed = assistant_resolve_install_permissions(true, false, true, true, true, true);
    assert(!killed.global_enabled);
    assert(!killed.manual_enabled);
    assert(!killed.automatic_enabled);
}

int main(void)
{
    test_constant_time_compare();
    test_pairing_backoff();
    test_install_permissions();
    puts("Assistant native security tests passed.");
    return 0;
}

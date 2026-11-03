#include "assistant_test_temp.h"
#include "assistant_pairing.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *token = "0123456789ABCDEF0123456789ABCDEF";
    AssistantPairingTrust trust = {0}, loaded = {0};
    assert(!assistant_pairing_contains(&trust, token));
    assert(!assistant_pairing_remember(&trust, "123456"));
    assert(!assistant_pairing_remember(&trust, "G123456789ABCDEF0123456789ABCDEF"));
    assert(assistant_pairing_remember(&trust, token));
    assert(assistant_pairing_remember(&trust, token) && trust.count == 1);
    assert(assistant_pairing_contains(&trust, token));
    assert(!assistant_pairing_contains(&trust, "0123456789ABCDEF0123456789ABCDE0"));

    char directory[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(directory, sizeof(directory), "uif-pairing"));
    char path[1024];
    snprintf(path, sizeof(path), "%s/devices.bin", directory);
    assert(assistant_pairing_save(&trust, path));
    assert(assistant_pairing_load(&loaded, path));
    assert(assistant_pairing_contains(&loaded, token));
    FILE *file = fopen(path, "rb");
    assert(file);
    unsigned char bytes[512];
    size_t length = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    assert(length == 73);
    for (size_t i = 0; i + 32 <= length; i++) assert(memcmp(bytes + i, token, 32) != 0);

    assert(assistant_pairing_forget(&loaded, token));
    assert(assistant_pairing_save(&loaded, path));
    assert(assistant_pairing_load(&trust, path) && trust.count == 0);
    assert(!assistant_pairing_contains(&trust, token));
    for (unsigned int i = 0; i < ASSISTANT_MAX_TRUSTED_DEVICES; i++)
    {
        char next[33];
        snprintf(next, sizeof(next), "%032u", i);
        assert(assistant_pairing_remember(&trust, next));
    }
    assert(!assistant_pairing_remember(&trust, token));
    assert(assistant_pairing_forget(&trust, "00000000000000000000000000000007"));
    assert(assistant_pairing_remember(&trust, token));
    assert(assistant_pairing_save(&trust, path));
    assert(!assistant_pairing_save(&trust, "/no-such-uif-folder/devices.bin"));
    assert(assistant_pairing_load(&loaded, path) && loaded.count == 8);

    file = fopen(path, "ab"); assert(file); fputc(0, file); fclose(file);
    assert(!assistant_pairing_load(&loaded, path) && loaded.count == 0);
    assert(assistant_pairing_save(&trust, path));
    file = fopen(path, "r+b"); assert(file); fseek(file, 12, SEEK_SET); fputc(0xFF, file); fclose(file);
    assert(!assistant_pairing_load(&loaded, path) && loaded.count == 0);
    file = fopen(path, "wb"); assert(file); fputs("UIFTRST1", file); fclose(file);
    assert(!assistant_pairing_load(&loaded, path) && loaded.count == 0);
    unlink(path);
    assert(!assistant_pairing_load(&loaded, path) && loaded.count == 0);
    rmdir(directory);
    puts("Pairing persistence, revocation, limits and corrupt-file tests passed.");
    return 0;
}

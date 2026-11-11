#include "assistant_pairing.h"

#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool token_hash(const char *token, unsigned char hash[32])
{
    if (!token || strlen(token) != 32) return false;
    for (unsigned int i = 0; i < 32; i++)
        if (!((token[i] >= '0' && token[i] <= '9')
            || (token[i] >= 'a' && token[i] <= 'f')
            || (token[i] >= 'A' && token[i] <= 'F'))) return false;
    return mbedtls_sha256_ret((const unsigned char *)token, 32, hash, 0) == 0;
}

static int find_hash(const AssistantPairingTrust *trust, const unsigned char hash[32])
{
    if (!trust || trust->count > ASSISTANT_MAX_TRUSTED_DEVICES) return -1;
    int found = -1;
    for (unsigned int i = 0; i < trust->count; i++)
    {
        unsigned int difference = 0;
        for (unsigned int j = 0; j < 32; j++) difference |= trust->hashes[i][j] ^ hash[j];
        if (!difference) found = (int)i;
    }
    return found;
}

bool assistant_pairing_contains(const AssistantPairingTrust *trust, const char *token)
{
    unsigned char hash[32];
    return token_hash(token, hash) && find_hash(trust, hash) >= 0;
}

bool assistant_pairing_remember(AssistantPairingTrust *trust, const char *token)
{
    unsigned char hash[32];
    if (!trust || !token_hash(token, hash)) return false;
    if (find_hash(trust, hash) >= 0) return true;
    if (trust->count >= ASSISTANT_MAX_TRUSTED_DEVICES) return false;
    memcpy(trust->hashes[trust->count++], hash, sizeof(hash));
    return true;
}

bool assistant_pairing_forget(AssistantPairingTrust *trust, const char *token)
{
    unsigned char hash[32];
    if (!token_hash(token, hash)) return false;
    int index = find_hash(trust, hash);
    if (index < 0) return false;
    trust->count--;
    memmove(trust->hashes[index], trust->hashes[index + 1], (trust->count - (unsigned int)index) * 32);
    memset(trust->hashes[trust->count], 0, 32);
    return true;
}

bool assistant_pairing_load(AssistantPairingTrust *trust, const char *path)
{
    memset(trust, 0, sizeof(*trust));
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    unsigned char bytes[9 + ASSISTANT_MAX_TRUSTED_DEVICES * 32 + 32];
    size_t length = fread(bytes, 1, sizeof(bytes), file);
    bool valid = !ferror(file) && fgetc(file) == EOF && length >= 41;
    if (fclose(file) != 0) valid = false;
    unsigned char hash[32];
    valid = valid && memcmp(bytes, "UIFTRST1", 8) == 0
        && bytes[8] <= ASSISTANT_MAX_TRUSTED_DEVICES
        && length == (size_t)(9 + bytes[8] * 32 + 32)
        && mbedtls_sha256_ret(bytes, length - 32, hash, 0) == 0
        && memcmp(hash, bytes + length - 32, 32) == 0;
    if (valid)
    {
        trust->count = bytes[8];
        memcpy(trust->hashes, bytes + 9, trust->count * 32);
    }
    return valid;
}

bool assistant_pairing_save(const AssistantPairingTrust *trust, const char *path)
{
    if (!trust || trust->count > ASSISTANT_MAX_TRUSTED_DEVICES) return false;
    char temporary[1024], backup[1024];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)
        || snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return false;
    unsigned char bytes[9 + ASSISTANT_MAX_TRUSTED_DEVICES * 32 + 32];
    size_t payload = 9 + trust->count * 32;
    memcpy(bytes, "UIFTRST1", 8);
    bytes[8] = (unsigned char)trust->count;
    memcpy(bytes + 9, trust->hashes, trust->count * 32);
    if (mbedtls_sha256_ret(bytes, payload, bytes + payload, 0) != 0) return false;
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool ok = fwrite(bytes, 1, payload + 32, file) == payload + 32 && fflush(file) == 0;
    if (fclose(file) != 0) ok = false;
    if (!ok) { unlink(temporary); return false; }

    // FAT cannot replace a destination with rename. Never load the backup on startup:
    // interruption during revocation must fail closed, not restore a revoked credential.
    bool existed = access(path, F_OK) == 0;
    unlink(backup);
    if (existed && rename(path, backup) != 0) { unlink(temporary); return false; }
    if (rename(temporary, path) != 0)
    {
        if (existed) rename(backup, path);
        unlink(temporary);
        return false;
    }
    unlink(backup);
    return true;
}

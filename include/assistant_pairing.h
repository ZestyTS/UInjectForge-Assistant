#ifndef ASSISTANT_PAIRING_H
#define ASSISTANT_PAIRING_H

#include <stdbool.h>

#define ASSISTANT_MAX_TRUSTED_DEVICES 8

typedef struct
{
    unsigned int count;
    unsigned char hashes[ASSISTANT_MAX_TRUSTED_DEVICES][32];
} AssistantPairingTrust;

bool assistant_pairing_load(AssistantPairingTrust *trust, const char *path);
bool assistant_pairing_save(const AssistantPairingTrust *trust, const char *path);
bool assistant_pairing_contains(const AssistantPairingTrust *trust, const char *token);
bool assistant_pairing_remember(AssistantPairingTrust *trust, const char *token);
bool assistant_pairing_forget(AssistantPairingTrust *trust, const char *token);

#endif

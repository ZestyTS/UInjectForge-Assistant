#ifndef ASSISTANT_STAGED_INSTALL_H
#define ASSISTANT_STAGED_INSTALL_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char request_id[33], package_name[256], title_id[17], tmd_sha256[65];
} AssistantStagedInstallRequest;

bool assistant_staged_install_parse(const char *json, AssistantStagedInstallRequest *request);
bool assistant_staged_install_identity(const char *package_path, char title_id[17], char digest[65]);
bool assistant_staged_install_claim(const char *journal_root, const AssistantStagedInstallRequest *request);
bool assistant_staged_install_record_result(const char *journal_root, const char *request_id, bool success);
bool assistant_staged_install_read_result(const char *journal_root, const char *request_id, bool *success);
#endif

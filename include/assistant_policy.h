#ifndef ASSISTANT_POLICY_H
#define ASSISTANT_POLICY_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ASSISTANT_POLICY_FILE "uinjectforge-install-policy.bin"
typedef enum { ASSISTANT_POLICY_UNKNOWN, ASSISTANT_POLICY_NAND, ASSISTANT_POLICY_ANY, ASSISTANT_POLICY_INVALID } AssistantPackagePolicy;
AssistantPackagePolicy assistant_policy_read(const char *package_path);
// Only call for unpublished, receiver-owned transfer staging.
bool assistant_policy_write(const char *package_path, bool nand_only);
#ifdef __cplusplus
}
#endif
#endif

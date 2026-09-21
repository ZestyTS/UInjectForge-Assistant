#ifndef ASSISTANT_PATHS_H
#define ASSISTANT_PATHS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t assistant_directory_creation_offset(const char *path);
bool assistant_sd_config_path_allowed(const char *path);

#ifdef __cplusplus
}
#endif
#endif

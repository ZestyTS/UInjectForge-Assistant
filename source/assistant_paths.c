#include "assistant_paths.h"

#include <string.h>
#include <strings.h>

bool assistant_sd_config_path_allowed(const char *path)
{
    if (!path) return false;
    if (strcasecmp(path, "nincfg.bin") == 0) return true;
    const char *prefix = "nintendont/configs/";
    size_t length = strlen(prefix);
    if (strlen(path) != length + 8 || strncasecmp(path, prefix, length) != 0
        || strcasecmp(path + length + 4, ".bin") != 0) return false;
    for (size_t i = length; i < length + 4; ++i)
        if (!((path[i] >= 'A' && path[i] <= 'Z') || (path[i] >= 'a' && path[i] <= 'z')
            || (path[i] >= '0' && path[i] <= '9'))) return false;
    return true;
}

size_t assistant_directory_creation_offset(const char *path)
{
    if (!path || !path[0])
        return 0;

    const char *namespace_root = path;
    const char *device_separator = strstr(path, ":/");
    if (device_separator)
        namespace_root = device_separator + 1;

    if (strncmp(namespace_root, "/vol/", 5) == 0)
    {
        const char *mount_end = strchr(namespace_root + 5, '/');
        return mount_end ? (size_t)(mount_end - path + 1) : strlen(path);
    }

    if (device_separator)
        return (size_t)(device_separator - path + 2);

    return path[0] == '/' ? 1U : 0U;
}

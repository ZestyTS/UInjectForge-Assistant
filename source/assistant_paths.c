#include "assistant_paths.h"

#include <string.h>

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

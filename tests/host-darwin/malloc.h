#ifndef UIF_ASSISTANT_HOST_DARWIN_MALLOC_H
#define UIF_ASSISTANT_HOST_DARWIN_MALLOC_H

#ifndef __APPLE__
#error "This malloc.h adapter is only for Darwin host tests"
#endif

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

/* Darwin has no GNU malloc.h/memalign. Use the real POSIX allocator;
 * its result is aligned and can be released with ordinary free().
 * posix_memalign returns an error number instead of setting errno. */
static inline void *memalign(size_t alignment, size_t bytes)
{
    void *memory = NULL;
    const int error = posix_memalign(&memory, alignment, bytes);
    if (error != 0) {
        errno = error;
        return NULL;
    }
    return memory;
}

#endif

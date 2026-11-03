#ifndef UIF_ASSISTANT_TEST_TEMP_H
#define UIF_ASSISTANT_TEST_TEMP_H

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define ASSISTANT_TEST_TEMP_ROOT_CAPACITY 512

/* Host fixtures belong to this run's configured temporary volume. Never truncate
   a root into a different directory; an overlong TMPDIR is a fixture error. */
static inline bool assistant_test_make_temp_directory(char *output, size_t capacity,
    const char *prefix)
{
    if (!output || capacity == 0 || !prefix || !*prefix)
    {
        errno = EINVAL;
        return false;
    }
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    int length = snprintf(output, capacity, "%s/%s-XXXXXX", base, prefix);
    if (length < 0 || (size_t)length >= capacity)
    {
        errno = ENAMETOOLONG;
        return false;
    }
    return mkdtemp(output) != NULL;
}

#endif

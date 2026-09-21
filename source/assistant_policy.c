#include "assistant_policy.h"
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static bool package_file(char *output, size_t capacity, const char *root, const char *name)
{
    int length = snprintf(output, capacity, "%s/%s", root, name);
    return length > 0 && (size_t)length < capacity;
}

static bool tmd_digest(const char *root, unsigned char output[32])
{
    char path[1024];
    if (!package_file(path, sizeof(path), root, "title.tmd")) return false;
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 16 * 1024 * 1024) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
    unsigned char buffer[4096];
    size_t count, total = 0;
    while (ok && (count = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        total += count;
        ok = total <= 16 * 1024 * 1024 && mbedtls_sha256_update_ret(&sha, buffer, count) == 0;
    }
    ok = ok && !ferror(file) && total == (size_t)info.st_size;
    if (fclose(file) != 0) ok = false;
    if (ok) ok = mbedtls_sha256_finish_ret(&sha, output) == 0;
    mbedtls_sha256_free(&sha);
    return ok;
}

AssistantPackagePolicy assistant_policy_read(const char *root)
{
    char path[1024];
    if (!root || !package_file(path, sizeof(path), root, ASSISTANT_POLICY_FILE)) return ASSISTANT_POLICY_INVALID;
    struct stat info;
    if (lstat(path, &info) != 0) return errno == ENOENT ? ASSISTANT_POLICY_UNKNOWN : ASSISTANT_POLICY_INVALID;
    if (!S_ISREG(info.st_mode) || info.st_size != 41) return ASSISTANT_POLICY_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return ASSISTANT_POLICY_INVALID;
    unsigned char record[41], digest[32];
    bool ok = fread(record, 1, sizeof(record), file) == sizeof(record) && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || memcmp(record, "UIFINST1", 8) != 0 || record[8] > 1 || !tmd_digest(root, digest)
        || memcmp(digest, record + 9, 32) != 0) return ASSISTANT_POLICY_INVALID;
    return record[8] ? ASSISTANT_POLICY_NAND : ASSISTANT_POLICY_ANY;
}

bool assistant_policy_write(const char *root, bool nand_only)
{
    char path[1024], temp[1024];
    unsigned char record[41] = "UIFINST1";
    record[8] = nand_only ? 1 : 0;
    if (!root || !package_file(path, sizeof(path), root, ASSISTANT_POLICY_FILE)
        || !package_file(temp, sizeof(temp), root, ASSISTANT_POLICY_FILE ".tmp")
        || !tmd_digest(root, record + 9)) return false;
    int fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); remove(temp); return false; }
    bool ok = fwrite(record, 1, sizeof(record), file) == sizeof(record);
    if (fflush(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    // The caller owns unpublished transfer staging. FAT rename cannot replace an
    // existing file; the outer directory commit publishes this record with the WUP.
    if (ok && remove(path) != 0 && errno != ENOENT) ok = false;
    if (ok) ok = rename(temp, path) == 0;
    if (!ok) remove(temp);
    return ok;
}

#define _POSIX_C_SOURCE 200809L
#include "assistant_test_temp.h"
#include "assistant_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int main(void)
{
    char root[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-policy"));
    char path[2048], renamed[1024];
    snprintf(path, sizeof(path), "%s/title.tmd", root);
    FILE *file = fopen(path, "wb"); assert(file); fputs("tmd-A", file); fclose(file);
    assert(assistant_policy_read(root) == ASSISTANT_POLICY_UNKNOWN);
    assert(assistant_policy_write(root, true));
    assert(assistant_policy_read(root) == ASSISTANT_POLICY_NAND);
    snprintf(path, sizeof(path), "%s/%s.tmp", root, ASSISTANT_POLICY_FILE);
    assert(symlink("title.tmd", path) == 0);
    assert(!assistant_policy_write(root, false));
    assert(assistant_policy_read(root) == ASSISTANT_POLICY_NAND);
    assert(unlink(path) == 0);
    snprintf(renamed, sizeof(renamed), "%s-renamed", root);
    assert(rename(root, renamed) == 0);
    assert(assistant_policy_read(renamed) == ASSISTANT_POLICY_NAND);
    snprintf(path, sizeof(path), "%s/title.tmd", renamed);
    file = fopen(path, "wb"); assert(file); fputs("tmd-B", file); fclose(file);
    assert(assistant_policy_read(renamed) == ASSISTANT_POLICY_INVALID);
    assert(assistant_policy_write(renamed, false));
    assert(assistant_policy_read(renamed) == ASSISTANT_POLICY_ANY);
    remove(path);
    assert(assistant_policy_read(renamed) == ASSISTANT_POLICY_INVALID);
    snprintf(path, sizeof(path), "%s/%s", renamed, ASSISTANT_POLICY_FILE); remove(path);
    rmdir(renamed);
    puts("Assistant durable policy rename/replacement tests passed.");
    return 0;
}

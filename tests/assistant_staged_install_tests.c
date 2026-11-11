#define _POSIX_C_SOURCE 200809L
#include "assistant_test_temp.h"
#include "assistant_staged_install.h"
#include "assistant_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *root, const char *name, const void *data, size_t size)
{
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(data, 1, size, file) == size); assert(!fclose(file));
}


/* Only exact authored entries inside this test's mkdtemp root are retired.
   unlink removes a symlink itself; directory removal never follows one. */
static void remove_fixture_entry(const char *root, const char *name, bool directory)
{
    char path[2048];
    int length = snprintf(path, sizeof(path), "%s/%s", root, name);
    assert(length >= 0 && (size_t)length < sizeof(path));
    int result = directory ? rmdir(path) : unlink(path);
    assert(result == 0);
}

int main(void)
{
    char root[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-staged"));
    unsigned char tmd[0xB04 + 48] = {0};
    tmd[1] = 1; tmd[3] = 4;
    tmd[0x18D] = 5; tmd[0x18F] = 2; tmd[0x193] = 1;
    tmd[0x1DF] = 1; tmd[0xB07] = 1; tmd[0xB13] = 16;
    unsigned char app[16] = {0};
    write_file(root, "title.tmd", tmd, sizeof(tmd));
    write_file(root, "title.tik", "ticket", 6); write_file(root, "title.cert", "cert", 4);
    write_file(root, "00000001.app", app, sizeof(app));
    char title[17], digest[65];
    assert(!assistant_staged_install_identity(root, title, digest));
    assert(assistant_policy_write(root, true));
    assert(!assistant_staged_install_identity(root, title, digest));
    assert(assistant_policy_write(root, false));
    assert(assistant_staged_install_identity(root, title, digest));
    assert(!strcmp(title, "0005000200000001") && strlen(digest) == 64);
    char json[1024];
    snprintf(json, sizeof(json), "{\"requestId\":\"0123456789abcdef0123456789abcdef\",\"packageName\":\"Game\",\"titleId\":\"%s\",\"tmdSha256\":\"%s\",\"storageTarget\":\"usb\"}", title, digest);
    AssistantStagedInstallRequest request;
    assert(assistant_staged_install_parse(json, &request));
    assert(!strcmp(request.request_id, "0123456789ABCDEF0123456789ABCDEF"));
    const char *bad[] = {"nand", "USB", "../Game", "Game/Other", "Game\\\\Other", ".Game"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char invalid[1024];
        snprintf(invalid, sizeof(invalid), "{\"requestId\":\"0123456789abcdef0123456789abcdef\",\"packageName\":\"%s\",\"titleId\":\"%s\",\"tmdSha256\":\"%s\",\"storageTarget\":\"%s\"}", i < 2 ? "Game" : bad[i], title, digest, i < 2 ? bad[i] : "usb");
        assert(!assistant_staged_install_parse(invalid, &request));
    }
    assert(!assistant_staged_install_parse("{}", &request));
    assert(!assistant_staged_install_parse("{\"requestId\":\"a\",\"requestId\":\"b\"}", &request));
    assert(assistant_staged_install_parse(json, &request));
    char journal[1024]; snprintf(journal, sizeof(journal), "%s/journal", root); assert(!mkdir(journal, 0700));
    assert(assistant_staged_install_claim(journal, &request));
    assert(!assistant_staged_install_claim(journal, &request));
    bool recovered = true;
    assert(!assistant_staged_install_read_result(journal, request.request_id, &recovered) && !recovered);
    assert(assistant_staged_install_record_result(journal, request.request_id, true));
    assert(assistant_staged_install_read_result(journal, request.request_id, &recovered) && recovered);
    assert(!assistant_staged_install_record_result(journal, request.request_id, false));
    assert(!assistant_staged_install_claim(journal, &request)); // Result recovery never clears claims.
    assert(!assistant_staged_install_read_result(journal, "../escape", &recovered));
    assert(!assistant_staged_install_record_result(journal, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", true));
    char receipt_name[96]; snprintf(receipt_name, sizeof(receipt_name), "result-%s.receipt", request.request_id);
    write_file(journal, receipt_name, "partial", 7);
    assert(!assistant_staged_install_read_result(journal, request.request_id, &recovered));
    assert(!assistant_staged_install_record_result(journal, request.request_id, true));
    unsigned char oversized[256] = {0}; write_file(journal, receipt_name, oversized, sizeof(oversized));
    assert(!assistant_staged_install_read_result(journal, request.request_id, &recovered));
    char receipt_path[2048]; snprintf(receipt_path, sizeof(receipt_path), "%s/%s", journal, receipt_name);
    assert(!unlink(receipt_path)); assert(!symlink("../title.tmd", receipt_path));
    assert(!assistant_staged_install_read_result(journal, request.request_id, &recovered));
    assert(!unlink(receipt_path));
    AssistantStagedInstallRequest other = request;
    other.request_id[0] = 'A'; other.tmd_sha256[0] = other.tmd_sha256[0] == 'A' ? 'B' : 'A';
    assert(assistant_staged_install_claim(journal, &other));
    assert(assistant_staged_install_record_result(journal, other.request_id, false));
    assert(assistant_staged_install_read_result(journal, other.request_id, &recovered) && !recovered);
    char claim_name[96]; snprintf(claim_name, sizeof(claim_name), "package-%s.claim", other.tmd_sha256);
    write_file(journal, claim_name, "changed", 7);
    assert(!assistant_staged_install_read_result(journal, other.request_id, &recovered));
    request.request_id[0] = 'F';
    assert(!assistant_staged_install_claim(journal, &request)); // Same package, different request: no replay.
    write_file(root, "00000001.app", app, 15);
    assert(!assistant_staged_install_identity(root, title, digest));
    write_file(root, "00000001.app", app, sizeof(app));
    tmd[0xB0B] = 2; tmd[0xB11] = 1; tmd[0xB13] = 0; // Hashed content, one 64 KiB block.
    write_file(root, "title.tmd", tmd, sizeof(tmd)); assert(assistant_policy_write(root, false));
    assert(!assistant_staged_install_identity(root, title, digest));
    unsigned char *hashed = calloc(1, 65536); assert(hashed);
    write_file(root, "00000001.app", hashed, 65536); free(hashed);
    assert(!assistant_staged_install_identity(root, title, digest));
    unsigned char h3[20] = {0}; write_file(root, "00000001.h3", h3, 20);
    assert(assistant_staged_install_identity(root, title, digest));
    tmd[0x193] = 2; write_file(root, "title.tmd", tmd, sizeof(tmd));
    assert(!assistant_staged_install_identity(root, title, digest)); // Stale TMD-bound policy.
    // The final failed no-replay claim intentionally retains its request file.
    const char *request_ids[] = {"0123456789ABCDEF0123456789ABCDEF", other.request_id, request.request_id};
    char cleanup_name[96];
    for (size_t i = 0; i < sizeof(request_ids) / sizeof(request_ids[0]); ++i) {
        int length = snprintf(cleanup_name, sizeof(cleanup_name), "request-%s.claim", request_ids[i]);
        assert(length >= 0 && (size_t)length < sizeof(cleanup_name));
        remove_fixture_entry(journal, cleanup_name, false);
    }
    const char *package_keys[] = {request.tmd_sha256, other.tmd_sha256};
    for (size_t i = 0; i < sizeof(package_keys) / sizeof(package_keys[0]); ++i) {
        int length = snprintf(cleanup_name, sizeof(cleanup_name), "package-%s.claim", package_keys[i]);
        assert(length >= 0 && (size_t)length < sizeof(cleanup_name));
        remove_fixture_entry(journal, cleanup_name, false);
    }
    int cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name), "result-%s.receipt", other.request_id);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(journal, cleanup_name, false);
    // The original receipt and its replacement symlink were removed above.
    assert(rmdir(journal) == 0);
    const char *fixture_files[] = {"title.tmd", "title.tik", "title.cert", "00000001.app", "00000001.h3", ASSISTANT_POLICY_FILE};
    for (size_t i = 0; i < sizeof(fixture_files) / sizeof(fixture_files[0]); ++i)
        remove_fixture_entry(root, fixture_files[i], false);
    assert(rmdir(root) == 0);
    puts("Staged install schema, identity, size, hashed-content, immutable result recovery and durable no-replay tests passed.");
    return 0;
}

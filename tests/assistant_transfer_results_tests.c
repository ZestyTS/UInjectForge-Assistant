#include "assistant_test_temp.h"
// Included after the production receiver functions by run-transfer-results.sh.
static AssistantState state;

static AssistantTransferResult result_for(const char *id, const char *status)
{
    AssistantTransferResult result = {0};
    result.has_result = true;
    result.success = true;
    copy_text(result.transfer_id, sizeof(result.transfer_id), id);
    copy_text(result.status, sizeof(result.status), status);
    copy_text(result.package_display_name, sizeof(result.package_display_name), "Link's Adventure");
    return result;
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

int main(int argc, char **argv)
{
    HttpRequest request = {0};
    state.paired = true;
    strcpy(state.access_token, "test-token");
    strcpy(request.authorization, "Bearer test-token");
    AssistantTransferResult a = result_for("A", "install-queued");
    AssistantTransferResult b = result_for("B", "install-queued");
    store_transfer_result(&state, &a);
    store_transfer_result(&state, &b);
    state.auto_install_pending = true;
    state.pending_install_result = b;
    state.install_active = true;
    g_install.automatic = true;
    g_install.transfer_result = a;
    handle_transfer_status(0, &state, &request, "B");
    assert(response_status == 200 && strstr(response_body, "\"status\":\"install-queued\""));
    assert(find_transfer_result(&state, "A") == &g_install.transfer_result);

    // A finishing must not erase B, even after a lost B commit response.
    a = result_for("A", "installed");
    store_transfer_result(&state, &a);
    state.install_active = false;
    handle_transfer_status(0, &state, &request, "B");
    assert(response_status == 200 && strstr(response_body, "\"transferId\":\"B\""));
    assert(strcmp(state.last_transfer_result.transfer_id, "A") == 0);
    state.auto_install_pending = false;
    b = result_for("B", "installed");
    store_transfer_result(&state, &b);
    handle_transfer_status(0, &state, &request, "B");
    assert(response_status == 200 && strstr(response_body, "\"status\":\"installed\""));
    if (argc > 1) {
        FILE *fixture = fopen(argv[1], "wb");
        assert(fixture);
        assert(fwrite(response_body, 1, strlen(response_body), fixture) == strlen(response_body));
        assert(fclose(fixture) == 0);
    }

    b.success = false;
    strcpy(b.status, "install-failed");
    store_transfer_result(&state, &b);
    handle_transfer_status(0, &state, &request, "B");
    assert(response_status == 200 && strstr(response_body, "\"success\":false"));
    assert(strstr(response_body, "\"status\":\"install-failed\""));

    // New uploads and expired results remain distinct from completed jobs.
    state.transfer.active = true;
    strcpy(state.transfer.transfer_id, "C");
    state.transfer.file_count = 1;
    strcpy(state.transfer.files[0].relative_path, "title.tmd");
    handle_transfer_status(0, &state, &request, "C");
    assert(response_status == 200 && strstr(response_body, "\"files\":["));
    for (int i = 0; i < 8; ++i) {
        char id[16];
        snprintf(id, sizeof(id), "new-%d", i);
        AssistantTransferResult next = result_for(id, "committed");
        store_transfer_result(&state, &next);
    }
    handle_transfer_status(0, &state, &request, "B");
    assert(response_status == 404);
    state.paired = false;
    handle_transfer_status(0, &state, &request, "C");
    assert(response_status == 401);
    // A fresh process can recover a terminal staged-install result, never a queued claim.
    char root[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-result"));
    char folder[1024];
    snprintf(folder, sizeof(folder), "%s/uinjectforge", root); assert(!mkdir(folder, 0700));
    snprintf(folder, sizeof(folder), "%s/uinjectforge/assistant", root); assert(!mkdir(folder, 0700));
    snprintf(folder, sizeof(folder), "%s/uinjectforge/assistant/install-requests", root); assert(!mkdir(folder, 0700));
    AssistantStagedInstallRequest staged = {0};
    strcpy(staged.request_id, "1234567890ABCDEF1234567890ABCDEF");
    strcpy(staged.title_id, "0005000212345678");
    memset(staged.tmd_sha256, 'A', 64);
    assert(assistant_staged_install_claim(folder, &staged));
    state.transfer.active = false;
    state.paired = true;
    copy_text(state.sd_root, sizeof(state.sd_root), root);
    handle_transfer_status(0, &state, &request, staged.request_id);
    assert(response_status == 404); // No conclusion from an interrupted/queued request.
    assert(assistant_staged_install_record_result(folder, staged.request_id, true));
    handle_transfer_status(0, &state, &request, staged.request_id);
    assert(response_status == 200 && strstr(response_body, "\"status\":\"installed\""));
    assert(strstr(response_body, "Current installed content was not verified"));
    assert(!assistant_staged_install_claim(folder, &staged));
    state.paired = false;
    handle_transfer_status(0, &state, &request, staged.request_id);
    assert(response_status == 401); // Durable results remain paired-only.
    state.transfer.active = false;
    state.paired = true;
    strcpy(state.access_token, "0123456789ABCDEF0123456789ABCDEF");
    strcpy(request.authorization, "Bearer 0123456789ABCDEF0123456789ABCDEF");
    strcpy(request.method, "POST");
    request.content_length = 32;
    request.body_prefix_len = 32;
    strcpy(request.body_prefix, "FEDCBA9876543210FEDCBA9876543210");
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 200 && state.pairing_trust.count == 1);
    assert(!state.access_token[0]);
    strcpy(request.method, "GET");
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 401); // Temporary session token was replaced, not persisted.
    strcpy(request.authorization, "Bearer FEDCBA9876543210FEDCBA9876543210");
    state.access_token[0] = '\0'; // Fresh process: only the persisted hash authorizes access.
    strcpy(request.method, "GET");
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 200);
    strcpy(request.method, "DELETE");
    state.transfer.active = true;
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 409 && state.pairing_trust.count == 1);
    state.transfer.active = false;
    pairing_storage_writable = false;
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 500 && state.pairing_trust.count == 1);
    pairing_storage_writable = true;
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 200 && state.pairing_trust.count == 0 && !state.paired);
    handle_remembered_pairing(0, &state, &request);
    assert(response_status == 401);
    char cleanup_name[96];
    int cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name), "request-%s.claim", staged.request_id);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(folder, cleanup_name, false);
    cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name), "package-%s.claim", staged.tmd_sha256);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(folder, cleanup_name, false);
    cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name), "result-%s.receipt", staged.request_id);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(folder, cleanup_name, false);
    assert(rmdir(folder) == 0);
    remove_fixture_entry(root, "uinjectforge/assistant", true);
    remove_fixture_entry(root, "uinjectforge", true);
    assert(rmdir(root) == 0);
    puts("Native transfer results: queued, overlapping, completed, failed, expired and authorization passed.");
    return 0;
}

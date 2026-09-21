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
    puts("Native transfer results: queued, overlapping, completed, failed, expired and authorization passed.");
    return 0;
}

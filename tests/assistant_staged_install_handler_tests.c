#include "assistant_test_temp.h"
static void put_file(const char *root, const char *name, const void *bytes, size_t size)
{
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(bytes, 1, size, file) == size); assert(!fclose(file));
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
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-install-handler"));
    AssistantState *state = calloc(1, sizeof(*state)); assert(state);
    snprintf(state->sd_root, sizeof(state->sd_root), "%s", root);
    char package[1024]; snprintf(package, sizeof(package), "%s/install/Game", root); assert(mkdir_recursive(package));
    unsigned char tmd[0xB04 + 48] = {0}, app[16] = {0};
    tmd[1] = 1; tmd[3] = 4; tmd[0x18D] = 5; tmd[0x18F] = 2; tmd[0x193] = 1;
    tmd[0x1DF] = 1; tmd[0xB07] = 1; tmd[0xB13] = 16;
    put_file(package, "title.tmd", tmd, sizeof(tmd)); put_file(package, "title.tik", "ticket", 6);
    put_file(package, "title.cert", "cert", 4); put_file(package, "00000001.app", app, 16);
    assert(assistant_policy_write(package, false));
    char title[17], digest[65]; assert(assistant_staged_install_identity(package, title, digest));
    HttpRequest *request = calloc(1, sizeof(*request)); assert(request);
    strcpy(request->authorization, "Bearer test");
    snprintf(request->body_prefix, sizeof(request->body_prefix), "{\"requestId\":\"0123456789abcdef0123456789abcdef\",\"packageName\":\"Game\",\"titleId\":\"%s\",\"tmdSha256\":\"%s\",\"storageTarget\":\"usb\"}", title, digest);
    request->content_length = (long long)strlen(request->body_prefix);
    state->install_enabled = state->auto_install_enabled = true;
    handle_staged_install(-1, state, request); assert(response_status == 401 && !state->auto_install_pending);
    state->paired = true;
    bool *busy[] = {&state->transfer.active, &state->receiving_file, &state->verifying_files, &state->install_active, &state->auto_install_pending, &state->title_launch_pending};
    for (unsigned i = 0; i < 6; ++i) {
        *busy[i] = true; handle_staged_install(-1, state, request); assert(response_status == 409); *busy[i] = false;
    }
    state->auto_install_enabled = false;
    handle_staged_install(-1, state, request); assert(response_status == 403);
    state->auto_install_enabled = true;
    catalog_ok = false; handle_staged_install(-1, state, request); assert(response_status == 409 && strstr(response_body, "error -42"));
    char preflight_error[256];
    AssistantStagedInstallRequest preflight_request;
    assert(assistant_staged_install_parse(request->body_prefix, &preflight_request));
    folder_result.state = ASSISTANT_TITLE_FOLDERS_ABSENT;
    assert(!validate_staged_install(package, &preflight_request, preflight_error, sizeof(preflight_error)));
    folder_result.error = 0; folder_result.checked_locations = 2;
    assert(validate_staged_install(package, &preflight_request, preflight_error, sizeof(preflight_error)));
    folder_result.state = ASSISTANT_TITLE_FOLDERS_PRESENT;
    assert(!validate_staged_install(package, &preflight_request, preflight_error, sizeof(preflight_error)));
    assert(strstr(preflight_error, "already installed"));
    folder_result.state = ASSISTANT_TITLE_FOLDERS_UNKNOWN;
    catalog_ok = true; catalog_partial = true;
    handle_staged_install(-1, state, request); assert(response_status == 409 && !state->auto_install_pending);
    catalog_partial = false; title_exists = true;
    unsigned before_folders = folder_checks;
    folder_result.state = ASSISTANT_TITLE_FOLDERS_ABSENT;
    handle_staged_install(-1, state, request); assert(response_status == 409 && strstr(response_body, "already installed"));
    assert(folder_checks == before_folders);
    title_exists = false; catalog_collision = true;
    handle_staged_install(-1, state, request); assert(response_status == 409);
    assert(folder_checks == before_folders);
    catalog_collision = false;
    request->content_length = 2049; handle_staged_install(-1, state, request); assert(response_status == 400);
    request->content_length = (long long)strlen(request->body_prefix);
    handle_staged_install(-1, state, request);
    assert(response_status == 202 && state->auto_install_pending && catalog_calls > 0);
    assert(state->auto_install_action.type == ASSISTANT_UI_ACTION_INSTALL_TO_USB);
    assert(!strcmp(state->pending_install_result.transfer_id, "0123456789ABCDEF0123456789ABCDEF"));
    assert(!strcmp(state->pending_install_result.post_transfer_action, "install-existing-usb"));
    assert(state->pending_install_result.uploaded_bytes == 0 && state->pending_install_result.uploaded_files == 0);
    state->auto_install_pending = false; // Simulate a restarted app after a lost response.
    handle_staged_install(-1, state, request); assert(response_status == 409 && !state->auto_install_pending);
    char error[256];
    tmd[0x193] = 2; put_file(package, "title.tmd", tmd, sizeof(tmd));
    assert(!validate_staged_install(package, &g_staged_install, error, sizeof(error)));
    free(request); free(state);
    char cleanup_name[160];
    int cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name),
        "uinjectforge/assistant/install-requests/request-%s.claim", g_staged_install.request_id);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(root, cleanup_name, false);
    cleanup_length = snprintf(cleanup_name, sizeof(cleanup_name),
        "uinjectforge/assistant/install-requests/package-%s.claim", g_staged_install.tmd_sha256);
    assert(cleanup_length >= 0 && (size_t)cleanup_length < sizeof(cleanup_name));
    remove_fixture_entry(root, cleanup_name, false);
    const char *fixture_files[] = {"title.tmd", "title.tik", "title.cert", "00000001.app", ASSISTANT_POLICY_FILE};
    for (size_t i = 0; i < sizeof(fixture_files) / sizeof(fixture_files[0]); ++i)
        remove_fixture_entry(package, fixture_files[i], false);
    const char *fixture_directories[] = {"install/Game", "install", "uinjectforge/assistant/install-requests", "uinjectforge/assistant", "uinjectforge"};
    for (size_t i = 0; i < sizeof(fixture_directories) / sizeof(fixture_directories[0]); ++i)
        remove_fixture_entry(root, fixture_directories[i], true);
    assert(rmdir(root) == 0);
    puts("Production staged-install handler authentication/busy/policy/catalog/collision/queue/no-replay tests passed.");
    return 0;
}

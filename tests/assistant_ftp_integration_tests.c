/* Actual main.c admission/callback/commit functions plus the real socket engine.
   Only platform lifecycle/MCP progress/HTTP reply sinks are host substitutes.
   All file bytes are synthetic and all paths descend from the runner's mktemp. */
typedef struct {
    AssistantState state;
    HttpRequest request;
    int control;
    uint16_t port;
    uint64_t now;
    char reply[4096];
    size_t reply_size;
} FtpIntegration;

static const unsigned char payload[] = "payload";
static unsigned fixture_number;

static int connect_loopback(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    return fd;
}

static void send_test_bytes(int fd, const void *bytes, size_t size)
{
    const char *cursor = bytes;
    while (size) {
        ssize_t n = send(fd, cursor, size, 0);
        assert(n > 0);
        cursor += n; size -= (size_t)n;
    }
}

static void integration_pump(FtpIntegration *test)
{
    assistant_ftp_pump(test->state.ftp_server, ++test->now, 1024);
    for (;;) {
        ssize_t n = recv(test->control, test->reply + test->reply_size,
            sizeof(test->reply) - test->reply_size - 1, 0);
        if (n <= 0) break;
        test->reply_size += (size_t)n;
        assert(test->reply_size < sizeof(test->reply) - 1);
        test->reply[test->reply_size] = 0;
    }
}

static void await_reply(FtpIntegration *test, const char *text)
{
    for (unsigned i = 0; i < 1000; i++) {
        integration_pump(test);
        if (strstr(test->reply, text)) return;
        struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
    }
    fprintf(stderr, "Missing expected FTP integration reply: %s\n", text);
    abort();
}

static void ftp_command(FtpIntegration *test, const char *command, const char *expected)
{
    test->reply_size = 0; test->reply[0] = 0;
    send_test_bytes(test->control, command, strlen(command));
    await_reply(test, expected);
}

static FtpIntegration *new_fixture(const char *root)
{
    FtpIntegration *test = calloc(1, sizeof(*test));
    assert(test);
    test->control = -1;
    char child[40]; snprintf(child, sizeof(child), "fixture-%u", ++fixture_number);
    assert(join_path_checked(test->state.sd_root, sizeof(test->state.sd_root), root, child));
    assert(mkdir(test->state.sd_root, 0700) == 0);
    test->state.paired = true;
    strcpy(test->state.access_token, "TEST_ONLY");
    strcpy(test->request.authorization, "Bearer TEST_ONLY");
    g_procui_exit_requested = false; proc_running = true;
    memset(&g_install, 0, sizeof(g_install));
    return test;
}

static void make_manifest(FtpIntegration *test, const char *transport, const char *name)
{
    unsigned char digest[32]; char sha[65];
    assert(mbedtls_sha256_ret(payload, sizeof(payload) - 1, digest, 0) == 0);
    digest_to_hex(digest, sha);
    int written = snprintf(test->request.body_prefix, sizeof(test->request.body_prefix),
        "{\"protocolVersion\":\"uif-assistant-transfer-v1\",%s"
        "\"packageDisplayName\":\"Synthetic integration only\",\"packageKind\":\"installable-wup\","
        "\"targetRootKind\":\"sd-install-folder\",\"targetRoot\":\"install/%s\","
        "\"postTransferAction\":\"copy-only\",\"installTarget\":\"usb\","
        "\"totalBytes\":7,\"fileCount\":1,\"files\":[{\"relativePath\":\"payload.bin\","
        "\"targetPath\":\"install/%s/payload.bin\",\"sizeBytes\":7,\"sha256\":\"%s\"}]}",
        transport, name, name, sha);
    assert(written > 0 && (size_t)written < sizeof(test->request.body_prefix));
    test->request.content_length = written;
    test->request.body_prefix_len = (size_t)written;
}

static void start_transaction(FtpIntegration *test)
{
    /* Supply an actual accepted IPv4 HTTP peer to production negotiation. The
       manifest body is already buffered, so this never reads from that peer. */
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    assert(bind(listener, (struct sockaddr *)&address, size) == 0 && listen(listener, 1) == 0);
    assert(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    int client = connect_loopback(ntohs(address.sin_port));
    int accepted = accept(listener, NULL, NULL); assert(accepted >= 0);
    handle_start_transfer(accepted, &test->state, &test->request);
    close(accepted); close(client); close(listener);
    if (response_status != 200 || !test->state.transfer.ftp_bulk) return;
    long long port = 0;
    assert(json_get_long(response_body, "ftpPort", &port) && port > 0 && port <= 65535);
    test->port = (uint16_t)port;
    assert(test->state.ftp_server && strstr(response_body, "\"bulkTransport\":\"ftp\""));
    test->control = connect_loopback(test->port);
    await_reply(test, "220 ");
    char user[128]; snprintf(user, sizeof(user), "USER %s\r\n", test->state.transfer.transfer_id);
    ftp_command(test, user, "331 ");
    char password[160]; snprintf(password, sizeof(password), "PASS %s\r\n", authorization_token(&test->request));
    ftp_command(test, password, "230 ");
    ftp_command(test, "TYPE I\r\n", "200 ");
}

static int start_payload(FtpIntegration *test)
{
    ftp_command(test, "EPSV\r\n", "229 ");
    const char *marker = strstr(test->reply, "(|||");
    unsigned port = 0; assert(marker && sscanf(marker, "(|||%u|)", &port) == 1 && port <= 65535);
    int data = connect_loopback((uint16_t)port);
    ftp_command(test, "STOR payload.bin\r\n", "150 ");
    assert(test->state.ftp_file && test->state.ftp_entry && test->state.receiving_file);
    return data;
}

static void complete_payload(FtpIntegration *test, const void *bytes, size_t size, const char *reply)
{
    int data = start_payload(test);
    test->reply_size = 0; test->reply[0] = 0;
    send_test_bytes(data, bytes, size); assert(shutdown(data, SHUT_WR) == 0);
    await_reply(test, reply); close(data);
    assert(!test->state.ftp_file && !test->state.ftp_entry && !test->state.ftp_stdio_buffer);
    assert(!test->state.receiving_file);
}

static void final_path(FtpIntegration *test, char path[UIF_MAX_PATH])
{
    char folder[UIF_MAX_PATH];
    assert(join_path_checked(folder, sizeof(folder), test->state.sd_root, test->state.transfer.target_root));
    assert(join_path_checked(path, UIF_MAX_PATH, folder, "payload.bin"));
}

static void cancel_transaction(FtpIntegration *test)
{
    char incoming[UIF_MAX_PATH], id[64];
    strcpy(incoming, test->state.transfer.incoming_root); strcpy(id, test->state.transfer.transfer_id);
    handle_cancel_transfer(-1, &test->state, &test->request, id);
    assert(response_status == 200 && !test->state.transfer.active && !test->state.ftp_server);
    assert(!test->state.ftp_file && !test->state.ftp_entry && !test->state.ftp_stdio_buffer);
    assert(!path_exists(incoming));
    assert(test->state.last_transfer_result.canceled && !test->state.last_transfer_result.success);
}

static void dispose_fixture(FtpIntegration *test)
{
    stop_ftp_transfer(&test->state);
    if (test->control >= 0) close(test->control);
    remove_tree(test->state.sd_root); free(test);
}

static void admission_and_http_modes(const char *root)
{
    const char *modes[] = {"", "\"bulkTransport\":\"http\",", "\"bulkTransport\":\"unsupported\","};
    for (unsigned i = 0; i < 3; i++) {
        FtpIntegration *test = new_fixture(root); make_manifest(test, modes[i], "ModeCheck");
        start_transaction(test);
        assert(response_status == (i == 2 ? 400 : 200));
        assert(!test->state.transfer.ftp_bulk && !test->state.ftp_server);
        assert(test->state.transfer.active == (i != 2));
        dispose_fixture(test);
    }
    FtpIntegration *test = new_fixture(root); make_manifest(test, "\"bulkTransport\":\"ftp\",", "AuthCheck");
    test->state.paired = false; start_transaction(test);
    assert(response_status == 401 && !test->state.transfer.active && !test->state.ftp_server);
    dispose_fixture(test);
    test = new_fixture(root); make_manifest(test, "\"bulkTransport\":\"ftp\",", "RememberedPeer");
    test->state.paired = false;
    strcpy(test->state.access_token, "DIFFERENT_OLD_SESSION");
    strcpy(test->request.authorization, "Bearer 0123456789ABCDEF0123456789ABCDEF");
    assert(assistant_pairing_remember(&test->state.pairing_trust, authorization_token(&test->request)));
    start_transaction(test); assert(response_status == 200);
    complete_payload(test, payload, 7, "226 ");
    cancel_transaction(test); dispose_fixture(test);
}

static void commit_seals_and_hashes_actual_staging(const char *root, bool tamper, bool busy_commit)
{
    FtpIntegration *test = new_fixture(root); make_manifest(test, "\"bulkTransport\":\"ftp\",", "NewPackage");
    start_transaction(test); assert(response_status == 200 && test->state.transfer.ftp_bulk);
    handle_commit(-1, &test->state, &test->request, test->state.transfer.transfer_id);
    assert(response_status == 409 && test->state.ftp_server && !test->state.transfer.ftp_sealed);
    http_body_calls = 0;
    handle_upload(-1, &test->state, &test->request, test->state.transfer.transfer_id, "payload.bin");
    assert(response_status == 409 && http_body_calls == 0);
    if (busy_commit) {
        int data = start_payload(test);
        handle_commit(-1, &test->state, &test->request, test->state.transfer.transfer_id);
        assert(response_status == 409 && test->state.ftp_server && !test->state.transfer.ftp_sealed);
        test->reply_size = 0; test->reply[0] = 0;
        send_test_bytes(data, payload, 7); shutdown(data, SHUT_WR); await_reply(test, "226 "); close(data);
    } else complete_payload(test, payload, 7, "226 ");
    assert(test->state.transfer.uploaded_files == 1 && test->state.transfer.uploaded_bytes == 7);
    assert(test->state.transfer.files[0].uploaded);
    if (tamper) {
        FILE *file = fopen(test->state.transfer.files[0].staged_path, "r+b");
        assert(file && fputc('X', file) != EOF && fclose(file) == 0);
    }
    char destination[UIF_MAX_PATH]; final_path(test, destination);
    controls = install_ticks = polls = renders = verification_controls = 0;
    handle_commit(-1, &test->state, &test->request, test->state.transfer.transfer_id);
    assert(!test->state.ftp_server && test->state.transfer.ftp_sealed);
    assert(verification_controls > 0 && controls == install_ticks && controls == polls);
    assert(!test->state.verifying_files);
    if (tamper) {
        assert(response_status == 400 && !path_exists(destination));
        assert(!test->state.last_transfer_result.success && test->state.transfer.active);
        cancel_transaction(test);
    } else {
        assert(response_status == 200 && !test->state.transfer.active);
        assert(test->state.last_transfer_result.success && !test->state.last_transfer_result.canceled);
        FILE *file = fopen(destination, "rb"); unsigned char bytes[8] = {0};
        assert(file && fread(bytes, 1, sizeof(bytes), file) == 7 && !memcmp(bytes, payload, 7));
        assert(fclose(file) == 0);
        char id[64]; strcpy(id, test->state.last_transfer_result.transfer_id);
        handle_commit(-1, &test->state, &test->request, id);
        assert(response_status == 200 && strstr(response_body, "\"replayed\":true"));
    }
    dispose_fixture(test);
}

static void failed_payloads_and_partial_cancel(const char *root)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        FtpIntegration *test = new_fixture(root); make_manifest(test, "\"bulkTransport\":\"ftp\",", "NeverPublished");
        start_transaction(test); assert(response_status == 200);
        if (mode < 2) {
            complete_payload(test, mode == 0 ? "short" : "overflow", mode == 0 ? 5 : 8, "426 ");
            assert(assistant_ftp_failed(test->state.ftp_server));
            assert(!test->state.transfer.files[0].uploaded && !test->state.transfer.uploaded_files && !test->state.transfer.uploaded_bytes);
            assert(!path_exists(test->state.transfer.files[0].staged_path));
            handle_commit(-1, &test->state, &test->request, test->state.transfer.transfer_id);
            assert(response_status == 400);
        } else {
            int data = start_payload(test); send_test_bytes(data, "pay", 3);
            for (unsigned i = 0; i < 1000 && test->state.ftp_written != 3; i++) {
                integration_pump(test); struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
            }
            assert(test->state.ftp_written == 3);
            cancel_transaction(test); close(data);
            assert(!test->state.receiving_file);
            dispose_fixture(test); continue;
        }
        char destination[UIF_MAX_PATH]; final_path(test, destination); assert(!path_exists(destination));
        cancel_transaction(test); dispose_fixture(test);
    }
}

static void separate_active_install_is_immutable(const char *root)
{
    FtpIntegration *test = new_fixture(root);
    test->state.install_active = true;
    strcpy(g_install.action.package_name, "OldOwnedPackage");
    g_install.automatic = true; g_install.transfer_result.has_result = true;
    strcpy(g_install.transfer_result.transfer_id, "OLD_INSTALL");
    strcpy(g_install.transfer_result.status, "installing");
    unsigned char before[sizeof(g_install)]; memcpy(before, &g_install, sizeof(before));
    char old_path[UIF_MAX_PATH]; assert(join_path_checked(old_path, sizeof(old_path), test->state.sd_root, "install/OldOwnedPackage/sentinel"));
    assert(ensure_parent_directory(old_path)); FILE *file = fopen(old_path, "wb");
    assert(file && fwrite("OLD", 1, 3, file) == 3 && fclose(file) == 0);
    make_manifest(test, "\"bulkTransport\":\"ftp\",", "OldOwnedPackage"); start_transaction(test);
    assert(response_status == 400 && !test->state.transfer.active);
    test->state.auto_install_pending = true;
    make_manifest(test, "\"bulkTransport\":\"ftp\",", "SeparateNext"); start_transaction(test);
    assert(response_status == 400 && !test->state.transfer.active);
    test->state.auto_install_pending = false;
    start_transaction(test); assert(response_status == 200);
    complete_payload(test, payload, 7, "226 ");
    handle_commit(-1, &test->state, &test->request, test->state.transfer.transfer_id);
    assert(response_status == 200 && test->state.install_active && !test->state.auto_install_pending);
    assert(!memcmp(before, &g_install, sizeof(before)));
    assert(find_transfer_result(&test->state, "OLD_INSTALL") == &g_install.transfer_result);
    assert(test->state.last_transfer_result.success && strcmp(test->state.last_transfer_result.transfer_id, "OLD_INSTALL"));
    unsigned char bytes[4] = {0}; file = fopen(old_path, "rb");
    assert(file && fread(bytes, 1, 4, file) == 3 && !memcmp(bytes, "OLD", 3) && fclose(file) == 0);
    dispose_fixture(test);
}

int main(int argc, char **argv)
{
    assert(argc == 2 && strlen(argv[1]) < 300);
    signal(SIGPIPE, SIG_IGN);
    admission_and_http_modes(argv[1]);
    commit_seals_and_hashes_actual_staging(argv[1], false, false);
    commit_seals_and_hashes_actual_staging(argv[1], true, false);
    commit_seals_and_hashes_actual_staging(argv[1], false, true);
    failed_payloads_and_partial_cancel(argv[1]);
    separate_active_install_is_immutable(argv[1]);
    assert(telemetry_events > 0);
    puts("Production FTP integration passed: admission, actual callbacks/TCP, full commit hash, tamper/short/extra, active-STOR conflict, cancellation, replay and isolated active-install ownership.");
    return 0;
}

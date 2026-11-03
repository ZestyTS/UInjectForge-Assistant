#include "assistant_test_temp.h"
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

typedef struct {
    int fd;
    const unsigned char *bytes;
    size_t size, fragment;
} Sender;

static void *send_bytes(void *context)
{
    Sender *s = context;
    for (size_t at = 0; at < s->size;) {
        size_t take = s->size - at < s->fragment ? s->size - at : s->fragment;
        ssize_t n = send(s->fd, s->bytes + at, take, 0);
        if (n <= 0) break;
        at += (size_t)n;
    }
    shutdown(s->fd, SHUT_WR);
    return NULL;
}

static void check_upload(const char *root, const unsigned char *data, size_t size,
    size_t prefix, size_t sent, bool bad_hash, bool exit_flag, bool home_exit, bool disk_full)
{
    static AssistantState state;
    static HttpRequest request;
    AssistantFile file = {0};
    memset(&state, 0, sizeof(state));
    memset(&request, 0, sizeof(request));
    polls = install_ticks = controls = discovery = broadcasts = renders = 0;
    fake_ticks = 1000;
    g_procui_exit_requested = exit_flag;
    proc_running = !home_exit;
    timing_message[0] = 0;
    snprintf(file.staged_path, sizeof(file.staged_path), "%s/staged.app", root);
    snprintf(file.relative_path, sizeof(file.relative_path), "staged.app");
    assert(access(file.staged_path, F_OK) != 0);
    upload_failure_path = disk_full ? file.staged_path : NULL;
    upload_failure_open_count = 0;
    upload_failure_write_fd = -1;
    unsigned char digest[32];
    assert(mbedtls_sha256_ret(data, size, digest, 0) == 0);
    digest_to_hex(digest, file.sha256);
    if (bad_hash) file.sha256[0] = file.sha256[0] == '0' ? '1' : '0';
    memcpy(request.x_sha256, file.sha256, sizeof(file.sha256));
    file.size_bytes = request.content_length = request.x_size = (long long)size;
    assert(prefix <= sizeof(request.body_prefix));
    request.body_prefix_len = prefix;
    memcpy(request.body_prefix, data, prefix);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    Sender sender = {.fd = sockets[1], .bytes = data + prefix, .size = sent, .fragment = 997};
    pthread_t thread;
    assert(pthread_create(&thread, NULL, send_bytes, &sender) == 0);
    char error[UIF_MAX_TEXT] = {0};
    bool success = receive_file_body(sockets[0], &state, &request, &file, error, sizeof(error));
    upload_failure_path = NULL;
    if (disk_full) {
        assert(upload_failure_open_count == 1 && upload_failure_write_fd >= 0);
        // Check closure before any later fixture operation could reuse this FD.
        errno = 0;
        int descriptor_status = fcntl(upload_failure_write_fd, F_GETFD);
        assert(descriptor_status == -1 && errno == EBADF);
    } else {
        assert(upload_failure_open_count == 0);
    }
    shutdown(sockets[0], SHUT_RDWR);
    assert(pthread_join(thread, NULL) == 0);
    close(sockets[0]); close(sockets[1]);
    bool expected = !bad_hash && !exit_flag && !home_exit && !disk_full && sent == size - prefix;
    assert(success == expected && file.uploaded == expected);
    assert(!state.receiving_file && state.receiving_file_path[0] == 0);
    assert(state.receiving_file_bytes == 0 && state.receiving_file_total_bytes == 0);
    assert(polls == install_ticks && polls == controls && polls == discovery && polls == broadcasts);
    assert(strstr(timing_message, expected ? "verified" : "failed"));
    if (expected) {
        FILE *readback = fopen(file.staged_path, "rb");
        assert(readback);
        unsigned char buffer[4096];
        size_t at = 0, n;
        while ((n = fread(buffer, 1, sizeof(buffer), readback)) > 0) {
            assert(at + n <= size && memcmp(buffer, data + at, n) == 0);
            at += n;
        }
        assert(!ferror(readback) && at == size);
        fclose(readback);
        char persisted_sha[65];
        long long persisted_size = -1;
        assert(hash_file_sha256(file.staged_path, persisted_sha, &persisted_size, NULL));
        assert(persisted_size == (long long)size && strcmp(persisted_sha, file.sha256) == 0);
        assert(hash_file_sha256(file.staged_path, persisted_sha, NULL, NULL));
        assert(remove(file.staged_path) == 0);
        if (size > UIF_UPLOAD_BUFFER) {
            unsigned reads = 0, pumps = 0;
            const char *calls = strrchr(timing_message, ';');
            assert(calls && sscanf(calls, "; %u reads/%u pumps", &reads, &pumps) == 2);
            assert(reads > pumps && pumps == polls);
            assert(polls <= (fake_ticks - 1000) / 25 + 1);
        }
    } else {
        assert(error[0] && access(file.staged_path, F_OK) != 0);
        if (disk_full) assert(strstr(error, "finalize") || strstr(error, "persist"));
        if (bad_hash) assert(strstr(error, "verification"));
        if (exit_flag || home_exit) assert(strstr(error, "exiting"));
        if (sent != size - prefix && !exit_flag && !home_exit) assert(strstr(error, "closed"));
    }
}

static void check_rejected_headers(const char *root)
{
    static AssistantState state;
    static HttpRequest request;
    AssistantFile file = {.size_bytes = 3};
    memset(&request, 0, sizeof(request));
    snprintf(file.staged_path, sizeof(file.staged_path), "%s/rejected.app", root);
    memset(file.sha256, '0', 64);
    request.x_size = 3;
    request.content_length = 4;
    memcpy(request.x_sha256, file.sha256, sizeof(file.sha256));
    char error[UIF_MAX_TEXT];
    assert(!receive_file_body(-1, &state, &request, &file, error, sizeof(error)));
    assert(strstr(error, "size") && access(file.staged_path, F_OK) != 0);
    request.content_length = 3;
    request.x_sha256[0] = '1';
    assert(!receive_file_body(-1, &state, &request, &file, error, sizeof(error)));
    assert(strstr(error, "SHA-256") && access(file.staged_path, F_OK) != 0);
    assert(!file.uploaded);
}

static void check_verification_progress(const char *root, const unsigned char *data, size_t size)
{
    char path[UIF_MAX_PATH], digest[65], expected[65];
    snprintf(path, sizeof(path), "%s/verify.app", root);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size && fclose(file) == 0);
    unsigned char hash[32];
    assert(mbedtls_sha256_ret(data, size, hash, 0) == 0);
    digest_to_hex(hash, expected);
    static AssistantState state;
    state.verifying_files = true;
    state.receiving_file_total_bytes = (long long)size;
    g_procui_exit_requested = false;
    proc_running = true;
    fake_tick_step = 100;
    renders = polls = 0;
    long long bytes = -1;
    assert(hash_file_sha256(path, digest, &bytes, &state));
    assert(bytes == (long long)size && state.receiving_file_bytes == bytes);
    assert(!strcmp(digest, expected) && renders > 0 && polls > 0 && state.file_last_activity_ticks > 0);
    g_procui_exit_requested = true;
    assert(!hash_file_sha256(path, digest, &bytes, &state) && bytes == 0);
    g_procui_exit_requested = false;
    proc_running = false;
    assert(!hash_file_sha256(path, digest, &bytes, &state) && bytes == 0);
    proc_running = true;
    fake_tick_step = 1;
    clear_receiving_file_state(&state);
    assert(!state.verifying_files && !state.verified_bytes && !state.file_last_activity_ticks);
    assert(remove(path) == 0);
}

static void check_idle_timeout(const char *root)
{
    static AssistantState state;
    static HttpRequest request;
    AssistantFile file = {.size_bytes = 3};
    memset(&state, 0, sizeof(state));
    memset(&request, 0, sizeof(request));
    snprintf(file.staged_path, sizeof(file.staged_path), "%s/idle.app", root);
    memset(file.sha256, '0', 64);
    request.x_size = request.content_length = 3;
    memcpy(request.x_sha256, file.sha256, sizeof(file.sha256));
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    char error[UIF_MAX_TEXT];
    g_procui_exit_requested = false;
    proc_running = true;
    fake_tick_step = 10000;
    assert(!receive_file_body(sockets[0], &state, &request, &file, error, sizeof(error)));
    fake_tick_step = 1;
    assert(strstr(error, "30 seconds") && !file.uploaded && !state.receiving_file);
    assert(access(file.staged_path, F_OK) != 0);
    close(sockets[0]); close(sockets[1]);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    char root[ASSISTANT_TEST_TEMP_ROOT_CAPACITY];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-upload-test"));
    size_t size = 2U * 1024U * 1024U + 731U;
    unsigned char *data = malloc(size);
    assert(data);
    for (size_t i = 0; i < size; i++) data[i] = (unsigned char)(i * 29U + 7U);
    check_upload(root, data, size, 193, size - 193, false, false, false, false);
    check_upload(root, data, 4096, 4096, 0, false, false, false, false);
    check_upload(root, data, 0, 0, 0, false, false, false, false);
    check_upload(root, data, 4097, 3, 4094, true, false, false, false);
    check_upload(root, data, 4097, 3, 4, false, false, false, false);
    check_upload(root, data, 4097, 3, 4094, false, true, false, false);
    check_upload(root, data, 4097, 3, 4094, false, false, true, false);
    check_upload(root, data, 4097, 4097, 0, false, false, false, true);
    check_rejected_headers(root);
    check_idle_timeout(root);
    check_verification_progress(root, data, size);
    char failed_sha[65] = "unchanged";
    long long failed_size = -1;
    assert(!hash_file_sha256(root, failed_sha, &failed_size, NULL));
    assert(failed_size == 0 && strcmp(failed_sha, "unchanged") == 0);
    free(data);
    assert(rmdir(root) == 0);
    puts("Production upload receiver: buffered fragments/tails, commit hash/readback, headers, idle timeout, EOF, HOME, cancel and flush failure passed.");
    return 0;
}

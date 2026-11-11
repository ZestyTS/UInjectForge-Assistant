#define _POSIX_C_SOURCE 200809L
#include "assistant_ftp.h"
#include "assistant_upload.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7F000001U
#endif

#define USER "0011223344556677"
#define PASSWORD "DUMMY_TEST_ONLY_0123456789ABCDEF"

typedef struct {
    unsigned char bytes[512 * 1024];
    size_t used;
    int begins, finishes, completes, progresses;
    bool reject_begin, reject_write, reject_finish;
    int64_t expected;
    int64_t progress;
    char path[768];
} Sink;

typedef struct {
    AssistantFtpServer *server;
    Sink sink;
    int control;
    uint16_t port;
    uint64_t now;
    char received[16384];
    size_t received_used;
} Fixture;

static bool begin(void *context, const char *path, int64_t *expected)
{
    Sink *sink = context;
    sink->begins++;
    if (sink->reject_begin || (strcmp(path, "folder/payload.bin") && strcmp(path, "zero.bin")
        && strcmp(path, "folder/UTF8-\xC3\xA9.bin"))) return false;
    assert(strlen(path) < sizeof(sink->path));
    strcpy(sink->path, path);
    sink->used = 0;
    *expected = sink->expected;
    return true;
}

static bool write_bytes(void *context, const void *bytes, size_t size)
{
    Sink *sink = context;
    if (sink->reject_write) return false;
    assert(size <= sizeof(sink->bytes) - sink->used);
    memcpy(sink->bytes + sink->used, bytes, size);
    sink->used += size;
    return true;
}

static bool finish(void *context, bool complete)
{
    Sink *sink = context;
    sink->finishes++;
    if (complete) sink->completes++;
    return !sink->reject_finish;
}

static void progress(void *context, int64_t value)
{
    Sink *sink = context;
    sink->progresses++;
    sink->progress = value;
}

static int connect_port(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    int enabled = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    return fd;
}

static void receive_control(Fixture *fixture)
{
    if (fixture->control < 0) return;
    for (;;) {
        assert(fixture->received_used < sizeof(fixture->received) - 1);
        ssize_t size = recv(fixture->control, fixture->received + fixture->received_used,
            sizeof(fixture->received) - fixture->received_used - 1, 0);
        if (size <= 0) break;
        fixture->received_used += (size_t)size;
        fixture->received[fixture->received_used] = 0;
    }
}

static void pump(Fixture *fixture, size_t budget)
{
    assistant_ftp_pump(fixture->server, ++fixture->now, budget);
    receive_control(fixture);
}

static void wait_text_at(Fixture *fixture, const char *text, const char *caller, unsigned int line)
{
    for (unsigned int attempt = 0; attempt < 1000; attempt++) {
        pump(fixture, 256 * 1024);
        if (strstr(fixture->received, text)) return;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    AssistantFtpDiagnostics diagnostics = {0};
    assert(assistant_ftp_get_diagnostics(fixture->server, &diagnostics));
    fprintf(stderr, "Missing expected public FTP reply at %s:%u: %s; received: %s; "
        "now=%llu busy=%d failed=%d begins=%d finishes=%d completes=%d bytes=%zu "
        "accepted-options=%d/%d receives=%llu eof=%llu errors=%llu\n",
        caller, line, text, fixture->received, (unsigned long long)fixture->now,
        assistant_ftp_busy(fixture->server), assistant_ftp_failed(fixture->server),
        fixture->sink.begins, fixture->sink.finishes, fixture->sink.completes, fixture->sink.used,
        diagnostics.receive_buffer.attempted, diagnostics.send_buffer.attempted,
        (unsigned long long)diagnostics.receive_calls, (unsigned long long)diagnostics.end_of_file,
        (unsigned long long)diagnostics.receive_errors);
    abort();
}

#define wait_text(fixture, text) wait_text_at(fixture, text, __func__, __LINE__)

/* A connected client does not promise that one nonblocking server pump has
   accepted it. Establish acceptance before measuring an idle data timeout. The
   public option-attempt fields are set by actual accepted-socket tuning; option
   success is deliberately irrelevant to this scheduling precondition. */
static void wait_data_accepted(Fixture *fixture)
{
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        pump(fixture, 1024);
        AssistantFtpDiagnostics diagnostics = {0};
        assert(assistant_ftp_get_diagnostics(fixture->server, &diagnostics));
        if (diagnostics.receive_buffer.attempted && diagnostics.send_buffer.attempted)
            return;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    fprintf(stderr, "Data socket was not accepted before the bounded host fixture deadline.\n");
    abort();
}

static void clear_received(Fixture *fixture)
{
    fixture->received_used = 0;
    fixture->received[0] = 0;
}

static void wait_size(Fixture *fixture, size_t expected, size_t budget)
{
    for (unsigned int attempt = 0; attempt < 1000; attempt++) {
        pump(fixture, budget);
        if (fixture->sink.used == expected) return;
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    assert(fixture->sink.used == expected);
}

static void wait_closed(Fixture *fixture, int fd)
{
    for (unsigned int attempt = 0; attempt < 1000; attempt++) {
        pump(fixture, 1024);
        char byte;
        ssize_t received = recv(fd, &byte, 1, 0);
        if (received == 0 || (received < 0 && errno == ECONNRESET)) return;
        assert(received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR));
        struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
    }
    assert(!"Socket did not close within bounded host deadline");
}

static void send_bytes(int fd, const void *bytes, size_t size)
{
    const char *cursor = bytes;
    while (size) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t sent = send(fd, cursor, size, flags);
        assert(sent > 0);
        cursor += sent; size -= (size_t)sent;
    }
}

static void send_command(Fixture *fixture, const char *text, const char *expected)
{
    clear_received(fixture);
    send_bytes(fixture->control, text, strlen(text));
    wait_text(fixture, expected);
}

static Fixture *create_with_peer(uint32_t peer)
{
    Fixture *fixture = calloc(1, sizeof(*fixture));
    assert(fixture);
    fixture->control = -1;
    fixture->sink.expected = 7;
    AssistantFtpCallbacks callbacks = {begin, write_bytes, finish, progress};
    fixture->server = assistant_ftp_start(&fixture->sink, USER, PASSWORD, peer, &callbacks, &fixture->port);
    assert(fixture->server && fixture->port);
    return fixture;
}

static Fixture *create(void) { return create_with_peer(htonl(INADDR_LOOPBACK)); }

static void connect_control(Fixture *fixture)
{
    fixture->control = connect_port(fixture->port);
    clear_received(fixture);
    wait_text(fixture, "220 ");
}

static void authenticate(Fixture *fixture)
{
    connect_control(fixture);
    send_command(fixture, "USER " USER "\r\n", "331 ");
    send_command(fixture, "PASS " PASSWORD "\r\n", "230 ");
    send_command(fixture, "TYPE I\r\n", "200 ");
}

static uint16_t passive(Fixture *fixture)
{
    send_command(fixture, "EPSV\r\n", "229 ");
    char *port_text = strstr(fixture->received, "(|||");
    assert(port_text);
    unsigned int port = 0;
    assert(sscanf(port_text, "(|||%u|)", &port) == 1 && port && port <= 65535);
    return (uint16_t)port;
}

static int start_file(Fixture *fixture, const char *path)
{
    int data = connect_port(passive(fixture));
    char line[1000];
    snprintf(line, sizeof(line), "STOR %s\r\n", path);
    send_command(fixture, line, "150 ");
    assert(assistant_ftp_busy(fixture->server));
    return data;
}

static void dispose(Fixture *fixture)
{
    assistant_ftp_destroy(fixture->server);
    if (fixture->control >= 0) close(fixture->control);
    memset(fixture, 0, sizeof(*fixture)); free(fixture);
}

static void successful_payloads(void)
{
    Fixture *f = create(); authenticate(f);
    int data = start_file(f, "folder/payload.bin");
    clear_received(f);
    send_bytes(data, "pay", 3);
    wait_size(f, 3, 1);
    assert(f->sink.used == 3 && f->sink.finishes == 0);
    send_bytes(data, "load", 4);
    wait_size(f, 7, 2);
    assert(f->sink.used == 7 && f->sink.finishes == 0);
    assert(!strstr(f->received, "226 "));
    assert(shutdown(data, SHUT_WR) == 0);
    wait_text(f, "226 "); close(data);
    assert(f->sink.finishes == 1 && f->sink.completes == 1 && f->sink.progress == 7);
    assert(!memcmp(f->sink.bytes, "payload", 7));
    assert(!assistant_ftp_busy(f->server) && !assistant_ftp_failed(f->server));

    /* Managed FTP may open a fresh control socket for each file. */
    close(f->control); f->control = -1;
    authenticate(f);
    f->sink.expected = 0;
    data = start_file(f, "zero.bin");
    clear_received(f); shutdown(data, SHUT_WR); wait_text(f, "226 "); close(data);
    assert(f->sink.begins == 2 && f->sink.finishes == 2 && f->sink.completes == 2 && f->sink.used == 0);

    f->sink.expected = 7;
    data = start_file(f, "folder/UTF8-\xC3\xA9.bin");
    clear_received(f); send_bytes(data, "payload", 7); shutdown(data, SHUT_WR);
    wait_text(f, "226 "); close(data);
    assert(f->sink.completes == 3 && !strcmp(f->sink.path, "folder/UTF8-\xC3\xA9.bin"));
    assert(assistant_ftp_seal(f->server));
    assert(assistant_ftp_seal(f->server));
    assert(!assistant_ftp_busy(f->server));
    dispose(f);
}

static void credentials_and_commands(void)
{
    Fixture *f = create(); connect_control(f);
    send_command(f, "STOR folder/payload.bin\r\n", "530 ");
    send_command(f, "USER wrong\r\n", "331 ");
    send_command(f, "PASS " PASSWORD "\r\n", "530 ");
    assert(!assistant_ftp_failed(f->server) && f->sink.begins == 0);
    close(f->control); f->control = -1;
    for (int i = 0; i < 5; i++) pump(f, 1024);
    connect_control(f);
    send_command(f, "USER " USER "\r\n", "331 ");
    send_command(f, "PASS wrong\r\n", "530 ");
    assert(!assistant_ftp_failed(f->server));
    close(f->control); f->control = -1;
    for (int i = 0; i < 5; i++) pump(f, 1024);
    connect_control(f);
    send_command(f, "USER " USER "\r\n", "331 ");
    send_command(f, "PASS " PASSWORD "\r\n", "230 ");
    send_command(f, "EPSV\r\n", "503 ");
    send_command(f, "TYPE A\r\n", "502 ");
    send_command(f, "TYPE I\r\n", "200 ");
    const char *unsupported[] = {"PORT 127,0,0,1,8,8\r\n", "EPRT |1|127.0.0.1|2048|\r\n",
        "PASV\r\n", "CWD /\r\n", "LIST\r\n", "RETR title.tik\r\n", "DELE file\r\n",
        "RNFR file\r\n", "RNTO elsewhere\r\n", "MKD dir\r\n", "SITE exec\r\n"};
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) send_command(f, unsupported[i], "502 ");
    assert(f->sink.begins == 0 && !assistant_ftp_busy(f->server));
    send_command(f, "NOOP\r\nQUIT\r\n", "221 ");
    assert(strstr(f->received, "200 "));
    assert(assistant_ftp_seal(f->server)); dispose(f);
}

static void fragmented_and_bounded_control(void)
{
    Fixture *f = create(); connect_control(f);
    const char *commands = "USER " USER "\r\nPASS " PASSWORD "\r\nTYPE I\r\n";
    clear_received(f);
    for (size_t i = 0; commands[i]; i++) { send_bytes(f->control, commands + i, 1); pump(f, 1024); }
    wait_text(f, "200 ");
    assert(strstr(f->received, "331 ") && strstr(f->received, "230 "));
    int extra = connect_port(f->port);
    for (int i = 0; i < 5; i++) pump(f, 1024);
    char byte;
    assert(recv(extra, &byte, 1, 0) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    send_command(f, "NOOP\r\n", "200 ");
    close(f->control); f->control = extra; clear_received(f);
    wait_text(f, "220 ");
    send_command(f, "STOR folder/payload.bin\r\n", "530 ");
    assert(assistant_ftp_seal(f->server)); dispose(f);

    const char *malformed[] = {"NOOP\n", "NO\rOP\r\n", "NO\tOP\r\n"};
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        f = create(); connect_control(f); send_command(f, malformed[i], "500 ");
        assert(f->sink.begins == 0); dispose(f);
    }
    f = create(); connect_control(f);
    char overlong[1100]; memset(overlong, 'A', sizeof(overlong));
    clear_received(f); send_bytes(f->control, overlong, sizeof(overlong)); wait_text(f, "500 ");
    assert(f->sink.begins == 0); dispose(f);
    f = create(); connect_control(f);
    const char nul[] = {'N', 'O', 0, 'O', 'P', '\r', '\n'};
    clear_received(f); send_bytes(f->control, nul, sizeof(nul)); wait_text(f, "500 "); dispose(f);
}

static void invalid_paths(void)
{
    const char *paths[] = {"../payload", "folder/../payload", "./payload", "/payload",
        "folder//payload", "folder\\payload", "sd:/payload", "folder/", ""};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        Fixture *f = create(); authenticate(f);
        int data = connect_port(passive(f));
        char command_line[1000]; snprintf(command_line, sizeof(command_line), "STOR %s\r\n", paths[i]);
        send_command(f, command_line, "550 ");
        assert(f->sink.begins == 0 && !assistant_ftp_busy(f->server));
        close(data); assert(assistant_ftp_seal(f->server)); dispose(f);
    }
    Fixture *f = create(); authenticate(f);
    int data = connect_port(passive(f));
    char command_line[900]; memset(command_line, 'x', sizeof(command_line));
    memcpy(command_line, "STOR ", 5); command_line[773] = '\r'; command_line[774] = '\n';
    clear_received(f); send_bytes(f->control, command_line, 775); wait_text(f, "550 ");
    assert(f->sink.begins == 0); close(data); dispose(f);
    f = create(); authenticate(f);
    data = connect_port(passive(f));
    send_command(f, "STOR not-in-manifest.bin\r\n", "550 ");
    assert(f->sink.begins == 1 && f->sink.finishes == 0);
    close(data); assert(assistant_ftp_seal(f->server)); dispose(f);
}

static void payload_failures(void)
{
    for (int scenario = 0; scenario < 7; scenario++) {
        Fixture *f = create(); authenticate(f);
        if (scenario == 2) f->sink.reject_write = true;
        if (scenario == 3) f->sink.reject_finish = true;
        if (scenario == 4) f->sink.expected = -1;
        int data;
        if (scenario == 4) {
            data = connect_port(passive(f));
            send_command(f, "STOR folder/payload.bin\r\n", "451 ");
        } else {
            data = start_file(f, "folder/payload.bin"); clear_received(f);
            if (scenario == 0) send_bytes(data, "short", 5);
            if (scenario == 1) send_bytes(data, "overflow", 8);
            if (scenario == 2 || scenario == 3) send_bytes(data, "payload", 7);
            if (scenario == 5) {
                close(f->control); f->control = -1;
                for (int i = 0; i < 1000 && f->sink.finishes == 0; i++) {
                    pump(f, 1024);
                    struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
                }
            } else if (scenario == 6) {
                assert(!assistant_ftp_seal(f->server));
            } else {
                shutdown(data, SHUT_WR); wait_text(f, "426 ");
            }
        }
        close(data);
        if (!assistant_ftp_failed(f->server)) fprintf(stderr, "Payload failure scenario %d was not marked failed.\n", scenario);
        assert(assistant_ftp_failed(f->server));
        assert(f->sink.finishes == 1);
        assert(f->sink.completes == (scenario == 3 ? 1 : 0));
        if (scenario == 1) assert(f->sink.used <= 7);
        assert(!assistant_ftp_seal(f->server));
        assert(f->sink.finishes == 1);
        dispose(f);
    }
}

static void pending_stall_and_seal(void)
{
    Fixture *idle = create(); connect_control(idle); clear_received(idle);
    idle->now += 30000; pump(idle, 1024); wait_text(idle, "421 ");
    assert(!assistant_ftp_failed(idle->server));
    close(idle->control); idle->control = -1;
    authenticate(idle); assert(assistant_ftp_seal(idle->server)); dispose(idle);
    for (int state = 0; state < 3; state++) {
        Fixture *f = create(); authenticate(f);
        int data = -1;
        uint16_t port = passive(f);
        if (state > 0) { data = connect_port(port); wait_data_accepted(f); }
        if (state == 2) send_command(f, "STOR folder/payload.bin\r\n", "150 ");
        assert(assistant_ftp_busy(f->server));
        if (state == 0) send_command(f, "EPSV\r\n", "503 ");
        clear_received(f); f->now += 30000; pump(f, 1024); wait_text(f, "426 ");
        assert(assistant_ftp_failed(f->server) && !assistant_ftp_busy(f->server));
        assert(f->sink.finishes == (state == 2 ? 1 : 0));
        assert(!assistant_ftp_seal(f->server));
        if (data >= 0) close(data);
        dispose(f);
    }
    Fixture *f = create(); authenticate(f);
    int data = connect_port(passive(f)); pump(f, 1024);
    assert(!assistant_ftp_seal(f->server));
    assert(assistant_ftp_failed(f->server) && f->sink.finishes == 0);
    close(data); dispose(f);

    f = create(); authenticate(f); data = start_file(f, "folder/payload.bin");
    f->now += 100; pump(f, 1024); f->now = 0; pump(f, 1024);
    assert(!assistant_ftp_failed(f->server));
    clear_received(f); send_bytes(data, "payload", 7); shutdown(data, SHUT_WR); wait_text(f, "226 ");
    close(data); assert(assistant_ftp_seal(f->server)); dispose(f);
}

static void payload_budget_and_wrong_peer(void)
{
    Fixture *f = create(); authenticate(f);
    f->sink.expected = 32768;
    int data = start_file(f, "folder/payload.bin");
    unsigned char bytes[32768];
    for (size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (unsigned char)i;
    send_bytes(data, bytes, sizeof(bytes));
    size_t old = f->sink.used;
    for (int i = 0; i < 8; i++) {
        pump(f, 17); assert(f->sink.used >= old && f->sink.used - old <= 17); old = f->sink.used;
    }
    pump(f, 0); assert(f->sink.used == old);
    shutdown(data, SHUT_WR); clear_received(f); wait_text(f, "226 "); close(data);
    assert(f->sink.used == sizeof(bytes) && !memcmp(f->sink.bytes, bytes, sizeof(bytes)));
    assert(assistant_ftp_seal(f->server)); dispose(f);

    /* The client is loopback; a different expected address must not receive a
       greeting. No alias binding, LAN contact, or platform configuration needed. */
    f = create_with_peer(htonl(0x7F000002U));
    f->control = connect_port(f->port);
    wait_closed(f, f->control);
    assert(f->received_used == 0 && f->sink.begins == 0 && !assistant_ftp_failed(f->server));
    assert(assistant_ftp_seal(f->server)); dispose(f);
}

static void owner_cadence_preserves_data_and_control(void)
{
    Fixture *f = create(); authenticate(f);
    f->sink.expected = 32768;
    int data = start_file(f, "folder/payload.bin");
    unsigned char bytes[32768];
    memset(bytes, 0x5a, sizeof(bytes));
    send_bytes(data, bytes, sizeof(bytes));
    AssistantUploadPump owner = {0};
    unsigned owner_calls = 0;
    size_t unpaced_bytes = 0;
    uint64_t last_service = 0;
    clear_received(f);
    send_bytes(f->control, "NOOP\r\n", 6);
    for (unsigned attempt = 0; attempt < 10000 && f->sink.used < sizeof(bytes); attempt++) {
        bool due = assistant_upload_owner_control_due(&owner, true, f->now + 1);
        if (due) {
            assert(!owner_calls || f->now + 1 - last_service >= 25);
            last_service = f->now + 1;
            assistant_upload_owner_control_complete(&owner, true, last_service);
            owner_calls++;
        }
        size_t old = f->sink.used;
        pump(f, 17); /* data/control work remains outside the platform cadence */
        assert(f->sink.used >= old && f->sink.used - old <= 17);
        if (!due) unpaced_bytes += f->sink.used - old;
        if (f->sink.used == old) {
            struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL);
        }
    }
    assert(f->sink.used == sizeof(bytes) && !memcmp(f->sink.bytes, bytes, sizeof(bytes)));
    assert(unpaced_bytes > 0 && owner_calls > 1);
    assert(strstr(f->received, "200 ")); /* control remains live during payload */
    assert(f->sink.finishes == 0 && assistant_ftp_busy(f->server));
    shutdown(data, SHUT_WR); wait_text(f, "226 "); close(data);
    assert(f->sink.finishes == 1 && f->sink.completes == 1);
    assert(assistant_ftp_seal(f->server)); dispose(f);
}

static void invalid_start(void)
{
    uint16_t port = 0;
    AssistantFtpCallbacks callbacks = {begin, write_bytes, finish, progress};
    assert(!assistant_ftp_start(NULL, "", PASSWORD, htonl(INADDR_LOOPBACK), &callbacks, &port));
    assert(!assistant_ftp_start(NULL, USER, "", htonl(INADDR_LOOPBACK), &callbacks, &port));
    assert(!assistant_ftp_start(NULL, USER, PASSWORD, 0, &callbacks, &port));
    assert(!assistant_ftp_start(NULL, USER, PASSWORD, htonl(INADDR_LOOPBACK), NULL, &port));
    assert(!assistant_ftp_start(NULL, USER, PASSWORD, htonl(INADDR_LOOPBACK), &callbacks, NULL));
    char long_value[130]; memset(long_value, 'X', sizeof(long_value) - 1); long_value[129] = 0;
    assert(!assistant_ftp_start(NULL, long_value, PASSWORD, htonl(INADDR_LOOPBACK), &callbacks, &port));
    assert(!assistant_ftp_start(NULL, USER, long_value, htonl(INADDR_LOOPBACK), &callbacks, &port));
    assert(!assistant_ftp_busy(NULL) && !assistant_ftp_failed(NULL) && !assistant_ftp_seal(NULL));
    assistant_ftp_pump(NULL, 0, 0); assistant_ftp_close(NULL); assistant_ftp_destroy(NULL);
}

int main(void)
{
    invalid_start();
    successful_payloads();
    credentials_and_commands();
    fragmented_and_bounded_control();
    invalid_paths();
    payload_failures();
    pending_stall_and_seal();
    payload_budget_and_wrong_peer();
    owner_cadence_preserves_data_and_control();
    puts("Scoped FTP real-loopback tests passed: auth, bounded/fragmented controls, peer pin, binary EPSV uploads, exact EOF, failures, budget, owner-service fairness, stall, seal, and reconnect.");
    return 0;
}

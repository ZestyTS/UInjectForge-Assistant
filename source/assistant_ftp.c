#include "assistant_ftp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define FTP_BUFFER (256U * 1024U)
#define FTP_LINE 1024U
#define FTP_OUTPUT 2048U
#define FTP_CREDENTIAL 128U
#define FTP_IDLE_MS 30000U
#define FTP_SOCKET_BUFFER (32 * 1024)

struct AssistantFtpServer {
    int listener, control, passive, data;
    uint32_t peer;
    void *context;
    AssistantFtpCallbacks callbacks;
    char user[FTP_CREDENTIAL + 1], password[FTP_CREDENTIAL + 1];
    char line[FTP_LINE], output[FTP_OUTPUT];
    size_t line_used, output_used, output_sent;
    bool user_ok, authenticated, binary, closing_control;
    bool active, failed, closed, clock_started;
    int64_t expected, received;
    uint64_t last_progress, control_activity, now;
    unsigned char buffer[FTP_BUFFER];
    AssistantFtpDiagnostics diagnostics;
};

static void add_counter(uint64_t *counter, uint64_t value)
{
    *counter = UINT64_MAX - *counter < value ? UINT64_MAX : *counter + value;
}

static int read_socket_option(int fd, int option, int *value)
{
    socklen_t size = sizeof(*value);
    *value = 0;
    if (getsockopt(fd, SOL_SOCKET, option, value, &size) < 0) {
        int error = errno ? errno : EIO;
        *value = 0;
        return error;
    }
    if (size != sizeof(*value)) { *value = 0; return EIO; }
    return 0;
}

static AssistantFtpSocketOption tune_socket_option(int fd, int option, int requested)
{
    AssistantFtpSocketOption result = {0};
    result.supported = result.attempted = true;
    result.requested = requested;
    result.before_error = read_socket_option(fd, option, &result.before_value);
    if (setsockopt(fd, SOL_SOCKET, option, &requested, sizeof(requested)) < 0)
        result.set_error = errno ? errno : EIO;
    result.after_error = read_socket_option(fd, option, &result.after_value);
    return result;
}

static void tune_data_socket(AssistantFtpServer *server, int fd)
{
    /* Match the public FTPiiU Wii U accepted-data-socket policy. These are
       best-effort performance options; failure never bypasses an upload guard
       or makes a valid transfer fail solely because tuning is unavailable. */
#if defined(__WIIU__) && defined(SO_WINSCALE)
    server->diagnostics.window_scale = tune_socket_option(fd, SO_WINSCALE, 1);
#else
    server->diagnostics.window_scale = (AssistantFtpSocketOption){0};
    server->diagnostics.window_scale.requested = 1;
    server->diagnostics.window_scale.before_error = ENOTSUP;
    server->diagnostics.window_scale.set_error = ENOTSUP;
    server->diagnostics.window_scale.after_error = ENOTSUP;
#endif
    server->diagnostics.receive_buffer = tune_socket_option(fd, SO_RCVBUF, FTP_SOCKET_BUFFER);
    server->diagnostics.send_buffer = tune_socket_option(fd, SO_SNDBUF, FTP_SOCKET_BUFFER);
}

static void reset_receive_diagnostics(AssistantFtpServer *server)
{
    AssistantFtpDiagnostics previous = server->diagnostics;
    server->diagnostics = (AssistantFtpDiagnostics){0};
    server->diagnostics.window_scale = previous.window_scale;
    server->diagnostics.receive_buffer = previous.receive_buffer;
    server->diagnostics.send_buffer = previous.send_buffer;
}

static void wipe(void *data, size_t size)
{
    volatile unsigned char *cursor = data;
    while (size--) *cursor++ = 0;
}

static bool would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static void close_socket(int *fd) { if (*fd >= 0) close(*fd); *fd = -1; }

static bool socket_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    return true;
}

static int make_listener(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = 0;
    socklen_t length = sizeof(address);
    if (!socket_nonblocking(fd) || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0
        || listen(fd, 1) < 0 || getsockname(fd, (struct sockaddr *)&address, &length) < 0) {
        close(fd); return -1;
    }
    *port = ntohs(address.sin_port);
    return fd;
}

static bool constant_equal(const char *actual, const char *expected)
{
    size_t actual_size = 0;
    while (actual_size <= FTP_CREDENTIAL && actual[actual_size]) actual_size++;
    size_t expected_size = strlen(expected);
    unsigned int difference = (unsigned int)(actual_size ^ expected_size);
    for (size_t i = 0; i < FTP_CREDENTIAL; i++) {
        unsigned char a = i < actual_size ? (unsigned char)actual[i] : 0;
        difference |= a ^ (unsigned char)expected[i];
    }
    return difference == 0;
}

static void finish_upload(AssistantFtpServer *server, bool complete)
{
    if (!server->active) return;
    server->active = false;
    bool persisted = server->callbacks.finish(server->context, complete);
    if (!complete || !persisted) server->failed = true;
}

static void close_data(AssistantFtpServer *server, bool complete)
{
    close_socket(&server->data);
    close_socket(&server->passive);
    finish_upload(server, complete);
}

static void reset_control(AssistantFtpServer *server)
{
    close_data(server, false);
    close_socket(&server->control);
    wipe(server->line, sizeof(server->line));
    server->line_used = server->output_used = server->output_sent = 0;
    server->user_ok = server->authenticated = server->binary = server->closing_control = false;
}

static void reply(AssistantFtpServer *server, const char *message)
{
    size_t size = strlen(message);
    if (server->output_sent) {
        memmove(server->output, server->output + server->output_sent, server->output_used - server->output_sent);
        server->output_used -= server->output_sent;
        server->output_sent = 0;
    }
    if (size > sizeof(server->output) - server->output_used) {
        reset_control(server); return;
    }
    memcpy(server->output + server->output_used, message, size);
    server->output_used += size;
}

static void flush_replies(AssistantFtpServer *server)
{
    if (server->control < 0) return;
    if (server->output_sent < server->output_used) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t sent = send(server->control, server->output + server->output_sent,
            server->output_used - server->output_sent, flags);
        if (sent > 0) server->output_sent += (size_t)sent;
        else if (sent == 0 || !would_block()) { reset_control(server); return; }
    }
    if (server->output_sent == server->output_used) {
        server->output_sent = server->output_used = 0;
        if (server->closing_control) reset_control(server);
    }
}

static bool safe_path(const char *path)
{
    size_t size = strlen(path);
    if (!size || size > 767 || path[0] == '/' || path[size - 1] == '/') return false;
    const char *part = path;
    for (const unsigned char *cursor = (const unsigned char *)path;; cursor++) {
        unsigned char c = *cursor;
        if (c && (c < 32 || c == 127 || c == '\\' || c == ':')) return false;
        if (!c || c == '/') {
            size_t length = (const char *)cursor - part;
            if (!length || (length == 1 && part[0] == '.')
                || (length == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (!c) return true;
            part = (const char *)cursor + 1;
        }
    }
}

static void command(AssistantFtpServer *server, char *line)
{
    char *argument = strchr(line, ' ');
    if (argument) *argument++ = 0;
    else argument = line + strlen(line);
    if (!strcasecmp(line, "QUIT") && !*argument) {
        close_data(server, false);
        reply(server, "221 Goodbye.\r\n"); server->closing_control = true; return;
    }
    if (!strcasecmp(line, "NOOP") && !*argument) { reply(server, "200 OK.\r\n"); return; }
    if (!strcasecmp(line, "USER")) {
        if (assistant_ftp_busy(server)) { reply(server, "503 Transfer active.\r\n"); return; }
        server->authenticated = server->binary = false;
        server->user_ok = constant_equal(argument, server->user);
        reply(server, "331 Password required.\r\n"); return;
    }
    if (!strcasecmp(line, "PASS")) {
        if (assistant_ftp_busy(server)) { reply(server, "503 Transfer active.\r\n"); return; }
        bool password_ok = constant_equal(argument, server->password);
        server->authenticated = server->user_ok && password_ok;
        reply(server, server->authenticated ? "230 Authenticated.\r\n" : "530 Authentication failed.\r\n");
        if (!server->authenticated) server->closing_control = true;
        return;
    }
    if (!server->authenticated) { reply(server, "530 Authentication required.\r\n"); return; }
    if (server->failed) { reply(server, "451 Transfer failed; cancel this transaction.\r\n"); return; }
    if (!strcasecmp(line, "TYPE") && !strcasecmp(argument, "I")) {
        if (server->active) reply(server, "503 Transfer active.\r\n");
        else { server->binary = true; reply(server, "200 Binary mode.\r\n"); }
        return;
    }
    if (!strcasecmp(line, "EPSV") && !*argument) {
        if (!server->binary) { reply(server, "503 Select TYPE I first.\r\n"); return; }
        if (assistant_ftp_busy(server)) { reply(server, "503 Passive channel already reserved.\r\n"); return; }
        uint16_t port;
        server->passive = make_listener(&port);
        if (server->passive < 0) { reply(server, "425 Cannot open data channel.\r\n"); return; }
        server->last_progress = server->now;
        char message[96];
        snprintf(message, sizeof(message), "229 Entering Extended Passive Mode (|||%u|).\r\n", (unsigned int)port);
        reply(server, message); return;
    }
    if (!strcasecmp(line, "STOR")) {
        if (!server->binary || server->active || (server->passive < 0 && server->data < 0)) {
            reply(server, "503 Reserve a passive data channel first.\r\n"); return;
        }
        if (!safe_path(argument)) { close_data(server, false); reply(server, "550 Invalid manifest path.\r\n"); return; }
        int64_t expected = -1;
        if (!server->callbacks.begin(server->context, argument, &expected)) {
            close_data(server, false); reply(server, "550 File not admitted.\r\n"); return;
        }
        reset_receive_diagnostics(server);
        server->active = true;
        server->expected = expected;
        server->received = 0;
        server->last_progress = server->now;
        if (expected < 0) {
            close_data(server, false); reply(server, "451 Invalid admitted size.\r\n"); return;
        }
        reply(server, "150 Opening binary data transfer.\r\n"); return;
    }
    reply(server, "502 Command unavailable.\r\n");
}

AssistantFtpServer *assistant_ftp_start(void *context, const char *user, const char *password,
    uint32_t peer, const AssistantFtpCallbacks *callbacks, uint16_t *port)
{
    if (!user || !password || !*user || !*password || !callbacks || !callbacks->begin
        || !callbacks->write || !callbacks->finish || !port || !peer
        || strlen(user) > FTP_CREDENTIAL || strlen(password) > FTP_CREDENTIAL) return NULL;
    AssistantFtpServer *server = calloc(1, sizeof(*server));
    if (!server) return NULL;
    server->listener = server->control = server->passive = server->data = -1;
    server->context = context; server->callbacks = *callbacks; server->peer = peer;
    strcpy(server->user, user); strcpy(server->password, password);
    server->listener = make_listener(port);
    if (server->listener < 0) { assistant_ftp_destroy(server); return NULL; }
    return server;
}

static bool accept_control(AssistantFtpServer *server)
{
    /* A sender may reconnect for the next file immediately after closing its
       previous control socket. Leave its bounded listener backlog pending until
       the old EOF has been serviced instead of rejecting that valid reconnect. */
    if (server->control >= 0) return false;
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = accept(server->listener, (struct sockaddr *)&address, &length);
    if (fd < 0) return false;
    if (address.sin_family != AF_INET || address.sin_addr.s_addr != server->peer
        || !socket_nonblocking(fd)) { close(fd); return false; }
    server->control = fd;
    server->control_activity = server->now;
    reply(server, "220 UIF scoped transfer endpoint.\r\n");
    return true;
}

static void accept_data(AssistantFtpServer *server)
{
    if (server->passive < 0) return;
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = accept(server->passive, (struct sockaddr *)&address, &length);
    if (fd < 0) return;
    if (address.sin_family != AF_INET || address.sin_addr.s_addr != server->peer
        || server->data >= 0 || !socket_nonblocking(fd)) { close(fd); return; }
    server->data = fd;
    tune_data_socket(server, fd);
    close_socket(&server->passive);
    server->last_progress = server->now;
}

static void read_control(AssistantFtpServer *server)
{
    unsigned int commands = 0;
    for (size_t bytes = 0; bytes < FTP_LINE && commands < 8 && server->control >= 0
        && !server->closing_control; bytes++) {
        unsigned char c;
        ssize_t received = recv(server->control, &c, 1, 0);
        if (received == 0) { reset_control(server); return; }
        if (received < 0) { if (!would_block()) reset_control(server); return; }
        server->control_activity = server->now;
        if (c == '\n') {
            if (!server->line_used || server->line[server->line_used - 1] != '\r') {
                reply(server, "500 CRLF required.\r\n"); server->closing_control = true; return;
            }
            server->line[server->line_used - 1] = 0;
            command(server, server->line);
            wipe(server->line, sizeof(server->line)); server->line_used = 0;
            commands++;
        } else if (!c || (c < 32 && c != '\r') || c == 127
            || (server->line_used && server->line[server->line_used - 1] == '\r')
            || server->line_used >= sizeof(server->line) - 1) {
            reply(server, "500 Invalid command line.\r\n"); server->closing_control = true; return;
        } else server->line[server->line_used++] = (char)c;
    }
}

void assistant_ftp_pump(AssistantFtpServer *server, uint64_t now_ms, size_t byte_budget)
{
    if (!server || server->closed) return;
    if (server->clock_started && now_ms < server->now) {
        server->last_progress = now_ms;
        server->control_activity = now_ms;
    }
    server->clock_started = true; server->now = now_ms;
    flush_replies(server);
    read_control(server);
    if (accept_control(server)) {
        flush_replies(server);
        read_control(server);
    }
    accept_data(server);
    if (byte_budget > FTP_BUFFER) byte_budget = FTP_BUFFER;
    size_t consumed = 0;
    if (server->active && server->data >= 0) add_counter(&server->diagnostics.pump_calls, 1);
    while (server->active && server->data >= 0 && consumed < byte_budget) {
        size_t remaining = byte_budget - consumed;
        /* Read at most one byte beyond the manifest size to reject overruns
           without ever writing excess bytes into the owner's staging file. */
        int64_t file_remaining = server->expected - server->received;
        if ((uint64_t)file_remaining < remaining) remaining = (size_t)file_remaining + 1;
        add_counter(&server->diagnostics.receive_calls, 1);
        ssize_t received = recv(server->data, server->buffer, remaining, 0);
        if (received == 0) {
            add_counter(&server->diagnostics.end_of_file, 1);
            bool complete = server->received == server->expected;
            close_data(server, complete);
            server->control_activity = now_ms;
            reply(server, complete && !server->failed ? "226 Transfer complete.\r\n" : "426 Incomplete transfer.\r\n");
            break;
        }
        if (received < 0) {
            if (errno == EINTR) add_counter(&server->diagnostics.interrupted, 1);
            else if (would_block()) add_counter(&server->diagnostics.would_block, 1);
            else {
                add_counter(&server->diagnostics.receive_errors, 1);
                close_data(server, false); reply(server, "426 Data connection failed.\r\n");
            }
            break;
        }
        add_counter(&server->diagnostics.positive_receives, 1);
        add_counter(&server->diagnostics.received_bytes, (uint64_t)received);
        size_t bucket = received < 1024 ? 0 : received < 16384 ? 1 : received < 65536 ? 2 : 3;
        add_counter(&server->diagnostics.receive_size_buckets[bucket], 1);
        if ((int64_t)received > file_remaining
            || !server->callbacks.write(server->context, server->buffer, (size_t)received)) {
            close_data(server, false); reply(server, "426 Invalid or failed payload.\r\n"); break;
        }
        consumed += (size_t)received; server->received += received;
        server->last_progress = now_ms;
        server->control_activity = now_ms;
        if (server->callbacks.progress) server->callbacks.progress(server->context, server->received);
    }
    if (assistant_ftp_busy(server) && now_ms - server->last_progress >= FTP_IDLE_MS) {
        server->failed = true; close_data(server, false);
        server->control_activity = now_ms;
        reply(server, "426 Data transfer stalled.\r\n");
    }
    if (server->control >= 0 && !assistant_ftp_busy(server)
        && now_ms - server->control_activity >= FTP_IDLE_MS) {
        reply(server, "421 Control connection idle.\r\n");
        server->closing_control = true;
    }
    flush_replies(server);
}

bool assistant_ftp_get_diagnostics(const AssistantFtpServer *server, AssistantFtpDiagnostics *diagnostics)
{
    if (!server || !diagnostics) return false;
    *diagnostics = server->diagnostics;
    return true;
}

bool assistant_ftp_busy(const AssistantFtpServer *server)
{
    return server && (server->active || server->data >= 0 || server->passive >= 0);
}

bool assistant_ftp_failed(const AssistantFtpServer *server) { return server && server->failed; }

bool assistant_ftp_seal(AssistantFtpServer *server)
{
    if (!server) return false;
    bool good = !assistant_ftp_busy(server) && !server->failed;
    if (!good) server->failed = true;
    assistant_ftp_close(server);
    return good;
}

void assistant_ftp_close(AssistantFtpServer *server)
{
    if (!server) return;
    if (assistant_ftp_busy(server)) server->failed = true;
    reset_control(server); close_socket(&server->listener);
    server->closed = true;
    wipe(server->password, sizeof(server->password)); wipe(server->user, sizeof(server->user));
}

void assistant_ftp_destroy(AssistantFtpServer *server)
{
    if (!server) return;
    assistant_ftp_close(server); wipe(server, sizeof(*server)); free(server);
}

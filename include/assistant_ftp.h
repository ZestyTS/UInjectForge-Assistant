#ifndef ASSISTANT_FTP_H
#define ASSISTANT_FTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A transfer-scoped, write-only FTP endpoint. No filesystem access is performed
   here. The owner must bind begin/write/finish to its admitted manifest staging
   transaction, and seal successfully before hashing or publishing that staging. */
typedef struct AssistantFtpServer AssistantFtpServer;

/* Experimental numeric diagnostics only. Zero values with nonzero error codes
   are unknown, never evidence that the socket accepted an option. */
typedef struct {
    int requested, before_value, before_error, set_error, after_value, after_error;
    bool supported, attempted;
} AssistantFtpSocketOption;

typedef struct {
    AssistantFtpSocketOption window_scale, receive_buffer, send_buffer;
    uint64_t pump_calls, receive_calls, positive_receives, received_bytes;
    uint64_t would_block, interrupted, end_of_file, receive_errors, receive_size_buckets[4];
} AssistantFtpDiagnostics;

typedef struct {
    bool (*begin)(void *context, const char *relative_path, int64_t *expected_bytes);
    bool (*write)(void *context, const void *bytes, size_t size);
    /* Called exactly once after an accepted begin. complete requires exact size
       and data EOF. Return false for a flush/close/persisted-size failure. */
    bool (*finish)(void *context, bool complete);
    void (*progress)(void *context, int64_t file_bytes);
} AssistantFtpCallbacks;

/* peer_ipv4_network_order is the paired HTTP client's IPv4 address. Both FTP
   channels are restricted to that address. Credentials are copied, never logged.
   Port is ephemeral and returned in host order. One server owns one transaction. */
AssistantFtpServer *assistant_ftp_start(void *context, const char *user,
    const char *password, uint32_t peer_ipv4_network_order,
    const AssistantFtpCallbacks *callbacks, uint16_t *control_port_host_order);

/* Cooperative, nonblocking networking. now_ms must be monotonic; no clock or
   sleep is used internally. At most byte_budget payload bytes (max 256 KiB) are
   passed to write per call. Controls are independently bounded. A 30-second
   period without data progress aborts an admitted/pending data connection.
   An idle control connection is also closed after 30 seconds; it can reconnect
   without failing a transaction that has no admitted or pending upload. */
void assistant_ftp_pump(AssistantFtpServer *server, uint64_t now_ms, size_t byte_budget);
/* Snapshot only; does not service sockets or change transaction state. Payload
   counters restart at admitted STOR; options describe its current data socket. */
bool assistant_ftp_get_diagnostics(const AssistantFtpServer *server, AssistantFtpDiagnostics *diagnostics);
bool assistant_ftp_busy(const AssistantFtpServer *server);
bool assistant_ftp_failed(const AssistantFtpServer *server);

/* Closes listeners and every socket. Returns false if any payload or pending
   passive channel existed, or a previous admitted upload failed. After sealing,
   no FTP writer can race the owner's final complete-byte verification. */
bool assistant_ftp_seal(AssistantFtpServer *server);
void assistant_ftp_close(AssistantFtpServer *server);
void assistant_ftp_destroy(AssistantFtpServer *server);

#endif

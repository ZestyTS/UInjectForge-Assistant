#include <assert.h>

static int response_status;
static char response_body[262144];
// Persistence is tested separately against real files. Inject a storage failure
// here to exercise the production HTTP handler's rollback behavior.
static bool pairing_storage_writable = true;
static bool read_body_to_memory(int fd, const HttpRequest *request, char **body)
{
    (void)fd;
    if (request->content_length != 32 || request->body_prefix_len != 32) return false;
    *body = malloc(33);
    assert(*body);
    memcpy(*body, request->body_prefix, 32);
    (*body)[32] = '\0';
    return true;
}
static bool save_pairing_trust(AssistantState *state, const AssistantPairingTrust *trust)
{
    if (!pairing_storage_writable) return false;
    state->pairing_trust = *trust;
    return true;
}
static void send_json(int fd, int status, const char *reason, const char *body)
{
    (void)fd; (void)reason;
    response_status = status;
    assert(strlen(body) < sizeof(response_body));
    strcpy(response_body, body);
}
static void send_message(int fd, int status, const char *reason, const char *message)
{
    send_json(fd, status, reason, message);
}

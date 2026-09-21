#include <assert.h>

static int response_status;
static char response_body[262144];
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

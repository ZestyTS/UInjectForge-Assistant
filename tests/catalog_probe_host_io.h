#include <assert.h>
static bool authorized;
static int response_status, probe_calls;
static char *response_body;
static AssistantCatalogProbeKind received_kind;
static uint32_t received_app;
static bool fail_probe;
static unsigned folder_probe_calls;
static AssistantTitleFolderResult folder_result = {ASSISTANT_TITLE_FOLDERS_UNKNOWN, "test-unknown", -9, 0};
bool assistant_title_folder_id_valid(const char *id) { return !strcmp(id, "0005000200000001"); }
AssistantTitleFolderResult assistant_check_title_folders(const char *id)
{ assert(assistant_title_folder_id_valid(id)); folder_probe_calls++; return folder_result; }
static bool authorization_ok(AssistantState *state, const HttpRequest *request)
{ (void)state; (void)request; return authorized; }
static void send_message(int fd, int status, const char *reason, const char *message)
{ (void)fd; (void)reason; (void)message; response_status = status; }
static void send_json(int fd, int status, const char *reason, const char *body)
{ (void)fd; (void)reason; response_status = status; free(response_body); response_body = strdup(body); }
bool assistant_probe_title_catalog(AssistantCatalogProbeKind kind, uint32_t app,
    AssistantCatalogProbeVisitor visitor, void *context, AssistantCatalogProbeResult *result)
{
    probe_calls++; received_kind = kind; received_app = app;
    *result = (AssistantCatalogProbeResult){"captured", 0, 407, 1, 407, 39479, 0, true};
    AssistantCatalogProbeRecord record = {"0005000012345678", "/vol/quoted\"/test\\path", "usb", 0x80000000, 1, true};
    if (!visitor(&record, context)) { result->stage = "response-capacity"; return false; }
    result->exported_count = 1;
    if (fail_probe) { result->stage = "list"; result->error = -15; return false; }
    return true;
}

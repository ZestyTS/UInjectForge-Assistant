#include <assert.h>
static int response_status;
static char response_body[2048];
static bool catalog_ok = true, title_exists, catalog_collision, catalog_partial;
static unsigned catalog_calls;
static unsigned folder_checks;
static AssistantTitleFolderResult folder_result = {ASSISTANT_TITLE_FOLDERS_UNKNOWN, "test-unknown", -9, 0};
AssistantTitleFolderResult assistant_check_title_folders(const char *id)
{ assert(!strcmp(id, "0005000200000001")); folder_checks++; return folder_result; }
static bool authorization_ok(const AssistantState *state, const HttpRequest *request)
{ return state->paired && !strcmp(request->authorization, "Bearer test"); }
static bool read_body_to_memory(int fd, const HttpRequest *request, char **body)
{
    (void)fd;
    *body = malloc((size_t)request->content_length + 1); assert(*body);
    memcpy(*body, request->body_prefix, (size_t)request->content_length);
    (*body)[request->content_length] = 0;
    return true;
}
static void send_json(int fd, int status, const char *reason, const char *body)
{ (void)fd; (void)reason; response_status = status; snprintf(response_body, sizeof(response_body), "%s", body); }
static void send_message(int fd, int status, const char *reason, const char *body) { send_json(fd, status, reason, body); }
static void set_status(AssistantState *state, const char *text) { snprintf(state->status, sizeof(state->status), "%s", text); }
static void join_path(char *path, size_t size, const char *root, const char *child) { snprintf(path, size, "%s/%s", root, child); }
static bool mkdir_recursive(const char *path)
{
    char copy[1024]; snprintf(copy, sizeof(copy), "%s", path);
    for (char *p = copy + 1; *p; ++p) if (*p == '/') { *p = 0; if (mkdir(copy, 0700) && errno != EEXIST) return false; *p = '/'; }
    return !mkdir(copy, 0700) || errno == EEXIST;
}
static bool validate_install_package_for_install(const AssistantState *state, const char *name,
    char *path, size_t size, char *mcp, size_t mcp_size, char *error, size_t error_size)
{
    (void)error; (void)error_size;
    snprintf(path, size, "%s/install/%s", state->sd_root, name);
    snprintf(mcp, mcp_size, "/vol/app_sd/install/%s/", name);
    return true;
}
static bool SYSCheckTitleExists(uint64_t title) { (void)title; return title_exists; }
bool assistant_visit_title_catalog(AssistantTitleVisitor visitor, void *context, bool *truncated)
{
    ++catalog_calls;
    *truncated = catalog_partial;
    if (catalog_collision) { AssistantTitleLocation title = {0}; strcpy(title.title_id, "0005000200000001"); visitor(&title, context); }
    return catalog_ok;
}
const char *assistant_catalog_stage(void) { return catalog_ok ? "complete" : "list"; }
int32_t assistant_catalog_error(void) { return catalog_ok ? 0 : -42; }

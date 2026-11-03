#include "assistant_title_folders.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef UIF_ASSISTANT_HOST_TEST
#include "title_folders_host_platform.h"
#else
#include <coreinit/filesystem_fsa.h>
#include <mocha/mocha.h>
#endif

bool assistant_title_folder_id_valid(const char *title_id)
{
    if (!title_id || strnlen(title_id, 17) != 16 || strncmp(title_id, "00050002", 8)) return false;
    for (unsigned i = 8; i < 16; ++i) {
        char c = title_id[i];
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;
    }
    return true;
}

bool assistant_open_title_folder_session(AssistantTitleFolderSession *session, AssistantTitleFolderResult *result)
{
    if (!session || !result) return false;
    *result = (AssistantTitleFolderResult){ASSISTANT_TITLE_FOLDERS_UNKNOWN, "invalid-session", 0, 0};
    if (session->ready) return false;
    session->client = -1;
    int32_t error = Mocha_InitLibrary();
    if (error) { result->stage = "mocha-init"; result->error = error; return false; }
    result->stage = "fsa-init";
    if ((error = FSAInit()) != FS_ERROR_OK) goto finish;
    result->stage = "fsa-client";
    session->client = FSAAddClient(NULL);
    if (session->client < 0) { error = session->client; goto finish; }
    result->stage = "fs-permission";
    if ((error = Mocha_UnlockFSClientEx(session->client)) != 0) goto finish;
    session->ready = true;
    result->stage = "session-ready";
finish:
    result->error = error;
    // Finish all Mocha/MCP traffic before an install context can be established.
    int32_t deinit_error = Mocha_DeInitLibrary();
    if (deinit_error) { result->stage = "mocha-close"; result->error = deinit_error; session->ready = false; }
    if (!session->ready && session->client >= 0) {
        int32_t close_error = FSADelClient(session->client);
        if (close_error) { result->stage = "fsa-close"; result->error = close_error; }
        session->client = -1;
    }
    return session->ready;
}

int32_t assistant_close_title_folder_session(AssistantTitleFolderSession *session)
{
    if (!session || !session->ready) return 0;
    int32_t error = FSADelClient(session->client);
    session->ready = false;
    session->client = -1;
    return error;
}

AssistantTitleFolderResult assistant_check_title_folders_in_session(const char *title_id, const AssistantTitleFolderSession *session)
{
    AssistantTitleFolderResult result = {ASSISTANT_TITLE_FOLDERS_UNKNOWN, "invalid-title-id", 0, 0};
    if (!assistant_title_folder_id_valid(title_id)) return result;
    if (!session || !session->ready || session->client < 0) { result.stage = "invalid-session"; return result; }
    FSAClientHandle client = session->client;
    int32_t error = 0;

    const char *roots[] = {"/vol/storage_mlc01/usr/title", "/vol/storage_usb01/usr/title", "/vol/storage_usb02/usr/title"};
    bool available[3] = {false, false, false};
    FSAStat stat;
    for (unsigned i = 0; i < 3; ++i) {
        result.stage = "storage-root";
        memset(&stat, 0, sizeof(stat));
        error = FSAGetStat(client, roots[i], &stat);
        if (i == 2 && error == FS_ERROR_NOT_FOUND) { error = 0; continue; }
        if (error != FS_ERROR_OK) goto finish;
        if (!(stat.flags & FS_STAT_DIRECTORY)) { result.stage = "invalid-storage-root"; goto finish; }
        available[i] = true;
    }
    unsigned long low = strtoul(title_id + 8, NULL, 16);
    for (unsigned i = 0; i < 3; ++i) {
        if (!available[i]) continue;
        char path[128];
        snprintf(path, sizeof(path), "%s/00050002/%08lx", roots[i], low);
        result.stage = "title-folder";
        memset(&stat, 0, sizeof(stat));
        error = FSAGetStat(client, path, &stat);
        if (error == FS_ERROR_OK) {
            result.state = ASSISTANT_TITLE_FOLDERS_PRESENT;
            result.stage = "title-exists";
            goto finish;
        }
        if (error != FS_ERROR_NOT_FOUND) goto finish;
        result.checked_locations++;
    }
    // A missing root is a disconnected/unreadable device, not an empty library.
    // Recheck roots after the target lookups, including the optional second USB.
    for (unsigned i = 0; i < 3; ++i) {
        result.stage = "storage-recheck";
        memset(&stat, 0, sizeof(stat));
        error = FSAGetStat(client, roots[i], &stat);
        if (!available[i] && error == FS_ERROR_NOT_FOUND) { error = 0; continue; }
        if (error != FS_ERROR_OK) goto finish;
        if (!available[i] || !(stat.flags & FS_STAT_DIRECTORY)) { result.stage = "storage-changed"; goto finish; }
    }
    result.state = ASSISTANT_TITLE_FOLDERS_ABSENT;
    result.stage = "folders-absent";
    error = 0;
finish:
    result.error = error;
    return result;
}

AssistantTitleFolderResult assistant_check_title_folders(const char *title_id)
{
    AssistantTitleFolderResult result = {ASSISTANT_TITLE_FOLDERS_UNKNOWN, "invalid-title-id", 0, 0};
    if (!assistant_title_folder_id_valid(title_id)) return result;
    AssistantTitleFolderSession session = {-1, false};
    if (!assistant_open_title_folder_session(&session, &result)) return result;
    result = assistant_check_title_folders_in_session(title_id, &session);
    int32_t close_error = assistant_close_title_folder_session(&session);
    if (close_error && result.state != ASSISTANT_TITLE_FOLDERS_PRESENT) {
        result.state = ASSISTANT_TITLE_FOLDERS_UNKNOWN;
        result.stage = "fsa-close";
        result.error = close_error;
    }
    return result;
}

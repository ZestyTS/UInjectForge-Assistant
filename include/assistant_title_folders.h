#ifndef ASSISTANT_TITLE_FOLDERS_H
#define ASSISTANT_TITLE_FOLDERS_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ASSISTANT_TITLE_FOLDERS_UNKNOWN,
    ASSISTANT_TITLE_FOLDERS_ABSENT,
    ASSISTANT_TITLE_FOLDERS_PRESENT
} AssistantTitleFolderState;

typedef struct {
    AssistantTitleFolderState state;
    const char *stage;
    int32_t error;
    uint32_t checked_locations;
} AssistantTitleFolderResult;

typedef struct {
    int32_t client;
    bool ready;
} AssistantTitleFolderSession;

/* Only generated-title directory metadata is read. Errors never prove absence. */
bool assistant_title_folder_id_valid(const char *title_id);
AssistantTitleFolderResult assistant_check_title_folders(const char *title_id);
bool assistant_open_title_folder_session(AssistantTitleFolderSession *session, AssistantTitleFolderResult *result);
AssistantTitleFolderResult assistant_check_title_folders_in_session(const char *title_id, const AssistantTitleFolderSession *session);
int32_t assistant_close_title_folder_session(AssistantTitleFolderSession *session);
#endif

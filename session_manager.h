// part of aiterm project
// session_manager.h
// Header File used to manage sessions
// By: Peter Talbott
// Assisted by: Gemini
// May 2026


#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <uuid/uuid.h>
#include "gui.h"
#include "utils.h"

// structure to handle threaded mysql transactions
typedef enum {
    CMD_SESSION_LIST,
    CMD_SESSION_NEW,
    CMD_SESSION_LOAD,
    CMD_SESSION_DELETE,
    CMD_SESSION_SHOW,        // New: Show current session info
    CMD_SESSION_DESCRIPTION,  // New: Update description
    CMD_SESSION_DEFAULT,
    CMD_SESSION_NO_DEFAULT
} SessionCmdType;

typedef struct {
    AppContext *app;
    SessionCmdType type;
    char *arg; // Used for UUID or Description
} SessionThreadData;

typedef struct {
    char *uuid;
    char *description;
    gboolean is_default;
} SessionEntry;

typedef struct {
    GList *entries; // A linked list of SessionEntry
    int count;
} SessionListResult;

typedef struct {
    char *uuid;
    char *description;
    gboolean is_default;
} SessionShowResult;

char* session_create_tee_chunk(AppContext *app, const char *raw_data);
char* session_create_history_chunk(AppContext *app, const char *raw_data) ;

void session_init(AppContext *app);
void session_log(AppContext *app, const char *data);
void session_sync_to_db(AppContext *app);
void cmd_session_list(AppContext *app, const char *args);
void cmd_session_new(AppContext *app, const char *args);
void cmd_session_load(AppContext *app, const char *uuid);
void cmd_session_delete(AppContext *app, const char *uuid);
void write_to_ai_pane_wrapper(AppContext *app, char *data);

gpointer session_db_worker(gpointer data);

#endif


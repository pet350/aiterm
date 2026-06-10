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

char* session_create_tee_chunk(AppContext *app, const char *raw_data);
char* session_create_history_chunk(AppContext *app, const char *raw_data) ;

void session_init(AppContext *app);
void session_log(AppContext *app, const char *data);
void session_sync_to_db(AppContext *app);

#endif

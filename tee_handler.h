#ifndef TEE_HANDLER_H
#define TEE_HANDLER_H

#include "gui.h"

// Standardized Prototypes for v0.7.3-beta
void tee_handler_init(AppContext *app);
char* tee_extract_for_ai(AppContext *app);
void tee_handle_input(AppContext *app, const char *text);
void tee_handle_output(AppContext *app, const char *text);
void tee_flush_timed(AppContext *app);

#endif



#ifndef TEE_HANDLER_H
#define TEE_HANDLER_H

#include "gui.h"

typedef struct {
    char *cmd_buffer;
    char *out_buffer;
    guint timer_id;
    AppContext *app;
    GMutex mutex; // Add this [cite: 214]
} TeeContext;

void tee_handler_init(AppContext *app);
void tee_handle_input(const char *text);
void tee_handle_output(const char *text);
void tee_flush_timed();

#endif

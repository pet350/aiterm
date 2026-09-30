// Part of project: aiterm
// tee_handler.h
// C Program header file for tee functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef TEE_HANDLER_H
#define TEE_HANDLER_H

#include "gui.h"

// Standardized Prototypes for v0.7.3-beta
void tee_handler_init(AppContext *app);
char* tee_extract_for_ai(AppContext *app);
char* tee_peek_for_ai(AppContext *app);
/* Return the newest persisted terminal TEE snapshot for AI context. */
char* tee_get_latest_for_ai(AppContext *app);
void tee_handle_input(AppContext *app, const char *text);
void tee_handle_output(AppContext *app, const char *text_in);
void tee_flush_timed(AppContext *app);
/* Persist pending TEE data without triggering an AI request. */
void tee_flush_pending_to_history(AppContext *app);
void pipe_snmp_to_gemini(AppContext *app, const char *raw_snmp_data);
void snmp_flush_to_gemini(AppContext *app);

#endif



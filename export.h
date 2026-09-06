// part of the aiterm project
// export.h
// Header file for exporting session context
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// September 2026

#ifndef EXPORT_H
#define EXPORT_H

#include "gui.h"

void export_session(AppContext *app);

char *get_terminal_text(AppContext *app);
char *get_ai_pane_text(AppContext *app);
char *escape_html(const char *text);
char *escape_xml(const char *text);
char *build_plain(const char *ai, const char *term, int include_both);
char *build_html(const char *ai, const char *term, int include_both);
char *build_json(const char *ai, const char *term, int include_both);
char *build_xml(const char *ai, const char *term, int include_both);

#endif

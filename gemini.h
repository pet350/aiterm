/* gemini.h
* Part of project: aiterm
* C Program header file for gemini  functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* April, May 2026
*/

#ifndef GEMINI_H
#define GEMINI_H

#include "gui.h"

typedef struct AppContext AppContext;

char* perform_gemini_request(AppContext *app, const char *prompt);
char* send_to_gemini(AppContext *app, const char *prompt);
gpointer ai_thread_func(gpointer data);


#endif



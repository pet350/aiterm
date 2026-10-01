// Part of project: aiterm
// openai.h
// C Program header file for updating AI functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef OPENAI_H
#define OPENAI_H

#include <curl/curl.h>
#include "utils.h"

// Function prototype so other files know it exists
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

char* send_to_openai(AppContext *app, const char *prompt, const char *terminal_context);
char* send_to_openai_ex(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history);
char* send_to_openai_ex_types(AppContext *app, const char *prompt, const char *terminal_context, gboolean include_history, TagType prompt_type, TagType context_type);

#define OPENAI_MODEL "gpt-5-nano"

#endif


// part of the aiterm project
// xml_tagging.h
// Header file for xml AI payloads
// By: Peter Talbott
// Assisted by: Gemini and OpenAI
// aiterm The terminal emulator with an AI Pane
// August 2026

#ifndef XML_TAGGING_H
#define XML_TAGGING_H

#include "gui.h"
#include <pthread.h>
#include <vte/vte.h>
#include <json-c/json.h>

// XML Tagging Function Prototypes
char* xml_wrap(AppContext *app, const char *input);
char* xml_wrap_with_type(AppContext *app, const char *input, TagType type);

#endif

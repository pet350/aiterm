/* help.h
* Part of project: aiterm
* C Program header file for help functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* April, May 2026
*/


#ifndef HELP_H
#define HELP_H

// Set a static buffer size
#define HELP_BUFFER_SIZE 8192

// Returns the static help menu string
char* get_hw_stats();
const char* get_help_text();
const char* get_version_info();
const char* get_cmd_help();
const char* get_features_text();

#endif



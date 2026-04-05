#ifndef OPENAI_H
#define OPENAI_H

#include <curl/curl.h>

// Shared memory structure for CURL responses
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Function prototype so other files know it exists
size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

char* send_to_openai(const char *api_key, const char *prompt);

#define OPENAI_MODEL "gpt-5-nano"

#endif
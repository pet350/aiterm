#ifndef GEMINI_H
#define GEMINI_H

/* * Returns a dynamically allocated string containing the JSON response.
 * Caller is responsible for freeing the memory.
 */
char* send_to_gemini(const char *api_key, const char *prompt);

#endif

/* crypto.h
* Part of project: aiterm
* C Program header file for crypto functions
* By: Peter Talbott
* With assistance from Gemini and OpenAI
* April, May 2026
*/


#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdlib.h>

// Returns a hex-encoded XOR string. Caller must free().
char* crypt_to_hex(const char *input);

// Returns a decrypted plaintext string from hex. Caller must free().
char* hex_to_decrypt(const char *hex);

#endif



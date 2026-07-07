// Part of project: aiterm
// crypto.h
// C Program header file for crypto functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// April, May 2026

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdlib.h>

void bin_to_hex(const unsigned char *bin, size_t len, char *hex);
void hex_to_bin(const char *hex, unsigned char *bin);
char* hex_to_decrypt(const char *hex_encrypted, const char *master_key);
char* crypt_to_hex(const char *plaintext, const char *master_key);

#endif


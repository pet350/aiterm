// crypto.c
// Part of the aiterm project
// C Program file for crypto functions
// By: Peter Talbott
// With assistance from Gemini and OpenAI
// May 2026

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"

// A static internal key for XOR operations
static const char *XOR_KEY = "aiterm-v0.7.3-secure-key";

static void xor_process(char *data, size_t len) {
    size_t key_len = strlen(XOR_KEY);
    for (size_t i = 0; i < len; i++) {
        data[i] ^= XOR_KEY[i % key_len];
    }
}

char* crypt_to_hex(const char *input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    char *temp = strdup(input);
    xor_process(temp, len);

    char *hex = malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + (i * 2), "%02x", (unsigned char)temp[i]);
    }
    hex[len * 2] = '\0';
    free(temp);
    return hex;
}

char* hex_to_decrypt(const char *hex) {
    if (!hex) return NULL;
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return NULL;

    size_t data_len = hex_len / 2;
    char *data = malloc(data_len + 1);
    for (size_t i = 0; i < data_len; i++) {
        unsigned int val;
        sscanf(hex + (i * 2), "%02x", &val);
        data[i] = (char)val;
    }
    data[data_len] = '\0';
    xor_process(data, data_len);
    return data;
}


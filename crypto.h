#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdlib.h>

// Returns a hex-encoded XOR string. Caller must free().
char* crypt_to_hex(const char *input);

// Returns a decrypted plaintext string from hex. Caller must free().
char* hex_to_decrypt(const char *hex);

#endif



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "crypto.h"

#define SALT_LEN 16
#define IV_LEN 16

// Helper: Convert Binary to Hex
void bin_to_hex(const unsigned char *bin, size_t len, char *hex) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + (i * 2), "%02x", bin[i]);
    }
}

// Helper: Convert Hex to Binary
void hex_to_bin(const char *hex, unsigned char *bin) {
    size_t len = strlen(hex);
    for (size_t i = 0; i < len / 2; i++) {
        sscanf(hex + (i * 2), "%02hhx", &bin[i]);
    }
}

// Key Derivation
int derive_key(const char *master_password, unsigned char *salt, unsigned char *key) {
    if (!master_password) return 0;
    return PKCS5_PBKDF2_HMAC(master_password, strlen(master_password), 
                             salt, SALT_LEN, 10000, EVP_sha256(), 32, key);
}

// The core Encryption function
int encrypt_data(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, unsigned char *ciphertext) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len;
    int ciphertext_len;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
    ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}

// Wrapper: Encrypt -> Hex
char* crypt_to_hex(const char *plaintext, const char *master_key) {
    unsigned char salt[SALT_LEN] = "aiterm-v0.7.3-sl"; // 16 bytes
    unsigned char iv[IV_LEN];
    RAND_bytes(iv, IV_LEN);

    unsigned char key[32];
    derive_key(master_key, salt, key);

    int pt_len = strlen(plaintext);
    unsigned char *ciphertext = malloc(pt_len + 32); // Extra space for padding
    
    int ct_len = encrypt_data((unsigned char*)plaintext, pt_len, key, iv, ciphertext);

    // Combine: Salt + IV + Ciphertext
    int total_len = SALT_LEN + IV_LEN + ct_len;
    unsigned char *blob = malloc(total_len);
    memcpy(blob, salt, SALT_LEN);
    memcpy(blob + SALT_LEN, iv, IV_LEN);
    memcpy(blob + SALT_LEN + IV_LEN, ciphertext, ct_len);

    char *hex_output = malloc(total_len * 2 + 1);
    bin_to_hex(blob, total_len, hex_output);

    free(ciphertext); free(blob);
    return hex_output;
}

// Wrapper: Hex -> Decrypt
char* hex_to_decrypt(const char *hex_encrypted, const char *master_key) {
    int total_len = strlen(hex_encrypted) / 2;
    unsigned char *blob = malloc(total_len);
    hex_to_bin(hex_encrypted, blob);

    unsigned char salt[SALT_LEN];
    unsigned char iv[IV_LEN];
    memcpy(salt, blob, SALT_LEN);
    memcpy(iv, blob + SALT_LEN, IV_LEN);

    unsigned char key[32];
    derive_key(master_key, salt, key);

    unsigned char *ciphertext = blob + SALT_LEN + IV_LEN;
    int ct_len = total_len - SALT_LEN - IV_LEN;
    unsigned char *plaintext = malloc(ct_len);

    // Decrypt logic
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len, pt_len;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ct_len);
    pt_len = len;
    EVP_DecryptFinal_ex(ctx, plaintext + len, &len);
    pt_len += len;
    EVP_CIPHER_CTX_free(ctx);

    plaintext[pt_len] = '\0'; // Null terminate
    free(blob);
    return (char*)plaintext;
}

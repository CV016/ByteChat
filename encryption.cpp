#include "encryption.hpp"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <iostream>

std::vector<unsigned char> Encryption::key(KEY_SIZE);
bool Encryption::initialized = false;

bool Encryption::initialize(const std::string& password) {
    return deriveKey(password);
}

bool Encryption::deriveKey(const std::string& password) {
    // Salt for key derivation
    unsigned char salt[8] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0xA7, 0xB8};
    
    // Derive key using PBKDF2
    if (PKCS5_PBKDF2_HMAC_SHA1(
            password.c_str(), password.length(),
            salt, sizeof(salt),
            10000, // iterations
            KEY_SIZE,
            key.data()) != 1) {
        return false;
    }
    
    initialized = true;
    return true;
}

std::string Encryption::encrypt(const std::string& plaintext) {
    if (!initialized) {
        std::cerr << "Encryption not initialized" << std::endl;
        return plaintext;
    }
    
    // Generate IV
    std::vector<unsigned char> iv(IV_SIZE);
    RAND_bytes(iv.data(), IV_SIZE);
    
    // Initialize context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data());
    
    // Encrypt
    int clen = plaintext.size() + AES_BLOCK_SIZE;
    std::vector<unsigned char> ciphertext(clen);
    int outlen;
    
    EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen, 
                      (const unsigned char*)plaintext.c_str(), plaintext.size());
    
    int tmplen;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen);
    outlen += tmplen;
    
    EVP_CIPHER_CTX_free(ctx);
    
    // Prepend IV to the ciphertext
    std::string result(iv.begin(), iv.end());
    result.append(ciphertext.begin(), ciphertext.begin() + outlen);
    
    return result;
}

std::string Encryption::decrypt(const std::string& ciphertext) {
    if (!initialized) {
        std::cerr << "Encryption not initialized" << std::endl;
        return ciphertext;
    }
    
    if (ciphertext.size() <= IV_SIZE) {
        return ciphertext;
    }
    
    // Extract IV
    std::vector<unsigned char> iv(ciphertext.begin(), ciphertext.begin() + IV_SIZE);
    
    // Initialize context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data());
    
    // Decrypt
    int plen = ciphertext.size() - IV_SIZE + AES_BLOCK_SIZE;
    std::vector<unsigned char> plaintext(plen);
    int outlen;
    
    EVP_DecryptUpdate(ctx, plaintext.data(), &outlen,
                     (const unsigned char*)ciphertext.c_str() + IV_SIZE, 
                     ciphertext.size() - IV_SIZE);
    
    int tmplen;
    EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &tmplen);
    outlen += tmplen;
    
    EVP_CIPHER_CTX_free(ctx);
    
    return std::string(plaintext.begin(), plaintext.begin() + outlen);
}
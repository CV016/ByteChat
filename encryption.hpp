#ifndef ENCRYPTION_HPP
#define ENCRYPTION_HPP

#include <string>
#include <vector>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

class Encryption {
public:
    // Initialize encryption with a password
    static bool initialize(const std::string& password);
    
    // Encrypt a string
    static std::string encrypt(const std::string& plaintext);
    
    // Decrypt a string
    static std::string decrypt(const std::string& ciphertext);
    
private:
    static const int KEY_SIZE = 32; // 256 bits
    static const int IV_SIZE = 16;  // 128 bits
    static std::vector<unsigned char> key;
    static bool initialized;
    
    // Derive key from password using PBKDF2
    static bool deriveKey(const std::string& password);
};

#endif // ENCRYPTION_HPP
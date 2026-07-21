// crypto_utils.h
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/rand.h>

class CryptoUtils {
public:
    static std::string base64Encode(const unsigned char* data, size_t len) {
        static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string ret;
        ret.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            ret += base64_chars[(data[i] >> 2) & 0x3F];
            ret += base64_chars[((data[i] & 0x3) << 4) | ((i + 1 < len ? data[i + 1] : 0) >> 4)];
            ret += (i + 1 < len) ? base64_chars[((data[i + 1] & 0xF) << 2) | ((i + 2 < len ? data[i + 2] : 0) >> 6)] : '=';
            ret += (i + 2 < len) ? base64_chars[data[i + 2] & 0x3F] : '=';
        }
        return ret;
    }

    static std::vector<unsigned char> base64Decode(const std::string& encoded) {
        static const int decode_table[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
        };
        std::vector<unsigned char> result;
        int val = 0, valb = -8;
        for (unsigned char c : encoded) {
            if (c == '=') break;
            int v = decode_table[c];
            if (v < 0) continue;
            val = (val << 6) + v;
            valb += 6;
            if (valb >= 0) {
                result.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }
        return result;
    }

    static std::string get32ByteKey(const std::string& input) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

        const EVP_MD* md = EVP_sha256();
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;

        if (1 != EVP_DigestInit_ex(ctx, md, nullptr)) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestInit_ex failed");
        }
        if (1 != EVP_DigestUpdate(ctx, input.c_str(), input.size())) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }
        if (1 != EVP_DigestFinal_ex(ctx, hash, &hashLen)) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestFinal_ex failed");
        }
        EVP_MD_CTX_free(ctx);

        return std::string(reinterpret_cast<char*>(hash), hashLen);
    }

    static std::string encryptAES256CBC(const std::string& plaintext, const std::string& key) {
        std::string safe_key = get32ByteKey(key);
        unsigned char iv[16];
        if (!RAND_bytes(iv, sizeof(iv))) throw std::runtime_error("RAND_bytes failed");

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

        int len, ciphertext_len;
        std::vector<unsigned char> ciphertext(plaintext.size() + 16 + 16);

        if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (const unsigned char*)safe_key.data(), iv)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptInit_ex failed");
        }
        if (1 != EVP_EncryptUpdate(ctx, ciphertext.data() + 16, &len, (const unsigned char*)plaintext.data(), plaintext.size())) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptUpdate failed");
        }
        ciphertext_len = len;
        if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + 16 + len, &len)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptFinal_ex failed");
        }
        ciphertext_len += len;
        EVP_CIPHER_CTX_free(ctx);

        std::memcpy(ciphertext.data(), iv, 16);
        return base64Encode(ciphertext.data(), 16 + ciphertext_len);
    }

    static std::string decryptAES256CBC(const std::string& ciphertextB64, const std::string& key) {
        std::string safe_key = get32ByteKey(key);
        auto data = base64Decode(ciphertextB64);
        if (data.size() < 16) throw std::runtime_error("Invalid ciphertext size");

        const unsigned char* iv = data.data();
        const unsigned char* ciphertext = data.data() + 16;
        int ciphertext_len = data.size() - 16;

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

        int len, plaintext_len;
        std::vector<unsigned char> plaintext(ciphertext_len + 16);

        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, (const unsigned char*)safe_key.data(), iv)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptInit_ex failed");
        }
        if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptUpdate failed");
        }
        plaintext_len = len;
        if (1 != EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptFinal_ex failed (wrong key or corrupted data)");
        }
        plaintext_len += len;
        EVP_CIPHER_CTX_free(ctx);

        return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
    }
};
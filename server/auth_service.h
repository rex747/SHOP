// auth_service.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <nlohmann/json.hpp>
#include "database.h"
#include "config_server.h"
#include "crypto_utils.h"
#include "logger_server.h"

extern Logger g_serverLogger;

using json = nlohmann::json;

class ReplayCache {
private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> cache_;
    std::chrono::seconds ttl_;

public:
    explicit ReplayCache(std::chrono::seconds ttl) : ttl_(ttl) {}

    bool isUsed(const std::string& phone, const std::string& code) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();
        auto it = cache_.find(phone);
        if (it != cache_.end()) {
            auto& codes = it->second;
            auto code_it = codes.find(code);
            if (code_it != codes.end()) {
                if (now - code_it->second < ttl_) {
                    return true;
                }
                else {
                    codes.erase(code_it);
                }
            }
        }
        return false;
    }

    void markUsed(const std::string& phone, const std::string& code) {
        std::lock_guard<std::mutex> lock(mtx_);
        cache_[phone][code] = std::chrono::steady_clock::now();
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            for (auto code_it = it->second.begin(); code_it != it->second.end(); ) {
                if (now - code_it->second >= ttl_) {
                    code_it = it->second.erase(code_it);
                }
                else {
                    ++code_it;
                }
            }
            if (it->second.empty()) {
                it = cache_.erase(it);
            }
            else {
                ++it;
            }
        }
    }
};

class AuthService {
private:
    std::shared_ptr<Database> db_;
    ReplayCache replay_cache_;

    std::string base64EncodeJWT(const unsigned char* data, size_t len) {
        return CryptoUtils::base64Encode(data, len);
    }

    std::vector<unsigned char> base32Decode(const std::string& encoded) {
        std::vector<unsigned char> result;
        if (encoded.empty()) return result;
        int buffer = 0;
        int bitsLeft = 0;
        for (char c : encoded) {
            if (c == '=') break;
            int val = 0;
            if (c >= 'A' && c <= 'Z') val = c - 'A';
            else if (c >= '2' && c <= '7') val = c - '2' + 26;
            else continue;
            buffer = (buffer << 5) | val;
            bitsLeft += 5;
            if (bitsLeft >= 8) {
                result.push_back((buffer >> (bitsLeft - 8)) & 0xFF);
                bitsLeft -= 8;
            }
        }
        return result;
    }

    std::string generateTOTPCodeForCounter(const std::vector<unsigned char>& key, int64_t counter) {
        unsigned char counterBytes[8]{};
        for (int i = 7; i >= 0; --i) {
            counterBytes[i] = counter & 0xFF;
            counter >>= 8;
        }
        unsigned char hmacResult[EVP_MAX_MD_SIZE];
        unsigned int hmacLen;
        HMAC(EVP_sha1(), key.data(), key.size(), counterBytes, 8, hmacResult, &hmacLen);

        int offset = hmacResult[hmacLen - 1] & 0x0F;
        int binary = ((hmacResult[offset] & 0x7F) << 24) |
            ((hmacResult[offset + 1] & 0xFF) << 16) |
            ((hmacResult[offset + 2] & 0xFF) << 8) |
            (hmacResult[offset + 3] & 0xFF);

        int otp = binary % 1000000;

        std::ostringstream oss;
        oss << std::setw(6) << std::setfill('0') << otp;
        return oss.str();
    }

    std::string generateJWT(const std::string& phone, int expirySeconds) {
        json header = { {"typ", "JWT"}, {"alg", "HS256"} };
        int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
        json payload = { {"phone", phone}, {"iat", now}, {"exp", now + expirySeconds} };

        std::string headerB64 = base64EncodeJWT((const unsigned char*)header.dump().c_str(), header.dump().size());
        std::string payloadB64 = base64EncodeJWT((const unsigned char*)payload.dump().c_str(), payload.dump().size());
        std::string message = headerB64 + "." + payloadB64;

        unsigned char hmacResult[EVP_MAX_MD_SIZE];
        unsigned int hmacLen;
        std::string jwt_secret = Config::JWT_SECRET;
        HMAC(EVP_sha256(), jwt_secret.data(), static_cast<int>(jwt_secret.size()),
            reinterpret_cast<const unsigned char*>(message.data()), message.size(), hmacResult, &hmacLen);

        std::string signature = base64EncodeJWT(hmacResult, hmacLen);
        return message + "." + signature;
    }

    std::string hashToken(const std::string& token) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;
        EVP_Digest(token.c_str(), token.size(), hash, &hashLen, EVP_sha256(), nullptr);
        std::stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

public:
    AuthService(std::shared_ptr<Database> db)
        : db_(db), replay_cache_(std::chrono::seconds(90)) {
    }

    bool initialize() {
        return db_->initialize();
    }

    std::string generateTOTPSecret() {
        unsigned char bytes[20];
        RAND_bytes(bytes, sizeof(bytes));
        static const char base32_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::string result;
        result.reserve(32);
        for (int i = 0; i < 20; i += 5) {
            result += base32_chars[(bytes[i] >> 3) & 0x1F];
            result += base32_chars[((bytes[i] & 0x07) << 2) | ((bytes[i + 1] >> 6) & 0x03)];
            result += base32_chars[(bytes[i + 1] >> 1) & 0x1F];
            result += base32_chars[((bytes[i + 1] & 0x01) << 4) | ((bytes[i + 2] >> 4) & 0x0F)];
            result += base32_chars[((bytes[i + 2] & 0x0F) << 1) | ((bytes[i + 3] >> 7) & 0x01)];
            result += base32_chars[(bytes[i + 3] >> 2) & 0x1F];
            result += base32_chars[((bytes[i + 3] & 0x03) << 3) | ((bytes[i + 4] >> 5) & 0x07)];
            result += base32_chars[bytes[i + 4] & 0x1F];
        }
        return result;
    }

    std::string generateTOTPURI(const std::string& secret, const std::string& phone) {
        return "otpauth://totp/SHOP:" + phone + "?secret=" + secret + "&issuer=SHOP&algorithm=SHA1&digits=6&period=30";
    }

    bool verifyTOTP(const std::string& phone, const std::string& code, int allowedDrift = 1) {
        if (code.length() != 6) return false;

        if (replay_cache_.isUsed(phone, code)) {
            return false; // Replay attack detected
        }

        auto secretOpt = db_->getTOTPSecret(phone);
        if (!secretOpt) return false;

        auto key = base32Decode(*secretOpt);
        if (key.empty()) return false;

        int64_t currentTime = std::chrono::system_clock::now().time_since_epoch().count() / 1000 / 1000 / 1000;
        int64_t counter = currentTime / 30;

        for (int i = -allowedDrift; i <= allowedDrift; ++i) {
            if (generateTOTPCodeForCounter(key, counter + i) == code) {
                replay_cache_.markUsed(phone, code);
                return true;
            }
        }
        return false;
    }

    std::pair<std::string, std::string> generateTokens(const std::string& phone) {
        std::string accessToken = generateJWT(phone, Config::JWT_ACCESS_EXPIRY_SECONDS);
        std::string refreshToken = generateJWT(phone, Config::JWT_REFRESH_EXPIRY_SECONDS);

        try {
            auto clientOpt = db_->getClientByPhone(phone);
            if (clientOpt) {
                int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

                // ИСПРАВЛЕНИЕ ОШИБКИ E0265: Использование публичного метода вместо прямого доступа к приватному conn_
                db_->saveAuthTokens(
                    clientOpt->id,
                    hashToken(accessToken),
                    hashToken(refreshToken),
                    now + Config::JWT_REFRESH_EXPIRY_SECONDS
                );
            }
        }
        catch (const std::exception& e) {
            std::cerr << "generateTokens DB error: " << e.what() << std::endl;
        }
        return { accessToken, refreshToken };
    }
};

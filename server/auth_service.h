// auth_service.h
#pragma once

#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <nlohmann/json.hpp>
#include "database.h"
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

using json = nlohmann::json;

class AuthService {
private:
    std::map<std::string, std::pair<std::string, int64_t>> m_smsCodes;
    std::mutex m_mutex;

    std::string base64Encode(const unsigned char* data, size_t len) {
        static const char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string ret;
        ret.reserve(((len + 2) / 3) * 4);

        for (size_t i = 0; i < len; i += 3) {
            ret += base64_chars[(data[i] >> 2) & 0x3F];
            ret += base64_chars[((data[i] & 0x3) << 4) |
                ((i + 1 < len ? data[i + 1] : 0) >> 4)];
            ret += (i + 1 < len) ? base64_chars[((data[i + 1] & 0xF) << 2) |
                ((i + 2 < len ? data[i + 2] : 0) >> 6)] : '=';
            ret += (i + 2 < len) ? base64_chars[data[i + 2] & 0x3F] : '=';
        }

        return ret;
    }

    std::string generateJWT(const std::string& phone, int expirySeconds) {
        json header = { {"typ", "JWT"}, {"alg", "HS256"} };

        int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
        json payload = {
            {"phone", phone},
            {"iat", now},
            {"exp", now + expirySeconds}
        };

        std::string headerB64 = base64Encode(
            (const unsigned char*)header.dump().c_str(),
            header.dump().size()
        );
        std::string payloadB64 = base64Encode(
            (const unsigned char*)payload.dump().c_str(),
            payload.dump().size()
        );

        std::string message = headerB64 + "." + payloadB64;

        unsigned char hmacResult[EVP_MAX_MD_SIZE];
        unsigned int hmacLen;

        HMAC(EVP_sha256(), Config::JWT_SECRET, strlen(Config::JWT_SECRET),
            (const unsigned char*)message.c_str(), message.size(),
            hmacResult, &hmacLen);

        std::string signature = base64Encode(hmacResult, hmacLen);

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
    bool initialize() {
        return Database::initialize();
    }

    // --- storeSMSCode ---
    void storeSMSCode(const std::string& phone, const std::string& code) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int64_t expiry = std::chrono::system_clock::now().time_since_epoch().count() / 1000 + 300;
        m_smsCodes[phone] = { code, expiry };
        try {
            pqxx::work txn{ *g_dbConnection };
            txn.exec(
                "INSERT INTO auth_sms_codes (phone, code) VALUES ($1, $2)",
                pqxx::params{ phone, code }
            );
            txn.commit();
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("storeSMSCode DB error: ") + e.what());
        }
    }

    bool verifySMSCode(const std::string& phone, const std::string& code) {
        // Check in-memory cache first
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_smsCodes.find(phone);
            if (it != m_smsCodes.end()) {
                int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
                if (it->second.first == code && it->second.second > now) {
                    m_smsCodes.erase(it);
                    return true;
                }
            }
        }

        // Check in database
        try {
            pqxx::work txn{ *g_dbConnection };
            auto result = txn.exec(
                "SELECT code FROM auth_sms_codes WHERE phone = $1 AND code = $2 "
                "AND used = FALSE AND created_at > EXTRACT(EPOCH FROM NOW()) - 300",
                pqxx::params{ phone, code }
            );
            if (!result.empty()) {
                txn.exec(
                    "UPDATE auth_sms_codes SET used = TRUE WHERE phone = $1 AND code = $2",
                    pqxx::params{ phone, code }
                );
                txn.commit();
                return true;
            }
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("verifySMSCode DB error: ") + e.what());
        }

        return false;
    }

    std::pair<std::string, std::string> generateTokens(const std::string& phone) {
        std::string accessToken = generateJWT(phone, Config::JWT_ACCESS_EXPIRY_SECONDS);
        std::string refreshToken = generateJWT(phone, Config::JWT_REFRESH_EXPIRY_SECONDS);

        // Store tokens in database
        try {
            auto clientOpt = Database::getClientByPhone(phone);
            if (clientOpt) {
                pqxx::work txn{ *g_dbConnection };
                int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
                txn.exec(
                    "INSERT INTO auth_tokens (client_id, access_token_hash, refresh_token_hash, expires_at) "
                    "VALUES ($1, $2, $3, $4)",
                    pqxx::params{ clientOpt->id, hashToken(accessToken), hashToken(refreshToken), now + Config::JWT_REFRESH_EXPIRY_SECONDS }
                );
                txn.commit();
            }
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("generateTokens DB error: ") + e.what());
        }

        return { accessToken, refreshToken };
    }

    bool validateToken(const std::string& token) {
        // Simplified validation - in production, verify signature and expiry
        return !token.empty();
    }
};

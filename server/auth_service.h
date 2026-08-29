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
        HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()), counterBytes, 8, hmacResult, &hmacLen);

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

    std::string generateJWT(const std::string& phone, const std::string& role, int expirySeconds) {
        json header = { {"typ", "JWT"}, {"alg", "HS256"} };
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        // ДОБАВЛЕНО поле "role" в payload JWT. Это позволяет серверным
        // эндпоинтам и клиенту определять роль из токена без запроса к БД.
        json payload = { {"phone", phone}, {"role", role}, {"iat", now}, {"exp", now + expirySeconds} };
        std::string headerB64 = base64UrlEncode(reinterpret_cast<const unsigned char*>(header.dump().c_str()), header.dump().size());
        std::string payloadB64 = base64UrlEncode(reinterpret_cast<const unsigned char*>(payload.dump().c_str()), payload.dump().size());
        std::string message = headerB64 + "." + payloadB64;
        unsigned char hmacResult[EVP_MAX_MD_SIZE];
        unsigned int hmacLen;
        std::string jwt_secret = Config::JWT_SECRET;
        HMAC(EVP_sha256(), jwt_secret.data(), static_cast<int>(jwt_secret.size()),
            reinterpret_cast<const unsigned char*>(message.data()), message.size(), hmacResult, &hmacLen);
        std::string signature = base64UrlEncode(hmacResult, hmacLen);
        return message + "." + signature;
    }

    std::string hashToken(const std::string& token) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;
        EVP_Digest(token.c_str(), static_cast<int>(token.size()), hash, &hashLen, EVP_sha256(), nullptr);
        std::stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
    

    // Вспомогательная функция: удаляет нулевые байты из строки
    static std::string sanitizeString(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        for (char c : input) {
            if (c != '\0') {
                result.push_back(c);
            }
        }
        return result;
    }

    // Вспомогательные функции для JWT
    std::string base64UrlEncode(const unsigned char* data, size_t len) {
        std::string encoded = CryptoUtils::base64Encode(data, len);
        for (char& c : encoded) {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }
        while (!encoded.empty() && encoded.back() == '=') {
            encoded.pop_back();
        }
        return encoded;
    }

    std::vector<unsigned char> base64UrlDecode(const std::string& encoded) {
        std::string base64 = encoded;
        for (char& c : base64) {
            if (c == '-') c = '+';
            else if (c == '_') c = '/';
        }
        while (base64.size() % 4) {
            base64.push_back('=');
        }
        return CryptoUtils::base64Decode(base64);
    }

public:
    explicit AuthService(std::shared_ptr<Database> db)
        : db_(std::move(db)), replay_cache_(std::chrono::seconds(90)) {
    }

    bool initialize() {
        return db_->initialize();
    }

    std::string hashString(const std::string& str) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;
        EVP_Digest(str.c_str(), static_cast<int>(str.size()), hash, &hashLen, EVP_sha256(), nullptr);
        std::stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
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

    // =========================================================================
    // НОВЫЙ МЕТОД: ПРОВЕРКА ПАРОЛЯ ТОВАРОВЕДА/ДИРЕКТОРА.
    //
    // СХЕМА ХРАНЕНИЯ (не ломает БД, использует существующее поле
    // totp_secret_encrypted и существующие методы setTOTPSecret/getTOTPSecret):
    //   1. При регистрации: пароль хешируется (SHA-256 через hashString),
    //      затем ХЕШ шифруется через CryptoUtils::encryptAES256CBC и
    //      сохраняется в totp_secret_encrypted методом db_->setTOTPSecret.
    //   2. При входе: db_->getTOTPSecret УЖЕ расшифровывает значение
    //      (возвращает хеш). Мы хешируем введённый пароль и сравниваем.
    //
    // Потокобезопасность: метод выполняет только чтение через существующий
    // потокобезопасный Database::getTOTPSecret. Состояние класса не меняет.
    // =========================================================================
    bool verifyPassword(const std::string& phone, const std::string& password) {
        auto storedHashOpt = db_->getTOTPSecret(phone);
        if (!storedHashOpt) {
            g_serverLogger.warning("verifyPassword: no stored password hash for phone=" + phone);
            return false;
        }
        std::string inputHash = hashString(password);
        bool match = (inputHash == *storedHashOpt);
        g_serverLogger.info("verifyPassword: phone=" + phone + ", match=" + (match ? "true" : "false"));
        return match;
    }

    std::pair<std::string, std::string> generateTokens(const std::string& phone) {
        // Определяем роль клиента для встраивания в JWT.
        // Если клиент не найден (не должно происходить при штатном потоке),
        // используем роль по умолчанию "client".
        std::string role = "client";
        std::optional<Client> clientOpt;
        try {
            clientOpt = db_->getClientByPhone(phone);
            if (clientOpt) {
                role = clientOpt->role;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "generateTokens getClientByPhone error: " << e.what() << std::endl;
        }

        // Генерируем токены С РОЛЬЮ в payload (новое).
        std::string accessToken = generateJWT(phone, role, Config::JWT_ACCESS_EXPIRY_SECONDS);
        std::string refreshToken = generateJWT(phone, role, Config::JWT_REFRESH_EXPIRY_SECONDS);

        // Сохранение хешей токенов в БД — ЛОГИКА НЕ МЕНЯЕТСЯ.
        if (clientOpt) {
            try {
                int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
                db_->saveAuthTokens(
                    clientOpt->id,
                    hashToken(accessToken),
                    hashToken(refreshToken),
                    now + Config::JWT_REFRESH_EXPIRY_SECONDS
                );
            }
            catch (const std::exception& e) {
                std::cerr << "generateTokens DB error: " << e.what() << std::endl;
            }
        }
        return { accessToken, refreshToken };
    }

    std::optional<std::string> verifyJWT(const std::string& token) {
        // Разбиваем на части
        size_t first_dot = token.find('.');
        if (first_dot == std::string::npos) return std::nullopt;
        size_t second_dot = token.find('.', first_dot + 1);
        if (second_dot == std::string::npos) return std::nullopt;
        std::string headerB64 = token.substr(0, first_dot);
        std::string payloadB64 = token.substr(first_dot + 1, second_dot - first_dot - 1);
        std::string signatureB64 = token.substr(second_dot + 1);

        // Декодируем payload
        auto payloadData = base64UrlDecode(payloadB64);
        if (payloadData.empty()) return std::nullopt;
        std::string payloadStr(payloadData.begin(), payloadData.end());
        json payload;
        try { payload = json::parse(payloadStr); }
        catch (...) { return std::nullopt; }

        // Проверяем exp
        int64_t exp = payload.value("exp", 0);
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (exp < now) return std::nullopt;

        // Проверяем подпись
        std::string message = headerB64 + "." + payloadB64;
        std::string jwt_secret = Config::JWT_SECRET;
        unsigned char hmacResult[EVP_MAX_MD_SIZE];
        unsigned int hmacLen;
        HMAC(EVP_sha256(), jwt_secret.data(), static_cast<int>(jwt_secret.size()),
            reinterpret_cast<const unsigned char*>(message.data()), message.size(),
            hmacResult, &hmacLen);
        std::string expectedSig = base64UrlEncode(hmacResult, hmacLen);
        if (expectedSig != signatureB64) return std::nullopt;

        // Извлекаем phone
        if (!payload.contains("phone") || !payload["phone"].is_string()) return std::nullopt;
        return payload["phone"].get<std::string>();
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ГЕНЕРАЦИЯ СЛУЧАЙНОГО ПАРОЛЯ ДЛЯ ТОВАРОВЕДА/ДИРЕКТОРА.
    // Использует криптографически стойкий RAND_bytes (OpenSSL), который
    // уже применяется в generateTOTPSecret. Потокобезопасен: RAND_bytes
    // является потокобезопасным в OpenSSL 1.1+.
    //
    // Формат: 10 символов из алфавита без неоднозначных символов
    // (исключены 0, O, 1, l, I для удобства ручного ввода).
    // =========================================================================
    std::string generateRandomPassword(int length = 10) {
        static const char charset[] =
            "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
        std::vector<unsigned char> bytes(length);
        if (RAND_bytes(bytes.data(), length) != 1) {
            // Fallback не требуется: при ошибке RAND_bytes бросаем исключение,
            // вызывающий код обработает его как ошибку регистрации.
            throw std::runtime_error("RAND_bytes failed during password generation");
        }
        std::string password;
        password.reserve(length);
        for (int i = 0; i < length; i++) {
            password += charset[bytes[i] % (sizeof(charset) - 1)];
        }
        return password;
    }
};
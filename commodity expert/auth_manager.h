// auth_manager.h
#pragma once

#include <windows.h>
#include <string>
#include <optional>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "https_client.h"
#include "config.h"
#include "string_utils.h"
#include "logger.h"

extern HTTPSClient g_httpsClient;
extern Logger g_logger;

// -----------------------------------------------------------------------------
// AuthToken Ч полное определение ƒќ использовани€ в AuthManager
// -----------------------------------------------------------------------------
struct AuthToken
{
    std::string accessToken;
    std::string refreshToken;
    std::int64_t expiresAt = 0;  // Unix timestamp в секундах

    AuthToken() = default;
    AuthToken(const std::string& access, const std::string& refresh, std::int64_t exp)
        : accessToken(access), refreshToken(refresh), expiresAt(exp) {
    }

    bool isValid() const
    {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now < expiresAt;
    }
};

// -----------------------------------------------------------------------------
// AuthManager
// -----------------------------------------------------------------------------
class AuthManager
{
private:
    // структура авторизации
    std::optional<AuthToken> m_token;
    std::string m_phone;  // храним в UTF-8
    std::string m_role;   // роль пользовател€ в формате "client"/"worker"/"director"
    bool m_isLoggedIn = false;
    int m_clientId = 0;
    std::wstring m_fullName;

    // отслеживание последней попытки входа ----
    bool m_loginAttempted = false;
    std::string m_lastAttemptPhone;

    bool refreshAccessToken()
    {
        if (!m_token || m_token->refreshToken.empty())
            return false;
        g_logger.error(L"Token is't refreshed or empty");

        nlohmann::json request;
        request["refresh_token"] = m_token->refreshToken;

        auto response = g_httpsClient.post(L"/api/v1/auth/refresh", request);
        if (!response)
            return false;

        if (response->contains("access_token") && (*response)["access_token"].is_string())
        {
            m_token->accessToken = (*response)["access_token"].get<std::string>();

            if (response->contains("refresh_token") && (*response)["refresh_token"].is_string())
                m_token->refreshToken = (*response)["refresh_token"].get<std::string>();

            auto now = std::chrono::system_clock::now();
            m_token->expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count() + 3600;
            return true;
        }

        return false;
    }

public:
    struct TOTPSetupResult {
        bool success;
        std::string secret;
        std::string uri;
    };

    TOTPSetupResult setupTOTP(const std::wstring& phone) {
        nlohmann::json request;
        request["phone"] = wstring_to_utf8(phone);
        auto response = g_httpsClient.post(L"/api/v1/auth/totp/setup", request);

        TOTPSetupResult result{ false, "", "" };
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            result.success = true;
            if (response->contains("secret")) result.secret = (*response)["secret"].get<std::string>();
            if (response->contains("uri")) result.uri = (*response)["uri"].get<std::string>();
            m_phone = wstring_to_utf8(phone);
        }
        return result;
    }

    bool verifyTOTP(const std::wstring& phone, const std::wstring& code) {
        nlohmann::json request;
        request["phone"] = wstring_to_utf8(phone);
        request["code"] = wstring_to_utf8(code);

        auto response = g_httpsClient.post(L"/api/v1/auth/totp/verify", request);
        if (!response) return false;

        if (response->contains("success") && (*response)["success"].get<bool>() &&
            response->contains("access_token") && (*response)["access_token"].is_string() &&
            response->contains("refresh_token") && (*response)["refresh_token"].is_string()) {

            auto now = std::chrono::system_clock::now();
            std::int64_t exp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count() + 3600;

            m_token.emplace(
                (*response)["access_token"].get<std::string>(),
                (*response)["refresh_token"].get<std::string>(),
                exp
            );
            m_phone = wstring_to_utf8(phone);
            // ”станавливаем флаг входа, но clientId и fullName будут установлены позже
            m_isLoggedIn = true;
            return true;
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // ћ≈“ќƒ дл€ установки токенов извне (без нарушени€ инкапсул€ции)
    // -------------------------------------------------------------------------
    void setAuthTokens(const std::string& accessToken,
        const std::string& refreshToken,
        std::int64_t expiresAt,
        const std::string& phone) {
        m_token.emplace(accessToken, refreshToken, expiresAt);
        m_phone = phone;
        m_isLoggedIn = true;   // токен получен Ч считаем пользовател€ вошедшим
        g_logger.info(L"AuthManager::setAuthTokens: tokens set for phone " + utf8_to_wstring(phone));
    }

    std::wstring getAuthToken() {
        if (!m_token || !m_token->isValid()) {
            if (!refreshAccessToken()) {
                m_token.reset();
                g_logger.info(L"[settAuthTokens:getAuthTokens]: не удалось обновить токен");
                return L"";
            }
        }
        return utf8_to_wstring(m_token->accessToken);
    }

    bool isAuthenticated() const {
        return m_token.has_value() && m_token->isValid();
    }

    void logout() {
        m_token.reset();
        m_phone.clear();
		m_role.clear(); 
        m_isLoggedIn = false;
        m_clientId = 0;
        m_fullName.clear();
        m_loginAttempted = false;
        m_lastAttemptPhone.clear();
    }

    std::wstring getPhone() const {
        return utf8_to_wstring(m_phone);
    }

    void setLoggedIn(bool logged, int clientId = 0, const std::wstring& fullName = L"", const std::wstring& phone = L"") {
        m_isLoggedIn = logged;
        m_clientId = clientId;
        m_fullName = fullName;
        if (!phone.empty()) {
            m_phone = wstring_to_utf8(phone);
        }
        g_logger.info(L"Login state set: logged=" + std::to_wstring(logged) + L", clientId=" + std::to_wstring(clientId) + L", name=" + fullName);
    }

    bool isLoggedIn() const { return m_isLoggedIn; }
    int getClientId() const { return m_clientId; }
    std::wstring getFullName() const { return m_fullName; }

    // ---- доступ к роли пользовател€ (дл€ main.cpp и director_window.h) ----
    void setRole(const std::string& role) {
        m_role = role;
        g_logger.info(L"AuthManager::setRole: role=" + utf8_to_wstring(role));
    }
    std::wstring getRole() const {
        return utf8_to_wstring(m_role);
    }

    // ---- ћ≈“ќƒџ ƒЋя ќ“—Ћ≈∆»¬јЌ»я ѕќѕџ“ » ¬’ќƒј ----
    void setLoginAttempted(bool attempted, const std::string& phone = "") {
        m_loginAttempted = attempted;
        if (!phone.empty()) m_lastAttemptPhone = phone;
        else m_lastAttemptPhone.clear();
        g_logger.info(L"AuthManager::setLoginAttempted: " + std::wstring(attempted ? L"true" : L"false") +
            L", phone=" + utf8_to_wstring(m_lastAttemptPhone));
    }

    bool wasLoginAttempted() const { return m_loginAttempted; }
    std::string getLastAttemptPhone() const { return m_lastAttemptPhone; }
    // ---------------------------------------------------------

    // ƒл€ отладки (не используетс€ в продакшене)
    friend class RegistrationWindow; // разрешаем доступ только дл€ тестировани€,
    // но лучше использовать публичный метод
};

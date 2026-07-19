// auth_manager.h
#pragma once

#include <windows.h>
#include <string>
#include <optional>
#include <chrono>
#include <cstdint>
#include "string_utils.h"

#include <nlohmann/json.hpp>

#include "https_client.h"
#include "config.h"

extern HTTPSClient g_httpsClient;
extern Logger g_logger;



// -----------------------------------------------------------------------------
// AuthToken — полное определение ДО использования в AuthManager
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
    std::optional<AuthToken> m_token;
    std::string m_phone;  // храним в UTF-8

    bool refreshAccessToken()
    {
        if (!m_token || m_token->refreshToken.empty())
            return false;

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
    bool authenticate(const std::wstring& phone, const std::wstring& smsCode)
    {
        nlohmann::json request;
        request["phone"] = wstring_to_utf8(phone);
        request["sms_code"] = wstring_to_utf8(smsCode);

        auto response = g_httpsClient.post(L"/api/v1/auth/verify", request);
        if (!response)
        {
            g_logger.warning(L"Authentication failed for phone: " + phone);
            return false;
        }

        if (response->contains("access_token") && (*response)["access_token"].is_string() &&
            response->contains("refresh_token") && (*response)["refresh_token"].is_string())
        {
            auto now = std::chrono::system_clock::now();
            std::int64_t exp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count() + 3600;

            m_token.emplace(
                (*response)["access_token"].get<std::string>(),
                (*response)["refresh_token"].get<std::string>(),
                exp
            );

            m_phone = wstring_to_utf8(phone);
            g_logger.info(L"Authentication successful for phone: " + phone);
            return true;
        }

        g_logger.warning(L"Authentication failed for phone: " + phone);
        return false;
    }

    bool sendSMSCode(const std::wstring& phone)
    {
        nlohmann::json request;
        request["phone"] = wstring_to_utf8(phone);

        auto response = g_httpsClient.post(L"/api/v1/auth/sms", request);
        if (response && response->contains("success") && (*response)["success"].get<bool>())
        {
            g_logger.info(L"SMS code sent to: " + phone);
            return true;
        }

        g_logger.error(L"Failed to send SMS code to: " + phone);
        return false;
    }

    std::wstring getAuthToken()
    {
        if (!m_token || !m_token->isValid())
        {
            if (!refreshAccessToken())
            {
                m_token.reset();
                return L"";
            }
        }
        return utf8_to_wstring(m_token->accessToken);
    }

    bool isAuthenticated() const
    {
        return m_token.has_value() && m_token->isValid();
    }

    void logout()
    {
        m_token.reset();
        m_phone.clear();
    }

    std::wstring getPhone() const
    {
        return utf8_to_wstring(m_phone);
    }

    // Вызывается после успешной регистрации без SMS-верификации.
    // Помечает клиента как зарегистрированного и сохраняет телефон.
    void markAsRegistered(const std::wstring& phone, const std::wstring& /*fullName*/)
    {
        m_phone = wstring_to_utf8(phone);
        // Создаём "пустой" токен-заглушку, чтобы isAuthenticated() возвращал true.
        // Настоящий токен будет получен при первом же запросе к серверу.
        m_token.emplace("", "",
            std::chrono::system_clock::now().time_since_epoch().count() / 1000 + 3600);
        g_logger.info(L"Client marked as registered (no SMS): " + phone);
    }
};
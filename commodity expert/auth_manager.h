// auth_manager.h
#pragma once
#include <windows.h>
#include <string>
#include <optional>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>

#include "https_client.h"
#include "config.h"
#include "string_utils.h"
#include "logger.h"

extern HTTPSClient g_httpsClient;
extern Logger g_logger;

struct AuthToken {
    std::string accessToken;
    std::string refreshToken;
    std::int64_t expiresAt = 0;

    AuthToken() = default;
    AuthToken(const std::string& access, const std::string& refresh, std::int64_t exp)
        : accessToken(access), refreshToken(refresh), expiresAt(exp) {}

    [[nodiscard]] bool isValid() const {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now < (expiresAt - 60); // 60 секунд запаса для предотвращения гонки на границе срока
    }
};

class AuthManager {
private:
    std::optional<AuthToken> m_token;
    mutable std::mutex m_tokenMutex;
    // МУТЕКС: Гарантирует, что только один поток может выполнять refresh одновременно
    mutable std::mutex m_refreshMutex;
    mutable std::mutex m_stateMutex;
    std::string m_phone;
    std::string m_role;
    bool m_isLoggedIn = false;
    int m_clientId = 0;
    std::wstring m_fullName;
    bool m_loginAttempted = false;
    std::string m_lastAttemptPhone;

    bool refreshAccessToken()
    {
        // Сериализация: только один поток может выполнять refresh
        std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

        // Double-checked locking: возможно, пока мы ждали m_refreshMutex,
        // другой поток уже успешно обновил токен
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            if (m_token && m_token->isValid())
                return true;
            if (!m_token || m_token->refreshToken.empty())
                return false;
        }

        // 1. Быстро читаем текущий refresh_token под локом
        std::string refreshTok;
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            if (!m_token || m_token->refreshToken.empty())
                return false;
            refreshTok = m_token->refreshToken;
        } // лок отпущен ПЕРЕД сетью

        g_logger.info(L"[refreshAccessToken] Starting token refresh...");

        nlohmann::json request;
        request["refresh_token"] = refreshTok;

        auto response = g_httpsClient.post(L"/api/v1/auth/refresh", request);
        if (!response) {
            g_logger.error(L"[refreshAccessToken] Network error during refresh");
            return false;
        }

        // 2. Проверяем реальный результат сервера
        if (!response->contains("access_token") || !(*response)["access_token"].is_string()) {
            g_logger.error(L"[refreshAccessToken] Server did not return access_token (probably 401 / revoked)");
            // Не трогаем m_token — пусть следующий вызов getAuthToken() увидит просроченный
            return false;
        }

        // 3. Атомарно обновляем токен
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            // Защита от гонки: если за время сети кто-то уже обновил токен — не затираем
            if (!m_token || m_token->refreshToken != refreshTok) {
                g_logger.warning(L"[refreshAccessToken] Token was already refreshed by another thread, skip");
                return m_token && m_token->isValid();
            }
            m_token->accessToken = (*response)["access_token"].get<std::string>();
            if (response->contains("refresh_token") && (*response)["refresh_token"].is_string())
                m_token->refreshToken = (*response)["refresh_token"].get<std::string>();
            auto now = std::chrono::system_clock::now();
            m_token->expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count() + 3600;
        }

        g_logger.info(L"[refreshAccessToken] Token refreshed successfully");
        return true;
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
            std::lock_guard<std::mutex> lock(m_stateMutex);
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
            {
                std::lock_guard<std::mutex> lock(m_tokenMutex);
                m_token.emplace(
                    (*response)["access_token"].get<std::string>(),
                    (*response)["refresh_token"].get<std::string>(),
                    exp
                );
            }
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_phone = wstring_to_utf8(phone);
                m_isLoggedIn = true;
            }
            return true;
        }
        return false;
    }

    void setAuthTokens(const std::string& accessToken, const std::string& refreshToken,
        std::int64_t expiresAt, const std::string& phone) {
            {
                std::lock_guard<std::mutex> lock(m_tokenMutex);
                m_token.emplace(accessToken, refreshToken, expiresAt);
            }
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_phone = phone;
                m_isLoggedIn = true;
            }
            g_logger.info(L"AuthManager::setAuthTokens: tokens set for phone " + utf8_to_wstring(phone));
    }

    std::wstring getAuthToken()
    {
        // 1. Быстрая проверка под локом
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            if (m_token && m_token->isValid())
                return utf8_to_wstring(m_token->accessToken);
        }

        // ================================================================
        // ИСПРАВЛЕНИЕ: первая попытка обновления.
        // Если она не удалась из-за временной сетевой ошибки
        // (WinHTTP таймаут, исчерпание пула соединений при 5–7
        // одновременных запросах от refreshList + pollLoop +
        // showItemInputForm + onItemSave + printReceipt),
        // выполняется ВТОРАЯ попытка.
        //
        // Ранее: при первой неудаче сразу возвращалась пустая строка.
        // Теперь: одна повторная попытка с паузой 500 мс.
        //
        // Новые переменные и методы НЕ добавлены.
        // Используется только существующий refreshAccessToken().
        // ================================================================
        if (!refreshAccessToken()) {
            // Пауза 500 мс перед повторной попыткой.
            // Это даёт время освободить соединения WinHTTP,
            // занятые параллельными потоками (refreshList, pollLoop).
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Повторная попытка обновления токена
            if (!refreshAccessToken()) {
                std::lock_guard<std::mutex> lock(m_tokenMutex);
                if (!m_token || !m_token->isValid()) {
                    // m_token НЕ сбрасываем (исправление 1 сохранено).
                    // Если токен существует, но невалиден, и обе попытки
                    // обновления не удались, возвращаем пустую строку.
                    // Пользователь увидит "Ошибка авторизации" и сможет
                    // войти заново.
                    g_logger.error(L"[getAuthToken]: обе попытки обновления токена "
                        L"не удались, возврат пустой строки");
                    return L"";
                }
                // Другой поток успел обновить токен между попытками
                return utf8_to_wstring(m_token->accessToken);
            }
        }

        // 3. Читаем уже обновлённый токен
        std::lock_guard<std::mutex> lock(m_tokenMutex);
        if (m_token && m_token->isValid())
            return utf8_to_wstring(m_token->accessToken);
        return L"";
    }

    [[nodiscard]] bool isAuthenticated() const {
        std::lock_guard<std::mutex> lock(m_tokenMutex);
        return m_token.has_value() && m_token->isValid();
    }

    void logout() {
        {
            std::lock_guard<std::mutex> lock(m_tokenMutex);
            m_token.reset();
        }
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_phone.clear();
            m_role.clear();
            m_isLoggedIn = false;
            m_clientId = 0;
            m_fullName.clear();
        }
        m_loginAttempted = false;
        m_lastAttemptPhone.clear();
    }

    [[nodiscard]] std::wstring getPhone() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return utf8_to_wstring(m_phone);
    }

    void setLoggedIn(bool logged, int clientId = 0, const std::wstring& fullName = L"", const std::wstring& phone = L"") {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_isLoggedIn = logged;
        m_clientId = clientId;
        m_fullName = fullName;
        if (!phone.empty()) {
            m_phone = wstring_to_utf8(phone);
        }
        g_logger.info(L"Login state set: logged=" + std::to_wstring(logged) +
            L", clientId=" + std::to_wstring(clientId) + L", name=" + fullName);
    }

    [[nodiscard]] bool isLoggedIn() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_isLoggedIn;
    }

    [[nodiscard]] int getClientId() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_clientId;
    }

    [[nodiscard]] std::wstring getFullName() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_fullName;
    }

    void setRole(const std::string& role) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_role = role;
        g_logger.info(L"AuthManager::setRole: role=" + utf8_to_wstring(role));
    }

    [[nodiscard]] std::wstring getRole() const {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return utf8_to_wstring(m_role);
    }

    void setLoginAttempted(bool attempted, const std::string& phone = "") {
        m_loginAttempted = attempted;
        if (!phone.empty()) m_lastAttemptPhone = phone;
        else m_lastAttemptPhone.clear();
        g_logger.info(L"AuthManager::setLoginAttempted: " + std::wstring(attempted ? L"true" : L"false") +
            L", phone=" + utf8_to_wstring(m_lastAttemptPhone));
    }

    [[nodiscard]] bool wasLoginAttempted() const { return m_loginAttempted; }
    [[nodiscard]] std::string getLastAttemptPhone() const { return m_lastAttemptPhone; }

    friend class RegistrationWindow;
};
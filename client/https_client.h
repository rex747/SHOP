// https_client.h
#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include "logger.h"
#include "config.h"

extern Logger g_logger;

using json = nlohmann::json;

class HTTPSClient {
private:
    HINTERNET m_hSession = nullptr;
    std::wstring m_serverName;
    INTERNET_PORT m_port;

    std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    std::wstring utf8_to_wstring(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

public:
    HTTPSClient(const std::wstring& serverName, INTERNET_PORT port)
        : m_serverName(serverName), m_port(port) {
        m_hSession = WinHttpOpen(L"KioskClient/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        g_logger.info(L"HTTPSClient initialized for server: " + serverName + L":" + std::to_wstring(port));
        if (!m_hSession) {
            g_logger.error(L"WinHttpOpen failed: " + std::to_wstring(GetLastError()));
        }
    }

    ~HTTPSClient() {
        if (m_hSession) {
            WinHttpCloseHandle(m_hSession);
            g_logger.info(L"HTTPSClient session closed");
        }
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД initialize()
    // =========================================================================
    // ПРИЧИНА ИСПРАВЛЕНИЯ:
    // 1. WINHTTP_OPTION_SECURE_PROTOCOLS должен устанавливаться ТОЛЬКО на
    //    session handle (m_hSession), а НЕ на request handle (hRequest).
    //    Установка на hRequest вызывает ошибку 12018
    //    (ERROR_WINHTTP_INCORRECT_HANDLE_TYPE) и TLS 1.2 не включается
    //    принудительно, что приводит к нестабильному TLS-рукопожатию.
    //
    // 2. WinHttpSetTimeouts вызывается ОДИН РАЗ при инициализации сессии.
    //    Ранее setTimeouts() вызывался при КАЖДОМ запросе из РАЗНЫХ потоков
    //    (фоновый поток обновления очереди, асинхронная загрузка товаров,
    //    асинхронное сохранение товаров), что создавало гонку за m_hSession
    //    и приводило к обрыву TLS-рукопожатия (stream truncated) и
    //    таймаутам (12002).
    // =========================================================================
    bool initialize() {
        if (!m_hSession) {
            g_logger.error(L"HTTPSClient::initialize() failed: session is null.");
            return false;
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ 1: Принудительное включение TLS 1.2 на уровне СЕССИИ.
        // WINHTTP_OPTION_SECURE_PROTOCOLS устанавливается на m_hSession,
        // а НЕ на hRequest. Это гарантирует, что все запросы через данную
        // сессию будут использовать TLS 1.2.
        // =====================================================================
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            DWORD err = GetLastError();
            g_logger.error(L"HTTPSClient::initialize() - WinHttpSetOption(SECURE_PROTOCOLS) on SESSION failed: " +
                std::to_wstring(err) + L". TLS 1.2 enforcement FAILED.");
            return false;
        }
        g_logger.info(L"HTTPSClient::initialize() - TLS 1.2 enforced on session handle successfully.");

        // =====================================================================
        // ИСПРАВЛЕНИЕ 2: Установка таймаутов ОДИН РАЗ при инициализации.
        // Ранее setTimeouts() вызывался при каждом запросе из разных потоков,
        // что создавало гонку за m_hSession. Теперь таймауты устанавливаются
        // один раз, и конкурентный доступ к m_hSession для изменения
        // параметров исключён.
        // =====================================================================
        if (!setTimeouts()) {
            g_logger.error(L"HTTPSClient::initialize() - setTimeouts() failed.");
            return false;
        }
        g_logger.info(L"HTTPSClient::initialize() - Timeouts configured: resolve=5000, connect=10000, send=10000, receive=10000 ms.");

        g_logger.info(L"HTTPSClient::initialize() succeeded.");
        return true;
    }

    // =========================================================================
    // Метод установки таймаутов.
    // Вызывается ТОЛЬКО из initialize() для исключения гонки потоков.
    // =========================================================================
    bool setTimeouts(DWORD resolveTimeout = 5000, DWORD connectTimeout = 10000,
        DWORD sendTimeout = 10000, DWORD receiveTimeout = 10000) {
        if (!m_hSession) {
            g_logger.error(L"setTimeouts: session handle is null");
            return false;
        }
        if (!WinHttpSetTimeouts(m_hSession, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSetTimeouts failed: " + std::to_wstring(err));
            return false;
        }
        return true;
    }

    // =========================================================================
    // POST-запрос
    // =========================================================================
    std::optional<json> post(const std::wstring& path, const json& requestBody,
        const std::wstring& authToken = L"") {
        if (!m_hSession) {
            g_logger.error(L"HTTP Session is not initialized");
            return std::nullopt;
        }

        std::string utf8Body = requestBody.dump();
        g_logger.info(L"Preparing POST request to: " + path);
        g_logger.info(L"Request body size: " + std::to_wstring(utf8Body.length()) + L" bytes");

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authToken.empty()) {
            headers += L"Authorization: Bearer " + authToken + L"\r\n";
            g_logger.info(L"Added Authorization header (Bearer token).");
        }

        g_logger.info(L"Connecting to: https://" + m_serverName + L":" + std::to_wstring(m_port) + path);

        HINTERNET hConnect = WinHttpConnect(m_hSession, m_serverName.c_str(), m_port, 0);
        if (!hConnect) {
            g_logger.error(L"WinHttpConnect failed: " + std::to_wstring(GetLastError()));
            return std::nullopt;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            g_logger.error(L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Убрана установка WINHTTP_OPTION_SECURE_PROTOCOLS на
        // hRequest. TLS 1.2 теперь принудительно включён на уровне СЕССИИ
        // в методе initialize(). Установка на hRequest вызывала ошибку 12018
        // и не обеспечивала корректное TLS-рукопожатие.
        // =====================================================================

        // Игнорирование ошибок сертификата (для самоподписанных сертификатов)
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags))) {
            g_logger.warning(L"WinHttpSetOption (SECURITY_FLAGS) failed: " + std::to_wstring(GetLastError()));
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Убран вызов setTimeouts() из метода post().
        // Таймауты установлены ОДИН РАЗ в initialize(). Вызов из разных
        // потоков создавал гонку за m_hSession и приводил к обрыву
        // TLS-рукопожатия.
        // =====================================================================

        if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            (LPVOID)utf8Body.c_str(), (DWORD)utf8Body.length(),
            (DWORD)utf8Body.length(), 0)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err) +
                L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(err) +
                L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        std::string responseText;
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                g_logger.error(L"WinHttpQueryDataAvailable failed: " + std::to_wstring(GetLastError()));
                break;
            }
            if (dwSize > 0) {
                std::vector<char> buffer(dwSize + 1, 0);
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, (LPVOID)buffer.data(), dwSize, &dwDownloaded)) {
                    responseText.append(buffer.data(), dwDownloaded);
                }
            }
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);

        if (responseText.empty()) {
            g_logger.warning(L"Empty response received from server");
            return std::nullopt;
        }

        g_logger.info(L"Response received, size: " + std::to_wstring(responseText.length()) +
            L", body: " + utf8_to_wstring(responseText));

        try {
            return json::parse(responseText);
        }
        catch (const json::parse_error& e) {
            g_logger.error(L"JSON parse error: " + utf8_to_wstring(e.what()));
            return std::nullopt;
        }
    }

    // =========================================================================
    // GET-запрос
    // =========================================================================
    std::optional<json> get(const std::wstring& path, const std::wstring& authToken = L"") {
        if (!m_hSession) {
            g_logger.error(L"HTTP Session is not initialized");
            return std::nullopt;
        }

        g_logger.info(L"Preparing GET request to: " + path);

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authToken.empty()) {
            headers += L"Authorization: Bearer " + authToken + L"\r\n";
            g_logger.info(L"Added Authorization header (Bearer token).");
        }

        HINTERNET hConnect = WinHttpConnect(m_hSession, m_serverName.c_str(), m_port, 0);
        if (!hConnect) {
            g_logger.error(L"WinHttpConnect failed: " + std::to_wstring(GetLastError()));
            return std::nullopt;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            g_logger.error(L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Убрана установка WINHTTP_OPTION_SECURE_PROTOCOLS на
        // hRequest. TLS 1.2 теперь принудительно включён на уровне СЕССИИ
        // в методе initialize().
        // =====================================================================

        // Игнорирование ошибок сертификата (для самоподписанных сертификатов)
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags))) {
            g_logger.warning(L"WinHttpSetOption (SECURITY_FLAGS) failed: " + std::to_wstring(GetLastError()));
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Убран вызов setTimeouts() из метода get().
        // Таймауты установлены ОДИН РАЗ в initialize().
        // =====================================================================

        if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err) +
                L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(err) +
                L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        std::string responseText;
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize > 0) {
                std::vector<char> buffer(dwSize + 1, 0);
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                    responseText.append(buffer.data(), dwDownloaded);
                }
            }
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);

        if (responseText.empty()) {
            g_logger.warning(L"Empty response received");
            return std::nullopt;
        }

        try {
            return json::parse(responseText);
        }
        catch (const json::parse_error& e) {
            g_logger.error(L"JSON parse error: " + utf8_to_wstring(e.what()));
            return std::nullopt;
        }
    }
};
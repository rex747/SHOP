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
        m_hSession = WinHttpOpen(L"KioskClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		g_logger.info(L"HTTPSClient initialized for server: " + serverName + L":" + std::to_wstring(port));
        if (!m_hSession) {
            g_logger.error(L"WinHttpOpen failed: " + std::to_wstring(GetLastError()));
        }
    }

    ~HTTPSClient() {
        if (m_hSession) {
            WinHttpCloseHandle(m_hSession);
			g_logger.log(LogLevel::INFO, L"HTTPSClient session closed");
        }
    }

    // ✅ ИСПРАВЛЕНИЕ ОШИБКИ C2039: Добавлен метод initialize для совместимости с main.cpp
    bool initialize() {
        return m_hSession != nullptr;
        if (!m_hSession) {
            g_logger.error(L"HTTPSClient::initialize() failed: session is null.");
        }
    }

    // ✅ ИСПРАВЛЕНИЕ ОШИБКИ C2660: Добавлен третий аргумент authToken со значением по умолчанию
    std::optional<json> post(const std::wstring& path, const json& requestBody, const std::wstring& authToken = L"") {
        if (!m_hSession) {
            g_logger.error(L"HTTP Session is not initialized");
            return std::nullopt;
        }

        std::string utf8Body = requestBody.dump();
        g_logger.info(L"Preparing POST request to: " + path);
        g_logger.info(L"Request body size: " + std::to_wstring(utf8Body.length()) + L" bytes");

        // Формируем базовые заголовки
        std::wstring headers = L"Content-Type: application/json\r\n";

        // ✅ ДОБАВЛЕНО: Если передан токен, добавляем заголовок Authorization для безопасности
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

        g_logger.info(L"Opening HTTPS request (WINHTTP_FLAG_SECURE)...");
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            g_logger.error(L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ ОШИБКИ 12029/12045:
        // Игнорируем ошибки проверки сертификата (неизвестный CA, несовпадение имени, истекший срок).
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;

        g_logger.info(L"Setting SECURITY_FLAG_IGNORE_* options for SSL...");

        if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags))) {
            g_logger.warning(L"WinHttpSetOption (SECURITY_FLAGS) failed: " + std::to_wstring(GetLastError()) + L". Proceeding anyway.");
        }

        g_logger.info(L"Sending POST request to: " + path);

        // ✅ Отправляем запрос с обновленными заголовками (включающими Bearer token, если он есть)
        BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            (LPVOID)utf8Body.c_str(), (DWORD)utf8Body.length(), (DWORD)utf8Body.length(), 0);

        if (!bResults) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err));
            if (err == 12029) {
                g_logger.error(L"ERROR 12029: Cannot connect. CHECK IF SERVER IP AND PORT (8443) ARE CORRECT AND FIREWALL ALLOWS IT.");
            }
            else if (err == 12045) {
                g_logger.error(L"ERROR 12045: Cannot connect. CHECK IF SERVER IP AND PORT (8443) ARE CORRECT AND FIREWALL ALLOWS IT.");
            }
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }
        
        g_logger.info(L"Waiting for response via WinHttpReceiveResponse...");
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (!bResults) {
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(GetLastError()));
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        DWORD dwSize = 0;
        std::string responseText;
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

        g_logger.info(L"Response received successfully. Size: " + std::to_wstring(responseText.length()) + L" bytes.");

        try {
            return json::parse(responseText);
        }
        catch (const json::parse_error& e) {
            g_logger.error(L"JSON parse error in response: " + utf8_to_wstring(e.what()));
            g_logger.error(L"Raw response: " + utf8_to_wstring(responseText));
            return std::nullopt;
        }
    }

    // -------------------------------------------------------------------------
    // GET-запрос с поддержкой авторизации Bearer токеном
    // -------------------------------------------------------------------------
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
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            g_logger.error(L"WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

        if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err));
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(GetLastError()));
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
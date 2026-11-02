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

    [[nodiscard]] std::string wstring_to_utf8(const std::wstring& wstr) const {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    [[nodiscard]] std::wstring utf8_to_wstring(const std::string& str) const {
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

    bool initialize() {
        if (!m_hSession) {
            g_logger.error(L"HTTPSClient::initialize() failed: session is null.");
            return false;
        }

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(m_hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            DWORD err = GetLastError();
            g_logger.error(L"HTTPSClient::initialize() - WinHttpSetOption(SECURE_PROTOCOLS) on SESSION failed: " +
                std::to_wstring(err) + L". TLS 1.2 enforcement FAILED.");
            return false;
        }
        g_logger.info(L"HTTPSClient::initialize() - TLS 1.2 enforced on session handle successfully.");

        if (!setTimeouts()) {
            g_logger.error(L"HTTPSClient::initialize() - setTimeouts() failed.");
            return false;
        }
        g_logger.info(L"HTTPSClient::initialize() - Timeouts configured: resolve=5000, connect=10000, send=10000, receive=10000 ms.");
        return true;
    }

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

    std::optional<json> post(const std::wstring& path, const json& requestBody, const std::wstring& authToken = L"") {
        if (!m_hSession) {
            g_logger.error(L"HTTP Session is not initialized");
            return std::nullopt;
        }

        std::string utf8Body = requestBody.dump();
        g_logger.info(L"Preparing POST request to: " + path);

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authToken.empty()) {
            headers += L"Authorization: Bearer " + authToken + L"\r\n";
        }

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

        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

        if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            (LPVOID)utf8Body.c_str(), (DWORD)utf8Body.length(),
            (DWORD)utf8Body.length(), 0)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err) + L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(err) + L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        // ИСПРАВЛЕНИЕ: Проверка HTTP статуса ответа
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);

        bool haveStatus = WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX
        ) == TRUE;

        if (haveStatus && statusCode != HTTP_STATUS_OK) {
            g_logger.warning(
                L"HTTP POST returned non-200 status: " +
                std::to_wstring(statusCode) +
                L" (path: " + path + L")"
            );
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
            if (haveStatus && statusCode != HTTP_STATUS_OK) {
                json errorObject;
                errorObject["_http_status"] = static_cast<int>(statusCode);
                errorObject["error"] = "HTTP error";
                return errorObject;
            }

            g_logger.warning(L"Empty response received from server");
            return std::nullopt;
        }

        try {
            json parsed = json::parse(responseText);

            if (haveStatus) {
                if (!parsed.is_object()) {
                    json wrapper;
                    wrapper["data"] = parsed;
                    parsed = wrapper;
                }

                parsed["_http_status"] = static_cast<int>(statusCode);
            }

            return parsed;
        }
        catch (const json::parse_error& e) {
            if (haveStatus && statusCode != HTTP_STATUS_OK) {
                json errorObject;
                errorObject["_http_status"] = static_cast<int>(statusCode);
                errorObject["error"] = "HTTP error";
                errorObject["raw"] = responseText;
                return errorObject;
            }

            g_logger.error(
                L"JSON parse error: " +
                utf8_to_wstring(e.what()) +
                L" | Raw response: " +
                utf8_to_wstring(responseText)
            );

            return std::nullopt;
        }
    }

    std::optional<json> get(const std::wstring& path, const std::wstring& authToken = L"") {
        if (!m_hSession) {
            g_logger.error(L"HTTP Session is not initialized");
            return std::nullopt;
        }

        g_logger.info(L"Preparing GET request to: " + path);
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authToken.empty()) {
            headers += L"Authorization: Bearer " + authToken + L"\r\n";
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

        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

        if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpSendRequest failed: " + std::to_wstring(err) + L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD err = GetLastError();
            g_logger.error(L"WinHttpReceiveResponse failed: " + std::to_wstring(err) + L" (path: " + path + L")");
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return std::nullopt;
        }

        // ИСПРАВЛЕНИЕ: Проверка HTTP статуса ответа
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
            if (statusCode != HTTP_STATUS_OK) {
                g_logger.error(L"HTTP GET request failed with status code: " + std::to_wstring(statusCode) + L" (path: " + path + L")");
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                return std::nullopt;
            }
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
            g_logger.error(L"JSON parse error: " + utf8_to_wstring(e.what()) + L" | Raw response: " + utf8_to_wstring(responseText));
            return std::nullopt;
        }
    }
};
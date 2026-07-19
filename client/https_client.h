// https_client.h
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "logger.h"
#include "config.h"
#include "string_utils.h"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

extern Logger g_logger;

class HTTPSClient {
private:
    std::wstring m_host;
    int m_port;
    HINTERNET m_hSession;
    HINTERNET m_hConnect;
    bool m_initialized;

    std::wstring getErrorDescription(DWORD errorCode) {
        LPWSTR buffer = nullptr;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&buffer, 0, nullptr
        );
        std::wstring result = buffer ? buffer : L"Unknown error";
        LocalFree(buffer);
        return result;
    }

public:
    HTTPSClient(const std::wstring& host, int port)
        : m_host(host), m_port(port), m_hSession(nullptr),
        m_hConnect(nullptr), m_initialized(false) {
    }

    ~HTTPSClient() {
        cleanup();
    }

    bool initialize() {
        if (m_initialized) return true;

        m_hSession = WinHttpOpen(L"TerminalKiosk/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);

        if (!m_hSession) {
            g_logger.error(L"WinHttpOpen failed: " +
                std::to_wstring(GetLastError()));
            return false;
        }

        // Set timeouts
        DWORD timeout = 30000; // 30 seconds
        WinHttpSetTimeouts(m_hSession, timeout, timeout, timeout, timeout);

        m_hConnect = WinHttpConnect(m_hSession, m_host.c_str(),
            m_port, 0);

        if (!m_hConnect) {
            g_logger.error(L"WinHttpConnect failed: " +
                std::to_wstring(GetLastError()));
            cleanup();
            return false;
        }

        m_initialized = true;
        g_logger.info(L"HTTPS client initialized successfully");
        return true;
    }

    void cleanup() {
        if (m_hConnect) {
            WinHttpCloseHandle(m_hConnect);
            m_hConnect = nullptr;
        }
        if (m_hSession) {
            WinHttpCloseHandle(m_hSession);
            m_hSession = nullptr;
        }
        m_initialized = false;
    }

    std::optional<json> sendRequest(const std::wstring& path,
        const std::wstring& method,
        const json& requestBody = json::object(),
        const std::wstring& authToken = L"") {
        if (!m_initialized) {
            g_logger.error(L"HTTPS client not initialized");
            return std::nullopt;
        }

        HINTERNET hRequest = WinHttpOpenRequest(
            m_hConnect, method.c_str(), path.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE  // HTTPS
        );

        if (!hRequest) {
            g_logger.error(L"WinHttpOpenRequest failed");
            return std::nullopt;
        }

        // Set security flags
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;

        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS,
            &flags, sizeof(flags));

        if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags))) {
            g_logger.warning(L"Failed to set security flags: " + std::to_wstring(GetLastError()));
        }

        // Add headers
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authToken.empty()) {
            headers += L"Authorization: Bearer " + authToken + L"\r\n";
        }

        // Convert JSON to string
        std::string requestStr = requestBody.dump();
        

        // Send request
        BOOL result = WinHttpSendRequest(
            hRequest, headers.c_str(), -1,
            (LPVOID)requestStr.c_str(),
            static_cast<DWORD>(requestStr.size()), static_cast<DWORD>(requestStr.size()), 0
        );

        if (!result) {
            g_logger.error(L"WinHttpSendRequest failed");
            WinHttpCloseHandle(hRequest);
            return std::nullopt;
        }

        // Receive response
        result = WinHttpReceiveResponse(hRequest, nullptr);
        if (!result) {
            g_logger.error(L"WinHttpReceiveResponse failed");
            WinHttpCloseHandle(hRequest);
            return std::nullopt;
        }

        // Get status code
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size,
            WINHTTP_NO_HEADER_INDEX);

        // Read response body (ВСЕГДА, независимо от статуса)
        std::wstring response;
        DWORD bytesAvailable = 0;
        do {
            bytesAvailable = 0;
            WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
            if (bytesAvailable > 0) {
                std::vector<wchar_t> buffer(bytesAvailable / sizeof(wchar_t) + 1);
                DWORD bytesRead = 0;
                WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
                response.append(buffer.data(), bytesRead / sizeof(wchar_t));
            }
        } while (bytesAvailable > 0);

        WinHttpCloseHandle(hRequest);

        // Parse JSON response
        std::string responseStr = wstring_to_utf8(response);
        json responseJson;
        try {
            responseJson = json::parse(responseStr);
        }
        catch (const json::exception& e) {
            g_logger.error(L"JSON parse error: " + utf8_to_wstring(e.what()));
            return std::nullopt;
        }

        // Return parsed JSON with status code info
        responseJson["_http_status"] = statusCode;

        if (statusCode != 200 && statusCode != 201) {
            g_logger.error(L"HTTP error: " + std::to_wstring(statusCode) +
                L", body: " + utf8_to_wstring(responseStr));
        }

        return responseJson;
    }

    // Convenience methods
    std::optional<json> get(const std::wstring& path, const std::wstring& token = L"") {
        return sendRequest(path, L"GET", json::object(), token);
    }

    std::optional<json> post(const std::wstring& path, const json& data,
        const std::wstring& token = L"") {
        return sendRequest(path, L"POST", data, token);
    }

    std::optional<json> put(const std::wstring& path, const json& data,
        const std::wstring& token = L"") {
        return sendRequest(path, L"PUT", data, token);
    }
};

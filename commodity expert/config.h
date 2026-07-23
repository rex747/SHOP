// config.h
#pragma once

#include <string>
#include <filesystem>
#include <shlobj.h>

namespace Config {
    // Server configuration
    constexpr const wchar_t* SERVER_HOST = L"77.222.32.209";
    constexpr int SERVER_PORT = 8443;
    constexpr const wchar_t* SERVER_BASE_URL = L"https://77.222.32.209:8443/api/v1";

    // Database configuration
    constexpr const wchar_t* DB_HOST = L"77.222.32.209";
    constexpr int DB_PORT = 5432;
    constexpr const wchar_t* DB_NAME = L"kiosk_db";
    constexpr const wchar_t* DB_USER = L"kiosk_user";
    constexpr const wchar_t* DB_PASSWORD = L"secure_password_here";

    // 1C Integration
    constexpr const wchar_t* ONEC_BASE_URL = L"http://1c-server:1550/hs/exchange";
    constexpr const wchar_t* ONEC_USER = L"1c_user";
    constexpr const wchar_t* ONEC_PASSWORD = L"1c_password";

    // Terminal settings
    constexpr int MAX_ITEMS_GENERAL_QUEUE = 20;
    constexpr int MAX_ITEMS_PAID_QUEUE = 20;
    constexpr int PAID_ACCEPTANCE_PRICE = 200;
    constexpr int EXPENSIVE_ITEM_THRESHOLD = 5000;
    constexpr int TRUST_ACCEPTANCE_LIMIT = 1;

    // UI settings
    constexpr int BUTTON_HEIGHT = 60;
    constexpr int BUTTON_MARGIN = 10;
    constexpr COLORREF PRIMARY_COLOR = RGB(76, 175, 80);
    constexpr COLORREF SECONDARY_COLOR = RGB(33, 150, 243);
    constexpr COLORREF ACCENT_COLOR = RGB(255, 87, 34);
    constexpr COLORREF BACK_BUTTON_COLOR = RGB(211, 47, 47);

    // Paths
    inline std::wstring getAppDataPath() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA,
            nullptr, 0, path))) {
            return std::wstring(path) + L"\\TerminalKiosk";
        }
        return L"C:\\TerminalKiosk";
    }

    inline std::wstring getLogPath() {
        return getAppDataPath() + L"\\logs";
    }

    inline std::wstring getDatabasePath() {
        return getAppDataPath() + L"\\local.db";
    }

    // Create directories if not exist
    inline void initializePaths() {
        std::error_code ec;
        std::filesystem::create_directories(getLogPath(), ec);
        std::filesystem::create_directories(getAppDataPath(), ec);
    }
}

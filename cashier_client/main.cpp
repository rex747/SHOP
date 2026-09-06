// cashier_main.cpp
// Точка входа клиентской части кассира
// POS моноблок АТОЛ, Windows IoT
// C++20, WinAPI, Visual Studio 2022 (без CMake)
#define WIN32_LEAN_AND_MEAN
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#include <windows.h>
#include <commctrl.h>
#include <string>
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "auth_manager.h"
#include "cashier_window.h"
#include "login_window.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace std;

HINSTANCE g_hInstance;
Logger g_logger(L"cashier.log");
HTTPSClient g_httpsClient(Config::SERVER_HOST, Config::SERVER_PORT);
AuthManager g_authManager;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInstance = hInstance;

    // Инициализация общих контролов
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    g_logger.log(LogLevel::INFO, L"Cashier application started");
    Config::initializePaths();

    // Инициализация HTTPS клиента
    if (!g_httpsClient.initialize()) {
        g_logger.log(LogLevel::ERROR, L"Failed to initialize HTTPS client");
        MessageBoxW(nullptr, L"Ошибка инициализации сети", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    // Авторизация кассира (телефон + пароль, как у товароведа)
    LoginWindow loginWnd;
    bool loginSuccess = loginWnd.show(nullptr);

    if (!loginSuccess) {
        g_logger.log(LogLevel::WARNING, L"Cashier login cancelled or failed");
        return 0;
    }

    // Проверка роли: кассир или товаровед
    std::wstring userRole = g_authManager.getRole();
    g_logger.log(LogLevel::INFO, L"Cashier role: " + userRole);

    if (userRole != L"cashier" && userRole != L"worker") {
        g_logger.log(LogLevel::ERROR, L"Access denied: role is not cashier or worker");
        MessageBoxW(nullptr, L"Доступ запрещён. Требуется роль кассира.",
            L"Ошибка доступа", MB_ICONERROR);
        return 1;
    }

    g_logger.log(LogLevel::INFO, L"Cashier authenticated: " +
        g_authManager.getFullName());

    // Запуск окна кассира
    CashierWindow cashierWnd;
    cashierWnd.show();

    g_logger.log(LogLevel::INFO, L"Cashier application shutdown");
    return 0;
}
// main.cpp
// Terminal Self-Service Kiosk - Main Entry Point
// C++20, WinAPI, HTTPS

#define WIN32_LEAN_AND_MEAN
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
//#define UNICODE
//#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <iostream>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "config.h"
#include "https_client.h"
#include "database_local.h"
#include "auth_manager.h"
#include "queue_manager.h"
#include "registration_dialog.h"
#include "main_window.h"
#include "logger.h"
#include "receipt_printer.h"

using namespace std;

HINSTANCE g_hInstance;
HWND g_hMainWnd;
Logger g_logger(L"terminal.log");
HTTPSClient g_httpsClient(L"77.222.32.209", 8080);
QueueManager g_queueManager;
AuthManager g_authManager;
ReceiptPrinter g_printer;

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow
);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    // Initialize common controls
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize logger
    g_logger.log(LogLevel::INFO, L"Terminal application started");

    Config::initializePaths();   // создаёт каталоги для логов и БД

    // Initialize local database
    if (!LocalDB::initialize()) {
        MessageBox(nullptr, L"Failed to initialize local database",
            L"Error", MB_ICONERROR);
        return 1;
    }

    // Initialize HTTPS client with SSL
    if (!g_httpsClient.initialize()) {
        g_logger.log(LogLevel::ERROR, L"Failed to initialize HTTPS client");
        return 1;
    }

    // Register window class
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MainWndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"TerminalKioskClass";
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        g_logger.log(LogLevel::ERROR, L"Failed to register window class");
        return 1;
    }

    // Create main window (fullscreen for kiosk mode)
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    g_hMainWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        L"TerminalKioskClass",
        L"ДОБРО - Комиссионный магазин",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hMainWnd) {
        g_logger.log(LogLevel::ERROR, L"Failed to create main window");
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    g_logger.log(LogLevel::INFO, L"Main window created successfully");

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_logger.log(LogLevel::INFO, L"Terminal application shutdown");
    return static_cast<int>(msg.wParam);
}
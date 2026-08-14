// main.cpp – WorkerApp (окно товароведа)
// Отдельное приложение для управления очередями
// Теперь требует авторизации через LoginWindow

#define WIN32_LEAN_AND_MEAN
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma warning(disable: 4996)

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <memory>
#include <thread>

#include "config.h"
#include "https_client.h"
#include "logger.h"
#include "string_utils.h"
#include "worker_window.h"
#include "auth_manager.h"
#include "login_window.h"
#include "director_window.h"

// Глобальные объекты
HINSTANCE g_hInstance;
Logger g_logger(L"worker.log");
HTTPSClient g_httpsClient(Config::SERVER_HOST, Config::SERVER_PORT);
AuthManager g_authManager;

constexpr const wchar_t* DIRECTOR_PHONE = L"+79914869324"; // Телефон директора магазина Добро

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    g_hInstance = hInstance;

    // Инициализация общих элементов управления
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_TAB_CLASSES |
        ICC_LISTVIEW_CLASSES;  // Добавлены Tab и ListView
    InitCommonControlsEx(&icex);

    g_logger.info(L"WorkerApp started");
    g_logger.info(L"Director phone constant: " + std::wstring(DIRECTOR_PHONE));

    // Инициализация HTTPS клиента
    if (!g_httpsClient.initialize()) {
        g_logger.error(L"HTTPS client initialization failed");
        MessageBoxW(nullptr, L"Не удалось инициализировать HTTPS клиент", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    // === Восстановлен вход товароведа ===
    HWND hDummy = GetDesktopWindow();
    LoginWindow loginWnd;
    bool loginSuccess = loginWnd.show(hDummy);
    if (!loginSuccess) {
        g_logger.info(L"Login cancelled or failed, exiting WorkerApp");
        MessageBoxW(nullptr, L"Для работы товароведа требуется вход в систему.", L"Внимание", MB_OK);
        return 0;
    }

    g_logger.info(L"Login successful, starting WorkerWindow");

	//Определение роли пользователя по номеру телефона
    std::wstring userPhone = g_authManager.getPhone();
    g_logger.info(L"User phone after login: " + userPhone);

	// Если пользователь является директором, открываем окно директора
    if (userPhone == DIRECTOR_PHONE) {
        g_logger.info(L"DIRECTOR DETECTED: phone=" + userPhone);
        g_logger.info(L"Opening Director Control Panel...");

        MessageBoxW(nullptr,
            L"Добро пожаловать, Директор магазина!\n\n"
            L"Открывается панель управления.",
            L"Панель управления директора",
            MB_OK | MB_ICONINFORMATION);

        // Открываем панель управления директора
        DirectorWindow directorWnd;
        directorWnd.show();

        g_logger.info(L"DirectorApp shut down");
        return 0;
    }
    // СТАНДАРТНЫЙ ПУТЬ: ТОВАРОВЕД
    
    g_logger.info(L"WORKER DETECTED: phone=" + userPhone);
    g_logger.info(L"Starting WorkerWindow");

    // Открываем окно товароведа
    WorkerWindow workerWnd;
    workerWnd.show();

    g_logger.info(L"WorkerApp shut down");
    return 0;
}
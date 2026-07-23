// main.cpp – WorkerApp (окно товароведа)
// Отдельное приложение для управления очередью "Первый раз"

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

// Глобальные объекты (аналогично основному приложению)
HINSTANCE g_hInstance;
Logger g_logger(L"worker.log");          // свой лог-файл
HTTPSClient g_httpsClient(Config::SERVER_HOST, Config::SERVER_PORT);

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    g_hInstance = hInstance;

    // Инициализация общих элементов управления
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    g_logger.info(L"WorkerApp started");

    // Инициализация HTTPS клиента
    if (!g_httpsClient.initialize()) {
        g_logger.error(L"HTTPS client initialization failed");
        MessageBoxW(nullptr, L"Не удалось инициализировать HTTPS клиент", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    // Создание и показ окна товароведа
    WorkerWindow workerWnd;
    workerWnd.show();   // Эта функция запускает собственный цикл сообщений

    g_logger.info(L"WorkerApp shut down");
    return 0;
}
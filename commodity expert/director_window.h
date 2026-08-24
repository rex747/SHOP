// director_window.h
// =============================================================================
// ПАНЕЛЬ УПРАВЛЕНИЯ ДИРЕКТОРА МАГАЗИНА ДОБРО
// =============================================================================
// ИСПРАВЛЕНИЯ ОТ 21.08.2026 (ТРЕТЬЯ ИТЕРАЦИЯ):
//
// НОВЫЙ ФУНКЦИОНАЛ: ПРИЧИНА БЛОКИРОВКИ КОМИТЕНТА
// -------------------------------------------------
// 1. Добавлен класс BlockReasonDialog — модальное окно с полем ввода текста
//    (до 1000 символов) для указания причины блокировки.
// 2. В ListView комитентов добавлена колонка 11 "Причина блокировки".
// 3. При блокировке директор указывает причину, которая сохраняется на сервере
//    и отображается в строке сведений о комитенте после столбца "Статус".
// 4. При разблокировке причина автоматически очищается на сервере.
// 5. Серверная валидация: причина не может превышать 1000 символов.
//
// Архитектура:
// - Окно использует Win32 API (CreateWindowExW, ListView, Tab Control)
// - Все сетевые запросы выполняются асинхронно в фоновых потоках
// - Для обновления UI используются PostMessage с WM_APP + N
// =============================================================================
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "auth_manager.h"
#include "string_utils.h"
#pragma comment(lib, "comctl32.lib")
extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;
using json = nlohmann::json;
// =============================================================================
// ИДЕНТИФИКАТОРЫ ЭЛЕМЕНТОВ УПРАВЛЕНИЯ
// =============================================================================
#define IDC_DIRECTOR_TAB_CONTROL        7001
#define IDC_DIRECTOR_CLIENTS_LIST       7002
#define IDC_DIRECTOR_WORKERS_LIST       7003
#define IDC_DIRECTOR_LOWQUALITY_LIST    7004
#define IDC_DIRECTOR_SUMMARY_LABEL      7005
#define IDC_DIRECTOR_BLOCK_BTN          7006
#define IDC_DIRECTOR_UNBLOCK_BTN        7007
#define IDC_DIRECTOR_REFRESH_BTN        7008
#define IDC_DIRECTOR_CLOSE_BTN          7009
#define IDC_DIRECTOR_STATUS_LABEL       7010
#define IDC_DIRECTOR_SEARCH_EDIT        7011
#define IDC_DIRECTOR_SEARCH_BTN         7012
#define IDC_DIRECTOR_EXPIRED_LIST       7013
// Сообщения для асинхронного обновления UI
#define WM_DIRECTOR_DATA_LOADED         (WM_APP + 10)
#define WM_DIRECTOR_BLOCK_RESULT        (WM_APP + 11)
#define WM_DIRECTOR_ERROR               (WM_APP + 12)
// =============================================================================
// КОНСТАНТЫ
// =============================================================================
namespace DirectorConfig {
    // Телефон директора магазина (захардкожено согласно требованиям)
    constexpr const wchar_t* DIRECTOR_PHONE = L"+79914869324";
    // Цвета UI
    constexpr COLORREF HEADER_COLOR = RGB(45, 45, 48);      // Тёмный фон заголовка
    constexpr COLORREF PRIMARY_COLOR = RGB(0, 122, 204);    // Синий (основной)
    constexpr COLORREF SUCCESS_COLOR = RGB(76, 175, 80);    // Зелёный (успех)
    constexpr COLORREF DANGER_COLOR = RGB(211, 47, 47);     // Красный (опасность)
    constexpr COLORREF WARNING_COLOR = RGB(255, 152, 0);    // Оранжевый (предупреждение)
    constexpr COLORREF BACKGROUND_COLOR = RGB(245, 245, 245); // Светлый фон
    // Размеры
    constexpr int WINDOW_WIDTH = 1400;
    constexpr int WINDOW_HEIGHT = 900;
    constexpr int BUTTON_HEIGHT = 45;
    constexpr int BUTTON_WIDTH = 180;
    constexpr int MARGIN = 20;
    // Интервал автообновления (мс)
    constexpr int AUTO_REFRESH_INTERVAL_MS = 30000; // 30 секунд
    // =====================================================================
    // НОВОЕ: Константы для диалога причины блокировки
    // =====================================================================
    constexpr int MAX_BLOCK_REASON_LENGTH = 1000;       // Максимальная длина причины
    constexpr int BLOCK_REASON_DIALOG_WIDTH = 550;      // Ширина диалога
    constexpr int BLOCK_REASON_DIALOG_HEIGHT = 320;     // Высота диалога
}
// =============================================================================
// СТРУКТУРА ДАННЫХ ДЛЯ РЕЗУЛЬТАТА БЛОКИРОВКИ
// =============================================================================
struct BlockResult {
    bool success;
    int clientId;
    bool blocked;
    std::wstring message;
    std::wstring blockReason;   // НОВОЕ: причина блокировки
    BlockResult() : success(false), clientId(0), blocked(false) {}
};
// =============================================================================
// СТРУКТУРА ДАННЫХ ДЛЯ СТАТИСТИКИ
// =============================================================================
struct DirectorStats {
    json clients;       // Массив комитентов
    json workers;       // Массив товароведов
    json lowQuality;    // Массив не проданных товаров из-за низкого качества
    json expired;       // Массив товаров, выбывших по 15-дневному сроку
    json summary;       // Общая сводка
    bool loaded;        // Флаг успешной загрузки
    DirectorStats() : loaded(false) {}
};
// =============================================================================
// КЛАСС ДИАЛОГА ВВОДА ПРИЧИНЫ БЛОКИРОВКИ
// =============================================================================
// Модальное окно с полем ввода текста (до 1000 символов) для указания
// причины блокировки комитента. Создаётся программно без использования
// ресурсов (.rc файлов).
// =============================================================================
class BlockReasonDialog {
private:
    HWND m_hWnd;                    // Дескриптор окна диалога
    HWND m_hEdit;                   // Поле ввода причины
    HWND m_hCharCountLabel;         // Label со счётчиком символов
    std::wstring m_reason;          // Введённая причина
    bool m_confirmed;               // Флаг подтверждения (OK/Cancel)
    static constexpr const wchar_t* CLASS_NAME = L"BlockReasonDialogClass";
    // =====================================================================
    // Обработчик изменения текста в поле ввода (для обновления счётчика)
    // =====================================================================
    void updateCharCount() {
        int len = GetWindowTextLengthW(m_hEdit);
        wchar_t buf[32];
        swprintf_s(buf, L"%d / %d", len, DirectorConfig::MAX_BLOCK_REASON_LENGTH);
        SetWindowTextW(m_hCharCountLabel, buf);
        // Меняем цвет счётчика, если превышен лимит
        if (len >= DirectorConfig::MAX_BLOCK_REASON_LENGTH) {
            SetTextColor(GetDC(m_hCharCountLabel), DirectorConfig::DANGER_COLOR);
        }
        else {
            SetTextColor(GetDC(m_hCharCountLabel), RGB(0, 0, 0));
        }
    }
    // =====================================================================
    // Оконная процедура диалога
    // =====================================================================
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        BlockReasonDialog* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<BlockReasonDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            return 0;
        }
        pThis = reinterpret_cast<BlockReasonDialog*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);
        switch (msg) {
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            // Обработка изменения текста в поле ввода (для счётчика символов)
            if (id == 1001 && code == EN_CHANGE) {
                pThis->updateCharCount();
                return 0;
            }
            // Кнопка "Заблокировать" (OK)
            if (id == 1 && code == BN_CLICKED) {
                wchar_t buf[1024];
                GetWindowTextW(pThis->m_hEdit, buf, 1024);
                pThis->m_reason = buf;
                pThis->m_confirmed = true;
                DestroyWindow(hWnd);
                return 0;
            }
            // Кнопка "Отмена"
            if (id == 2 && code == BN_CLICKED) {
                pThis->m_confirmed = false;
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            pThis->m_confirmed = false;
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            // Не вызываем PostQuitMessage, так как это модальный диалог
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    // =====================================================================
    // Создание элементов управления диалога
    // =====================================================================
    void createControls() {
        g_logger.info(L"BlockReasonDialog: createControls started");
        // Шрифты
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hFontTitle = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        // Заголовок диалога
        HWND hTitle = CreateWindowExW(0, L"STATIC",
            L"Укажите причину блокировки комитента:",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            20, 20, DirectorConfig::BLOCK_REASON_DIALOG_WIDTH - 40, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        // Поле ввода причины (многострочное, с вертикальной прокруткой)
        m_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL |
            WS_VSCROLL | WS_HSCROLL | ES_WANTRETURN,
            20, 60, DirectorConfig::BLOCK_REASON_DIALOG_WIDTH - 40, 170,
            m_hWnd, (HMENU)(INT_PTR)1001, g_hInstance, nullptr);
        SendMessageW(m_hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        // Ограничение длины текста (1000 символов)
        SendMessageW(m_hEdit, EM_SETLIMITTEXT, DirectorConfig::MAX_BLOCK_REASON_LENGTH, 0);
        // Счётчик символов
        m_hCharCountLabel = CreateWindowExW(0, L"STATIC", L"0 / 1000",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            20, 235, DirectorConfig::BLOCK_REASON_DIALOG_WIDTH - 40, 20,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hCharCountLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        // Кнопка "Заблокировать" (по умолчанию)
        HWND hOkBtn = CreateWindowExW(0, L"BUTTON", L" Заблокировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            20, 260, 200, 40,
            m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(hOkBtn, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        // Кнопка "Отмена"
        HWND hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отмена",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            240, 260, 200, 40,
            m_hWnd, (HMENU)(INT_PTR)2, g_hInstance, nullptr);
        SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        // Фокус на поле ввода
        SetFocus(m_hEdit);
        g_logger.info(L"BlockReasonDialog: controls created");
    }
public:
    BlockReasonDialog() : m_hWnd(nullptr), m_hEdit(nullptr), m_hCharCountLabel(nullptr),
        m_confirmed(false) {
        g_logger.info(L"BlockReasonDialog: constructor");
    }
    ~BlockReasonDialog() {
        g_logger.info(L"BlockReasonDialog: destructor");
    }
    // =====================================================================
    // Показать модальный диалог
    // @param hParent Родительское окно
    // @param outReason Вывод: введённая причина (если подтверждено)
    // @return true если пользователь нажал "Заблокировать", false если "Отмена"
    // =====================================================================
    bool show(HWND hParent, std::wstring& outReason) {
        g_logger.info(L"BlockReasonDialog: show() called");
        // Регистрируем класс окна (один раз)
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = g_hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszClassName = CLASS_NAME;
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            if (!RegisterClassExW(&wcex)) {
                g_logger.error(L"BlockReasonDialog: RegisterClassExW failed");
                return false;
            }
            classRegistered = true;
            g_logger.info(L"BlockReasonDialog: class registered");
        }
        // Вычисляем позицию окна (по центру экрана)
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenW - DirectorConfig::BLOCK_REASON_DIALOG_WIDTH) / 2;
        int y = (screenH - DirectorConfig::BLOCK_REASON_DIALOG_HEIGHT) / 2;
        // Создаём окно диалога
        m_hWnd = CreateWindowExW(
            WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME,
            CLASS_NAME,
            L"Причина блокировки комитента",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            x, y,
            DirectorConfig::BLOCK_REASON_DIALOG_WIDTH,
            DirectorConfig::BLOCK_REASON_DIALOG_HEIGHT,
            hParent, nullptr, g_hInstance, this
        );
        if (!m_hWnd) {
            g_logger.error(L"BlockReasonDialog: CreateWindowExW failed");
            return false;
        }
        // Делаем родительское окно недоступным (модальность)
        EnableWindow(hParent, FALSE);
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        g_logger.info(L"BlockReasonDialog: window shown, entering modal loop");
        // Модальный цикл обработки сообщений
        MSG msg;
        while (IsWindow(m_hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // Восстанавливаем родительское окно
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        // Возвращаем результат
        outReason = m_reason;
        g_logger.info(L"BlockReasonDialog: modal loop ended, confirmed=" +
            std::wstring(m_confirmed ? L"true" : L"false") +
            L", reason length=" + std::to_wstring(m_reason.length()));
        return m_confirmed;
    }
};
// =============================================================================
// КЛАСС ПАНЕЛИ УПРАВЛЕНИЯ ДИРЕКТОРА
// =============================================================================
class DirectorWindow {
private:
    // =========================================================================
    // ДЕСКРИПТОРЫ ОКОН И ЭЛЕМЕНТОВ УПРАВЛЕНИЯ
    // =========================================================================
    HWND m_hWnd;                    // Главное окно
    HWND m_hTabControl;             // Табы (Комитенты, Товароведы, Брак)
    HWND m_hClientsList;            // ListView для комитентов
    HWND m_hWorkersList;            // ListView для товароведов
    HWND m_hLowQualityList;         // ListView для не проданных товаров
    HWND m_hExpiredList;            // ListView для выбывших товаров
    HWND m_hSummaryLabel;           // Label для общей сводки
    HWND m_hBlockBtn;               // Кнопка "Заблокировать"
    HWND m_hUnblockBtn;             // Кнопка "Разблокировать"
    HWND m_hRefreshBtn;             // Кнопка "Обновить"
    HWND m_hCloseBtn;               // Кнопка "Закрыть"
    HWND m_hStatusLabel;            // Label для статуса
    HWND m_hSearchEdit;             // Поле поиска
    HWND m_hSearchBtn;              // Кнопка поиска
    // =========================================================================
    // ШРИФТЫ И КИСТИ
    // =========================================================================
    HFONT m_hFontTitle;             // Шрифт заголовка (24pt Bold)
    HFONT m_hFontNormal;            // Основной шрифт (16pt)
    HFONT m_hFontSmall;             // Малый шрифт (14pt)
    HFONT m_hFontButton;            // Шрифт кнопок (16pt Bold)
    HBRUSH m_hBackgroundBrush;      // Кисть фона
    // =========================================================================
    // ДАННЫЕ
    // =========================================================================
    DirectorStats m_stats;          // Загруженная статистика
    std::mutex m_dataMutex;         // Мьютекс для защиты данных
    std::atomic<bool> m_isLoading;  // Флаг загрузки данных
    std::atomic<bool> m_isBlocking; // Флаг выполнения блокировки
    int m_selectedClientId;         // ID выбранного клиента для блокировки
    std::wstring m_selectedClientPhone; // Телефон выбранного клиента
    std::wstring m_searchFilter;    // Фильтр поиска
    // =========================================================================
    // ПОТОК АВТООБНОВЛЕНИЯ
    // =========================================================================
    std::thread m_refreshThread;    // Поток автообновления
    std::atomic<bool> m_running;    // Флаг работы потока
    // =========================================================================
    // ИМЯ КЛАССА ОКНА
    // =========================================================================
    static constexpr const wchar_t* CLASS_NAME = L"DirectorWindowClass";
    // =========================================================================
    // ИНИЦИАЛИЗАЦИЯ ШРИФТОВ И КИСТЕЙ
    // =========================================================================
    void createFontsAndBrushes() {
        g_logger.info(L"DirectorWindow: createFontsAndBrushes started");
        // Заголовок окна
        m_hFontTitle = CreateFontW(
            28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        // Основной текст
        m_hFontNormal = CreateFontW(
            18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        // Малый текст (статус)
        m_hFontSmall = CreateFontW(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        // Кнопки
        m_hFontButton = CreateFontW(
            16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        // Кисть фона
        m_hBackgroundBrush = CreateSolidBrush(DirectorConfig::BACKGROUND_COLOR);
        g_logger.info(L"DirectorWindow: fonts and brushes created");
    }
    // =========================================================================
    // ОСВОБОЖДЕНИЕ ШРИФТОВ И КИСТЕЙ
    // =========================================================================
    void releaseFontsAndBrushes() {
        g_logger.info(L"DirectorWindow: releaseFontsAndBrushes started");
        if (m_hFontTitle) { DeleteObject(m_hFontTitle); m_hFontTitle = nullptr; }
        if (m_hFontNormal) { DeleteObject(m_hFontNormal); m_hFontNormal = nullptr; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = nullptr; }
        if (m_hFontButton) { DeleteObject(m_hFontButton); m_hFontButton = nullptr; }
        if (m_hBackgroundBrush) { DeleteObject(m_hBackgroundBrush); m_hBackgroundBrush = nullptr; }
        g_logger.info(L"DirectorWindow: fonts and brushes released");
    }
    // =========================================================================
    // СОЗДАНИЕ ЭЛЕМЕНТОВ УПРАВЛЕНИЯ
    // =========================================================================
    void createControls() {
        g_logger.info(L"DirectorWindow: createControls started");
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        // =====================================================================
        // ЗАГОЛОВОК ОКНА
        // =====================================================================
        HWND hTitle = CreateWindowExW(
            0, L"STATIC", L"Панель управления директора магазина ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 10, width, 40,
            m_hWnd, nullptr, g_hInstance, nullptr
        );
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
        // =====================================================================
        // ПАНЕЛЬ КНОПОК УПРАВЛЕНИЯ
        // =====================================================================
        int btnY = 60;
        int btnX = DirectorConfig::MARGIN;
        // Кнопка "Обновить"
        m_hRefreshBtn = CreateWindowExW(
            0, L"BUTTON", L"⟳ Обновить данные",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, btnY, DirectorConfig::BUTTON_WIDTH, DirectorConfig::BUTTON_HEIGHT,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_REFRESH_BTN, g_hInstance, nullptr
        );
        SendMessageW(m_hRefreshBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        btnX += DirectorConfig::BUTTON_WIDTH + 10;
        // Кнопка "Заблокировать"
        m_hBlockBtn = CreateWindowExW(
            0, L"BUTTON", L"🚫 Заблокировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, btnY, DirectorConfig::BUTTON_WIDTH, DirectorConfig::BUTTON_HEIGHT,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_BLOCK_BTN, g_hInstance, nullptr
        );
        SendMessageW(m_hBlockBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        btnX += DirectorConfig::BUTTON_WIDTH + 10;
        // Кнопка "Разблокировать"
        m_hUnblockBtn = CreateWindowExW(
            0, L"BUTTON", L"✓ Разблокировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            btnX, btnY, DirectorConfig::BUTTON_WIDTH, DirectorConfig::BUTTON_HEIGHT,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_UNBLOCK_BTN, g_hInstance, nullptr
        );
        SendMessageW(m_hUnblockBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        btnX += DirectorConfig::BUTTON_WIDTH + 10;
        // Кнопка "Закрыть"
        m_hCloseBtn = CreateWindowExW(
            0, L"BUTTON", L" Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            width - DirectorConfig::BUTTON_WIDTH - DirectorConfig::MARGIN, btnY,
            DirectorConfig::BUTTON_WIDTH, DirectorConfig::BUTTON_HEIGHT,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_CLOSE_BTN, g_hInstance, nullptr
        );
        SendMessageW(m_hCloseBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        // =====================================================================
        // ПОИСК
        // =====================================================================
        int searchY = btnY + DirectorConfig::BUTTON_HEIGHT + 15;
        HWND hSearchLabel = CreateWindowExW(
            0, L"STATIC", L"Поиск по телефону или ФИО:",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            DirectorConfig::MARGIN, searchY + 8, 250, 30,
            m_hWnd, nullptr, g_hInstance, nullptr
        );
        SendMessageW(hSearchLabel, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        m_hSearchEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            DirectorConfig::MARGIN + 260, searchY, 300, 35,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_SEARCH_EDIT, g_hInstance, nullptr
        );
        SendMessageW(m_hSearchEdit, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        m_hSearchBtn = CreateWindowExW(
            0, L"BUTTON", L"🔍 Найти",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            DirectorConfig::MARGIN + 570, searchY, 120, 35,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_SEARCH_BTN, g_hInstance, nullptr
        );
        SendMessageW(m_hSearchBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        // =====================================================================
        // TAB CONTROL (ВКЛАДКИ)
        // =====================================================================
        int tabY = searchY + 50;
        int tabHeight = height - tabY - 100;
        m_hTabControl = CreateWindowExW(
            0, WC_TABCONTROLW, L"",
            WS_VISIBLE | WS_CHILD | TCS_TABS | TCS_FIXEDWIDTH,
            DirectorConfig::MARGIN, tabY,
            width - 2 * DirectorConfig::MARGIN, tabHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_TAB_CONTROL, g_hInstance, nullptr
        );
        SendMessageW(m_hTabControl, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        // Добавляем вкладки
        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(L"📋 Комитенты");
        TabCtrl_InsertItem(m_hTabControl, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(L" Товароведы (Эффективность)");
        TabCtrl_InsertItem(m_hTabControl, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(L"⚠️ Не продано (Низкое качество)");
        TabCtrl_InsertItem(m_hTabControl, 2, &tie);
        tie.pszText = const_cast<LPWSTR>(L"⏰ Выбывшие из продажи (15 дней)");
        TabCtrl_InsertItem(m_hTabControl, 3, &tie);
        // =====================================================================
        // LISTVIEW ДЛЯ КОМИТЕНТОВ (ВКЛАДКА 0)
        // =====================================================================
        createClientsListView();
        // =====================================================================
        // LISTVIEW ДЛЯ ТОВАРОВЕДОВ (ВКЛАДКА 1)
        // =====================================================================
        createWorkersListView();
        // =====================================================================
        // LISTVIEW ДЛЯ НЕ ПРОДАННЫХ ТОВАРОВ (ВКЛАДКА 2)
        // =====================================================================
        createLowQualityListView();
        // =====================================================================
        // LISTVIEW ДЛЯ ПРОСРОЧЕННЫХ ТОВАРОВ (ВКЛАДКА 3)
        // =====================================================================
        createExpiredListView();
        // =====================================================================
        // СВОДКА (ВНИЗУ ОКНА)
        // =====================================================================
        m_hSummaryLabel = CreateWindowExW(
            0, L"STATIC", L"Загрузка данных...",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            DirectorConfig::MARGIN, height - 80,
            width - 2 * DirectorConfig::MARGIN, 30,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_SUMMARY_LABEL, g_hInstance, nullptr
        );
        SendMessageW(m_hSummaryLabel, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        // =====================================================================
        // СТАТУС (ВНИЗУ ОКНА)
        // =====================================================================
        m_hStatusLabel = CreateWindowExW(
            0, L"STATIC", L"Готово к работе",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            DirectorConfig::MARGIN, height - 45,
            width - 2 * DirectorConfig::MARGIN, 25,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_STATUS_LABEL, g_hInstance, nullptr
        );
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontSmall, TRUE);
        // Показываем первую вкладку
        showTab(0);
        g_logger.info(L"DirectorWindow: all controls created");
    }
    // =========================================================================
    // СОЗДАНИЕ LISTVIEW ДЛЯ КОМИТЕНТОВ
    // =========================================================================
    void createClientsListView() {
        RECT rc;
        GetClientRect(m_hTabControl, &rc);
        TabCtrl_AdjustRect(m_hTabControl, FALSE, &rc);
        POINT pt = { rc.left, rc.top };
        ClientToScreen(m_hTabControl, &pt);
        ScreenToClient(m_hWnd, &pt);
        int listX = pt.x + 10;
        int listY = pt.y + 10;
        int listWidth = rc.right - rc.left - 20;
        int listHeight = rc.bottom - rc.top - 40;
        g_logger.info(L"DirectorWindow: createClientsListView - final position: " +
            std::to_wstring(listX) + L"," + std::to_wstring(listY) +
            L", size: " + std::to_wstring(listWidth) + L"x" + std::to_wstring(listHeight));
        m_hClientsList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            listX, listY, listWidth, listHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_CLIENTS_LIST, g_hInstance, nullptr
        );
        SendMessageW(m_hClientsList, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        ListView_SetExtendedListViewStyle(m_hClientsList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        struct ColumnDef {
            const wchar_t* text;
            int width;
        };
        ColumnDef columns[] = {
            { L"ID", 50 },
            { L"Телефон", 130 },
            { L"ФИО комитента", 250 },
            { L"E-mail", 180 },
            { L"Сдано товаров (шт)", 140 },
            { L"Сдано на сумму (₽)", 150 },
            { L"Продано товаров (шт)", 150 },
            { L"Продано на сумму (₽)", 160 },
            { L"Не продано (шт)", 130 },
            { L"Брак / Низкое качество (шт)", 180 },
            { L"Статус", 120 },
            // =================================================================
            // НОВАЯ КОЛОНКА 11: Причина блокировки
            // Отображается после столбца "Статус"
            // =================================================================
            { L"Причина блокировки", 300 }
        };
        for (int i = 0; i < _countof(columns); i++) {
            col.pszText = const_cast<LPWSTR>(columns[i].text);
            col.cx = columns[i].width;
            col.iSubItem = i;
            ListView_InsertColumn(m_hClientsList, i, &col);
        }
        g_logger.info(L"DirectorWindow: clients ListView created with " +
            std::to_wstring(_countof(columns)) + L" columns");
    }
    // =========================================================================
    // СОЗДАНИЕ LISTVIEW ДЛЯ ТОВАРОВЕДОВ
    // =========================================================================
    void createWorkersListView() {
        RECT rc;
        GetClientRect(m_hTabControl, &rc);
        TabCtrl_AdjustRect(m_hTabControl, FALSE, &rc);
        POINT pt = { rc.left, rc.top };
        ClientToScreen(m_hTabControl, &pt);
        ScreenToClient(m_hWnd, &pt);
        int listX = pt.x + 10;
        int listY = pt.y + 10;
        int listWidth = rc.right - rc.left - 20;
        int listHeight = rc.bottom - rc.top - 40;
        g_logger.info(L"DirectorWindow: createWorkersListView - final position: " +
            std::to_wstring(listX) + L"," + std::to_wstring(listY) +
            L", size: " + std::to_wstring(listWidth) + L"x" + std::to_wstring(listHeight));
        m_hWorkersList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            listX, listY, listWidth, listHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_WORKERS_LIST, g_hInstance, nullptr
        );
        SendMessageW(m_hWorkersList, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        ListView_SetExtendedListViewStyle(m_hWorkersList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        struct ColumnDef {
            const wchar_t* text;
            int width;
        };
        ColumnDef columns[] = {
            { L"ID", 50 },
            { L"Телефон", 130 },
            { L"ФИО товароведа", 250 },
            { L"E-mail", 180 },
            { L"Внесено товаров (шт)", 160 },
            { L"Внесено на сумму (₽)", 170 },
            { L"Продано товаров (шт)", 160 },
            { L"Продано на сумму (₽)", 170 },
            { L"Эффективность (%)", 150 }
        };
        for (int i = 0; i < _countof(columns); i++) {
            col.pszText = const_cast<LPWSTR>(columns[i].text);
            col.cx = columns[i].width;
            col.iSubItem = i;
            ListView_InsertColumn(m_hWorkersList, i, &col);
        }
        g_logger.info(L"DirectorWindow: workers ListView created with " +
            std::to_wstring(_countof(columns)) + L" columns");
    }
    // =========================================================================
    // СОЗДАНИЕ LISTVIEW ДЛЯ НЕ ПРОДАННЫХ ТОВАРОВ
    // =========================================================================
    void createLowQualityListView() {
        RECT rc;
        GetClientRect(m_hTabControl, &rc);
        TabCtrl_AdjustRect(m_hTabControl, FALSE, &rc);
        POINT pt = { rc.left, rc.top };
        ClientToScreen(m_hTabControl, &pt);
        ScreenToClient(m_hWnd, &pt);
        int listX = pt.x + 10;
        int listY = pt.y + 10;
        int listWidth = rc.right - rc.left - 20;
        int listHeight = rc.bottom - rc.top - 40;
        g_logger.info(L"DirectorWindow: createLowQualityListView - final position: " +
            std::to_wstring(listX) + L"," + std::to_wstring(listY) +
            L", size: " + std::to_wstring(listWidth) + L"x" + std::to_wstring(listHeight));
        m_hLowQualityList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            listX, listY, listWidth, listHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_LOWQUALITY_LIST, g_hInstance, nullptr
        );
        SendMessageW(m_hLowQualityList, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        ListView_SetExtendedListViewStyle(m_hLowQualityList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        struct ColumnDef {
            const wchar_t* text;
            int width;
        };
        ColumnDef columns[] = {
            { L"№ товара", 80 },
            { L"Наименование", 250 },
            { L"Цена (₽)", 100 },
            { L"Кол-во", 70 },
            { L"Состояние", 150 },
            { L"Примечание", 200 },
            { L"Комитент", 200 },
            { L"Товаровед", 150 },
            { L"Дата добавления", 150 }
        };
        for (int i = 0; i < _countof(columns); i++) {
            col.pszText = const_cast<LPWSTR>(columns[i].text);
            col.cx = columns[i].width;
            col.iSubItem = i;
            ListView_InsertColumn(m_hLowQualityList, i, &col);
        }
        g_logger.info(L"DirectorWindow: low quality ListView created with " +
            std::to_wstring(_countof(columns)) + L" columns");
    }
    // =========================================================================
    // СОЗДАНИЕ LISTVIEW ДЛЯ ПРОСРОЧЕННЫХ ТОВАРОВ (ВКЛАДКА 3)
    // =========================================================================
    void createExpiredListView() {
        g_logger.info(L"DirectorWindow: createExpiredListView started");
        RECT rc;
        GetClientRect(m_hTabControl, &rc);
        TabCtrl_AdjustRect(m_hTabControl, FALSE, &rc);
        POINT pt = { rc.left, rc.top };
        ClientToScreen(m_hTabControl, &pt);
        ScreenToClient(m_hWnd, &pt);
        int listX = pt.x + 10;
        int listY = pt.y + 10;
        int listWidth = rc.right - rc.left - 20;
        int listHeight = rc.bottom - rc.top - 40;
        g_logger.info(L"DirectorWindow: createExpiredListView - final position: " +
            std::to_wstring(listX) + L"," + std::to_wstring(listY) +
            L", size: " + std::to_wstring(listWidth) + L"x" + std::to_wstring(listHeight));
        m_hExpiredList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            listX, listY, listWidth, listHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_DIRECTOR_EXPIRED_LIST, g_hInstance, nullptr
        );
        SendMessageW(m_hExpiredList, WM_SETFONT, (WPARAM)m_hFontNormal, TRUE);
        ListView_SetExtendedListViewStyle(m_hExpiredList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        struct ColumnDef {
            const wchar_t* text;
            int width;
        };
        ColumnDef columns[] = {
            { L"№ товара", 80 },
            { L"Наименование", 200 },
            { L"Цена (₽)", 90 },
            { L"Кол-во", 60 },
            { L"Не продано", 90 },
            { L"Комитент", 180 },
            { L"Телефон", 120 },
            { L"Приложение №", 100 },
            { L"Дата приёмки", 130 },
            { L"Срок истёк", 130 },
            { L"Дата выбытия", 130 }
        };
        for (int i = 0; i < _countof(columns); i++) {
            col.pszText = const_cast<LPWSTR>(columns[i].text);
            col.cx = columns[i].width;
            col.iSubItem = i;
            ListView_InsertColumn(m_hExpiredList, i, &col);
        }
        g_logger.info(L"DirectorWindow: expired ListView created with " +
            std::to_wstring(_countof(columns)) + L" columns");
    }
    // =========================================================================
    // ПОКАЗАТЬ ВКЛАДКУ
    // =========================================================================
    void showTab(int tabIndex) {
        g_logger.info(L"DirectorWindow: showTab(" + std::to_wstring(tabIndex) + L")");
        ShowWindow(m_hClientsList, SW_HIDE);
        ShowWindow(m_hWorkersList, SW_HIDE);
        ShowWindow(m_hLowQualityList, SW_HIDE);
        ShowWindow(m_hExpiredList, SW_HIDE);
        switch (tabIndex) {
        case 0: ShowWindow(m_hClientsList, SW_SHOW); break;
        case 1: ShowWindow(m_hWorkersList, SW_SHOW); break;
        case 2: ShowWindow(m_hLowQualityList, SW_SHOW); break;
        case 3: ShowWindow(m_hExpiredList, SW_SHOW); break;
        }
    }
    // =========================================================================
    // АСИНХРОННАЯ ЗАГРУЗКА СТАТИСТИКИ С СЕРВЕРА
    // =========================================================================
    void loadDirectorStats() {
        if (m_isLoading.exchange(true)) {
            g_logger.warning(L"DirectorWindow: loadDirectorStats already in progress");
            return;
        }
        g_logger.info(L"DirectorWindow: loadDirectorStats started");
        SetWindowTextW(m_hStatusLabel, L"Загрузка данных с сервера...");
        EnableWindow(m_hRefreshBtn, FALSE);
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            g_logger.error(L"DirectorWindow: auth token is empty");
            PostMessageW(m_hWnd, WM_DIRECTOR_ERROR, 0, (LPARAM)L"Ошибка авторизации. Токен отсутствует.");
            return;
        }
        std::thread([this, authToken]() {
            g_logger.info(L"DirectorWindow: background thread for stats loading started");
            auto response = g_httpsClient.get(L"/api/v1/director/stats", authToken);
            DirectorStats stats;
            if (response && !response->contains("error")) {
                stats.clients = response->value("clients", json::array());
                stats.workers = response->value("workers", json::array());
                stats.lowQuality = response->value("low_quality_items", json::array());
                stats.expired = response->value("expired_items", json::array());
                stats.summary = response->value("summary", json::object());
                stats.loaded = true;
                g_logger.info(L"DirectorWindow: stats loaded successfully - clients=" +
                    std::to_wstring(stats.clients.size()) +
                    L", workers=" + std::to_wstring(stats.workers.size()) +
                    L", lowQuality=" + std::to_wstring(stats.lowQuality.size()) +
                    L", expired=" + std::to_wstring(stats.expired.size()));
            }
            else {
                stats.loaded = false;
                g_logger.error(L"DirectorWindow: failed to load stats from server");
            }
            {
                std::lock_guard<std::mutex> lock(m_dataMutex);
                m_stats = std::move(stats);
            }
            PostMessageW(m_hWnd, WM_DIRECTOR_DATA_LOADED, 0, 0);
            g_logger.info(L"DirectorWindow: background thread completed");
            }).detach();
    }
    // =========================================================================
    // ОБНОВЛЕНИЕ UI ПОСЛЕ ЗАГРУЗКИ ДАННЫХ
    // =========================================================================
    void updateUI() {
        g_logger.info(L"DirectorWindow: updateUI started");
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (!m_stats.loaded) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка загрузки данных");
            m_isLoading = false;
            EnableWindow(m_hRefreshBtn, TRUE);
            return;
        }
        // =====================================================================
        // ОБНОВЛЯЕМ СПИСОК КОМИТЕНТОВ
        // =====================================================================
        ListView_DeleteAllItems(m_hClientsList);
        for (size_t i = 0; i < m_stats.clients.size(); i++) {
            const auto& client = m_stats.clients[i];
            if (!m_searchFilter.empty()) {
                std::wstring phone = utf8_to_wstring(client.value("phone", ""));
                std::wstring fullName = utf8_to_wstring(client.value("full_name", ""));
                if (phone.find(m_searchFilter) == std::wstring::npos &&
                    fullName.find(m_searchFilter) == std::wstring::npos) {
                    continue;
                }
            }
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = ListView_GetItemCount(m_hClientsList);
            item.iSubItem = 0;
            item.lParam = client.value("id", 0);
            std::wstring idStr = std::to_wstring(client.value("id", 0));
            item.pszText = const_cast<LPWSTR>(idStr.c_str());
            int index = ListView_InsertItem(m_hClientsList, &item);
            // Колонка 1: Телефон
            std::wstring phone = utf8_to_wstring(client.value("phone", ""));
            ListView_SetItemText(m_hClientsList, index, 1, const_cast<LPWSTR>(phone.c_str()));
            // Колонка 2: ФИО
            std::wstring fullName = utf8_to_wstring(client.value("full_name", ""));
            ListView_SetItemText(m_hClientsList, index, 2, const_cast<LPWSTR>(fullName.c_str()));
            // Колонка 3: E-mail
            std::wstring email = utf8_to_wstring(client.value("email", ""));
            ListView_SetItemText(m_hClientsList, index, 3, const_cast<LPWSTR>(email.c_str()));
            // Колонка 4: Сдано товаров (шт)
            std::wstring totalItems = std::to_wstring(client.value("total_items_count", 0));
            ListView_SetItemText(m_hClientsList, index, 4, const_cast<LPWSTR>(totalItems.c_str()));
            // Колонка 5: Сдано на сумму (₽)
            wchar_t totalValueBuf[50];
            swprintf_s(totalValueBuf, L"%.2f", client.value("total_items_value", 0.0));
            ListView_SetItemText(m_hClientsList, index, 5, totalValueBuf);
            // Колонка 6: Продано товаров (шт)
            std::wstring soldItems = std::to_wstring(client.value("sold_items_count", 0));
            ListView_SetItemText(m_hClientsList, index, 6, const_cast<LPWSTR>(soldItems.c_str()));
            // Колонка 7: Продано на сумму (₽)
            wchar_t soldValueBuf[50];
            swprintf_s(soldValueBuf, L"%.2f", client.value("sold_items_value", 0.0));
            ListView_SetItemText(m_hClientsList, index, 7, soldValueBuf);
            // Колонка 8: Не продано (шт)
            std::wstring unsoldItems = std::to_wstring(client.value("unsold_count", 0));
            ListView_SetItemText(m_hClientsList, index, 8, const_cast<LPWSTR>(unsoldItems.c_str()));
            // Колонка 9: Брак / Низкое качество (шт)
            std::wstring lowQuality = std::to_wstring(client.value("low_quality_count", 0));
            ListView_SetItemText(m_hClientsList, index, 9, const_cast<LPWSTR>(lowQuality.c_str()));
            // Колонка 10: Статус
            bool isBlocked = client.value("is_blocked", false);
            std::wstring status = isBlocked ? L"🚫 ЗАБЛОКИРОВАН" : L"✓ Активен";
            ListView_SetItemText(m_hClientsList, index, 10, const_cast<LPWSTR>(status.c_str()));
            // =================================================================
            // НОВАЯ КОЛОНКА 11: Причина блокировки
            // Отображается после столбца "Статус"
            // Если комитент не заблокирован — пустая строка
            // =================================================================
            std::wstring blockReason = utf8_to_wstring(client.value("block_reason", ""));
            ListView_SetItemText(m_hClientsList, index, 11, const_cast<LPWSTR>(blockReason.c_str()));
        }
        g_logger.info(L"DirectorWindow: clients list updated, count=" +
            std::to_wstring(ListView_GetItemCount(m_hClientsList)));
        // =====================================================================
        // ОБНОВЛЯЕМ СПИСОК ТОВАРОВЕДОВ
        // =====================================================================
        ListView_DeleteAllItems(m_hWorkersList);
        for (size_t i = 0; i < m_stats.workers.size(); i++) {
            const auto& worker = m_stats.workers[i];
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(m_hWorkersList);
            item.iSubItem = 0;
            std::wstring idStr = std::to_wstring(worker.value("id", 0));
            item.pszText = const_cast<LPWSTR>(idStr.c_str());
            int index = ListView_InsertItem(m_hWorkersList, &item);
            std::wstring phone = utf8_to_wstring(worker.value("phone", ""));
            ListView_SetItemText(m_hWorkersList, index, 1, const_cast<LPWSTR>(phone.c_str()));
            std::wstring fullName = utf8_to_wstring(worker.value("full_name", ""));
            ListView_SetItemText(m_hWorkersList, index, 2, const_cast<LPWSTR>(fullName.c_str()));
            std::wstring email = utf8_to_wstring(worker.value("email", ""));
            ListView_SetItemText(m_hWorkersList, index, 3, const_cast<LPWSTR>(email.c_str()));
            std::wstring enteredCount = std::to_wstring(worker.value("total_entered_count", 0));
            ListView_SetItemText(m_hWorkersList, index, 4, const_cast<LPWSTR>(enteredCount.c_str()));
            wchar_t enteredValueBuf[50];
            swprintf_s(enteredValueBuf, L"%.2f", worker.value("total_entered_value", 0.0));
            ListView_SetItemText(m_hWorkersList, index, 5, enteredValueBuf);
            std::wstring soldCount = std::to_wstring(worker.value("total_sold_count", 0));
            ListView_SetItemText(m_hWorkersList, index, 6, const_cast<LPWSTR>(soldCount.c_str()));
            wchar_t soldValueBuf[50];
            swprintf_s(soldValueBuf, L"%.2f", worker.value("total_sold_value", 0.0));
            ListView_SetItemText(m_hWorkersList, index, 7, soldValueBuf);
            wchar_t efficiencyBuf[20];
            swprintf_s(efficiencyBuf, L"%.1f%%", worker.value("efficiency_percent", 0.0));
            ListView_SetItemText(m_hWorkersList, index, 8, efficiencyBuf);
        }
        g_logger.info(L"DirectorWindow: workers list updated, count=" +
            std::to_wstring(ListView_GetItemCount(m_hWorkersList)));
        // =====================================================================
        // ОБНОВЛЯЕМ СПИСОК НЕ ПРОДАННЫХ ТОВАРОВ
        // =====================================================================
        ListView_DeleteAllItems(m_hLowQualityList);
        for (size_t i = 0; i < m_stats.lowQuality.size(); i++) {
            const auto& itemData = m_stats.lowQuality[i];
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(m_hLowQualityList);
            item.iSubItem = 0;
            std::wstring itemNumber = std::to_wstring(itemData.value("item_number", 0));
            item.pszText = const_cast<LPWSTR>(itemNumber.c_str());
            int index = ListView_InsertItem(m_hLowQualityList, &item);
            std::wstring description = utf8_to_wstring(itemData.value("description", ""));
            ListView_SetItemText(m_hLowQualityList, index, 1, const_cast<LPWSTR>(description.c_str()));
            wchar_t priceBuf[50];
            swprintf_s(priceBuf, L"%.2f", itemData.value("estimated_price", 0.0));
            ListView_SetItemText(m_hLowQualityList, index, 2, priceBuf);
            std::wstring quantity = std::to_wstring(itemData.value("quantity", 0));
            ListView_SetItemText(m_hLowQualityList, index, 3, const_cast<LPWSTR>(quantity.c_str()));
            std::wstring condition = utf8_to_wstring(itemData.value("condition", ""));
            ListView_SetItemText(m_hLowQualityList, index, 4, const_cast<LPWSTR>(condition.c_str()));
            std::wstring note = utf8_to_wstring(itemData.value("note", ""));
            ListView_SetItemText(m_hLowQualityList, index, 5, const_cast<LPWSTR>(note.c_str()));
            std::wstring clientName = utf8_to_wstring(itemData.value("client_name", ""));
            ListView_SetItemText(m_hLowQualityList, index, 6, const_cast<LPWSTR>(clientName.c_str()));
            std::wstring workerName = utf8_to_wstring(itemData.value("worker_name", ""));
            ListView_SetItemText(m_hLowQualityList, index, 7, const_cast<LPWSTR>(workerName.c_str()));
            int64_t createdAt = itemData.value("created_at", 0);
            std::wstring dateStr = formatTimestamp(createdAt);
            ListView_SetItemText(m_hLowQualityList, index, 8, const_cast<LPWSTR>(dateStr.c_str()));
        }
        g_logger.info(L"DirectorWindow: low quality list updated, count=" +
            std::to_wstring(ListView_GetItemCount(m_hLowQualityList)));
        // =====================================================================
        // ОБНОВЛЯЕМ СПИСОК ПРОСРОЧЕННЫХ ТОВАРОВ (ВКЛАДКА 3)
        // =====================================================================
        ListView_DeleteAllItems(m_hExpiredList);
        for (size_t i = 0; i < m_stats.expired.size(); i++) {
            const auto& itemData = m_stats.expired[i];
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(m_hExpiredList);
            item.iSubItem = 0;
            std::wstring itemNumber = std::to_wstring(itemData.value("item_number", 0));
            item.pszText = const_cast<LPWSTR>(itemNumber.c_str());
            int index = ListView_InsertItem(m_hExpiredList, &item);
            std::wstring description = utf8_to_wstring(itemData.value("description", ""));
            ListView_SetItemText(m_hExpiredList, index, 1, const_cast<LPWSTR>(description.c_str()));
            wchar_t priceBuf[50];
            swprintf_s(priceBuf, L"%.2f", itemData.value("estimated_price", 0.0));
            ListView_SetItemText(m_hExpiredList, index, 2, priceBuf);
            std::wstring quantity = std::to_wstring(itemData.value("quantity", 0));
            ListView_SetItemText(m_hExpiredList, index, 3, const_cast<LPWSTR>(quantity.c_str()));
            std::wstring unsoldQty = std::to_wstring(itemData.value("unsold_quantity", 0));
            ListView_SetItemText(m_hExpiredList, index, 4, const_cast<LPWSTR>(unsoldQty.c_str()));
            std::wstring clientName = utf8_to_wstring(itemData.value("client_name", ""));
            ListView_SetItemText(m_hExpiredList, index, 5, const_cast<LPWSTR>(clientName.c_str()));
            std::wstring clientPhone = utf8_to_wstring(itemData.value("client_phone", ""));
            ListView_SetItemText(m_hExpiredList, index, 6, const_cast<LPWSTR>(clientPhone.c_str()));
            std::wstring appendixNum = std::to_wstring(itemData.value("appendix_number", 0));
            ListView_SetItemText(m_hExpiredList, index, 7, const_cast<LPWSTR>(appendixNum.c_str()));
            int64_t createdAt = itemData.value("created_at", 0);
            std::wstring createdDateStr = formatTimestamp(createdAt);
            ListView_SetItemText(m_hExpiredList, index, 8, const_cast<LPWSTR>(createdDateStr.c_str()));
            int64_t validUntil = itemData.value("valid_until", 0);
            std::wstring validUntilStr = formatTimestamp(validUntil);
            ListView_SetItemText(m_hExpiredList, index, 9, const_cast<LPWSTR>(validUntilStr.c_str()));
            int64_t expiredAt = itemData.value("expired_at", 0);
            std::wstring expiredAtStr = formatTimestamp(expiredAt);
            ListView_SetItemText(m_hExpiredList, index, 10, const_cast<LPWSTR>(expiredAtStr.c_str()));
        }
        g_logger.info(L"DirectorWindow: expired list updated, count=" +
            std::to_wstring(ListView_GetItemCount(m_hExpiredList)));
        // =====================================================================
        // ОБНОВЛЯЕМ СВОДКУ
        // =====================================================================
        if (m_stats.summary.is_object()) {
            int totalItems = m_stats.summary.value("total_items", 0);
            double totalValue = m_stats.summary.value("total_value", 0.0);
            int totalSold = m_stats.summary.value("total_sold", 0);
            double totalSoldValue = m_stats.summary.value("total_sold_value", 0.0);
            int totalLowQuality = m_stats.summary.value("total_low_quality", 0);
            int totalClients = m_stats.summary.value("total_clients", 0);
            int blockedClients = m_stats.summary.value("blocked_clients_count", 0);
            int expiredCount = m_stats.summary.value("expired_items_count", 0);
            wchar_t summaryBuf[700];
            swprintf_s(summaryBuf,
                L"📊 ИТОГО: Комитентов: %d | Товаров сдано: %d шт на %.2f ₽ | "
                L"Продано: %d шт на %.2f ₽ | Брак: %d шт | "
                L"Выбыло по сроку (15 дней): %d шт | Заблокировано: %d",
                totalClients, totalItems, totalValue,
                totalSold, totalSoldValue, totalLowQuality,
                expiredCount, blockedClients);
            SetWindowTextW(m_hSummaryLabel, summaryBuf);
            g_logger.info(L"DirectorWindow: summary updated - " + std::wstring(summaryBuf));
        }
        // =====================================================================
        // ЗАВЕРШАЕМ ЗАГРУЗКУ
        // =====================================================================
        m_isLoading = false;
        EnableWindow(m_hRefreshBtn, TRUE);
        std::wstring statusTimeText = L"Данные обновлены: " + getCurrentTimeString();
        SetWindowTextW(m_hStatusLabel, statusTimeText.c_str());
        g_logger.info(L"DirectorWindow: updateUI completed");
    }
    // =========================================================================
    // АСИНХРОННАЯ БЛОКИРОВКА/РАЗБЛОКИРОВКА КЛИЕНТА
    // =========================================================================
    // НОВОЕ: добавлен параметр blockReason для передачи причины блокировки
    void blockClient(int clientId, bool blocked, const std::wstring& blockReason = L"") {
        if (m_isBlocking.exchange(true)) {
            g_logger.warning(L"DirectorWindow: blockClient already in progress");
            return;
        }
        g_logger.info(L"DirectorWindow: blockClient started - clientId=" +
            std::to_wstring(clientId) + L", blocked=" + std::to_wstring(blocked) +
            L", blockReason='" + blockReason + L"'");
        SetWindowTextW(m_hStatusLabel, blocked ?
            L"Блокировка клиента..." : L"Разблокировка клиента...");
        EnableWindow(m_hBlockBtn, FALSE);
        EnableWindow(m_hUnblockBtn, FALSE);
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            g_logger.error(L"DirectorWindow: auth token is empty for blockClient");
            m_isBlocking = false;
            EnableWindow(m_hBlockBtn, TRUE);
            EnableWindow(m_hUnblockBtn, TRUE);
            return;
        }
        std::thread([this, clientId, blocked, blockReason, authToken]() {
            g_logger.info(L"DirectorWindow: background thread for blockClient started");
            json request;
            request["client_id"] = clientId;
            request["blocked"] = blocked;
            // =================================================================
            // НОВОЕ: передаём причину блокировки на сервер
            // =================================================================
            request["block_reason"] = wstring_to_utf8(blockReason);
            auto response = g_httpsClient.post(
                L"/api/v1/director/block_client", request, authToken);
            BlockResult result;
            result.clientId = clientId;
            result.blocked = blocked;
            result.blockReason = blockReason;
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                result.success = true;
                result.message = blocked ?
                    L"Клиент успешно заблокирован" :
                    L"Клиент успешно разблокирован";
                g_logger.info(L"DirectorWindow: blockClient SUCCESS - clientId=" +
                    std::to_wstring(clientId));
            }
            else {
                result.success = false;
                result.message = L"Ошибка блокировки/разблокировки клиента";
                if (response && response->contains("error")) {
                    result.message += L": " +
                        utf8_to_wstring((*response)["error"].get<std::string>());
                }
                g_logger.error(L"DirectorWindow: blockClient FAILED - clientId=" +
                    std::to_wstring(clientId));
            }
            BlockResult* resultPtr = new BlockResult(result);
            PostMessageW(m_hWnd, WM_DIRECTOR_BLOCK_RESULT, 0, (LPARAM)resultPtr);
            g_logger.info(L"DirectorWindow: background thread for blockClient completed");
            }).detach();
    }
    // =========================================================================
    // ОБРАБОТКА ВЫБОРА КЛИЕНТА В СПИСКЕ
    // =========================================================================
    void onClientSelected() {
        int selectedIndex = ListView_GetNextItem(m_hClientsList, -1, LVNI_SELECTED);
        if (selectedIndex == -1) {
            m_selectedClientId = 0;
            m_selectedClientPhone.clear();
            EnableWindow(m_hBlockBtn, FALSE);
            EnableWindow(m_hUnblockBtn, FALSE);
            return;
        }
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = selectedIndex;
        item.iSubItem = 0;
        ListView_GetItem(m_hClientsList, &item);
        m_selectedClientId = static_cast<int>(item.lParam);
        wchar_t phoneBuf[50];
        ListView_GetItemText(m_hClientsList, selectedIndex, 1, phoneBuf, 50);
        m_selectedClientPhone = phoneBuf;
        wchar_t statusBuf[50];
        ListView_GetItemText(m_hClientsList, selectedIndex, 10, statusBuf, 50);
        bool isBlocked = (std::wstring(statusBuf).find(L"ЗАБЛОКИРОВАН") != std::wstring::npos);
        EnableWindow(m_hBlockBtn, !isBlocked);
        EnableWindow(m_hUnblockBtn, isBlocked);
        g_logger.info(L"DirectorWindow: client selected - id=" +
            std::to_wstring(m_selectedClientId) +
            L", phone=" + m_selectedClientPhone +
            L", blocked=" + std::to_wstring(isBlocked));
    }
    // =========================================================================
    // ФОРМАТИРОВАНИЕ TIMESTAMP В СТРОКУ
    // =========================================================================
    std::wstring formatTimestamp(int64_t timestamp) {
        if (timestamp == 0) return L"-";
        time_t time = static_cast<time_t>(timestamp);
        struct tm tm_buf;
        if (localtime_s(&tm_buf, &time) != 0) {
            return L"-";
        }
        wchar_t buf[32];
        wcsftime(buf, 32, L"%Y-%m-%d %H:%M", &tm_buf);
        return std::wstring(buf);
    }
    // =========================================================================
    // ПОЛУЧЕНИЕ ТЕКУЩЕГО ВРЕМЕНИ В ВИДЕ СТРОКИ
    // =========================================================================
    std::wstring getCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        time_t time = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        if (localtime_s(&tm_buf, &time) != 0) {
            return L"";
        }
        wchar_t buf[32];
        wcsftime(buf, 32, L"%H:%M:%S", &tm_buf);
        return std::wstring(buf);
    }
    // =========================================================================
    // ПОИСК
    // =========================================================================
    void onSearch() {
        wchar_t searchBuf[100];
        GetWindowTextW(m_hSearchEdit, searchBuf, 100);
        m_searchFilter = searchBuf;
        g_logger.info(L"DirectorWindow: search filter set to: " + m_searchFilter);
        updateUI();
    }
    // =========================================================================
    // ОКОННАЯ ПРОЦЕДУРА
    // =========================================================================
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DirectorWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<DirectorWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createFontsAndBrushes();
            pThis->createControls();
            pThis->loadDirectorStats();
            pThis->m_running = true;
            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(DirectorConfig::AUTO_REFRESH_INTERVAL_MS));
                    if (pThis->m_running && !pThis->m_isLoading) {
                        g_logger.info(L"DirectorWindow: auto-refresh triggered");
                        pThis->loadDirectorStats();
                    }
                }
                });
            return 0;
        }
        pThis = reinterpret_cast<DirectorWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);
        switch (msg) {
        case WM_DIRECTOR_DATA_LOADED:
            g_logger.info(L"DirectorWindow: WM_DIRECTOR_DATA_LOADED received");
            pThis->updateUI();
            return 0;
        case WM_DIRECTOR_BLOCK_RESULT: {
            BlockResult* result = reinterpret_cast<BlockResult*>(lParam);
            if (result->success) {
                // =================================================================
                // НОВОЕ: показываем причину блокировки в сообщении об успехе
                // =================================================================
                std::wstring msgText = result->message;
                if (result->blocked && !result->blockReason.empty()) {
                    msgText += L"\n\nПричина: " + result->blockReason;
                }
                MessageBoxW(pThis->m_hWnd, msgText.c_str(),
                    L"Успех", MB_OK | MB_ICONINFORMATION);
                pThis->loadDirectorStats();
            }
            else {
                MessageBoxW(pThis->m_hWnd, result->message.c_str(),
                    L"Ошибка", MB_OK | MB_ICONERROR);
            }
            pThis->m_isBlocking = false;
            EnableWindow(pThis->m_hBlockBtn, TRUE);
            EnableWindow(pThis->m_hUnblockBtn, TRUE);
            delete result;
            return 0;
        }
        case WM_DIRECTOR_ERROR: {
            const wchar_t* errorMsg = reinterpret_cast<const wchar_t*>(lParam);
            SetWindowTextW(pThis->m_hStatusLabel, errorMsg);
            pThis->m_isLoading = false;
            EnableWindow(pThis->m_hRefreshBtn, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                switch (id) {
                case IDC_DIRECTOR_REFRESH_BTN:
                    g_logger.info(L"DirectorWindow: Refresh button clicked");
                    pThis->loadDirectorStats();
                    break;
                case IDC_DIRECTOR_BLOCK_BTN:
                    g_logger.info(L"DirectorWindow: Block button clicked");
                    if (pThis->m_selectedClientId > 0) {
                        // =================================================================
                        // НОВОЕ: показываем диалог ввода причины блокировки
                        // =================================================================
                        std::wstring reason;
                        BlockReasonDialog dialog;
                        if (dialog.show(pThis->m_hWnd, reason)) {
                            // Пользователь подтвердил блокировку с причиной
                            g_logger.info(L"DirectorWindow: block confirmed with reason: " + reason);
                            pThis->blockClient(pThis->m_selectedClientId, true, reason);
                        }
                        else {
                            g_logger.info(L"DirectorWindow: block cancelled by user");
                        }
                    }
                    break;
                case IDC_DIRECTOR_UNBLOCK_BTN:
                    g_logger.info(L"DirectorWindow: Unblock button clicked");
                    if (pThis->m_selectedClientId > 0) {
                        int result = MessageBoxW(pThis->m_hWnd,
                            (L"Вы уверены, что хотите разблокировать клиента\n" +
                                pThis->m_selectedClientPhone + L"?\n\n" +
                                L"Причина блокировки будет удалена.").c_str(),
                            L"Подтверждение разблокировки",
                            MB_YESNO | MB_ICONQUESTION);
                        if (result == IDYES) {
                            // При разблокировке причина очищается на сервере
                            pThis->blockClient(pThis->m_selectedClientId, false, L"");
                        }
                    }
                    break;
                case IDC_DIRECTOR_SEARCH_BTN:
                    g_logger.info(L"DirectorWindow: Search button clicked");
                    pThis->onSearch();
                    break;
                case IDC_DIRECTOR_CLOSE_BTN:
                    g_logger.info(L"DirectorWindow: Close button clicked");
                    DestroyWindow(hWnd);
                    break;
                }
            }
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
            // Обработка NM_CUSTOMDRAW для подсветки заблокированных комитентов
            if (nmhdr->hwndFrom == pThis->m_hClientsList &&
                nmhdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                switch (lplvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int clientId = static_cast<int>(lplvcd->nmcd.lItemlParam);
                    bool isBlocked = false;
                    {
                        std::lock_guard<std::mutex> lock(pThis->m_dataMutex);
                        if (pThis->m_stats.loaded && pThis->m_stats.clients.is_array()) {
                            for (const auto& client : pThis->m_stats.clients) {
                                if (client.value("id", 0) == clientId) {
                                    isBlocked = client.value("is_blocked", false);
                                    break;
                                }
                            }
                        }
                    }
                    if (isBlocked) {
                        lplvcd->clrText = DirectorConfig::DANGER_COLOR;
                        lplvcd->clrTextBk = GetSysColor(COLOR_WINDOW);
                    }
                    else {
                        lplvcd->clrText = RGB(0, 0, 0);
                        lplvcd->clrTextBk = GetSysColor(COLOR_WINDOW);
                    }
                    return CDRF_NEWFONT;
                }
                }
                return CDRF_DODEFAULT;
            }
            // Tab Control - смена вкладки
            if (nmhdr->hwndFrom == pThis->m_hTabControl &&
                nmhdr->code == TCN_SELCHANGE) {
                int tabIndex = TabCtrl_GetCurSel(pThis->m_hTabControl);
                pThis->showTab(tabIndex);
                g_logger.info(L"DirectorWindow: tab changed to " +
                    std::to_wstring(tabIndex));
            }
            // ListView - выбор элемента
            if (nmhdr->hwndFrom == pThis->m_hClientsList &&
                nmhdr->code == LVN_ITEMCHANGED) {
                pThis->onClientSelected();
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)pThis->m_hBackgroundBrush;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        case WM_CLOSE:
            g_logger.info(L"DirectorWindow: WM_CLOSE received");
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            g_logger.info(L"DirectorWindow: WM_DESTROY, cleaning up");
            pThis->m_running = false;
            if (pThis->m_refreshThread.joinable()) {
                pThis->m_refreshThread.join();
            }
            pThis->releaseFontsAndBrushes();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
public:
    // =========================================================================
    // КОНСТРУКТОР
    // =========================================================================
    DirectorWindow()
        : m_hWnd(nullptr)
        , m_hTabControl(nullptr)
        , m_hClientsList(nullptr)
        , m_hWorkersList(nullptr)
        , m_hLowQualityList(nullptr)
        , m_hExpiredList(nullptr)
        , m_hSummaryLabel(nullptr)
        , m_hBlockBtn(nullptr)
        , m_hUnblockBtn(nullptr)
        , m_hRefreshBtn(nullptr)
        , m_hCloseBtn(nullptr)
        , m_hStatusLabel(nullptr)
        , m_hSearchEdit(nullptr)
        , m_hSearchBtn(nullptr)
        , m_hFontTitle(nullptr)
        , m_hFontNormal(nullptr)
        , m_hFontSmall(nullptr)
        , m_hFontButton(nullptr)
        , m_hBackgroundBrush(nullptr)
        , m_isLoading(false)
        , m_isBlocking(false)
        , m_selectedClientId(0)
        , m_running(false)
    {
        g_logger.info(L"DirectorWindow: constructor");
    }
    // =========================================================================
    // ДЕСТРУКТОР
    // =========================================================================
    ~DirectorWindow() {
        g_logger.info(L"DirectorWindow: destructor");
    }
    // =========================================================================
    // ПОКАЗАТЬ ОКНО (МОДАЛЬНО)
    // =========================================================================
    void show() {
        g_logger.info(L"DirectorWindow: show() called");
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = g_hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszClassName = CLASS_NAME;
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            if (!RegisterClassExW(&wcex)) {
                g_logger.error(L"DirectorWindow: RegisterClassExW failed");
                return;
            }
            classRegistered = true;
            g_logger.info(L"DirectorWindow: class registered");
        }
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenW - DirectorConfig::WINDOW_WIDTH) / 2;
        int y = (screenH - DirectorConfig::WINDOW_HEIGHT) / 2;
        m_hWnd = CreateWindowExW(
            WS_EX_WINDOWEDGE,
            CLASS_NAME,
            L"Панель управления директора магазина ДОБРО",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
            x, y,
            DirectorConfig::WINDOW_WIDTH,
            DirectorConfig::WINDOW_HEIGHT,
            nullptr, nullptr, g_hInstance, this
        );
        if (!m_hWnd) {
            g_logger.error(L"DirectorWindow: CreateWindowExW failed");
            return;
        }
        g_logger.info(L"DirectorWindow: window created successfully");
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        g_logger.info(L"DirectorWindow: window shown, entering message loop");
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) break;
        }
        g_logger.info(L"DirectorWindow: message loop ended, window closed");
    }
};
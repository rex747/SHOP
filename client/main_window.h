// main_window.h
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "config.h"
#include "queue_manager.h"
#include "registration_dialog.h"
#include "registration_window.h"
#include "logger.h"
#include "auth_manager.h"
#include "receipt_printer.h"


extern QueueManager g_queueManager;
extern AuthManager g_authManager;
extern Logger g_logger;
extern HINSTANCE g_hInstance;
extern HWND g_hMainWnd;
extern ReceiptPrinter g_printer;

static HBRUSH g_hBrushBtn = nullptr;
static HBRUSH g_hBrushBackBtn = nullptr;
static HBRUSH g_hBrushWindow = nullptr;
static HFONT g_hFontTitle = nullptr;
static HFONT g_hFontButton = nullptr;
static HWND g_hCloseBtn = nullptr;

// Control IDs
#define ID_BTN_REGISTER 1001
#define ID_BTN_SUBMIT_ITEMS 1002
#define ID_BTN_MY_SALES 1003
#define ID_BTN_ADDRESSES 1004
#define ID_BTN_CONSIGNOR_NUMBER 1005
#define ID_BTN_OTHER_SERVICES 1006
#define ID_BTN_BACK 1007
#define ID_BTN_CLOSE 1008

#define ID_BTN_GENERAL_QUEUE 2001
#define ID_BTN_FIRST_TIME 2002
#define ID_BTN_EXTRA_20 2003
#define ID_BTN_TRUST 2004
#define ID_BTN_PAID 2005
#define ID_BTN_EXPENSIVE 2006

#define ID_EDIT_PHONE 3001
#define ID_EDIT_CONSIGNOR 3002

// Window states
enum class WindowState {
    MAIN_MENU,
    SUBMIT_MENU,
    REGISTRATION,
    CONSIGNOR_LOOKUP,
    TICKET_ISSUED
};

class MainWindow {
private:
    WindowState m_currentState;
    std::vector<HWND> m_buttons;
    HWND m_currentTicketLabel;
    QueueTicket m_currentTicket;

    // Очистка всех дочерних окон главного окна
    void clearWindow() {
        std::vector<HWND> children;
        EnumChildWindows(m_hWnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* vec = reinterpret_cast<std::vector<HWND>*>(lParam);
            vec->push_back(hwnd);
            return TRUE;
            }, reinterpret_cast<LPARAM>(&children));

        for (HWND child : children) {
            DestroyWindow(child);
        }
        m_buttons.clear();
        g_hCloseBtn = nullptr;
        m_currentTicketLabel = nullptr;
    }

    // Создание главного меню (кнопка "Регистрация" первая)
    void createMainMenu() {
        clearWindow();
        m_currentState = WindowState::MAIN_MENU;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int startY = 300;
        int btnWidth = 400;
        int btnHeight = Config::BUTTON_HEIGHT;
        int centerX = (screenWidth - btnWidth) / 2;

        // Заголовок
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"ДОБРО КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 30, btnWidth, 220,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Кнопка закрытия в правом верхнем углу
        int closeBtnSize = 20;
        g_hCloseBtn = CreateWindowExW(0, L"BUTTON", L"✕",
            WS_CHILD | BS_PUSHBUTTON,
            screenWidth - closeBtnSize - 20, 20, closeBtnSize, closeBtnSize,
            m_hWnd, (HMENU)ID_BTN_CLOSE, g_hInstance, nullptr);
        m_buttons.push_back(g_hCloseBtn);

        // Кнопки главного меню – теперь "Регистрация" первая
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Регистрация",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_REGISTER, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Сдать товар",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + btnHeight + 10, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_SUBMIT_ITEMS, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Мои продажи",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_MY_SALES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Адреса магазинов",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_ADDRESSES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Узнать номер комитента",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_CONSIGNOR_NUMBER, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Другие услуги",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 5, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_OTHER_SERVICES, g_hInstance, nullptr));

        styleButtons();
    }

    // Создание меню выбора очереди (исправлено: заголовок не перекрывается, убрано лишнее расстояние)
    void createSubmitMenu() {
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int btnWidth = 500;
        int btnHeight = 100;
        int backBtnHeight = 60;
        int gap = 5;
        int backGap = 25;
        int centerX = (screenWidth - btnWidth) / 2;

        // Название магазина – увеличенная высота для комфортного отображения шрифта 48pt
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"ДОБРО КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 20, btnWidth, 100,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Подзаголовок – размещён с отступом 10 пикселей после заголовка
        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"Выберите тип очереди",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 20 + 100 + 10, btnWidth, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Стартовая позиция кнопок – сразу после подзаголовка с отступом 20 пикселей
        int startY = 20 + 100 + 10 + 50 + 20;

        // Кнопки выбора очереди
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ОБЩАЯ ОЧЕРЕДЬ\n(до 20 товаров)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_GENERAL_QUEUE, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"Я ПЕРВЫЙ РАЗ\n(оформление договора)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + btnHeight + gap, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_FIRST_TIME, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"+20 ПОЗИЦИЙ\n(дополнительно к базовым)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_EXTRA_20, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"НА ДОВЕРИИ\n(без присутствия)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_TRUST, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ПЛАТНЫЙ ПРИЕМ\n(200 руб., без очереди)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_PAID, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ДОРОГОЙ ТОВАР\n(>5000 руб.)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 5, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_EXPENSIVE, g_hInstance, nullptr));

        // Кнопка "Назад"
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"НАЗАД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + gap) * 5 + btnHeight + backGap, btnWidth, backBtnHeight,
            m_hWnd, (HMENU)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
    }

    void styleButtons() {
        for (HWND btn : m_buttons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
    }

    void handleQueueSelection(QueueType type) {
        if (!g_authManager.isAuthenticated()) {
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) == IDOK) {
                proceedWithQueue(type);
            }
        }
        else {
            proceedWithQueue(type);
        }
    }

    void proceedWithQueue(QueueType type) {
        auto clientOpt = LocalDB::getClientByPhone(g_authManager.getPhone());
        if (!clientOpt) {
            g_logger.error(L"Client not found in local DB");
            return;
        }

        auto ticketOpt = g_queueManager.getTicket(
            clientOpt->id, type,
            (type == QueueType::PAID || type == QueueType::GENERAL) ?
            Config::MAX_ITEMS_GENERAL_QUEUE : 1,
            g_authManager.getAuthToken()
        );

        if (ticketOpt) {
            m_currentTicket = ticketOpt.value();
            showTicketIssued();
            g_printer.printTicket(m_currentTicket,
                clientOpt->name,
                clientOpt->phone);
        }
    }

    void showTicketIssued() {
        clearWindow();
        m_currentState = WindowState::TICKET_ISSUED;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int centerX = screenWidth / 2;

        std::wstringstream ss;
        ss << L"ВАШ ТАЛОН: " << m_currentTicket.ticketNumber << L"\n";
        ss << L"Позиция в очереди: " << m_currentTicket.position << L"\n";
        ss << L"Окно: " << m_currentTicket.windowNumber << L"\n";

        if (m_currentTicket.estimatedWaitTime > 0) {
            ss << L"Время ожидания: ~" << m_currentTicket.estimatedWaitTime << L" мин";
        }

        m_currentTicketLabel = CreateWindowExW(0, L"STATIC", ss.str().c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 200, 200, 400, 200,
            m_hWnd, nullptr, g_hInstance, nullptr);

        HFONT hFont = CreateFontW(
            32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"
        );
        SendMessageW(m_currentTicketLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"ВЕРНУТЬСЯ В МЕНЮ",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 150, 450, 300, 60,
            m_hWnd, (HMENU)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
    }

public:
    HWND m_hWnd;

    MainWindow() : m_currentState(WindowState::MAIN_MENU),
        m_currentTicketLabel(nullptr) {
    }

    void create(HWND hParent) {
        createMainMenu();
    }

    void handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND:
            onCommand(LOWORD(wParam));
            break;
        case WM_PAINT:
            onPaint();
            break;
        }
    }

    void onCommand(int cmd) {
        switch (cmd) {
        case ID_BTN_REGISTER:
        {
            RegistrationWindow* regWnd = new RegistrationWindow();
            regWnd->show(m_hWnd);
            // Объект будет удалён в WM_DESTROY окна регистрации
        }
        break;

        case ID_BTN_SUBMIT_ITEMS:
            createSubmitMenu();
            break;

        case ID_BTN_GENERAL_QUEUE:
            handleQueueSelection(QueueType::GENERAL);
            break;

        case ID_BTN_FIRST_TIME:
            handleQueueSelection(QueueType::FIRST_TIME);
            break;

        case ID_BTN_EXTRA_20:
            handleQueueSelection(QueueType::EXTRA_20);
            break;

        case ID_BTN_TRUST:
            handleQueueSelection(QueueType::TRUST);
            break;

        case ID_BTN_PAID:
            handleQueueSelection(QueueType::PAID);
            break;

        case ID_BTN_EXPENSIVE:
            handleQueueSelection(QueueType::EXPENSIVE);
            break;

        case ID_BTN_BACK:
            if (m_currentState == WindowState::SUBMIT_MENU ||
                m_currentState == WindowState::TICKET_ISSUED) {
                createMainMenu();
            }
            break;

        case ID_BTN_CLOSE:
            PostQuitMessage(0);
            break;

        case ID_BTN_MY_SALES:
            MessageBoxW(m_hWnd, L"Раздел в разработке", L"Информация", MB_OK);
            break;

        case ID_BTN_ADDRESSES:
            MessageBoxW(m_hWnd, L"Раздел в разработке", L"Информация", MB_OK);
            break;

        case ID_BTN_CONSIGNOR_NUMBER:
            MessageBoxW(m_hWnd, L"Раздел в разработке", L"Информация", MB_OK);
            break;

        case ID_BTN_OTHER_SERVICES:
            MessageBoxW(m_hWnd, L"Раздел в разработке", L"Информация", MB_OK);
            break;
        }
    }

    void onPaint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT rect;
        GetClientRect(m_hWnd, &rect);
        FillRect(hdc, &rect, g_hBrushWindow);
        EndPaint(m_hWnd, &ps);
    }
};

// Global window instance
MainWindow g_mainWindow;

// Window procedure
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hBrushBtn = CreateSolidBrush(Config::ACCENT_COLOR);
        g_hBrushBackBtn = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        g_hBrushWindow = CreateSolidBrush(RGB(255, 255, 255));
        g_hFontTitle = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_hFontButton = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_mainWindow.m_hWnd = hWnd;
        g_mainWindow.create(hWnd);
        return 0;

    case WM_MOUSEMOVE:
    {
        if (!IsWindow(g_hCloseBtn)) break;

        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        bool inZone = (x >= screenWidth - 100 && y <= 100);
        ShowWindow(g_hCloseBtn, inZone ? SW_SHOW : SW_HIDE);
    }
    break;

    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        HWND hBtn = (HWND)lParam;
        SetTextColor(hdc, RGB(255, 255, 255));

        DWORD btnId = GetDlgCtrlID(hBtn);
        if (btnId == ID_BTN_BACK || btnId == ID_BTN_CLOSE) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)g_hBrushBackBtn;
        }
        SetBkColor(hdc, Config::PRIMARY_COLOR);
        return (LRESULT)g_hBrushBtn;
    }
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND:
        g_mainWindow.handleMessage(msg, wParam, lParam);
        return 0;

    case WM_PAINT:
        g_mainWindow.handleMessage(msg, wParam, lParam);
        return 0;

    case WM_DESTROY:
        DeleteObject(g_hBrushBtn);
        DeleteObject(g_hBrushBackBtn);
        DeleteObject(g_hBrushWindow);
        DeleteObject(g_hFontTitle);
        DeleteObject(g_hFontButton);
        g_hCloseBtn = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
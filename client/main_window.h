//main_window.h
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>

#include "config.h"
#include "queue_manager.h"
#include "registration_dialog.h"
#include "logger.h"
#include "auth_manager.h"
#include "receipt_printer.h"
#include "https_client.h"
#include "login_window.h"
#include "string_utils.h"

extern QueueManager g_queueManager;
extern AuthManager g_authManager;
extern Logger g_logger;
extern HINSTANCE g_hInstance;
extern HWND g_hMainWnd;
extern ReceiptPrinter g_printer;
extern HTTPSClient g_httpsClient;

static HBRUSH g_hBrushBtn = nullptr;
static HBRUSH g_hBrushGreen = nullptr;      // зеленая кисть для кнопок оплаты
static HBRUSH g_hBrushBackBtn = nullptr;
static HBRUSH g_hBrushWindow = nullptr;
static HFONT g_hFontTitle = nullptr;
static HFONT g_hFontButton = nullptr;
static HFONT g_hFontEdit = nullptr;
static HFONT g_hFontLabel = nullptr;
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

// New IDs for general queue input page
#define ID_EDIT_CONSIGNOR_ID 4001
#define ID_BTN_DIGIT_0 4002
#define ID_BTN_DIGIT_1 4003
#define ID_BTN_DIGIT_2 4004
#define ID_BTN_DIGIT_3 4005
#define ID_BTN_DIGIT_4 4006
#define ID_BTN_DIGIT_5 4007
#define ID_BTN_DIGIT_6 4008
#define ID_BTN_DIGIT_7 4009
#define ID_BTN_DIGIT_8 4010
#define ID_BTN_DIGIT_9 4011
#define ID_BTN_CLEAR 4012
#define ID_BTN_NEXT 4013
#define ID_BTN_PRINT_TICKET 4014
#define ID_BTN_RESET_ID 4015
#define ID_STATIC_NAME_DISPLAY 4016
#define ID_STATIC_DAILY_COUNT 4017
#define ID_STATIC_DESCRIPTION 4018
#define ID_STATIC_PROMPT 4019

// New IDs for +20 positions input page
#define ID_BTN_PAY_CARD 5010
#define ID_BTN_PAY_QR   5011
#define IDC_STATIC_EXTRA20_TEXT 5012
#define ID_STATIC_EXTRA20_TITLE 5013
#define ID_STATIC_EXTRA20_SUBTITLE 5014

// New IDs for trustAcceptains
#define ID_STATIC_TRUST_TITLE      6001
#define ID_STATIC_TRUST_SUBTITLE   6002
#define IDC_STATIC_TRUST_TEXT      6003
#define ID_BTN_TRUST_PRINT         6004
#define ID_BTN_TRUST_BACK          6005

// ID для окна платного приёма
#define ID_BTN_PAID_PRINT           6006
#define ID_BTN_PAID_BACK            6007
#define ID_STATIC_PAID_TITLE        6008
#define ID_STATIC_PAID_SUBTITLE     6009
#define IDC_STATIC_PAID_TEXT        6010
#define ID_STATIC_PAID_QUEUE_COUNT  6011

// ID для окна приема дорогих вещей
#define ID_BTN_EXPENSIVE_PRINT       6012
#define ID_BTN_EXPENSIVE_BACK        6013
#define ID_STATIC_EXPENSIVE_TITLE    6014
#define ID_STATIC_EXPENSIVE_SUBTITLE 6015
#define IDC_STATIC_EXPENSIVE_TEXT    6016
#define ID_STATIC_EXPENSIVE_QUEUE_COUNT 6017

// ID для окна отображения статистики продаж
#define ID_BTN_SALES_BACK 7001
#define ID_STATIC_SALES_STATUS 7002
#define ID_LIST_SALES 7003
// ========== ДОБАВЛЕНО: ID для статика информации о комитенте ==========
#define ID_STATIC_CONSIGNOR_INFO 8001

#define WM_SALES_DATA_READY (WM_USER + 200)
#define WM_SALES_LOADING_START (WM_USER + 201)

// ID для окна "Узнать номер комитента" 
#define WM_CONSIGNOR_DATA_READY (WM_USER + 202)
#define WM_CONSIGNOR_LOADING_START (WM_USER + 203)

// Window states
enum class WindowState {
    MAIN_MENU,
    SUBMIT_MENU,
    REGISTRATION,
    CONSIGNOR_LOOKUP,
    TICKET_ISSUED,
    GENERAL_QUEUE_INPUT,
    EXTRA_20_PAYMENT,
    MY_SALES,
    ADDRESSES
};

class MainWindow {
private:
    WindowState m_currentState;
    std::vector<HWND> m_buttons;
    HWND m_currentTicketLabel;
    QueueTicket m_currentTicket;

    // ---- Переменные для страницы ввода ID ----
    HWND m_hEditConsignorId;
    HWND m_hStaticNameDisplay;
    HWND m_hStaticDailyCount;
    HWND m_hStaticDescription;
    HWND m_hStaticPrompt;
    HWND m_hBtnNext;
    HWND m_hBtnPrintTicket;
    HWND m_hBtnResetId;
    HWND m_hBtnClear;
    HWND m_hSalesListView;       // ListView для отображения продаж
    HWND m_hSalesStatusLabel;    // для отображения статуса загрузки информации
    HWND m_hClientInfoLabel;     // отображение информации о клиенте
    HWND m_hSalesBackBtn;        // кнопка "Назад" (красная)

    std::vector<HWND> m_digitButtons;
    int m_currentClientId;
    std::wstring m_currentClientName;
    std::wstring m_currentClientPhone;
    bool m_nameConfirmed;
    HWND m_hConsignorInfoLabel;  // статик для вывода информации о комитенте

    // ------------------------------------------------------------
    // Вспомогательные методы
    // ------------------------------------------------------------
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
        m_digitButtons.clear();
        g_hCloseBtn = nullptr;
        m_currentTicketLabel = nullptr;
        m_hEditConsignorId = nullptr;
        m_hStaticNameDisplay = nullptr;
        m_hStaticDailyCount = nullptr;
        m_hStaticDescription = nullptr;
        m_hStaticPrompt = nullptr;
        m_hBtnNext = nullptr;
        m_hBtnPrintTicket = nullptr;
        m_hBtnResetId = nullptr;
        m_hBtnClear = nullptr;
        m_hSalesListView = nullptr;
        m_hSalesStatusLabel = nullptr;
        m_hClientInfoLabel = nullptr;    
        m_hSalesBackBtn = nullptr;
        m_nameConfirmed = false;
        m_hConsignorInfoLabel = nullptr;
        m_currentClientId = 0;
        m_currentClientName.clear();
        m_currentClientPhone.clear();
    }

    void styleButtons() {
        for (HWND btn : m_buttons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
        for (HWND btn : m_digitButtons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
    }

    // ------------------------------------------------------------
    // Создание главного меню
    // ------------------------------------------------------------
    void createMainMenu() {
        clearWindow();
        m_currentState = WindowState::MAIN_MENU;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int btnWidth = 400;
        int btnHeight = Config::BUTTON_HEIGHT;
        int centerX = (screenWidth - btnWidth) / 2;

        // Заголовок: "ДОБРО" и "Комиссионный магазин" (две строки)
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 30, btnWidth, 120,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 150, btnWidth, 100,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Приветствие или инструкция
        int greetingY = 30 + 220 + 20;
        int greetingHeight = 80;
        if (g_authManager.isLoggedIn()) {
            std::wstring greeting = g_authManager.getFullName() + L" добрый день. Ваш id " + std::to_wstring(g_authManager.getClientId());
            HWND hGreeting = CreateWindowExW(0, L"STATIC", greeting.c_str(),
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                centerX, greetingY, btnWidth, greetingHeight,
                m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hGreeting, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
        else {
            const wchar_t* instruction = L"Нажмите кнопку «Регистрация» или в секции «Сдать товар» выберете «Я в первый раз (оформление договора)» для оформления договора у товароведа.";
            HWND hInstruction = CreateWindowExW(0, L"STATIC", instruction,
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                centerX, greetingY, btnWidth, greetingHeight,
                m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hInstruction, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }

        // Кнопка закрытия
        int closeBtnSize = 20;
        g_hCloseBtn = CreateWindowExW(0, L"BUTTON", L"✕",
            WS_CHILD | BS_PUSHBUTTON,
            screenWidth - closeBtnSize - 20, 20, closeBtnSize, closeBtnSize,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_CLOSE, g_hInstance, nullptr);
        m_buttons.push_back(g_hCloseBtn);

        int startY = greetingY + greetingHeight + 20;

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Регистрация",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_REGISTER, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Сдать товар",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + btnHeight + 10, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_SUBMIT_ITEMS, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Мои продажи",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_MY_SALES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Адреса магазинов",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_ADDRESSES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Узнать номер комитента",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_CONSIGNOR_NUMBER, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Другие услуги",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 5, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_OTHER_SERVICES, g_hInstance, nullptr));

        styleButtons();
    }

    // ------------------------------------------------------------
    // Меню выбора очереди
    // ------------------------------------------------------------
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

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"ДОБРО КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 20, btnWidth, 100,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"Выберите тип очереди",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 20 + 100 + 10, btnWidth, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int startY = 20 + 100 + 10 + 50 + 20;

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ОБЩАЯ ОЧЕРЕДЬ\n(до 20 товаров)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_GENERAL_QUEUE, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"Я ПЕРВЫЙ РАЗ\n(оформление договора)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + btnHeight + gap, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_FIRST_TIME, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"+20 ПОЗИЦИЙ\n(дополнительно к базовым)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_EXTRA_20, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"НА ДОВЕРИИ\n(без присутствия)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_TRUST, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ПЛАТНЫЙ ПРИЕМ\n(200 руб., без очереди)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_PAID, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ДОРОГОЙ ТОВАР\n(>5000 руб.)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 5, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_EXPENSIVE, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"НАЗАД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + gap) * 5 + btnHeight + backGap, btnWidth, backBtnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
    }
    //-------------------------------------------------------------
    // Страница отображения статистики продаж пользователя (подтягиваем данные из 1С)
    //-------------------------------------------------------------
    void loadSalesData() {
        g_logger.info(L"loadSalesData: started");

        // Сначала сообщаем UI о начале загрузки (статус)
        PostMessageW(m_hWnd, WM_SALES_LOADING_START, 0, 0);

        // Проверка авторизации
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            std::wstring msg = L"Клиент не авторизован";
            SetWindowTextW(m_hClientInfoLabel, msg.c_str());
            g_logger.warning(L"loadSalesData: " + msg);
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            std::wstring msg = L"Токен отсутствует, выполните вход";
            SetWindowTextW(m_hClientInfoLabel, msg.c_str());
            g_logger.warning(L"loadSalesData: " + msg);
            return;
        }

        // Показываем, что клиент авторизован
        std::wstring fullName = g_authManager.getFullName();
        std::wstring clientInfo;
        if (!fullName.empty()) {
            clientInfo = L"Клиент " + fullName + L" авторизован";
        }
        else {
            clientInfo = L"Клиент авторизован";
        }
        SetWindowTextW(m_hClientInfoLabel, clientInfo.c_str());
        g_logger.info(L"loadSalesData: client info set to: " + clientInfo);

        int clientId = g_authManager.getClientId();
        g_logger.info(L"loadSalesData: clientId = " + std::to_wstring(clientId));

        // Запускаем асинхронный запрос
        std::thread([this, clientId, authToken]() {
            // Сначала сообщаем UI о начале загрузки
            PostMessageW(m_hWnd, WM_SALES_LOADING_START, 0, 0);

            std::wstring path = L"/api/v1/clients/sales?client_id=" + std::to_wstring(clientId);
            auto response = g_httpsClient.get(path, authToken);

            // Возвращаем результат в UI-поток
            PostMessageW(m_hWnd, WM_SALES_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<json>(response)));
            }).detach();
        g_logger.info(L"loadSalesData: async request sent");
    }

    void createMySales() {
        clearWindow();
        m_currentState = WindowState::MY_SALES;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int top = 30;

        // Заголовок
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Мои продажи",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 80,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 110;

        // Информация о клиенте (ФИО, статус авторизации) -----
        m_hClientInfoLabel = CreateWindowExW(0, L"STATIC", L"",   // изначально пусто
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hClientInfoLabel, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        top += 60; // отступ

        // ----- ЭЛЕМЕНТ СТАТУСА: "Загрузка данных..." / результат -----
        m_hSalesStatusLabel = CreateWindowExW(0, L"STATIC", L"Загрузка данных...",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, (HMENU)ID_STATIC_SALES_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hSalesStatusLabel, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        top += 60; // отступ

        // ListView
        int listHeight = screenHeight - top - 120;
        m_hSalesListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
            20, top, screenWidth - 40, listHeight,
            m_hWnd, (HMENU)ID_LIST_SALES, g_hInstance, nullptr);
        SendMessageW(m_hSalesListView, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Настройка колонок ListView
        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::vector<std::wstring> headers = {
            L"Дата отчёта", L"№ товара", L"Наименование",
            L"Цена за ед.", L"Кол-во", L"Дата продажи",
            L"Состояние", L"Примечание"
        };
        std::vector<int> widths = { 120, 100, 200, 120, 80, 120, 180, 200 };
        for (size_t i = 0; i < headers.size(); ++i) {
            col.pszText = const_cast<LPWSTR>(headers[i].c_str());
            col.cx = widths[i];
            col.iSubItem = static_cast<int>(i);
            ListView_InsertColumn(m_hSalesListView, i, &col);
        }

        // Кнопка "Назад" (красная)
        int btnWidth = 200, btnHeight = 60;
        int backY = screenHeight - btnHeight - 30;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, backY, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_SALES_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Запрос данных
        loadSalesData();
    }

    // Обработчик Адреса магазинов
    void createAddressesWindow() {
        clearWindow();
        m_currentState = WindowState::ADDRESSES;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int top = 30;

        // Заголовок "ДОБРО"
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 80,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 90;

        // Подзаголовок "Комиссионный магазин"
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 60,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 80;

        // Заголовок списка "Перечень и адреса магазинов Добро"
        HWND hListTitle = CreateWindowExW(0, L"STATIC", L"Перечень и адреса магазинов Добро",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hListTitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 70;

        // Список адресов магазинов (три строки)
        const wchar_t* addresses[] = {
            L"«магазин Добро – Садовая»: город Санкт-Петербург, ул. Садовая д.39/12",
            L"«магазин Добро – Ладожская»: город Санкт-Петербург, Индустриальный пр., д.34",
            L"«магазин Добро – Озерки»: город Санкт-Петербург, Выборгское шоссе, д.3/1"
        };

        int lineHeight = 50;
        for (int i = 0; i < 3; ++i) {
            HWND hAddr = CreateWindowExW(0, L"STATIC", addresses[i],
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                50, top + i * lineHeight, screenWidth - 100, lineHeight,
                m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hAddr, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        }
        top += 3 * lineHeight + 40;

        // Кнопка "Назад" (красная, как в окне "Мои продажи")
        int btnWidth = 200, btnHeight = 60;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, top, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Принудительно устанавливаем красный фон для кнопки "Назад"
        // (обрабатывается в WM_CTLCOLORBTN по ID_BTN_BACK)

        g_logger.info(L"Addresses window created");
    }

    // Обработчик получения номера комитента
    void createConsignorNumber() {
        clearWindow();
        m_currentState = WindowState::CONSIGNOR_LOOKUP;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int top = 30;

        // Заголовок "ДОБРО"
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 80,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 90;

        // Подзаголовок "Комиссионный магазин"
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 60,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 80;

        // Создаём статический элемент для отображения информации о пользователе
        m_hConsignorInfoLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 100,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_CONSIGNOR_INFO, g_hInstance, nullptr);
        SendMessageW(m_hConsignorInfoLabel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 120;

        // Проверка авторизации
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Пользователь не авторизован");
            // Добавляем кнопку "Назад"
            int btnWidth = 200, btnHeight = 60;
            HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                centerX - btnWidth / 2, top, btnWidth, btnHeight,
                m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
            m_buttons.push_back(hBack);
            SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
            g_logger.info(L"createConsignorNumber: user not authorized, showing message");
            return;
        }

        // Авторизован – показываем загрузку и добавляем кнопку "Назад" внизу
        SetWindowTextW(m_hConsignorInfoLabel, L"Загрузка данных...");
        int btnWidth = 200, btnHeight = 60;
        int backY = screenHeight - btnHeight - 30;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, backY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Запускаем асинхронный запрос к серверу
        loadConsignorData();
    }

    // =========================================================================
    // Асинхронный запрос данных о комитенте
    // =========================================================================
    void loadConsignorData() {
        g_logger.info(L"loadConsignorData: started");

        // Повторная проверка авторизации (на случай, если состояние изменилось)
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Пользователь не авторизован");
            g_logger.warning(L"loadConsignorData: user not logged in");
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Токен отсутствует, выполните вход");
            g_logger.warning(L"loadConsignorData: auth token empty");
            return;
        }

        // Отправляем сообщение о начале загрузки (для обновления статуса, если потребуется)
        PostMessageW(m_hWnd, WM_CONSIGNOR_LOADING_START, 0, 0);

        // Запускаем поток для выполнения HTTP-запроса
        std::thread([this, authToken]() {
            try {
                g_logger.info(L"Consignor thread: starting GET request");
                std::wstring path = L"/api/v1/clients/me";
                auto response = g_httpsClient.get(path, authToken);
                g_logger.info(L"Consignor thread: GET request completed, posting result");
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<json>(response)));
            }
            catch (const std::exception& e) {
                g_logger.error(L"Consignor thread exception: " + utf8_to_wstring(e.what()));
                // Отправляем пустой optional, чтобы обработать ошибку
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<json>(std::nullopt)));
            }
            catch (...) {
                g_logger.error(L"Consignor thread unknown exception");
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<json>(std::nullopt)));
            }
            }).detach();

        g_logger.info(L"loadConsignorData: async request sent");
    }

    // =========================================================================
    // ДОБАВЛЕННЫЙ МЕТОД: Обработчик ответа от сервера для окна "Узнать номер комитента"
    // =========================================================================
    void onConsignorDataReady(const std::optional<json>& response) {
        g_logger.info(L"onConsignorDataReady: received response");

        if (!response) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Ошибка соединения с сервером");
            g_logger.error(L"onConsignorDataReady: no response (connection error)");
            InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE); // принудительная перерисовка
            return;
        }

        // Проверяем наличие обязательного поля "id"
        if (!response->contains("id") || !(*response)["id"].is_number_integer()) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Некорректный ответ сервера");
            g_logger.error(L"onConsignorDataReady: invalid response format (missing id)");
            InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
            return;
        }

        int clientId = (*response)["id"].get<int>();
        std::string nameUtf8 = response->value("name", "");
        std::wstring fullName = utf8_to_wstring(nameUtf8);

        // Если имя не пришло, используем локальное из AuthManager (запасной вариант)
        if (fullName.empty()) {
            fullName = g_authManager.getFullName();
            g_logger.warning(L"onConsignorDataReady: name not in response, using local value");
        }

        // Формируем строку для отображения
        std::wstring infoText = fullName + L"\nВаш номер комитента: " + std::to_wstring(clientId);
        SetWindowTextW(m_hConsignorInfoLabel, infoText.c_str());
        InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE); // опять перерисовываем окно

        g_logger.info(L"onConsignorDataReady: displayed info for client " + std::to_wstring(clientId) +
            L", name=" + fullName);
    }

    void onSalesDataReady(const std::optional<json>& response) {
        g_logger.info(L"onSalesDataReady: received response");
        if (!response) {
            SetWindowTextW(m_hSalesStatusLabel, L"Ошибка соединения с сервером");
            g_logger.error(L"onSalesDataReady: no response (connection error)");
            return;
        }
        if (!response->contains("sales") || !(*response)["sales"].is_array()) {
            SetWindowTextW(m_hSalesStatusLabel, L"Некорректный ответ сервера");
            g_logger.error(L"onSalesDataReady: invalid response format");
            return;
        }

        auto& sales = (*response)["sales"];
        if (sales.empty()) {
            SetWindowTextW(m_hSalesStatusLabel, L"Нет данных о продажах");
            g_logger.info(L"onSalesDataReady: no sales data");
            return;
        }

        // Очищаем ListView
        ListView_DeleteAllItems(m_hSalesListView);

        // Заполняем
        int index = 0;
        for (const auto& item : sales) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = index;

            std::vector<std::wstring> columns;
            columns.push_back(utf8_to_wstring(item.value("report_date", "")));
            columns.push_back(utf8_to_wstring(item.value("item_number", "")));
            columns.push_back(utf8_to_wstring(item.value("item_name", "")));
            columns.push_back(::to_wstring(item.value("price", 0.0)));          // double
            columns.push_back(std::to_wstring(item.value("quantity", 0)));
            columns.push_back(utf8_to_wstring(item.value("sale_date", "")));
            columns.push_back(utf8_to_wstring(item.value("status", "")));
            columns.push_back(utf8_to_wstring(item.value("note", "")));

            for (size_t i = 0; i < columns.size(); ++i) {
                lvi.iSubItem = static_cast<int>(i);
                lvi.pszText = const_cast<LPWSTR>(columns[i].c_str());
                if (i == 0)
                    ListView_InsertItem(m_hSalesListView, &lvi);
                else
                    ListView_SetItem(m_hSalesListView, &lvi);
            }
            ++index;
        }

        std::wstring status = L"Найдено записей: " + std::to_wstring(index);
        SetWindowTextW(m_hSalesStatusLabel, status.c_str());
        g_logger.info(L"onSalesDataReady: loaded " + std::to_wstring(index) + L" records");
    }

    // ------------------------------------------------------------
    // Новая страница: ввод ID комитента для общей очереди
    // ------------------------------------------------------------
    void createGeneralQueueInput() {
        clearWindow();
        m_currentState = WindowState::GENERAL_QUEUE_INPUT;
        m_nameConfirmed = false;
        m_currentClientId = 0;
        m_currentClientName.clear();
        m_currentClientPhone.clear();

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int top = 40;

        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 60,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 60;

        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 40,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        top += 50;

        const wchar_t* descText =
            L"Вы выбрали ОБЩУЮ ОЧЕРЕДЬ. Вы получаете талончик электронной очереди и ожидаете своего времени приема.\n"
            L"Количество товаров ограничено 20 наименованиями. При большой загруженности, время ожидания может достигать 4 часов и более.";
        m_hStaticDescription = CreateWindowExW(0, L"STATIC", descText,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 100,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_DESCRIPTION, g_hInstance, nullptr);
        SendMessageW(m_hStaticDescription, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 110;

        std::wstring dailyCountText = L"Количество ожидающих приема человек: ";
        std::wstring authToken = g_authManager.getAuthToken();
        int dailyCount = g_queueManager.getDailyCount(QueueType::GENERAL, authToken);
        dailyCountText += std::to_wstring(dailyCount);
        m_hStaticDailyCount = CreateWindowExW(0, L"STATIC", dailyCountText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 40,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_DAILY_COUNT, g_hInstance, nullptr);
        SendMessageW(m_hStaticDailyCount, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 50;

        const wchar_t* promptText =
            L"Введите номер комитента и нажмите кнопку Далее.\n"
            L"Если номер ввели неверно, нажмите кнопку Сбросить.";
        m_hStaticPrompt = CreateWindowExW(0, L"STATIC", promptText,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_PROMPT, g_hInstance, nullptr);
        SendMessageW(m_hStaticPrompt, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 70;

        int editWidth = 300;
        int editHeight = 50;
        int editX = centerX - editWidth / 2;
        m_hEditConsignorId = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
            editX, top, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_EDIT_CONSIGNOR_ID, g_hInstance, nullptr);
        SendMessageW(m_hEditConsignorId, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
        SendMessageW(m_hEditConsignorId, EM_SETLIMITTEXT, (WPARAM)10, 0);
        top += editHeight + 10;

        int digitBtnSize = 50;
        int digitGap = 8;
        int digitRowWidth = digitBtnSize * 5 + digitGap * 4;
        int digitStartX = centerX - digitRowWidth / 2;

        int row1Y = top;
        for (int i = 1; i <= 5; ++i) {
            int x = digitStartX + (i - 1) * (digitBtnSize + digitGap);
            HWND btn = CreateWindowExW(0, L"BUTTON", std::to_wstring(i).c_str(),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                x, row1Y, digitBtnSize, digitBtnSize,
                m_hWnd, (HMENU)(INT_PTR)(ID_BTN_DIGIT_0 + i), g_hInstance, nullptr);
            m_digitButtons.push_back(btn);
            m_buttons.push_back(btn);
        }
        top += digitBtnSize + digitGap;

        int row2Y = top;
        for (int i = 6; i <= 9; ++i) {
            int x = digitStartX + (i - 6) * (digitBtnSize + digitGap);
            HWND btn = CreateWindowExW(0, L"BUTTON", std::to_wstring(i).c_str(),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                x, row2Y, digitBtnSize, digitBtnSize,
                m_hWnd, (HMENU)(INT_PTR)(ID_BTN_DIGIT_0 + i), g_hInstance, nullptr);
            m_digitButtons.push_back(btn);
            m_buttons.push_back(btn);
        }
        int x0 = digitStartX + 4 * (digitBtnSize + digitGap);
        HWND btn0 = CreateWindowExW(0, L"BUTTON", L"0",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x0, row2Y, digitBtnSize, digitBtnSize,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_DIGIT_0, g_hInstance, nullptr);
        m_digitButtons.push_back(btn0);
        m_buttons.push_back(btn0);
        top += digitBtnSize + 15;

        int clearWidth = 120;
        m_hBtnClear = CreateWindowExW(0, L"BUTTON", L"Сбросить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - clearWidth / 2, top, clearWidth, 40,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_CLEAR, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnClear);
        top += 50;

        int nextWidth = 200;
        m_hBtnNext = CreateWindowExW(0, L"BUTTON", L"Далее",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - nextWidth / 2, top, nextWidth, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_NEXT, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnNext);
        top += 70;

        m_hStaticNameDisplay = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_NAME_DISPLAY, g_hInstance, nullptr);
        SendMessageW(m_hStaticNameDisplay, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        ShowWindow(m_hStaticNameDisplay, SW_HIDE);
        top += 60;

        m_hBtnResetId = CreateWindowExW(0, L"BUTTON", L"Сбросить",
            WS_CHILD | BS_PUSHBUTTON,
            screenWidth - 150, top - 60, 100, 40,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_RESET_ID, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnResetId);
        ShowWindow(m_hBtnResetId, SW_HIDE);

        int printWidth = 300;
        m_hBtnPrintTicket = CreateWindowExW(0, L"BUTTON", L"Печать талона",
            WS_CHILD | BS_PUSHBUTTON,
            centerX - printWidth / 2, top + 20, printWidth, 70,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_PRINT_TICKET, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnPrintTicket);
        ShowWindow(m_hBtnPrintTicket, SW_HIDE);
        top += 100;

        int backWidth = 200;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - backWidth / 2, top + 20, backWidth, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);

        styleButtons();
        SetFocus(m_hEditConsignorId);

        g_logger.info(L"General queue input page created, daily count = " + std::to_wstring(dailyCount));
    }

    // ------------------------------------------------------------
    // Обработка выбора очереди (для других типов)
    // ------------------------------------------------------------
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
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
    }

    // ------------------------------------------------------------
    // Обработчик нажатия "Далее" на странице ввода ID
    // ------------------------------------------------------------
    void onGeneralQueueNext() {
        wchar_t buf[20];
        GetWindowTextW(m_hEditConsignorId, buf, 20);
        std::wstring idStr(buf);
        if (idStr.empty()) {
            SetWindowTextW(m_hStaticNameDisplay, L"Введите ID комитента");
            ShowWindow(m_hStaticNameDisplay, SW_SHOW);
            return;
        }

        int id = _wtoi(idStr.c_str());
        if (id <= 0) {
            SetWindowTextW(m_hStaticNameDisplay, L"Некорректный ID");
            ShowWindow(m_hStaticNameDisplay, SW_SHOW);
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/clients/by_id?id=" + idStr;
        auto response = g_httpsClient.get(path, authToken);

        if (!response || !response->contains("id")) {
            SetWindowTextW(m_hStaticNameDisplay, L"Клиент не найден");
            ShowWindow(m_hStaticNameDisplay, SW_SHOW);
            g_logger.warning(L"Client not found for id " + idStr);
            return;
        }

        m_currentClientId = id;
        m_currentClientName = utf8_to_wstring((*response)["name"].get<std::string>());
        m_currentClientPhone = utf8_to_wstring((*response)["phone"].get<std::string>());

        std::wstring displayText = m_currentClientName + L", можете распечатать талон.";
        SetWindowTextW(m_hStaticNameDisplay, displayText.c_str());
        ShowWindow(m_hStaticNameDisplay, SW_SHOW);

        ShowWindow(m_hBtnNext, SW_HIDE);
        ShowWindow(m_hBtnPrintTicket, SW_SHOW);
        ShowWindow(m_hBtnResetId, SW_SHOW);

        m_nameConfirmed = true;
        g_logger.info(L"Client name fetched for id " + idStr + L": " + m_currentClientName);
    }

    // ------------------------------------------------------------
    // Обработчик "Сбросить" (рядом с именем)
    // ------------------------------------------------------------
    void onGeneralQueueResetId() {
        m_nameConfirmed = false;
        m_currentClientId = 0;
        m_currentClientName.clear();
        m_currentClientPhone.clear();

        SetWindowTextW(m_hEditConsignorId, L"");

        ShowWindow(m_hStaticNameDisplay, SW_HIDE);
        ShowWindow(m_hBtnPrintTicket, SW_HIDE);
        ShowWindow(m_hBtnResetId, SW_HIDE);

        ShowWindow(m_hBtnNext, SW_SHOW);

        SetFocus(m_hEditConsignorId);
        g_logger.info(L"General queue input reset");
    }

    // ------------------------------------------------------------
    // Обработчик "Печать талона" общей очереди
    // ------------------------------------------------------------
    void onGeneralQueuePrint() {
        if (!m_nameConfirmed || m_currentClientId == 0) {
            MessageBoxW(m_hWnd, L"Сначала подтвердите ID комитента", L"Ошибка", MB_OK);
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        auto ticketOpt = g_queueManager.getTicket(
            m_currentClientId,
            QueueType::GENERAL,
            Config::MAX_ITEMS_GENERAL_QUEUE,
            authToken
        );

        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"Failed to get ticket for client id " + std::to_wstring(m_currentClientId));
            return;
        }

        m_currentTicket = ticketOpt.value();

        bool printed = g_printer.printTicket(m_currentTicket,
            m_currentClientName,
            m_currentClientPhone);
        if (printed) {
            g_logger.info(L"Ticket printed: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"Ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        showTicketIssued();
    }

    // --- Обработчик для "Я первый раз" (оформление договора) ---
    void handleFirstTime() {
        json request;
        request["window"] = "1";
        auto response = g_httpsClient.post(L"/api/v1/queue/first_time/create", request, L"");
        if (!response || !response->contains("ticket_number")) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"FirstTime: failed to create ticket");
            return;
        }
        std::string ticketNumberUtf8 = (*response)["ticket_number"].get<std::string>();
        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        QueueTicket ticket;
        ticket.ticketNumber = ticketNumber;
        ticket.type = QueueType::FIRST_TIME;
        ticket.position = 0;
        ticket.itemsCount = 1;
        ticket.windowNumber = L"1";
        ticket.estimatedWaitTime = 0;
        ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

        bool printed = g_printer.printTicket(ticket, L"Новый клиент", L"");
        if (printed) {
            g_logger.info(L"FirstTime ticket printed: " + ticketNumber);
        }
        else {
            g_logger.warning(L"FirstTime ticket print failed, saved to file: " + ticketNumber);
            MessageBoxW(m_hWnd, L"Талон сохранён в файл (печать не удалась).", L"Предупреждение", MB_OK);
        }

        m_currentTicket = ticket;
        showTicketIssued();
    }

    // Обработчик для "+20 позиций"
    void showExtra20Payment() {
        clearWindow();
        m_currentState = WindowState::EXTRA_20_PAYMENT;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 30, screenWidth - 100, 80,
            m_hWnd, (HMENU)ID_STATIC_EXTRA20_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"СДАЧА +20 товаров",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 120, screenWidth - 100, 60,
            m_hWnd, (HMENU)ID_STATIC_EXTRA20_SUBTITLE, g_hInstance, nullptr);
        SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали услугу +20 товаров к базовым.\n200 руб. за 20 дополнительных товаров.",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 350, centerY - 160, 700, 120,
            m_hWnd, (HMENU)IDC_STATIC_EXTRA20_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int btnW = 300, btnH = 60, gap = 20;
        int startY = centerY + 20;

        HWND btnCard = CreateWindowExW(0, L"BUTTON", L"Оплата банковской картой",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAY_CARD, g_hInstance, nullptr);
        m_buttons.push_back(btnCard);

        HWND btnQR = CreateWindowExW(0, L"BUTTON", L"Оплата по QR-коду",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAY_QR, g_hInstance, nullptr);
        m_buttons.push_back(btnQR);

        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + 2 * (btnH + gap), btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);

        styleButtons();
        g_logger.info(L"Extra20 payment page shown");
    }

    // === ИСПРАВЛЕННЫЙ МЕТОД ===
    void onExtra20PaymentSuccess() {
        g_logger.info(L"[onExtra20PaymentSuccess] Starting extra 20 payment flow...");

        std::wstring authToken = g_authManager.getAuthToken();
        g_logger.info(L"[onExtra20PaymentSuccess] Current authToken: " +
            (authToken.empty() ? std::wstring(L"<empty>") : std::wstring(L"<present> (length=" + std::to_wstring(authToken.length()) + L")")));

        int clientId = g_authManager.getClientId();
        bool isLogged = g_authManager.isLoggedIn();

        g_logger.info(L"[onExtra20PaymentSuccess] Login status: isLoggedIn=" + std::to_wstring(isLogged) +
            L", clientId=" + std::to_wstring(clientId));

        if (!isLogged || clientId == 0) {
            g_logger.info(L"[onExtra20PaymentSuccess] No valid login. Showing registration dialog...");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                g_logger.warning(L"[onExtra20PaymentSuccess] Registration cancelled by user");
                return;
            }
            authToken = g_authManager.getAuthToken();
            clientId = g_authManager.getClientId();
            g_logger.info(L"[onExtra20PaymentSuccess] After registration: clientId=" + std::to_wstring(clientId));
        }

        if (clientId == 0) {
            MessageBoxW(m_hWnd, L"Не удалось определить ID клиента. Попробуйте войти заново.", L"Ошибка", MB_OK);
            g_logger.error(L"[onExtra20PaymentSuccess] clientId is still 0");
            return;
        }

        g_logger.info(L"[onExtra20PaymentSuccess] Proceeding with clientId: " + std::to_wstring(clientId));

        // Получаем талон (токен может быть пустым — QueueManager обработает)
        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::EXTRA_20, 20, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"[onExtra20PaymentSuccess] Failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }

        m_currentTicket = ticketOpt.value();
        g_logger.info(L"[onExtra20PaymentSuccess] Ticket obtained successfully: " + m_currentTicket.ticketNumber);

        bool printed = g_printer.printTicket(m_currentTicket,
            g_authManager.getFullName(),
            g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"[onExtra20PaymentSuccess] Ticket printed: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"[onExtra20PaymentSuccess] Ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        showTicketIssued();  // Переход к экрану с талоном
        g_logger.info(L"[onExtra20PaymentSuccess] Extra 20 payment flow completed successfully.");
    }

    // обработчик для сдачи вещей на доверии
    void showTrustAcceptanceWindow() {
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU; // или добавим новое состояние, но можно использовать существующее

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Заголовок "ДОБРО"
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 30, screenWidth - 100, 80,
            m_hWnd, (HMENU)ID_STATIC_TRUST_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 110, screenWidth - 100, 60,
            m_hWnd, (HMENU)ID_STATIC_TRUST_SUBTITLE, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Текст в зелёном прямоугольнике (как в +20 позиций)
        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали НА ДОВЕРИИ.\n"
            L"Вы без очереди оставляете товар и сведения о себе.\n"
            L"Мы оформим их без Вашего участия.\n"
            L"Вещам БЕЗ ОЦЕНКИ будет присвоена цена 1 рубль.\n"
            L"ВАЖНО: магазин НЕ несет ответственности за количество, качество и сохранность Ваших вещей.",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 180, 800, 200,
            m_hWnd, (HMENU)IDC_STATIC_TRUST_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        // Установим зелёный фон для этого статика (обрабатывается в WM_CTLCOLORSTATIC)

        // Кнопка "Печать талона"
        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 30;
        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Печать талона",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_TRUST_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        // Кнопка "Назад" (красная)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_TRUST_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);

        styleButtons();
        g_logger.info(L"Trust acceptance window shown");
    }

    // обработчик печати талона сдачи на доверии
    void onTrustAcceptancePrint() {
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"Trust acceptance print attempted without valid login");
            return;
        }

        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();

        g_logger.info(L"Trust acceptance print requested for client ID: " + std::to_wstring(clientId));

        auto ticketOpt = g_queueManager.getTrustAcceptance(clientId, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"Failed to get trust acceptance ticket for client " + std::to_wstring(clientId));
            return;
        }

        m_currentTicket = ticketOpt.value();

        // Печать талона с дополнительной фразой
        bool printed = g_printer.printTicketWithExtraMessage(
            m_currentTicket,
            g_authManager.getFullName(),
            g_authManager.getPhone(),
            L"(Фамилия, имя и отчество пользователя) прикрепите данный талон к пакету с товаром. Пакет оставьте у окна № " + m_currentTicket.windowNumber
        );


        if (printed) {
            g_logger.info(L"Trust acceptance ticket printed: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"Trust acceptance ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        // Показываем экран с талоном
        showTicketIssued();
    }

    // Обработчик для платной очереди
    void showPaidAcceptanceWindow() {
        // 1. Проверка авторизации
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.info(L"PaidAcceptance: user not logged in, showing registration dialog");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                // Пользователь отменил регистрацию – возврат в меню выбора очереди
                createSubmitMenu();
                return;
            }
            // После успешной регистрации токены установлены в AuthManager
            g_logger.info(L"PaidAcceptance: user registered, proceeding to paid window");
        }

        // Очищаем окно и устанавливаем новое состояние (можно использовать существующее или добавить новое,
        // но для простоты используем состояние SUBMIT_MENU, оно подходит)
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU; // или создать новое, но не обязательно

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Заголовок "ДОБРО" (верхний)
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 30, screenWidth - 100, 80,
            m_hWnd, (HMENU)ID_STATIC_PAID_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Подзаголовок "КОМИССИОННЫЙ МАГАЗИН"
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 110, screenWidth - 100, 60,
            m_hWnd, (HMENU)ID_STATIC_PAID_SUBTITLE, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Основной текст в зелёном прямоугольнике
        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали ПЛАТНЫЙ ПРИЕМ (200 руб.).\n"
            L"При выборе данной опции, очередь короче, но постановка в данную очередь возможна при оплате 200 руб.\n"
            L"Количество товаров ограничено 20 наименованиями.\n\n"
            L"Стоимость услуги 200 руб.",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 220, 800, 220,
            m_hWnd, (HMENU)IDC_STATIC_PAID_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Количество ожидающих в очереди (зелёный прямоугольник)
        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::PAID, authToken);
        std::wstring countText = L"Количество ожидающих в очереди: " + std::to_wstring(queueCount);
        HWND hQueueCount = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 300, centerY - 10, 600, 50,
            m_hWnd, (HMENU)ID_STATIC_PAID_QUEUE_COUNT, g_hInstance, nullptr);
        SendMessageW(hQueueCount, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        // Установим зелёный фон для этого статика (обрабатывается в WM_CTLCOLORSTATIC)
        // Для этого добавим обработку ID_STATIC_PAID_QUEUE_COUNT в WM_CTLCOLORSTATIC

        // Кнопка "Оплатить"
        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 70;
        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Оплатить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        // Кнопка "Назад" (красная)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);

        styleButtons();
        g_logger.info(L"Paid acceptance window shown, queue count: " + std::to_wstring(queueCount));
    }

    // Обработчик кнопки "Оплатить" для платной очереди, распечатывает талон
    void onPaidAcceptancePrint() {
        g_logger.info(L"PaidAcceptance: start printing ticket");

        // Дополнительная проверка авторизации (на случай, если окно было открыто без неё)
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"PaidAcceptance print attempted without valid login");
            createSubmitMenu();
            return;
        }

        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        int itemsCount = Config::MAX_ITEMS_GENERAL_QUEUE; // 20

        g_logger.info(L"PaidAcceptance: client ID " + std::to_wstring(clientId) +
            L", items count " + std::to_wstring(itemsCount));

        // Получение талона
        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::PAID, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"PaidAcceptance: failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }

        m_currentTicket = ticketOpt.value();
        g_logger.info(L"PaidAcceptance: ticket obtained: " + m_currentTicket.ticketNumber);

        // Печать талона
        bool printed = g_printer.printTicket(m_currentTicket,
            g_authManager.getFullName(),
            g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"PaidAcceptance: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"PaidAcceptance: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        // Показать экран с талоном
        showTicketIssued();
        g_logger.info(L"PaidAcceptance: completed successfully");
    }

    // Обработчик приема дорогого товара (>5000 руб)
    void showExpensiveAcceptanceWindow() {
        // 1. Проверка авторизации
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.info(L"ExpensiveAcceptance: user not logged in, showing registration dialog");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                // Пользователь отменил регистрацию – возврат в меню выбора очереди
                createSubmitMenu();
                return;
            }
            // После успешной регистрации токены установлены в AuthManager
            g_logger.info(L"ExpensiveAcceptance: user registered, proceeding to expensive window");
        }

        // Очищаем окно и устанавливаем состояние (используем SUBMIT_MENU для единообразия)
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Заголовок "ДОБРО" (верхний)
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"ДОБРО",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 30, screenWidth - 100, 80,
            m_hWnd, (HMENU)ID_STATIC_EXPENSIVE_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Подзаголовок "КОМИССИОННЫЙ МАГАЗИН"
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 110, screenWidth - 100, 60,
            m_hWnd, (HMENU)ID_STATIC_EXPENSIVE_SUBTITLE, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Основной текст в зелёном прямоугольнике
        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали ДОРОГОЙ ТОВАР (>5000 руб.).\n"
            L"Если у Вас есть вещи стоимостью более 5000 руб., то смело выбирайте этот вариант.\n"
            L"В течение 10-15 минут Вас примет самый опытный товаровед нашего магазина\n"
            L"(в данном случае принимаются вещи, оценочная стоимость каждой из которых более 5000 руб.).",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 220, 800, 220,
            m_hWnd, (HMENU)IDC_STATIC_EXPENSIVE_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Количество ожидающих в очереди (зелёный прямоугольник)
        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::EXPENSIVE, authToken);
        std::wstring countText = L"Количество ожидающих в очереди: " + std::to_wstring(queueCount);
        HWND hQueueCount = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 300, centerY - 10, 600, 50,
            m_hWnd, (HMENU)ID_STATIC_EXPENSIVE_QUEUE_COUNT, g_hInstance, nullptr);
        SendMessageW(hQueueCount, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // Кнопка "Взять талон"
        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 70;
        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Взять талон",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_EXPENSIVE_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        // Кнопка "Назад" (красная)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_EXPENSIVE_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);

        styleButtons();
        g_logger.info(L"Expensive acceptance window shown, queue count: " + std::to_wstring(queueCount));
    }

    // Обработчик кнопки "Взять талон" для дорогих вещей (> 5000 руб)
    void onExpensiveAcceptancePrint() {
        g_logger.info(L"ExpensiveAcceptance: start printing ticket");

        // Дополнительная проверка авторизации
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"ExpensiveAcceptance print attempted without valid login");
            createSubmitMenu();
            return;
        }

        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        int itemsCount = 1; // для дорогого товара – 1 позиция

        g_logger.info(L"ExpensiveAcceptance: client ID " + std::to_wstring(clientId) +
            L", items count " + std::to_wstring(itemsCount));

        // Получение талона
        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::EXPENSIVE, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"ExpensiveAcceptance: failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }

        m_currentTicket = ticketOpt.value();
        g_logger.info(L"ExpensiveAcceptance: ticket obtained: " + m_currentTicket.ticketNumber);

        // Печать талона
        bool printed = g_printer.printTicket(m_currentTicket,
            g_authManager.getFullName(),
            g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"ExpensiveAcceptance: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"ExpensiveAcceptance: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        // Показать экран с талоном
        showTicketIssued();
        g_logger.info(L"ExpensiveAcceptance: completed successfully");
    }

public:
    HWND m_hWnd;

    MainWindow() : m_currentState(WindowState::MAIN_MENU),
        m_currentTicketLabel(nullptr),
        m_hEditConsignorId(nullptr),
        m_hStaticNameDisplay(nullptr),
        m_hStaticDailyCount(nullptr),
        m_hStaticDescription(nullptr),
        m_hStaticPrompt(nullptr),
        m_hBtnNext(nullptr),
        m_hBtnPrintTicket(nullptr),
        m_hBtnResetId(nullptr),
        m_hBtnClear(nullptr),
        m_hSalesListView(nullptr),
        m_hSalesStatusLabel(nullptr),
        m_hClientInfoLabel(nullptr),   
        m_hSalesBackBtn(nullptr),
        m_currentClientId(0),
        m_nameConfirmed(false),
        m_hConsignorInfoLabel(nullptr) {
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
        case WM_SALES_LOADING_START:
            // Устанавливаем статус загрузки (в нижний элемент)
            if (m_hSalesStatusLabel) {
                SetWindowTextW(m_hSalesStatusLabel, L"Загрузка данных...");
                InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
                g_logger.info(L"WM_SALES_LOADING_START: status set to 'Загрузка данных...'");
            }
            break;
        case WM_SALES_DATA_READY:
        {
            auto* respPtr = reinterpret_cast<std::optional<json>*>(lParam);
            if (respPtr) {
                onSalesDataReady(*respPtr);
                delete respPtr;
            }
            break;
        }
        // ===== ОБРАБОТКА СООБЩЕНИЙ ДЛЯ ОКНА "Узнать номер комитента" =====
        case WM_CONSIGNOR_LOADING_START:
            // При необходимости можно обновить статус, но у нас уже установлено "Загрузка..."
            if (m_hConsignorInfoLabel) {
                SetWindowTextW(m_hConsignorInfoLabel, L"Загрузка данных...");
                InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
                g_logger.info(L"WM_CONSIGNOR_LOADING_START: status set to 'Загрузка данных...'");
            }
            break;
        case WM_CONSIGNOR_DATA_READY:
        {
            auto* respPtr = reinterpret_cast<std::optional<json>*>(lParam);
            if (respPtr) {
                onConsignorDataReady(*respPtr);
                delete respPtr;
            }
            else {
                g_logger.error(L"WM_CONSIGNOR_DATA_READY: null pointer received");
            }
            break;
        }
        
        }
    }

    void onCommand(int cmd) {
        if (cmd >= ID_BTN_DIGIT_0 && cmd <= ID_BTN_DIGIT_9) {
            if (m_hEditConsignorId && IsWindowEnabled(m_hEditConsignorId)) {
                int digit = cmd - ID_BTN_DIGIT_0;
                wchar_t ch = L'0' + digit;
                wchar_t str[2] = { ch, L'\0' };
                int len = GetWindowTextLengthW(m_hEditConsignorId);
                SendMessageW(m_hEditConsignorId, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                SendMessageW(m_hEditConsignorId, EM_REPLACESEL, TRUE, (LPARAM)str);
            }
            return;
        }

        switch (cmd) {
        case ID_BTN_REGISTER:
        {
            // Открываем окно входа (LoginWindow)
            LoginWindow loginWnd;
            bool loginSuccess = loginWnd.show(m_hWnd);
            if (loginSuccess) {
                // Успешный вход – обновляем главное меню (чтобы показать приветствие)
                createMainMenu();
                g_logger.info(L"MainWindow: login successful, main menu refreshed");
            }
            else {
                // Если была попытка входа (был введён номер), но она не удалась,
                // значит пользователь не найден – открываем страницу "Я первый раз"
                if (g_authManager.wasLoginAttempted()) {
                    g_logger.info(L"MainWindow: login failed, redirecting to First Time page");
                    // Сбрасываем флаг попытки, чтобы не зациклиться
                    g_authManager.setLoginAttempted(false);
                    // Открываем страницу "Я первый раз (оформление договора)"
                    handleFirstTime();
                }
                else {
                    // Просто закрыли окно входа (отмена) – ничего не делаем
                    g_logger.info(L"MainWindow: login window cancelled");
                }
            }
            break;
        }
        break;

        case ID_BTN_SUBMIT_ITEMS:
            createSubmitMenu();
            break;

        case ID_BTN_GENERAL_QUEUE:
            createGeneralQueueInput();
            break;

        case ID_BTN_FIRST_TIME:
            handleFirstTime();
            break;

        case ID_BTN_EXTRA_20:
            showExtra20Payment();
            break;
        
        case ID_BTN_TRUST:
            showTrustAcceptanceWindow();
            break;

        case ID_BTN_TRUST_PRINT:
            onTrustAcceptancePrint();
            break;

        case ID_BTN_TRUST_BACK:
            // Возврат на экран выбора типа сдачи товара
            createSubmitMenu();
            break;

        case ID_BTN_PAID:
            showPaidAcceptanceWindow();
            break;

        case ID_BTN_PAID_PRINT:
            onPaidAcceptancePrint();
            break;

        case ID_BTN_PAID_BACK:
            // Возврат на экран выбора типа сдачи товара
            createSubmitMenu();
            break;

        case ID_BTN_EXPENSIVE:
            showExpensiveAcceptanceWindow();
            break;

        case ID_BTN_EXPENSIVE_PRINT:
            onExpensiveAcceptancePrint();
            break;

        case ID_BTN_EXPENSIVE_BACK:
            // Возврат на экран выбора типа сдачи товара
            createSubmitMenu();
            break;

        case ID_BTN_PAY_CARD:
        case ID_BTN_PAY_QR:
            onExtra20PaymentSuccess();
            break;

        case ID_BTN_BACK:
            if (m_currentState == WindowState::SUBMIT_MENU ||
                m_currentState == WindowState::TICKET_ISSUED) {
                createMainMenu();
            }
            else if (m_currentState == WindowState::GENERAL_QUEUE_INPUT) {
                createSubmitMenu();
            }
            else if (m_currentState == WindowState::EXTRA_20_PAYMENT) {
                createSubmitMenu();
            }
            else if (m_currentState == WindowState::ADDRESSES) {  
                createMainMenu();
            }
            else if (m_currentState == WindowState::CONSIGNOR_LOOKUP) {
                createMainMenu();
            }
            break;

        case ID_BTN_CLOSE:
            PostQuitMessage(0);
            break;

        case ID_BTN_CLEAR:
            if (m_hEditConsignorId) {
                SetWindowTextW(m_hEditConsignorId, L"");
                SetFocus(m_hEditConsignorId);
            }
            break;

        case ID_BTN_NEXT:
            onGeneralQueueNext();
            break;

        case ID_BTN_RESET_ID:
            onGeneralQueueResetId();
            break;

        case ID_BTN_PRINT_TICKET:
            onGeneralQueuePrint();
            break;

        case ID_BTN_MY_SALES:
            createMySales();
            break;

        case ID_BTN_SALES_BACK:
            createMainMenu();
            break;

        case ID_BTN_ADDRESSES:
            createAddressesWindow();
            break;

        case ID_BTN_CONSIGNOR_NUMBER:
            createConsignorNumber();
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
extern MainWindow g_mainWindow;

// Window procedure
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hBrushBtn = CreateSolidBrush(Config::ACCENT_COLOR);
        g_hBrushGreen = CreateSolidBrush(Config::PRIMARY_COLOR);   // зеленая кисть
        g_hBrushBackBtn = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        g_hBrushWindow = CreateSolidBrush(RGB(255, 255, 255));
        g_hFontTitle = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_hFontButton = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_hFontEdit = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_hFontLabel = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        g_mainWindow.m_hWnd = hWnd;
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
        // Красные кнопки
        if (btnId == ID_BTN_BACK || btnId == ID_BTN_CLOSE || btnId == ID_BTN_RESET_ID) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)g_hBrushBackBtn;
        }
        // Зелёные кнопки (оплата, далее, печать)
        if (btnId == ID_BTN_PAY_CARD || btnId == ID_BTN_PAY_QR ||
            btnId == ID_BTN_NEXT || btnId == ID_BTN_PRINT_TICKET) {
            SetBkColor(hdc, Config::PRIMARY_COLOR);
            return (LRESULT)g_hBrushGreen;
        }
        // Остальные – оранжевые (акцентные)
        SetBkColor(hdc, Config::ACCENT_COLOR);
        return (LRESULT)g_hBrushBtn;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        DWORD id = GetDlgCtrlID(hStatic);

        // ================================================================
        // ИСПРАВЛЕНИЕ: для статика с информацией о комитенте устанавливаем
        // белый фон, чтобы старый текст не наслаивался
        // ================================================================
        if (id == ID_STATIC_CONSIGNOR_INFO) {
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)g_hBrushWindow;
        }

        if (id == IDC_STATIC_EXTRA20_TEXT || id == IDC_STATIC_TRUST_TEXT ||
            id == IDC_STATIC_PAID_TEXT || id == ID_STATIC_PAID_QUEUE_COUNT ||
            id == IDC_STATIC_EXPENSIVE_TEXT || id == ID_STATIC_EXPENSIVE_QUEUE_COUNT) {
            SetBkColor(hdc, Config::PRIMARY_COLOR);
            SetTextColor(hdc, RGB(255, 255, 255));
            return (LRESULT)g_hBrushGreen;
        }

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

    // Добавляем обработку всех пользовательских сообщений
    case WM_SALES_LOADING_START:
    case WM_SALES_DATA_READY:
    case WM_CONSIGNOR_LOADING_START:
    case WM_CONSIGNOR_DATA_READY:
        g_mainWindow.handleMessage(msg, wParam, lParam);
        return 0;

    case WM_DESTROY:
        DeleteObject(g_hBrushBtn);
        DeleteObject(g_hBrushGreen);
        DeleteObject(g_hBrushBackBtn);
        DeleteObject(g_hBrushWindow);
        DeleteObject(g_hFontTitle);
        DeleteObject(g_hFontButton);
        DeleteObject(g_hFontEdit);
        DeleteObject(g_hFontLabel);
        g_hCloseBtn = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
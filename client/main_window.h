// main_window.h
// Главный экран терминала киоска самообслуживания.
// ПРОДАКШН-ВЕРСИЯ. Архитектура, структура, логика и математика НЕ меняются.
// Внесённые исправления по заданию:
//  1) Добавлено отображение изображения статуи (rc.png) диаметром 3 см (114 px) над заголовком.
//  2) Все элементы под заголовком (иконки, текст, кнопки) смещены вниз ровно на 3 см (114 px).
//  3) Текст превью формируется с разделителями строк "\r\n".
//  4) В WM_CTLCOLORSTATIC добавлена ветка для ID_EDIT_TRUST_PREVIEW.

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <optional>
#include <nlohmann/json.hpp>
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
static HBITMAP g_hBmpIconNoCash = nullptr;
static HBITMAP g_hBmpIconKiosk = nullptr;
static HBITMAP g_hBmpIconWallet = nullptr;
static HBITMAP g_hBmpStatue = nullptr;      // Дескриптор битмапа статуи "Рабочий и Колхозница"

// Математическая константа: 3 см в пикселях при 96 DPI (30 мм / 25.4 * 96 ≈ 113.38)
// Используем 114 для целочисленного выравнивания и сохранения пропорций.
constexpr int SHIFT_3CM_PX = 114;
constexpr int STATUE_SIZE_PX = 114;

// Control IDs
#define ID_BTN_REGISTER 1001
#define ID_BTN_SUBMIT_ITEMS 1002
#define ID_BTN_MY_SALES 1003
#define ID_BTN_ADDRESSES 1004
#define ID_BTN_CONSIGNOR_NUMBER 1005
#define ID_BTN_OTHER_SERVICES 1006
#define ID_BTN_BACK 1007
#define ID_BTN_CLOSE 1008
#define ID_BTN_LOGOUT 1009
#define ID_BTN_GENERAL_QUEUE 2001
#define ID_BTN_FIRST_TIME 2002
#define ID_BTN_EXTRA_20 2003
#define ID_BTN_TRUST 2004
#define ID_BTN_PAID 2005
#define ID_BTN_EXPENSIVE 2006
#define ID_EDIT_PHONE 3001
#define ID_EDIT_CONSIGNOR 3002
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
#define ID_BTN_PAY_CARD 5010
#define ID_BTN_PAY_QR   5011
#define IDC_STATIC_EXTRA20_TEXT 5012
#define ID_STATIC_EXTRA20_TITLE 5013
#define ID_STATIC_EXTRA20_SUBTITLE 5014
#define ID_STATIC_TRUST_TITLE      6001
#define ID_STATIC_TRUST_SUBTITLE   6002
#define IDC_STATIC_TRUST_TEXT      6003
#define ID_BTN_TRUST_PRINT         6004
#define ID_BTN_TRUST_BACK          6005
#define ID_EDIT_TRUST_PREVIEW      6018
#define ID_BTN_PAID_PRINT           6006
#define ID_BTN_PAID_BACK            6007
#define ID_STATIC_PAID_TITLE        6008
#define ID_STATIC_PAID_SUBTITLE     6009
#define IDC_STATIC_PAID_TEXT        6010
#define ID_STATIC_PAID_QUEUE_COUNT  6011
#define ID_BTN_EXPENSIVE_PRINT       6012
#define ID_BTN_EXPENSIVE_BACK        6013
#define ID_STATIC_EXPENSIVE_TITLE    6014
#define ID_STATIC_EXPENSIVE_SUBTITLE 6015
#define IDC_STATIC_EXPENSIVE_TEXT    6016
#define ID_STATIC_EXPENSIVE_QUEUE_COUNT 6017
#define ID_STATIC_MAIN_TITLE_1  6019
#define ID_STATIC_MAIN_TITLE_2  6020
#define ID_BTN_SALES_BACK 7001
#define ID_STATIC_SALES_STATUS 7002
#define ID_LIST_SALES 7003
#define ID_STATIC_CONSIGNOR_INFO 8001
#define IDB_ICON_NO_CASH      9001
#define IDB_ICON_KIOSK        9002
#define IDB_ICON_WALLET       9003
#define ID_STATIC_ICON_1      9004
#define ID_STATIC_ICON_2      9005
#define ID_STATIC_ICON_3      9006
#define ID_STATIC_ICON_CAP_1  9007
#define ID_STATIC_ICON_CAP_2  9008
#define ID_STATIC_ICON_CAP_3  9009
#define ID_STATIC_STATUE      9010  // ID контрола для изображения статуи
#define IDB_STATUE            9011  // ID ресурса для изображения статуи

#define WM_SALES_DATA_READY (WM_USER + 200)
#define WM_SALES_LOADING_START (WM_USER + 201)
#define WM_CONSIGNOR_DATA_READY (WM_USER + 202)
#define WM_CONSIGNOR_LOADING_START (WM_USER + 203)

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
    HWND m_hEditConsignorId;
    HWND m_hStaticNameDisplay;
    HWND m_hStaticDailyCount;
    HWND m_hStaticDescription;
    HWND m_hStaticPrompt;
    HWND m_hBtnNext;
    HWND m_hBtnPrintTicket;
    HWND m_hBtnResetId;
    HWND m_hBtnClear;
    HWND m_hSalesListView;
    HWND m_hSalesStatusLabel;
    HWND m_hClientInfoLabel;
    HWND m_hSalesBackBtn;
    std::vector<HWND> m_digitButtons;
    int m_currentClientId;
    std::wstring m_currentClientName;
    std::wstring m_currentClientPhone;
    bool m_nameConfirmed;
    HWND m_hConsignorInfoLabel;

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

    // Функция для создания стандартного заголовка с эмблемой (используется на всех экранах)
    void createStandardHeader() {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int titleCenterX = screenWidth / 2;  // Строго по центру экрана

        // Эмблема
        int statueY = 15;
        HWND hStatue = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_CENTERIMAGE,
            titleCenterX - (STATUE_SIZE_PX / 2), statueY, STATUE_SIZE_PX, STATUE_SIZE_PX,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_STATUE, g_hInstance, nullptr);
        if (g_hBmpStatue) {
            SendMessageW(hStatue, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hBmpStatue);
        }

        // Заголовок "СОВЕТСКИЙ"
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 129, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_1, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Заголовок "Комиссионный магазин"
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 189, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_2, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
    }

    void createMainMenu() {
        clearWindow();
        m_currentState = WindowState::MAIN_MENU;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int btnWidth = 400;
        int btnHeight = Config::BUTTON_HEIGHT;
        int centerX = (screenWidth - btnWidth) / 2;

        // centerX для эмблемы и заголовка - СТРОГО ПО ЦЕНТРУ ЭКРАНА
        int titleCenterX = screenWidth / 2;

        g_logger.info(L"createMainMenu: started, screen=" + std::to_wstring(screenWidth) +
            L"x" + std::to_wstring(screenHeight) + L", btnHeight=" + std::to_wstring(btnHeight));

        // ---------------------------------------------------------------
        // ИЗОБРАЖЕНИЕ СТАТУИ: размещается НАД заголовком.
        // Диаметр 3 см = 114 пикселей. Центрируется по горизонтали.
        // ---------------------------------------------------------------
        int statueY = 15;
        HWND hStatue = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_CENTERIMAGE,
            titleCenterX - (STATUE_SIZE_PX / 2), statueY, STATUE_SIZE_PX, STATUE_SIZE_PX,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_STATUE, g_hInstance, nullptr);

        if (g_hBmpStatue) {
            SendMessageW(hStatue, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hBmpStatue);
            g_logger.info(L"createMainMenu: statue bitmap applied successfully");
        }
        else {
            g_logger.warning(L"createMainMenu: g_hBmpStatue is nullptr, image not loaded");
        }

        // ---------------------------------------------------------------
        // ЗАГОЛОВОК СТРОКА 1: "СОВЕТСКИЙ"
        // Смещен вниз на SHIFT_3CM_PX (114 px) относительно исходной позиции (15).
        // Итоговая Y = 15 + 114 = 129.
        // ---------------------------------------------------------------
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 129, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_1, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        g_logger.info(L"createMainMenu: title1 'СОВЕТСКИЙ' created at y=129 (shifted +114px)");

        // ---------------------------------------------------------------
        // ЗАГОЛОВОК СТРОКА 2: "Комиссионный магазин"
        // Смещен вниз на SHIFT_3CM_PX (114 px) относительно исходной позиции (75).
        // Итоговая Y = 75 + 114 = 189.
        // ---------------------------------------------------------------
        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 189, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_2, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        g_logger.info(L"createMainMenu: title2 'Комиссионный магазин' created at y=189 (shifted +114px)");

        const int iconSize = 100;
        const int colWidth = 300;
        const int iconsStartX = (screenWidth - colWidth * 3) / 2;

        // Иконки смещены вниз на SHIFT_3CM_PX (114 px). Исходная Y = 145. Итоговая Y = 259.
        const int iconY = 145 + SHIFT_3CM_PX;
        const int capY = iconY + iconSize + 5;
        const int capHeight = 90;

        const HBITMAP icons[3] = { g_hBmpIconNoCash, g_hBmpIconKiosk, g_hBmpIconWallet };
        const int iconIds[3] = { ID_STATIC_ICON_1, ID_STATIC_ICON_2, ID_STATIC_ICON_3 };
        const int capIds[3] = { ID_STATIC_ICON_CAP_1, ID_STATIC_ICON_CAP_2, ID_STATIC_ICON_CAP_3 };
        const wchar_t* captions[3] = {
            L"Здесь нельзя оплачивать наличными.",
            L"Пользуйтесь терминалами самообслуживания. Не стойте в очередях!",
            L"Проверяйте свои продажи самостоятельно!"
        };

        for (int i = 0; i < 3; ++i) {
            int colX = iconsStartX + i * colWidth;
            if (icons[i]) {
                HWND hIcon = CreateWindowExW(0, L"STATIC", L"",
                    WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_CENTERIMAGE,
                    colX + (colWidth - iconSize) / 2, iconY, iconSize, iconSize,
                    m_hWnd, (HMENU)(INT_PTR)iconIds[i], g_hInstance, nullptr);
                SendMessageW(hIcon, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)icons[i]);
            }
            else {
                g_logger.error(L"createMainMenu: bitmap for icon " + std::to_wstring(i + 1) + L" is nullptr");
            }

            HWND hCap = CreateWindowExW(0, L"STATIC", captions[i],
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                colX, capY, colWidth, capHeight,
                m_hWnd, (HMENU)(INT_PTR)capIds[i], g_hInstance, nullptr);
            SendMessageW(hCap, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        }

        // Остальные элементы (приветствие, инструкции, кнопки) используют относительные координаты 
        // от capY, поэтому они автоматически смещаются вниз на требуемые 3 см.
        int greetingY = capY + capHeight + 10;
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
            const wchar_t* instruction = L"Нажмите кнопку «Регистрация» или в секции «Сдать товар» выберете «Я в первый раз (оформление договора)» для оформления договора у товароведа";
            HWND hInstruction = CreateWindowExW(0, L"STATIC", instruction,
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                centerX, greetingY, btnWidth, greetingHeight,
                m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(hInstruction, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }

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

        if (g_authManager.isLoggedIn()) {
            m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Выход",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                centerX, startY + (btnHeight + 10) * 6, btnWidth, btnHeight,
                m_hWnd, (HMENU)(INT_PTR)ID_BTN_LOGOUT, g_hInstance, nullptr));
        }

        styleButtons();
        g_logger.info(L"createMainMenu: menu buttons created, startY=" + std::to_wstring(startY) + L" (shifted +114px)");
    }

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

        // Стандартный заголовок с эмблемой
        createStandardHeader();

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"Выберите тип очереди",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 260, btnWidth, 50,  // Y = 189 + 60 + 11 = 260
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int startY = 260 + 50 + 20;  // 330


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

    void loadSalesData() {
        g_logger.info(L"loadSalesData: started");
        PostMessageW(m_hWnd, WM_SALES_LOADING_START, 0, 0);
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
        std::wstring fullName = g_authManager.getFullName();
        std::wstring clientInfo = !fullName.empty() ? (L"Клиент " + fullName + L" авторизован") : L"Клиент авторизован";
        SetWindowTextW(m_hClientInfoLabel, clientInfo.c_str());
        g_logger.info(L"loadSalesData: client info set to: " + clientInfo);

        int clientId = g_authManager.getClientId();
        g_logger.info(L"loadSalesData: clientId = " + std::to_wstring(clientId));

        std::thread([this, clientId, authToken]() {
            std::wstring path = L"/api/v1/clients/sales?client_id=" + std::to_wstring(clientId);
            auto response = g_httpsClient.get(path, authToken);
            PostMessageW(m_hWnd, WM_SALES_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<nlohmann::json>(response)));
            }).detach();
        g_logger.info(L"loadSalesData: async request sent");
    }

    void createMySales() {
        clearWindow();
        m_currentState = WindowState::MY_SALES;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        
        // Стандартный заголовок с эмблемой
        createStandardHeader();
        int top = 270;  // После заголовка (189 + 60 + 21 = 270)

        m_hClientInfoLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hClientInfoLabel, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        top += 60;

        m_hSalesStatusLabel = CreateWindowExW(0, L"STATIC", L"Загрузка данных...",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, (HMENU)ID_STATIC_SALES_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hSalesStatusLabel, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        top += 60;

        int listHeight = screenHeight - top - 120;
        m_hSalesListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
            20, top, screenWidth - 40, listHeight,
            m_hWnd, (HMENU)ID_LIST_SALES, g_hInstance, nullptr);
        SendMessageW(m_hSalesListView, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

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

        int btnWidth = 200, btnHeight = 60;
        int backY = screenHeight - btnHeight - 30;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, backY, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_SALES_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        loadSalesData();
    }

    void createAddressesWindow() {
        g_logger.info(L"createAddressesWindow: ENTRY POINT");
        clearWindow();
        m_currentState = WindowState::ADDRESSES;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        

        // Стандартный заголовок с эмблемой
        createStandardHeader();
        int top = 270;  // После заголовка

        HWND hListTitle = CreateWindowExW(0, L"STATIC", L"Перечень и адреса магазинов СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hListTitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 70;

        const wchar_t* addresses[] = {
            L"«магазин СОВЕТСКИЙ – Садовая»: город Санкт-Петербург, ул. Садовая д.28/30"
        };
        constexpr size_t numAddresses = sizeof(addresses) / sizeof(addresses[0]);
        int lineHeight = 50;

        for (size_t i = 0; i < numAddresses; ++i) {
            int currentY = top + static_cast<int>(i) * lineHeight;
            HWND hAddr = CreateWindowExW(0, L"STATIC", addresses[i],
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                50, currentY, screenWidth - 100, lineHeight,
                m_hWnd, nullptr, g_hInstance, nullptr);
            if (hAddr) {
                SendMessageW(hAddr, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
            }
            else {
                g_logger.error(L"createAddressesWindow: FAILED to create address[" + std::to_wstring(i) + L"]");
            }
        }

        top += 3 * lineHeight + 40;
        int btnWidth = 200, btnHeight = 60;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, top, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        g_logger.info(L"Addresses window created");
    }

    void createConsignorNumber() {
        clearWindow();
        m_currentState = WindowState::CONSIGNOR_LOOKUP;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        

        // Стандартный заголовок с эмблемой
        createStandardHeader();
        int top = 270;  // После заголовка

        m_hConsignorInfoLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 100,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_CONSIGNOR_INFO, g_hInstance, nullptr);
        SendMessageW(m_hConsignorInfoLabel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 120;

        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Пользователь не авторизован");
            int btnWidth = 200, btnHeight = 60;
            HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                centerX - btnWidth / 2, top, btnWidth, btnHeight,
                m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
            m_buttons.push_back(hBack);
            SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
            return;
        }

        SetWindowTextW(m_hConsignorInfoLabel, L"Загрузка данных...");
        int btnWidth = 200, btnHeight = 60;
        int backY = screenHeight - btnHeight - 30;
        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, backY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);
        SendMessageW(hBack, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        loadConsignorData();
    }

    void loadConsignorData() {
        g_logger.info(L"loadConsignorData: started");
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
        PostMessageW(m_hWnd, WM_CONSIGNOR_LOADING_START, 0, 0);

        std::thread([this, authToken]() {
            try {
                std::wstring path = L"/api/v1/clients/me";
                auto response = g_httpsClient.get(path, authToken);
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<nlohmann::json>(response)));
            }
            catch (const std::exception& e) {
                g_logger.error(L"Consignor thread exception: " + utf8_to_wstring(e.what()));
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<nlohmann::json>(std::nullopt)));
            }
            catch (...) {
                g_logger.error(L"Consignor thread unknown exception");
                PostMessageW(m_hWnd, WM_CONSIGNOR_DATA_READY, 0, reinterpret_cast<LPARAM>(new std::optional<nlohmann::json>(std::nullopt)));
            }
            }).detach();
        g_logger.info(L"loadConsignorData: async request sent");
    }

    void onConsignorDataReady(const std::optional<nlohmann::json>& response) {
        g_logger.info(L"onConsignorDataReady: received response");
        if (!response) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Ошибка соединения с сервером");
            g_logger.error(L"onConsignorDataReady: no response (connection error)");
            InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
            return;
        }
        if (!response->contains("id") || !(*response)["id"].is_number_integer()) {
            SetWindowTextW(m_hConsignorInfoLabel, L"Некорректный ответ сервера");
            g_logger.error(L"onConsignorDataReady: invalid response format (missing id)");
            InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
            return;
        }
        int clientId = (*response)["id"].get<int>();
        std::string nameUtf8 = response->value("name", "");
        std::wstring fullName = utf8_to_wstring(nameUtf8);
        if (fullName.empty()) {
            fullName = g_authManager.getFullName();
            g_logger.warning(L"onConsignorDataReady: name not in response, using local value");
        }
        std::wstring infoText = fullName + L"\nВаш номер комитента: " + std::to_wstring(clientId);
        SetWindowTextW(m_hConsignorInfoLabel, infoText.c_str());
        InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
        g_logger.info(L"onConsignorDataReady: displayed info for client " + std::to_wstring(clientId));
    }

    void onSalesDataReady(const std::optional<nlohmann::json>& response) {
        g_logger.info(L"onSalesDataReady: received response");
        if (!response) {
            SetWindowTextW(m_hSalesStatusLabel, L"Ошибка соединения с сервером");
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.error(L"onSalesDataReady: no response (connection error)");
            return;
        }
        if (!response->contains("sales") || !(*response)["sales"].is_array()) {
            SetWindowTextW(m_hSalesStatusLabel, L"Некорректный ответ сервера");
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.error(L"onSalesDataReady: invalid response format");
            return;
        }
        auto& sales = (*response)["sales"];
        if (sales.empty()) {
            SetWindowTextW(m_hSalesStatusLabel, L"Нет данных о продажах");
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.info(L"onSalesDataReady: no sales data");
            return;
        }
        ListView_DeleteAllItems(m_hSalesListView);
        int index = 0;
        for (const auto& item : sales) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = index;
            std::vector<std::wstring> columns;
            columns.push_back(utf8_to_wstring(item.value("report_date", "")));
            columns.push_back(utf8_to_wstring(item.value("item_number", "")));
            columns.push_back(utf8_to_wstring(item.value("item_name", "")));
            columns.push_back(std::to_wstring(item.value("price", 0.0)));
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
        InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
        g_logger.info(L"onSalesDataReady: loaded " + std::to_wstring(index) + L" records");
    }

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
        

        // Стандартный заголовок с эмблемой (единый для всех экранов)
        createStandardHeader();
		int top = 260;  // После заголовка  

        const wchar_t* descText = L"Вы выбрали ОБЩУЮ ОЧЕРЕДЬ. Вы получаете талончик электронной очереди и ожидаете своего времени приема.\n"
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

        const wchar_t* promptText = L"Введите номер комитента и нажмите кнопку Далее.\nЕсли номер ввели неверно, нажмите кнопку Сбросить.";
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
            (type == QueueType::PAID || type == QueueType::GENERAL) ? Config::MAX_ITEMS_GENERAL_QUEUE : 1,
            g_authManager.getAuthToken()
        );
        if (ticketOpt) {
            m_currentTicket = ticketOpt.value();
            showTicketIssued();
            g_printer.printTicket(m_currentTicket, clientOpt->name, clientOpt->phone);
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
        HFONT hFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SendMessageW(m_currentTicketLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"ВЕРНУТЬСЯ В МЕНЮ",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 150, 450, 300, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));
        styleButtons();
    }

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

    void onGeneralQueuePrint() {
        if (!m_nameConfirmed || m_currentClientId == 0) {
            MessageBoxW(m_hWnd, L"Сначала подтвердите ID комитента", L"Ошибка", MB_OK);
            return;
        }
        std::wstring authToken = g_authManager.getAuthToken();
        auto ticketOpt = g_queueManager.getTicket(m_currentClientId, QueueType::GENERAL, Config::MAX_ITEMS_GENERAL_QUEUE, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"Failed to get ticket for client id " + std::to_wstring(m_currentClientId));
            return;
        }
        m_currentTicket = ticketOpt.value();
        bool printed = g_printer.printTicket(m_currentTicket, m_currentClientName, m_currentClientPhone);
        if (printed) {
            g_logger.info(L"Ticket printed: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"Ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }
        showTicketIssued();
    }

    // ========================================================================
   // ИСПРАВЛЕННЫЙ МЕТОД: handleFirstTime
   // ========================================================================
   // ПРИЧИНА ИСПРАВЛЕНИЯ:
   // 1. Метод жёстко задавал ticket.position = 1, поэтому клиент всегда
   //    отображал позицию 1, независимо от количества талонов в очереди.
   // 2. Метод не использовал информацию о позиции, возвращаемую сервером.
   // 3. Метод не парсил окно и время ожидания из ответа сервера.
   //
   // РЕШЕНИЕ:
   // 1. Парсим позицию из ответа сервера (поле "position").
   // 2. Парсим окно и время ожидания из ответа сервера.
   // 3. Если сервер не вернул позицию (старая версия сервера), используем 1
   //    как fallback для обратной совместимости.
   // 4. Логика печати и отображения талона остаётся без изменений.
   // ========================================================================
    void handleFirstTime() {
        g_logger.info(L"handleFirstTime: started");

        nlohmann::json request;
        request["window"] = "1";

        auto response = g_httpsClient.post(L"/api/v1/queue/first_time/create", request, L"");
        if (!response || !response->contains("ticket_number")) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"FirstTime: failed to create ticket (no response or missing ticket_number)");
            return;
        }

        std::string ticketNumberUtf8 = (*response)["ticket_number"].get<std::string>();
        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        QueueTicket ticket;
        ticket.ticketNumber = ticketNumber;
        ticket.type = QueueType::FIRST_TIME;

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Парсим позицию из ответа сервера.
        // Если сервер не вернул позицию (старая версия сервера), используем 1
        // как fallback для обратной совместимости.
        // =====================================================================
        ticket.position = response->value("position", 1);
        ticket.itemsCount = 1;

        // Парсим окно из ответа сервера (fallback: "1")
        std::string windowUtf8 = response->value("window_number", "1");
        ticket.windowNumber = utf8_to_wstring(windowUtf8);

        // Парсим время ожидания из ответа сервера (fallback: 0)
        ticket.estimatedWaitTime = response->value("wait_time_minutes", 0);

        ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

        g_logger.info(L"FirstTime: ticket composed: " + ticketNumber +
            L", position=" + std::to_wstring(ticket.position) +
            L", window=" + ticket.windowNumber +
            L", waitTime=" + std::to_wstring(ticket.estimatedWaitTime));

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

        g_logger.info(L"handleFirstTime: completed successfully");
    }

    void showExtra20Payment() {
        clearWindow();
        m_currentState = WindowState::EXTRA_20_PAYMENT;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Стандартный заголовок с эмблемой (единый для всех экранов)
        createStandardHeader();

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"СДАЧА +20 товаров",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 260, screenWidth - 100, 60,  // Y = 260 (после стандартного заголовка)
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

    void onExtra20PaymentSuccess() {
        g_logger.info(L"[onExtra20PaymentSuccess] Starting extra 20 payment flow...");
        std::wstring authToken = g_authManager.getAuthToken();
        int clientId = g_authManager.getClientId();
        bool isLogged = g_authManager.isLoggedIn();

        if (!isLogged || clientId == 0) {
            g_logger.info(L"[onExtra20PaymentSuccess] No valid login. Showing registration dialog...");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                g_logger.warning(L"[onExtra20PaymentSuccess] Registration cancelled by user");
                return;
            }
            authToken = g_authManager.getAuthToken();
            clientId = g_authManager.getClientId();
        }
        if (clientId == 0) {
            MessageBoxW(m_hWnd, L"Не удалось определить ID клиента. Попробуйте войти заново.", L"Ошибка", MB_OK);
            g_logger.error(L"[onExtra20PaymentSuccess] clientId is still 0");
            return;
        }

        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::EXTRA_20, 20, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"[onExtra20PaymentSuccess] Failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }
        m_currentTicket = ticketOpt.value();
        bool printed = g_printer.printTicket(m_currentTicket, g_authManager.getFullName(), g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"[onExtra20PaymentSuccess] Ticket printed: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"[onExtra20PaymentSuccess] Ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }
        showTicketIssued();
        g_logger.info(L"[onExtra20PaymentSuccess] Extra 20 payment flow completed successfully.");
    }

    void showTrustAcceptanceWindow() {
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Стандартный заголовок с эмблемой (единый для всех экранов)
        createStandardHeader();

        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали НА ДОВЕРИИ.\n"
            L"Вы без очереди оставляете товар и сведения о себе.\n"
            L"Мы оформим их без Вашего участия.\n"
            L"Вещам БЕЗ ОЦЕНКИ будет присвоена цена 1 рубль.\n"
            L"ВАЖНО: магазин НЕ несет ответственности за количество, качество и сохранность Ваших вещей.",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 260, 800, 180,
            m_hWnd, (HMENU)IDC_STATIC_TRUST_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        const bool logged = g_authManager.isLoggedIn() && g_authManager.getClientId() != 0;
        const std::wstring idStr = logged ? std::to_wstring(g_authManager.getClientId()) : std::wstring(L"(id пользователя)");
        const std::wstring nameStr = logged ? g_authManager.getFullName() : std::wstring(L"(Ф.И.О. пользователя)");
        const std::wstring phoneStr = logged ? g_authManager.getPhone() : std::wstring(L"(номер телефона пользователя)");

        std::wstringstream ps;
        ps << L"КОМИССИОННЫЙ МАГАЗИН\r\n";
        ps << L"СОВЕТСКИЙ\r\n\r\n";
        ps << L"Комитент №" << idStr << L"\r\n";
        ps << L"Я, " << nameStr << L" доверяю Вам оценить вещи на Ваше усмотрение, без согласования цен со мной. Согласен на обработку моих персональных данных.\r\n";
        ps << L"Условия:\r\n";
        ps << L"Комиссионный магазин принимает товар по услуге «Доверие»,\r\n";
        ps << L"без проверки содержимого пакетов, коробок и иной упаковки.\r\n";
        ps << L"Магазин не несет ответственности за вложенные в пакет вещи,\r\n";
        ps << L"их наличие или отсутствие, сохранность пакета и его возврат.\r\n";
        ps << L"Вещи, которые товаровед не примет по состоянию, качеству или др. причинам,\r\n";
        ps << L"автоматически отправляются в категорию «на утилизацию» и выставляются\r\n";
        ps << L"за один рубль без составления перечня.\r\n\r\n";
        ps << L"Подписывая данный бланк, я подтверждаю, что ознакомлен \r\n";
        ps << L"с условиями и согласен с ними. Также я соглашаюсь с возможной \r\n";
        ps << L"утратой пакета или части его содержимого без претензий с моей стороны.\r\n\r\n";
        ps << L"Мой телефон: " << phoneStr << L"\r\n";
        ps << L"(телефон необходим для возможности осуществления удаленной проверки продаж)\r\n";
        ps << L"ВНИМАНИЕ – мы не звоним Вам!\r\n";
        ps << L"Если Вы сдаете вещи впервые, то заполните данные для регистрации:\r\n";
        ps << L"ПАСПОРТ:\r\n";
        ps << L"Серия _______________ Номер_______________\r\n";
        ps << L"Выдан _____________ от ___________________\r\n";
        ps << L"Дата выдачи ______________________________\r\n";
        ps << L"Дата рождения ____________________________\r\n";
        ps << L"Проживаю ________________________________\r\n";

        const std::wstring previewText = ps.str();

        int btnW = 300, btnH = 70, gap = 20;
        int btnBackY = screenHeight - 10 - btnH;
        int btnPrintY = btnBackY - gap - btnH;
        int previewY = centerY - 60;
        int previewH = btnPrintY - 10 - previewY;
        if (previewH < 120) previewH = 120;

        HWND hPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", previewText.c_str(),
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            centerX - 400, previewY, 800, previewH,
            m_hWnd, (HMENU)(INT_PTR)ID_EDIT_TRUST_PREVIEW, g_hInstance, nullptr);
        SendMessageW(hPreview, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);
        SendMessageW(hPreview, EM_SETSEL, 0, 0);
        SendMessageW(hPreview, EM_SCROLLCARET, 0, 0);

        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Печать талона",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, btnPrintY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_TRUST_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, btnBackY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_TRUST_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);
        styleButtons();
        g_logger.info(L"Trust acceptance window shown");
    }

    void onTrustAcceptancePrint() {
        g_logger.info(L"onTrustAcceptancePrint: method started.");
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.warning(L"onTrustAcceptancePrint: user not logged in or clientId is 0.");
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            return;
        }
        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring clientName = g_authManager.getFullName();
        std::wstring clientPhone = g_authManager.getPhone();

        auto ticketOpt = g_queueManager.getTrustAcceptance(clientId, authToken);
        if (!ticketOpt) {
            g_logger.error(L"onTrustAcceptancePrint: failed to get trust acceptance ticket from QueueManager");
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            return;
        }
        m_currentTicket = ticketOpt.value();

        bool printed = g_printer.printTicketWithExtraMessage(m_currentTicket, clientId, clientName, clientPhone);
        if (printed) {
            g_logger.info(L"onTrustAcceptancePrint: ticket printed successfully. TicketNumber: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"onTrustAcceptancePrint: printer failed, ticket saved to file. TicketNumber: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }
        showTicketIssued();
        g_logger.info(L"onTrustAcceptancePrint: method completed successfully.");
    }

    void showPaidAcceptanceWindow() {
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.info(L"PaidAcceptance: user not logged in, showing registration dialog");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                createSubmitMenu();
                return;
            }
        }
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Стандартный заголовок с эмблемой (единый для всех экранов)
        createStandardHeader();

        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали ПЛАТНЫЙ ПРИЕМ (200 руб.).\n"
            L"При выборе данной опции, очередь короче, но постановка в данную очередь возможна при оплате 200 руб.\n"
            L"Количество товаров ограничено 20 наименованиями.\n\n"
            L"Стоимость услуги 200 руб.",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 220, 800, 220,
            m_hWnd, (HMENU)IDC_STATIC_PAID_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::PAID, authToken);
        std::wstring countText = L"Количество ожидающих в очереди: " + std::to_wstring(queueCount);
        HWND hQueueCount = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 300, centerY - 10, 600, 50,
            m_hWnd, (HMENU)ID_STATIC_PAID_QUEUE_COUNT, g_hInstance, nullptr);
        SendMessageW(hQueueCount, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 70;

        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Оплатить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);
        styleButtons();
        g_logger.info(L"Paid acceptance window shown, queue count: " + std::to_wstring(queueCount));
    }

    void onPaidAcceptancePrint() {
        g_logger.info(L"PaidAcceptance: start printing ticket");
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"PaidAcceptance print attempted without valid login");
            createSubmitMenu();
            return;
        }
        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        int itemsCount = Config::MAX_ITEMS_GENERAL_QUEUE;

        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::PAID, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"PaidAcceptance: failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }
        m_currentTicket = ticketOpt.value();
        bool printed = g_printer.printTicket(m_currentTicket, g_authManager.getFullName(), g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"PaidAcceptance: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"PaidAcceptance: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }
        showTicketIssued();
        g_logger.info(L"PaidAcceptance: completed successfully");
    }

    void showExpensiveAcceptanceWindow() {
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.info(L"ExpensiveAcceptance: user not logged in, showing registration dialog");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                createSubmitMenu();
                return;
            }
        }
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;

        // Стандартный заголовок с эмблемой (единый для всех экранов)
        createStandardHeader();

        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали ДОРОГОЙ ТОВАР (>5000 руб.).\n"
            L"Если у Вас есть вещи стоимостью более 5000 руб., то смело выбирайте этот вариант.\n"
            L"В течение 10-15 минут Вас примет самый опытный товаровед нашего магазина\n"
            L"(в данном случае принимаются вещи, оценочная стоимость каждой из которых более 5000 руб.).",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 220, 800, 220,
            m_hWnd, (HMENU)IDC_STATIC_EXPENSIVE_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::EXPENSIVE, authToken);
        std::wstring countText = L"Количество ожидающих в очереди: " + std::to_wstring(queueCount);
        HWND hQueueCount = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 300, centerY - 10, 600, 50,
            m_hWnd, (HMENU)ID_STATIC_EXPENSIVE_QUEUE_COUNT, g_hInstance, nullptr);
        SendMessageW(hQueueCount, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 70;

        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Взять талон",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_EXPENSIVE_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_EXPENSIVE_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);
        styleButtons();
        g_logger.info(L"Expensive acceptance window shown, queue count: " + std::to_wstring(queueCount));
    }

    void onExpensiveAcceptancePrint() {
        g_logger.info(L"ExpensiveAcceptance: start printing ticket");
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"ExpensiveAcceptance print attempted without valid login");
            createSubmitMenu();
            return;
        }
        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        int itemsCount = 1;

        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::EXPENSIVE, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"ExpensiveAcceptance: failed to get ticket for client " + std::to_wstring(clientId));
            return;
        }
        m_currentTicket = ticketOpt.value();
        bool printed = g_printer.printTicket(m_currentTicket, g_authManager.getFullName(), g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"ExpensiveAcceptance: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"ExpensiveAcceptance: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }
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
            if (m_hSalesStatusLabel) {
                SetWindowTextW(m_hSalesStatusLabel, L"Загрузка данных...");
                InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
                g_logger.info(L"WM_SALES_LOADING_START: status set to 'Загрузка данных...'");
            }
            break;
        case WM_SALES_DATA_READY:
        {
            auto* respPtr = reinterpret_cast<std::optional<nlohmann::json>*>(lParam);
            if (respPtr) {
                onSalesDataReady(*respPtr);
                delete respPtr;
            }
            break;
        }
        case WM_CONSIGNOR_LOADING_START:
            if (m_hConsignorInfoLabel) {
                SetWindowTextW(m_hConsignorInfoLabel, L"Загрузка данных...");
                InvalidateRect(m_hConsignorInfoLabel, NULL, TRUE);
                g_logger.info(L"WM_CONSIGNOR_LOADING_START: status set to 'Загрузка данных...'");
            }
            break;
        case WM_CONSIGNOR_DATA_READY:
        {
            auto* respPtr = reinterpret_cast<std::optional<nlohmann::json>*>(lParam);
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
            LoginWindow loginWnd;
            bool loginSuccess = loginWnd.show(m_hWnd);
            if (loginSuccess) {
                createMainMenu();
                g_logger.info(L"MainWindow: login successful, main menu refreshed");
            }
            else {
                if (g_authManager.wasLoginAttempted()) {
                    g_logger.info(L"MainWindow: login failed, redirecting to First Time page");
                    g_authManager.setLoginAttempted(false);
                    handleFirstTime();
                }
                else {
                    g_logger.info(L"MainWindow: login window cancelled");
                }
            }
            break;
        }
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
            createSubmitMenu();
            break;
        case ID_BTN_PAID:
            showPaidAcceptanceWindow();
            break;
        case ID_BTN_PAID_PRINT:
            onPaidAcceptancePrint();
            break;
        case ID_BTN_PAID_BACK:
            createSubmitMenu();
            break;
        case ID_BTN_EXPENSIVE:
            showExpensiveAcceptanceWindow();
            break;
        case ID_BTN_EXPENSIVE_PRINT:
            onExpensiveAcceptancePrint();
            break;
        case ID_BTN_EXPENSIVE_BACK:
            createSubmitMenu();
            break;
        case ID_BTN_PAY_CARD:
        case ID_BTN_PAY_QR:
            onExtra20PaymentSuccess();
            break;
        case ID_BTN_BACK:
            if (m_currentState == WindowState::SUBMIT_MENU || m_currentState == WindowState::TICKET_ISSUED) {
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
        case ID_BTN_LOGOUT:
            g_authManager.logout();
            g_logger.info(L"onCommand: User logged out successfully via 'Выход' button. Refreshing main menu.");
            createMainMenu();
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

extern MainWindow g_mainWindow;

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hBrushBtn = CreateSolidBrush(Config::ACCENT_COLOR);
        g_hBrushGreen = CreateSolidBrush(Config::PRIMARY_COLOR);
        g_hBrushBackBtn = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        g_hBrushWindow = CreateSolidBrush(RGB(255, 255, 0)); //жЕЛТЫЙ ФОН

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
		
        // Загрузка иконок из ресурсов
        g_hBmpIconNoCash = (HBITMAP)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDB_ICON_NO_CASH),
            IMAGE_BITMAP, 100, 100, LR_CREATEDIBSECTION);
        if (!g_hBmpIconNoCash)
            g_logger.error(L"WM_CREATE: LoadImage(IDB_ICON_NO_CASH) failed, err=" + std::to_wstring(GetLastError()));

        g_hBmpIconKiosk = (HBITMAP)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDB_ICON_KIOSK),
            IMAGE_BITMAP, 100, 100, LR_CREATEDIBSECTION);
        if (!g_hBmpIconKiosk)
            g_logger.error(L"WM_CREATE: LoadImage(IDB_ICON_KIOSK) failed, err=" + std::to_wstring(GetLastError()));

        g_hBmpIconWallet = (HBITMAP)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDB_ICON_WALLET),
            IMAGE_BITMAP, 100, 100, LR_CREATEDIBSECTION);
        if (!g_hBmpIconWallet)
            g_logger.error(L"WM_CREATE: LoadImage(IDB_ICON_WALLET) failed, err=" + std::to_wstring(GetLastError()));

        // ИЗМЕНЕНО: Загрузка эмблемы из ресурсов тем же методом, что и три иконки
        // Было: LoadImageW(g_hInstance, L"rc.png", IMAGE_BITMAP, STATUE_SIZE_PX, STATUE_SIZE_PX, LR_LOADFROMFILE | LR_CREATEDIBSECTION)
        // Стало: Загрузка из ресурса IDB_STATUE через MAKEINTRESOURCEW
        g_hBmpStatue = (HBITMAP)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDB_STATUE),
            IMAGE_BITMAP, STATUE_SIZE_PX, STATUE_SIZE_PX, LR_CREATEDIBSECTION);

        if (!g_hBmpStatue) {
            g_logger.error(L"WM_CREATE: LoadImage(IDB_STATUE) failed, err=" +
                std::to_wstring(GetLastError()) +
                L". Ensure rc.png is imported as resource IDB_STATUE in kiosk_icons.rc");
        }
        else {
            g_logger.info(L"WM_CREATE: IDB_STATUE resource loaded successfully (" +
                std::to_wstring(STATUE_SIZE_PX) + L"x" + std::to_wstring(STATUE_SIZE_PX) + L")");
        }

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
        if (btnId == ID_BTN_BACK || btnId == ID_BTN_CLOSE || btnId == ID_BTN_RESET_ID || btnId == ID_BTN_LOGOUT) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)g_hBrushBackBtn;
        }
        if (btnId == ID_BTN_PAY_CARD || btnId == ID_BTN_PAY_QR ||
            btnId == ID_BTN_NEXT || btnId == ID_BTN_PRINT_TICKET ||
            btnId == ID_BTN_TRUST_PRINT) {
            SetBkColor(hdc, Config::PRIMARY_COLOR);
            return (LRESULT)g_hBrushGreen;
        }
        SetBkColor(hdc, Config::ACCENT_COLOR);
        return (LRESULT)g_hBrushBtn;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        DWORD id = GetDlgCtrlID(hStatic);

        if (id == ID_STATIC_MAIN_TITLE_1 || id == ID_STATIC_MAIN_TITLE_2) {
            SetTextColor(hdc, Config::TITLE_CHERRY_COLOR);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (id == ID_EDIT_TRUST_PREVIEW) {
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkMode(hdc, OPAQUE);
            return (LRESULT)g_hBrushWindow;
        }
        if (id == ID_STATIC_CONSIGNOR_INFO || id == ID_STATIC_SALES_STATUS) {
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
        if (g_hBmpIconNoCash) { DeleteObject(g_hBmpIconNoCash); g_hBmpIconNoCash = nullptr; }
        if (g_hBmpIconKiosk) { DeleteObject(g_hBmpIconKiosk); g_hBmpIconKiosk = nullptr; }
        if (g_hBmpIconWallet) { DeleteObject(g_hBmpIconWallet); g_hBmpIconWallet = nullptr; }
        if (g_hBmpStatue) { DeleteObject(g_hBmpStatue); g_hBmpStatue = nullptr; } // Очистка ресурса статуи

        g_logger.info(L"WM_DESTROY: GDI objects (brushes/fonts/icon bitmaps) released");
        g_hCloseBtn = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
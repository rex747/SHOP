#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include "config.h"
#include "queue_manager.h"
#include "registration_dialog.h"
#include "registration_window.h"
#include "logger.h"
#include "auth_manager.h"
#include "receipt_printer.h"
#include "https_client.h"

extern QueueManager g_queueManager;
extern AuthManager g_authManager;
extern Logger g_logger;
extern HINSTANCE g_hInstance;
extern HWND g_hMainWnd;
extern ReceiptPrinter g_printer;
extern HTTPSClient g_httpsClient;

static HBRUSH g_hBrushBtn = nullptr;
static HBRUSH g_hBrushBackBtn = nullptr;
static HBRUSH g_hBrushWindow = nullptr;
static HFONT g_hFontTitle = nullptr;
static HFONT g_hFontButton = nullptr;
static HFONT g_hFontEdit = nullptr;
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

// Window states
enum class WindowState {
    MAIN_MENU,
    SUBMIT_MENU,
    REGISTRATION,
    CONSIGNOR_LOOKUP,
    TICKET_ISSUED,
    GENERAL_QUEUE_INPUT   // новое состояние
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
    std::vector<HWND> m_digitButtons;
    int m_currentClientId;
    std::wstring m_currentClientName;
    std::wstring m_currentClientPhone;
    bool m_nameConfirmed;

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
        m_nameConfirmed = false;
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
        int greetingY = 30 + 220 + 20; // 270 (после двух строк заголовка)
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

        // ---------- ИСПРАВЛЕНИЕ: вычисляем startY динамически ----------
        int startY = greetingY + greetingHeight + 20; // 270 + 60 + 20 = 350

        // Кнопки
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

        // ---- Заголовок (две строки) ----
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

        // ---- Текст описания ----
        const wchar_t* descText =
            L"Вы выбрали ОБЩУЮ ОЧЕРЕДЬ. Вы получаете талончик электронной очереди и ожидаете своего времени приема.\n"
            L"Количество товаров ограничено 20 наименованиями. При большой загруженности, время ожидания может достигать 4 часов и более.";
        m_hStaticDescription = CreateWindowExW(0, L"STATIC", descText,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 100,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_DESCRIPTION, g_hInstance, nullptr);
        SendMessageW(m_hStaticDescription, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 110;

        // ---- Количество ожидающих ----
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

        // ---- Текст "Введите номер комитента..." ----
        const wchar_t* promptText =
            L"Введите номер комитента и нажмите кнопку Далее.\n"
            L"Если номер ввели неверно, нажмите кнопку Сбросить.";
        m_hStaticPrompt = CreateWindowExW(0, L"STATIC", promptText,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_PROMPT, g_hInstance, nullptr);
        SendMessageW(m_hStaticPrompt, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        top += 70;

        // ---- Поле ввода ID ----
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

        // ---- Кнопки цифр (0-9) ----
        int digitBtnSize = 50;
        int digitGap = 8;
        int digitRowWidth = digitBtnSize * 5 + digitGap * 4;
        int digitStartX = centerX - digitRowWidth / 2;

        // Строка 1: 1-5
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

        // Строка 2: 6-9, 0
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
        // кнопка 0
        int x0 = digitStartX + 4 * (digitBtnSize + digitGap);
        HWND btn0 = CreateWindowExW(0, L"BUTTON", L"0",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x0, row2Y, digitBtnSize, digitBtnSize,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_DIGIT_0, g_hInstance, nullptr);
        m_digitButtons.push_back(btn0);
        m_buttons.push_back(btn0);
        top += digitBtnSize + 15;

        // ---- Кнопка "Сбросить" (очистка поля) ----
        int clearWidth = 120;
        m_hBtnClear = CreateWindowExW(0, L"BUTTON", L"Сбросить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - clearWidth / 2, top, clearWidth, 40,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_CLEAR, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnClear);
        top += 50;

        // ---- Кнопка "Далее" (зелёная) ----
        int nextWidth = 200;
        m_hBtnNext = CreateWindowExW(0, L"BUTTON", L"Далее",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - nextWidth / 2, top, nextWidth, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_NEXT, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnNext);
        top += 70;

        // ---- Метка для отображения имени (изначально скрыта) ----
        m_hStaticNameDisplay = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_NAME_DISPLAY, g_hInstance, nullptr);
        SendMessageW(m_hStaticNameDisplay, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        ShowWindow(m_hStaticNameDisplay, SW_HIDE);
        top += 60;

        // ---- Кнопка "Сбросить" (рядом с именем, изначально скрыта) ----
        m_hBtnResetId = CreateWindowExW(0, L"BUTTON", L"Сбросить",
            WS_CHILD | BS_PUSHBUTTON,
            screenWidth - 150, top - 60, 100, 40,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_RESET_ID, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnResetId);
        ShowWindow(m_hBtnResetId, SW_HIDE);

        // ---- Кнопка "Печать талона" (зелёная, изначально скрыта) ----
        int printWidth = 300;
        m_hBtnPrintTicket = CreateWindowExW(0, L"BUTTON", L"Печать талона",
            WS_CHILD | BS_PUSHBUTTON,
            centerX - printWidth / 2, top + 20, printWidth, 70,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_PRINT_TICKET, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnPrintTicket);
        ShowWindow(m_hBtnPrintTicket, SW_HIDE);
        top += 100;

        // ---- Кнопка "Назад" (красная) ----
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

        // Запрос к серверу для получения данных клиента
        std::wstring authToken = g_authManager.getAuthToken();
        std::wstring path = L"/api/v1/clients/by_id?id=" + idStr;
        auto response = g_httpsClient.get(path, authToken);

        if (!response || !response->contains("id")) {
            SetWindowTextW(m_hStaticNameDisplay, L"Клиент не найден");
            ShowWindow(m_hStaticNameDisplay, SW_SHOW);
            g_logger.warning(L"Client not found for id " + idStr);
            return;
        }

        // Сохраняем данные
        m_currentClientId = id;
        m_currentClientName = utf8_to_wstring((*response)["name"].get<std::string>());
        m_currentClientPhone = utf8_to_wstring((*response)["phone"].get<std::string>());

        // Отображаем имя
        std::wstring displayText = m_currentClientName + L", можете распечатать талон.";
        SetWindowTextW(m_hStaticNameDisplay, displayText.c_str());
        ShowWindow(m_hStaticNameDisplay, SW_SHOW);

        // Скрываем кнопку "Далее", показываем "Печать талона" и кнопку "Сбросить" (рядом с именем)
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
        // Сбрасываем состояние
        m_nameConfirmed = false;
        m_currentClientId = 0;
        m_currentClientName.clear();
        m_currentClientPhone.clear();

        // Очищаем поле ввода
        SetWindowTextW(m_hEditConsignorId, L"");

        // Скрываем элементы
        ShowWindow(m_hStaticNameDisplay, SW_HIDE);
        ShowWindow(m_hBtnPrintTicket, SW_HIDE);
        ShowWindow(m_hBtnResetId, SW_HIDE);

        // Показываем кнопку "Далее"
        ShowWindow(m_hBtnNext, SW_SHOW);

        SetFocus(m_hEditConsignorId);
        g_logger.info(L"General queue input reset");
    }

    // ------------------------------------------------------------
    // Обработчик "Печать талона"
    // ------------------------------------------------------------
    void onGeneralQueuePrint() {
        if (!m_nameConfirmed || m_currentClientId == 0) {
            MessageBoxW(m_hWnd, L"Сначала подтвердите ID комитента", L"Ошибка", MB_OK);
            return;
        }

        // Получаем талон
        std::wstring authToken = g_authManager.getAuthToken();
        auto ticketOpt = g_queueManager.getTicket(
            m_currentClientId,
            QueueType::GENERAL,
            Config::MAX_ITEMS_GENERAL_QUEUE, // 20
            authToken
        );

        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"Failed to get ticket for client id " + std::to_wstring(m_currentClientId));
            return;
        }

        m_currentTicket = ticketOpt.value();

        // Печать
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

        // Показываем страницу с талоном
        showTicketIssued();
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
        m_currentClientId(0),
        m_nameConfirmed(false) {
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
        // ---- Цифровые кнопки ----
        if (cmd >= ID_BTN_DIGIT_0 && cmd <= ID_BTN_DIGIT_9) {
            if (m_hEditConsignorId && IsWindowEnabled(m_hEditConsignorId)) {
                int digit = cmd - ID_BTN_DIGIT_0;
                wchar_t ch = L'0' + digit;
                wchar_t str[2] = { ch, L'\0' };  // <-- ИСПРАВЛЕНИЕ: передаём строку
                int len = GetWindowTextLengthW(m_hEditConsignorId);
                SendMessageW(m_hEditConsignorId, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                SendMessageW(m_hEditConsignorId, EM_REPLACESEL, TRUE, (LPARAM)str);
            }
            return;
        }

        switch (cmd) {
            // ---- Главное меню ----
        case ID_BTN_REGISTER:
        {
            RegistrationWindow* regWnd = new RegistrationWindow();
            regWnd->show(m_hWnd);
        }
        break;

        case ID_BTN_SUBMIT_ITEMS:
            createSubmitMenu();
            break;

            // ---- Меню выбора очереди ----
        case ID_BTN_GENERAL_QUEUE:
            createGeneralQueueInput();
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

            // ---- Общие кнопки ----
        case ID_BTN_BACK:
            if (m_currentState == WindowState::SUBMIT_MENU ||
                m_currentState == WindowState::TICKET_ISSUED) {
                createMainMenu();
            }
            else if (m_currentState == WindowState::GENERAL_QUEUE_INPUT) {
                createSubmitMenu();
            }
            break;

        case ID_BTN_CLOSE:
            PostQuitMessage(0);
            break;

            // ---- Страница ввода ID ----
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

            // ---- Заглушки ----
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
extern MainWindow g_mainWindow;

// Window procedure
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hBrushBtn = CreateSolidBrush(Config::ACCENT_COLOR);
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
        g_mainWindow.m_hWnd = hWnd;
        //g_mainWindow.create(hWnd);
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
        // Кнопка "Назад", "Закрыть", "Сбросить" – красные
        if (btnId == ID_BTN_BACK || btnId == ID_BTN_CLOSE || btnId == ID_BTN_RESET_ID) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)g_hBrushBackBtn;
        }
        // Кнопка "Далее" – зелёная (как основная кнопка)
        if (btnId == ID_BTN_NEXT || btnId == ID_BTN_PRINT_TICKET) {
            SetBkColor(hdc, Config::PRIMARY_COLOR);
            return (LRESULT)g_hBrushBtn;
        }
        // Остальные – стандартный цвет (акцентный)
        SetBkColor(hdc, Config::ACCENT_COLOR);
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
        DeleteObject(g_hFontEdit);
        g_hCloseBtn = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
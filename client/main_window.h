// main_window.h

#pragma once
#include  <windows.h >
#include  <commctrl.h >
#include  <string >
#include  <vector >
#include  <sstream >
#include  <optional >
#include  <nlohmann/json.hpp >
#include <iomanip>
#include <chrono>
#include <unordered_set>

#include  "config.h "
#include  "queue_manager.h "
#include  "registration_dialog.h "
#include  "logger.h "
#include  "auth_manager.h "
#include  "receipt_printer.h "
#include  "https_client.h "
#include  "login_window.h "
#include  "string_utils.h "
#include "payment_manager.h"

extern QueueManager g_queueManager;
extern AuthManager g_authManager;
extern Logger g_logger;
extern HINSTANCE g_hInstance;
extern HWND g_hMainWnd;
extern ReceiptPrinter g_printer;
extern HTTPSClient g_httpsClient;
extern PaymentManager g_paymentManager;

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
#define ID_BTN_ADDITIONAL_SERVICES 1010 // КНОПКА: Дополнительные услуги

#define ID_BTN_GENERAL_QUEUE 2001
#define ID_BTN_FIRST_TIME 2002
#define ID_BTN_EXTRA_20 2003
#define ID_BTN_TRUST 2004
#define ID_BTN_PAID 2005
#define ID_BTN_EXPENSIVE 2006

#define ID_BTN_SELECT_APPRAISER 2007 // КНОПКА: Выбрать товароведа
#define ID_BTN_APPRAISER_PAY 2008    // КНОПКА: Оплатить выбор товароведа
#define ID_BTN_APPRAISER_BACK 2009   // КНОПКА: Назад из выбора товароведа

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

// НОВЫЕ ID для тестирования платежей
#define ID_BTN_TEST_PAY            5100
#define ID_BTN_TEST_CANCEL         5101
#define ID_BTN_TEST_EMERGENCY      5102

// Кнопки отмены платежа для "Платный прием"
#define ID_BTN_PAID_CANCEL              6021
#define ID_BTN_PAID_EMERGENCY_CANCEL    6022

// Кнопки отмены платежа для "Выбор товароведа"
#define ID_BTN_APPRAISER_CANCEL         2010
#define ID_BTN_APPRAISER_EMERGENCY_CANCEL 2011

// Кнопки отмены платежа для "+20 товаров"
#define ID_BTN_EXTRA20_CANCEL           5013
#define ID_BTN_EXTRA20_EMERGENCY_CANCEL 5014


#define WM_SALES_DATA_READY (WM_USER + 200)
#define WM_SALES_LOADING_START (WM_USER + 201)
#define WM_CONSIGNOR_DATA_READY (WM_USER + 202)
#define WM_CONSIGNOR_LOADING_START (WM_USER + 203)

// ID для модального окна ввода номера приложения
#define ID_APPENDIX_INPUT_EDIT      7100
#define ID_BTN_APPENDIX_CONTINUE    7101
#define ID_BTN_APPENDIX_CANCEL      7102
#define ID_BTN_APPENDIX_BACKSPACE   7103   // НОВОЕ: удаление последней цифры

enum class WindowState {
    MAIN_MENU,
    SUBMIT_MENU,
    ADDITIONAL_SERVICES,  // Меню дополнительных услуг
    APPRAISER_SELECTION,  // Окно выбора товароведа
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

    HWND m_hAppendixDialog;
    HWND m_hAppendixEdit;
    bool m_appendixDialogActive;
    int  m_appendixDialogResult;

    std::optional<nlohmann::json> m_pendingAppendixItems;  // кэш ответа по приложению

    static LRESULT CALLBACK AppendixDialogWndProc(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        MainWindow* pThis = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(pThis));
            return 0;
        }
        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            // =================================================================
            // НОВОЕ: обработка виртуальной числовой клавиатуры
            // =================================================================
            if (cmd >= ID_BTN_DIGIT_0 && cmd <= ID_BTN_DIGIT_9) {
                int digit = cmd - ID_BTN_DIGIT_0;
                wchar_t ch = L'0' + digit;
                wchar_t str[2] = { ch, L'\0' };
                int len = GetWindowTextLengthW(pThis->m_hAppendixEdit);
                // Защита от переполнения (лимит 10 символов, как в EM_SETLIMITTEXT)
                if (len < 10) {
                    SendMessageW(pThis->m_hAppendixEdit, EM_SETSEL,
                        (WPARAM)len, (LPARAM)len);
                    SendMessageW(pThis->m_hAppendixEdit, EM_REPLACESEL,
                        TRUE, (LPARAM)str);
                }
                return 0;
            }
            if (cmd == ID_BTN_CLEAR) {
                SetWindowTextW(pThis->m_hAppendixEdit, L"");
                SetFocus(pThis->m_hAppendixEdit);
                return 0;
            }
            if (cmd == ID_BTN_APPENDIX_BACKSPACE) {
                int len = GetWindowTextLengthW(pThis->m_hAppendixEdit);
                if (len > 0) {
                    SendMessageW(pThis->m_hAppendixEdit, EM_SETSEL,
                        (WPARAM)(len - 1), (LPARAM)len);
                    SendMessageW(pThis->m_hAppendixEdit, EM_REPLACESEL,
                        TRUE, (LPARAM)L"");
                }
                return 0;
            }
            if (cmd == ID_BTN_APPENDIX_CONTINUE) {
                wchar_t buf[32] = { 0 };
                GetWindowTextW(pThis->m_hAppendixEdit, buf, 32);
                std::wstring numStr(buf);

                if (numStr.empty()) {
                    MessageBoxW(hWnd, L"Введите номер приложения",
                        L"Ошибка", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                // Проверка: получаем ПЕРЕЧЕНЬ ТОВАРОВ по номеру приложения.
                // Используется НОВЫЙ роут, который на сервере делегирует в
                // существующий db_->getAppendixByNumber().
                int clientId = g_authManager.getClientId();
                std::wstring authToken = g_authManager.getAuthToken();
                std::wstring path =
                    L"/api/v1/clients/appendix_by_number?number=" + numStr;

                auto response = g_httpsClient.get(path, authToken);

                if (!response || response->contains("error")) {
                    std::wstring errText = L"Приложение с таким номером не найдено";
                    if (response && response->contains("error")) {
                        std::string errUtf8 = (*response)["error"].get<std::string>();
                        errText = utf8_to_wstring(errUtf8);
                    }
                    MessageBoxW(hWnd, errText.c_str(),
                        L"Ошибка", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                if (!response->contains("items") || !(*response)["items"].is_array()) {
                    MessageBoxW(hWnd,
                        L"Сервер вернул некорректный ответ",
                        L"Ошибка", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                // Сохраняем полученный JSON в кэш — его использует onSalesDataReady
                pThis->m_pendingAppendixItems = *response;

                pThis->m_appendixDialogResult = _wtoi(numStr.c_str());
                pThis->m_appendixDialogActive = false;
                DestroyWindow(hWnd);
                return 0;
            }
            else if (cmd == ID_BTN_APPENDIX_CANCEL) {
                pThis->m_appendixDialogResult = 0;
                pThis->m_appendixDialogActive = false;
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            pThis->m_appendixDialogResult = 0;
            pThis->m_appendixDialogActive = false;
            DestroyWindow(hWnd);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    int showAppendixInputDialog() {
        // Регистрация класса окна (однократно)
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = AppendixDialogWndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"AppendixInputDialogClass";
            RegisterClassExW(&wc);
            registered = true;
        }

        // =================================================================
        // ИЗМЕНЕНИЕ: увеличена высота окна для размещения клавиатуры
        // Было: dlgWidth=500, dlgHeight=250
        // Стало: dlgWidth=500, dlgHeight=600
        // =================================================================
        int dlgWidth = 500;
        int dlgHeight = 600;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int dlgX = (screenWidth - dlgWidth) / 2;
        int dlgY = (screenHeight - dlgHeight) / 2;

        m_appendixDialogActive = true;
        m_appendixDialogResult = 0;
        m_hAppendixDialog = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            L"AppendixInputDialogClass",
            L"Выбрать товары по приложению",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            dlgX, dlgY, dlgWidth, dlgHeight,
            m_hWnd, nullptr, g_hInstance, this);
        if (!m_hAppendixDialog) {
            m_appendixDialogActive = false;
            return 0;
        }

        // =================================================================
        // Заголовок-подсказка
        // =================================================================
        HWND hLabel = CreateWindowExW(0, L"STATIC",
            L"Введите номер приложения:",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 20, dlgWidth - 100, 30,
            m_hAppendixDialog, nullptr, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        // =================================================================
        // Поле ввода (EDIT) — увеличено по высоте для удобства тапа
        // =================================================================
        m_hAppendixEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER |
            ES_CENTER | WS_TABSTOP,
            50, 65, dlgWidth - 100, 60,
            m_hAppendixDialog, (HMENU)ID_APPENDIX_INPUT_EDIT,
            g_hInstance, nullptr);
        SendMessageW(m_hAppendixEdit, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        SendMessageW(m_hAppendixEdit, EM_SETLIMITTEXT, (WPARAM)10, 0);

        // =================================================================
        // НОВОЕ: виртуальная числовая клавиатура 3x4
        // =================================================================
        const int btnW = 120;
        const int btnH = 70;
        const int gap = 15;
        const int rowWidth = btnW * 3 + gap * 2;       // 390
        const int startX = (dlgWidth - rowWidth) / 2;  // 55
        int rowY = 155;

        // Ряд 1: 1, 2, 3
        for (int i = 1; i <= 3; ++i) {
            int x = startX + (i - 1) * (btnW + gap);
            HWND btn = CreateWindowExW(0, L"BUTTON",
                std::to_wstring(i).c_str(),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                x, rowY, btnW, btnH,
                m_hAppendixDialog,
                (HMENU)(INT_PTR)(ID_BTN_DIGIT_0 + i),
                g_hInstance, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        }
        rowY += btnH + gap;

        // Ряд 2: 4, 5, 6
        for (int i = 4; i <= 6; ++i) {
            int x = startX + (i - 4) * (btnW + gap);
            HWND btn = CreateWindowExW(0, L"BUTTON",
                std::to_wstring(i).c_str(),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                x, rowY, btnW, btnH,
                m_hAppendixDialog,
                (HMENU)(INT_PTR)(ID_BTN_DIGIT_0 + i),
                g_hInstance, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        }
        rowY += btnH + gap;

        // Ряд 3: 7, 8, 9
        for (int i = 7; i <= 9; ++i) {
            int x = startX + (i - 7) * (btnW + gap);
            HWND btn = CreateWindowExW(0, L"BUTTON",
                std::to_wstring(i).c_str(),
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                x, rowY, btnW, btnH,
                m_hAppendixDialog,
                (HMENU)(INT_PTR)(ID_BTN_DIGIT_0 + i),
                g_hInstance, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        }
        rowY += btnH + gap;

        // Ряд 4: Сбросить, 0, Удалить (←)
        {
            HWND btnClear = CreateWindowExW(0, L"BUTTON", L"Сбросить",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                startX, rowY, btnW, btnH,
                m_hAppendixDialog, (HMENU)ID_BTN_CLEAR,
                g_hInstance, nullptr);
            SendMessageW(btnClear, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

            HWND btn0 = CreateWindowExW(0, L"BUTTON", L"0",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                startX + btnW + gap, rowY, btnW, btnH,
                m_hAppendixDialog, (HMENU)ID_BTN_DIGIT_0,
                g_hInstance, nullptr);
            SendMessageW(btn0, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

            HWND btnBack = CreateWindowExW(0, L"BUTTON", L"\x2190",  // ←
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                startX + 2 * (btnW + gap), rowY, btnW, btnH,
                m_hAppendixDialog, (HMENU)ID_BTN_APPENDIX_BACKSPACE,
                g_hInstance, nullptr);
            SendMessageW(btnBack, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
        }
        rowY += btnH + gap;

        // =================================================================
        // Кнопки управления диалогом: «Продолжить» и «Назад»
        // =================================================================
        HWND hContinue = CreateWindowExW(0, L"BUTTON", L"Продолжить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            50, rowY + 10, 180, 55,
            m_hAppendixDialog, (HMENU)ID_BTN_APPENDIX_CONTINUE,
            g_hInstance, nullptr);
        SendMessageW(hContinue, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            dlgWidth - 230, rowY + 10, 180, 55,
            m_hAppendixDialog, (HMENU)ID_BTN_APPENDIX_CANCEL,
            g_hInstance, nullptr);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        EnableWindow(m_hWnd, FALSE);
        SetFocus(m_hAppendixEdit);

        MSG msg;
        while (m_appendixDialogActive && GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
                SendMessageW(m_hAppendixDialog, WM_COMMAND,
                    ID_BTN_APPENDIX_CONTINUE, 0);
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                SendMessageW(m_hAppendixDialog, WM_COMMAND,
                    ID_BTN_APPENDIX_CANCEL, 0);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        EnableWindow(m_hWnd, TRUE);
        SetForegroundWindow(m_hWnd);
        HWND hDlg = m_hAppendixDialog;
        m_hAppendixDialog = nullptr;
        m_hAppendixEdit = nullptr;
        if (hDlg && IsWindow(hDlg)) {
            DestroyWindow(hDlg);
        }
        return m_appendixDialogResult;
    }

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
        m_hAppendixDialog = nullptr;
        m_hAppendixEdit = nullptr;
        m_appendixDialogActive = false;
        m_appendixDialogResult = 0;
        m_pendingAppendixItems.reset();
    }

    void styleButtons() {
        for (HWND btn : m_buttons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
        for (HWND btn : m_digitButtons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        }
    }

    void createStandardHeader() {
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int titleCenterX = screenWidth / 2;
        int statueY = 15;
        HWND hStatue = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_CENTERIMAGE,
            titleCenterX - (STATUE_SIZE_PX / 2), statueY, STATUE_SIZE_PX, STATUE_SIZE_PX,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_STATUE, g_hInstance, nullptr);
        if (g_hBmpStatue) {
            SendMessageW(hStatue, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hBmpStatue);
        }
        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 129, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_1, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
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
        int titleCenterX = screenWidth / 2;

        g_logger.info(L"createMainMenu: started, screen=" + std::to_wstring(screenWidth) +
            L"x" + std::to_wstring(screenHeight) + L", btnHeight=" + std::to_wstring(btnHeight));

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

        HWND hTitle1 = CreateWindowExW(0, L"STATIC", L"СОВЕТСКИЙ",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 129, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_1, g_hInstance, nullptr);
        SendMessageW(hTitle1, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hTitle2 = CreateWindowExW(0, L"STATIC", L"Комиссионный магазин",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, 189, screenWidth - 100, 60,
            m_hWnd, (HMENU)(INT_PTR)ID_STATIC_MAIN_TITLE_2, g_hInstance, nullptr);
        SendMessageW(hTitle2, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        const int iconSize = 100;
        const int colWidth = 300;
        const int iconsStartX = (screenWidth - colWidth * 3) / 2;
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

        // =========================================================================
        // НОВАЯ КНОПКА: Дополнительные услуги
        // =========================================================================
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Дополнительные услуги",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_ADDITIONAL_SERVICES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Мои продажи",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_MY_SALES, g_hInstance, nullptr));
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Адреса магазинов",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_ADDRESSES, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Узнать номер комитента",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + 10) * 5, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_CONSIGNOR_NUMBER, g_hInstance, nullptr));

        if (g_authManager.isLoggedIn()) {
            int exitBtnY = startY + (btnHeight + 10) * 5 + btnHeight + 20;
            g_logger.info(L"createMainMenu: Consignor button Y=" +
                std::to_wstring(startY + (btnHeight + 10) * 5) +
                L", Exit button Y=" + std::to_wstring(exitBtnY) +
                L" (double gap 20 px after Consignor button, no overlaps)");

            m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"Выход",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                centerX, exitBtnY, btnWidth, btnHeight,
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

        createStandardHeader();

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"Выберите тип очереди",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 260, btnWidth, 50,
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

        // =========================================================================
        // ИЗМЕНЕНИЕ: Кнопка "+20 ПОЗИЦИЙ" УДАЛЕНА отсюда и перенесена в
        // createAdditionalServicesMenu(). Индексы умножения сдвинуты.
        // =========================================================================

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"НА ДОВЕРИИ\n(без присутствия)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 2, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_TRUST, g_hInstance, nullptr));
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ПЛАТНЫЙ ПРИЕМ\n(200 руб., без очереди)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 3, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_PAID, g_hInstance, nullptr));
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ДОРОГОЙ ТОВАР\n(>5000 руб.)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + (btnHeight + gap) * 4, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_EXPENSIVE, g_hInstance, nullptr));
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"НАЗАД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + gap) * 4 + btnHeight + backGap, btnWidth, backBtnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));
        styleButtons();
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: createAdditionalServicesMenu
    // Создает окно "Дополнительные платные услуги"
    // =========================================================================
    void createAdditionalServicesMenu() {
        clearWindow();
        m_currentState = WindowState::ADDITIONAL_SERVICES;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int btnWidth = 500;
        int btnHeight = 100;
        int backBtnHeight = 60;
        int gap = 5;
        int backGap = 25;
        int centerX = (screenWidth - btnWidth) / 2;

        createStandardHeader();

        HWND hSubtitle = CreateWindowExW(0, L"STATIC", L"Дополнительные платные услуги",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX, 260, btnWidth, 50,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hSubtitle, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        g_logger.info(L"createAdditionalServicesMenu: Subtitle created");

        int startY = 260 + 50 + 20;  // 330

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"ВЫБРАТЬ ТОВАРОВЕДА\n(200 руб.)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_SELECT_APPRAISER, g_hInstance, nullptr));

        // Кнопка "+20 ПОЗИЦИЙ" перенесена сюда из createSubmitMenu()
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON",
            L"+20 ПОЗИЦИЙ\n(дополнительно к базовым)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_MULTILINE,
            centerX, startY + btnHeight + gap, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_EXTRA_20, g_hInstance, nullptr));

        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"НАЗАД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX, startY + (btnHeight + gap) * 2 + backGap, btnWidth, backBtnHeight,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
        g_logger.info(L"createAdditionalServicesMenu: Menu created successfully");
    }

    // =========================================================================
    // МЕТОД: showAppraiserSelectionWindow
    // Окно выбора товароведа с оплатой 200 руб.
    // =========================================================================
    void showAppraiserSelectionWindow() {
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.info(L"AppraiserSelection: user not logged in, showing registration dialog");
            RegistrationDialog regDlg;
            if (regDlg.show(m_hWnd) != IDOK) {
                createAdditionalServicesMenu();
                return;
            }
        }
        clearWindow();
        m_currentState = WindowState::APPRAISER_SELECTION;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;
        createStandardHeader();

        std::wstring countText = L"Вы выбрали платную услугу ВЫБОР ТОВАРОВЕДА.\n"
            L"Стоимость услуги 200 руб.\n\n"
            L"При нажатии кнопки «Оплатить» будет произведена оплата 200 рублей.";

        HWND hText = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 200, 800, 200,
            m_hWnd, (HMENU)IDC_STATIC_PAID_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 50;

        HWND btnPay = CreateWindowExW(0, L"BUTTON", L"Оплатить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_APPRAISER_PAY, g_hInstance, nullptr);
        m_buttons.push_back(btnPay);

        // Кнопка "Назад" (сдвинута вниз)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + 3 * (btnH + gap), btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_APPRAISER_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);


        styleButtons();
        g_logger.info(L"Appraiser selection window shown with payment option (100 RUB)");
    }

    // =========================================================================
    // МЕТОД: onAppraiserPaymentSuccess
    // Реальная оплата 200 руб. через PaymentManager с автоматической отменой при ошибке
    // =========================================================================
    void onAppraiserPaymentSuccess() {
        g_logger.info(L"AppraiserPayment: start processing payment");

        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"AppraiserPayment: attempted without valid login");
            createAdditionalServicesMenu();
            return;
        }

        // ШАГ 1: Вызов реальной оплаты 200 рублей (20000 копеек)
        g_logger.info(L"AppraiserPayment: calling PaymentManager.ProcessPayment(20000)");
        auto paymentResult = g_paymentManager.ProcessPayment(20000);

        if (!paymentResult.has_value()) {
            g_logger.error(L"AppraiserPayment: ProcessPayment returned no value (critical error)");
            MessageBoxW(m_hWnd,
                L"Сбой связи с платёжным терминалом.\nПовторите попытку позже.",
                L"Ошибка оплаты", MB_OK | MB_ICONERROR);
            return;
        }

        if (!paymentResult->isSuccess) {
            std::string errDesc = g_paymentManager.extractField(paymentResult->rawResponse, "19");
            std::wstring wErrDesc;
            if (errDesc.empty()) {
                wErrDesc = L"Транзакция отклонена банком.";
            }
            else {
                // Конвертация из windows-1251 (кодировка ответа DualConnector)
                int size_needed = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
                wErrDesc.resize(size_needed);
                MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], size_needed);
            }
            g_logger.error(L"AppraiserPayment: Payment REJECTED. Reason: " + wErrDesc);
            MessageBoxW(m_hWnd,
                (L"Оплата не прошла.\nПричина: " + wErrDesc).c_str(),
                L"Отказ в оплате", MB_OK | MB_ICONWARNING);
            return;
        }

        g_logger.info(L"AppraiserPayment: Payment APPROVED. AuthCode=" +
            std::wstring(paymentResult->authCode.begin(), paymentResult->authCode.end()) +
            L", RRN=" + std::wstring(paymentResult->rrn.begin(), paymentResult->rrn.end()));

        // ШАГ 2: Получение талона
        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();
        int itemsCount = Config::MAX_ITEMS_GENERAL_QUEUE;

        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::PAID, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"AppraiserPayment: failed to get ticket for client " + std::to_wstring(clientId));
            g_logger.warning(L"AppraiserPayment: emergency cancellation due to ticket failure");
            // АВТОМАТИЧЕСКАЯ АВАРИЙНАЯ ОТМЕНА при ошибке получения талона
            g_paymentManager.ProcessEmergencyCancellation();
            return;
        }

        m_currentTicket = ticketOpt.value();

        // ШАГ 3: Печать талона
        bool printed = g_printer.printTicket(m_currentTicket, g_authManager.getFullName(), g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"AppraiserPayment: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"AppraiserPayment: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        showTicketIssued(true);  // показываем кнопки отмены

        g_logger.info(L"AppraiserPayment: completed successfully");
    }

    void loadSalesData() {
        g_logger.info(L"loadSalesData: started");

        if (!m_hSalesStatusLabel) {
            return;
        }

        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            SetWindowTextW(m_hSalesStatusLabel, L"Клиент не авторизован");
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.warning(L"loadSalesData: client is not authorized");
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            SetWindowTextW(m_hSalesStatusLabel, L"Токен отсутствует, выполните вход");
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.warning(L"loadSalesData: auth token is empty");
            return;
        }

        PostMessageW(m_hWnd, WM_SALES_LOADING_START, 0, 0);

        int clientId = g_authManager.getClientId();

        bool appendixListMode = m_nameConfirmed && m_currentClientName.empty();
        HWND hWnd = m_hWnd;
        std::thread([hWnd, clientId, authToken, appendixListMode]() {
            std::wstring path;
            if (appendixListMode) {
                path = L"/api/v1/clients/my_appendices?client_id=" + std::to_wstring(clientId);
            }
            else {
                path = L"/api/v1/items?client_id=" + std::to_wstring(clientId);
            }

            auto response = g_httpsClient.get(path, authToken);

            if (IsWindow(hWnd)) {
                auto* payload = new std::optional<nlohmann::json>(response);
                if (!PostMessageW(hWnd, WM_SALES_DATA_READY, 0, reinterpret_cast<LPARAM>(payload))) {
                    delete payload;
                }
            }
            }).detach();

        for (HWND btn : m_buttons) {
            if (GetDlgCtrlID(btn) != ID_BTN_BACK) {
                EnableWindow(btn, FALSE);
            }
        }

        if (m_hSalesListView) {
            ShowWindow(m_hSalesListView, SW_SHOW);
        }

        if (m_hClientInfoLabel) {
            ShowWindow(m_hClientInfoLabel, SW_HIDE);
        }

        if (m_hBtnNext && !m_nameConfirmed) {
            ShowWindow(m_hBtnNext, SW_HIDE);
        }

        g_logger.info(L"loadSalesData: async request sent");
    }

    void createMySales() {
        clearWindow();
        m_currentState = WindowState::MY_SALES;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;

        createStandardHeader();

        int top = 270;

        m_hSalesStatusLabel = CreateWindowExW(0, L"STATIC", L"Выберите режим просмотра",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            50, top, screenWidth - 100, 50,
            m_hWnd, (HMENU)ID_STATIC_SALES_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hSalesStatusLabel, WM_SETFONT, (WPARAM)g_hFontLabel, TRUE);

        top += 70;

        int btnWidth = 500;
        int btnHeight = 100;
        int gap = 10;

        HWND hAllGoods = CreateWindowExW(0, L"BUTTON", L"Посмотреть все мои продажи",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, top, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_MY_SALES, g_hInstance, nullptr);
        m_buttons.push_back(hAllGoods);

        HWND hByAppendix = CreateWindowExW(0, L"BUTTON", L"Выбрать товары по приложению",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnWidth / 2, top + btnHeight + gap, btnWidth, btnHeight,
            m_hWnd, (HMENU)ID_BTN_SALES_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hByAppendix);

        top += (btnHeight + gap) * 2 + 20;

        int backHeight = 60;
        int backY = screenHeight - backHeight - 20;
        int totalHeight = 60;
        int totalY = backY - totalHeight - 10;

        int printHeight = 70;
        int printY = totalY - printHeight - 10;

        int nextHeight = 70;
        int nextY = printY - nextHeight - 10;

        int listHeight = nextY - top - 20;
        if (listHeight < 180) {
            listHeight = 180;
        }

        m_hSalesListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
            20, top, screenWidth - 40, listHeight,
            m_hWnd, (HMENU)ID_LIST_SALES, g_hInstance, nullptr);
        SendMessageW(m_hSalesListView, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        ShowWindow(m_hSalesListView, SW_HIDE);

        m_hBtnPrintTicket = CreateWindowExW(0, L"BUTTON", L"Распечатать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 250, printY, 500, printHeight,
            m_hWnd, (HMENU)ID_BTN_PRINT_TICKET, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnPrintTicket);

        m_hBtnNext = CreateWindowExW(0, L"BUTTON", L"Показать выбранное приложение",
            WS_CHILD | BS_PUSHBUTTON,
            centerX - 250, nextY, 500, nextHeight,
            m_hWnd, (HMENU)ID_BTN_NEXT, g_hInstance, nullptr);
        m_buttons.push_back(m_hBtnNext);
        ShowWindow(m_hBtnNext, SW_HIDE);

        m_hClientInfoLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_CENTER,
            50, totalY, screenWidth - 100, totalHeight,
            m_hWnd, (HMENU)ID_STATIC_PAID_QUEUE_COUNT, g_hInstance, nullptr);
        SendMessageW(m_hClientInfoLabel, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        ShowWindow(m_hClientInfoLabel, SW_HIDE);

        HWND hBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 100, backY, 200, backHeight,
            m_hWnd, (HMENU)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBack);

        styleButtons();
    }

    void createAddressesWindow() {
        g_logger.info(L"createAddressesWindow: ENTRY POINT");
        clearWindow();
        m_currentState = WindowState::ADDRESSES;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        createStandardHeader();
        int top = 270;
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
        createStandardHeader();
        int top = 270;
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
        if (m_currentState != WindowState::MY_SALES) {
            return;
        }

        g_logger.info(L"onSalesDataReady: received response");

        for (HWND btn : m_buttons) {
            EnableWindow(btn, TRUE);
        }

        if (!m_hSalesListView || !m_hSalesStatusLabel || !m_hClientInfoLabel) {
            return;
        }

        auto clearListAndColumns = [&]() {
            ListView_DeleteAllItems(m_hSalesListView);

            HWND hHeader = ListView_GetHeader(m_hSalesListView);
            if (hHeader) {
                int colCount = Header_GetItemCount(hHeader);
                for (int i = colCount - 1; i >= 0; --i) {
                    ListView_DeleteColumn(m_hSalesListView, i);
                }
            }
            };

        auto addColumns = [&](const std::vector<std::wstring>& headers, const std::vector<int>& widths) {
            LVCOLUMNW col = { 0 };
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

            for (size_t i = 0; i < headers.size(); ++i) {
                col.pszText = const_cast<LPWSTR>(headers[i].c_str());
                col.cx = widths[i];
                col.iSubItem = static_cast<int>(i);
                ListView_InsertColumn(m_hSalesListView, static_cast<int>(i), &col);
            }
            };

        // =====================================================================
        // ИЗМЕНЕНО: из-за добавления колонки «Сумма на руки» (позиция 4)
        // колонка «Состояние» смещена с индекса 6 на индекс 7.
        // Условие подсветки проданных (lParam=1 для NM_CUSTOMDRAW) должно
        // проверять ИМЕННО 7-й элемент (было 6).
        // =====================================================================
        auto addRow = [&](int index, const std::vector<std::wstring>& columns) {
            LVITEMW lvi = { 0 };
            lvi.iItem = index;

            LPARAM rowParam = 0;
            if (columns.size() > 7 && columns[7] == L"Продан") {
                rowParam = 1;
            }

            for (size_t i = 0; i < columns.size(); ++i) {
                lvi.iSubItem = static_cast<int>(i);
                lvi.pszText = const_cast<LPWSTR>(columns[i].c_str());

                if (i == 0) {
                    lvi.mask = LVIF_TEXT | LVIF_PARAM;
                    lvi.lParam = rowParam;
                    ListView_InsertItem(m_hSalesListView, &lvi);
                }
                else {
                    lvi.mask = LVIF_TEXT;
                    ListView_SetItem(m_hSalesListView, &lvi);
                }
            }
            };

        auto showTotal = [&](const std::wstring& prefix, double total) {
            std::wostringstream oss;
            oss << prefix << std::fixed << std::setprecision(2) << total << L" руб.";
            SetWindowTextW(m_hClientInfoLabel, oss.str().c_str());
            ShowWindow(m_hClientInfoLabel, SW_SHOW);
            InvalidateRect(m_hClientInfoLabel, NULL, TRUE);
            };

        auto getCurrentReportDate = []() -> std::wstring {
            auto now = std::chrono::system_clock::now();
            auto timeNow = std::chrono::system_clock::to_time_t(now);
            struct tm tmInfo;
            localtime_s(&tmInfo, &timeNow);
            std::wstringstream ss;
            ss << std::put_time(&tmInfo, L"%d.%m.%Y");
            return ss.str();
            };

        auto formatEpoch = [](long long ts) -> std::wstring {
            if (ts <= 0) {
                return L"";
            }

            time_t t = static_cast<time_t>(ts);
            struct tm tmInfo;
            localtime_s(&tmInfo, &t);
            std::wstringstream ss;
            ss << std::put_time(&tmInfo, L"%d.%m.%Y %H:%M");
            return ss.str();
            };

        // =====================================================================
        // МАТЕМАТИКА ВОЗНАГРАЖДЕНИЯ (НЕ МЕНЯЕТСЯ):
        //   1..7000 руб.          -> 50%
        //   >7000 и <50000 руб.   -> 30%
        //   >=50000 руб.          -> 20%
        // =====================================================================
        auto getClientPercent = [](double price) -> double {
            if (price >= 1.0 && price <= 7000.0) {
                return 0.5;
            }

            if (price > 7000.0 && price < 50000.0) {
                return 0.3;
            }

            if (price >= 50000.0) {
                return 0.2;
            }

            return 0.0;
            };

        // =====================================================================
        // ВЕТКА 1: результат /api/v1/clients/appendix_by_number
        // (путь «Мои продажи → Выбрать товары по приложению»).
        //
        // ИЗМЕНЕНИЯ:
        //   - заголовок «№ товара» заменён на «№ приложения»;
        //   - вставлена колонка «Сумма на руки» (индекс 4);
        //   - номер приложения берётся из m_currentClientName
        //     (он установлен showAppendixInputDialog() в ID_BTN_SALES_BACK);
        //   - штрих-код (article) по-прежнему используется ТОЛЬКО в фоне
        //     для дедупликации строк (usedArticles), в таблицу не выводится;
        //   - добавлено полное логгирование по каждой строке.
        // =====================================================================
        if (m_pendingAppendixItems.has_value()) {
            const nlohmann::json resp = *m_pendingAppendixItems;  // КОПИЯ

            if (m_currentClientName.empty()) {
                m_pendingAppendixItems.reset();
                SetWindowTextW(m_hSalesStatusLabel, L"Не указан номер приложения");
                ShowWindow(m_hClientInfoLabel, SW_HIDE);
                if (m_hBtnNext) {
                    ShowWindow(m_hBtnNext, SW_HIDE);
                }
                InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
                return;
            }

            const auto& items =
                (resp.contains("items") && resp["items"].is_array())
                ? resp["items"]
                : nlohmann::json::array();

            clearListAndColumns();

            // ИЗМЕНЕНО: «№ товара» -> «№ приложения», добавлена «Сумма на руки».
            std::vector<std::wstring> headers = {
                L"Дата отчёта",
                L"№ приложения",
                L"Наименование",
                L"Цена за ед.",
                L"Сумма на руки",
                L"Кол-во",
                L"Дата продажи",
                L"Состояние"
            };
            std::vector<int> widths = {
                120,
                140,
                260,
                120,
                140,
                80,
                140,
                180
            };
            addColumns(headers, widths);

            std::wstring reportDate = getCurrentReportDate();
            std::unordered_set<std::wstring> usedArticles;
            double totalPayout = 0.0;
            int index = 0;

            // ОДИН проход по всем товарам (sold/expired/returned/...).
            // Статус влияет только на подсветку и на итог «К выдаче на руки».
            for (const auto& item : items) {
                std::string statusRaw = item.value("status", "");
                bool isSold = (statusRaw == "sold");

                // Штрих-код используется ТОЛЬКО в фоне для дедупликации.
                // В таблицу выводится НЕ он, а номер приложения.
                std::wstring article =
                    utf8_to_wstring(item.value("barcode", ""));
                if (article.empty()) {
                    int itemId = item.value("id", 0);
                    if (itemId > 0) {
                        article = L"SHOP" + std::to_wstring(itemId);
                    }
                    else {
                        article = std::to_wstring(
                            item.value("item_number", 0));
                    }
                }

                if (usedArticles.find(article) != usedArticles.end()) {
                    continue;
                }
                usedArticles.insert(article);

                double price = item.value("estimated_price", 0.0);
                std::wstring saleDate;
                if (item.contains("sale_date") &&
                    !item["sale_date"].is_null()) {
                    saleDate = formatEpoch(
                        item["sale_date"].get<long long>());
                }

                // Математика: 50/30/20 % через существующую лямбду.
                if (isSold) {
                    double percent = getClientPercent(price);
                    totalPayout += price * percent;
                }

                // ИЗМЕНЕНО: в колонке «№ приложения» выводится номер
                // приложения, введённый пользователем (m_currentClientName).
                // Сумма на руки считается по цене.
                double payout = price * getClientPercent(price);
                std::wstring appendixDisplay = m_currentClientName;

                std::vector<std::wstring> columns;
                columns.push_back(reportDate);              // 0
                columns.push_back(appendixDisplay);         // 1
                columns.push_back(                         // 2
                    utf8_to_wstring(item.value("description", "")));
                columns.push_back(to_wstring(price));       // 3
                columns.push_back(to_wstring(payout));      // 4 (новое)
                columns.push_back(L"1");                    // 5
                columns.push_back(saleDate);                // 6
                columns.push_back(translateStatus(statusRaw)); // 7

                g_logger.info(L"onSalesDataReady[branch1]: row " +
                    std::to_wstring(index) +
                    L" appendix='" + appendixDisplay + L"'" +
                    L" article(background)='" + article + L"'" +
                    L" price=" + to_wstring(price) +
                    L" payout=" + to_wstring(payout) +
                    L" status='" + translateStatus(statusRaw) + L"'");

                addRow(index, columns);
                ++index;
            }

            m_pendingAppendixItems.reset();

            ShowWindow(m_hSalesListView, SW_SHOW);
            if (m_hBtnNext) {
                ShowWindow(m_hBtnNext, SW_HIDE);
            }

            if (index == 0) {
                std::wstring statusText =
                    L"В приложении №" + m_currentClientName +
                    L" нет товаров";
                SetWindowTextW(m_hSalesStatusLabel, statusText.c_str());
                ShowWindow(m_hClientInfoLabel, SW_HIDE);
            }
            else {
                std::wstring statusText =
                    L"Товары приложения №" + m_currentClientName +
                    L": " + std::to_wstring(index);
                SetWindowTextW(m_hSalesStatusLabel, statusText.c_str());
                std::wstring totalPrefix =
                    L"К выдаче на руки: ";
                showTotal(totalPrefix, totalPayout);
            }

            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.info(L"onSalesDataReady: pending appendix items drawn, "
                L"count=" + std::to_wstring(index) +
                L", totalPayout=" + std::to_wstring(totalPayout));
            return;
        }

        if (!response) {
            SetWindowTextW(m_hSalesStatusLabel, L"Ошибка соединения с сервером");
            ShowWindow(m_hClientInfoLabel, SW_HIDE);

            if (m_hBtnNext) {
                ShowWindow(m_hBtnNext, SW_HIDE);
            }

            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            g_logger.error(L"onSalesDataReady: no response");
            return;
        }

        // =====================================================================
        // ВЕТКА 2: список приложений клиента (/api/v1/clients/my_appendices).
        // НЕ МЕНЯЕТСЯ — это отдельный экран со списком номеров приложений.
        // =====================================================================
        if (response->contains("appendices") && (*response)["appendices"].is_array()) {
            const auto& appendices = (*response)["appendices"];

            clearListAndColumns();

            std::vector<std::wstring> headers = {
                L"Номер приложения"
            };

            std::vector<int> widths = { 300 };
            addColumns(headers, widths);

            int index = 0;

            for (const auto& app : appendices) {
                long long appendixNumber = app.value("appendix_number", 0LL);

                if (appendixNumber <= 0) {
                    continue;
                }

                std::vector<std::wstring> columns;
                columns.push_back(std::to_wstring(appendixNumber));
                addRow(index, columns);
                ++index;
            }

            if (index == 0) {
                SetWindowTextW(m_hSalesStatusLabel, L"Нет приложений к договору");
                ShowWindow(m_hClientInfoLabel, SW_HIDE);

                if (m_hBtnNext) {
                    ShowWindow(m_hBtnNext, SW_HIDE);
                }
            }
            else {
                SetWindowTextW(m_hSalesStatusLabel,
                    L"Выберите номер приложения и нажмите «Показать выбранное приложение»");

                ShowWindow(m_hClientInfoLabel, SW_HIDE);

                if (m_hBtnNext) {
                    ShowWindow(m_hBtnNext, SW_SHOW);
                }
            }

            ShowWindow(m_hSalesListView, SW_SHOW);
            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);

            g_logger.info(L"onSalesDataReady: client appendices loaded, count=" + std::to_wstring(index));

            return;
        }

        // =====================================================================
        // ВЕТКА 3: /api/v1/items (путь «Посмотреть все мои продажи» и
        //          путь «Показать выбранное приложение» → appendixMode=true).
        //
        // ИЗМЕНЕНИЯ:
        //   - заголовок «№ товара» заменён на «№ приложения»;
        //   - вставлена колонка «Сумма на руки» (индекс 4);
        //   - номер приложения:
        //       appendixMode == false -> item.value("appendix_number", 0LL)
        //       appendixMode == true  -> m_currentClientName
        //   - штрих-код (article) по-прежнему используется ТОЛЬКО в фоне
        //     для дедупликации строк (usedArticles);
        //   - добавлено полное логгирование по каждой строке.
        // =====================================================================
        if (response->contains("items") && (*response)["items"].is_array()) {
            const auto& items = (*response)["items"];

            clearListAndColumns();

            // ИЗМЕНЕНО: «№ товара» -> «№ приложения», добавлена «Сумма на руки».
            std::vector<std::wstring> headers = {
                L"Дата отчёта",
                L"№ приложения",
                L"Наименование",
                L"Цена за ед.",
                L"Сумма на руки",
                L"Кол-во",
                L"Дата продажи",
                L"Состояние"
            };

            std::vector<int> widths = {
                120,
                140,
                260,
                120,
                140,
                80,
                140,
                180
            };

            addColumns(headers, widths);

            std::wstring reportDate = getCurrentReportDate();
            std::unordered_set<std::wstring> usedArticles;
            double totalPayout = 0.0;
            int index = 0;
            bool appendixMode = !m_currentClientName.empty();

            for (int pass = 0; pass < 2; ++pass) {
                for (const auto& item : items) {
                    long long appendixNumber = item.value("appendix_number", 0LL);

                    if (appendixMode &&
                        std::to_wstring(appendixNumber) != m_currentClientName) {
                        continue;
                    }

                    std::string statusRaw = item.value("status", "");
                    bool isSold = (statusRaw == "sold");

                    if (!appendixMode && !isSold) {
                        continue;
                    }

                    if (pass == 0 && !isSold) {
                        continue;
                    }

                    if (pass == 1 && isSold) {
                        continue;
                    }

                    // Штрих-код — только в фоне, для дедупликации.
                    std::wstring article = utf8_to_wstring(item.value("barcode", ""));

                    if (article.empty()) {
                        int itemId = item.value("id", 0);
                        if (itemId > 0) {
                            article = L"SHOP" + std::to_wstring(itemId);
                        }
                        else {
                            article = std::to_wstring(item.value("item_number", 0));
                        }
                    }

                    if (usedArticles.find(article) != usedArticles.end()) {
                        continue;
                    }

                    usedArticles.insert(article);

                    double price = item.value("estimated_price", 0.0);

                    std::wstring saleDate;
                    if (item.contains("sale_date") && !item["sale_date"].is_null()) {
                        saleDate = formatEpoch(item["sale_date"].get<long long>());
                    }

                    if (isSold) {
                        double percent = getClientPercent(price);
                        totalPayout += price * percent;
                    }

                    // ИЗМЕНЕНО: номер приложения для отображения.
                    // В appendixMode используется уже введённый m_currentClientName,
                    // иначе — per-item appendix_number из БД (COALESCE → 0).
                    std::wstring appendixDisplay = appendixMode
                        ? m_currentClientName
                        : std::to_wstring(appendixNumber);
                    double payout = price * getClientPercent(price);

                    std::vector<std::wstring> columns;
                    columns.push_back(reportDate);              // 0
                    columns.push_back(appendixDisplay);         // 1
                    columns.push_back(utf8_to_wstring(item.value("description", ""))); // 2
                    columns.push_back(to_wstring(price));       // 3
                    columns.push_back(to_wstring(payout));      // 4 (новое)
                    columns.push_back(L"1");                    // 5
                    columns.push_back(saleDate);                // 6
                    columns.push_back(translateStatus(statusRaw)); // 7

                    g_logger.info(L"onSalesDataReady[branch3]: row " +
                        std::to_wstring(index) +
                        L" pass=" + std::to_wstring(pass) +
                        L" appendix='" + appendixDisplay + L"'" +
                        L" article(background)='" + article + L"'" +
                        L" price=" + to_wstring(price) +
                        L" payout=" + to_wstring(payout) +
                        L" sold=" + (isSold ? L"true" : L"false") +
                        L" status='" + translateStatus(statusRaw) + L"'");

                    addRow(index, columns);
                    ++index;
                }
            }

            ShowWindow(m_hSalesListView, SW_SHOW);

            if (m_hBtnNext) {
                ShowWindow(m_hBtnNext, SW_HIDE);
            }

            if (!appendixMode) {
                std::wstring statusText =
                    L"Проданные товары: " + std::to_wstring(index);
                SetWindowTextW(m_hSalesStatusLabel, statusText.c_str());
                showTotal(L"К выдаче на руки: ", totalPayout);
            }
            else {
                std::wstring statusText =
                    L"Товары приложения №" + m_currentClientName +
                    L": " + std::to_wstring(index);
                SetWindowTextW(m_hSalesStatusLabel, statusText.c_str());

                std::wstring totalPrefix =
                    L"К выдаче №" + m_currentClientName + L": ";
                showTotal(totalPrefix, totalPayout);
            }

            InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
            return;
        }

        SetWindowTextW(m_hSalesStatusLabel, L"Некорректный ответ сервера");
        ShowWindow(m_hClientInfoLabel, SW_HIDE);

        if (m_hBtnNext) {
            ShowWindow(m_hBtnNext, SW_HIDE);
        }

        InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
        g_logger.error(L"onSalesDataReady: invalid response format");
    }

    // маппер статусов товаров (чтобы не переделывать БД на сервере)
    static std::wstring translateStatus(const std::string& status) {
        if (status == "pending")   return L"В зале";
        if (status == "accepted")  return L"Принят";
        if (status == "expired")  return L"Истек срок";
        if (status == "compensated")return L"Компенсирован";
        if (status == "on_sale")   return L"На продаже";
        if (status == "sold")      return L"Продан";
        if (status == "returned")  return L"Возвращён";
        if (status == "rejected")  return L"Отклонён";
        if (status == "utilized")  return L"Утилизирован";
        if (status == "paid_out")  return L"Выплачен";
        // Если пришёл неизвестный статус — вернём исходный, но в исходном регистре с заглавной
        if (status.empty()) return L"—";
        std::wstring w = utf8_to_wstring(status);
        if (!w.empty()) w[0] = static_cast<wchar_t>(towupper(w[0]));
        return w;
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
        createStandardHeader();
        int top = 260;
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
        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            g_logger.error(L"User not logged in or clientId is 0");
            MessageBoxW(m_hWnd, L"Пользователь не авторизован", L"Ошибка", MB_OK);
            return;
        }

        int clientId = g_authManager.getClientId();
        std::wstring clientName = g_authManager.getFullName();
        std::wstring clientPhone = g_authManager.getPhone();
        std::wstring authToken = g_authManager.getAuthToken();

        auto ticketOpt = g_queueManager.getTicket(
            clientId, type,
            (type == QueueType::PAID || type == QueueType::GENERAL) ? Config::MAX_ITEMS_GENERAL_QUEUE : 1,
            authToken
        );

        if (ticketOpt) {
            m_currentTicket = ticketOpt.value();
            showTicketIssued();
            g_printer.printTicket(m_currentTicket, clientName, clientPhone);
        }
        else {
            g_logger.error(L"Failed to get ticket for client " + std::to_wstring(clientId));
            MessageBoxW(m_hWnd, L"Не удалось получить талон", L"Ошибка", MB_OK);
        }
    }

    void showTicketIssued(bool showCancelButtons = false) {
        clearWindow();
        m_currentState = WindowState::TICKET_ISSUED;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int centerX = screenWidth / 2;
        std::wstringstream ss;
        ss << L"ВАШ ТАЛОН: " << m_currentTicket.ticketNumber << L"\n";
        ss << L"Позиция в очереди: " << m_currentTicket.position << L"\n";
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

        int btnW = 300, btnH = 60, gap = 20;
        int btnStartY = 450;

        // Кнопка "ВЕРНУТЬСЯ В МЕНЮ" — всегда присутствует
        m_buttons.push_back(CreateWindowExW(0, L"BUTTON", L"ВЕРНУТЬСЯ В МЕНЮ",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, btnStartY, btnW, btnH,
            m_hWnd, (HMENU)(INT_PTR)ID_BTN_BACK, g_hInstance, nullptr));

        styleButtons();
        std::wstring logMsg = std::wstring(L"showTicketIssued: ticket displayed") +
            (showCancelButtons ? L" with cancellation buttons" : L"");
        g_logger.info(logMsg);
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
        ticket.position = response->value("position", 1);
        ticket.itemsCount = 1;
        std::string windowUtf8 = response->value("window_number", "1");
        ticket.windowNumber = utf8_to_wstring(windowUtf8);
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

    // =========================================================================
    // МЕТОД: showExtra20Payment
    // Окно оплаты услуги "+20 ПОЗИЦИЙ" за 200 руб. (тестовый режим)
    // =========================================================================
    void showExtra20Payment() {
        clearWindow();
        m_currentState = WindowState::EXTRA_20_PAYMENT;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;
        createStandardHeader();

        std::wstring countText = L"Вы выбрали платную услугу +20 ПОЗИЦИЙ.\n"
            L"Стоимость услуги 200 руб.\n\n"
            L"При нажатии кнопки «Оплатить» будет произведена оплата 200 рублей.";

        HWND hText = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 200, 800, 200,
            m_hWnd, (HMENU)IDC_STATIC_EXTRA20_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 50;


        // Кнопка "Оплата банковской картой"
        HWND btnCard = CreateWindowExW(0, L"BUTTON", L"Оплата банковской картой",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAY_CARD, g_hInstance, nullptr);
        m_buttons.push_back(btnCard);

        // Кнопка "Назад" (сдвинута вниз)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + 4 * (btnH + gap), btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);


        styleButtons();
        g_logger.info(L"Extra20 payment window shown with payment option (100 RUB)");
    }

    // =========================================================================
    // МЕТОД: onExtra20PaymentSuccess
    // Реальная оплата 200 руб. через PaymentManager с автоматической отменой при ошибке
    // =========================================================================
    void onExtra20PaymentSuccess() {
        g_logger.info(L"Extra20Payment: start processing payment");

        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"Extra20Payment: attempted without valid login");
            createAdditionalServicesMenu();
            return;
        }

        // ШАГ 1: Вызов реальной оплаты 200 рублей (20000 копеек)
        g_logger.info(L"Extra20Payment: calling PaymentManager.ProcessPayment(20000)");
        auto paymentResult = g_paymentManager.ProcessPayment(20000);

        if (!paymentResult.has_value()) {
            g_logger.error(L"Extra20Payment: ProcessPayment returned no value (critical error)");
            MessageBoxW(m_hWnd,
                L"Сбой связи с платёжным терминалом.\nПовторите попытку позже.",
                L"Ошибка оплаты", MB_OK | MB_ICONERROR);
            return;
        }

        if (!paymentResult->isSuccess) {
            std::string errDesc = g_paymentManager.extractField(paymentResult->rawResponse, "19");
            std::wstring wErrDesc;
            if (errDesc.empty()) {
                wErrDesc = L"Транзакция отклонена банком.";
            }
            else {
                // Конвертация из windows-1251 (кодировка ответа DualConnector)
                int size_needed = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
                wErrDesc.resize(size_needed);
                MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], size_needed);
            }
            g_logger.error(L"Extra20Payment: Payment REJECTED. Reason: " + wErrDesc);
            MessageBoxW(m_hWnd,
                (L"Оплата не прошла.\nПричина: " + wErrDesc).c_str(),
                L"Отказ в оплате", MB_OK | MB_ICONWARNING);
            return;
        }

        g_logger.info(L"Extra20Payment: Payment APPROVED. AuthCode=" +
            std::wstring(paymentResult->authCode.begin(), paymentResult->authCode.end()) +
            L", RRN=" + std::wstring(paymentResult->rrn.begin(), paymentResult->rrn.end()));

        // ШАГ 2: Получение талона
        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();

        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::EXTRA_20, 20, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"Extra20Payment: failed to get ticket for client " + std::to_wstring(clientId));
            g_logger.warning(L"Extra20Payment: emergency cancellation due to ticket failure");
            // АВТОМАТИЧЕСКАЯ АВАРИЙНАЯ ОТМЕНА при ошибке получения талона
            g_paymentManager.ProcessEmergencyCancellation();
            return;
        }

        m_currentTicket = ticketOpt.value();

        // ШАГ 3: Печать талона
        bool printed = g_printer.printTicket(m_currentTicket, g_authManager.getFullName(), g_authManager.getPhone());
        if (printed) {
            g_logger.info(L"Extra20Payment: ticket printed successfully: " + m_currentTicket.ticketNumber);
        }
        else {
            g_logger.warning(L"Extra20Payment: ticket print failed, saved to file: " + m_currentTicket.ticketNumber);
            MessageBoxW(m_hWnd, L"Талон не напечатан, но сохранён в файл.", L"Предупреждение", MB_OK);
        }

        showTicketIssued(true);  //показываем кнопки отмены

        g_logger.info(L"Extra20Payment: completed successfully");
    }

    void showTrustAcceptanceWindow() {
        clearWindow();
        m_currentState = WindowState::SUBMIT_MENU;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int centerX = screenWidth / 2;
        int centerY = screenHeight / 2;
        createStandardHeader();
        HWND hText = CreateWindowExW(0, L"STATIC",
            L"Вы выбрали НА ДОВЕРИИ.\n"
            L"Вы без очереди оставляете товар\n"
            L"и сведения о себе.\n"
            L"Мы оформим их без Вашего участия.\n"
            L"Вещам БЕЗ ОЦЕНКИ будет присвоена\n"
            L"цена 1 рубль.\n"
            L"ВАЖНО: магазин НЕ несет \n"
            L"ответственности за количество,\n"
            L"качество и сохранность Ваших вещей.",
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
        ps << L"Я, " << nameStr << L" доверяю Вам оценить\r\n";
        ps << L"вещи на Ваше усмотрение, без согласования\r\n";
        ps << L"цен со мной.Согласен на обработку моих\r\n";
        ps << L"персональных данных.\r\n";
        ps << L"Условия:\r\n";
        ps << L"Комиссионный магазин принимает товар по\r\n";
        ps << L"услуге «Доверие», \r\n";
        ps << L"без проверки содержимого пакетов, коробок\r\n";
        ps << L"и иной упаковки.\r\n";
        ps << L"Магазин не несет ответственности за\r\n";
        ps << L"вложенные в пакет вещи, их наличие \r\n";
        ps << L"или отсутствие, сохранность пакета\r\n";
        ps << L"и его возврат.\r\n";
        ps << L"Вещи, которые товаровед не примет\r\n";
        ps << L"по состоянию, качеству или др.причинам,\r\n";
        ps << L"автоматически отправляются в категорию\r\n";
        ps << L"«на утилизацию» и выставляются\r\n";
        ps << L"за один рубль без составления перечня.\r\n\r\n";
        ps << L"Подписывая данный бланк, я подтверждаю,\r\n";
        ps << L"что ознакомлен с условиями и согласен\r\n";
        ps << L"с ними. Также я соглашаюсь с возможной \r\n";
        ps << L"утратой пакета или части его содержимого\r\n";
        ps << L"без претензий с моей стороны.\r\n\r\n";
        ps << L"Мой телефон: " << phoneStr << L"\r\n";
        ps << L"(телефон необходим для возможности\r\n";
        ps << L"осуществления удаленной проверки продаж)\r\n";
        ps << L"ВНИМАНИЕ – мы не звоним Вам!\r\n";
        ps << L"Если Вы сдаете вещи впервые, то\r\n";
        ps << L"заполните данные для регистрации : \r\n";
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
        createStandardHeader();

        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::PAID, authToken);

        std::wostringstream ss;
        ss << L"Вы выбрали ПЛАТНЫЙ ПРИЕМ (200 руб.).\n"
            << L"При выборе данной опции, очередь короче,\n"
            << L"но постановка в данную очередь возможна при оплате 200 руб.\n"
            << L"Количество товаров ограничено 20 наименованиями.\n\n"
            << L"Стоимость услуги 200 руб.\n\n"
            << L"Количество ожидающих в очереди: " << queueCount;

        std::wstring countText = ss.str();

        HWND hText = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 260, 800, 300,
            m_hWnd, (HMENU)IDC_STATIC_PAID_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);

        g_logger.info(L"PaidAcceptance: single text block created (800x300 at centerY-260), queue count merged into text");

        int btnW = 300, btnH = 70, gap = 20;
        int startY = centerY + 70;

        HWND btnPrint = CreateWindowExW(0, L"BUTTON", L"Оплатить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_PRINT, g_hInstance, nullptr);
        m_buttons.push_back(btnPrint);

        // Кнопка "Простая отмена"
        HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Простая отмена платежа",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + btnH + gap, btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_CANCEL, g_hInstance, nullptr);
        m_buttons.push_back(btnCancel);

        // Кнопка "Аварийная отмена"
        HWND btnEmergency = CreateWindowExW(0, L"BUTTON", L"Аварийная отмена платежа",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + 2 * (btnH + gap), btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_EMERGENCY_CANCEL, g_hInstance, nullptr);
        m_buttons.push_back(btnEmergency);

        // Кнопка "Назад" (сдвинута вниз)
        HWND btnBack = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - btnW / 2, startY + 3 * (btnH + gap), btnW, btnH,
            m_hWnd, (HMENU)ID_BTN_PAID_BACK, g_hInstance, nullptr);
        m_buttons.push_back(btnBack);

        styleButtons();
        g_logger.info(L"Paid acceptance window shown, queue count: " + std::to_wstring(queueCount));
    }

    void onPaidAcceptancePrint() {
        g_logger.info(L"PaidAcceptance: start payment flow");

        if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
            MessageBoxW(m_hWnd, L"Вы не авторизованы. Пожалуйста, войдите.", L"Ошибка", MB_OK);
            g_logger.warning(L"PaidAcceptance print attempted without valid login");
            createSubmitMenu();
            return;
        }

        int clientId = g_authManager.getClientId();
        std::wstring authToken = g_authManager.getAuthToken();

        // =========================================================================
        // ШАГ 1. ИНИЦИАЛИЗАЦИЯ ТЕРМИНАЛА PAX Q25 ЧЕРЕЗ DUALCONNECTOR 2.0
        // =========================================================================
        g_logger.info(L"PaidAcceptance: calling PaymentManager.ProcessPayment(10 000)");
        auto paymentResult = g_paymentManager.ProcessPayment(Config::PAID_ACCEPTANCE_PRICE * 100);

        if (!paymentResult.has_value()) {
            g_logger.error(L"PaidAcceptance: ProcessPayment returned no value (critical error)");
            MessageBoxW(m_hWnd,
                L"Сбой связи с платёжным терминалом.\nПовторите попытку позже.",
                L"Ошибка оплаты", MB_OK | MB_ICONERROR);
            return;
        }

        if (!paymentResult->isSuccess) {
            std::string errDesc = g_paymentManager.extractField(paymentResult->rawResponse, "19");
            std::wstring wErrDesc;
            if (errDesc.empty()) {
                wErrDesc = L"Транзакция отклонена банком.";
            }
            else {
                int size_needed = MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), NULL, 0);
                wErrDesc.resize(size_needed);
                MultiByteToWideChar(1251, 0, errDesc.c_str(), (int)errDesc.size(), &wErrDesc[0], size_needed);
            }
            g_logger.error(L"PaidAcceptance: Payment REJECTED. Reason: " + wErrDesc);
            MessageBoxW(m_hWnd,
                (L"Оплата не прошла.\nПричина: " + wErrDesc).c_str(),
                L"Отказ в оплате", MB_OK | MB_ICONWARNING);
            return;
        }

        g_logger.info(L"PaidAcceptance: Payment APPROVED. AuthCode=" +
            std::wstring(paymentResult->authCode.begin(), paymentResult->authCode.end()) +
            L", RRN=" + std::wstring(paymentResult->rrn.begin(), paymentResult->rrn.end()));

        // =========================================================================
        // ШАГ 2. ПОЛУЧЕНИЕ ТАЛОНА (логика и математика НЕ изменены)
        // =========================================================================
        int itemsCount = Config::MAX_ITEMS_GENERAL_QUEUE;
        auto ticketOpt = g_queueManager.getTicket(clientId, QueueType::PAID, itemsCount, authToken);
        if (!ticketOpt) {
            MessageBoxW(m_hWnd, L"Не удалось получить талон. Проверьте соединение с сервером.", L"Ошибка", MB_OK);
            g_logger.error(L"PaidAcceptance: failed to get ticket for client " + std::to_wstring(clientId));

            g_logger.warning(L"PaidAcceptance: emergency cancellation due to ticket failure");
            g_paymentManager.ProcessEmergencyCancellation();
            return;
        }

        m_currentTicket = ticketOpt.value();

        // =========================================================================
        // ШАГ 3. ПЕЧАТЬ ТАЛОНА (логика НЕ изменена)
        // =========================================================================
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

        showTicketIssued(true);  // показываем кнопки отмены
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
        createStandardHeader();
        std::wstring authToken = g_authManager.getAuthToken();
        int queueCount = g_queueManager.getDailyCount(QueueType::EXPENSIVE, authToken);
        std::wstring countText = L"Количество ожидающих в очереди: " + std::to_wstring(queueCount);
        countText =
            L"Вы выбрали ДОРОГОЙ ТОВАР (>5000 руб.).\n"
            L"Если у Вас есть вещи стоимостью более 5000 руб., то смело выбирайте этот вариант.\n"
            L"В течение 10-15 минут Вас примет самый опытный товаровед нашего магазина\n"
            L"(в данном случае принимаются вещи, оценочная стоимость каждой из которых более 5000 руб.).\n\n"
            + countText;
        HWND hText = CreateWindowExW(0, L"STATIC", countText.c_str(),
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            centerX - 400, centerY - 260, 800, 300,
            m_hWnd, (HMENU)IDC_STATIC_EXPENSIVE_TEXT, g_hInstance, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
        g_logger.info(L"ExpensiveAcceptance: single text block created (800x300 at centerY-260), queue count merged into text");
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

    // НОВЫЕ МЕТОДЫ ДЛЯ ТЕСТИРОВАНИЯ ПЛАТЕЖЕЙ (БЕЗ ЗАГЛУШЕК)
    void onTestPayment() {
        g_logger.info(L"onTestPayment: Initiating test payment of 200 RUB (20 000 kopecks)");
        auto resp = g_paymentManager.ProcessPayment(10000);
        if (resp && resp->isSuccess) {
            std::wstring msg = L"Оплата успешна!\nКод авторизации: " + std::wstring(resp->authCode.begin(), resp->authCode.end()) +
                L"\nRRN: " + std::wstring(resp->rrn.begin(), resp->rrn.end());
            MessageBoxW(m_hWnd, msg.c_str(), L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"onTestPayment: Success. AuthCode=" + std::wstring(resp->authCode.begin(), resp->authCode.end()));
        }
        else {
            std::string errDesc = g_paymentManager.extractField(resp ? resp->rawResponse : "", "19");
            MessageBoxW(m_hWnd, (L"Оплата отклонена или произошла ошибка.\n" + std::wstring(errDesc.begin(), errDesc.end())).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"onTestPayment: Failed or rejected.");
        }
    }

    void onTestCancellation() {
        g_logger.info(L"onTestCancellation: Initiating cancellation using last successful transaction data");

        // Получаем реальные данные последней успешной операции из PaymentManager
        auto [hasData, authCode, rrn, amountStr] = g_paymentManager.GetLastTransactionData();

        if (!hasData) {
            MessageBoxW(m_hWnd, L"Нет данных о последней успешной операции для отмены.\nСначала проведите успешную оплату.", L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.warning(L"onTestCancellation: Aborted. No previous successful transaction found in state.");
            return;
        }

        int amount = std::stoi(amountStr);
        g_logger.info(L"onTestCancellation: Using real data -> Amount: " + std::to_wstring(amount) +
            L", AuthCode: " + std::wstring(authCode.begin(), authCode.end()) +
            L", RRN: " + std::wstring(rrn.begin(), rrn.end()));

        auto resp = g_paymentManager.ProcessCancellation(amount, authCode, rrn);

        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Отмена успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"onTestCancellation: Success.");
        }
        else {
            std::string errDesc = g_paymentManager.extractField(resp ? resp->rawResponse : "", "19");
            MessageBoxW(m_hWnd, (L"Отмена не удалась.\n" + std::wstring(errDesc.begin(), errDesc.end())).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"onTestCancellation: Failed.");
        }
    }

    void onTestEmergencyCancellation() {
        g_logger.info(L"onTestEmergencyCancellation: Initiating emergency cancellation");
        auto resp = g_paymentManager.ProcessEmergencyCancellation();
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Аварийная отмена успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"onTestEmergencyCancellation: Success.");
        }
        else {
            MessageBoxW(m_hWnd, L"Аварийная отмена не удалась (возможно, нет предыдущей успешной операции или она уже была отменена).", L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"onTestEmergencyCancellation: Failed.");
        }
    }

    // =========================================================================
    // МЕТОДЫ ОТМЕНЫ ПЛАТЕЖА ДЛЯ "ПЛАТНЫЙ ПРИЕМ"
    // =========================================================================
    void onPaidAcceptanceCancel() {
        g_logger.info(L"PaidAcceptance: start simple cancellation");

        auto [hasData, authCode, rrn, amountStr] = g_paymentManager.GetLastTransactionData();
        if (!hasData) {
            MessageBoxW(m_hWnd, L"Нет данных о последней успешной операции.\nСначала проведите успешную оплату.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.warning(L"PaidAcceptance: cancellation aborted - no previous transaction");
            return;
        }

        int amount = std::stoi(amountStr);
        g_logger.info(L"PaidAcceptance: cancelling transaction - Amount: " + std::to_wstring(amount) +
            L", AuthCode: " + std::wstring(authCode.begin(), authCode.end()) +
            L", RRN: " + std::wstring(rrn.begin(), rrn.end()));

        auto resp = g_paymentManager.ProcessCancellation(amount, authCode, rrn);
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Отмена платежа успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"PaidAcceptance: cancellation successful");
        }
        else {
            std::string errDesc = g_paymentManager.extractField(resp ? resp->rawResponse : "", "19");
            std::wstring wErrDesc = errDesc.empty() ? L"Неизвестная ошибка" : utf8_to_wstring(errDesc);
            MessageBoxW(m_hWnd, (L"Отмена не удалась.\n" + wErrDesc).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"PaidAcceptance: cancellation failed - " + wErrDesc);
        }
    }

    void onPaidAcceptanceEmergencyCancel() {
        g_logger.info(L"PaidAcceptance: start emergency cancellation");

        auto resp = g_paymentManager.ProcessEmergencyCancellation();
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Аварийная отмена успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"PaidAcceptance: emergency cancellation successful");
        }
        else {
            MessageBoxW(m_hWnd, L"Аварийная отмена не удалась.\nВозможно, нет предыдущей успешной операции.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"PaidAcceptance: emergency cancellation failed");
        }
    }

    // =========================================================================
    // МЕТОДЫ ОТМЕНЫ ПЛАТЕЖА ДЛЯ "ВЫБОР ТОВАРОВЕДА"
    // =========================================================================
    void onAppraiserCancel() {
        g_logger.info(L"AppraiserSelection: start simple cancellation");

        auto [hasData, authCode, rrn, amountStr] = g_paymentManager.GetLastTransactionData();
        if (!hasData) {
            MessageBoxW(m_hWnd, L"Нет данных о последней успешной операции.\nСначала проведите успешную оплату.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.warning(L"AppraiserSelection: cancellation aborted - no previous transaction");
            return;
        }

        int amount = std::stoi(amountStr);
        g_logger.info(L"AppraiserSelection: cancelling transaction - Amount: " + std::to_wstring(amount) +
            L", AuthCode: " + std::wstring(authCode.begin(), authCode.end()) +
            L", RRN: " + std::wstring(rrn.begin(), rrn.end()));

        auto resp = g_paymentManager.ProcessCancellation(amount, authCode, rrn);
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Отмена платежа успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"AppraiserSelection: cancellation successful");
        }
        else {
            std::string errDesc = g_paymentManager.extractField(resp ? resp->rawResponse : "", "19");
            std::wstring wErrDesc = errDesc.empty() ? L"Неизвестная ошибка" : utf8_to_wstring(errDesc);
            MessageBoxW(m_hWnd, (L"Отмена не удалась.\n" + wErrDesc).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"AppraiserSelection: cancellation failed - " + wErrDesc);
        }
    }

    void onAppraiserEmergencyCancel() {
        g_logger.info(L"AppraiserSelection: start emergency cancellation");

        auto resp = g_paymentManager.ProcessEmergencyCancellation();
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Аварийная отмена успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"AppraiserSelection: emergency cancellation successful");
        }
        else {
            MessageBoxW(m_hWnd, L"Аварийная отмена не удалась.\nВозможно, нет предыдущей успешной операции.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"AppraiserSelection: emergency cancellation failed");
        }
    }

    // =========================================================================
    // МЕТОДЫ ОТМЕНЫ ПЛАТЕЖА ДЛЯ "+20 ТОВАРОВ"
    // =========================================================================
    void onExtra20Cancel() {
        g_logger.info(L"Extra20Payment: start simple cancellation");

        auto [hasData, authCode, rrn, amountStr] = g_paymentManager.GetLastTransactionData();
        if (!hasData) {
            MessageBoxW(m_hWnd, L"Нет данных о последней успешной операции.\nСначала проведите успешную оплату.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.warning(L"Extra20Payment: cancellation aborted - no previous transaction");
            return;
        }

        int amount = std::stoi(amountStr);
        g_logger.info(L"Extra20Payment: cancelling transaction - Amount: " + std::to_wstring(amount) +
            L", AuthCode: " + std::wstring(authCode.begin(), authCode.end()) +
            L", RRN: " + std::wstring(rrn.begin(), rrn.end()));

        auto resp = g_paymentManager.ProcessCancellation(amount, authCode, rrn);
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Отмена платежа успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"Extra20Payment: cancellation successful");
        }
        else {
            std::string errDesc = g_paymentManager.extractField(resp ? resp->rawResponse : "", "19");
            std::wstring wErrDesc = errDesc.empty() ? L"Неизвестная ошибка" : utf8_to_wstring(errDesc);
            MessageBoxW(m_hWnd, (L"Отмена не удалась.\n" + wErrDesc).c_str(), L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"Extra20Payment: cancellation failed - " + wErrDesc);
        }
    }

    void onExtra20EmergencyCancel() {
        g_logger.info(L"Extra20Payment: start emergency cancellation");

        auto resp = g_paymentManager.ProcessEmergencyCancellation();
        if (resp && resp->isSuccess) {
            MessageBoxW(m_hWnd, L"Аварийная отмена успешна!", L"Успех", MB_OK | MB_ICONINFORMATION);
            g_logger.info(L"Extra20Payment: emergency cancellation successful");
        }
        else {
            MessageBoxW(m_hWnd, L"Аварийная отмена не удалась.\nВозможно, нет предыдущей успешной операции.",
                L"Ошибка", MB_OK | MB_ICONERROR);
            g_logger.error(L"Extra20Payment: emergency cancellation failed");
        }
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
        m_hConsignorInfoLabel(nullptr),
        m_hAppendixDialog(nullptr),
        m_hAppendixEdit(nullptr),
        m_appendixDialogActive(false),
        m_pendingAppendixItems(std::nullopt),
        m_appendixDialogResult(0) {}

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

            // =========================================================================
            // ОБРАБОТКА НОВОЙ КНОПКИ: Дополнительные услуги
            // =========================================================================
        case ID_BTN_ADDITIONAL_SERVICES:
            createAdditionalServicesMenu();
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

            // =========================================================================
            // ОБРАБОТКА НОВЫХ КНОПОК: Выбор товароведа
            // =========================================================================
        case ID_BTN_SELECT_APPRAISER:
            showAppraiserSelectionWindow();
            break;
        case ID_BTN_APPRAISER_PAY:
            onAppraiserPaymentSuccess();
            break;
        case ID_BTN_APPRAISER_BACK:
            createAdditionalServicesMenu();
            break;

        case ID_BTN_PAY_CARD:
            onExtra20PaymentSuccess();
            break;
        case ID_BTN_PAY_QR:
            // Кнопка "Назад" в окне Extra20Payment — возврат в меню дополнительных услуг
            createAdditionalServicesMenu();
            break;
            // НОВЫЕ ОБРАБОТЧИКИ ДЛЯ ТЕСТИРОВАНИЯ ПЛАТЕЖЕЙ
        case ID_BTN_TEST_PAY:
            onTestPayment();
            break;
        case ID_BTN_TEST_CANCEL:
            onTestCancellation();
            break;
        case ID_BTN_TEST_EMERGENCY:
            onTestEmergencyCancellation();
            break;

            // Обработчики отмены для "Платный прием"
        case ID_BTN_PAID_CANCEL:
            onPaidAcceptanceCancel();
            break;
        case ID_BTN_PAID_EMERGENCY_CANCEL:
            onPaidAcceptanceEmergencyCancel();
            break;

            // Обработчики отмены для "Выбор товароведа"
        case ID_BTN_APPRAISER_CANCEL:
            onAppraiserCancel();
            break;
        case ID_BTN_APPRAISER_EMERGENCY_CANCEL:
            onAppraiserEmergencyCancel();
            break;

            // Обработчики отмены для "+20 товаров"
        case ID_BTN_EXTRA20_CANCEL:
            onExtra20Cancel();
            break;
        case ID_BTN_EXTRA20_EMERGENCY_CANCEL:
            onExtra20EmergencyCancel();
            break;

        case ID_BTN_BACK:
            if (m_currentState == WindowState::SUBMIT_MENU ||
                m_currentState == WindowState::TICKET_ISSUED ||
                m_currentState == WindowState::ADDITIONAL_SERVICES) {
                createMainMenu();
            }
            else if (m_currentState == WindowState::GENERAL_QUEUE_INPUT) {
                createSubmitMenu();
            }
            else if (m_currentState == WindowState::EXTRA_20_PAYMENT) {
                createAdditionalServicesMenu();
            }
            else if (m_currentState == WindowState::ADDRESSES) {
                createMainMenu();
            }
            else if (m_currentState == WindowState::CONSIGNOR_LOOKUP) {
                createMainMenu();
            }
            else if (m_currentState == WindowState::MY_SALES) {
                createMainMenu();
            }
            break;
        case ID_BTN_CLOSE:
            g_logger.info(L"ID_BTN_CLOSE: initiating graceful shutdown via WM_CLOSE");
            SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
            break;

        case ID_BTN_CLEAR:
            if (m_hEditConsignorId) {
                SetWindowTextW(m_hEditConsignorId, L"");
                SetFocus(m_hEditConsignorId);
            }
            break;
        case ID_BTN_NEXT:
            if (m_currentState == WindowState::GENERAL_QUEUE_INPUT) {
                onGeneralQueueNext();
            }
            else if (m_currentState == WindowState::MY_SALES) {
                if (!m_hSalesListView) {
                    break;
                }

                int selectedIndex = ListView_GetNextItem(m_hSalesListView, -1, LVNI_SELECTED);

                if (selectedIndex < 0) {
                    if (m_hSalesStatusLabel) {
                        SetWindowTextW(m_hSalesStatusLabel, L"Выберите номер приложения");
                        InvalidateRect(m_hSalesStatusLabel, NULL, TRUE);
                    }
                    break;
                }

                wchar_t appendixBuf[64] = { 0 };
                ListView_GetItemText(m_hSalesListView, selectedIndex, 0, appendixBuf, 64);

                if (appendixBuf[0] == L'\0') {
                    break;
                }

                m_currentClientName = appendixBuf;
                m_nameConfirmed = true;

                if (m_hBtnNext) {
                    ShowWindow(m_hBtnNext, SW_HIDE);
                }

                loadSalesData();
            }
            break;
        case ID_BTN_RESET_ID:
            onGeneralQueueResetId();
            break;
        case ID_BTN_PRINT_TICKET:
        {
            if (m_currentState == WindowState::MY_SALES) {
                if (!m_hSalesListView ||
                    ListView_GetItemCount(m_hSalesListView) == 0) {
                    MessageBoxW(m_hWnd, L"Нет данных для печати", L"Печать", MB_OK);
                    break;
                }

                auto now = std::chrono::system_clock::now();
                auto timeNow = std::chrono::system_clock::to_time_t(now);
                struct tm tmInfo;
                localtime_s(&tmInfo, &timeNow);

                std::wstringstream dateStream;
                dateStream << std::put_time(&tmInfo, L"%d.%m.%Y");
                std::wstring currentDate = dateStream.str();

                std::wostringstream report;

                auto wrapText = [&report](const std::wstring& text) {
                    const size_t maxLen = 36;
                    if (text.empty()) {
                        return;
                    }

                    for (size_t pos = 0; pos < text.size(); pos += maxLen) {
                        report << text.substr(pos, maxLen) << L"\n";
                    }
                    };

                report << L"================================\n";
                report << L"Комиссионный магазин СОВЕТСКИЙ\n";
                report << L"================================\n";
                report << L"Дата: " << currentDate << L"\n";

                std::wstring modeLine;
                if (m_currentClientName.empty()) {
                    modeLine = L"Все товары клиента";
                }
                else {
                    modeLine = L"Приложение №" + m_currentClientName;
                }

                wrapText(modeLine);

                report << L"--------------------------------\n";

                int rowCount = ListView_GetItemCount(m_hSalesListView);

                // =============================================================
                // ИЗМЕНЕНО: колонки сдвинуты из-за добавления «Сумма на руки»
                //   было: 0-дата, 1-товар, 2-наим, 3-цена, 4-кол-во,
                //         5-дата продажи, 6-состояние
                //   стало: 0-дата, 1-приложение, 2-наим, 3-цена,
                //          4-сумма, 5-кол-во, 6-дата продажи, 7-состояние
                // =============================================================
                for (int row = 0; row < rowCount; ++row) {
                    wchar_t appendixBuf[128] = { 0 };
                    wchar_t nameBuf[1024] = { 0 };
                    wchar_t priceBuf[64] = { 0 };
                    wchar_t sumBuf[64] = { 0 };
                    wchar_t stateBuf[64] = { 0 };

                    ListView_GetItemText(m_hSalesListView, row, 1, appendixBuf, 128);
                    ListView_GetItemText(m_hSalesListView, row, 2, nameBuf, 1024);
                    ListView_GetItemText(m_hSalesListView, row, 3, priceBuf, 64);
                    ListView_GetItemText(m_hSalesListView, row, 4, sumBuf, 64);
                    ListView_GetItemText(m_hSalesListView, row, 7, stateBuf, 64);

                    report << std::to_wstring(row + 1) << L". № приложения: "
                        << appendixBuf << L"\n";
                    wrapText(std::wstring(nameBuf));
                    report << L"Цена: " << priceBuf << L"\n";
                    report << L"Сумма на руки: " << sumBuf << L"\n";
                    report << L"Статус: " << stateBuf << L"\n";
                    report << L"--------------------------------\n";
                }

                if (m_hClientInfoLabel) {
                    wchar_t totalBuf[256] = { 0 };
                    GetWindowTextW(m_hClientInfoLabel, totalBuf, 256);
                    wrapText(std::wstring(totalBuf));
                }

                report << L"================================\n";

                bool printed = g_printer.printContentViaGDI(report.str(), L"SalesReport");

                if (!printed) {
                    MessageBoxW(m_hWnd, L"Не удалось напечатать отчет", L"Ошибка печати", MB_OK);
                }
            }
            else {
                onGeneralQueuePrint();
            }

            break;
        }
        case ID_BTN_MY_SALES:
            if (m_currentState == WindowState::MY_SALES) {
                m_nameConfirmed = false;
                m_currentClientName.clear();

                if (m_hBtnNext) {
                    ShowWindow(m_hBtnNext, SW_HIDE);
                }

                loadSalesData();
            }
            else {
                if (!g_authManager.isLoggedIn() || g_authManager.getClientId() == 0) {
                    LoginWindow loginWnd;
                    bool loginSuccess = loginWnd.show(m_hWnd);

                    if (!loginSuccess) {
                        createMainMenu();
                        break;
                    }
                }

                createMySales();
            }
            break;
        case ID_BTN_SALES_BACK:
            if (m_currentState == WindowState::MY_SALES) {
                int appendixNumber = showAppendixInputDialog();
                if (appendixNumber > 0) {
                    m_nameConfirmed = true;
                    m_currentClientName = std::to_wstring(appendixNumber);
                    if (m_hBtnNext) {
                        ShowWindow(m_hBtnNext, SW_HIDE);
                    }
                    // Данные уже получены в диалоге и лежат в m_pendingAppendixItems.
                    // Повторный запрос к серверу не нужен.
                    // Вызываем onSalesDataReady напрямую с пустым response —
                    // он увидит m_pendingAppendixItems и отрисует их.
                    std::optional<nlohmann::json> empty;
                    onSalesDataReady(empty);
                }
            }
            else {
                createMainMenu();
            }
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
        g_hBrushWindow = CreateSolidBrush(RGB(255, 255, 0)); // Желтый фон
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

        // Получаем текущее состояние видимости крестика
        bool isVisible = (GetWindowLongPtrW(g_hCloseBtn, GWL_STYLE) & WS_VISIBLE) != 0;

        if (inZone && !isVisible) {
            ShowWindow(g_hCloseBtn, SW_SHOW);
        }
        else if (!inZone && isVisible) {
            ShowWindow(g_hCloseBtn, SW_HIDE);
        }
    }
    break;
    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        HWND hBtn = (HWND)lParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        DWORD btnId = GetDlgCtrlID(hBtn);
        // Красные кнопки: Назад, Закрыть, Сбросить, Выход, все кнопки отмены
        if (btnId == ID_BTN_BACK || btnId == ID_BTN_CLOSE || btnId == ID_BTN_RESET_ID ||
            btnId == ID_BTN_LOGOUT || btnId == ID_BTN_APPRAISER_BACK ||
            btnId == ID_BTN_PAID_CANCEL || btnId == ID_BTN_PAID_EMERGENCY_CANCEL ||
            btnId == ID_BTN_APPRAISER_CANCEL || btnId == ID_BTN_APPRAISER_EMERGENCY_CANCEL ||
            btnId == ID_BTN_EXTRA20_CANCEL || btnId == ID_BTN_EXTRA20_EMERGENCY_CANCEL) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)g_hBrushBackBtn;
        }

        // Зеленые кнопки: Оплата, Далее, Печать, Оплатить
        if (btnId == ID_BTN_PAY_CARD || btnId == ID_BTN_PAY_QR ||
            btnId == ID_BTN_NEXT || btnId == ID_BTN_PRINT_TICKET ||
            btnId == ID_BTN_TRUST_PRINT || btnId == ID_BTN_APPRAISER_PAY ||
            btnId == ID_BTN_PAID_PRINT) {
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
    case WM_NOTIFY:
    {
        LPNMHDR pNotify = reinterpret_cast<LPNMHDR>(lParam);

        // =============================================================
        // ИЗМЕНЕНО: подсветка проданных (lParam=1) теперь корректно
        // ставится в addRow на основании columns[7] (было columns[6]).
        // Здесь код не меняется — используется тот же lParam.
        // =============================================================
        if (pNotify->idFrom == ID_LIST_SALES &&
            pNotify->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW pCustomDraw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

            if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }

            if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                if (pCustomDraw->nmcd.lItemlParam == 1) {
                    pCustomDraw->clrText = RGB(0, 128, 0);
                }
                else {
                    pCustomDraw->clrText = RGB(0, 0, 0);
                }

                return CDRF_NEWFONT;
            }

            return CDRF_DODEFAULT;
        }

        break;
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
        if (g_hBmpStatue) { DeleteObject(g_hBmpStatue); g_hBmpStatue = nullptr; }
        g_logger.info(L"WM_DESTROY: GDI objects (brushes/fonts/icon bitmaps) released");
        g_hCloseBtn = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
// worker_window.h
// =============================================================================
// ФИНАЛЬНАЯ PRODUCTION-ВЕРСИЯ (C++20, Visual Studio 2022)
// ИСПРАВЛЕНИЯ ПО ЗАДАНИЮ (ДОПОЛНЕНИЕ К ПРЕДЫДУЩЕМУ РЕШЕНИЮ):
// 1. Устранено пересечение меток имени товароведа и суммы за сутки с метками
//    выплат комитенту: каждая группа кнопок теперь на отдельной строке.
// 2. Кнопка «Удалить строку», имя товароведа и сумма за сутки отображаются
//    НЕЗАВИСИМО от типа очереди (добавлены во все массивы управления).
// 3. Позиционирование: справа от «Отмена» — имя, ещё правее — сумма.
//    Кнопка «Удалить строку» — строкой выше.
// =============================================================================
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <sapi.h>
#include <sphelper.h>
#include <nlohmann/json.hpp>
#include <utility>
#include <cmath>
#include <regex>
#include <atomic>
#include <mutex>
#include <fstream>
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "string_utils.h"
#include "auth_manager.h"
#include "receipt_printer.h"
#include "price_tag_printer.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "comctl32.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;

using json = nlohmann::json;

// =============================================================================
// ИДЕНТИФИКАТОРЫ ЭЛЕМЕНТОВ УПРАВЛЕНИЯ
// =============================================================================
#define ID_ITEM_DATE_LABEL              1001
#define ID_ITEM_NUMBER_EDIT             1002
#define ID_ITEM_DESC_EDIT               1003
#define ID_ITEM_PRICE_EDIT              1004
#define ID_ITEM_QTY_EDIT                1005
#define ID_ITEM_CONDITION_EDIT          1006
#define ID_ITEM_NOTE_EDIT               1007
#define ID_ITEM_ADD_BTN                 1008
#define ID_ITEM_SAVE_BTN                1009
#define ID_ITEM_CANCEL_BTN              1010
#define ID_ITEM_LISTVIEW                1011
#define ID_ITEM_TOTAL_QTY               1012
#define ID_ITEM_TOTAL_PRICE             1013
#define ID_ITEM_BACK_BTN                1014
#define ID_ITEM_TOTAL_CLIENT_AMOUNT     1017
#define ID_ITEM_TOTAL_STORE_AMOUNT      1018
#define ID_ITEM_EDIT_RECORD_BTN         1019
#define ID_ITEM_CANCEL_INPUT_BTN        1020
#define ID_WORKER_NAME_LABEL            1021
#define ID_DAILY_AMOUNT_LABEL           1022
#define ID_DELETE_ROW_BTN               1023
#define ID_COMMIT_LASTNAME_LABEL        1101
#define ID_COMMIT_LASTNAME_EDIT         1102
#define ID_COMMIT_FIRSTNAME_LABEL       1103
#define ID_COMMIT_FIRSTNAME_EDIT        1104
#define ID_COMMIT_MIDDLENAME_LABEL      1105
#define ID_COMMIT_MIDDLENAME_EDIT       1106
#define ID_COMMIT_BIRTHDATE_LABEL       1107
#define ID_COMMIT_BIRTHDATE_EDIT        1108
#define ID_COMMIT_PASSTYPE_LABEL        1109
#define ID_COMMIT_PASSTYPE_COMBO        1110
#define ID_COMMIT_PASSSERIES_LABEL      1111
#define ID_COMMIT_PASSSERIES_EDIT       1112
#define ID_COMMIT_PASSNUMBER_LABEL      1113
#define ID_COMMIT_PASSNUMBER_EDIT       1114
#define ID_COMMIT_PHONE_LABEL           1115
#define ID_COMMIT_PHONE_EDIT            1116
#define ID_COMMIT_ADDRESS_LABEL         1117
#define ID_COMMIT_ADDRESS_EDIT          1118
#define ID_COMMIT_SAVE_BTN              1119
#define ID_COMMIT_CANCEL_BTN            1120
#define ID_COMMIT_FIO_LABEL             1121
#define ID_COMMIT_FIO_VALUE             1122
#define ID_COMMIT_ID_LABEL              1123
#define ID_COMMIT_ID_VALUE              1124
#define ID_COMMIT_PHONE_DISPLAY_LABEL   1125
#define ID_COMMIT_PHONE_DISPLAY_VALUE   1126
#define ID_COMMIT_BIRTHDATE_DISPLAY_LABEL   1127
#define ID_COMMIT_BIRTHDATE_DISPLAY_VALUE   1128
#define ID_DELETE_TICKET_BTN            4
#define ID_SEARCH_CLIENT_BTN            5
#define WM_RECEIPT_PRINT                (WM_APP + 4)
#define WM_COMMIT_SAVED                 (WM_APP + 5)
#define WM_CLIENT_DATA_LOADED           (WM_APP + 6)
#define WM_DELETE_TICKET_RESULT         (WM_APP + 7)

// =============================================================================
// БЕЗОПАСНАЯ ПЕРЕДАЧА ВЛАДЕНИЯ ЧЕРЕЗ PostMessage
// =============================================================================
template <typename T>
bool SafePostMessageOwnership(HWND hWnd, UINT msg, WPARAM wParam,
    std::unique_ptr<T> payload) noexcept
{
    if (!payload) {
        g_logger.warning(L"SafePostMessageOwnership: null payload, nothing to post");
        return false;
    }
    if (!IsWindow(hWnd)) {
        g_logger.warning(L"SafePostMessageOwnership: target window is invalid, "
            L"memory will be released by unique_ptr");
        return false;
    }
    DWORD targetThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (targetThreadId == 0) {
        g_logger.error(L"SafePostMessageOwnership: GetWindowThreadProcessId failed, err="
            + std::to_wstring(GetLastError()));
        return false;
    }
    T* rawPtr = payload.release();
    if (!PostMessageW(hWnd, msg, wParam, reinterpret_cast<LPARAM>(rawPtr))) {
        DWORD err = GetLastError();
        g_logger.error(L"SafePostMessageOwnership: PostMessageW failed, msg="
            + std::to_wstring(msg) + L", err=" + std::to_wstring(err));
        payload.reset(rawPtr);
        return false;
    }
    g_logger.info(L"SafePostMessageOwnership: payload posted successfully, msg="
        + std::to_wstring(msg));
    return true;
}

// =============================================================================
// СТРУКТУРА ItemSaveResult (глобальная область видимости)
// =============================================================================
struct ItemSaveResult {
    bool success;
    std::wstring errorMsg;
    long long appendixNumber;
    double itemsTotal;
    json createdItems;
};

// =============================================================================
// НОРМАЛИЗАЦИЯ ТЕЛЕФОНА
// =============================================================================
namespace WorkerPhoneUtils {
    inline std::wstring extractDigits(const std::wstring& input) {
        std::wstring digits;
        digits.reserve(input.size());
        for (wchar_t ch : input) {
            if (ch >= L'0' && ch <= L'9') digits += ch;
        }
        return digits;
    }
    inline std::wstring normalizePhone(const std::wstring& input) {
        std::wstring digits = extractDigits(input);
        if (digits.length() == 11) {
            if (digits[0] == L'7' || digits[0] == L'8') return L"+7" + digits.substr(1);
            return L"";
        }
        if (digits.length() == 10) return L"+7" + digits;
        return L"";
    }
}

// =============================================================================
// ПОДКЛАССИФИКАЦИЯ EDIT-КОНТРОЛОВ ДЛЯ НАВИГАЦИИ ПО Enter
// =============================================================================
static LRESULT CALLBACK EditEnterSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        g_logger.info(L"EditEnterSubclassProc: VK_RETURN pressed in control");
        HWND hParent = GetParent(hWnd);
        if (hParent) {
            HWND hNext = GetWindow(hWnd, GW_HWNDNEXT);
            HWND hStart = hNext;
            bool found = false;
            while (hNext && !found) {
                wchar_t className[256];
                GetClassNameW(hNext, className, 256);
                if (wcscmp(className, L"Edit") == 0) {
                    if (IsWindowVisible(hNext) && IsWindowEnabled(hNext)) {
                        LONG style = GetWindowLongW(hNext, GWL_STYLE);
                        if (!(style & ES_READONLY)) {
                            SetFocus(hNext);
                            g_logger.info(L"EditEnterSubclassProc: focus moved to next EDIT control");
                            found = true;
                        }
                    }
                }
                hNext = GetWindow(hNext, GW_HWNDNEXT);
                if (!hNext) {
                    hNext = GetTopWindow(hParent);
                }
                if (hNext == hStart) {
                    break;
                }
            }
            if (!found) {
                g_logger.warning(L"EditEnterSubclassProc: no suitable next EDIT control found");
            }
        }
        return 0;
    }
    WNDPROC origProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (origProc) return CallWindowProcW(origProc, hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void SubclassEditForEnterNavigation(HWND hEdit) {
    if (!hEdit) return;
    WNDPROC origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditEnterSubclassProc)));
    SetWindowLongPtrW(hEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(origProc));
    g_logger.info(L"SubclassEditForEnterNavigation: subclass installed on EDIT control");
}

static void UnsubclassEdit(HWND hEdit) {
    if (!hEdit) return;
    WNDPROC origProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hEdit, GWLP_USERDATA));
    if (origProc) {
        SetWindowLongPtrW(hEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(origProc));
        SetWindowLongPtrW(hEdit, GWLP_USERDATA, 0);
        g_logger.info(L"UnsubclassEdit: original procedure restored");
    }
}

// =============================================================================
// КЛАСС МОДАЛЬНОГО ОКНА «ПОИСК КОМИТЕНТА»
// =============================================================================
class ClientSearchDialog {
private:
    HWND m_hWnd;
    HWND m_hIdEdit;
    HWND m_hLastNameEdit;
    HWND m_hFirstNameEdit;
    HWND m_hMiddleNameEdit;
    HWND m_hSearchBtn;
    HWND m_hCloseBtn;
    HWND m_hResultEdit;
    HWND m_hStatusLabel;
    HFONT m_hFont;
    HFONT m_hFontBold;
    bool m_isSearching;
    static constexpr UINT WM_SEARCH_RESULT = WM_APP + 200;
    static constexpr const wchar_t* CLASS_NAME = L"ClientSearchDialogClass";
    static constexpr int DIALOG_WIDTH = 750;
    static constexpr int DIALOG_HEIGHT = 680;

    void releaseResources() {
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = nullptr; }
        if (m_hFontBold) { DeleteObject(m_hFontBold); m_hFontBold = nullptr; }
        g_logger.info(L"ClientSearchDialog: resources released");
    }

    void createControls() {
        g_logger.info(L"ClientSearchDialog: createControls started");
        m_hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontBold = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        int labelW = 180, editW = 350, editH = 28, rowH = 38;
        int left = 20, top = 15;

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Поиск комитента в базе данных",
            WS_VISIBLE | WS_CHILD | SS_CENTER, left, top, DIALOG_WIDTH - 40, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 40;

        HWND hIdLabel = CreateWindowExW(0, L"STATIC", L"ID комитента:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hIdLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hIdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
            left + labelW + 10, top, editW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hIdEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hIdEdit, EM_SETLIMITTEXT, 10, 0);
        SubclassEditForEnterNavigation(m_hIdEdit);
        top += rowH;

        HWND hLastNameLabel = CreateWindowExW(0, L"STATIC", L"Фамилия:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLastNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLastNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hLastNameEdit, EM_SETLIMITTEXT, 64, 0);
        SubclassEditForEnterNavigation(m_hLastNameEdit);
        top += rowH;

        HWND hFirstNameLabel = CreateWindowExW(0, L"STATIC", L"Имя:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hFirstNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hFirstNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hFirstNameEdit, EM_SETLIMITTEXT, 64, 0);
        SubclassEditForEnterNavigation(m_hFirstNameEdit);
        top += rowH;

        HWND hMiddleNameLabel = CreateWindowExW(0, L"STATIC", L"Отчество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hMiddleNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hMiddleNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hMiddleNameEdit, EM_SETLIMITTEXT, 64, 0);
        SubclassEditForEnterNavigation(m_hMiddleNameEdit);
        top += rowH + 5;

        m_hSearchBtn = CreateWindowExW(0, L"BUTTON", L"Найти",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 150, 40,
            m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(m_hSearchBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        m_hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 170, top, 150, 40,
            m_hWnd, (HMENU)(INT_PTR)2, g_hInstance, nullptr);
        SendMessageW(m_hCloseBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 50;

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Введите ID комитента или ФИО и нажмите «Найти»",
            WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, DIALOG_WIDTH - 40, 25,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 30;

        m_hResultEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            left, top, DIALOG_WIDTH - 40, DIALOG_HEIGHT - top - 20,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hResultEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hResultEdit, EM_SETLIMITTEXT, 65535, 0);

        m_isSearching = false;
        SetFocus(m_hIdEdit);
        g_logger.info(L"ClientSearchDialog: all controls created successfully");
    }

    void onSearch() {
        if (m_isSearching) return;
        wchar_t buf[256];
        GetWindowTextW(m_hIdEdit, buf, 256); std::wstring idText = buf;
        GetWindowTextW(m_hLastNameEdit, buf, 256); std::wstring lastName = buf;
        GetWindowTextW(m_hFirstNameEdit, buf, 256); std::wstring firstName = buf;
        GetWindowTextW(m_hMiddleNameEdit, buf, 256); std::wstring middleName = buf;

        bool hasId = !idText.empty();
        bool hasFio = !lastName.empty() || !firstName.empty() || !middleName.empty();

        if (!hasId && !hasFio) {
            SetWindowTextW(m_hStatusLabel, L"Введите ID комитента или хотя бы одно поле ФИО");
            return;
        }

        m_isSearching = true;
        EnableWindow(m_hSearchBtn, FALSE);
        SetWindowTextW(m_hStatusLabel, L"Поиск на сервере...");
        SetWindowTextW(m_hResultEdit, L"");

        std::wstring path;
        if (hasId) {
            path = L"/api/v1/clients/search?id=" + idText;
        }
        else {
            path = L"/api/v1/clients/search?";
            bool firstParam = true;
            if (!lastName.empty()) { if (!firstParam) path += L"&"; path += L"last_name=" + lastName; firstParam = false; }
            if (!firstName.empty()) { if (!firstParam) path += L"&"; path += L"first_name=" + firstName; firstParam = false; }
            if (!middleName.empty()) { if (!firstParam) path += L"&"; path += L"middle_name=" + middleName; }
        }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка авторизации: токен отсутствует");
            m_isSearching = false;
            EnableWindow(m_hSearchBtn, TRUE);
            return;
        }

        HWND hWndCopy = m_hWnd;
        std::thread([hWndCopy, path, authToken]() {
            auto pResult = std::make_unique<std::wstring>();
            try {
                auto response = g_httpsClient.get(path, authToken);
                if (response && response->contains("clients")
                    && (*response)["clients"].is_array())
                {
                    const auto& clients = (*response)["clients"];
                    const int count = static_cast<int>(clients.size());
                    if (count == 0) {
                        *pResult = L"Комитент не найден в базе данных.";
                    }
                    else {
                        for (int i = 0; i < count; ++i) {
                            const auto& c = clients[i];
                            if (i > 0) {
                                *pResult += L"\r\n\r\n"
                                    L"========================================\r\n\r\n";
                            }
                            *pResult += L"ID комитента: "
                                + std::to_wstring(c.value("id", 0)) + L"\r\n";
                            *pResult += L"ФИО: "
                                + utf8_to_wstring(c.value("full_name", "")) + L"\r\n";
                            *pResult += L"Телефон: "
                                + utf8_to_wstring(c.value("phone", "")) + L"\r\n";
                            *pResult += L"Статус: "
                                + std::wstring(c.value("is_blocked", false)
                                    ? L"ЗАБЛОКИРОВАН" : L"Активен")
                                + L"\r\n";
                        }
                    }
                }
                else {
                    *pResult = L"Ошибка поиска.";
                    if (response && response->contains("error")
                        && (*response)["error"].is_string())
                    {
                        *pResult += L"\n"
                            + utf8_to_wstring((*response)["error"].get<std::string>());
                    }
                }
            }
            catch (const json::exception& e) {
                g_logger.error(L"ClientSearchDialog::onSearch thread: JSON exception: "
                    + utf8_to_wstring(e.what()));
                *pResult = L"Ошибка обработки ответа сервера (JSON).";
            }
            catch (const std::exception& e) {
                g_logger.error(L"ClientSearchDialog::onSearch thread: std::exception: "
                    + utf8_to_wstring(e.what()));
                *pResult = L"Ошибка сети или обработки.";
            }
            catch (...) {
                g_logger.error(L"ClientSearchDialog::onSearch thread: unknown exception");
                *pResult = L"Неизвестная ошибка поиска.";
            }
            SafePostMessageOwnership(hWndCopy, ClientSearchDialog::WM_SEARCH_RESULT,
                0, std::move(pResult));
            }).detach();
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        ClientSearchDialog* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<ClientSearchDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            return 0;
        }
        pThis = reinterpret_cast<ClientSearchDialog*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED) {
                if (LOWORD(wParam) == 1) { pThis->onSearch(); return 0; }
                if (LOWORD(wParam) == 2) { DestroyWindow(hWnd); return 0; }
            }
            break;
        case WM_SEARCH_RESULT: {
            std::unique_ptr<std::wstring> pResult(
                reinterpret_cast<std::wstring*>(lParam));
            if (!pResult) {
                g_logger.warning(L"WM_SEARCH_RESULT: null payload");
                return 0;
            }
            SetWindowTextW(pThis->m_hResultEdit, pResult->c_str());
            SetWindowTextW(pThis->m_hStatusLabel, L"Поиск завершён");
            pThis->m_isSearching = false;
            EnableWindow(pThis->m_hSearchBtn, TRUE);
            return 0;
        }
        case WM_CLOSE:
        case WM_DESTROY:
            UnsubclassEdit(pThis->m_hIdEdit);
            UnsubclassEdit(pThis->m_hLastNameEdit);
            UnsubclassEdit(pThis->m_hFirstNameEdit);
            UnsubclassEdit(pThis->m_hMiddleNameEdit);
            DestroyWindow(hWnd);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    ClientSearchDialog() : m_hWnd(nullptr), m_hFont(nullptr), m_hFontBold(nullptr), m_isSearching(false),
        m_hIdEdit(nullptr), m_hLastNameEdit(nullptr), m_hFirstNameEdit(nullptr), m_hMiddleNameEdit(nullptr),
        m_hSearchBtn(nullptr), m_hCloseBtn(nullptr), m_hResultEdit(nullptr), m_hStatusLabel(nullptr) {}
    ~ClientSearchDialog() { releaseResources(); }

    void show(HWND hParent) {
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
            RegisterClassExW(&wcex);
            classRegistered = true;
        }
        int x = (GetSystemMetrics(SM_CXSCREEN) - DIALOG_WIDTH) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - DIALOG_HEIGHT) / 2;
        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME, CLASS_NAME, L"Поиск комитента",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, DIALOG_WIDTH, DIALOG_HEIGHT, hParent, nullptr, g_hInstance, this);
        if (!m_hWnd) return;
        EnableWindow(hParent, FALSE);
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        MSG msg;
        while (IsWindow(m_hWnd) && GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }
};

// =============================================================================
// КЛАСС WorkerWindow
// =============================================================================
class WorkerWindow {
private:
    HWND m_hWnd;
    HWND m_hListBox;
    HWND m_hAcceptBtn;
    HWND m_hServeBtn;
    HWND m_hStatusLabel;
    HWND m_hComboQueue;
    HFONT m_hFont;
    HFONT m_hSmallFont;
    HBRUSH m_hBrush;
    std::vector<json> m_tickets;
    std::mutex m_mutex;
    std::atomic<bool> m_running;
    std::thread m_refreshThread;
    std::wstring m_currentQueueType;
    std::vector<std::wstring> m_queueTypeIds;
    std::atomic<bool> m_closing{ false };
    ISpVoice* m_pVoice = nullptr;
    bool m_comInitialized = false;
    static constexpr int REFRESH_INTERVAL_MS = 2000;

    HWND m_hItemDateLabel, m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit;
    HWND m_hItemQtyEdit, m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn;
    HWND m_hItemSaveBtn, m_hItemCancelBtn, m_hItemListView;
    HWND m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemBackBtn;
    HWND m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel;
    HWND m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice;
    HWND m_hLabelQty, m_hLabelCondition, m_hLabelNote;
    HWND m_hEditRecordBtn;
    HWND m_hCancelInputBtn;
    HWND m_hCommitBlockTitle;
    std::vector<HWND> m_scrollableControls;
    int m_rightScrollPos = 0;
    int m_rightContentHeight = 0;

    HWND m_hCommitLastNameLabel, m_hCommitLastNameEdit;
    HWND m_hCommitFirstNameLabel, m_hCommitFirstNameEdit;
    HWND m_hCommitMiddleNameLabel, m_hCommitMiddleNameEdit;
    HWND m_hCommitBirthDateLabel, m_hCommitBirthDateEdit;
    HWND m_hCommitPassTypeLabel, m_hCommitPassTypeCombo;
    HWND m_hCommitPassSeriesLabel, m_hCommitPassSeriesEdit;
    HWND m_hCommitPassNumberLabel, m_hCommitPassNumberEdit;
    HWND m_hCommitPhoneLabel, m_hCommitPhoneEdit;
    HWND m_hCommitAddressLabel, m_hCommitAddressEdit;
    HWND m_hCommitSaveBtn, m_hCommitCancelBtn;
    HWND m_hCommitFioLabel, m_hCommitFioValue;
    HWND m_hCommitIdLabel, m_hCommitIdValue;
    HWND m_hCommitPhoneDisplayLabel, m_hCommitPhoneDisplayValue;
    HWND m_hCommitBirthDateDisplayLabel, m_hCommitBirthDateDisplayValue;
    HWND m_hDeleteTicketBtn;
    HWND m_hSearchClientBtn;

    // Новые элементы управления для задачи
    HWND m_hWorkerNameLabel;
    HWND m_hDailyAmountLabel;
    HWND m_hDeleteRowBtn;

    int m_currentClientId;
    std::wstring m_currentTicketNumber;
    std::wstring m_currentWindowNumber;
    std::wstring m_currentClientName;
    int64_t m_currentTicketAcceptedAt = 0;
    long long m_currentAppendixNumber = 0;
    std::mutex m_appendixMutex;
    json m_currentAcceptedTicket;
    bool m_hasCurrentAcceptedTicket = false;
    std::vector<json> m_tempItems;
    bool m_isSaving;
    bool m_isCommitDataSaved;
    std::wstring m_savedCommitPhone;
    int m_editingItemIndex;
    std::wstring m_loadedClientFio;
    std::wstring m_loadedClientPhone;
    std::wstring m_loadedClientBirthDate;
    std::mutex m_clientDataMutex;

    // Мьютекс для потокобезопасной работы с локальным файлом суммы за сутки
    std::mutex m_dailyAmountMutex;

    enum class Mode { QUEUE_LIST, ITEM_INPUT };
    Mode m_currentMode;
    int m_selectedIndex;
    int m_maxItemNumber;

    struct ReceiptSnapshot {
        bool valid = false;
        int clientId = 0;
        long long appendixNumber = 0;
        std::vector<json> items;
        int totalQty = 0;
        double totalValue = 0.0;
        double totalClientAmount = 0.0;
    };
    ReceiptSnapshot m_receiptSnapshot;
    std::mutex m_receiptSnapshotMutex;

    void unsubclassAllEdits() {
        HWND edits[] = {
            m_hCommitLastNameEdit, m_hCommitFirstNameEdit, m_hCommitMiddleNameEdit,
            m_hCommitBirthDateEdit, m_hCommitPassSeriesEdit, m_hCommitPassNumberEdit,
            m_hCommitPhoneEdit, m_hCommitAddressEdit,
            m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit, m_hItemNoteEdit
        };
        for (HWND h : edits) UnsubclassEdit(h);
    }

    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) return;
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
    }

    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (!m_pVoice) return;
        std::wstring full = L"Клиент с номером " + number + L" просим подойти к окну номер " + windowNumber;
        m_pVoice->Speak(full.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    std::wstring getWindowNumberForCurrentWorker() {
        std::wstring workerPhone = g_authManager.getPhone();
        for (int i = 0; i < Config::WORKER_WINDOW_MAP_SIZE; ++i) {
            if (workerPhone == Config::WORKER_WINDOW_MAP[i].phone) {
                return std::to_wstring(Config::WORKER_WINDOW_MAP[i].windowNumber);
            }
        }
        return std::to_wstring(Config::DEFAULT_WINDOW_NUMBER);
    }

    std::vector<json> fetchTicketsFromServer(const std::wstring& queueType) {
        std::wstring path;
        if (queueType == L"first_time") path = L"/api/v1/queue/first_time/waiting";
        else if (queueType == L"trust") path = L"/api/v1/queue/trust/waiting";
        else path = L"/api/v1/queue/waiting?type=" + queueType;
        auto response = g_httpsClient.get(path, L"");
        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            return (*response)["tickets"].get<std::vector<json>>();
        }
        return {};
    }

    void loadClientDataFromServer(int clientId) {
        if (clientId <= 0) return;
        std::wstring authToken = g_authManager.getAuthToken();
        HWND hWndCopy = m_hWnd;
        std::thread([this, clientId, authToken, hWndCopy]() {
            try {
                std::wstring path = L"/api/v1/clients/by_id?id=" + std::to_wstring(clientId);
                auto response = g_httpsClient.get(path, authToken);
                if (response && !response->contains("error")) {
                    std::wstring fio = utf8_to_wstring(response->value("name", ""));
                    std::wstring phone = utf8_to_wstring(response->value("phone", ""));
                    std::wstring birthDate = utf8_to_wstring(response->value("birth_date", ""));
                    {
                        std::lock_guard<std::mutex> lock(m_clientDataMutex);
                        m_loadedClientFio = fio; m_loadedClientPhone = phone; m_loadedClientBirthDate = birthDate;
                    }
                    PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)clientId, 0);
                }
                else {
                    PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)0, 0);
                }
            }
            catch (const json::exception& e) {
                g_logger.error(L"loadClientDataFromServer thread: JSON exception: " + utf8_to_wstring(e.what()));
                PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)0, 0);
            }
            catch (const std::exception& e) {
                g_logger.error(L"loadClientDataFromServer thread: std::exception: " + utf8_to_wstring(e.what()));
                PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)0, 0);
            }
            catch (...) {
                g_logger.error(L"loadClientDataFromServer thread: unknown exception");
                PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)0, 0);
            }
            }).detach();
    }

    bool hasCurrentAcceptedTicket() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hasCurrentAcceptedTicket;
    }

    void setCurrentAcceptedTicket(const json& ticket) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentAcceptedTicket = ticket;
        m_hasCurrentAcceptedTicket = true;
    }

    void removeCurrentAcceptedTicketFromLocalList() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasCurrentAcceptedTicket || !m_currentAcceptedTicket.is_object()) return;
        std::string acceptedNumber = m_currentAcceptedTicket.value("ticket_number", std::string(""));
        if (!acceptedNumber.empty()) {
            for (auto it = m_tickets.begin(); it != m_tickets.end(); ++it) {
                if (it->value("ticket_number", std::string("")) == acceptedNumber) {
                    m_tickets.erase(it); break;
                }
            }
        }
        if (m_selectedIndex >= (int)m_tickets.size()) m_selectedIndex = (int)m_tickets.size() - 1;
        m_currentAcceptedTicket = nullptr;
        m_hasCurrentAcceptedTicket = false;
    }

    void setupRightPanelScrollBar() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientH = rc.bottom - rc.top;
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = m_rightContentHeight; si.nPage = clientH; si.nPos = 0;
        SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
        bool needScroll = (m_currentMode == Mode::ITEM_INPUT) && (m_rightContentHeight > clientH);
        ShowScrollBar(m_hWnd, SB_VERT, needScroll ? TRUE : FALSE);
        m_rightScrollPos = 0;
    }

    void scrollRightPanel(int newPos) {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientH = rc.bottom - rc.top;
        int maxScroll = m_rightContentHeight - clientH;
        if (maxScroll < 0) maxScroll = 0;
        if (newPos < 0) newPos = 0;
        if (newPos > maxScroll) newPos = maxScroll;
        if (newPos == m_rightScrollPos) return;
        int delta = m_rightScrollPos - newPos;
        for (HWND h : m_scrollableControls) {
            if (!h) continue;
            RECT wr; GetWindowRect(h, &wr);
            POINT pt{ wr.left, wr.top };
            ScreenToClient(m_hWnd, &pt);
            SetWindowPos(h, NULL, pt.x, pt.y + delta, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        m_rightScrollPos = newPos;
        SetScrollPos(m_hWnd, SB_VERT, newPos, TRUE);

        InvalidateRect(m_hWnd, NULL, TRUE); // Принудительно перерисовываем фон, чтобы убрать "хвосты" от старых позиций контролов
    }

    void updateUI() {
        std::vector<json> ticketsCopy;
        int selectedIndex = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ticketsCopy = m_tickets;
            selectedIndex = m_selectedIndex;
        }
        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < ticketsCopy.size(); ++i) {
            const auto& ticket = ticketsCopy[i];
            std::string ticketNumber = ticket["ticket_number"].get<std::string>();
            std::wstring displayText = utf8_to_wstring(ticketNumber);
            if (m_currentQueueType != L"first_time" && ticket.contains("client_id")) {
                int clientId = ticket["client_id"].get<int>();
                displayText += L" (id: " + std::to_wstring(clientId) + L")";
            }
            int index = (int)SendMessageW(m_hListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
            if (index != LB_ERR) SendMessageW(m_hListBox, LB_SETITEMDATA, index, (LPARAM)i);
        }
        if (selectedIndex >= 0 && selectedIndex < (int)ticketsCopy.size()) {
            SendMessageW(m_hListBox, LB_SETCURSEL, selectedIndex, 0);
        }
        else {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_selectedIndex = -1;
        }
        int count = static_cast<int>(ticketsCopy.size());
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        SetWindowTextW(m_hStatusLabel, status.c_str());

        bool isInputMode = (m_currentMode == Mode::ITEM_INPUT);
        bool hasActiveAccepted = hasCurrentAcceptedTicket();
        EnableWindow(m_hAcceptBtn, (count > 0) && !isInputMode && !hasActiveAccepted);
        EnableWindow(m_hServeBtn, !isInputMode && hasActiveAccepted);

        bool deleteTicketEnabled = false;
        if (hasActiveAccepted) {
            if (!m_currentTicketNumber.empty() && m_currentTicketAcceptedAt > 0) {
                auto now = std::chrono::system_clock::now();
                int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                deleteTicketEnabled = (nowSec - m_currentTicketAcceptedAt >= 120);
            }
        }
        else {
            deleteTicketEnabled = (count > 0) && !isInputMode;
        }
        EnableWindow(m_hDeleteTicketBtn, deleteTicketEnabled);
    }

    void refreshList() {
        std::wstring queueType;
        int selectedIndex = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queueType = m_currentQueueType;
            selectedIndex = m_selectedIndex;
        }
        auto newTickets = fetchTicketsFromServer(queueType);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentQueueType != queueType) return;
            if (m_hasCurrentAcceptedTicket && m_currentAcceptedTicket.is_object()) {
                std::string acceptedNumber = m_currentAcceptedTicket.value("ticket_number", std::string(""));
                if (!acceptedNumber.empty()) {
                    int oldAcceptedIndex = -1;
                    for (int i = 0; i < (int)m_tickets.size(); ++i) {
                        if (m_tickets[i].value("ticket_number", std::string("")) == acceptedNumber) { oldAcceptedIndex = i; break; }
                    }
                    bool presentInNewTickets = false;
                    for (const auto& t : newTickets) {
                        if (t.value("ticket_number", std::string("")) == acceptedNumber) { presentInNewTickets = true; break; }
                    }
                    if (!presentInNewTickets) {
                        int insertPos = oldAcceptedIndex;
                        if (insertPos < 0 || insertPos >(int)newTickets.size()) insertPos = (int)newTickets.size();
                        newTickets.insert(newTickets.begin() + insertPos, m_currentAcceptedTicket);
                    }
                }
            }
            m_tickets = std::move(newTickets);
            if (selectedIndex >= (int)m_tickets.size()) selectedIndex = -1;
            m_selectedIndex = selectedIndex;
        }
        if (m_running.load() && IsWindow(m_hWnd)) PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    }

    void repositionItemInputControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left, height = rc.bottom - rc.top;
        int leftListWidth = (width * 20) / 100;
        if (leftListWidth < 180) leftListWidth = 180;
        int rightStart = leftListWidth + 10, rightWidth = width - rightStart - 10;
        if (rightWidth < 400) { showItemInputControls(false); return; }
        SetWindowPos(m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
        SetWindowPos(m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);
        int btnW = (leftListWidth - 40 - 10) / 2;
        SetWindowPos(m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
        SetWindowPos(m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
        destroyItemInputControls();
        createItemInputControls(rightStart, 80, rightWidth);
    }

    void destroyItemInputControls() {
        unsubclassAllEdits();
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemListView, m_hItemTotalQtyLabel,
            m_hItemTotalPriceLabel, m_hItemBackBtn,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel,
            m_hEditRecordBtn, m_hCancelInputBtn,
            m_hWorkerNameLabel, m_hDailyAmountLabel, m_hDeleteRowBtn,
            m_hDeleteTicketBtn,
            m_hCommitLastNameLabel, m_hCommitLastNameEdit,
            m_hCommitFirstNameLabel, m_hCommitFirstNameEdit,
            m_hCommitMiddleNameLabel, m_hCommitMiddleNameEdit,
            m_hCommitBirthDateLabel, m_hCommitBirthDateEdit,
            m_hCommitPassTypeLabel, m_hCommitPassTypeCombo,
            m_hCommitPassSeriesLabel, m_hCommitPassSeriesEdit,
            m_hCommitPassNumberLabel, m_hCommitPassNumberEdit,
            m_hCommitPhoneLabel, m_hCommitPhoneEdit,
            m_hCommitAddressLabel, m_hCommitAddressEdit,
            m_hCommitSaveBtn, m_hCommitCancelBtn,
            m_hCommitFioLabel, m_hCommitFioValue,
            m_hCommitIdLabel, m_hCommitIdValue,
            m_hCommitPhoneDisplayLabel, m_hCommitPhoneDisplayValue,
            m_hCommitBirthDateDisplayLabel, m_hCommitBirthDateDisplayValue,
            m_hCommitBlockTitle
        };
        for (auto& c : controls) { if (c) { DestroyWindow(c); c = nullptr; } }
    }

    // =============================================================================
    // МЕТОДЫ ДЛЯ РАБОТЫ С ЛОКАЛЬНЫМ ФАЙЛОМ СУММЫ ЗА СУТКИ
    // =============================================================================
    std::wstring getDailyAmountFilePath() {
        return Config::getAppDataPath() + L"\\daily_amount.json";
    }

    std::wstring getCurrentDateStr() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        wchar_t buf[32];
        wcsftime(buf, 32, L"%Y-%m-%d", &timeinfo);
        return buf;
    }

    double loadDailyAmount() {
        std::wstring path = getDailyAmountFilePath();
        std::ifstream file(path);
        if (!file.is_open()) {
            g_logger.info(L"loadDailyAmount: file not found, returning 0.0");
            return 0.0;
        }
        try {
            json j = json::parse(file);
            std::wstring date = utf8_to_wstring(j.value("date", ""));
            std::wstring today = getCurrentDateStr();
            if (date != today) {
                g_logger.info(L"loadDailyAmount: date mismatch (file=" + date + L", today=" + today + L"), returning 0.0");
                return 0.0;
            }
            double amount = j.value("amount", 0.0);
            g_logger.info(L"loadDailyAmount: loaded amount=" + std::to_wstring(amount) + L" for date=" + date);
            return amount;
        }
        catch (const std::exception& e) {
            g_logger.error(L"loadDailyAmount: exception: " + utf8_to_wstring(e.what()));
            return 0.0;
        }
    }

    void saveDailyAmount(double amount) {
        std::wstring path = getDailyAmountFilePath();
        json j;
        j["date"] = wstring_to_utf8(getCurrentDateStr());
        j["amount"] = amount;
        std::ofstream file(path, std::ios::trunc);
        if (file.is_open()) {
            file << j.dump(2);
            g_logger.info(L"saveDailyAmount: saved amount=" + std::to_wstring(amount) + L" to file");
        }
        else {
            g_logger.error(L"saveDailyAmount: failed to open file for writing");
        }
    }

    void updateDailyAmountDisplay() {
        if (!m_hDailyAmountLabel) return;
        double amount = 0.0;
        {
            std::lock_guard<std::mutex> lock(m_dailyAmountMutex);
            amount = loadDailyAmount();
        }
        wchar_t buf[128];
        swprintf_s(buf, L"Внесено за сутки: %d руб.", static_cast<int>(amount));
        SetWindowTextW(m_hDailyAmountLabel, buf);
        g_logger.info(L"updateDailyAmountDisplay: updated label with amount=" + std::to_wstring(amount));
    }

    void updateWorkerNameDisplay() {
        if (!m_hWorkerNameLabel) return;
        std::wstring workerName = g_authManager.getFullName();
        if (workerName.empty()) {
            workerName = g_authManager.getPhone();
        }
        if (workerName.empty()) {
            workerName = L"Товаровед";
        }
        SetWindowTextW(m_hWorkerNameLabel, workerName.c_str());
        g_logger.info(L"updateWorkerNameDisplay: set worker name to: " + workerName);
    }

    void createItemInputControls(int left, int top, int width) {
        if (width < 400) return;
        int labelW = 200, editW = width - labelW - 20, editH = 28, rowH = 35;

        if (m_currentQueueType == L"first_time") {
            m_hCommitBlockTitle = CreateWindowExW(0, L"STATIC", L"=== Данные нового комитента ===",
                WS_VISIBLE | WS_CHILD | SS_LEFT, left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(m_hCommitBlockTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            top += 10;

            if (!m_isCommitDataSaved) {
                m_hCommitLastNameLabel = CreateWindowExW(0, L"STATIC", L"Фамилия:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_LASTNAME_LABEL, g_hInstance, nullptr);
                m_hCommitLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_LASTNAME_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitLastNameEdit);
                top += rowH;

                m_hCommitFirstNameLabel = CreateWindowExW(0, L"STATIC", L"Имя:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIRSTNAME_LABEL, g_hInstance, nullptr);
                m_hCommitFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIRSTNAME_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitFirstNameEdit);
                top += rowH;

                m_hCommitMiddleNameLabel = CreateWindowExW(0, L"STATIC", L"Отчество:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_MIDDLENAME_LABEL, g_hInstance, nullptr);
                m_hCommitMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_MIDDLENAME_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitMiddleNameEdit);
                top += rowH;

                m_hCommitBirthDateLabel = CreateWindowExW(0, L"STATIC", L"Дата рождения:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_LABEL, g_hInstance, nullptr);
                m_hCommitBirthDateEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitBirthDateEdit);
                top += rowH;

                m_hCommitPassTypeLabel = CreateWindowExW(0, L"STATIC", L"Тип паспорта:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSTYPE_LABEL, g_hInstance, nullptr);
                m_hCommitPassTypeCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, left + labelW + 5, top, editW, editH + 100, m_hWnd, (HMENU)ID_COMMIT_PASSTYPE_COMBO, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassTypeCombo, CB_ADDSTRING, 0, (LPARAM)L"Паспорт РФ");
                SendMessageW(m_hCommitPassTypeCombo, CB_ADDSTRING, 0, (LPARAM)L"Загранпаспорт");
                SendMessageW(m_hCommitPassTypeCombo, CB_SETCURSEL, 0, 0);
                top += rowH;

                m_hCommitPassSeriesLabel = CreateWindowExW(0, L"STATIC", L"Серия:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSSERIES_LABEL, g_hInstance, nullptr);
                m_hCommitPassSeriesEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, editW / 3, editH, m_hWnd, (HMENU)ID_COMMIT_PASSSERIES_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitPassSeriesEdit);
                top += rowH;

                m_hCommitPassNumberLabel = CreateWindowExW(0, L"STATIC", L"Номер:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSNUMBER_LABEL, g_hInstance, nullptr);
                m_hCommitPassNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_COMMIT_PASSNUMBER_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitPassNumberEdit);
                top += rowH;

                m_hCommitPhoneLabel = CreateWindowExW(0, L"STATIC", L"Телефон:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_LABEL, g_hInstance, nullptr);
                m_hCommitPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitPhoneEdit);
                top += rowH;

                m_hCommitAddressLabel = CreateWindowExW(0, L"STATIC", L"Адрес:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ADDRESS_LABEL, g_hInstance, nullptr);
                m_hCommitAddressEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ADDRESS_EDIT, g_hInstance, nullptr);
                SubclassEditForEnterNavigation(m_hCommitAddressEdit);
                top += rowH + 5;

                m_hCommitSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить данные", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 220, 35, m_hWnd, (HMENU)ID_COMMIT_SAVE_BTN, g_hInstance, nullptr);
                m_hCommitCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 230, top, 150, 35, m_hWnd, (HMENU)ID_COMMIT_CANCEL_BTN, g_hInstance, nullptr);
                top += 50;
            }

            int dispTop = top;
            m_hCommitFioLabel = CreateWindowExW(0, L"STATIC", L"ФИО:", WS_CHILD | SS_RIGHT, left, dispTop, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_LABEL, g_hInstance, nullptr);
            m_hCommitFioValue = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, left + labelW + 5, dispTop, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_VALUE, g_hInstance, nullptr);
            m_hCommitIdLabel = CreateWindowExW(0, L"STATIC", L"ID:", WS_CHILD | SS_RIGHT, left, dispTop + rowH, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_LABEL, g_hInstance, nullptr);
            m_hCommitIdValue = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, left + labelW + 5, dispTop + rowH, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_VALUE, g_hInstance, nullptr);
            m_hCommitPhoneDisplayLabel = CreateWindowExW(0, L"STATIC", L"Телефон:", WS_CHILD | SS_RIGHT, left, dispTop + 2 * rowH, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_LABEL, g_hInstance, nullptr);
            m_hCommitPhoneDisplayValue = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, left + labelW + 5, dispTop + 2 * rowH, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_VALUE, g_hInstance, nullptr);

            if (m_isCommitDataSaved) {
                top += 3 * rowH + 10;
                ShowWindow(m_hCommitFioLabel, SW_SHOW); ShowWindow(m_hCommitFioValue, SW_SHOW);
                ShowWindow(m_hCommitIdLabel, SW_SHOW); ShowWindow(m_hCommitIdValue, SW_SHOW);
                ShowWindow(m_hCommitPhoneDisplayLabel, SW_SHOW); ShowWindow(m_hCommitPhoneDisplayValue, SW_SHOW);
            }
            else {
                ShowWindow(m_hCommitFioLabel, SW_HIDE); ShowWindow(m_hCommitFioValue, SW_HIDE);
                ShowWindow(m_hCommitIdLabel, SW_HIDE); ShowWindow(m_hCommitIdValue, SW_HIDE);
                ShowWindow(m_hCommitPhoneDisplayLabel, SW_HIDE); ShowWindow(m_hCommitPhoneDisplayValue, SW_HIDE);
            }
        }
        else {
            m_hCommitBlockTitle = CreateWindowExW(0, L"STATIC", L"=== Данные комитента ===",
                WS_VISIBLE | WS_CHILD | SS_LEFT, left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(m_hCommitBlockTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            top += 10;

            m_hCommitFioLabel = CreateWindowExW(0, L"STATIC", L"ФИО:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_LABEL, g_hInstance, nullptr);
            m_hCommitFioValue = CreateWindowExW(0, L"STATIC", L"Загрузка...", WS_VISIBLE | WS_CHILD | SS_LEFT, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_VALUE, g_hInstance, nullptr);
            top += rowH;

            m_hCommitIdLabel = CreateWindowExW(0, L"STATIC", L"ID:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_LABEL, g_hInstance, nullptr);
            m_hCommitIdValue = CreateWindowExW(0, L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_LEFT, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_VALUE, g_hInstance, nullptr);
            top += rowH;

            m_hCommitBirthDateDisplayLabel = CreateWindowExW(0, L"STATIC", L"Дата рождения:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_DISPLAY_LABEL, g_hInstance, nullptr);
            m_hCommitBirthDateDisplayValue = CreateWindowExW(0, L"STATIC", L"Загрузка...", WS_VISIBLE | WS_CHILD | SS_LEFT, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_DISPLAY_VALUE, g_hInstance, nullptr);
            top += rowH;

            m_hCommitPhoneDisplayLabel = CreateWindowExW(0, L"STATIC", L"Телефон:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_LABEL, g_hInstance, nullptr);
            m_hCommitPhoneDisplayValue = CreateWindowExW(0, L"STATIC", L"Загрузка...", WS_VISIBLE | WS_CHILD | SS_LEFT, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_VALUE, g_hInstance, nullptr);
            top += rowH + 10;
        }

        m_hTitle = CreateWindowExW(0, L"STATIC", L"Ввод товаров комитента", WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 35;

        m_hLabelDate = CreateWindowExW(0, L"STATIC", L"Дата/время:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemDateLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DATE_LABEL, g_hInstance, nullptr);
        top += rowH;

        m_hLabelNumber = CreateWindowExW(0, L"STATIC", L"Номер приложения к договору:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNumber, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        m_hItemNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, 120, editH, m_hWnd, (HMENU)ID_ITEM_NUMBER_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNumberEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += rowH;

        m_hLabelDesc = CreateWindowExW(0, L"STATIC", L"Наименование товара:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemDescEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DESC_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemDescEdit, EM_SETLIMITTEXT, 200, 0);
        SubclassEditForEnterNavigation(m_hItemDescEdit);
        top += rowH;

        m_hLabelPrice = CreateWindowExW(0, L"STATIC", L"Цена единицы:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemPriceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_ITEM_PRICE_EDIT, g_hInstance, nullptr);
        SubclassEditForEnterNavigation(m_hItemPriceEdit);
        top += rowH;

        m_hLabelQty = CreateWindowExW(0, L"STATIC", L"Количество:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemQtyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, 80, editH, m_hWnd, (HMENU)ID_ITEM_QTY_EDIT, g_hInstance, nullptr);
        SubclassEditForEnterNavigation(m_hItemQtyEdit);
        top += rowH;

        m_hLabelCondition = CreateWindowExW(0, L"STATIC", L"Состояние:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemConditionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"состояние износа 30%", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_READONLY, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_CONDITION_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemConditionEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;

        m_hLabelNote = CreateWindowExW(0, L"STATIC", L"Примечание:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        m_hItemNoteEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_NOTE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNoteEdit, EM_SETLIMITTEXT, 300, 0);
        SubclassEditForEnterNavigation(m_hItemNoteEdit);
        top += rowH + 5;

        m_hItemAddBtn = CreateWindowExW(0, L"BUTTON", L"Добавить позицию", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 35, m_hWnd, (HMENU)ID_ITEM_ADD_BTN, g_hInstance, nullptr);
        top += 45;

        m_hItemListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL, left, top, width - 10, 300, m_hWnd, (HMENU)ID_ITEM_LISTVIEW, g_hInstance, nullptr);
        SendMessageW(m_hItemListView, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        LVCOLUMNW col = { 0 }; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::vector<std::wstring> headers = { L"№", L"Наименование", L"Цена", L"Кол-во", L"Состояние", L"Примечание" };
        std::vector<int> widths = { 40, 160, 80, 60, 100, 120 };
        int totalColWidth = width - 10, sum = 0; for (auto w : widths) sum += w;
        for (size_t i = 0; i < headers.size(); ++i) {
            col.pszText = const_cast<LPWSTR>(headers[i].c_str());
            col.cx = (int)((float)widths[i] / sum * totalColWidth);
            col.iSubItem = static_cast<int>(i);
            ListView_InsertColumn(m_hItemListView, static_cast<int>(i), &col);
        }
        top += 310;

        m_hItemTotalQtyLabel = CreateWindowExW(0, L"STATIC", L"Общее количество: 0", 
            WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_QTY, g_hInstance, nullptr);
        m_hItemTotalPriceLabel = CreateWindowExW(0, L"STATIC", L"Общая цена: 0", 
            WS_VISIBLE | WS_CHILD | SS_LEFT, left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_PRICE, g_hInstance, nullptr);
        top += 40;

        m_hItemTotalClientAmountLabel = CreateWindowExW(0, L"STATIC", L"Выплата комитенту: 0", 
            WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_CLIENT_AMOUNT, g_hInstance, nullptr);
        
        m_hItemTotalStoreAmountLabel = CreateWindowExW(0, L"STATIC", L"Прибыль магазина: 0", 
            WS_VISIBLE | WS_CHILD | SS_LEFT, left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_STORE_AMOUNT, g_hInstance, nullptr);
        // =====================================================================
        // ИСПРАВЛЕНИЕ: каждая группа кнопок теперь на ОТДЕЛЬНОЙ строке.
        // Это устраняет пересечение меток выплат комитенту с кнопками
        // «Удалить строку», «Отмена», именем товароведа и суммой за сутки.
        // =====================================================================
        top += 30;  // ← переход на новую строку после выплат комитенту

        // Строка 2: «Редактировать» + «Удалить строку» — строго под «Выплата комитенту»
        m_hEditRecordBtn = CreateWindowExW(0, L"BUTTON", L"Редактировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left, top, 180, 30, m_hWnd, (HMENU)ID_ITEM_EDIT_RECORD_BTN, g_hInstance, nullptr);
        m_hDeleteRowBtn = CreateWindowExW(0, L"BUTTON", L"Удалить строку",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 180 + 10, top, 180, 30, m_hWnd, (HMENU)ID_DELETE_ROW_BTN, g_hInstance, nullptr);
        g_logger.info(L"createItemInputControls: EditRecordBtn+DeleteRowBtn at y=" +
            std::to_wstring(top) + L", left=" + std::to_wstring(left));

        top += 40;  // компактный переход на следующую строку

        // Строка 3: «Отмена» + имя товароведа + сумма за сутки
        m_hCancelInputBtn = CreateWindowExW(0, L"BUTTON", L"Отмена",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left, top, 180, 30, m_hWnd, (HMENU)ID_ITEM_CANCEL_INPUT_BTN, g_hInstance, nullptr);

        // Высота метки уменьшена с 68 до 32 — имя в одну строку, пустое место исчезло
        m_hWorkerNameLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOPREFIX,
            left + 180 + 10, top, 220, 32,
            m_hWnd, (HMENU)ID_WORKER_NAME_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hWorkerNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        g_logger.info(L"createItemInputControls: WorkerNameLabel height=32, y=" + std::to_wstring(top));

        m_hDailyAmountLabel = CreateWindowExW(0, L"STATIC", L"Внесено за сутки: 0 руб.",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left + 180 + 10 + 220 + 15, top, 280, 32,
            m_hWnd, (HMENU)ID_DAILY_AMOUNT_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hDailyAmountLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        g_logger.info(L"createItemInputControls: DailyAmountLabel at y=" + std::to_wstring(top));

        // Обновляем отображение имени товароведа и суммы за сутки
        updateWorkerNameDisplay();
        updateDailyAmountDisplay();

        top += 40;  // ← переход на новую строку после «Отмена»/имя/сумма

        // Строка 4: кнопки «Сохранить все», «Отменить», «Назад»
        m_hItemSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить все", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_SAVE_BTN, g_hInstance, nullptr);
        m_hItemCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 200, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_CANCEL_BTN, g_hInstance, nullptr);
        m_hItemBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 400, top, 120, 40, m_hWnd, (HMENU)ID_ITEM_BACK_BTN, g_hInstance, nullptr);

        if (!m_hDeleteTicketBtn) {
            m_hDeleteTicketBtn = CreateWindowExW(0, L"BUTTON", L"Удалить (не пришёл)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top + 45, 180, 40, m_hWnd, (HMENU)ID_DELETE_TICKET_BTN, g_hInstance, nullptr);
        }
        else {
            SetWindowPos(m_hDeleteTicketBtn, NULL, left, top + 45, 180, 40, SWP_NOZORDER);
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ: новые элементы добавлены в массив scrollables,
        // чтобы они корректно прокручивались вместе с остальными контролами.
        // =====================================================================
        HWND scrollables[] = {
            m_hCommitBlockTitle, m_hCommitLastNameLabel, m_hCommitLastNameEdit, m_hCommitFirstNameLabel, m_hCommitFirstNameEdit,
            m_hCommitMiddleNameLabel, m_hCommitMiddleNameEdit, m_hCommitBirthDateLabel, m_hCommitBirthDateEdit,
            m_hCommitPassTypeLabel, m_hCommitPassTypeCombo, m_hCommitPassSeriesLabel, m_hCommitPassSeriesEdit,
            m_hCommitPassNumberLabel, m_hCommitPassNumberEdit, m_hCommitPhoneLabel, m_hCommitPhoneEdit,
            m_hCommitAddressLabel, m_hCommitAddressEdit, m_hCommitSaveBtn, m_hCommitCancelBtn,
            m_hCommitFioLabel, m_hCommitFioValue, m_hCommitIdLabel, m_hCommitIdValue,
            m_hCommitPhoneDisplayLabel, m_hCommitPhoneDisplayValue, m_hCommitBirthDateDisplayLabel, m_hCommitBirthDateDisplayValue,
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice, m_hLabelQty, m_hLabelCondition, m_hLabelNote,
            m_hItemDateLabel, m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit, m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn,
            m_hItemListView, m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel,
            m_hEditRecordBtn, m_hDeleteRowBtn, m_hCancelInputBtn, m_hWorkerNameLabel, m_hDailyAmountLabel,
            m_hItemSaveBtn, m_hItemCancelBtn, m_hItemBackBtn, m_hDeleteTicketBtn
        };
        m_scrollableControls.clear();
        for (HWND h : scrollables) if (h) m_scrollableControls.push_back(h);
        m_rightContentHeight = top + 150;
        setupRightPanelScrollBar();
        g_logger.info(L"createItemInputControls: finished, rightContentHeight=" + std::to_wstring(m_rightContentHeight));
    }

    // =============================================================================
    // ИСПРАВЛЕНИЕ: новые элементы добавлены в showItemInputControls,
    // чтобы они отображались/скрывались вместе с остальными контролами
    // НЕЗАВИСИМО от типа очереди.
    // =============================================================================
    void showItemInputControls(bool show) {
        int flag = show ? SW_SHOW : SW_HIDE;
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemListView,
            m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemBackBtn,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel,
            m_hEditRecordBtn, m_hDeleteRowBtn, m_hCancelInputBtn,
            m_hWorkerNameLabel, m_hDailyAmountLabel,
            m_hDeleteTicketBtn
        };
        for (auto c : controls) { if (c) ShowWindow(c, flag); }
        g_logger.info(L"showItemInputControls: show=" + std::to_wstring(show ? 1 : 0));
    }

    void setCurrentDateTime() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo; localtime_s(&timeinfo, &time_t_now);
        wchar_t buf[100]; wcsftime(buf, 100, L"%Y-%m-%d %H:%M:%S", &timeinfo);
        SetWindowTextW(m_hItemDateLabel, buf);
    }

    void loadClientItems(int clientId) {
        m_maxItemNumber = 0;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) return;
        std::wstring path = L"/api/v1/items?client_id=" + std::to_wstring(clientId);
        auto response = g_httpsClient.get(path, authToken);
        if (response && response->contains("items") && (*response)["items"].is_array()) {
            auto& items = (*response)["items"];
            for (const auto& item : items) {
                if (item.contains("item_number") && item["item_number"].is_number()) {
                    int num = item["item_number"].get<int>();
                    if (num > m_maxItemNumber) m_maxItemNumber = num;
                }
            }
        }
    }

    void updateNextItemNumber() {
        int nextNumber = m_maxItemNumber + static_cast<int>(m_tempItems.size()) + 1;
        (void)nextNumber;
    }

    void updateTempItemsList() {
        ListView_DeleteAllItems(m_hItemListView);
        int totalQty = 0;
        double totalPrice = 0.0, totalClientAmount = 0.0, totalStoreAmount = 0.0;
        for (size_t i = 0; i < m_tempItems.size(); ++i) {
            const auto& item = m_tempItems[i];
            int displayNumber = static_cast<int>(i) + 1;
            std::wstring desc = utf8_to_wstring(item.value("description", ""));
            double price = item.value("estimated_price", 0.0);
            int qty = item.value("quantity", 1);
            std::wstring condition = utf8_to_wstring(item.value("condition", ""));
            std::wstring note = utf8_to_wstring(item.value("note", ""));

            // ЦЕЛОЧИСЛЕННОЕ ОТОБРАЖЕНИЕ ЦЕНЫ (без нулей после точки)
            std::wstring priceStr = (price > 0) ? std::to_wstring(static_cast<int>(price)) : L"";

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = static_cast<int>(i);
            std::vector<std::wstring> columns = { std::to_wstring(displayNumber), desc, priceStr, std::to_wstring(qty), condition, note };
            for (size_t col = 0; col < columns.size(); ++col) {
                lvi.iSubItem = static_cast<int>(col);
                lvi.pszText = const_cast<LPWSTR>(columns[col].c_str());
                if (col == 0) ListView_InsertItem(m_hItemListView, &lvi);
                else ListView_SetItem(m_hItemListView, &lvi);
            }
            totalQty += qty; totalPrice += price * qty;
            totalClientAmount += item.value("client_amount", 0.0);
            totalStoreAmount += item.value("store_amount", 0.0);
        }
        SetWindowTextW(m_hItemTotalQtyLabel, (L"Общее количество: " + std::to_wstring(totalQty)).c_str());

        // ЦЕЛОЧИСЛЕННОЕ ОТОБРАЖЕНИЕ ОБЩИХ СУММ
        wchar_t buf[128];
        swprintf_s(buf, L"Общая цена: %d", static_cast<int>(totalPrice));
        SetWindowTextW(m_hItemTotalPriceLabel, buf);

        wchar_t clientAmountBuf[128];
        swprintf_s(clientAmountBuf, L"Выплата комитенту: %d", static_cast<int>(totalClientAmount));
        SetWindowTextW(m_hItemTotalClientAmountLabel, clientAmountBuf);

        wchar_t storeAmountBuf[128];
        swprintf_s(storeAmountBuf, L"Прибыль магазина: %d", static_cast<int>(totalStoreAmount));
        SetWindowTextW(m_hItemTotalStoreAmountLabel, storeAmountBuf);

        g_logger.info(L"updateTempItemsList: updated list with " + std::to_wstring(m_tempItems.size()) + L" items, totalPrice=" + std::to_wstring(totalPrice));
    }

    void onItemAdd() {
        g_logger.info(L"onItemAdd: started");
        if (m_currentQueueType == L"first_time" && !m_isCommitDataSaved) {
            MessageBoxW(m_hWnd, L"Сначала сохраните данные комитента", L"Внимание", MB_OK);
            return;
        }
        if (m_tempItems.size() >= Config::MAX_ITEMS_GENERAL_QUEUE) {
            MessageBoxW(m_hWnd, L"Достигнут лимит 20 позиций.\nПожалуйста, нажмите «Сохранить все».",
                L"Лимит достигнут", MB_OK | MB_ICONINFORMATION);
            EnableWindow(m_hItemAddBtn, FALSE);
            return;
        }

        wchar_t descBuf[256];
        GetWindowTextW(m_hItemDescEdit, descBuf, 256);
        std::wstring desc = descBuf;
        if (desc.empty()) {
            MessageBoxW(m_hWnd, L"Введите наименование товара", L"Внимание", MB_OK);
            return;
        }

        wchar_t priceBuf[256];
        GetWindowTextW(m_hItemPriceEdit, priceBuf, 32);
        std::wstring priceStr = priceBuf;
        for (auto& c : priceStr) if (c == L',') c = L'.';
        double price = 0.0;
        try { price = std::stod(priceStr); }
        catch (...) {
            MessageBoxW(m_hWnd, L"Некорректная цена", L"Ошибка", MB_OK);
            return;
        }
        if (price < 0) {
            MessageBoxW(m_hWnd, L"Цена не может быть отрицательной", L"Ошибка", MB_OK);
            return;
        }

        int qty = 1;
        wchar_t qtyBuf[128];
        GetWindowTextW(m_hItemQtyEdit, qtyBuf, 16);
        try { qty = std::stoi(qtyBuf); }
        catch (...) {
            MessageBoxW(m_hWnd, L"Введите целое количество", L"Ошибка", MB_OK);
            return;
        }
        if (qty < 1) {
            MessageBoxW(m_hWnd, L"Количество не менее 1", L"Ошибка", MB_OK);
            return;
        }

        wchar_t condBuf[256], noteBuf[512];
        GetWindowTextW(m_hItemConditionEdit, condBuf, 256);
        GetWindowTextW(m_hItemNoteEdit, noteBuf, 512);

        CommissionCalc::CommissionRates rates = CommissionCalc::calculateByPrice(price);
        double clientPercent = rates.clientPercent;
        double storePercent = rates.storePercent;
        double clientAmount = price * qty * clientPercent / 100.0;
        double storeAmount = price * qty * storePercent / 100.0;

        json item;
        item["description"] = wstring_to_utf8(desc);
        item["estimated_price"] = price;
        item["quantity"] = qty;
        item["condition"] = wstring_to_utf8(condBuf);
        item["note"] = wstring_to_utf8(noteBuf);
        item["client_percent"] = clientPercent;
        item["store_percent"] = storePercent;
        item["client_amount"] = clientAmount;
        item["store_amount"] = storeAmount;

        if (m_editingItemIndex >= 0 && m_editingItemIndex < (int)m_tempItems.size()) {
            m_tempItems[m_editingItemIndex] = item;
            m_editingItemIndex = -1;
        }
        else {
            m_tempItems.push_back(item);
        }

        updateTempItemsList();
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        updateNextItemNumber();
        SetFocus(m_hItemDescEdit);

        if (m_tempItems.size() >= Config::MAX_ITEMS_GENERAL_QUEUE) {
            EnableWindow(m_hItemAddBtn, FALSE);
            MessageBoxW(m_hWnd, L"Вы добавили 20 позиций.\nНажмите «Сохранить все» для фиксации перечня.",
                L"Лимит 20 позиций", MB_OK | MB_ICONINFORMATION);
        }
    }

    void onEditRecord() {
        int sel = ListView_GetNextItem(m_hItemListView, -1, LVNI_SELECTED);
        if (sel < 0 || sel >= (int)m_tempItems.size()) {
            MessageBoxW(m_hWnd, L"Выберите запись для редактирования", L"Внимание", MB_OK); return;
        }
        m_editingItemIndex = sel;
        const auto& item = m_tempItems[sel];
        SetWindowTextW(m_hItemDescEdit, utf8_to_wstring(item.value("description", "")).c_str());
        SetWindowTextW(m_hItemPriceEdit, std::to_wstring(item.value("estimated_price", 0.0)).c_str());
        SetWindowTextW(m_hItemQtyEdit, std::to_wstring(item.value("quantity", 1)).c_str());
        SetWindowTextW(m_hItemConditionEdit, utf8_to_wstring(item.value("condition", "")).c_str());
        SetWindowTextW(m_hItemNoteEdit, utf8_to_wstring(item.value("note", "")).c_str());
    }

    // Удаление выбранной строки из перечня товаров
    void onDeleteRow() {
        g_logger.info(L"onDeleteRow: started");
        int sel = ListView_GetNextItem(m_hItemListView, -1, LVNI_SELECTED);
        if (sel < 0 || sel >= (int)m_tempItems.size()) {
            MessageBoxW(m_hWnd, L"Выберите строку для удаления", L"Внимание", MB_OK);
            g_logger.warning(L"onDeleteRow: no row selected");
            return;
        }

        std::wstring desc = utf8_to_wstring(m_tempItems[sel].value("description", ""));
        std::wstring confirmMsg = L"Удалить позицию \"" + desc + L"\" из перечня товаров?";
        int confirmResult = MessageBoxW(m_hWnd, confirmMsg.c_str(), L"Подтверждение удаления", MB_YESNO | MB_ICONQUESTION);
        if (confirmResult != IDYES) {
            g_logger.info(L"onDeleteRow: user cancelled deletion");
            return;
        }

        m_tempItems.erase(m_tempItems.begin() + sel);
        m_editingItemIndex = -1;
        updateTempItemsList();
        updateNextItemNumber();
        EnableWindow(m_hItemAddBtn, TRUE);
        g_logger.info(L"onDeleteRow: row " + std::to_wstring(sel) + L" deleted successfully");
    }

    void onCancelInput() {
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        m_editingItemIndex = -1;
        EnableWindow(m_hItemAddBtn, TRUE);
    }

    void onCommitSave() {
        g_logger.info(L"onCommitSave: started");
        wchar_t buf[256];
        GetWindowTextW(m_hCommitLastNameEdit, buf, 256); std::wstring lastName = buf;
        GetWindowTextW(m_hCommitFirstNameEdit, buf, 256); std::wstring firstName = buf;
        GetWindowTextW(m_hCommitMiddleNameEdit, buf, 256); std::wstring middleName = buf;
        GetWindowTextW(m_hCommitBirthDateEdit, buf, 256); std::wstring birthDate = buf;
        GetWindowTextW(m_hCommitPassSeriesEdit, buf, 256); std::wstring passSeries = buf;
        GetWindowTextW(m_hCommitPassNumberEdit, buf, 256); std::wstring passNumber = buf;
        GetWindowTextW(m_hCommitPhoneEdit, buf, 256); std::wstring phone = buf;
        GetWindowTextW(m_hCommitAddressEdit, buf, 256); std::wstring address = buf;

        if (lastName.empty() || firstName.empty()) { MessageBoxW(m_hWnd, L"Заполните Фамилию и Имя", L"Ошибка", MB_OK); return; }
        if (address.empty()) { MessageBoxW(m_hWnd, L"Заполните адрес проживания", L"Ошибка", MB_OK); return; }

        std::wstring normalizedPhone = WorkerPhoneUtils::normalizePhone(phone);
        if (normalizedPhone.empty()) {
            MessageBoxW(m_hWnd,
                L"Некорректный номер телефона.\n\n"
                L"Введите 10 или 11 цифр в любом формате:\n"
                L"  +7-906-111-11-11\n"
                L"  89061111111\n"
                L"  79061111111\n"
                L"  9061111111",
                L"Ошибка телефона", MB_OK | MB_ICONWARNING);
            SetFocus(m_hCommitPhoneEdit);
            return;
        }
        g_logger.info(L"onCommitSave: phone normalized from '" + phone + L"' to '" + normalizedPhone + L"'");

        json request;
        request["last_name"] = wstring_to_utf8(lastName);
        request["first_name"] = wstring_to_utf8(firstName);
        request["middle_name"] = wstring_to_utf8(middleName);
        request["birth_date"] = wstring_to_utf8(birthDate);
        request["passport_type"] = "rf";
        request["passport_series"] = wstring_to_utf8(passSeries);
        request["passport_number"] = wstring_to_utf8(passNumber);
        request["phone"] = wstring_to_utf8(normalizedPhone);
        request["address"] = wstring_to_utf8(address);

        std::wstring authToken = g_authManager.getAuthToken();
        HWND hWndCopy = m_hWnd;

        std::thread([request, authToken, hWndCopy]() {
            int resultClientId = 0;
            try {
                auto response = g_httpsClient.post(
                    L"/api/v1/clients/register_committee", request, authToken);
                if (response && response->contains("success")
                    && (*response)["success"].is_boolean()
                    && (*response)["success"].get<bool>())
                {
                    if (response->contains("client_id")
                        && (*response)["client_id"].is_number_integer())
                    {
                        resultClientId = (*response)["client_id"].get<int>();
                        g_logger.info(L"onCommitSave: success, clientId="
                            + std::to_wstring(resultClientId));
                    }
                    else {
                        g_logger.error(L"onCommitSave: server response missing client_id");
                    }
                }
                else {
                    std::wstring errMsg = L"Ошибка регистрации комитента";
                    if (response && response->contains("error")
                        && (*response)["error"].is_string())
                    {
                        errMsg += L": "
                            + utf8_to_wstring((*response)["error"].get<std::string>());
                    }
                    g_logger.error(L"onCommitSave: " + errMsg);
                }
            }
            catch (const json::exception& e) {
                g_logger.error(L"onCommitSave: JSON exception: "
                    + utf8_to_wstring(e.what()));
            }
            catch (const std::exception& e) {
                g_logger.error(L"onCommitSave: std::exception: "
                    + utf8_to_wstring(e.what()));
            }
            catch (...) {
                g_logger.error(L"onCommitSave: unknown exception");
            }

            if (!IsWindow(hWndCopy)) {
                g_logger.warning(L"onCommitSave: target window destroyed, "
                    L"skipping PostMessage");
                return;
            }
            if (!PostMessageW(hWndCopy, WM_COMMIT_SAVED,
                static_cast<WPARAM>(resultClientId), 0))
            {
                g_logger.error(L"onCommitSave: PostMessageW(WM_COMMIT_SAVED) failed, err="
                    + std::to_wstring(GetLastError()));
            }
            }).detach();
    }

    void onCommitCancel() {
        SetWindowTextW(m_hCommitLastNameEdit, L""); SetWindowTextW(m_hCommitFirstNameEdit, L"");
        SetWindowTextW(m_hCommitMiddleNameEdit, L""); SetWindowTextW(m_hCommitBirthDateEdit, L"");
        SetWindowTextW(m_hCommitPassSeriesEdit, L""); SetWindowTextW(m_hCommitPassNumberEdit, L"");
        SetWindowTextW(m_hCommitPhoneEdit, L""); SetWindowTextW(m_hCommitAddressEdit, L"");
    }

    void onItemSave() {
        if (m_isSaving) return;
        if (m_tempItems.empty()) { MessageBoxW(m_hWnd, L"Нет позиций для сохранения", L"Внимание", MB_OK); return; }
        if (m_currentClientId <= 0) { MessageBoxW(m_hWnd, L"Ошибка: клиент не выбран.", L"Ошибка", MB_OK | MB_ICONERROR); return; }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) { MessageBoxW(m_hWnd, L"Ошибка авторизации.", L"Ошибка", MB_OK); return; }

        m_isSaving = true; EnableWindow(m_hItemSaveBtn, FALSE);

        json request;
        request["client_id"] = m_currentClientId;
        request["items"] = m_tempItems;
        int workerId = g_authManager.getClientId();
        request["worker_id"] = workerId;

        long long appendixNum;
        {
            std::lock_guard<std::mutex> lock(m_appendixMutex);
            appendixNum = m_currentAppendixNumber;
        }
        if (appendixNum > 0) {
            request["appendix_number"] = appendixNum;
            g_logger.info(L"onItemSave: passing reserved appendix_number=" +
                std::to_wstring(appendixNum) +
                L" to server for clientId=" + std::to_wstring(m_currentClientId));
        }
        else {
            g_logger.warning(L"onItemSave: appendix_number=0, server will generate globally unique number");
        }

        // Вычисляем сумму товаров для обновления daily amount
        double itemsTotal = 0.0;
        for (const auto& item : m_tempItems) {
            double price = item.value("estimated_price", 0.0);
            int qty = item.value("quantity", 1);
            itemsTotal += price * qty;
        }
        g_logger.info(L"onItemSave: calculated itemsTotal=" + std::to_wstring(itemsTotal));

        HWND hWndCopy = m_hWnd;

        std::thread([this, request, authToken, hWndCopy, itemsTotal]() {
            g_logger.info(L"onItemSave thread: started");
            ItemSaveResult* pRes = new ItemSaveResult{ false, L"Ошибка сохранения.", 0, itemsTotal, json::array() };
            try {
                auto response = g_httpsClient.post(L"/api/v1/items/batch", request, authToken);
                if (response && response->contains("success") && (*response)["success"].is_boolean() && (*response)["success"].get<bool>()) {
                    pRes->success = true;
                    if (response->contains("appendix_number") && (*response)["appendix_number"].is_number_integer()) {
                        pRes->appendixNumber = (*response)["appendix_number"].get<long long>();
                    }
                    // =====================================================================
                    // ИСПРАВЛЕНИЕ (2026-10-13): сохраняем массив созданных items
                    // с их id и barcode из ответа сервера. Это необходимо для
                    // обогащения m_receiptSnapshot.items реальными id перед
                    // печатью ценников через PriceTagPrinter.
                    // =====================================================================
                    if (response->contains("items") && (*response)["items"].is_array()) {
                        pRes->createdItems = (*response)["items"];
                        g_logger.info(L"onItemSave thread: received " +
                            std::to_wstring(pRes->createdItems.size()) +
                            L" created items with ids from server");
                    }
                    else {
                        g_logger.warning(L"onItemSave thread: server response missing 'items' array");
                    }
                    g_logger.info(L"onItemSave thread: success, appendixNumber=" + std::to_wstring(pRes->appendixNumber));
                }
                else if (response && response->contains("error") && (*response)["error"].is_string()) {
                    pRes->errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
                    g_logger.error(L"onItemSave thread: server error: " + pRes->errorMsg);
                }
            }
            catch (const json::exception& e) {
                g_logger.error(L"onItemSave thread: JSON exception: " + utf8_to_wstring(e.what()));
                pRes->errorMsg += L"\nОшибка обработки ответа сервера (JSON).";
            }
            catch (const std::exception& e) {
                g_logger.error(L"onItemSave thread: std::exception: " + utf8_to_wstring(e.what()));
                pRes->errorMsg += L"\nОшибка сети: " + utf8_to_wstring(e.what());
            }
            catch (...) {
                g_logger.error(L"onItemSave thread: unknown exception");
                pRes->errorMsg += L"\nНеизвестная ошибка сохранения.";
            }
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_APP + 3, 0, reinterpret_cast<LPARAM>(pRes));
            }
            else {
                delete pRes;
            }
            }).detach();
    }

    void onItemCancel() {
        SetWindowTextW(m_hItemDescEdit, L""); SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1"); SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        m_tempItems.clear(); updateTempItemsList(); updateNextItemNumber();
        m_editingItemIndex = -1;
        EnableWindow(m_hItemAddBtn, TRUE);
    }

    ReceiptSnapshot fetchLatestAppendixFromServer(int clientId) {
        ReceiptSnapshot snap;
        if (clientId <= 0) return snap;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) return snap;
        auto resp = g_httpsClient.get(L"/api/v1/appendix/latest?client_id=" + std::to_wstring(clientId), authToken);
        if (!resp || resp->contains("error")) return snap;
        snap.valid = true;
        snap.clientId = clientId;
        snap.appendixNumber = resp->value("appendix_number", 0LL);
        snap.totalQty = resp->value("total_quantity", 0);
        snap.totalValue = resp->value("total_value", 0.0);
        snap.totalClientAmount = resp->value("total_client_amount", 0.0);
        if (resp->contains("items") && (*resp)["items"].is_array()) {
            for (const auto& it : (*resp)["items"]) {
                json j;
                j["id"] = it.value("id", 0);
                j["item_number"] = it.value("item_number", 0);
                j["barcode"] = it.value("barcode", "");
                j["description"] = it.value("description", "");
                j["condition"] = it.value("condition", "");
                j["quantity"] = it.value("quantity", 1);
                j["estimated_price"] = it.value("estimated_price", 0.0);
                j["client_amount"] = it.value("client_amount", 0.0);
                j["note"] = it.value("note", "");
                snap.items.push_back(j);
            }
        }
        return snap;
    }

    void printReceiptForServedClient(int clientId) {
        if (clientId <= 0) return;

        // =====================================================================
        // ИСПРАВЛЕНИЕ: Используем m_receiptSnapshot (член класса), а не
        // локальную переменную. Если snapshot заполнен в WM_APP+3 из
        // локальных данных — запрос к серверу НЕ выполняется, и все поля
        // товаров (id, description, barcode) будут корректно переданы
        // в PriceTagPrinter для генерации штрих-кодов.
        // Fallback на сервер сохраняется на случай, если snapshot невалиден
        // (например, при восстановлении сессии или повторной печати).
        // =====================================================================
        ReceiptSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(m_receiptSnapshotMutex);
            snap = m_receiptSnapshot;  // ← копируем из члена класса
        }

        bool needReload = !snap.valid || snap.clientId != clientId || snap.items.empty();

        if (needReload) {
            g_logger.info(L"printReceiptForServedClient: snapshot invalid, "
                L"falling back to server fetch for clientId=" + std::to_wstring(clientId));
            snap = fetchLatestAppendixFromServer(clientId);
        }
        else {
            g_logger.info(L"printReceiptForServedClient: using local snapshot, "
                L"appendixNumber=" + std::to_wstring(snap.appendixNumber) +
                L", items=" + std::to_wstring(snap.items.size()));
        }

        if (!snap.valid) {
            g_logger.warning(L"printReceiptForServedClient: no valid data for printing");
            return;
        }

        HWND hWndCopy = m_hWnd;
        std::thread([hWndCopy, snap]() {
            try {
                ReceiptData data;
                data.appendixNumber = snap.appendixNumber;
                data.clientId = snap.clientId;
                data.totalQty = snap.totalQty;
                data.totalValue = snap.totalValue;
                data.totalClientAmount = snap.totalClientAmount;
                for (const auto& it : snap.items) {
                    ReceiptItem ri;
                    ri.itemId = it.value("id", 0);
                    ri.description = utf8_to_wstring(it.value("description", ""));
                    ri.characteristic = utf8_to_wstring(it.value("condition", ""));
                    ri.quantity = it.value("quantity", 1);
                    ri.price = it.value("estimated_price", 0.0);
                    ri.clientAmount = it.value("client_amount", 0.0);
                    ri.note = utf8_to_wstring(it.value("note", ""));
                    data.items.push_back(ri);
                }
                auto resp = g_httpsClient.get(
                    L"/api/v1/clients/by_id?id=" + std::to_wstring(snap.clientId), L"");
                if (resp && resp->contains("name") && (*resp)["name"].is_string()) {
                    data.clientFullName = utf8_to_wstring(
                        (*resp)["name"].get<std::string>());
                }
                if (data.clientFullName.empty()) {
                    data.clientFullName = L"Клиент #" + std::to_wstring(snap.clientId);
                }
                auto pCopy = std::make_unique<ReceiptData>(std::move(data));
                SafePostMessageOwnership(hWndCopy, WM_RECEIPT_PRINT, 0, std::move(pCopy));
            }
            catch (const json::exception& e) {
                g_logger.error(L"printReceiptForServedClient thread: JSON exception: " +
                    utf8_to_wstring(e.what()));
            }
            catch (const std::exception& e) {
                g_logger.error(L"printReceiptForServedClient thread: std::exception: " +
                    utf8_to_wstring(e.what()));
            }
            catch (...) {
                g_logger.error(L"printReceiptForServedClient thread: unknown exception");
            }
            }).detach();
    }

    void returnToQueueList() {
        m_currentMode = Mode::QUEUE_LIST;
        showItemInputControls(false);
        scrollRightPanel(0);
        ShowScrollBar(m_hWnd, SB_VERT, FALSE);
        PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    }

    void showItemInputForm(int clientId, const std::wstring& ticketNumber,
        const std::wstring& windowNumber, const std::wstring& clientName) {
        m_currentClientId = clientId;
        m_currentTicketNumber = ticketNumber;
        m_currentWindowNumber = windowNumber;
        m_currentClientName = clientName;
        m_tempItems.clear();
        m_isSaving = false;
        m_currentMode = Mode::ITEM_INPUT;
        m_editingItemIndex = -1;
        {
            std::lock_guard<std::mutex> lock(m_appendixMutex);
            m_currentAppendixNumber = 0;
        }
        g_logger.info(L"showItemInputForm: reset m_currentAppendixNumber to 0, clientId=" +
            std::to_wstring(clientId));

        repositionItemInputControls();
        if (m_currentQueueType != L"first_time" && clientId > 0) {
            loadClientDataFromServer(clientId);
        }

        HWND hWndCopy = m_hWnd;
        std::thread([this, clientId, hWndCopy]() {
            try {
                loadClientItems(clientId);
                bool numberReserved = false;
                std::wstring authToken = g_authManager.getAuthToken();
                if (clientId > 0 && !authToken.empty()) {
                    std::wstring path = L"/api/v1/appendix/next?client_id=" +
                        std::to_wstring(clientId);
                    auto resp = g_httpsClient.get(path, authToken);
                    if (resp && resp->contains("appendix_number") &&
                        (*resp)["appendix_number"].is_number_integer()) {
                        long long num = (*resp)["appendix_number"].get<long long>();
                        {
                            std::lock_guard<std::mutex> lock(m_appendixMutex);
                            m_currentAppendixNumber = num;
                        }
                        numberReserved = true;
                        g_logger.info(L"showItemInputForm: RESERVED appendix_number=" +
                            std::to_wstring(num) + L" for clientId=" + std::to_wstring(clientId));
                    }
                    else {
                        g_logger.warning(L"showItemInputForm: server response missing appendix_number");
                    }
                }
                else {
                    g_logger.warning(L"showItemInputForm: clientId=0 or authToken empty");
                }

                if (!numberReserved) {
                    {
                        std::lock_guard<std::mutex> lock(m_appendixMutex);
                        m_currentAppendixNumber = 0;
                    }
                    g_logger.warning(L"showItemInputForm: FALLBACK - appendix_number=0 for clientId=" +
                        std::to_wstring(clientId));
                }

                if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
            }
            catch (const std::exception& e) {
                g_logger.error(L"showItemInputForm thread: " + utf8_to_wstring(e.what()));
                if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
            }
            catch (...) {
                g_logger.error(L"showItemInputForm thread: unknown exception");
                if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
            }
            }).detach();

        setCurrentDateTime();
        showItemInputControls(true);
        EnableWindow(m_hAcceptBtn, FALSE);
        EnableWindow(m_hServeBtn, FALSE);
    }

    void onAccept() {
        EnableWindow(m_hAcceptBtn, FALSE);
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            EnableWindow(m_hAcceptBtn, TRUE);
            MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK); return;
        }
        m_selectedIndex = sel;
        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
            EnableWindow(m_hAcceptBtn, TRUE);
            MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK); return;
        }

        std::string ticketNumberUtf8;
        int clientId = -1;
        json acceptedTicket;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (itemData >= (LRESULT)m_tickets.size()) {
                EnableWindow(m_hAcceptBtn, TRUE);
                MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK); return;
            }
            const json& ticket = m_tickets[itemData];
            acceptedTicket = ticket;
            ticketNumberUtf8 = ticket.value("ticket_number", std::string(""));
            if (ticket.contains("client_id") && ticket["client_id"].is_number()) {
                clientId = ticket["client_id"].get<int>();
            }
        }

        if (ticketNumberUtf8.empty()) {
            EnableWindow(m_hAcceptBtn, TRUE);
            MessageBoxW(m_hWnd, L"Ошибка идентификации талона", L"Ошибка", MB_OK); return;
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        setCurrentAcceptedTicket(acceptedTicket);
        std::wstring workerWindowNumber = getWindowNumberForCurrentWorker();

        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/accept";
        else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/accept";
        else endpoint = L"/api/v1/queue/accept";

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        request["window_number"] = wstring_to_utf8(workerWindowNumber);

        auto response = g_httpsClient.post(endpoint, request, L"");
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            std::wstring windowW = utf8_to_wstring(response->value("window_number", wstring_to_utf8(workerWindowNumber)));
            auto nowAccept = std::chrono::system_clock::now();
            m_currentTicketAcceptedAt = std::chrono::duration_cast<std::chrono::seconds>(nowAccept.time_since_epoch()).count();
            speak(ticketNumber, windowW);

            if (m_currentQueueType == L"first_time") {
                m_isCommitDataSaved = false;
                showItemInputForm(0, ticketNumber, windowW, L"Новый комитент");
            }
            else {
                if (clientId <= 0) {
                    MessageBoxW(m_hWnd, L"Нет client_id", L"Ошибка", MB_OK);
                    EnableWindow(m_hAcceptBtn, TRUE);
                    return;
                }
                showItemInputForm(clientId, ticketNumber, windowW, L"Клиент #" + std::to_wstring(clientId));
            }
            updateUI();
        }
        else {
            std::wstring err = L"Ошибка принятия талона. Возможно, он уже занят другим товароведом.";
            if (response && response->contains("error")) {
                err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
            EnableWindow(m_hAcceptBtn, TRUE);
            removeCurrentAcceptedTicketFromLocalList();
            updateUI();
        }
    }

    void onServe() {
        if (m_currentMode == Mode::ITEM_INPUT) return;
        std::wstring ticketNumber = m_currentTicketNumber;
        int clientId = m_currentClientId;
        bool needServe = !ticketNumber.empty();

        if (needServe) {
            std::string ticketNumberUtf8 = wstring_to_utf8(ticketNumber);
            std::wstring endpoint;
            if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/serve";
            else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/serve";
            else endpoint = L"/api/v1/queue/serve";

            json request; request["ticket_number"] = ticketNumberUtf8;
            auto response = g_httpsClient.post(endpoint, request, L"");
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                m_currentTicketNumber.clear();
                m_currentClientId = 0;
                m_currentTicketAcceptedAt = 0;
                removeCurrentAcceptedTicketFromLocalList();
            }
            else {
                std::wstring err = L"Ошибка обслуживания талона.";
                if (response && response->contains("error")) err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
                MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
                return;
            }
        }

        auto freshTickets = fetchTicketsFromServer(m_currentQueueType);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tickets = std::move(freshTickets);
            m_selectedIndex = -1;
        }
        updateUI();

        size_t ticketCount;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ticketCount = m_tickets.size();
        }
        if (ticketCount > 0) {
            SendMessageW(m_hListBox, LB_SETCURSEL, 0, 0);
            m_selectedIndex = 0;
            onAccept();
        }
    }

    void onDeleteTicket() {
        g_logger.info(L"onDeleteTicket: entered");
        std::string ticketNumberUtf8;
        int clientId = -1;
        int64_t baseTime = 0;
        LRESULT itemData = LB_ERR;
        bool deleteCurrentAccepted = false;

        if (hasCurrentAcceptedTicket() && !m_currentTicketNumber.empty()) {
            deleteCurrentAccepted = true;
            ticketNumberUtf8 = wstring_to_utf8(m_currentTicketNumber);
            clientId = m_currentClientId;
            baseTime = m_currentTicketAcceptedAt;
        }
        else {
            int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(m_hWnd, L"Выберите талон для удаления", L"Внимание", MB_OK);
                return;
            }
            itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
            if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
                MessageBoxW(m_hWnd, L"Ошибка идентификации талона", L"Ошибка", MB_OK);
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            if (itemData >= (LRESULT)m_tickets.size()) return;
            const json& ticket = m_tickets[itemData];
            ticketNumberUtf8 = ticket.value("ticket_number", std::string(""));
            if (ticket.contains("client_id") && ticket["client_id"].is_number()) {
                clientId = ticket["client_id"].get<int>();
            }
            baseTime = ticket.value("created_at", (int64_t)0);
        }

        if (ticketNumberUtf8.empty()) {
            MessageBoxW(m_hWnd, L"Не определён номер талона для удаления", L"Ошибка", MB_OK);
            return;
        }
        if (baseTime <= 0) {
            MessageBoxW(m_hWnd, L"Не определено время талона", L"Ошибка", MB_OK);
            return;
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        auto now = std::chrono::system_clock::now();
        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        int64_t ageSeconds = nowSec - baseTime;

        if (ageSeconds < 120) {
            int remainingSeconds = 120 - static_cast<int>(ageSeconds);
            std::wstring msg = L"Талон можно удалить только после 2 минут ожидания.\nОсталось: " + std::to_wstring(remainingSeconds) + L" сек.";
            MessageBoxW(m_hWnd, msg.c_str(), L"Внимание", MB_OK);
            return;
        }

        std::wstring confirmMsg = L"Удалить талон " + ticketNumber + L" из очереди «" + m_currentQueueType + L"»?\n\nКомитент не подошёл к окну в течение 2 минут.";
        int confirmResult = MessageBoxW(m_hWnd, confirmMsg.c_str(), L"Подтверждение удаления", MB_YESNO | MB_ICONQUESTION);
        if (confirmResult != IDYES) return;

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        request["queue_type"] = wstring_to_utf8(m_currentQueueType);

        std::wstring authToken = g_authManager.getAuthToken();
        auto response = g_httpsClient.post(L"/api/v1/queue/delete_ticket", request, authToken);

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            if (deleteCurrentAccepted) {
                removeCurrentAcceptedTicketFromLocalList();
                m_currentTicketNumber.clear();
                m_currentClientId = 0;
                m_currentTicketAcceptedAt = 0;
                returnToQueueList();
            }
            else {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData != LB_ERR && itemData < (LRESULT)m_tickets.size()) {
                    m_tickets.erase(m_tickets.begin() + itemData);
                }
                if (m_selectedIndex >= (int)m_tickets.size()) {
                    m_selectedIndex = (int)m_tickets.size() - 1;
                }
                updateUI();
            }
            MessageBoxW(m_hWnd, (L"Талон " + ticketNumber + L" удалён из очереди.").c_str(), L"Успех", MB_OK);
        }
    }

    void onSearchClient() {
        g_logger.info(L"onSearchClient: opening ClientSearchDialog");
        ClientSearchDialog dialog;
        dialog.show(m_hWnd);
        g_logger.info(L"onSearchClient: ClientSearchDialog closed");
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WorkerWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            pThis = (WorkerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis); pThis->m_hWnd = hWnd;
            pThis->createControls();
            pThis->m_currentMode = Mode::QUEUE_LIST;
            pThis->m_maxItemNumber = 0;
            pThis->m_selectedIndex = -1;
            {
                std::lock_guard<std::mutex> lock(pThis->m_mutex);
                pThis->m_tickets = pThis->fetchTicketsFromServer(pThis->m_currentQueueType);
            }
            pThis->updateUI();
            pThis->m_running.store(true);
            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running.load()) {
                    for (int waitedMs = 0; waitedMs < REFRESH_INTERVAL_MS && pThis->m_running.load(); waitedMs += 100) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (!pThis->m_running.load()) break;
                    try { pThis->refreshList(); }
                    catch (...) {}
                }
                });
            return 0;
        }

        pThis = (WorkerWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);

        if (msg == WM_SIZE) {
            if (pThis->m_currentMode == Mode::ITEM_INPUT) {
                pThis->repositionItemInputControls();
                pThis->showItemInputControls(true);
            }
            else {
                RECT rc; GetClientRect(hWnd, &rc);
                int width = rc.right - rc.left, height = rc.bottom - rc.top;
                int leftListWidth = (width * 20) / 100;
                if (leftListWidth < 180) leftListWidth = 180;
                SetWindowPos(pThis->m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
                SetWindowPos(pThis->m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);
                int btnW = (leftListWidth - 40 - 10) / 2;
                SetWindowPos(pThis->m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hSearchClientBtn, NULL, width - 320, 10, 180, 40, SWP_NOZORDER);
            }
            return 0;
        }

        if (msg == WM_VSCROLL) {
            RECT rc; GetClientRect(hWnd, &rc);
            int clientH = rc.bottom - rc.top;
            int maxScroll = pThis->m_rightContentHeight - clientH;
            if (maxScroll < 0) maxScroll = 0;
            int pos = pThis->m_rightScrollPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: pos -= 35; break;
            case SB_LINEDOWN: pos += 35; break;
            case SB_PAGEUP: pos -= clientH; break;
            case SB_PAGEDOWN: pos += clientH; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = (short)HIWORD(wParam); break;
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = maxScroll; break;
            }
            pThis->scrollRightPanel(pos);
            return 0;
        }

        if (msg == WM_MOUSEWHEEL) {
            int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            pThis->scrollRightPanel(pThis->m_rightScrollPos - (wheelDelta / 120) * 60);
            return 0;
        }

        switch (msg) {
        case WM_APP + 1:
            pThis->updateUI();
            return 0;

        case WM_APP + 2:
            pThis->updateNextItemNumber();
            pThis->updateTempItemsList();
            {
                std::lock_guard<std::mutex> lock(pThis->m_appendixMutex);
                if (pThis->m_currentAppendixNumber > 0) {
                    SetWindowTextW(pThis->m_hItemNumberEdit,
                        std::to_wstring(pThis->m_currentAppendixNumber).c_str());
                }
                else {
                    SetWindowTextW(pThis->m_hItemNumberEdit, L"Загрузка...");
                }
            }
            return 0;

        case WM_APP + 3: {
            g_logger.info(L"WndProc: WM_APP+3 received");
            std::unique_ptr<ItemSaveResult> res(
                reinterpret_cast<ItemSaveResult*>(lParam));
            if (!res) {
                g_logger.warning(L"WndProc: WM_APP+3 received null payload");
                return 0;
            }
            pThis->m_isSaving = false;
            EnableWindow(pThis->m_hItemSaveBtn, TRUE);
            if (res->success) {
                // =====================================================================
                // ИСПРАВЛЕНИЕ (2026-10-13): Заполняем m_receiptSnapshot из ЛОКАЛЬНЫХ
                // данных, но ОБОГАЩАЕМ позиции реальными id из серверного ответа
                // res->createdItems. Это устраняет корень ошибки:
                //   PriceTagPrinter: itemId is invalid, barcode cannot be generated
                //
                // Логика обогащения:
                //   - m_tempItems содержит позиции в порядке их добавления пользователем.
                //   - Сервер возвращает createdItems в том же порядке (ORDER BY item_number ASC, id ASC),
                //     что соответствует порядку добавления.
                //   - Для каждой позиции m_tempItems[i] копируем id из createdItems[i].
                //   - Если массивы разной длины (защита от рассинхронизации) —
                //     обогащаем только совпадающие по индексу позиции.
                //
                // Потокобезопасность: m_receiptSnapshot защищён m_receiptSnapshotMutex.
                // m_tempItems в этот момент ещё не очищен и доступен только в UI-потоке.
                // Гонка исключена.
                // =====================================================================
                {
                    std::lock_guard<std::mutex> lock(pThis->m_receiptSnapshotMutex);
                    pThis->m_receiptSnapshot = ReceiptSnapshot{};
                    pThis->m_receiptSnapshot.valid = true;
                    pThis->m_receiptSnapshot.clientId = pThis->m_currentClientId;
                    pThis->m_receiptSnapshot.appendixNumber = res->appendixNumber;

                    // Копируем товары из m_tempItems в snapshot
                    pThis->m_receiptSnapshot.items = pThis->m_tempItems;

                    // =================================================================
                    // ИСПРАВЛЕНИЕ: обогащаем каждую позицию реальным id с сервера
                    // =================================================================
                    const size_t createdCount = res->createdItems.is_array() ? res->createdItems.size() : 0;
                    size_t enrichedCount = 0;
                    for (size_t i = 0; i < pThis->m_receiptSnapshot.items.size(); ++i) {
                        if (i < createdCount && res->createdItems[i].is_object() &&
                            res->createdItems[i].contains("id") &&
                            res->createdItems[i]["id"].is_number_integer())
                        {
                            int serverItemId = res->createdItems[i]["id"].get<int>();
                            pThis->m_receiptSnapshot.items[i]["id"] = serverItemId;

                            // Также обогащаем barcode, если сервер его вернул
                            if (res->createdItems[i].contains("barcode") &&
                                res->createdItems[i]["barcode"].is_string())
                            {
                                pThis->m_receiptSnapshot.items[i]["barcode"] =
                                    res->createdItems[i]["barcode"].get<std::string>();
                            }
                            ++enrichedCount;
                        }
                        else {
                            g_logger.warning(L"WM_APP+3: item at index " + std::to_wstring(i) +
                                L" could NOT be enriched with server id "
                                L"(createdCount=" + std::to_wstring(createdCount) + L")");
                        }
                    }

                    // Вычисляем суммы из локальных данных
                    int totalQty = 0;
                    double totalValue = 0.0;
                    double totalClientAmount = 0.0;
                    for (const auto& item : pThis->m_tempItems) {
                        int qty = item.value("quantity", 1);
                        double price = item.value("estimated_price", 0.0);
                        totalQty += qty;
                        totalValue += price * qty;
                        totalClientAmount += item.value("client_amount", 0.0);
                    }
                    pThis->m_receiptSnapshot.totalQty = totalQty;
                    pThis->m_receiptSnapshot.totalValue = totalValue;
                    pThis->m_receiptSnapshot.totalClientAmount = totalClientAmount;

                    g_logger.info(L"WM_APP+3: m_receiptSnapshot filled locally AND enriched with server ids, "
                        L"appendixNumber=" + std::to_wstring(res->appendixNumber) +
                        L", items=" + std::to_wstring(pThis->m_tempItems.size()) +
                        L", enrichedCount=" + std::to_wstring(enrichedCount) +
                        L", totalQty=" + std::to_wstring(totalQty) +
                        L", totalValue=" + std::to_wstring(totalValue));
                }
                g_logger.info(L"WM_APP+3: items saved successfully, appendixNumber="
                    + std::to_wstring(res->appendixNumber)
                    + L", clientId=" + std::to_wstring(pThis->m_currentClientId));
                // ОБНОВЛЕНИЕ ЛОКАЛЬНОЙ СУММЫ ЗА СУТКИ
                {
                    std::lock_guard<std::mutex> lock(pThis->m_dailyAmountMutex);
                    double currentAmount = pThis->loadDailyAmount();
                    double newAmount = currentAmount + res->itemsTotal;
                    pThis->saveDailyAmount(newAmount);
                    g_logger.info(L"WM_APP+3: updated daily amount from " + std::to_wstring(currentAmount) +
                        L" to " + std::to_wstring(newAmount) + L" (added " + std::to_wstring(res->itemsTotal) + L")");
                }
                pThis->updateDailyAmountDisplay();
                // ПЕЧАТЬ ЧЕКА И ЦЕННИКОВ — теперь printReceiptForServedClient использует
                // заполненный и обогащённый m_receiptSnapshot
                pThis->printReceiptForServedClient(pThis->m_currentClientId);
                // Очищаем m_tempItems ПОСЛЕ того, как snapshot уже скопировал и обогатил данные
                pThis->m_tempItems.clear();
                pThis->updateTempItemsList();
                g_logger.info(L"WM_APP+3: m_tempItems cleared, ListView reset before returnToQueueList");
                MessageBoxW(pThis->m_hWnd,
                    L"Товары успешно сохранены!\nНажмите «Обслужен (Следующий)».",
                    L"Успех", MB_OK);
                pThis->returnToQueueList();
            }
            else {
                g_logger.error(L"WM_APP+3: save failed, error: " + res->errorMsg);
                MessageBoxW(pThis->m_hWnd, res->errorMsg.c_str(), L"Ошибка", MB_OK);
            }
            return 0;
        }

        case WM_COMMIT_SAVED: {
            int clientId = (int)wParam;
            if (clientId > 0) {
                pThis->m_currentClientId = clientId;
                pThis->m_isCommitDataSaved = true;
                pThis->repositionItemInputControls();
                pThis->showItemInputControls(true);
                pThis->setCurrentDateTime();

                HWND hWndCopy = pThis->m_hWnd;
                std::thread([pThis, clientId, hWndCopy]() {
                    try {
                        pThis->loadClientItems(clientId);
                        bool numberReserved = false;
                        std::wstring authToken = g_authManager.getAuthToken();
                        if (!authToken.empty()) {
                            std::wstring path = L"/api/v1/appendix/next?client_id=" +
                                std::to_wstring(clientId);
                            auto resp = g_httpsClient.get(path, authToken);
                            if (resp && resp->contains("appendix_number") &&
                                (*resp)["appendix_number"].is_number_integer()) {
                                long long num = (*resp)["appendix_number"].get<long long>();
                                {
                                    std::lock_guard<std::mutex> lock(pThis->m_appendixMutex);
                                    pThis->m_currentAppendixNumber = num;
                                }
                                numberReserved = true;
                            }
                        }
                        if (!numberReserved) {
                            {
                                std::lock_guard<std::mutex> lock(pThis->m_appendixMutex);
                                pThis->m_currentAppendixNumber = 0;
                            }
                        }
                        if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
                    }
                    catch (const std::exception& e) {
                        g_logger.error(L"WM_COMMIT_SAVED thread: " + utf8_to_wstring(e.what()));
                        if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
                    }
                    catch (...) {
                        g_logger.error(L"WM_COMMIT_SAVED thread: unknown exception");
                        if (IsWindow(hWndCopy)) PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
                    }
                    }).detach();

                wchar_t fioBuf[256];
                GetWindowTextW(pThis->m_hCommitLastNameEdit, fioBuf, 256);
                std::wstring lastName = fioBuf;
                GetWindowTextW(pThis->m_hCommitFirstNameEdit, fioBuf, 256);
                std::wstring firstName = fioBuf;
                std::wstring fio = lastName + L" " + firstName;
                SetWindowTextW(pThis->m_hCommitFioValue, fio.c_str());
                SetWindowTextW(pThis->m_hCommitIdValue, std::to_wstring(clientId).c_str());
                ShowWindow(pThis->m_hCommitFioLabel, SW_SHOW); ShowWindow(pThis->m_hCommitFioValue, SW_SHOW);
                ShowWindow(pThis->m_hCommitIdLabel, SW_SHOW); ShowWindow(pThis->m_hCommitIdValue, SW_SHOW);
                MessageBoxW(pThis->m_hWnd, L"Данные комитента сохранены!", L"Успех", MB_OK);
            }
            else {
                MessageBoxW(pThis->m_hWnd, L"Ошибка сохранения данных комитента", L"Ошибка", MB_OK);
            }
            return 0;
        }

        case WM_CLIENT_DATA_LOADED: {
            int clientId = (int)wParam;
            if (clientId > 0) {
                std::wstring fio, phone, birthDate;
                {
                    std::lock_guard<std::mutex> lock(pThis->m_clientDataMutex);
                    fio = pThis->m_loadedClientFio;
                    phone = pThis->m_loadedClientPhone;
                    birthDate = pThis->m_loadedClientBirthDate;
                }
                if (pThis->m_hCommitFioValue)
                    SetWindowTextW(pThis->m_hCommitFioValue, fio.c_str());
                if (pThis->m_hCommitIdValue)
                    SetWindowTextW(pThis->m_hCommitIdValue, std::to_wstring(clientId).c_str());
                if (pThis->m_hCommitBirthDateDisplayValue)
                    SetWindowTextW(pThis->m_hCommitBirthDateDisplayValue, birthDate.c_str());
                if (pThis->m_hCommitPhoneDisplayValue)
                    SetWindowTextW(pThis->m_hCommitPhoneDisplayValue, phone.c_str());
            }
            return 0;
        }

        case WM_RECEIPT_PRINT: {
            std::unique_ptr<ReceiptData> pData(
                reinterpret_cast<ReceiptData*>(lParam));
            if (!pData) {
                g_logger.warning(L"WndProc: WM_RECEIPT_PRINT received null payload");
                return 0;
            }
            EnableWindow(pThis->m_hAcceptBtn, FALSE);
            EnableWindow(pThis->m_hServeBtn, FALSE);
            bool ok = false;
            try {
                ok = ReceiptPrinter::print(*pData);
                if (pThis->m_currentQueueType == L"first_time") {
                    g_logger.info(L"WM_RECEIPT_PRINT: first_time queue → printing second copy of appendix");
                    ok = ReceiptPrinter::print(*pData) && ok;
                }
                const std::wstring workerFio = g_authManager.getFullName();
                PriceTagPrinter::printForReceipt(*pData, workerFio);
            }
            catch (const std::exception& e) {
                g_logger.error(L"WM_RECEIPT_PRINT: exception during print: "
                    + utf8_to_wstring(e.what()));
                ok = false;
            }
            catch (...) {
                g_logger.error(L"WM_RECEIPT_PRINT: unknown exception during print");
                ok = false;
            }
            pThis->updateUI();
            if (!ok) {
                MessageBoxW(pThis->m_hWnd, L"Ошибка печати чека",
                    L"Ошибка", MB_OK);
            }
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == 100 && code == LBN_SELCHANGE) {
                int newSel = (int)SendMessageW(pThis->m_hListBox, LB_GETCURSEL, 0, 0);
                if (newSel != LB_ERR) {
                    std::lock_guard<std::mutex> lock(pThis->m_mutex);
                    pThis->m_selectedIndex = newSel;
                }
                return 0;
            }
            if (id == 200 && code == CBN_SELCHANGE) {
                if (pThis->hasCurrentAcceptedTicket()) {
                    MessageBoxW(pThis->m_hWnd, L"Сначала завершите текущий талон.", L"Внимание", MB_OK); return 0;
                }
                if (pThis->m_currentMode == Mode::ITEM_INPUT) {
                    MessageBoxW(pThis->m_hWnd, L"Сначала завершите ввод товаров", L"Внимание", MB_OK); return 0;
                }
                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR && idx < (LRESULT)pThis->m_queueTypeIds.size()) {
                    std::lock_guard<std::mutex> lock(pThis->m_mutex);
                    pThis->m_currentQueueType = pThis->m_queueTypeIds[static_cast<int>(idx)];
                    pThis->m_selectedIndex = -1;
                }
                pThis->m_currentTicketNumber.clear();
                pThis->m_currentClientId = 0;
                pThis->refreshList();
                return 0;
            }
            if (id == ID_COMMIT_PASSTYPE_COMBO && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(pThis->m_hCommitPassTypeCombo, CB_GETCURSEL, 0, 0);
                if (sel == 0) {
                    SetWindowTextW(pThis->m_hCommitPassSeriesLabel, L"Серия паспорта (4 цифры):");
                    SetWindowTextW(pThis->m_hCommitPassNumberLabel, L"Номер паспорта (6 цифр):");
                    SendMessageW(pThis->m_hCommitPassSeriesEdit, EM_SETLIMITTEXT, 4, 0);
                    SendMessageW(pThis->m_hCommitPassNumberEdit, EM_SETLIMITTEXT, 6, 0);
                }
                else {
                    SetWindowTextW(pThis->m_hCommitPassSeriesLabel, L"Серия паспорта (2 цифры):");
                    SetWindowTextW(pThis->m_hCommitPassNumberLabel, L"Номер паспорта (8 цифр):");
                    SendMessageW(pThis->m_hCommitPassSeriesEdit, EM_SETLIMITTEXT, 2, 0);
                    SendMessageW(pThis->m_hCommitPassNumberEdit, EM_SETLIMITTEXT, 8, 0);
                }
                return 0;
            }
            if (id == ID_ITEM_ADD_BTN && code == BN_CLICKED) { pThis->onItemAdd(); return 0; }
            if (id == ID_ITEM_SAVE_BTN && code == BN_CLICKED) { pThis->onItemSave(); return 0; }
            if (id == ID_ITEM_CANCEL_BTN && code == BN_CLICKED) { pThis->onItemCancel(); return 0; }
            if (id == ID_ITEM_BACK_BTN && code == BN_CLICKED) {
                if (pThis->hasCurrentAcceptedTicket()) {
                    MessageBoxW(pThis->m_hWnd, L"Нельзя вернуться до завершения талона.", L"Внимание", MB_OK); return 0;
                }
                if (MessageBoxW(pThis->m_hWnd, L"Вернуться? Несохранённые данные будут потеряны.", L"Подтверждение", MB_YESNO) == IDYES) {
                    pThis->m_currentTicketNumber.clear(); pThis->m_currentClientId = 0;
                    pThis->m_currentTicketAcceptedAt = 0; pThis->removeCurrentAcceptedTicketFromLocalList();
                    pThis->returnToQueueList();
                }
                return 0;
            }
            if (id == ID_ITEM_EDIT_RECORD_BTN && code == BN_CLICKED) { pThis->onEditRecord(); return 0; }
            if (id == ID_DELETE_ROW_BTN && code == BN_CLICKED) { pThis->onDeleteRow(); return 0; }
            if (id == ID_ITEM_CANCEL_INPUT_BTN && code == BN_CLICKED) { pThis->onCancelInput(); return 0; }
            if (id == ID_COMMIT_SAVE_BTN && code == BN_CLICKED) { pThis->onCommitSave(); return 0; }
            if (id == ID_COMMIT_CANCEL_BTN && code == BN_CLICKED) { pThis->onCommitCancel(); return 0; }
            if (code == BN_CLICKED) {
                switch (id) {
                case 1: pThis->onAccept(); break;
                case 2: pThis->onServe(); break;
                case 3: DestroyWindow(pThis->m_hWnd); break;
                case ID_DELETE_TICKET_BTN: pThis->onDeleteTicket(); break;
                case ID_SEARCH_CLIENT_BTN: pThis->onSearchClient(); break;
                }
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            pThis->m_closing.store(true, std::memory_order_release);
            pThis->m_running.store(false);
            if (pThis->m_refreshThread.joinable())
                pThis->m_refreshThread.join();
            if (pThis->m_pVoice) {
                pThis->m_pVoice->Release();
                pThis->m_pVoice = nullptr;
            }
            CoUninitialize();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left, height = rc.bottom - rc.top;
        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hSmallFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 20, 10, 200, 200, m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        struct QueueTypeEntry { const wchar_t* displayName; const wchar_t* id; };
        QueueTypeEntry entries[] = {
            { L"Общая очередь", L"general" }, { L"В первый раз", L"first_time" },
            { L"+20 позиций", L"extra_20" }, { L"На доверии", L"trust" },
            { L"Платный прием", L"paid" }, { L"Дорогой товар", L"expensive" }
        };
        for (const auto& entry : entries) {
            int idx = (int)SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)entry.displayName);
            if (idx != CB_ERR) m_queueTypeIds.push_back(entry.id);
        }
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        if (!m_queueTypeIds.empty()) m_currentQueueType = m_queueTypeIds[0];

        int leftListWidth = (width * 25) / 100;
        if (leftListWidth < 200) leftListWidth = 200;

        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY, 20, 60, leftListWidth - 40, height - 190, m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, height - 120, leftListWidth - 40, 30, m_hWnd, (HMENU)101, g_hInstance, nullptr);

        int btnW = (leftListWidth - 40 - 10) / 2;
        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, height - 80, btnW, 50, m_hWnd, (HMENU)1, g_hInstance, nullptr);
        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + btnW + 10, height - 80, btnW, 50, m_hWnd, (HMENU)2, g_hInstance, nullptr);

        m_hSearchClientBtn = CreateWindowExW(0, L"BUTTON", L"Поиск комитента", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, width - 320, 10, 180, 40, m_hWnd, (HMENU)ID_SEARCH_CLIENT_BTN, g_hInstance, nullptr);
        HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, width - 120, 10, 100, 40, m_hWnd, (HMENU)3, g_hInstance, nullptr);

        int rightStart = leftListWidth + 20, rightWidth = width - rightStart - 20; if (rightWidth < 400) rightWidth = 400;
        createItemInputControls(rightStart, 80, rightWidth);
        showItemInputControls(true);
        setCurrentDateTime();

        m_tempItems.clear();
        m_currentClientId = -1;
        m_isCommitDataSaved = false;
        m_editingItemIndex = -1;
        updateTempItemsList();
    }

public:
    WorkerWindow() : m_hWnd(nullptr), m_hListBox(nullptr), m_hAcceptBtn(nullptr), m_hServeBtn(nullptr),
        m_hStatusLabel(nullptr), m_hComboQueue(nullptr), m_hFont(nullptr), m_hSmallFont(nullptr), m_hBrush(nullptr),
        m_running(false), m_currentMode(Mode::QUEUE_LIST), m_currentClientId(0), m_isSaving(false),
        m_maxItemNumber(0), m_selectedIndex(-1), m_hTitle(nullptr), m_hLabelDate(nullptr), m_hLabelNumber(nullptr),
        m_hLabelDesc(nullptr), m_hLabelPrice(nullptr), m_hLabelQty(nullptr), m_hLabelCondition(nullptr),
        m_hLabelNote(nullptr), m_hItemDateLabel(nullptr), m_hItemNumberEdit(nullptr), m_hItemDescEdit(nullptr),
        m_hItemPriceEdit(nullptr), m_hItemQtyEdit(nullptr), m_hItemConditionEdit(nullptr), m_hItemNoteEdit(nullptr),
        m_hItemAddBtn(nullptr), m_hItemSaveBtn(nullptr), m_hItemCancelBtn(nullptr), m_hItemListView(nullptr),
        m_hItemTotalQtyLabel(nullptr), m_hItemTotalPriceLabel(nullptr), m_hItemBackBtn(nullptr),
        m_hItemTotalClientAmountLabel(nullptr), m_hItemTotalStoreAmountLabel(nullptr), m_hEditRecordBtn(nullptr),
        m_hCancelInputBtn(nullptr), m_hCommitBlockTitle(nullptr), m_hCommitLastNameLabel(nullptr),
        m_hCommitLastNameEdit(nullptr), m_hCommitFirstNameLabel(nullptr), m_hCommitFirstNameEdit(nullptr),
        m_hCommitMiddleNameLabel(nullptr), m_hCommitMiddleNameEdit(nullptr), m_hCommitBirthDateLabel(nullptr),
        m_hCommitBirthDateEdit(nullptr), m_hCommitPassTypeLabel(nullptr), m_hCommitPassTypeCombo(nullptr),
        m_hCommitPassSeriesLabel(nullptr), m_hCommitPassSeriesEdit(nullptr), m_hCommitPassNumberLabel(nullptr),
        m_hCommitPassNumberEdit(nullptr), m_hCommitPhoneLabel(nullptr), m_hCommitPhoneEdit(nullptr),
        m_hCommitAddressLabel(nullptr), m_hCommitAddressEdit(nullptr), m_hCommitSaveBtn(nullptr),
        m_hCommitCancelBtn(nullptr), m_hCommitFioLabel(nullptr), m_hCommitFioValue(nullptr),
        m_hCommitIdLabel(nullptr), m_hCommitIdValue(nullptr), m_hCommitPhoneDisplayLabel(nullptr),
        m_hCommitPhoneDisplayValue(nullptr), m_hCommitBirthDateDisplayLabel(nullptr),
        m_hCommitBirthDateDisplayValue(nullptr), m_hDeleteTicketBtn(nullptr), m_hSearchClientBtn(nullptr),
        m_hWorkerNameLabel(nullptr), m_hDailyAmountLabel(nullptr), m_hDeleteRowBtn(nullptr),
        m_isCommitDataSaved(false), m_comInitialized(false), m_editingItemIndex(-1) {
        g_logger.info(L"WorkerWindow: TTS disabled on worker side — voice output routed to remote display monitor via QueueDisplayWindow");
    }

    ~WorkerWindow() {
        m_running.store(false);
        if (m_refreshThread.joinable()) m_refreshThread.join();
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hSmallFont) DeleteObject(m_hSmallFont);
        if (m_hBrush) DeleteObject(m_hBrush);
    }

    void show() {
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"WorkerWindowClass";
            RegisterClassExW(&wc);
            classRegistered = true;
        }
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        m_hWnd = CreateWindowExW(0, L"WorkerWindowClass", L"Товаровед - Управление очередями",
            WS_OVERLAPPEDWINDOW, 0, 0, screenW, screenH, nullptr, nullptr, g_hInstance, this);
        if (!m_hWnd) return;
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);

        MSG msg;
        BOOL getMessageResult = GetMessage(&msg, nullptr, 0, 0);
        while (getMessageResult > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) break;
            getMessageResult = GetMessage(&msg, nullptr, 0, 0);
        }
    }
};
// worker_window.h – ФИНАЛЬНАЯ ПРОДАКШН-ВЕРСИЯ (ИСПРАВЛЕНА ОШИБКА НАЛОЖЕНИЯ ЗАГОЛОВКА)
// =============================================================================
// ИСПРАВЛЕНИЯ ОТ 21.08.2026:
// 1. ИСПРАВЛЕНА ОШИБКА "=== Данны=== Данные комитента ===":
//    - Заголовок блока данных комитента теперь сохраняется в член класса m_hCommitBlockTitle
//    - Дескриптор корректно уничтожается в destroyItemInputControls()
//    - Это предотвращает утечку User-объектов и визуальные артефакты наложения текста
// 2. ИСПРАВЛЕНО ОТОБРАЖЕНИЕ ФИО И ТЕЛЕФОНА ПОСЛЕ СОХРАНЕНИЯ ДАННЫХ КОМИТЕНТА:
//    - ФИО и телефон сохраняются ДО вызова repositionItemInputControls()
//    - После пересоздания контролов данные устанавливаются в поля отображения
//    - Добавлено полное логгирование для отладки
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

// Идентификаторы для НОВЫХ полей комитента (очередь "first_time")
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

// Идентификаторы отображения даты рождения во всех очередях
#define ID_COMMIT_BIRTHDATE_DISPLAY_LABEL   1127
#define ID_COMMIT_BIRTHDATE_DISPLAY_VALUE   1128

// НОВЫЕ ИДЕНТИФИКАТОРЫ ДЛЯ КНОПКИ УДАЛЕНИЯ ТАЛОНА И ПОИСКА КОМИТЕНТА
#define ID_DELETE_TICKET_BTN            4    // Кнопка "Удалить (не пришёл)"
#define ID_SEARCH_CLIENT_BTN            5    // Кнопка "Поиск комитента"

#define WM_RECEIPT_PRINT                (WM_APP + 4)
#define WM_COMMIT_SAVED                 (WM_APP + 5)
#define WM_CLIENT_DATA_LOADED           (WM_APP + 6)  // данные клиента загружены
#define WM_DELETE_TICKET_RESULT         (WM_APP + 7)  // Результат удаления талона

// =============================================================================
// КЛАСС МОДАЛЬНОГО ОКНА «ПОИСК КОМИТЕНТА»
// =============================================================================
//
// Модальное окно для товароведа с возможностью поиска комитента по
// или по фамилии, имени и отчеству. Построено по паттерну класса
// BlockReasonDialog из director_window.h: программное создание контролов
// без использования . файлов, модальный цикл обработки сообщений,
// отключение родительского окна на время работы.
//
// Поля ввода:
//   - комитента (число)
//   - Фамилия (текст)
//   - Имя (текст)
//   - Отчество (текст)
//
// При нажатии кнопки «Найти» выполняется асинхронный запрос к серверу:
//   - Если заполнен → GET /api/v1/clients/search?id=...
//   - Если заполнено ФИО → GET /api/v1/clients/search?last_name=...&first_name=...&middle_name=...
//
// Результаты отображаются в многострочном текстовом поле (только чтение).
//
// Потокобезопасность:
//   - Сетевой запрос выполняется в фоновом потоке (паттерн из
//     loadClientDataFromServer / blockClient).
//   - Результат передаётся в -поток через PostMessageW с
//     пользовательским сообщением WM_SEARCH_RESULT.
//   - Дескриптор окна копируется в лямбду до запуска потока.
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

    // Сообщение для возврата результата поиска из фонового потока в -поток
    static constexpr UINT WM_SEARCH_RESULT = WM_APP + 200;

    static constexpr const wchar_t* CLASS_NAME = L"ClientSearchDialogClass";
    static constexpr int DIALOG_WIDTH = 750;
    static constexpr int DIALOG_HEIGHT = 680;

    // =================================================================
    // Освобождение ресурсов (шрифты)
    // =================================================================
    void releaseResources() {
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = nullptr; }
        if (m_hFontBold) { DeleteObject(m_hFontBold); m_hFontBold = nullptr; }
        g_logger.info(L"ClientSearchDialog: resources released");
    }

    // =================================================================
    // Создание элементов управления диалога
    // =================================================================
    void createControls() {
        g_logger.info(L"ClientSearchDialog: createControls started");

        m_hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontBold = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        int labelW = 180;
        int editW = 350;
        int editH = 28;
        int rowH = 38;
        int left = 20;
        int top = 15;

        // Заголовок
        HWND hTitle = CreateWindowExW(0, L"STATIC",
            L"Поиск комитента в базе данных",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            left, top, DIALOG_WIDTH - 40, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 40;

        // Поле ввода
        HWND hIdLabel = CreateWindowExW(0, L"STATIC",
            L" комитента:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hIdLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hIdEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
            left + labelW + 10, top, editW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hIdEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hIdEdit, EM_SETLIMITTEXT, 10, 0);
        top += rowH;

        // Поле ввода «Фамилия»
        HWND hLastNameLabel = CreateWindowExW(0, L"STATIC",
            L"Фамилия:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLastNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLastNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hLastNameEdit, EM_SETLIMITTEXT, 64, 0);
        top += rowH;

        // Поле ввода «Имя»
        HWND hFirstNameLabel = CreateWindowExW(0, L"STATIC",
            L"Имя:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hFirstNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hFirstNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hFirstNameEdit, EM_SETLIMITTEXT, 64, 0);
        top += rowH;

        // Поле ввода «Отчество»
        HWND hMiddleNameLabel = CreateWindowExW(0, L"STATIC",
            L"Отчество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hMiddleNameLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 10, top, editW, editH,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hMiddleNameEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hMiddleNameEdit, EM_SETLIMITTEXT, 64, 0);
        top += rowH + 5;

        // Кнопка «Найти»
        m_hSearchBtn = CreateWindowExW(0, L"BUTTON", L"Найти",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left, top, 150, 40,
            m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(m_hSearchBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

        // Кнопка «Закрыть»
        m_hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 170, top, 150, 40,
            m_hWnd, (HMENU)(INT_PTR)2, g_hInstance, nullptr);
        SendMessageW(m_hCloseBtn, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
        top += 50;

        // Статусная строка
        m_hStatusLabel = CreateWindowExW(0, L"STATIC",
            L"Введите комитента или ФИО и нажмите «Найти»",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left, top, DIALOG_WIDTH - 40, 25,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 30;

        // Многострочное текстовое поле для отображения результатов
        m_hResultEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL,
            left, top, DIALOG_WIDTH - 40, DIALOG_HEIGHT - top - 20,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hResultEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Ограничение на количество текста в поле результатов
        SendMessageW(m_hResultEdit, EM_SETLIMITTEXT, 65535, 0);

        m_isSearching = false;

        // Фокус на поле ввода
        SetFocus(m_hIdEdit);

        g_logger.info(L"ClientSearchDialog: all controls created successfully");
    }

    // =================================================================
    // Выполнение поиска (асинхронно, в фоновом потоке)
    // =================================================================
    void onSearch() {
        if (m_isSearching) {
            g_logger.info(L"ClientSearchDialog: search already in progress, ignoring");
            return;
        }

        // Читаем значения полей
        wchar_t buf[256];
        GetWindowTextW(m_hIdEdit, buf, 256);
        std::wstring idText = buf;
        GetWindowTextW(m_hLastNameEdit, buf, 256);
        std::wstring lastName = buf;
        GetWindowTextW(m_hFirstNameEdit, buf, 256);
        std::wstring firstName = buf;
        GetWindowTextW(m_hMiddleNameEdit, buf, 256);
        std::wstring middleName = buf;

        // Определяем тип поиска
        bool hasId = !idText.empty();
        bool hasFio = !lastName.empty() || !firstName.empty() || !middleName.empty();

        if (!hasId && !hasFio) {
            SetWindowTextW(m_hStatusLabel,
                L"Введите комитента или хотя бы одно поле ФИО");
            g_logger.warning(L"ClientSearchDialog: no search parameters entered");
            return;
        }

        g_logger.info(L"ClientSearchDialog: search started - id='" + idText +
            L"', lastName='" + lastName +
            L"', firstName='" + firstName +
            L"', middleName='" + middleName + L"'");

        m_isSearching = true;
        EnableWindow(m_hSearchBtn, FALSE);
        SetWindowTextW(m_hStatusLabel, L"Поиск на сервере...");
        SetWindowTextW(m_hResultEdit, L"");

        // Формируем путь запроса
        std::wstring path;
        if (hasId) {
            // Поиск по имеет приоритет
            path = L"/api/v1/clients/search?id=" + idText;
            g_logger.info(L"ClientSearchDialog: searching by id=" + idText);
        }
        else {
            // Поиск по ФИО
            path = L"/api/v1/clients/search?";
            bool firstParam = true;
            if (!lastName.empty()) {
                if (!firstParam) path += L"&";
                path += L"last_name=" + lastName;
                firstParam = false;
            }
            if (!firstName.empty()) {
                if (!firstParam) path += L"&";
                path += L"first_name=" + firstName;
                firstParam = false;
            }
            if (!middleName.empty()) {
                if (!firstParam) path += L"&";
                path += L"middle_name=" + middleName;
            }
            g_logger.info(L"ClientSearchDialog: searching by FIO, path=" + path);
        }

        // Получаем токен авторизации
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка авторизации: токен отсутствует");
            m_isSearching = false;
            EnableWindow(m_hSearchBtn, TRUE);
            g_logger.error(L"ClientSearchDialog: auth token is empty");
            return;
        }

        // Копируем дескриптор окна для безопасного использования в потоке
        HWND hWndCopy = m_hWnd;

        // Запускаем фоновый поток для сетевого запроса
        // (паттерн из loadClientDataFromServer / blockClient)
        std::thread([hWndCopy, path, authToken]() {
            g_logger.info(L"ClientSearchDialog: background thread started, path=" + path);

            auto response = g_httpsClient.get(path, authToken);

            // Формируем текстовый результат для отображения
            std::wstring resultText;

            if (response && response->contains("clients") &&
                (*response)["clients"].is_array()) {

                const auto& clients = (*response)["clients"];
                int count = static_cast<int>(clients.size());
                g_logger.info(L"ClientSearchDialog: server returned " +
                    std::to_wstring(count) + L" client(s)");

                if (count == 0) {
                    resultText = L"Комитент не найден в базе данных.";
                }
                else {
                    for (int i = 0; i < count; ++i) {
                        const auto& c = clients[i];
                        if (i > 0) {
                            resultText += L"\r\n\r\n========================================\r\n\r\n";
                        }
                        resultText += L" комитента: " +
                            std::to_wstring(c.value("id", 0)) + L"\r\n";
                        resultText += L"ФИО: " +
                            utf8_to_wstring(c.value("full_name", "")) + L"\r\n";
                        resultText += L"Телефон: " +
                            utf8_to_wstring(c.value("phone", "")) + L"\r\n";
                        resultText += L"E-mail: " +
                            utf8_to_wstring(c.value("email", "")) + L"\r\n";
                        resultText += L"Дата рождения: " +
                            utf8_to_wstring(c.value("birth_date", "")) + L"\r\n";
                        resultText += L"Тип паспорта: " +
                            utf8_to_wstring(c.value("passport_type", "")) + L"\r\n";
                        resultText += L"Серия паспорта: " +
                            utf8_to_wstring(c.value("passport_series", "")) + L"\r\n";
                        resultText += L"Номер паспорта: " +
                            utf8_to_wstring(c.value("passport_number", "")) + L"\r\n";
                        resultText += L"Адрес: " +
                            utf8_to_wstring(c.value("address", "")) + L"\r\n";
                        resultText += L"Сдано товаров: " +
                            std::to_wstring(c.value("items_submitted", 0)) + L" шт\r\n";
                        resultText += L"Продано товаров: " +
                            std::to_wstring(c.value("items_sold", 0)) + L" шт\r\n";

                        bool isBlocked = c.value("is_blocked", false);
                        resultText += L"Статус: " +
                            std::wstring(isBlocked ? L"ЗАБЛОКИРОВАН" : L"Активен") + L"\r\n";
                        if (isBlocked) {
                            resultText += L"Причина блокировки: " +
                                utf8_to_wstring(c.value("block_reason", "")) + L"\r\n";
                        }
                        resultText += L"Роль: " +
                            utf8_to_wstring(c.value("role", "")) + L"\r\n";
                    }
                }
            }
            else {
                std::wstring errorMsg = L"Ошибка поиска.";
                if (response && response->contains("error")) {
                    errorMsg += L"\n" + utf8_to_wstring(
                        (*response)["error"].get<std::string>());
                }
                resultText = errorMsg;
                g_logger.error(L"ClientSearchDialog: search failed - " + errorMsg);
            }

            // Передаём результат в -поток через
            // Выделяем копию строки в куче (будет удалена в обработчике)
            std::wstring* pResult = new std::wstring(resultText);
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, ClientSearchDialog::WM_SEARCH_RESULT,
                    0, (LPARAM)pResult);
                g_logger.info(L"ClientSearchDialog: WM_SEARCH_RESULT posted");
            }
            else {
                delete pResult;
                g_logger.warning(L"ClientSearchDialog: window destroyed before "
                    L"result could be posted");
            }
            }).detach();
    }

    // =================================================================
    // Оконная процедура диалога
    // =================================================================
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        ClientSearchDialog* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<ClientSearchDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            return 0;
        }
        pThis = reinterpret_cast<ClientSearchDialog*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                if (id == 1) {
                    // Кнопка «Найти»
                    pThis->onSearch();
                    return 0;
                }
                if (id == 2) {
                    // Кнопка «Закрыть»
                    g_logger.info(L"ClientSearchDialog: Close button clicked");
                    DestroyWindow(hWnd);
                    return 0;
                }
            }
            break;
        }
        case WM_SEARCH_RESULT: {
            // Результат поиска из фонового потока
            std::wstring* pResult = reinterpret_cast<std::wstring*>(lParam);
            if (pResult) {
                g_logger.info(L"ClientSearchDialog: WM_SEARCH_RESULT received, "
                    L"result length=" + std::to_wstring(pResult->length()));
                SetWindowTextW(pThis->m_hResultEdit, pResult->c_str());
                int count = 0;
                // Считаем количество найденных клиентов по разделителю
                size_t pos = 0;
                while ((pos = pResult->find(L" комитента:", pos)) !=
                    std::wstring::npos) {
                    count++;
                    pos += 1;
                }
                std::wstring statusText = (count > 0)
                    ? (L"Найдено комитентов: " + std::to_wstring(count))
                    : L"Поиск завершён";
                SetWindowTextW(pThis->m_hStatusLabel, statusText.c_str());
                pThis->m_isSearching = false;
                EnableWindow(pThis->m_hSearchBtn, TRUE);
                delete pResult;
            }
            return 0;
        }
        case WM_CLOSE:
            g_logger.info(L"ClientSearchDialog: WM_CLOSE received");
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            // Не вызываем - модальный диалог,
            // родительский цикл обработки сообщений продолжается
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    ClientSearchDialog()
        : m_hWnd(nullptr)
        , m_hIdEdit(nullptr)
        , m_hLastNameEdit(nullptr)
        , m_hFirstNameEdit(nullptr)
        , m_hMiddleNameEdit(nullptr)
        , m_hSearchBtn(nullptr)
        , m_hCloseBtn(nullptr)
        , m_hResultEdit(nullptr)
        , m_hStatusLabel(nullptr)
        , m_hFont(nullptr)
        , m_hFontBold(nullptr)
        , m_isSearching(false) {
        g_logger.info(L"ClientSearchDialog: constructor");
    }

    ~ClientSearchDialog() {
        releaseResources();
        g_logger.info(L"ClientSearchDialog: destructor");
    }

    // =================================================================
    // Показать модальный диалог
    // @param hParent родительское окно ( окно товароведа)
    // =================================================================
    void show(HWND hParent) {
        g_logger.info(L"ClientSearchDialog: show() called");

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
                g_logger.error(L"ClientSearchDialog: RegisterClassExW failed");
                return;
            }
            classRegistered = true;
            g_logger.info(L"ClientSearchDialog: window class registered");
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenW - DIALOG_WIDTH) / 2;
        int y = (screenH - DIALOG_HEIGHT) / 2;

        m_hWnd = CreateWindowExW(
            WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME,
            CLASS_NAME,
            L"Поиск комитента",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            x, y, DIALOG_WIDTH, DIALOG_HEIGHT,
            hParent, nullptr, g_hInstance, this
        );
        if (!m_hWnd) {
            g_logger.error(L"ClientSearchDialog: CreateWindowExW failed");
            return;
        }

        // Делаем родительское окно недоступным (модальность)
        EnableWindow(hParent, FALSE);
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        g_logger.info(L"ClientSearchDialog: window shown, entering modal loop");

        // Модальный цикл обработки сообщений
        MSG msg;
        while (IsWindow(m_hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Восстанавливаем родительское окно
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        g_logger.info(L"ClientSearchDialog: modal loop ended");
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
    ISpVoice* m_pVoice = nullptr;
    bool m_comInitialized = false;
    static constexpr int REFRESH_INTERVAL_MS = 2000;

    // =================================================================
    // ЭЛЕМЕНТЫ УПРАВЛЕНИЯ ВВОДА ТОВАРОВ
    // =================================================================
    HWND m_hItemDateLabel, m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit;
    HWND m_hItemQtyEdit, m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn;
    HWND m_hItemSaveBtn, m_hItemCancelBtn, m_hItemListView;
    HWND m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemBackBtn;
    HWND m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel;
    HWND m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice;
    HWND m_hLabelQty, m_hLabelCondition, m_hLabelNote;
    HWND m_hEditRecordBtn;
    HWND m_hCancelInputBtn;

    // ИСПРАВЛЕНИЕ: Заголовок блока данных комитента теперь является членом класса
    // для корректного уничтожения при пересоздании контролов
    HWND m_hCommitBlockTitle;
    // =====================================================================
    // ПРОКРУТКА ПРАВОЙ ПАНЕЛИ
    // =====================================================================
    std::vector<HWND> m_scrollableControls;   // контролы правой панели
    int m_rightScrollPos = 0;                 // текущее смещение прокрутки
    int m_rightContentHeight = 0;             // полная высота контента

    // =================================================================
    // ЭЛЕМЕНТЫ УПРАВЛЕНИЯ ДЛЯ ДАННЫХ КОМИТЕНТА (first_time)
    // =================================================================
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

    // Отображение сохраненных данных комитента
    HWND m_hCommitFioLabel, m_hCommitFioValue;
    HWND m_hCommitIdLabel, m_hCommitIdValue;
    HWND m_hCommitPhoneDisplayLabel, m_hCommitPhoneDisplayValue;

    // Отображение даты рождения комитента (для всех очередей)
    HWND m_hCommitBirthDateDisplayLabel, m_hCommitBirthDateDisplayValue;

    // КНОПКА УДАЛЕНИЯ ТАЛОНА И КНОПКА ПОИСКА
    HWND m_hDeleteTicketBtn;   // Кнопка "Удалить (не пришёл)"
    HWND m_hSearchClientBtn;   // Кнопка "Поиск комитента"

    int m_currentClientId;
    std::wstring m_currentTicketNumber;
    std::wstring m_currentWindowNumber;
    std::wstring m_currentClientName;
    // Время принятия/вызова текущего талона, секунды с эпохи
    int64_t m_currentTicketAcceptedAt = 0;
    // =====================================================================
    // ХРАНЕНИЕ ПРИНЯТОГО ТАЛОНА ДО ЕГО ЗАВЕРШЕНИЯ:
    // талон должен оставаться в списке до «Сохранить все»
    // или «Удалить (не пришёл)»
    // =====================================================================
    json m_currentAcceptedTicket;
    bool m_hasCurrentAcceptedTicket = false;
    std::vector<json> m_tempItems;
    bool m_isSaving;
    bool m_isCommitDataSaved;
    std::wstring m_savedCommitPhone;
    int m_editingItemIndex;

    // ПЕРЕМЕННЫЕ ДЛЯ ХРАНЕНИЯ ЗАГРУЖЕННЫХ ДАННЫХ КЛИЕНТА
    std::wstring m_loadedClientFio;
    std::wstring m_loadedClientPhone;
    std::wstring m_loadedClientBirthDate;
    std::mutex m_clientDataMutex;

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

    // =================================================================
    // ИНИЦИАЛИЗАЦИЯ TTS
    // =================================================================
    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            g_logger.error(L"CoInitializeEx failed");
            return;
        }
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (SUCCEEDED(hr)) g_logger.info(L"TTS initialized");
    }

    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (!m_pVoice) return;
        std::wstring full = L"Клиент с номером " + number + L" просим подойти к окну номер " + windowNumber;
        m_pVoice->Speak(full.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    // =========================================================================
    // ОПРЕДЕЛЕНИЕ НОМЕРА ОКНА ПО ТЕЛЕФОНУ ТОВАРОВЕДА
    // =========================================================================
    // Возвращает номер окна (в виде строки, например "1" ... "6"),
    // соответствующий авторизованному товароведу.
    //
    // Логика:
    //   1. Получаем телефон товароведа из AuthManager.
    //   2. Ищем телефон в маппинге Config::WORKER_WINDOW_MAP.
    //   3. Если найден — возвращаем соответствующий номер окна.
    //   4. Если не найден — возвращаем Config::DEFAULT_WINDOW_NUMBER.
    //
    // Потокобезопасность:
    //   Метод вызывается ТОЛЬКО из UI-потока (из onAccept()).
    //   g_authManager.getPhone() читает m_phone, который устанавливается
    //   при логине (до создания WorkerWindow). Гонка исключена.
    // =========================================================================
    std::wstring getWindowNumberForCurrentWorker() {
        // Получаем телефон авторизованного товароведа
        std::wstring workerPhone = g_authManager.getPhone();

        g_logger.info(L"getWindowNumberForCurrentWorker: worker phone=" + workerPhone);

        // Ищем телефон в маппинге
        for (int i = 0; i < Config::WORKER_WINDOW_MAP_SIZE; ++i) {
            if (workerPhone == Config::WORKER_WINDOW_MAP[i].phone) {
                int windowNum = Config::WORKER_WINDOW_MAP[i].windowNumber;
                std::wstring windowStr = std::to_wstring(windowNum);
                g_logger.info(L"getWindowNumberForCurrentWorker: MATCH FOUND - phone=" +
                    workerPhone + L" -> window=" + windowStr);
                return windowStr;
            }
        }

        // Телефон не найден в маппинге — используем окно по умолчанию
        std::wstring defaultWindow = std::to_wstring(Config::DEFAULT_WINDOW_NUMBER);
        g_logger.warning(L"getWindowNumberForCurrentWorker: phone=" + workerPhone +
            L" NOT FOUND in WORKER_WINDOW_MAP, using default window=" + defaultWindow);
        return defaultWindow;
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

    // =============================================================================
    // АСИНХРОННАЯ ЗАГРУЗКА ДАННЫХ КЛИЕНТА С СЕРВЕРА
    void loadClientDataFromServer(int clientId) {
        if (clientId <= 0) {
            g_logger.warning(L"loadClientDataFromServer: invalid clientId=" + std::to_wstring(clientId));
            return;
        }
        g_logger.info(L"loadClientDataFromServer: started for clientId=" + std::to_wstring(clientId));
        std::wstring authToken = g_authManager.getAuthToken();
        HWND hWndCopy = m_hWnd;

        // Запускаем фоновый поток для загрузки данных
        std::thread([this, clientId, authToken, hWndCopy]() {
            g_logger.info(L"loadClientDataFromServer: background thread started for clientId=" + std::to_wstring(clientId));
            std::wstring path = L"/api/v1/clients/by_id?id=" + std::to_wstring(clientId);
            auto response = g_httpsClient.get(path, authToken);

            if (response && !response->contains("error")) {
                std::wstring fio = utf8_to_wstring(response->value("name", ""));
                std::wstring phone = utf8_to_wstring(response->value("phone", ""));
                std::wstring birthDate = utf8_to_wstring(response->value("birth_date", ""));

                g_logger.info(L"loadClientDataFromServer: loaded data - fio='" + fio +
                    L"', phone='" + phone + L"', birthDate='" + birthDate + L"'");

                {
                    std::lock_guard<std::mutex> lock(m_clientDataMutex);
                    m_loadedClientFio = fio;
                    m_loadedClientPhone = phone;
                    m_loadedClientBirthDate = birthDate;
                }
                PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)clientId, 0);
                g_logger.info(L"loadClientDataFromServer: posted WM_CLIENT_DATA_LOADED for clientId=" + std::to_wstring(clientId));
            }
            else {
                g_logger.error(L"loadClientDataFromServer: failed to load data for clientId=" + std::to_wstring(clientId));
                PostMessageW(hWndCopy, WM_CLIENT_DATA_LOADED, (WPARAM)0, 0);
            }
            }).detach();
    }

    // =====================================================================
// Проверка: есть ли незавершённый принятый талон
// =====================================================================
    bool hasCurrentAcceptedTicket() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hasCurrentAcceptedTicket;
    }

    // =====================================================================
    // Сохранить принятый талон до его завершения
    // =====================================================================
    void setCurrentAcceptedTicket(const json& ticket) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentAcceptedTicket = ticket;
        m_hasCurrentAcceptedTicket = true;
    }

    // =====================================================================
    // Удалить принятый талон из локального списка и очистить его хранение
    // =====================================================================
    void removeCurrentAcceptedTicketFromLocalList() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_hasCurrentAcceptedTicket || !m_currentAcceptedTicket.is_object()) {
            return;
        }

        std::string acceptedNumber =
            m_currentAcceptedTicket.value("ticket_number", std::string(""));

        if (!acceptedNumber.empty()) {
            for (auto it = m_tickets.begin(); it != m_tickets.end(); ++it) {
                if (it->value("ticket_number", std::string("")) == acceptedNumber) {
                    m_tickets.erase(it);
                    break;
                }
            }
        }

        if (m_selectedIndex >= (int)m_tickets.size()) {
            m_selectedIndex = (int)m_tickets.size() - 1;
        }

        m_currentAcceptedTicket = nullptr;
        m_hasCurrentAcceptedTicket = false;

        g_logger.info(L"removeCurrentAcceptedTicketFromLocalList: accepted ticket removed, remaining tickets=" +
            std::to_wstring(m_tickets.size()));
    }

    // =====================================================================
// Настройка вертикальной полосы прокрутки правой панели
// =====================================================================
    void setupRightPanelScrollBar() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientH = rc.bottom - rc.top;

        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = m_rightContentHeight;
        si.nPage = clientH;
        si.nPos = 0;
        SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

        bool needScroll = (m_currentMode == Mode::ITEM_INPUT) &&
            (m_rightContentHeight > clientH);
        ShowScrollBar(m_hWnd, SB_VERT, needScroll ? TRUE : FALSE);
        m_rightScrollPos = 0;
    }

    // =====================================================================
    // Прокрутка правой панели на newPos (пиксели)
    // =====================================================================
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
            SetWindowPos(h, NULL, pt.x, pt.y + delta, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER);
        }

        m_rightScrollPos = newPos;
        SetScrollPos(m_hWnd, SB_VERT, newPos, TRUE);
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
            if (index != LB_ERR) {
                SendMessageW(m_hListBox, LB_SETITEMDATA, index, (LPARAM)i);
            }
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

        // =====================================================================
        // «Принять» и «Обслужен (Следующий)» запрещены, пока есть незавершённый
        // принятый талон.
        // =====================================================================
        EnableWindow(m_hAcceptBtn, (count > 0) && !isInputMode && !hasActiveAccepted);
        EnableWindow(m_hServeBtn, (count > 0) && !isInputMode && !hasActiveAccepted);

        // =====================================================================
        // Кнопка «Удалить (не пришёл)»:
        // - если есть текущий принятый талон, она становится активной через
        //   120 секунд после принятия/вызова;
        // - если текущего принятого талона нет, она активна в режиме списка,
        //   если есть талоны в списке.
        // =====================================================================
        bool deleteTicketEnabled = false;

        if (hasActiveAccepted) {
            if (!m_currentTicketNumber.empty() && m_currentTicketAcceptedAt > 0) {
                auto now = std::chrono::system_clock::now();
                int64_t nowSec =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        now.time_since_epoch()
                    ).count();

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

            // =====================================================================
            // Если есть незавершённый принятый талон, его нужно сохранить
            // в локальном списке даже в том случае, если сервер уже не вернул
            // его в списке ожидающих.
            // =====================================================================
            if (m_hasCurrentAcceptedTicket && m_currentAcceptedTicket.is_object()) {
                std::string acceptedNumber =
                    m_currentAcceptedTicket.value("ticket_number", std::string(""));

                if (!acceptedNumber.empty()) {
                    int oldAcceptedIndex = -1;

                    for (int i = 0; i < (int)m_tickets.size(); ++i) {
                        if (m_tickets[i].value("ticket_number", std::string("")) == acceptedNumber) {
                            oldAcceptedIndex = i;
                            break;
                        }
                    }

                    bool presentInNewTickets = false;

                    for (const auto& t : newTickets) {
                        if (t.value("ticket_number", std::string("")) == acceptedNumber) {
                            presentInNewTickets = true;
                            break;
                        }
                    }

                    if (!presentInNewTickets) {
                        int insertPos = oldAcceptedIndex;

                        if (insertPos < 0 || insertPos >(int)newTickets.size()) {
                            insertPos = (int)newTickets.size();
                        }

                        newTickets.insert(newTickets.begin() + insertPos, m_currentAcceptedTicket);
                    }
                }
            }

            m_tickets = std::move(newTickets);

            if (selectedIndex >= (int)m_tickets.size()) selectedIndex = -1;
            m_selectedIndex = selectedIndex;
        }
        // =====================================================================
        // Отправляем сообщение UI-потоку только если окно ещё существует.
        // Это защищает от попытки отправить сообщение уже уничтоженному окну.
        // =====================================================================
        if (m_running.load() && IsWindow(m_hWnd)) {
            PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
        }
        else {
            g_logger.warning(L"WorkerWindow: refreshList skipped PostMessage, window is closing");
        }
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
        g_logger.info(L"destroyItemInputControls: destroying all input controls");
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemListView, m_hItemTotalQtyLabel,
            m_hItemTotalPriceLabel, m_hItemBackBtn,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel,
            m_hEditRecordBtn, m_hCancelInputBtn,
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
            m_hCommitBlockTitle // ИСПРАВЛЕНИЕ: Добавлено уничтожение заголовка блока
        };
        for (auto& c : controls) {
            if (c) {
                DestroyWindow(c);
                c = nullptr;
            }
        }
        g_logger.info(L"destroyItemInputControls: all controls destroyed");
    }

    // =================================================================
    // СОЗДАНИЕ ЭЛЕМЕНТОВ УПРАВЛЕНИЯ ВВОДА ТОВАРОВ
    // =================================================================
    void createItemInputControls(int left, int top, int width) {
        if (width < 400) return;
        int labelW = 200, editW = width - labelW - 20, editH = 28, rowH = 35;

        // =================================================================
        // БЛОК ДАННЫХ КОМИТЕНТА (только для first_time)
        // =================================================================
        if (m_currentQueueType == L"first_time") {
            g_logger.info(L"createItemInputControls: creating COMMITTEE DATA block for first_time queue");

            // ИСПРАВЛЕНИЕ: Используем член класса m_hCommitBlockTitle вместо локальной переменной
            m_hCommitBlockTitle = CreateWindowExW(0, L"STATIC",
                L"=== Данные нового комитента ===",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(m_hCommitBlockTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            top += 10;

            // =================================================================
            // БЛОК ВВОДА ДАННЫХ КОМИТЕНТА (скрывается после сохранения)
            // =================================================================
            if (!m_isCommitDataSaved) {
                m_hCommitLastNameLabel = CreateWindowExW(0, L"STATIC", L"Фамилия:",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_LASTNAME_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitLastNameLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_LASTNAME_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitLastNameEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitLastNameEdit, EM_SETLIMITTEXT, 64, 0);
                top += rowH;

                m_hCommitFirstNameLabel = CreateWindowExW(0, L"STATIC", L"Имя:",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIRSTNAME_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitFirstNameLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIRSTNAME_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitFirstNameEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitFirstNameEdit, EM_SETLIMITTEXT, 64, 0);
                top += rowH;

                m_hCommitMiddleNameLabel = CreateWindowExW(0, L"STATIC", L"Отчество:",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_MIDDLENAME_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitMiddleNameLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_MIDDLENAME_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitMiddleNameEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitMiddleNameEdit, EM_SETLIMITTEXT, 64, 0);
                top += rowH;

                m_hCommitBirthDateLabel = CreateWindowExW(0, L"STATIC", L"Дата рождения (дд.мм.гггг):",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitBirthDateLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitBirthDateEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitBirthDateEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitBirthDateEdit, EM_SETLIMITTEXT, 10, 0);
                top += rowH;

                m_hCommitPassTypeLabel = CreateWindowExW(0, L"STATIC", L"Тип паспорта:",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSTYPE_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassTypeLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitPassTypeCombo = CreateWindowExW(0, L"COMBOBOX", L"",
                    WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                    left + labelW + 5, top, editW, editH + 100, m_hWnd, (HMENU)ID_COMMIT_PASSTYPE_COMBO, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassTypeCombo, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitPassTypeCombo, CB_ADDSTRING, 0, (LPARAM)L"Паспорт гражданина РФ");
                SendMessageW(m_hCommitPassTypeCombo, CB_ADDSTRING, 0, (LPARAM)L"Заграничный паспорт гражданина РФ");
                SendMessageW(m_hCommitPassTypeCombo, CB_SETCURSEL, 0, 0);
                top += rowH;

                m_hCommitPassSeriesLabel = CreateWindowExW(0, L"STATIC", L"Серия паспорта (4 цифры):",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSSERIES_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassSeriesLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitPassSeriesEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
                    left + labelW + 5, top, editW / 3, editH, m_hWnd, (HMENU)ID_COMMIT_PASSSERIES_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassSeriesEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitPassSeriesEdit, EM_SETLIMITTEXT, 4, 0);
                top += rowH;

                m_hCommitPassNumberLabel = CreateWindowExW(0, L"STATIC", L"Номер паспорта (6 цифр):",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PASSNUMBER_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassNumberLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitPassNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
                    left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_COMMIT_PASSNUMBER_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitPassNumberEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitPassNumberEdit, EM_SETLIMITTEXT, 6, 0);
                top += rowH;

                m_hCommitPhoneLabel = CreateWindowExW(0, L"STATIC", L"Телефон (+7-911-911-11-11):",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitPhoneLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitPhoneEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitPhoneEdit, EM_SETLIMITTEXT, 18, 0);
                top += rowH;

                m_hCommitAddressLabel = CreateWindowExW(0, L"STATIC", L"Адрес проживания:",
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ADDRESS_LABEL, g_hInstance, nullptr);
                SendMessageW(m_hCommitAddressLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitAddressEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                    left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ADDRESS_EDIT, g_hInstance, nullptr);
                SendMessageW(m_hCommitAddressEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                SendMessageW(m_hCommitAddressEdit, EM_SETLIMITTEXT, 200, 0);
                top += rowH + 5;

                m_hCommitSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить данные комитента",
                    WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                    left, top, 220, 35, m_hWnd, (HMENU)ID_COMMIT_SAVE_BTN, g_hInstance, nullptr);
                SendMessageW(m_hCommitSaveBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

                m_hCommitCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить",
                    WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                    left + 230, top, 150, 35, m_hWnd, (HMENU)ID_COMMIT_CANCEL_BTN, g_hInstance, nullptr);
                SendMessageW(m_hCommitCancelBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
                top += 50;

            }

            // =================================================================
           // БЛОК ОТОБРАЖЕНИЯ СОХРАНЕННЫХ ДАННЫХ КОМИТЕНТА
           // ИСПРАВЛЕНИЕ: скрытый блок больше НЕ резервирует место,
           // поэтому пустая полоса между блоками исчезает.
           // =================================================================
            int dispTop = top;

            m_hCommitFioLabel = CreateWindowExW(0, L"STATIC", L"ФИО комитента:",
                WS_CHILD | SS_RIGHT,
                left, dispTop, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitFioLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            m_hCommitFioValue = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | SS_LEFT,
                left + labelW + 5, dispTop, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitFioValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitIdLabel = CreateWindowExW(0, L"STATIC", L"ID комитента:",
                WS_CHILD | SS_RIGHT,
                left, dispTop + rowH, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitIdLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            m_hCommitIdValue = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | SS_LEFT,
                left + labelW + 5, dispTop + rowH, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitIdValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitPhoneDisplayLabel = CreateWindowExW(0, L"STATIC", L"Телефон:",
                WS_CHILD | SS_RIGHT,
                left, dispTop + 2 * rowH, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitPhoneDisplayLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            m_hCommitPhoneDisplayValue = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | SS_LEFT,
                left + labelW + 5, dispTop + 2 * rowH, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitPhoneDisplayValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            // Управление видимостью блока отображения.
            // Место в разметке блок занимает ТОЛЬКО когда он видим.
            if (m_isCommitDataSaved) {
                top += 3 * rowH + 10;
                ShowWindow(m_hCommitFioLabel, SW_SHOW);
                ShowWindow(m_hCommitFioValue, SW_SHOW);
                ShowWindow(m_hCommitIdLabel, SW_SHOW);
                ShowWindow(m_hCommitIdValue, SW_SHOW);
                ShowWindow(m_hCommitPhoneDisplayLabel, SW_SHOW);
                ShowWindow(m_hCommitPhoneDisplayValue, SW_SHOW);
            }
            else {
                ShowWindow(m_hCommitFioLabel, SW_HIDE);
                ShowWindow(m_hCommitFioValue, SW_HIDE);
                ShowWindow(m_hCommitIdLabel, SW_HIDE);
                ShowWindow(m_hCommitIdValue, SW_HIDE);
                ShowWindow(m_hCommitPhoneDisplayLabel, SW_HIDE);
                ShowWindow(m_hCommitPhoneDisplayValue, SW_HIDE);
            }
        }
        // =============================================================================
        // БЛОК ОТОБРАЖЕНИЯ ДАННЫХ КОМИТЕНТА ДЛЯ ДРУГИХ ОЧЕРЕДЕЙ
        // =============================================================================
        else if (m_currentQueueType == L"general" || m_currentQueueType == L"extra_20" ||
            m_currentQueueType == L"trust" || m_currentQueueType == L"paid" ||
            m_currentQueueType == L"expensive") {
            g_logger.info(L"createItemInputControls: creating COMMITTEE DATA DISPLAY block for queue: " + m_currentQueueType);

            // ИСПРАВЛЕНИЕ: Используем член класса m_hCommitBlockTitle
            m_hCommitBlockTitle = CreateWindowExW(0, L"STATIC",
                L"=== Данные комитента ===",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
            SendMessageW(m_hCommitBlockTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            top += 10;

            // ФИО комитента
            m_hCommitFioLabel = CreateWindowExW(0, L"STATIC", L"ФИО комитента:",
                WS_VISIBLE | WS_CHILD | SS_RIGHT,
                left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitFioLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitFioValue = CreateWindowExW(0, L"STATIC", L"Загрузка...",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_FIO_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitFioValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            top += rowH;

            // ID комитента
            m_hCommitIdLabel = CreateWindowExW(0, L"STATIC", L"ID комитента:",
                WS_VISIBLE | WS_CHILD | SS_RIGHT,
                left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitIdLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitIdValue = CreateWindowExW(0, L"STATIC", L"",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_ID_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitIdValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            top += rowH;

            // Дата рождения
            m_hCommitBirthDateDisplayLabel = CreateWindowExW(0, L"STATIC", L"Дата рождения:",
                WS_VISIBLE | WS_CHILD | SS_RIGHT,
                left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_DISPLAY_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitBirthDateDisplayLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitBirthDateDisplayValue = CreateWindowExW(0, L"STATIC", L"Загрузка...",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_BIRTHDATE_DISPLAY_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitBirthDateDisplayValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            top += rowH;

            // Телефон
            m_hCommitPhoneDisplayLabel = CreateWindowExW(0, L"STATIC", L"Телефон:",
                WS_VISIBLE | WS_CHILD | SS_RIGHT,
                left, top, labelW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_LABEL, g_hInstance, nullptr);
            SendMessageW(m_hCommitPhoneDisplayLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

            m_hCommitPhoneDisplayValue = CreateWindowExW(0, L"STATIC", L"Загрузка...",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_COMMIT_PHONE_DISPLAY_VALUE, g_hInstance, nullptr);
            SendMessageW(m_hCommitPhoneDisplayValue, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
            top += rowH + 10;

            g_logger.info(L"createItemInputControls: committee data display block created for queue: " + m_currentQueueType);
        }

        // =================================================================
        // БЛОК ВВОДА ТОВАРОВ (для всех типов очередей)
        // =================================================================
        m_hTitle = CreateWindowExW(0, L"STATIC", L"Ввод товаров комитента",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left, top, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 35;

        m_hLabelDate = CreateWindowExW(0, L"STATIC", L"Дата/время:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDate, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemDateLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_READONLY,
            left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DATE_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hItemDateLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += rowH;

        m_hLabelNumber = CreateWindowExW(0, L"STATIC", L"Номер приложения к договору:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNumber, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_READONLY,
            left + labelW + 5, top, 80, editH, m_hWnd, (HMENU)ID_ITEM_NUMBER_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNumberEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += rowH;

        m_hLabelDesc = CreateWindowExW(0, L"STATIC", L"Наименование товара:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDesc, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemDescEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DESC_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemDescEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        SendMessageW(m_hItemDescEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;

        m_hLabelPrice = CreateWindowExW(0, L"STATIC", L"Цена единицы товара:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelPrice, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemPriceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_ITEM_PRICE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemPriceEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += rowH;

        m_hLabelQty = CreateWindowExW(0, L"STATIC", L"Количество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelQty, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemQtyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
            left + labelW + 5, top, 80, editH, m_hWnd, (HMENU)ID_ITEM_QTY_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemQtyEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += rowH;

        m_hLabelCondition = CreateWindowExW(0, L"STATIC", L"Состояние:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelCondition, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemConditionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"состояние износа 30%",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_READONLY,
            left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_CONDITION_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemConditionEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        SendMessageW(m_hItemConditionEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;

        m_hLabelNote = CreateWindowExW(0, L"STATIC", L"Примечание:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNote, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemNoteEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_NOTE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNoteEdit, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        SendMessageW(m_hItemNoteEdit, EM_SETLIMITTEXT, 300, 0);
        top += rowH + 5;

        m_hItemAddBtn = CreateWindowExW(0, L"BUTTON", L"Добавить позицию",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left, top, 180, 35, m_hWnd, (HMENU)ID_ITEM_ADD_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemAddBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += 45;

        m_hItemListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
            left, top, width - 10, 150, m_hWnd, (HMENU)ID_ITEM_LISTVIEW, g_hInstance, nullptr);
        SendMessageW(m_hItemListView, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        LVCOLUMNW col = { 0 }; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::vector<std::wstring> headers = { L"№", L"Наименование товара", L"Цена", L"Кол-во", L"Состояние", L"Примечание" };
        std::vector<int> widths = { 40, 160, 80, 60, 100, 120 };
        int totalColWidth = width - 10, sum = 0; for (auto w : widths) sum += w;
        for (size_t i = 0; i < headers.size(); ++i) {
            col.pszText = const_cast<LPWSTR>(headers[i].c_str());
            col.cx = (int)((float)widths[i] / sum * totalColWidth);
            col.iSubItem = static_cast<int>(i);
            ListView_InsertColumn(m_hItemListView, static_cast<int>(i), &col);
        }
        top += 160;

        m_hItemTotalQtyLabel = CreateWindowExW(0, L"STATIC", L"Общее количество: 0",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_QTY, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalQtyLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemTotalPriceLabel = CreateWindowExW(0, L"STATIC", L"Общая цена: 0.00",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_PRICE, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalPriceLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += 40;

        m_hItemTotalClientAmountLabel = CreateWindowExW(0, L"STATIC", L"Выплата комитенту: 0.00",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_CLIENT_AMOUNT, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalClientAmountLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemTotalStoreAmountLabel = CreateWindowExW(0, L"STATIC", L"Прибыль магазина: 0.00",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_STORE_AMOUNT, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalStoreAmountLabel, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hEditRecordBtn = CreateWindowExW(0, L"BUTTON", L"Редактировать запись",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 640, top - 40, 180, 30, m_hWnd, (HMENU)ID_ITEM_EDIT_RECORD_BTN, g_hInstance, nullptr);
        SendMessageW(m_hEditRecordBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hCancelInputBtn = CreateWindowExW(0, L"BUTTON", L"Отмена",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 640, top, 180, 30, m_hWnd, (HMENU)ID_ITEM_CANCEL_INPUT_BTN, g_hInstance, nullptr);
        SendMessageW(m_hCancelInputBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        top += 40;

        m_hItemSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить все",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_SAVE_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemSaveBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 200, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_CANCEL_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemCancelBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hItemBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            left + 400, top, 120, 40, m_hWnd, (HMENU)ID_ITEM_BACK_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemBackBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        // =====================================================================
        // ИСПРАВЛЕНИЕ ИНТЕРФЕЙСА:
        // Кнопка «Удалить (не пришёл)» размещается под кнопкой «Сохранить все»
        // =====================================================================
        if (!m_hDeleteTicketBtn) {
            m_hDeleteTicketBtn = CreateWindowExW(0, L"BUTTON",
                L"Удалить (не пришёл)",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                left, top + 45, 180, 40,
                m_hWnd, (HMENU)ID_DELETE_TICKET_BTN, g_hInstance, nullptr);
            SendMessageW(m_hDeleteTicketBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        }
        else {
            SetWindowPos(m_hDeleteTicketBtn, NULL,
                left, top + 45, 180, 40,
                SWP_NOZORDER);
        }

        // =====================================================================
        // РЕЕСТР ПРОКРУЧИВАЕМЫХ КОНТРОЛОВ ПРАВОЙ ПАНЕЛИ И РАСЧЁТ ВЫСОТЫ
        // =====================================================================
        HWND scrollables[] = {
            m_hCommitBlockTitle,
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
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote,
            m_hItemDateLabel, m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit,
            m_hItemQtyEdit, m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn,
            m_hItemListView, m_hItemTotalQtyLabel, m_hItemTotalPriceLabel,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel,
            m_hEditRecordBtn, m_hCancelInputBtn,
            m_hItemSaveBtn, m_hItemCancelBtn, m_hItemBackBtn,
            m_hDeleteTicketBtn
        };
        m_scrollableControls.clear();
        for (HWND h : scrollables) if (h) m_scrollableControls.push_back(h);

        // Полная высота контента: нижний ряд кнопок (40) + кнопка удаления (45+40)
        m_rightContentHeight = top + 85;

        setupRightPanelScrollBar();

        g_logger.info(L"createItemInputControls: all controls created successfully");
    }

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
            m_hEditRecordBtn, m_hCancelInputBtn
        };
        for (auto c : controls) { if (c) ShowWindow(c, flag); }
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
        SetWindowTextW(m_hItemNumberEdit, std::to_wstring(nextNumber).c_str());
    }

    void updateTempItemsList() {
        ListView_DeleteAllItems(m_hItemListView);
        int totalQty = 0; double totalPrice = 0.0, totalClientAmount = 0.0, totalStoreAmount = 0.0;
        for (size_t i = 0; i < m_tempItems.size(); ++i) {
            const auto& item = m_tempItems[i];
            int itemNumber = m_maxItemNumber + static_cast<int>(i) + 1;
            std::wstring desc = utf8_to_wstring(item.value("description", ""));
            double price = item.value("estimated_price", 0.0);
            int qty = item.value("quantity", 1);
            std::wstring condition = utf8_to_wstring(item.value("condition", ""));
            std::wstring note = utf8_to_wstring(item.value("note", ""));

            LVITEMW lvi = { 0 }; lvi.mask = LVIF_TEXT; lvi.iItem = static_cast<int>(i);
            std::vector<std::wstring> columns = { std::to_wstring(itemNumber), desc, (price > 0 ? std::to_wstring(price) : L""), std::to_wstring(qty), condition, note };
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
        wchar_t buf[50]; swprintf_s(buf, L"Общая цена: %.2f", totalPrice);
        SetWindowTextW(m_hItemTotalPriceLabel, buf);
        wchar_t clientAmountBuf[64]; swprintf_s(clientAmountBuf, L"Выплата комитенту: %.2f", totalClientAmount);
        SetWindowTextW(m_hItemTotalClientAmountLabel, clientAmountBuf);
        wchar_t storeAmountBuf[64]; swprintf_s(storeAmountBuf, L"Прибыль магазина: %.2f", totalStoreAmount);
        SetWindowTextW(m_hItemTotalStoreAmountLabel, storeAmountBuf);
    }

    void onItemAdd() {
        g_logger.info(L"onItemAdd: started");
        if (m_currentQueueType == L"first_time" && !m_isCommitDataSaved) {
            MessageBoxW(m_hWnd, L"Сначала сохраните данные комитента", L"Внимание", MB_OK);
            return;
        }

        wchar_t descBuf[256]; GetWindowTextW(m_hItemDescEdit, descBuf, 256); std::wstring desc = descBuf;
        if (desc.empty()) { MessageBoxW(m_hWnd, L"Введите наименование товара", L"Внимание", MB_OK); return; }

        wchar_t priceBuf[32]; GetWindowTextW(m_hItemPriceEdit, priceBuf, 32); std::wstring priceStr = priceBuf;
        for (auto& c : priceStr) if (c == L',') c = L'.';
        double price = 0.0;
        try { price = std::stod(priceStr); }
        catch (...) { MessageBoxW(m_hWnd, L"Некорректная цена", L"Ошибка", MB_OK); return; }
        if (price < 0) { MessageBoxW(m_hWnd, L"Цена не может быть отрицательной", L"Ошибка", MB_OK); return; }

        int qty = 1; wchar_t qtyBuf[16]; GetWindowTextW(m_hItemQtyEdit, qtyBuf, 16);
        try { qty = std::stoi(qtyBuf); }
        catch (...) { MessageBoxW(m_hWnd, L"Введите целое количество", L"Ошибка", MB_OK); return; }
        if (qty < 1) { MessageBoxW(m_hWnd, L"Количество не менее 1", L"Ошибка", MB_OK); return; }

        wchar_t condBuf[256], noteBuf[512];
        GetWindowTextW(m_hItemConditionEdit, condBuf, 256);
        GetWindowTextW(m_hItemNoteEdit, noteBuf, 512);

        CommissionCalc::CommissionRates rates = CommissionCalc::calculateByPrice(price);
        double clientPercent = rates.clientPercent;
        double storePercent = rates.storePercent;
        double clientAmount = price * qty * clientPercent / 100.0;
        double storeAmount = price * qty * storePercent / 100.0;

        g_logger.info(L"onItemAdd: price=" + std::to_wstring(price) +
            L", clientPercent=" + std::to_wstring(clientPercent) +
            L", storePercent=" + std::to_wstring(storePercent) +
            L", clientAmount=" + std::to_wstring(clientAmount) +
            L", storeAmount=" + std::to_wstring(storeAmount));

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
            g_logger.info(L"onItemAdd: item updated at index " + std::to_wstring(m_editingItemIndex));
            m_editingItemIndex = -1;
        }
        else {
            m_tempItems.push_back(item);
            g_logger.info(L"onItemAdd: new item added, total=" + std::to_wstring(m_tempItems.size()));
        }

        updateTempItemsList();
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        updateNextItemNumber();
        SetFocus(m_hItemDescEdit);
    }

    void onEditRecord() {
        int sel = ListView_GetNextItem(m_hItemListView, -1, LVNI_SELECTED);
        if (sel < 0 || sel >= (int)m_tempItems.size()) {
            MessageBoxW(m_hWnd, L"Выберите запись для редактирования", L"Внимание", MB_OK);
            return;
        }
        m_editingItemIndex = sel;
        const auto& item = m_tempItems[sel];
        SetWindowTextW(m_hItemDescEdit, utf8_to_wstring(item.value("description", "")).c_str());
        SetWindowTextW(m_hItemPriceEdit, std::to_wstring(item.value("estimated_price", 0.0)).c_str());
        SetWindowTextW(m_hItemQtyEdit, std::to_wstring(item.value("quantity", 1)).c_str());
        SetWindowTextW(m_hItemConditionEdit, utf8_to_wstring(item.value("condition", "")).c_str());
        SetWindowTextW(m_hItemNoteEdit, utf8_to_wstring(item.value("note", "")).c_str());
        g_logger.info(L"onEditRecord: editing item at index " + std::to_wstring(sel));
    }

    void onCancelInput() {
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        m_editingItemIndex = -1;
        g_logger.info(L"onCancelInput: input fields cleared");
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

        if (lastName.empty() || firstName.empty()) {
            MessageBoxW(m_hWnd, L"Заполните Фамилию и Имя", L"Ошибка", MB_OK); return;
        }

        std::wregex dateRegex(L"^\\d{2}\\.\\d{2}\\.\\d{4}$");
        if (!std::regex_match(birthDate, dateRegex)) {
            MessageBoxW(m_hWnd, L"Дата рождения должна быть в формате дд.мм.гггг (например, 11.11.1990)", L"Ошибка", MB_OK); return;
        }

        std::wregex phoneRegex(L"^\\+7-\\d{3}-\\d{3}-\\d{2}-\\d{2}$");
        if (!std::regex_match(phone, phoneRegex)) {
            MessageBoxW(m_hWnd, L"Телефон должен быть в формате +7-911-911-11-11", L"Ошибка", MB_OK); return;
        }

        int passType = (int)SendMessageW(m_hCommitPassTypeCombo, CB_GETCURSEL, 0, 0);
        if (passType == 0) {
            if (passSeries.length() != 4) {
                MessageBoxW(m_hWnd, L"Серия паспорта РФ должна содержать 4 цифры", L"Ошибка", MB_OK); return;
            }
            if (passNumber.length() != 6) {
                MessageBoxW(m_hWnd, L"Номер паспорта РФ должен содержать 6 цифр", L"Ошибка", MB_OK); return;
            }
        }
        else {
            if (passSeries.length() != 2) {
                MessageBoxW(m_hWnd, L"Серия заграничного паспорта должна содержать 2 цифры", L"Ошибка", MB_OK); return;
            }
            if (passNumber.length() != 8) {
                MessageBoxW(m_hWnd, L"Номер заграничного паспорта должен содержать 8 цифр", L"Ошибка", MB_OK); return;
            }
        }

        if (address.empty()) {
            MessageBoxW(m_hWnd, L"Заполните адрес проживания", L"Ошибка", MB_OK); return;
        }

        std::wstring normalizedPhone = phone;
        for (auto& c : normalizedPhone) {
            if (c == L'-' || c == L' ' || c == L'(' || c == L')') c = L'\0';
        }
        std::wstring cleanPhone;
        for (auto c : normalizedPhone) if (c != L'\0') cleanPhone += c;

        json request;
        request["last_name"] = wstring_to_utf8(lastName);
        request["first_name"] = wstring_to_utf8(firstName);
        request["middle_name"] = wstring_to_utf8(middleName);
        request["birth_date"] = wstring_to_utf8(birthDate);
        request["passport_type"] = (passType == 0) ? "rf" : "foreign";
        request["passport_series"] = wstring_to_utf8(passSeries);
        request["passport_number"] = wstring_to_utf8(passNumber);
        request["phone"] = wstring_to_utf8(cleanPhone);
        request["address"] = wstring_to_utf8(address);

        std::wstring authToken = g_authManager.getAuthToken();
        std::thread([this, request, authToken]() {
            auto response = g_httpsClient.post(L"/api/v1/clients/register_committee", request, authToken);
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                int clientId = (*response)["client_id"].get<int>();
                PostMessageW(m_hWnd, WM_COMMIT_SAVED, (WPARAM)clientId, 0);
            }
            else {
                PostMessageW(m_hWnd, WM_COMMIT_SAVED, (WPARAM)0, 0);
            }
            }).detach();
    }

    void onCommitCancel() {
        SetWindowTextW(m_hCommitLastNameEdit, L"");
        SetWindowTextW(m_hCommitFirstNameEdit, L"");
        SetWindowTextW(m_hCommitMiddleNameEdit, L"");
        SetWindowTextW(m_hCommitBirthDateEdit, L"");
        SetWindowTextW(m_hCommitPassSeriesEdit, L"");
        SetWindowTextW(m_hCommitPassNumberEdit, L"");
        SetWindowTextW(m_hCommitPhoneEdit, L"");
        SetWindowTextW(m_hCommitAddressEdit, L"");
        SendMessageW(m_hCommitPassTypeCombo, CB_SETCURSEL, 0, 0);
        g_logger.info(L"onCommitCancel: committee data fields cleared");
    }

    void onItemSave() {
        if (m_isSaving) return;
        if (m_tempItems.empty()) { MessageBoxW(m_hWnd, L"Нет позиций для сохранения", L"Внимание", MB_OK); return; }
        if (m_currentClientId <= 0) { MessageBoxW(m_hWnd, L"Ошибка: клиент не выбран.", L"Ошибка", MB_OK | MB_ICONERROR); return; }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) { MessageBoxW(m_hWnd, L"Ошибка авторизации.", L"Ошибка", MB_OK); return; }

        m_isSaving = true; EnableWindow(m_hItemSaveBtn, FALSE);
        json request; request["client_id"] = m_currentClientId; request["items"] = m_tempItems;
        int workerId = g_authManager.getClientId(); request["worker_id"] = workerId;

        std::thread([this, request, authToken]() {
            auto response = g_httpsClient.post(L"/api/v1/items/batch", request, authToken);
            struct ItemSaveResult { bool success; std::wstring errorMsg; long long appendixNumber; };
            ItemSaveResult res{ false, L"Ошибка сохранения.", 0 };

            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                res.success = true;
                if (response->contains("appendix_number") && (*response)["appendix_number"].is_number())
                    res.appendixNumber = (*response)["appendix_number"].get<long long>();
            }
            else if (response && response->contains("error")) {
                res.errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }

            using ResultPtr = ItemSaveResult*;
            ResultPtr* p = new ResultPtr(new ItemSaveResult(res));
            PostMessageW(m_hWnd, WM_APP + 3, 0, (LPARAM)p);
            }).detach();
    }

    void onItemCancel() {
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");
        m_tempItems.clear(); updateTempItemsList(); updateNextItemNumber();
        m_editingItemIndex = -1;
    }

    ReceiptSnapshot fetchLatestAppendixFromServer(int clientId) {
        ReceiptSnapshot snap;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) return snap;
        auto resp = g_httpsClient.get(L"/api/v1/appendix/latest?client_id=" + std::to_wstring(clientId), authToken);
        if (!resp || resp->contains("error")) return snap;

        snap.valid = true; snap.clientId = clientId;
        snap.appendixNumber = resp->value("appendix_number", 0LL);
        snap.totalQty = resp->value("total_quantity", 0);
        snap.totalValue = resp->value("total_value", 0.0);
        snap.totalClientAmount = resp->value("total_client_amount", 0.0);

        if (resp->contains("items") && (*resp)["items"].is_array()) {
            for (const auto& it : (*resp)["items"]) {
                json j;
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
        ReceiptSnapshot snap;
        { std::lock_guard<std::mutex> lock(m_receiptSnapshotMutex); snap = m_receiptSnapshot; }
        if (!snap.valid || snap.clientId != clientId) snap = fetchLatestAppendixFromServer(clientId);
        if (!snap.valid) return;

        HWND hWndCopy = m_hWnd;
        std::thread([hWndCopy, snap]() {
            ReceiptData data;
            data.appendixNumber = snap.appendixNumber; data.clientId = snap.clientId;
            data.totalQty = snap.totalQty; data.totalValue = snap.totalValue; data.totalClientAmount = snap.totalClientAmount;

            for (const auto& it : snap.items) {
                ReceiptItem ri; ri.description = utf8_to_wstring(it.value("description", ""));
                ri.characteristic = utf8_to_wstring(it.value("condition", "")); ri.quantity = it.value("quantity", 1);
                ri.price = it.value("estimated_price", 0.0); ri.clientAmount = it.value("client_amount", 0.0);
                ri.note = utf8_to_wstring(it.value("note", ""));
                data.items.push_back(ri);
            }

            auto resp = g_httpsClient.get(L"/api/v1/clients/by_id?id=" + std::to_wstring(snap.clientId), L"");
            if (resp && resp->contains("name")) data.clientFullName = utf8_to_wstring((*resp)["name"].get<std::string>());
            if (data.clientFullName.empty()) data.clientFullName = L"Клиент #" + std::to_wstring(snap.clientId);

            ReceiptData* pCopy = new ReceiptData(data);
            PostMessageW(hWndCopy, WM_RECEIPT_PRINT, 0, (LPARAM)pCopy);
            }).detach();
    }

    void returnToQueueList() {
        g_logger.info(L"returnToQueueList: switching to QUEUE_LIST mode");
        m_currentMode = Mode::QUEUE_LIST;
        showItemInputControls(false);
        scrollRightPanel(0);
        ShowScrollBar(m_hWnd, SB_VERT, FALSE);
        PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    }

    void showItemInputForm(int clientId, const std::wstring& ticketNumber, const std::wstring& windowNumber, const std::wstring& clientName) {
        m_currentClientId = clientId; m_currentTicketNumber = ticketNumber;
        m_currentWindowNumber = windowNumber; m_currentClientName = clientName;
        m_tempItems.clear(); m_isSaving = false; m_currentMode = Mode::ITEM_INPUT;
        m_editingItemIndex = -1;

        repositionItemInputControls();

        if (m_currentQueueType != L"first_time" && clientId > 0) {
            g_logger.info(L"showItemInputForm: loading client data from server for clientId=" + std::to_wstring(clientId));
            loadClientDataFromServer(clientId);
        }

        std::thread([this, clientId]() {
            loadClientItems(clientId);
            PostMessageW(m_hWnd, WM_APP + 2, 0, 0);
            }).detach();

        setCurrentDateTime(); showItemInputControls(true);
        EnableWindow(m_hAcceptBtn, FALSE); EnableWindow(m_hServeBtn, FALSE);
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: onAccept()
    // =========================================================================
    // ПРИЧИНА ИСПРАВЛЕНИЯ:
    // 1. Для очередей general, extra_20, trust, paid, expensive серверный
    //    вызов accept НЕ выполнялся. Талон удалялся только из локального
    //    списка m_tickets, и сервер не знал о принятии. ТВ-монитор не мог
    //    отобразить эти талоны как "приглашённые к окну".
    //
    // 2. Номер окна был захардкожен как "1" для всех очередей.
    //    Не осуществлялась привязка окна к конкретному товароведу.
    //
    // 3. Для очереди trust серверный вызов acceptTrustTicket() существовал
    //    на сервере, но никогда не вызывался из клиента, потому что trust
    //    обрабатывался в первой ветке if (без серверного вызова).
    //
    // РЕШЕНИЕ:
    // 1. Для ВСЕХ 6 типов очередей выполняется серверный вызов accept.
    // 2. Номер окна определяется по телефону товароведа через
    //    getWindowNumberForCurrentWorker().
    // 3. Номер окна передаётся в запросе accept и обновляется на сервере.
    // 4. Голосовое приглашение произносит реальный номер окна товароведа.
    //
    // ЛОГИКА НЕ МЕНЯЕТСЯ:
    // - Математика очередей (позиция, время ожидания) не затрагивается.
    // - Бизнес-модель (комиссии, проценты) не затрагивается.
    // - Поведение при ошибке сервера: показывается сообщение об ошибке.
    // =========================================================================
    void onAccept() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK);
            return;
        }
        m_selectedIndex = sel;

        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
            MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK);
            return;
        }

        // Извлекаем данные талона из локального списка (потокобезопасно)
        std::string ticketNumberUtf8;
        int clientId = -1;
        json acceptedTicket;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (itemData >= (LRESULT)m_tickets.size()) {
                MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK);
                return;
            }

            const json& ticket = m_tickets[itemData];

            acceptedTicket = ticket;
            ticketNumberUtf8 = ticket.value("ticket_number", std::string(""));

            if (ticket.contains("client_id") && ticket["client_id"].is_number()) {
                clientId = ticket["client_id"].get<int>();
            }
        }

        if (ticketNumberUtf8.empty()) {
            MessageBoxW(m_hWnd, L"Ошибка идентификации талона", L"Ошибка", MB_OK);
            return;
        }
        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        g_logger.info(L"onAccept: ticketNumber=" + ticketNumber +
            L", queueType=" + m_currentQueueType +
            L", clientId=" + std::to_wstring(clientId));

        // =====================================================================
        // ЗАПОМИНАЕМ ПРИНЯТЫЙ ТАЛОН.
        // ОН НЕ ДОЛЖЕН УДАЛЯТЬСЯ ИЗ ОЧЕРЕДИ СРАЗУ ПОСЛЕ «Принять».
        // =====================================================================
        setCurrentAcceptedTicket(acceptedTicket);

        // =====================================================================
        // ОПРЕДЕЛЯЕМ НОМЕР ОКНА ТОВАРОВЕДА ПО ЕГО ТЕЛЕФОНУ
        // =====================================================================
        std::wstring workerWindowNumber = getWindowNumberForCurrentWorker();
        g_logger.info(L"onAccept: worker window number determined = " + workerWindowNumber);

        // =====================================================================
        // ОПРЕДЕЛЯЕМ ЭНДПОИНТ ДЛЯ СЕРВЕРНОГО ВЫЗОВА
        // =====================================================================
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") {
            endpoint = L"/api/v1/queue/first_time/accept";
        }
        else if (m_currentQueueType == L"trust") {
            endpoint = L"/api/v1/queue/trust/accept";
        }
        else {
            // general, extra_20, paid, expensive
            endpoint = L"/api/v1/queue/accept";
        }

        g_logger.info(L"onAccept: using endpoint=" + endpoint);

        // =====================================================================
        // ФОРМИРУЕМ ЗАПРОС С НОМЕРОМ ОКНА ТОВАРОВЕДА
        // =====================================================================
        json request;
        request["ticket_number"] = ticketNumberUtf8;
        request["window_number"] = wstring_to_utf8(workerWindowNumber);

        g_logger.info(L"onAccept: sending accept request, ticket=" + ticketNumber +
            L", window=" + workerWindowNumber);

        // =====================================================================
        // ВЫПОЛНЯЕМ СЕРВЕРНЫЙ ВЫЗОВ (синхронно, как для first_time)
        // =====================================================================
        auto response = g_httpsClient.post(endpoint, request, L"");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            // Извлекаем номер окна из ответа сервера
            // (сервер возвращает обновлённый window_number)
            std::wstring windowW = utf8_to_wstring(
                response->value("window_number", wstring_to_utf8(workerWindowNumber)));

            g_logger.info(L"onAccept: server confirmed acceptance, ticket=" + ticketNumber +
                L", window=" + windowW);

            // =====================================================================
            // ФИКСИРУЕМ МОМЕНТ ПРИНЯТИЯ/ВЫЗОВА ТАЛОНА К ОКНУ
            // =====================================================================
            auto nowAccept = std::chrono::system_clock::now();
            m_currentTicketAcceptedAt =
                std::chrono::duration_cast<std::chrono::seconds>(
                    nowAccept.time_since_epoch()
                ).count();

            g_logger.info(L"onAccept: accepted timestamp stored, ticket=" + ticketNumber +
                L", acceptedAt=" + std::to_wstring(m_currentTicketAcceptedAt));

            // =====================================================================
            // ГОЛОСОВОЕ ПРИГЛАШЕНИЕ С РЕАЛЬНЫМ НОМЕРОМ ОКНА ТОВАРОВЕДА
            // =====================================================================
            if (m_currentQueueType == L"first_time") {
                // Для first_time: произносим номер талона (client_id ещё нет)
                speak(ticketNumber, windowW);
            }
            else {
                // Для остальных очередей: произносим номер талона
                speak(ticketNumber, windowW);
            }

            // =====================================================================
            // ОТКРЫВАЕМ ФОРМУ ВВОДА ТОВАРОВ
            // =====================================================================
            if (m_currentQueueType == L"first_time") {
                m_isCommitDataSaved = false;
                showItemInputForm(0, ticketNumber, windowW, L"Новый комитент");
            }
            else {
                if (clientId <= 0) {
                    g_logger.error(L"onAccept: clientId is invalid for queue " +
                        m_currentQueueType + L", ticket=" + ticketNumber);
                    MessageBoxW(m_hWnd, L"Нет client_id", L"Ошибка", MB_OK);
                    return;
                }
                showItemInputForm(clientId, ticketNumber, windowW,
                    L"Клиент #" + std::to_wstring(clientId));
            }
                        
            updateUI();

            g_logger.info(L"onAccept: completed successfully for ticket=" + ticketNumber);
        }
        else {
            // =====================================================================
            // ОБРАБОТКА ОШИБКИ СЕРВЕРА
            // =====================================================================
            std::wstring err = L"Ошибка принятия талона.";
            if (response && response->contains("error")) {
                err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            g_logger.error(L"onAccept: FAILED for ticket=" + ticketNumber +
                L", error=" + err);
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
        }
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: onServe()
    // =========================================================================
    // ПРИЧИНА ИСПРАВЛЕНИЯ:
    // Метод брал ВЫБРАННЫЙ талон из списка ожидающих (m_hListBox), а не
    // ТЕКУЩИЙ ПРИНЯТЫЙ талон (m_currentTicketNumber). В результате:
    //   1. На сервер отправлялся запрос /api/v1/queue/serve с номером
    //      СЛЕДУЮЩЕГО ожидающего талона (например, D002), а не текущего
    //      принятого (D001).
    //   2. Сервер помечал ожидающий талон как «обслуженный», и тот
    //      исчезал из очереди, не будучи принятым.
    //   3. Талон не попадал в статус 'accepted', поэтому ТВ-монитор
    //      (QueueDisplayWindow) не озвучивал его.
    //   4. Текущий принятый талон (D001) оставался в статусе 'accepted'
    //      навсегда и не обслуживался.
    //
    // РЕШЕНИЕ:
    // 1. Использовать m_currentTicketNumber (текущий принятый талон)
    //    вместо выбранного талона из списка.
    // 2. Если m_currentTicketNumber пуст (талон уже обслужен автоматически
    //    для first_time, или товаровед ещё никого не принимал, или вернулся
    //    через «Назад» без сохранения товаров) — пропустить обслуживание
    //    и сразу принять следующий талон из очереди.
    // 3. После успешного обслуживания очистить m_currentTicketNumber и
    //    m_currentClientId, чтобы при повторном нажатии не обслуживать
    //    тот же талон повторно.
    // 4. Автоматически принять следующий талон через onAccept(), который
    //    выполнит серверный вызов /api/v1/queue/accept, озвучивание
    //    номера талона через TTS и открытие формы ввода товаров.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    // Метод вызывается ТОЛЬКО из UI-потока (обработчик WM_COMMAND,
    // идентификатор кнопки 2). Доступ к m_tickets защищён мьютексом
    // m_mutex. Поля m_currentTicketNumber и m_currentClientId читаются
    // и записываются только в UI-потоке. Метод fetchTicketsFromServer()
    // вызывается синхронно в UI-потоке, что блокирует интерфейс на время
    // запроса, но это приемлемо, так как пользователь нажал кнопку и ждёт
    // результата. Гонка потоков исключена.
    // =========================================================================
    void onServe() {
        if (m_currentMode == Mode::ITEM_INPUT) return;

        // =====================================================================
        // ШАГ 1: Определяем талон для обслуживания.
        // Используем m_currentTicketNumber — номер ТЕКУЩЕГО ПРИНЯТОГО
        // талона, который товаровед только что ввёл и сохранил.
        // Если он пуст — талон уже обслужен (например, автоматически
        // для first_time), или товаровед ещё никого не принимал, или
        // вернулся через «Назад» без сохранения товаров.
        // =====================================================================
        std::wstring ticketNumber = m_currentTicketNumber;
        int clientId = m_currentClientId;
        bool needServe = !ticketNumber.empty();

        g_logger.info(L"onServe: entered, currentTicketNumber='" + ticketNumber +
            L"', clientId=" + std::to_wstring(clientId) +
            L", queueType=" + m_currentQueueType +
            L", needServe=" + std::wstring(needServe ? L"true" : L"false"));

        if (needServe) {
            // =================================================================
            // ШАГ 2: Обслуживаем ТЕКУЩИЙ ПРИНЯТЫЙ талон.
            // Определяем серверный эндпоинт в зависимости от типа очереди.
            // =================================================================
            std::string ticketNumberUtf8 = wstring_to_utf8(ticketNumber);

            std::wstring endpoint;
            if (m_currentQueueType == L"first_time")
                endpoint = L"/api/v1/queue/first_time/serve";
            else if (m_currentQueueType == L"trust")
                endpoint = L"/api/v1/queue/trust/serve";
            else
                endpoint = L"/api/v1/queue/serve";

            json request;
            request["ticket_number"] = ticketNumberUtf8;
            g_logger.info(L"onServe: sending serve request for CURRENT ACCEPTED ticket=" +
                ticketNumber + L", endpoint=" + endpoint);

            auto response = g_httpsClient.post(endpoint, request, L"");
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                g_logger.info(L"onServe: server confirmed serve for ticket=" + ticketNumber);

                // =============================================================
                // Печать чека и ценников для обслуженного клиента.
                // Метод запускает фоновый поток, который формирует данные
                // и отправляет WM_RECEIPT_PRINT в UI-поток.
                // =============================================================
                printReceiptForServedClient(clientId);

                m_currentTicketNumber.clear();
                m_currentClientId = 0;
                m_currentTicketAcceptedAt = 0;

                removeCurrentAcceptedTicketFromLocalList();

                g_logger.info(L"onServe: current ticket data cleared after successful serve");
            }
            else {
                // =============================================================
                // Обработка ошибки сервера. При ошибке обслуживания НЕ
                // принимаем следующий талон, чтобы не потерять контекст.
                // =============================================================
                std::wstring err = L"Ошибка обслуживания талона.";
                if (response && response->contains("error")) {
                    err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
                }
                g_logger.error(L"onServe: FAILED for ticket=" + ticketNumber +
                    L", error=" + err);
                MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
                return;
            }
        }
        else {
            // =================================================================
            // m_currentTicketNumber пуст. Это означает одно из:
            //   а) Талон уже обслужен автоматически (для first_time).
            //   б) Товаровед ещё никого не принимал.
            //   в) Товаровед вернулся через «Назад» без сохранения товаров.
            // В любом из этих случаев пропускаем обслуживание и сразу
            // переходим к принятию следующего талона.
            // =================================================================
            g_logger.info(L"onServe: no current accepted ticket, "
                L"proceeding directly to accept next ticket");
        }

        // =====================================================================
        // ШАГ 3: Синхронно загружаем актуальный список ожидающих талонов.
        // Это исключает проблему устаревшего m_tickets, который обновляется
        // фоновым потоком каждые 2 секунды.
        // =====================================================================
        g_logger.info(L"onServe: fetching fresh waiting tickets for queue=" +
            m_currentQueueType);
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
        g_logger.info(L"onServe: fresh tickets loaded, count=" +
            std::to_wstring(ticketCount));

        // =====================================================================
        // ШАГ 4: Автоматически принимаем следующий талон из очереди.
        // Вызываем onAccept(), который выполнит:
        //   1. Серверный вызов /api/v1/queue/accept (перевод талона
        //      из 'waiting' в 'accepted').
        //   2. Озвучивание номера талона через TTS (в клиенте и на
        //      ТВ-мониторе через QueueDisplayWindow).
        //   3. Открытие формы ввода товаров для нового комитента.
        // =====================================================================
        if (ticketCount > 0) {
            g_logger.info(L"onServe: next ticket available, auto-accepting");
            SendMessageW(m_hListBox, LB_SETCURSEL, 0, 0);
            m_selectedIndex = 0;
            onAccept();
        }
        else {
            g_logger.info(L"onServe: no more waiting tickets in queue=" +
                m_currentQueueType + L", staying in QUEUE_LIST mode");
        }
    }

    // =========================================================================
// НОВЫЙ МЕТОД: УДАЛЕНИЕ ТАЛОНА, ПО КОТОРОМУ КОМИТЕНТ НЕ ПРИШЁЛ
// =========================================================================
//
// НАЗНАЧЕНИЕ:
// Позволяет товароведу удалить из любой очереди талон, по которому
// комитент не подошёл к окну в течение 2 минут.
//
// ЛОГИКА РАБОТЫ:
// 1. Проверяется, что выбран талон в списке (m_hListBox).
// 2. Из локального списка извлекаются
//    данные талона (номер, время создания, ).
// 3. На клиенте выполняется предварительная проверка: с момента
//    создания талона должно пройти не менее 120 секунд (2 минуты).
//    Это предотвращает отправку заведомо неверных запросов на сервер
//    и улучшает пользовательский опыт.
// 4. Отправляется запрос POST /api/v1/queue/delete_ticket на сервер.
// 5. Сервер выполняет атомарный с проверкой статуса и возраста.
// 6. При успешном удалении талон удаляется из локального списка
//    и обновляется.
// 7. При ошибке сервера показывается сообщение об ошибке.
//
// ПОТОКОБЕЗОПАСНОСТЬ:
// Метод вызывается ТОЛЬКО из - потока (обработчик, идентификатор кнопки 4).
// Доступ к защищён мьютексом.
// Поля и читаются только в -потоке.
// Сетевой запрос выполняется синхронно (как в и),
// что блокирует интерфейс на время запроса, но это приемлемо,
// так как пользователь нажал кнопку и ждёт результата.
// Гонка потоков исключена.
// =========================================================================
    void onDeleteTicket() {
        g_logger.info(L"onDeleteTicket: entered, currentQueueType=" + m_currentQueueType);

        std::string ticketNumberUtf8;
        int clientId = -1;
        int64_t baseTime = 0;
        LRESULT itemData = LB_ERR;
        bool deleteCurrentAccepted = false;

        // =====================================================================
        // ЕСЛИ ЕСТЬ ТЕКУЩИЙ ПРИНЯТЫЙ ТАЛОН — УДАЛЯЕМ ЕГО
        // =====================================================================
        if (hasCurrentAcceptedTicket() && !m_currentTicketNumber.empty()) {
            deleteCurrentAccepted = true;

            ticketNumberUtf8 = wstring_to_utf8(m_currentTicketNumber);
            clientId = m_currentClientId;
            baseTime = m_currentTicketAcceptedAt;

            g_logger.info(L"onDeleteTicket: deleting CURRENT ACCEPTED ticket=" +
                m_currentTicketNumber +
                L", acceptedAt=" + std::to_wstring(baseTime));
        }
        // =====================================================================
        // ИНАЧЕ РАБОТАЕМ С ВЫБРАННЫМ ТАЛОНОМ ИЗ СПИСКА
        // =====================================================================
        else {
            int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) {
                MessageBoxW(m_hWnd, L"Выберите талон для удаления",
                    L"Внимание", MB_OK);
                g_logger.warning(L"onDeleteTicket: no ticket selected");
                return;
            }

            itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
            if (itemData == LB_ERR || itemData < 0 ||
                itemData >= (LRESULT)m_tickets.size()) {
                MessageBoxW(m_hWnd, L"Ошибка идентификации талона",
                    L"Ошибка", MB_OK);
                g_logger.error(L"onDeleteTicket: invalid itemData=" +
                    std::to_wstring(itemData));
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            if (itemData >= (LRESULT)m_tickets.size()) {
                g_logger.error(L"onDeleteTicket: itemData out of range after lock");
                return;
            }

            const json& ticket = m_tickets[itemData];

            ticketNumberUtf8 = ticket.value("ticket_number", std::string(""));

            if (ticket.contains("client_id") && ticket["client_id"].is_number()) {
                clientId = ticket["client_id"].get<int>();
            }

            baseTime = ticket.value("created_at", (int64_t)0);
        }

        if (ticketNumberUtf8.empty()) {
            MessageBoxW(m_hWnd, L"Не определён номер талона для удаления",
                L"Ошибка", MB_OK);
            g_logger.error(L"onDeleteTicket: empty ticket number");
            return;
        }

        if (baseTime <= 0) {
            MessageBoxW(m_hWnd, L"Не определено время талона",
                L"Ошибка", MB_OK);
            g_logger.error(L"onDeleteTicket: invalid baseTime");
            return;
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        g_logger.info(L"onDeleteTicket: ticketNumber=" + ticketNumber +
            L", queueType=" + m_currentQueueType +
            L", clientId=" + std::to_wstring(clientId) +
            L", baseTime=" + std::to_wstring(baseTime));

        // =================================================================
        // ШАГ 3: Предварительная проверка на клиенте (>= 2 минуты)
        // =================================================================
        // Вычисляем текущее время в секундах ( )
        auto now = std::chrono::system_clock::now();
        int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        int64_t ageSeconds = nowSec - baseTime;

        g_logger.info(L"onDeleteTicket: ticket age=" +
            std::to_wstring(ageSeconds) + L" seconds (threshold=120)");

        if (ageSeconds < 120) {
            int remainingSeconds = 120 - static_cast<int>(ageSeconds);
            std::wstring msg = L"Талон можно удалить только после 2 минут ожидания.\n"
                L"Осталось: " + std::to_wstring(remainingSeconds) + L" сек.";
            MessageBoxW(m_hWnd, msg.c_str(), L"Внимание", MB_OK);
            g_logger.info(L"onDeleteTicket: ticket " + ticketNumber +
                L" is too young (age=" + std::to_wstring(ageSeconds) +
                L"s < 120s), deletion rejected on client side");
            return;
        }

        // =================================================================
        // ШАГ 4: Подтверждение удаления
        // =================================================================
        std::wstring confirmMsg = L"Удалить талон " + ticketNumber +
            L" из очереди «" + m_currentQueueType + L"»?\n\n"
            L"Комитент не подошёл к окну в течение 2 минут.";
        int confirmResult = MessageBoxW(m_hWnd, confirmMsg.c_str(),
            L"Подтверждение удаления", MB_YESNO | MB_ICONQUESTION);
        if (confirmResult != IDYES) {
            g_logger.info(L"onDeleteTicket: deletion cancelled by user for ticket " +
                ticketNumber);
            return;
        }

        // =================================================================
        // ШАГ 5: Отправляем запрос на сервер
        // =================================================================
        json request;
        request["ticket_number"] = ticketNumberUtf8;
        request["queue_type"] = wstring_to_utf8(m_currentQueueType);

        g_logger.info(L"onDeleteTicket: sending delete request to server, "
            L"ticket=" + ticketNumber +
            L", queueType=" + m_currentQueueType);

        std::wstring authToken = g_authManager.getAuthToken();
        auto response = g_httpsClient.post(
            L"/api/v1/queue/delete_ticket", request, authToken);

        // =================================================================
        // ШАГ 6: Обрабатываем ответ сервера
        // =================================================================
        if (response && response->contains("success") &&
            (*response)["success"].get<bool>()) {

            g_logger.info(L"onDeleteTicket: server confirmed deletion of ticket " +
                ticketNumber + L" from queue " + m_currentQueueType);

            // =================================================================
            // ЕСЛИ УДАЛЯЛИ ТЕКУЩИЙ ПРИНЯТЫЙ ТАЛОН
            // =================================================================
            if (deleteCurrentAccepted) {
                removeCurrentAcceptedTicketFromLocalList();

                m_currentTicketNumber.clear();
                m_currentClientId = 0;
                m_currentTicketAcceptedAt = 0;

                g_logger.info(L"onDeleteTicket: current accepted ticket data cleared");

                returnToQueueList();
            }
            // =================================================================
            // ЕСЛИ УДАЛЯЛИ ТАЛОН ИЗ СПИСКА ОЖИДАНИЯ
            // =================================================================
            else {
                std::lock_guard<std::mutex> lock(m_mutex);

                if (itemData != LB_ERR && itemData < (LRESULT)m_tickets.size()) {
                    m_tickets.erase(m_tickets.begin() + itemData);

                    g_logger.info(L"onDeleteTicket: ticket " + ticketNumber +
                        L" removed from local list, remaining tickets=" +
                        std::to_wstring(m_tickets.size()));
                }

                if (m_selectedIndex >= (int)m_tickets.size()) {
                    m_selectedIndex = (int)m_tickets.size() - 1;
                }

                updateUI();
            }

            MessageBoxW(m_hWnd,
                (L"Талон " + ticketNumber + L" удалён из очереди.").c_str(),
                L"Успех", MB_OK);
        }
    }

    // =========================================================================
// НОВЫЙ МЕТОД: ОТКРЫТИЕ ОКНА «ПОИСК КОМИТЕНТА»
// =========================================================================
//
// НАЗНАЧЕНИЕ:
// Открывает модальное окно для поиска комитента по
// или по фамилии, имени и отчеству.
//
// Реализация аналогична вызову в
// ( там используется для ввода причины блокировки).
//
// ПОТОКОБЕЗОПАСНОСТЬ:
// Метод вызывается ТОЛЬКО из - потока (обработчик, идентификатор кнопки 5).
// Модальное окно блокирует родительское окно до закрытия.
// Сетевые запросы внутри диалога выполняются в фоновых потоках.
// =========================================================================
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
            // =====================================================================
            // ЗАПУСК ФОНОВОГО ПОТОКА ОБНОВЛЕНИЯ СПИСКА ТАЛОНОВ
            // =====================================================================
            // Поток работает только пока окно живо и флаг m_running == true.
            //
            // Требования потокобезопасности:
            // 1. Флаг читается через atomic.
            // 2. Спать короткими интервалами, чтобы быстро остановиться при закрытии.
            // 3. Обернуть рабочую часть в try/catch, чтобы любое исключение
            //    не приводило к немедленному std::terminate().
            // 4. Никогда не выходить из потока с не-joined состоянием объекта.
            // =====================================================================
            pThis->m_running.store(true);

            pThis->m_refreshThread = std::thread([pThis]() {
                g_logger.info(L"WorkerWindow: refresh thread started");

                while (pThis->m_running.load()) {
                    // =================================================================
                    // Спим суммарно REFRESH_INTERVAL_MS, но короткими кусками по 100 мс.
                    // Это нужно, чтобы при закрытии окна не ждать полный интервал,
                    // а быстро увидеть, что флаг уже сброшен.
                    // =================================================================
                    for (int waitedMs = 0;
                        waitedMs < REFRESH_INTERVAL_MS && pThis->m_running.load();
                        waitedMs += 100) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }

                    if (!pThis->m_running.load()) {
                        break;
                    }

                    try {
                        pThis->refreshList();
                    }
                    catch (const std::exception& ex) {
                        g_logger.error(L"WorkerWindow: refresh thread exception: " +
                            utf8_to_wstring(ex.what()));
                    }
                    catch (...) {
                        g_logger.error(L"WorkerWindow: refresh thread unknown exception");
                    }
                }

                g_logger.info(L"WorkerWindow: refresh thread stopped");
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
                RECT rc;
                GetClientRect(hWnd, &rc);
                int width = rc.right - rc.left, height = rc.bottom - rc.top;
                int leftListWidth = (width * 20) / 100;
                if (leftListWidth < 180) leftListWidth = 180;
                SetWindowPos(pThis->m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
                SetWindowPos(pThis->m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);
                int btnW = (leftListWidth - 40 - 10) / 2;
                SetWindowPos(pThis->m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hSearchClientBtn, NULL,
                    width - 320, 10, 180, 40, SWP_NOZORDER);
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
            case SB_LINEUP:        pos -= 35; break;
            case SB_LINEDOWN:      pos += 35; break;
            case SB_PAGEUP:        pos -= clientH; break;
            case SB_PAGEDOWN:      pos += clientH; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = (short)HIWORD(wParam); break;
            case SB_TOP:           pos = 0; break;
            case SB_BOTTOM:        pos = maxScroll; break;
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
            return 0;
        case WM_APP + 3: {
            using ItemSaveResult = struct {
                bool success;
                std::wstring errorMsg;
                long long appendixNumber;
            };
            ItemSaveResult* res = *reinterpret_cast<ItemSaveResult**>(lParam);
            pThis->m_isSaving = false;
            EnableWindow(pThis->m_hItemSaveBtn, TRUE);

            if (res->success) {
                {
                    std::lock_guard<std::mutex> lock(pThis->m_receiptSnapshotMutex);
                    pThis->m_receiptSnapshot = ReceiptSnapshot{};
                    pThis->m_receiptSnapshot.valid = true;
                    pThis->m_receiptSnapshot.clientId = pThis->m_currentClientId;
                    pThis->m_receiptSnapshot.appendixNumber = res->appendixNumber;
                    pThis->m_receiptSnapshot.items = pThis->m_tempItems;

                    int qty = 0;
                    double value = 0.0, clientSum = 0.0;
                    for (const auto& it : pThis->m_tempItems) {
                        qty += it.value("quantity", 1);
                        value += it.value("estimated_price", 0.0) * it.value("quantity", 1);
                        clientSum += it.value("client_amount", 0.0);
                    }
                    pThis->m_receiptSnapshot.totalQty = qty;
                    pThis->m_receiptSnapshot.totalValue = value;
                    pThis->m_receiptSnapshot.totalClientAmount = clientSum;
                }
                g_logger.info(L"WM_APP+3: items saved successfully, appendixNumber=" + std::to_wstring(res->appendixNumber) +
                    L", clientId=" + std::to_wstring(pThis->m_currentClientId) +
                    L", queueType=" + pThis->m_currentQueueType);

                // =============================================================
                // ПОСЛЕ УСПЕШНОГО «Сохранить все» ПРИНЯТЫЙ ТАЛОН ДОЛЖЕН
                // БЫТЬ УДАЛЁН ИЗ ОЧЕРЕДИ.
                //
                // Для серверного завершения талона используется существующий
                // эндпоинт serve. Ранее он вызывался только для first_time.
                // Теперь вызывается для любой очереди после сохранения товаров.
                // =============================================================
                std::wstring savedTicketNumber = pThis->m_currentTicketNumber;
                int savedClientId = pThis->m_currentClientId;
                std::wstring savedQueueType = pThis->m_currentQueueType;

                if (!savedTicketNumber.empty()) {
                    std::thread([pThis, savedTicketNumber, savedClientId, savedQueueType]() {
                        std::wstring endpoint;

                        if (savedQueueType == L"first_time") {
                            endpoint = L"/api/v1/queue/first_time/serve";
                        }
                        else if (savedQueueType == L"trust") {
                            endpoint = L"/api/v1/queue/trust/serve";
                        }
                        else {
                            endpoint = L"/api/v1/queue/serve";
                        }

                        json request;
                        request["ticket_number"] = wstring_to_utf8(savedTicketNumber);

                        auto response = g_httpsClient.post(endpoint, request, L"");

                        if (response && response->contains("success") &&
                            (*response)["success"].get<bool>()) {
                            g_logger.info(L"WM_APP+3: ticket served after Save all, ticket=" +
                                savedTicketNumber);

                            pThis->printReceiptForServedClient(savedClientId);
                        }
                        else {
                            g_logger.error(L"WM_APP+3: failed to serve ticket after Save all, ticket=" +
                                savedTicketNumber);
                        }
                        }).detach();
                }

                // =============================================================
                // УДАЛЯЕМ ТАЛОН ИЗ ЛОКАЛЬНОГО СПИСКА И ОЧИЩАЕМ ЕГО ДАННЫЕ
                // =============================================================
                pThis->removeCurrentAcceptedTicketFromLocalList();

                pThis->m_currentTicketNumber.clear();
                pThis->m_currentClientId = 0;
                pThis->m_currentTicketAcceptedAt = 0;

                g_logger.info(L"WM_APP+3: current accepted ticket cleared after Save all");

                MessageBoxW(pThis->m_hWnd, L"Товары успешно сохранены!", L"Успех", MB_OK);
                pThis->returnToQueueList();
            }
            else {
                MessageBoxW(pThis->m_hWnd, res->errorMsg.c_str(), L"Ошибка", MB_OK);
            }
            delete res; return 0;
        }
        case WM_COMMIT_SAVED: {
            int clientId = (int)wParam;
            if (clientId > 0) {
                pThis->m_currentClientId = clientId;
                pThis->m_isCommitDataSaved = true;
                g_logger.info(L"WM_COMMIT_SAVED: committee data saved, clientId=" + std::to_wstring(clientId));

                wchar_t fioBuf[256];
                GetWindowTextW(pThis->m_hCommitLastNameEdit, fioBuf, 256); std::wstring lastName = fioBuf;
                GetWindowTextW(pThis->m_hCommitFirstNameEdit, fioBuf, 256); std::wstring firstName = fioBuf;
                GetWindowTextW(pThis->m_hCommitMiddleNameEdit, fioBuf, 256); std::wstring middleName = fioBuf;
                GetWindowTextW(pThis->m_hCommitPhoneEdit, fioBuf, 256); std::wstring phone = fioBuf;

                std::wstring fio = lastName;
                if (!firstName.empty()) {
                    if (!fio.empty()) fio += L" ";
                    fio += firstName;
                }
                if (!middleName.empty()) {
                    if (!fio.empty()) fio += L" ";
                    fio += middleName;
                }
                g_logger.info(L"WM_COMMIT_SAVED: saved FIO='" + fio + L"', phone='" + phone + L"'");

                pThis->repositionItemInputControls();
                pThis->showItemInputControls(true);

                pThis->setCurrentDateTime();
                g_logger.info(L"WM_COMMIT_SAVED: [FIX] setCurrentDateTime() called after repositionItemInputControls()");

                // === Загружаем товары клиента и обновляем поле «Номер приложения» ===
                // Используем существующий паттерн из showItemInputForm():
                // фоновый поток вызывает loadClientItems(), затем отправляет
                // WM_APP + 2, обработчик которого вызывает updateNextItemNumber().
                HWND hWndCopy = pThis->m_hWnd;
                std::thread([pThis, clientId, hWndCopy]() {
                    g_logger.info(L"WM_COMMIT_SAVED: [FIX] background thread started for loadClientItems, clientId=" + std::to_wstring(clientId));
                    pThis->loadClientItems(clientId);
                    g_logger.info(L"WM_COMMIT_SAVED: [FIX] loadClientItems completed, posting WM_APP+2");
                    PostMessageW(hWndCopy, WM_APP + 2, 0, 0);
                    }).detach();
                g_logger.info(L"WM_COMMIT_SAVED: [FIX] background thread for loadClientItems launched successfully");

                SetWindowTextW(pThis->m_hCommitFioValue, fio.c_str());
                SetWindowTextW(pThis->m_hCommitIdValue, std::to_wstring(clientId).c_str());
                SetWindowTextW(pThis->m_hCommitPhoneDisplayValue, phone.c_str());

                ShowWindow(pThis->m_hCommitFioLabel, SW_SHOW);
                ShowWindow(pThis->m_hCommitFioValue, SW_SHOW);
                ShowWindow(pThis->m_hCommitIdLabel, SW_SHOW);
                ShowWindow(pThis->m_hCommitIdValue, SW_SHOW);
                ShowWindow(pThis->m_hCommitPhoneDisplayLabel, SW_SHOW);
                ShowWindow(pThis->m_hCommitPhoneDisplayValue, SW_SHOW);

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
                g_logger.info(L"WM_CLIENT_DATA_LOADED: data loaded for clientId=" + std::to_wstring(clientId));
                std::wstring fio, phone, birthDate;
                {
                    std::lock_guard<std::mutex> lock(pThis->m_clientDataMutex);
                    fio = pThis->m_loadedClientFio;
                    phone = pThis->m_loadedClientPhone;
                    birthDate = pThis->m_loadedClientBirthDate;
                }
                g_logger.info(L"WM_CLIENT_DATA_LOADED: fio='" + fio + L"', phone='" + phone + L"', birthDate='" + birthDate + L"'");

                if (pThis->m_hCommitFioValue) SetWindowTextW(pThis->m_hCommitFioValue, fio.c_str());
                if (pThis->m_hCommitIdValue) SetWindowTextW(pThis->m_hCommitIdValue, std::to_wstring(clientId).c_str());
                if (pThis->m_hCommitBirthDateDisplayValue) SetWindowTextW(pThis->m_hCommitBirthDateDisplayValue, birthDate.c_str());
                if (pThis->m_hCommitPhoneDisplayValue) SetWindowTextW(pThis->m_hCommitPhoneDisplayValue, phone.c_str());
                g_logger.info(L"WM_CLIENT_DATA_LOADED: UI updated successfully");
            }
            else {
                g_logger.error(L"WM_CLIENT_DATA_LOADED: failed to load client data");
                if (pThis->m_hCommitFioValue) SetWindowTextW(pThis->m_hCommitFioValue, L"Ошибка загрузки");
                if (pThis->m_hCommitBirthDateDisplayValue) SetWindowTextW(pThis->m_hCommitBirthDateDisplayValue, L"Ошибка загрузки");
                if (pThis->m_hCommitPhoneDisplayValue) SetWindowTextW(pThis->m_hCommitPhoneDisplayValue, L"Ошибка загрузки");
            }
            return 0;
        }
        case WM_RECEIPT_PRINT: {
            ReceiptData* pData = reinterpret_cast<ReceiptData*>(lParam);
            if (pData) {
                g_logger.info(L"WM_RECEIPT_PRINT: printing receipt, appendix=" + std::to_wstring(pData->appendixNumber));
                EnableWindow(pThis->m_hAcceptBtn, FALSE);
                EnableWindow(pThis->m_hServeBtn, FALSE);
                bool ok = ReceiptPrinter::print(*pData);
                const std::wstring workerFio = g_authManager.getFullName();
                const bool tagsOk = PriceTagPrinter::printForReceipt(*pData, workerFio);
                g_logger.info(L"WM_RECEIPT_PRINT: price tags result=" + std::wstring(tagsOk ? L"true" : L"false"));
                pThis->updateUI();
                if (!ok) {
                    MessageBoxW(pThis->m_hWnd, L"Ошибка печати чека", L"Ошибка", MB_OK);
                }
                delete pData;
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
                    MessageBoxW(pThis->m_hWnd,
                        L"Сначала завершите текущий талон: «Сохранить все» или «Удалить (не пришёл)».",
                        L"Внимание", MB_OK);
                    return 0;
                }

                if (pThis->m_currentMode == Mode::ITEM_INPUT) {
                    MessageBoxW(pThis->m_hWnd, L"Сначала завершите ввод товаров", L"Внимание", MB_OK);
                    return 0;
                }                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR && idx < (LRESULT)pThis->m_queueTypeIds.size()) {
                    std::lock_guard<std::mutex> lock(pThis->m_mutex);
                    pThis->m_currentQueueType = pThis->m_queueTypeIds[static_cast<int>(idx)];
                    pThis->m_selectedIndex = -1;
                }
                // =============================================================
                // ИСПРАВЛЕНИЕ: Очищаем данные текущего талона при
                // переключении очереди. Это предотвращает обслуживание
                // талона из другой очереди при нажатии кнопки
                // «Обслужен (Следующий)». Например, если товаровед принял
                // талон в очереди «Дорогой товар», сохранил товары,
                // вернулся в список и переключился на «Общую очередь»,
                // то при нажатии «Обслужен (Следующий)» не должен
                // обслуживаться талон из «Дорогого товара».
                // =============================================================
                pThis->m_currentTicketNumber.clear();
                pThis->m_currentClientId = 0;
                g_logger.info(L"Queue switched: current ticket data cleared");
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
            if (id == ID_ITEM_ADD_BTN && code == BN_CLICKED) {
                pThis->onItemAdd();
                return 0;
            }
            if (id == ID_ITEM_SAVE_BTN && code == BN_CLICKED) {
                pThis->onItemSave();
                return 0;
            }
            if (id == ID_ITEM_CANCEL_BTN && code == BN_CLICKED) {
                pThis->onItemCancel();
                return 0;
            }
            if (id == ID_ITEM_BACK_BTN && code == BN_CLICKED) {
                if (pThis->hasCurrentAcceptedTicket()) {
                    MessageBoxW(pThis->m_hWnd,
                        L"Нельзя вернуться до завершения талона.\n"
                        L"Нажмите «Сохранить все» или «Удалить (не пришёл)».",
                        L"Внимание", MB_OK);
                    return 0;
                }

                if (MessageBoxW(pThis->m_hWnd, L"Вернуться? Несохранённые данные будут потеряны.", L"Подтверждение", MB_YESNO) == IDYES) {
                    pThis->m_currentTicketNumber.clear();
                    pThis->m_currentClientId = 0;
                    pThis->m_currentTicketAcceptedAt = 0;

                    pThis->removeCurrentAcceptedTicketFromLocalList();

                    g_logger.info(L"Back button: current ticket data cleared (items not saved)");
                    pThis->returnToQueueList();
                }
                return 0;
            }
            if (id == ID_ITEM_EDIT_RECORD_BTN && code == BN_CLICKED) {
                pThis->onEditRecord();
                return 0;
            }
            if (id == ID_ITEM_CANCEL_INPUT_BTN && code == BN_CLICKED) {
                pThis->onCancelInput();
                return 0;
            }
            if (id == ID_COMMIT_SAVE_BTN && code == BN_CLICKED) {
                pThis->onCommitSave();
                return 0;
            }
            if (id == ID_COMMIT_CANCEL_BTN && code == BN_CLICKED) {
                pThis->onCommitCancel();
                return 0;
            }
            if (code == BN_CLICKED) {
                switch (id) {
                case 1:
                    pThis->onAccept();
                    break;
                case 2:
                    pThis->onServe();
                    break;
                case 3:
                    DestroyWindow(pThis->m_hWnd);
                    break;
                    // =============================================================
                    // НОВОЕ: Кнопка «Удалить (не пришёл)»
                    // Удаляет выбранный талон, по которому комитент не подошёл
                    // к окну в течение 2 минут.
                    // =============================================================
                case ID_DELETE_TICKET_BTN:
                    pThis->onDeleteTicket();
                    break;
                    // =============================================================
                    // НОВОЕ: Кнопка «Поиск комитента»
                    // Открывает модальное окно поиска комитента по
                    // или по фамилии, имени и отчеству.
                    // =============================================================
                case ID_SEARCH_CLIENT_BTN:
                    pThis->onSearchClient();
                    break;
                }
                return 0;
            }
            break;
        }
        
        case WM_DESTROY:
            // =====================================================================
            // КОРРЕКТНОЕ УНИЧТОЖЕНИЕ ОКНА ТОВАРОВЕДА
            // =====================================================================
            // Здесь обязательно:
            // 1. Остановить флаг фонового потока.
            // 2. Дождаться завершения фонового потока через join().
            // 3. Освободить TTS.
            // 4. Только после этого вызвать PostQuitMessage(0).
            //
            // Это исключает уничтожение объекта с живым потоком и предотвращает
            // вызов std::terminate() в деструкторе std::thread.
            // =====================================================================
            g_logger.info(L"WorkerWindow: WM_DESTROY received, stopping refresh thread");

            pThis->m_running.store(false);

            if (pThis->m_refreshThread.joinable()) {
                g_logger.info(L"WorkerWindow: WM_DESTROY is joining refresh thread");
                pThis->m_refreshThread.join();
                g_logger.info(L"WorkerWindow: WM_DESTROY refresh thread joined successfully");
            }
            if (pThis->m_pVoice) {
                pThis->m_pVoice->Release();
                pThis->m_pVoice = nullptr;
                g_logger.info(L"WorkerWindow: TTS voice released");
            }
            CoUninitialize();
            g_logger.info(L"WorkerWindow: COM uninitialized");

            PostQuitMessage(0);
            g_logger.info(L"WorkerWindow: WM_DESTROY completed");
            return 0;       
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left, height = rc.bottom - rc.top;

        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hSmallFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 20, 10, 200, 200, m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        struct QueueTypeEntry {
            const wchar_t* displayName;
            const wchar_t* id;
        };
        QueueTypeEntry entries[] = {
            { L"Общая очередь", L"general" },
            { L"В первый раз (оформление) договора", L"first_time" },
            { L"+20 позиций", L"extra_20" },
            { L"На доверии", L"trust" },
            { L"Платный прием", L"paid" },
            { L"Дорогой товар", L"expensive" }
        };

        m_queueTypeIds.clear();
        for (const auto& entry : entries) {
            int idx = (int)SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)entry.displayName);
            if (idx != CB_ERR) m_queueTypeIds.push_back(entry.id);
        }
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        if (!m_queueTypeIds.empty()) m_currentQueueType = m_queueTypeIds[0];
        else m_currentQueueType = L"general";

        int leftListWidth = (width * 25) / 100;
        if (leftListWidth < 200) leftListWidth = 200;

        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY, 20, 60, leftListWidth - 40, height - 190, m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, height - 120, leftListWidth - 40, 30, m_hWnd, (HMENU)101, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        int btnW = (leftListWidth - 40 - 10) / 2;
        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, height - 80, btnW, 50, m_hWnd, (HMENU)1, g_hInstance, nullptr);
        SendMessageW(m_hAcceptBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + btnW + 10, height - 80, btnW, 50, m_hWnd, (HMENU)2, g_hInstance, nullptr);
        SendMessageW(m_hServeBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        // =====================================================================
        // НОВАЯ КНОПКА: «Поиск комитента»
        // Размещается в верхнем правом углу, рядом с кнопкой «Закрыть».
        // Идентификатор = 5 ( 5).
        // =====================================================================
        m_hSearchClientBtn = CreateWindowExW(0, L"BUTTON",
            L"Поиск комитента",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            width - 320, 10, 180, 40,
            m_hWnd, (HMENU)ID_SEARCH_CLIENT_BTN, g_hInstance, nullptr);
        SendMessageW(m_hSearchClientBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            width - 120, 10, 100, 40,
            m_hWnd, (HMENU)3, g_hInstance, nullptr);
        SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        int rightStart = leftListWidth + 20, rightWidth = width - rightStart - 20; if (rightWidth < 400) rightWidth = 400;
        createItemInputControls(rightStart, 80, rightWidth);
        showItemInputControls(true);
        setCurrentDateTime();

        SetWindowTextW(m_hItemNumberEdit, L"");
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"");
        SetWindowTextW(m_hItemConditionEdit, L"состояние износа 30%");
        SetWindowTextW(m_hItemNoteEdit, L"");

        m_tempItems.clear();
        m_currentClientId = -1;
        m_isCommitDataSaved = false;
        m_editingItemIndex = -1;
        updateTempItemsList();
    }

public:
    WorkerWindow() :
        m_hWnd(nullptr),
        m_hListBox(nullptr),
        m_hAcceptBtn(nullptr),
        m_hServeBtn(nullptr),
        m_hStatusLabel(nullptr),
        m_hComboQueue(nullptr),
        m_hFont(nullptr),
        m_hSmallFont(nullptr),
        m_hBrush(nullptr),
        m_running(false),
        m_currentMode(Mode::QUEUE_LIST),
        m_currentClientId(0),
        m_isSaving(false),
        m_maxItemNumber(0),
        m_selectedIndex(-1),
        m_hTitle(nullptr),
        m_hLabelDate(nullptr),
        m_hLabelNumber(nullptr),
        m_hLabelDesc(nullptr),
        m_hLabelPrice(nullptr),
        m_hLabelQty(nullptr),
        m_hLabelCondition(nullptr),
        m_hLabelNote(nullptr),
        m_hItemDateLabel(nullptr),
        m_hItemNumberEdit(nullptr),
        m_hItemDescEdit(nullptr),
        m_hItemPriceEdit(nullptr),
        m_hItemQtyEdit(nullptr),
        m_hItemConditionEdit(nullptr),
        m_hItemNoteEdit(nullptr),
        m_hItemAddBtn(nullptr),
        m_hItemSaveBtn(nullptr),
        m_hItemCancelBtn(nullptr),
        m_hItemListView(nullptr),
        m_hItemTotalQtyLabel(nullptr),
        m_hItemTotalPriceLabel(nullptr),
        m_hItemBackBtn(nullptr),
        m_hItemTotalClientAmountLabel(nullptr),
        m_hItemTotalStoreAmountLabel(nullptr),
        m_hEditRecordBtn(nullptr),
        m_hCancelInputBtn(nullptr),
        m_hCommitBlockTitle(nullptr), // ИСПРАВЛЕНИЕ: Инициализация нового члена класса
        m_hCommitLastNameLabel(nullptr),
        m_hCommitLastNameEdit(nullptr),
        m_hCommitFirstNameLabel(nullptr),
        m_hCommitFirstNameEdit(nullptr),
        m_hCommitMiddleNameLabel(nullptr),
        m_hCommitMiddleNameEdit(nullptr),
        m_hCommitBirthDateLabel(nullptr),
        m_hCommitBirthDateEdit(nullptr),
        m_hCommitPassTypeLabel(nullptr),
        m_hCommitPassTypeCombo(nullptr),
        m_hCommitPassSeriesLabel(nullptr),
        m_hCommitPassSeriesEdit(nullptr),
        m_hCommitPassNumberLabel(nullptr),
        m_hCommitPassNumberEdit(nullptr),
        m_hCommitPhoneLabel(nullptr),
        m_hCommitPhoneEdit(nullptr),
        m_hCommitAddressLabel(nullptr),
        m_hCommitAddressEdit(nullptr),
        m_hCommitSaveBtn(nullptr),
        m_hCommitCancelBtn(nullptr),
        m_hCommitFioLabel(nullptr),
        m_hCommitFioValue(nullptr),
        m_hCommitIdLabel(nullptr),
        m_hCommitIdValue(nullptr),
        m_hCommitPhoneDisplayLabel(nullptr),
        m_hCommitPhoneDisplayValue(nullptr),
        m_hCommitBirthDateDisplayLabel(nullptr),
        m_hCommitBirthDateDisplayValue(nullptr),
        m_hDeleteTicketBtn(nullptr),
        m_hSearchClientBtn(nullptr),
        m_isCommitDataSaved(false),
        m_comInitialized(false),
        m_editingItemIndex(-1) {
        initializeTTS();
    }

    ~WorkerWindow() {

        g_logger.info(L"WorkerWindow: destructor entered");

        m_running.store(false);

        if (m_refreshThread.joinable()) {
            g_logger.info(L"WorkerWindow: destructor is joining refresh thread");
            m_refreshThread.join();
            g_logger.info(L"WorkerWindow: destructor refresh thread joined successfully");
        }

        if (m_hFont) {
            DeleteObject(m_hFont);
            m_hFont = nullptr;
        }

        if (m_hSmallFont) {
            DeleteObject(m_hSmallFont);
            m_hSmallFont = nullptr;
        }

        if (m_hBrush) {
            DeleteObject(m_hBrush);
            m_hBrush = nullptr;
        }

        g_logger.info(L"WorkerWindow: destructor completed");
    }

    // =============================================================================
    // ПОКАЗ ОКНА ТОВАРОВЕДА
    // =============================================================================
    // ИСПРАВЛЕНИЯ:
    // 1. Регистрация класса выполняется один раз.
    // 2. Ошибка CreateWindowExW логируется с GetLastError.
    // 3. Цикл сообщений использует GetMessage(...) > 0.
    //    Ранее значение -1 ошибочно воспринималось как продолжение цикла.
    // =============================================================================
    void show()
    {
        g_logger.info(L"WorkerWindow::show() entered");

        static bool classRegistered = false;

        if (!classRegistered)
        {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"WorkerWindowClass";
            wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

            if (!RegisterClassExW(&wc))
            {
                DWORD err = GetLastError();
                g_logger.error(L"WorkerWindow: RegisterClassExW failed, error=" +
                    std::to_wstring(err));
                return;
            }

            classRegistered = true;
            g_logger.info(L"WorkerWindow: window class registered");
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        m_hWnd = CreateWindowExW(0,
            L"WorkerWindowClass",
            L"Товаровед - Управление очередями",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            screenW,
            screenH,
            nullptr,
            nullptr,
            g_hInstance,
            this);

        if (!m_hWnd)
        {
            DWORD err = GetLastError();
            g_logger.error(L"WorkerWindow: CreateWindowExW failed, error=" +
                std::to_wstring(err));
            return;
        }

        g_logger.info(L"WorkerWindow: window created");

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);

        g_logger.info(L"WorkerWindow: entering message loop");

        MSG msg;
        BOOL getMessageResult = GetMessage(&msg, nullptr, 0, 0);

        while (getMessageResult > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (!IsWindow(m_hWnd))
            {
                g_logger.info(L"WorkerWindow: window no longer exists, breaking message loop");
                break;
            }

            getMessageResult = GetMessage(&msg, nullptr, 0, 0);
        }

        if (getMessageResult == -1)
        {
            DWORD err = GetLastError();
            g_logger.error(L"WorkerWindow: GetMessage returned -1, error=" +
                std::to_wstring(err));
        }

        g_logger.info(L"WorkerWindow: message loop ended");
    }
};
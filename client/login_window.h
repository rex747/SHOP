// login_window.h
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#include "config.h"
#include "logger.h"
#include "auth_manager.h"
#include "https_client.h"
#include "string_utils.h"

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern AuthManager g_authManager;
extern HINSTANCE g_hInstance;

// Пользовательские сообщения для асинхронной обработки результата логина
// и таймера закрытия окна (чтобы не блокировать UI-поток Sleep'ом).
#define WM_LOGIN_RESULT     (WM_USER + 100)
#define TIMER_ID_CLOSE      1

/**
 * LoginWindow — модальное окно входа по номеру телефона с экранной клавиатурой.
 * Логика входа:
 * - При успешном входе устанавливается isLoggedIn = true.
 * - При неудаче (номер не найден) устанавливается флаг loginAttempted = true
 *   в AuthManager, чтобы вызывающий код мог отличить отмену от неудачи.
 * - При сетевой ошибке флаг не устанавливается (пользователь может повторить).
 */
class LoginWindow {
private:
    enum KeyIDs {
        ID_KEY_1 = 1000,
        ID_KEY_2,
        ID_KEY_3,
        ID_KEY_4,
        ID_KEY_5,
        ID_KEY_6,
        ID_KEY_7,
        ID_KEY_8,
        ID_KEY_9,
        ID_KEY_0,
        ID_KEY_PLUS,
        ID_KEY_BACKSPACE,
        ID_KEY_CLEAR
    };

    // Коды результата асинхронного логина (передаются через WM_LOGIN_RESULT)
    enum LoginResultCode {
        RESULT_NETWORK_ERROR = 0,
        RESULT_SUCCESS = 1,
        RESULT_NOT_FOUND = 2,
        RESULT_BLOCKED = 3   // НОВЫЙ: клиент заблокирован директором магазина
    };

    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hStatusLabel;
    HWND m_hSubmitBtn;
    std::vector<HWND> m_allKeyButtons;

    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush, m_hWhiteBrush, m_hGrayBrush;

    // Не-статический указатель на оригинальный WNDPROC Edit-контрола.
    // Статический член приводил бы к потере оригинального proc при повторном
    // создании LoginWindow и к потенциальному ACCESS_VIOLATION при уничтожении.
    WNDPROC m_origEditProc;

    // Данные результата логина (заполняются рабочим потоком, читаются UI-потоком)
    int m_resultCode;
    int m_clientId;
    std::wstring m_fullName;
    std::wstring m_normalizedPhone;
    bool m_loginSuccess;

    // Флаг, что идёт асинхронный запрос (защита от повторного нажатия)
    std::atomic<bool> m_isAuthenticating;

    static constexpr int KEY_WIDTH = 80;
    static constexpr int KEY_HEIGHT = 60;
    static constexpr int KEY_SPACING = 8;

    /**
     * Подкласс Edit-контрола.
     * Блокирует любой ввод с физической клавиатуры и из буфера обмена,
     * оставляя возможность программной установки текста (SetWindowTextW).
     * Почему не ES_READONLY: при ES_READONLY некоторые визуальные стили
     * и поведение фокуса отличаются; subclass даёт полный контроль.
     */
    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        switch (msg) {
            case WM_CHAR:
                case WM_KEYDOWN:
                    case WM_KEYUP:
                        case WM_PASTE:
                            case WM_CUT:
                                case WM_COPY:
                                    case WM_CONTEXTMENU:
                                        // Полностью игнорируем ввод с клавиатуры, вставку, вырезание,
                                        // копирование и контекстное меню. Текст меняется только
                                        // программно через экранную клавиатуру.
                                        return 0;
        }
        // Для всех остальных сообщений вызываем оригинальный обработчик.
        // Оригинальный proc хранится в USERDATA окна (см. createControls).
        WNDPROC orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (orig) {
            return CallWindowProc(orig, hWnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        LoginWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<LoginWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            return 0;
        }

        pThis = reinterpret_cast<LoginWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) {
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        switch (msg) {
        case WM_COMMAND:
            // Передаём полный wParam, чтобы можно было проверить notification code.
            pThis->onCommand(wParam);
            return 0;

        case WM_CTLCOLORBTN:
            return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);

        case WM_CTLCOLORSTATIC:
            return pThis->onCtlColorStatic((HDC)wParam);

        case WM_CTLCOLOREDIT:
            return pThis->onCtlColorEdit((HDC)wParam, (HWND)lParam);

        case WM_LOGIN_RESULT:
            pThis->onLoginResult();
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_ID_CLOSE) {
                KillTimer(hWnd, TIMER_ID_CLOSE);
                g_logger.info(L"LoginWindow: timer fired, destroying window");
                DestroyWindow(hWnd);
            }
            return 0;

        case WM_CLOSE:
            // Для модального окна логина достаточно DestroyWindow.
            // PostQuitMessage(0) опасен: он завершает весь message loop потока
            // и может преждевременно убить другие окна/фоновые потоки приложения.
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            pThis->releaseResources();
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;
        int centerX = clientWidth / 2;
        int startY = 60;

        m_hFontTitle = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontButton = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontLabel = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontEdit = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        m_hGreenBrush = CreateSolidBrush(Config::PRIMARY_COLOR);
        m_hRedBrush = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        m_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        m_hGrayBrush = CreateSolidBrush(RGB(80, 80, 80));

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Вход в систему",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 150, startY, 300, 60,
            m_hWnd, nullptr, g_hInstance, nullptr);
        if (!hTitle) {
            g_logger.error(L"LoginWindow: failed to create title static");
        }
        else {
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
        }
        startY += 80;

        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Номер телефона:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            centerX - 200, startY, 180, 40,
            m_hWnd, nullptr, g_hInstance, nullptr);
        if (!hLabel) {
            g_logger.error(L"LoginWindow: failed to create phone label");
        }
        else {
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        }

        // Поле ввода без ES_READONLY. Ввод блокируется через subclass
        // (см. EditSubclassProc), чтобы сохранить нормальный внешний вид
        // и при этом полностью запретить физическую клавиатуру и буфер обмена.
        m_hPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            centerX + 20, startY, 200, 40,
            m_hWnd, nullptr, g_hInstance, nullptr);
        if (!m_hPhoneEdit) {
            g_logger.error(L"LoginWindow: failed to create phone edit");
        }
        else {
            SendMessageW(m_hPhoneEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);

            // Сохраняем оригинальный WNDPROC в USERDATA самого Edit-контрола,
            // а также в члене класса. При уничтожении обязательно восстанавливаем.
            m_origEditProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc)));
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_origEditProc));
            g_logger.info(L"LoginWindow: phone edit subclassed");
        }
        startY += 60;

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 200, startY, 400, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        if (!m_hStatusLabel) {
            g_logger.error(L"LoginWindow: failed to create status label");
        }
        else {
            SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        }
        startY += 50;

        createKeyboard(centerX, startY);
        startY += (4 * (KEY_HEIGHT + KEY_SPACING)) + 20;

        m_hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Войти",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 100, startY, 200, 50,
            m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        if (!m_hSubmitBtn) {
            g_logger.error(L"LoginWindow: failed to create submit button");
        }
        else {
            SendMessageW(m_hSubmitBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        }

        g_logger.info(L"LoginWindow: all controls created");
    }

    void createKeyboard(int centerX, int startY) {
        const int totalWidth = 4 * (KEY_WIDTH + KEY_SPACING) - KEY_SPACING;
        const int startX = centerX - totalWidth / 2;

        struct KeyDef { int row, col; int id; const wchar_t* label; };
        std::vector<KeyDef> keys = {
            {0, 0, ID_KEY_1,         L"1"},
            {0, 1, ID_KEY_2,         L"2"},
            {0, 2, ID_KEY_3,         L"3"},
            {0, 3, ID_KEY_PLUS,      L"+"},
            {1, 0, ID_KEY_4,         L"4"},
            {1, 1, ID_KEY_5,         L"5"},
            {1, 2, ID_KEY_6,         L"6"},
            {1, 3, ID_KEY_BACKSPACE, L"⌫"},
            {2, 0, ID_KEY_7,         L"7"},
            {2, 1, ID_KEY_8,         L"8"},
            {2, 2, ID_KEY_9,         L"9"},
            {2, 3, ID_KEY_CLEAR,     L"✕"},
            {3, 1, ID_KEY_0,         L"0"}
        };

        for (const auto& k : keys) {
            int x = startX + k.col * (KEY_WIDTH + KEY_SPACING);
            int y = startY + k.row * (KEY_HEIGHT + KEY_SPACING);
            // BS_NOTIFY нужен для получения уведомлений, но в onCommand
            // мы обрабатываем ТОЛЬКО BN_CLICKED, иначе цифры дублируются.
            HWND hBtn = CreateWindowExW(0, L"BUTTON", k.label,
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY,
                x, y, KEY_WIDTH, KEY_HEIGHT,
                m_hWnd, (HMENU)(INT_PTR)k.id, g_hInstance, nullptr);
            if (!hBtn) {
                g_logger.error(L"LoginWindow: failed to create key button id=" + std::to_wstring(k.id));
            }
            else {
                SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
                m_allKeyButtons.push_back(hBtn);
                g_logger.debug(L"LoginWindow: key button created id=" + std::to_wstring(k.id) +
                    L" label=" + k.label);
            }
        }
        g_logger.info(L"LoginWindow: keyboard created, buttons=" + std::to_wstring(m_allKeyButtons.size()));
    }

    void releaseResources() {
        // Обязательно восстанавливаем оригинальный WNDPROC перед уничтожением
        // Edit-контрола. Иначе при повторном создании окна или при определённых
        // сценариях уничтожения возможен ACCESS_VIOLATION.
        if (m_hPhoneEdit && m_origEditProc) {
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_origEditProc));
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, 0);
            m_origEditProc = nullptr;
            g_logger.info(L"LoginWindow: edit subclass restored");
        }

        if (m_hFontTitle) { DeleteObject(m_hFontTitle);  m_hFontTitle = nullptr; }
        if (m_hFontButton) { DeleteObject(m_hFontButton); m_hFontButton = nullptr; }
        if (m_hFontLabel) { DeleteObject(m_hFontLabel);  m_hFontLabel = nullptr; }
        if (m_hFontEdit) { DeleteObject(m_hFontEdit);   m_hFontEdit = nullptr; }
        if (m_hGreenBrush) { DeleteObject(m_hGreenBrush); m_hGreenBrush = nullptr; }
        if (m_hRedBrush) { DeleteObject(m_hRedBrush);   m_hRedBrush = nullptr; }
        if (m_hWhiteBrush) { DeleteObject(m_hWhiteBrush); m_hWhiteBrush = nullptr; }
        if (m_hGrayBrush) { DeleteObject(m_hGrayBrush);  m_hGrayBrush = nullptr; }

        g_logger.info(L"LoginWindow: resources released");
    }

    LRESULT onCtlColorBtn(HDC hdc, HWND hBtn) {
        SetTextColor(hdc, RGB(255, 255, 255));
        if (hBtn == m_hSubmitBtn) {
            SetBkColor(hdc, Config::PRIMARY_COLOR);
            return (LRESULT)m_hGreenBrush;
        }
        else {
            SetBkColor(hdc, RGB(80, 80, 80));
            return (LRESULT)m_hGrayBrush;
        }
    }

    LRESULT onCtlColorStatic(HDC hdc) {
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    LRESULT onCtlColorEdit(HDC hdc, HWND hEdit) {
        if (hEdit == m_hPhoneEdit) {
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)m_hWhiteBrush;
        }
        return DefWindowProcW(m_hWnd, WM_CTLCOLOREDIT, (WPARAM)hdc, (LPARAM)hEdit);
    }

    /**
     * Обработка WM_COMMAND.
     * КРИТИЧНО: обрабатываем только BN_CLICKED.
     * Кнопки созданы с BS_NOTIFY, поэтому Windows при одном клике может
     * прислать несколько уведомлений (BN_CLICKED, BN_SETFOCUS, BN_KILLFOCUS).
     * Если реагировать на все — цифра/действие выполняются дважды.
     */
    void onCommand(WPARAM wParam) {
        const int cmd = LOWORD(wParam);
        const int code = HIWORD(wParam);

        // Игнорируем всё, кроме реального нажатия кнопки
        if (code != BN_CLICKED) {
            return;
        }

        if (m_isAuthenticating.load()) {
            // Игнорируем любые нажатия, пока идёт сетевой запрос
            return;
        }

        if (cmd >= ID_KEY_1 && cmd <= ID_KEY_CLEAR) {
            onKeyPress(cmd);
            return;
        }
        if (cmd == 1) {
            onSubmit();
            return;
        }
    }

    void onKeyPress(int id) {
        wchar_t currentText[40] = {};
        GetWindowTextW(m_hPhoneEdit, currentText, 40);
        std::wstring text = currentText;

        switch (id) {
        case ID_KEY_BACKSPACE:
            if (!text.empty()) {
                text.pop_back();
                SetWindowTextW(m_hPhoneEdit, text.c_str());
                g_logger.debug(L"LoginWindow: Backspace, new text=\"" + text + L"\"");
            }
            break;

        case ID_KEY_CLEAR:
            text.clear();
            SetWindowTextW(m_hPhoneEdit, L"");
            g_logger.debug(L"LoginWindow: Cleared phone field");
            break;

        case ID_KEY_PLUS:
            if (text.empty()) {
                text = L"+";
                SetWindowTextW(m_hPhoneEdit, text.c_str());
                g_logger.debug(L"LoginWindow: Added '+'");
            }
            break;

        default: {
            wchar_t digit = 0;
            if (id >= ID_KEY_1 && id <= ID_KEY_9) {
                digit = L'0' + (id - ID_KEY_1 + 1);
            }
            else if (id == ID_KEY_0) {
                digit = L'0';
            }
            else {
                return;
            }
            if (text.length() < 15) {
                text += digit;
                SetWindowTextW(m_hPhoneEdit, text.c_str());
                g_logger.debug(L"LoginWindow: Added digit " + std::wstring(1, digit) +
                    L", text=\"" + text + L"\"");
            }
            break;
        }
        }

        SetWindowTextW(m_hStatusLabel, L"");
        InvalidateRect(m_hPhoneEdit, nullptr, TRUE);
    }

    /**
     * Запускает процесс аутентификации.
     * Сетевой запрос выполняется в отдельном потоке, чтобы не блокировать UI.
     * Результат доставляется через PostMessage(WM_LOGIN_RESULT).
     */
    void onSubmit() {
        if (m_isAuthenticating.exchange(true)) {
            g_logger.info(L"[onSubmit]: запуск аутентификации");
            return; // уже идёт запрос
        }

        wchar_t buf[30] = {};
        GetWindowTextW(m_hPhoneEdit, buf, 30);
        std::wstring phone = trim(buf);

        if (phone.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Введите номер телефона в формате +79019001010");
            m_isAuthenticating = false;
            g_logger.warning(L"LoginWindow: empty phone submitted");
            return;
        }

        std::wstring normalized = normalizePhone(phone);
        if (normalized.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Неверный формат");
            m_isAuthenticating = false;
            g_logger.warning(L"LoginWindow: invalid phone format \"" + phone + L"\"");
            return;
        }

        // ---- Устанавливаем флаг попытки входа в AuthManager ----
        g_authManager.setLoginAttempted(true, wstring_to_utf8(normalized));

        SetWindowTextW(m_hStatusLabel, L"Проверка номера по базе...");
        EnableWindow(m_hSubmitBtn, FALSE);
        for (HWND hBtn : m_allKeyButtons) {
            EnableWindow(hBtn, FALSE);
        }
        g_logger.info(L"LoginWindow: authentication started for " + normalized);

        // Копируем нужные данные для потока (this может быть уничтожен,
        // поэтому HWND и нормализованный номер передаём по значению)
        HWND hWndCopy = m_hWnd;
        std::wstring phoneCopy = normalized;

        std::thread([this, hWndCopy, phoneCopy]() {
            nlohmann::json request;
            request["phone"] = wstring_to_utf8(phoneCopy);

            // g_httpsClient.post — блокирующий вызов. Выполняется вне UI-потока.
            auto response = g_httpsClient.post(L"/api/v1/clients/by_phone", request, L"");

            // Заполняем результат (читается только из UI-потока после PostMessage)
            if (!response) {
                g_logger.error(L"[LoginWindow:onSubmit:!response]: не получен ответ от сервера, проверьте соединение");
                m_resultCode = RESULT_NETWORK_ERROR;
                g_logger.error(L"Network error");
                m_loginSuccess = false;
                g_logger.error(L"LoginWindow: network error for " + phoneCopy);
            }
            // =====================================================================
            // НОВАЯ ПРОВЕРКА: КЛИЕНТ ЗАБЛОКИРОВАН ДИРЕКТОРОМ МАГАЗИНА
            // =====================================================================
            // Сервер возвращает HTTP 403 с JSON:
            //   {"error": "Ваш аккаунт заблокирован...", "blocked": true}
            // WinHTTP успешно получает этот ответ (HTTP 403 не является
            // транспортной ошибкой для WinHttpReceiveResponse).
            // Проверяем поле "blocked" ДО проверки полей успешного входа,
            // чтобы не перепутать блокировку с "пользователь не найден".
            // =====================================================================
            else if (response->contains("blocked") && (*response)["blocked"].is_boolean() &&
                (*response)["blocked"].get<bool>()) {
                g_logger.warning(L"[LoginWindow] Client is BLOCKED by director: " + phoneCopy);
                m_resultCode = RESULT_BLOCKED;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
            }
            else if (response->contains("id") && response->contains("name") &&
                response->contains("access_token") && response->contains("refresh_token")) {
                m_resultCode = RESULT_SUCCESS;
                m_loginSuccess = true;
                m_clientId = (*response)["id"].get<int>();
                std::string nameUtf8 = (*response)["name"].get<std::string>();
                m_fullName = utf8_to_wstring(nameUtf8);
                m_normalizedPhone = phoneCopy;

                // Сохраняем токены
                std::string accessToken = (*response)["access_token"].get<std::string>();
                std::string refreshToken = (*response)["refresh_token"].get<std::string>();
                int64_t expiresAt = 0;
                if (response->contains("expires_at") && (*response)["expires_at"].is_number_integer()) {
                    expiresAt = (*response)["expires_at"].get<int64_t>();
                }
                else {
                    // fallback: +1 час
                    auto now = std::chrono::system_clock::now();
                    expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
                        now.time_since_epoch()).count() + 3600;
                }
                g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt,
                    wstring_to_utf8(m_normalizedPhone));

                g_logger.info(L"LoginWindow: success, clientId=" + std::to_wstring(m_clientId) +
                    L", name=\"" + m_fullName + L"\", token saved");
            }
            else {
                m_resultCode = RESULT_NOT_FOUND;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
                g_logger.warning(L"LoginWindow: phone not found " + phoneCopy);
            }

            // Доставляем результат в UI-поток. Если окно уже уничтожено — PostMessage
            // просто вернёт FALSE, ничего страшного.
            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_LOGIN_RESULT, 0, 0);
                g_logger.info(L"LoginWindow: posted WM_LOGIN_RESULT, code=" +
                    std::to_wstring(m_resultCode));
            }
            }).detach();
    }

    /**
     * Обработчик результата логина (вызывается из UI-потока).
     * Здесь безопасно менять состояние контролов и вызывать setLoggedIn.
     */
    void onLoginResult() {
        switch (m_resultCode) {
        case RESULT_NETWORK_ERROR:
            SetWindowTextW(m_hStatusLabel, L"Ошибка соединения");
            EnableWindow(m_hSubmitBtn, TRUE);
            for (HWND hBtn : m_allKeyButtons) {
                EnableWindow(hBtn, TRUE);
            }
            m_isAuthenticating = false;
            g_logger.info(L"LoginWindow: UI re-enabled after network error");
            break;

        case RESULT_SUCCESS:
            g_authManager.setLoggedIn(true, m_clientId, m_fullName, m_normalizedPhone);
            // При успехе сбрасываем флаг попытки (она успешна)
            g_authManager.setLoginAttempted(false);
            SetWindowTextW(m_hStatusLabel, L"Успешный вход!");
            // Небольшая задержка через таймер, чтобы пользователь увидел статус.
            // Sleep в UI-потоке недопустим — окно «зависает».
            SetTimer(m_hWnd, TIMER_ID_CLOSE, 500, nullptr);
            g_logger.info(L"LoginWindow: success, timer set (500 ms)");
            break;

            // =====================================================================
            // НОВЫЙ CASE: КЛИЕНТ ЗАБЛОКИРОВАН ДИРЕКТОРОМ МАГАЗИНА
            // =====================================================================
            // Показываем модальное сообщение о блокировке. После закрытия
            // MessageBox очищаем поле ввода и разблокируем UI, чтобы
            // пользователь мог ввести другой номер.
            // Флаг loginAttempted НЕ устанавливаем, чтобы вызывающий код
            // (main_window.h) не перенаправлял на handleFirstTime().
            // =====================================================================
        case RESULT_BLOCKED:
            g_logger.warning(L"LoginWindow: client BLOCKED by director, showing message for " +
                m_normalizedPhone);

            // Показываем сообщение о блокировке
            MessageBoxW(m_hWnd,
                L"Ваш аккаунт заблокирован.\n\n"
                L"Обратитесь к администрации магазина.",
                L"Доступ запрещён",
                MB_OK | MB_ICONERROR);

            // Сбрасываем состояние: очищаем поле, разблокируем UI
            SetWindowTextW(m_hPhoneEdit, L"");
            SetWindowTextW(m_hStatusLabel, L"Аккаунт заблокирован");
            EnableWindow(m_hSubmitBtn, TRUE);
            for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
            m_isAuthenticating = false;

            // ВАЖНО: Сбрасываем флаг попытки входа.
            // Если этого не сделать, main_window.h в терминале увидит
            // wasLoginAttempted() == true и перенаправит на handleFirstTime()
            // вместо того, чтобы просто вернуться в главное меню.
            g_authManager.setLoginAttempted(false);

            g_logger.info(L"LoginWindow: blocked message shown, UI re-enabled, "
                L"loginAttempted reset to false");
            break;

        case RESULT_NOT_FOUND:
            g_authManager.setLoggedIn(false, 0, L"", m_normalizedPhone);
            SetWindowTextW(m_hStatusLabel, L"Номер не найден");
            SetTimer(m_hWnd, TIMER_ID_CLOSE, 1000, nullptr);
            g_logger.info(L"LoginWindow: not found, timer set (1000 ms)");
            break;
        }
    }

    /**
     * Нормализация номера телефона к формату +7XXXXXXXXXX (минимум 11 символов).
     * Оставляет только '+' (в начале) и цифры.
     */
    std::wstring normalizePhone(const std::wstring& input) {
        std::wstring result;
        bool hasPlus = false;
        for (wchar_t ch : input) {
            if (ch == L'+') {
                if (!hasPlus && result.empty()) {
                    hasPlus = true;
                    result += ch;
                }
            }
            else if (ch >= L'0' && ch <= L'9') {
                result += ch;
            }
        }
        if (!hasPlus && !result.empty()) {
            result = L"+" + result;
        }
        if (result.length() < 11) {
            return L"";
        }
        return result;
    }

    std::wstring trim(const std::wstring& s) {
        size_t start = s.find_first_not_of(L" \t");
        size_t end = s.find_last_not_of(L" \t");
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

public:
    LoginWindow()
        : m_hWnd(nullptr)
        , m_hPhoneEdit(nullptr)
        , m_hStatusLabel(nullptr)
        , m_hSubmitBtn(nullptr)
        , m_hFontTitle(nullptr)
        , m_hFontButton(nullptr)
        , m_hFontLabel(nullptr)
        , m_hFontEdit(nullptr)
        , m_hGreenBrush(nullptr)
        , m_hRedBrush(nullptr)
        , m_hWhiteBrush(nullptr)
        , m_hGrayBrush(nullptr)
        , m_origEditProc(nullptr)
        , m_resultCode(RESULT_NETWORK_ERROR)
        , m_clientId(0)
        , m_loginSuccess(false)
        , m_isAuthenticating(false)
    {
    }

    /**
     * Показывает окно логина модально относительно hParent.
     * @return true, если вход выполнен успешно (g_authManager.isLoggedIn() == true).
     *
     * После возврата вызывающий код обязан открыть главное окно (MainWindow),
     * если метод вернул true. Само LoginWindow только устанавливает состояние
     * в AuthManager и закрывается.
     */
    bool show(HWND hParent) {
        m_loginSuccess = false;
        m_isAuthenticating = false;

        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = g_hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszClassName = L"LoginWindowClass";
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            if (!RegisterClassExW(&wcex)) {
                g_logger.error(L"LoginWindow: RegisterClassExW failed");
                return false;
            }
            classRegistered = true;
            g_logger.info(L"LoginWindow class registered");
        }

        int height = 60 + 80 + 60 + 50 + 30 + (4 * (KEY_HEIGHT + KEY_SPACING)) + 20 + 50 + 20;
        int width = 500;
        int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, L"LoginWindowClass", L"Вход",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
            x, y, width, height, hParent, nullptr, g_hInstance, this);

        if (!m_hWnd) {
            g_logger.error(L"LoginWindow: Failed to create window");
            return false;
        }

        EnableWindow(hParent, FALSE);
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        g_logger.info(L"LoginWindow: shown");

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) {
                break;
            }
        }

        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);

        g_logger.info(L"LoginWindow: closed, success=" + std::wstring(m_loginSuccess ? L"true" : L"false"));
        return m_loginSuccess;
    }
};
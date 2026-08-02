// login_window.h (ПОЛНАЯ ПРОДАКШН-ВЕРСИЯ С ИСПРАВЛЕНИЯМИ ДИАЛОГА И АВТОВХОДА)
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
#include "worker_registration_window.h"

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern AuthManager g_authManager;
extern HINSTANCE g_hInstance;

#define WM_LOGIN_RESULT     (WM_USER + 100)
#define TIMER_ID_CLOSE      1

class LoginWindow {
private:
    enum KeyIDs {
        ID_KEY_1 = 1000, ID_KEY_2, ID_KEY_3, ID_KEY_4, ID_KEY_5,
        ID_KEY_6, ID_KEY_7, ID_KEY_8, ID_KEY_9, ID_KEY_0,
        ID_KEY_PLUS, ID_KEY_BACKSPACE, ID_KEY_CLEAR
    };
    enum LoginResultCode {
        RESULT_NETWORK_ERROR = 0,
        RESULT_SUCCESS = 1,
        RESULT_NOT_FOUND = 2
    };

    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hStatusLabel;
    HWND m_hSubmitBtn;
    std::vector<HWND> m_allKeyButtons;
    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush, m_hWhiteBrush, m_hGrayBrush;
    WNDPROC m_origEditProc;
    int m_resultCode;
    int m_clientId;
    std::wstring m_fullName;
    std::wstring m_normalizedPhone;
    bool m_loginSuccess;
    std::atomic<bool> m_isAuthenticating;

    static constexpr const wchar_t* ADMIN_PASSWORD = L"123";
    static constexpr int KEY_WIDTH = 80;
    static constexpr int KEY_HEIGHT = 60;
    static constexpr int KEY_SPACING = 8;

    // ---- Оконная процедура для диалога ввода пароля ----
    static LRESULT CALLBACK AdminPasswordDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                if (id == 2) { // OK
                    // Получаем введённый пароль
                    HWND hEdit = GetDlgItem(hDlg, 1);
                    wchar_t pwd[64];
                    GetWindowTextW(hEdit, pwd, 64);
                    std::wstring entered = pwd;
                    // Удаляем пробелы
                    size_t start = entered.find_first_not_of(L" \t\r\n");
                    size_t end = entered.find_last_not_of(L" \t\r\n");
                    if (start != std::wstring::npos && end != std::wstring::npos) {
                        entered = entered.substr(start, end - start + 1);
                    }
                    else {
                        entered.clear();
                    }
                    g_logger.info(L"AdminPasswordDialog: OK clicked, password length = " + std::to_wstring(entered.length()));
                    if (entered == ADMIN_PASSWORD) {
                        // Пароль верен – сохраняем результат и закрываем диалог с IDOK
                        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)1); // успех
                        
                    }
                    else {
                        MessageBoxW(hDlg, L"Неверный пароль", L"Ошибка", MB_OK | MB_ICONERROR);
                        SetWindowTextW(hEdit, L"");
                        SetFocus(hEdit);
                        g_logger.warning(L"AdminPasswordDialog: wrong password entered");
                    }
                    return TRUE;
                }
                else if (id == 3) { // Cancel
                    g_logger.info(L"AdminPasswordDialog: Cancel clicked");
                    SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0); // отмена
                    
                    return TRUE;
                }
            }
            break;
        }
        case WM_CLOSE: {
            g_logger.info(L"AdminPasswordDialog: WM_CLOSE received");
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0); // отмена
            //DestroyWindow(hDlg);
            return TRUE;
        }
        }
        return DefWindowProcW(hDlg, msg, wParam, lParam);
    }

    // ---- Остальные методы (без изменений) ----
    static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP:
        case WM_PASTE: case WM_CUT: case WM_COPY: case WM_CONTEXTMENU:
            return 0;
        }
        WNDPROC orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (orig) return CallWindowProc(orig, hWnd, msg, wParam, lParam);
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
            g_logger.info(L"LoginWindow: WM_CREATE processed");
            return 0;
        }

        pThis = reinterpret_cast<LoginWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND: pThis->onCommand(wParam); return 0;
        case WM_CTLCOLORBTN: return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);
        case WM_CTLCOLORSTATIC: return pThis->onCtlColorStatic((HDC)wParam);
        case WM_CTLCOLOREDIT: return pThis->onCtlColorEdit((HDC)wParam, (HWND)lParam);
        case WM_LOGIN_RESULT: pThis->onLoginResult(); return 0;
        case WM_TIMER:
            if (wParam == TIMER_ID_CLOSE) {
                KillTimer(hWnd, TIMER_ID_CLOSE);
                g_logger.info(L"LoginWindow: timer fired, destroying window");
                DestroyWindow(hWnd);
            }
            return 0;
        case WM_CLOSE:
            g_logger.info(L"LoginWindow: WM_CLOSE received");
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            g_logger.info(L"LoginWindow: WM_DESTROY, releasing resources");
            pThis->releaseResources();
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        g_logger.info(L"LoginWindow: createControls started");
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;
        int centerX = clientWidth / 2;
        int startY = 60;

        m_hFontTitle = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontButton = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontLabel = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontEdit = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        m_hGreenBrush = CreateSolidBrush(Config::PRIMARY_COLOR);
        m_hRedBrush = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        m_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        m_hGrayBrush = CreateSolidBrush(RGB(80, 80, 80));

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Вход в систему", WS_VISIBLE | WS_CHILD | SS_CENTER, centerX - 150, startY, 300, 60, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
        startY += 80;

        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Номер телефона:", WS_VISIBLE | WS_CHILD | SS_RIGHT, centerX - 200, startY, 180, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, centerX + 20, startY, 200, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hPhoneEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        m_origEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc)));
        SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_origEditProc));
        g_logger.info(L"LoginWindow: phone edit subclassed");
        startY += 60;

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_CENTER, centerX - 200, startY, 400, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        startY += 50;

        createKeyboard(centerX, startY);
        startY += (4 * (KEY_HEIGHT + KEY_SPACING)) + 20;

        m_hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Войти", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, centerX - 100, startY, 200, 50, m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(m_hSubmitBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        g_logger.info(L"LoginWindow: all controls created");
    }

    void createKeyboard(int centerX, int startY) {
        const int totalWidth = 4 * (KEY_WIDTH + KEY_SPACING) - KEY_SPACING;
        const int startX = centerX - totalWidth / 2;
        struct KeyDef { int row, col; int id; const wchar_t* label; };
        std::vector<KeyDef> keys = {
            {0, 0, ID_KEY_1, L"1"}, {0, 1, ID_KEY_2, L"2"}, {0, 2, ID_KEY_3, L"3"}, {0, 3, ID_KEY_PLUS, L"+"},
            {1, 0, ID_KEY_4, L"4"}, {1, 1, ID_KEY_5, L"5"}, {1, 2, ID_KEY_6, L"6"}, {1, 3, ID_KEY_BACKSPACE, L"⌫"},
            {2, 0, ID_KEY_7, L"7"}, {2, 1, ID_KEY_8, L"8"}, {2, 2, ID_KEY_9, L"9"}, {2, 3, ID_KEY_CLEAR, L"✕"},
            {3, 1, ID_KEY_0, L"0"}
        };
        for (const auto& k : keys) {
            int x = startX + k.col * (KEY_WIDTH + KEY_SPACING);
            int y = startY + k.row * (KEY_HEIGHT + KEY_SPACING);
            HWND hBtn = CreateWindowExW(0, L"BUTTON", k.label, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, x, y, KEY_WIDTH, KEY_HEIGHT, m_hWnd, (HMENU)(INT_PTR)k.id, g_hInstance, nullptr);
            if (hBtn) {
                SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
                m_allKeyButtons.push_back(hBtn);
            }
            else {
                g_logger.error(L"LoginWindow: failed to create key button id=" + std::to_wstring(k.id));
            }
        }
    }

    void releaseResources() {
        g_logger.info(L"LoginWindow: releaseResources started");
        if (m_hPhoneEdit && m_origEditProc) {
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_origEditProc));
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, 0);
            m_origEditProc = nullptr;
            g_logger.info(L"LoginWindow: edit subclass restored");
        }
        if (m_hFontTitle) { DeleteObject(m_hFontTitle); m_hFontTitle = nullptr; }
        if (m_hFontButton) { DeleteObject(m_hFontButton); m_hFontButton = nullptr; }
        if (m_hFontLabel) { DeleteObject(m_hFontLabel); m_hFontLabel = nullptr; }
        if (m_hFontEdit) { DeleteObject(m_hFontEdit); m_hFontEdit = nullptr; }
        if (m_hGreenBrush) { DeleteObject(m_hGreenBrush); m_hGreenBrush = nullptr; }
        if (m_hRedBrush) { DeleteObject(m_hRedBrush); m_hRedBrush = nullptr; }
        if (m_hWhiteBrush) { DeleteObject(m_hWhiteBrush); m_hWhiteBrush = nullptr; }
        if (m_hGrayBrush) { DeleteObject(m_hGrayBrush); m_hGrayBrush = nullptr; }
        g_logger.info(L"LoginWindow: resources released");
    }

    LRESULT onCtlColorBtn(HDC hdc, HWND hBtn) {
        SetTextColor(hdc, RGB(255, 255, 255));
        if (hBtn == m_hSubmitBtn) { SetBkColor(hdc, Config::PRIMARY_COLOR); return (LRESULT)m_hGreenBrush; }
        else { SetBkColor(hdc, RGB(80, 80, 80)); return (LRESULT)m_hGrayBrush; }
    }

    LRESULT onCtlColorStatic(HDC hdc) {
        SetTextColor(hdc, RGB(0, 0, 0)); SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    LRESULT onCtlColorEdit(HDC hdc, HWND hEdit) {
        if (hEdit == m_hPhoneEdit) { SetTextColor(hdc, RGB(0, 0, 0)); SetBkColor(hdc, RGB(255, 255, 255)); return (LRESULT)m_hWhiteBrush; }
        return DefWindowProcW(m_hWnd, WM_CTLCOLOREDIT, (WPARAM)hdc, (LPARAM)hEdit);
    }

    void onCommand(WPARAM wParam) {
        const int cmd = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code != BN_CLICKED) return;
        if (m_isAuthenticating.load()) {
            g_logger.info(L"LoginWindow: onCommand ignored, authenticating in progress");
            return;
        }
        if (cmd >= ID_KEY_1 && cmd <= ID_KEY_CLEAR) { onKeyPress(cmd); return; }
        if (cmd == 1) { onSubmit(); return; }
    }

    void onKeyPress(int id) {
        wchar_t currentText[40] = {};
        GetWindowTextW(m_hPhoneEdit, currentText, 40);
        std::wstring text = currentText;

        switch (id) {
        case ID_KEY_BACKSPACE: if (!text.empty()) { text.pop_back(); SetWindowTextW(m_hPhoneEdit, text.c_str()); } break;
        case ID_KEY_CLEAR: text.clear(); SetWindowTextW(m_hPhoneEdit, L""); break;
        case ID_KEY_PLUS: if (text.empty()) { text = L"+"; SetWindowTextW(m_hPhoneEdit, text.c_str()); } break;
        default: {
            wchar_t digit = 0;
            if (id >= ID_KEY_1 && id <= ID_KEY_9) digit = L'0' + (id - ID_KEY_1 + 1);
            else if (id == ID_KEY_0) digit = L'0';
            else return;
            if (text.length() < 15) { text += digit; SetWindowTextW(m_hPhoneEdit, text.c_str()); }
            break;
        }
        }
        SetWindowTextW(m_hStatusLabel, L"");
        InvalidateRect(m_hPhoneEdit, nullptr, TRUE);
    }

    void onSubmit() {
        if (m_isAuthenticating.exchange(true)) {
            g_logger.info(L"LoginWindow: onSubmit already in progress");
            return;
        }
        wchar_t buf[30] = {};
        GetWindowTextW(m_hPhoneEdit, buf, 30);
        std::wstring phone = trim(buf);
        if (phone.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Введите номер телефона в формате +79019001010");
            m_isAuthenticating = false;
            g_logger.warning(L"LoginWindow: onSubmit empty phone");
            return;
        }

        std::wstring normalized = normalizePhone(phone);
        if (normalized.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Неверный формат");
            m_isAuthenticating = false;
            g_logger.warning(L"LoginWindow: onSubmit invalid phone format: " + phone);
            return;
        }

        g_authManager.setLoginAttempted(true, wstring_to_utf8(normalized));
        SetWindowTextW(m_hStatusLabel, L"Проверка номера по базе...");
        EnableWindow(m_hSubmitBtn, FALSE);
        for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, FALSE);

        g_logger.info(L"LoginWindow: authentication started for " + normalized);
        HWND hWndCopy = m_hWnd;
        std::wstring phoneCopy = normalized;

        std::thread([this, hWndCopy, phoneCopy]() {
            g_logger.info(L"LoginWindow: background thread started for " + phoneCopy);
            nlohmann::json request;
            request["phone"] = wstring_to_utf8(phoneCopy);
            auto response = g_httpsClient.post(L"/api/v1/clients/by_phone", request, L"");

            if (!response) {
                g_logger.error(L"[LoginWindow] Network error for " + phoneCopy);
                m_resultCode = RESULT_NETWORK_ERROR;
                m_loginSuccess = false;
            }
            else if (response->contains("id") && response->contains("name") && response->contains("access_token") && response->contains("refresh_token")) {
                m_resultCode = RESULT_SUCCESS;
                m_loginSuccess = true;
                m_clientId = (*response)["id"].get<int>();
                m_fullName = utf8_to_wstring((*response)["name"].get<std::string>());
                m_normalizedPhone = phoneCopy;

                std::string accessToken = (*response)["access_token"].get<std::string>();
                std::string refreshToken = (*response)["refresh_token"].get<std::string>();
                int64_t expiresAt = response->contains("expires_at") && (*response)["expires_at"].is_number_integer() ? (*response)["expires_at"].get<int64_t>() : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

                g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt, wstring_to_utf8(m_normalizedPhone));
                g_logger.info(L"LoginWindow: success, clientId=" + std::to_wstring(m_clientId));
            }
            else {
                m_resultCode = RESULT_NOT_FOUND;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
                g_logger.warning(L"LoginWindow: phone not found " + phoneCopy);
            }

            if (IsWindow(hWndCopy)) {
                PostMessageW(hWndCopy, WM_LOGIN_RESULT, 0, 0);
                g_logger.info(L"LoginWindow: posted WM_LOGIN_RESULT");
            }
            else {
                g_logger.warning(L"LoginWindow: target window already destroyed");
            }
            }).detach();
    }

    void onLoginResult() {
        g_logger.info(L"LoginWindow: onLoginResult entered, resultCode=" + std::to_wstring(m_resultCode));
        switch (m_resultCode) {
        case RESULT_NETWORK_ERROR:
            SetWindowTextW(m_hStatusLabel, L"Ошибка соединения");
            EnableWindow(m_hSubmitBtn, TRUE);
            for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
            m_isAuthenticating = false;
            g_logger.info(L"LoginWindow: UI re-enabled after network error");
            break;

        case RESULT_SUCCESS:
            g_authManager.setLoggedIn(true, m_clientId, m_fullName, m_normalizedPhone);
            g_authManager.setLoginAttempted(false);
            SetWindowTextW(m_hStatusLabel, L"Успешный вход!");
            SetTimer(m_hWnd, TIMER_ID_CLOSE, 500, nullptr);
            g_logger.info(L"LoginWindow: success, timer set (500 ms)");
            break;

        case RESULT_NOT_FOUND: {
            g_authManager.setLoggedIn(false, 0, L"", m_normalizedPhone);
            g_logger.info(L"LoginWindow: phone not found, showing admin password dialog");

            bool passwordOk = showAdminPasswordDialog();
            g_logger.info(L"LoginWindow: admin password dialog result = " + std::wstring(passwordOk ? L"true" : L"false"));

            if (passwordOk) {
                g_logger.info(L"LoginWindow: password correct, creating WorkerRegistrationWindow for " + m_normalizedPhone);
                WorkerRegistrationWindow regWnd(m_normalizedPhone);
                int result = regWnd.show(m_hWnd);
                g_logger.info(L"LoginWindow: WorkerRegistrationWindow closed with result = " + std::to_wstring(result));

                if (result == IDOK) {
                    g_logger.info(L"LoginWindow: worker registered successfully, performing auto-login for " + m_normalizedPhone);

                    // Автоматический вход после регистрации
                    nlohmann::json request;
                    request["phone"] = wstring_to_utf8(m_normalizedPhone);
                    auto response = g_httpsClient.post(L"/api/v1/clients/by_phone", request, L"");

                    if (response && response->contains("id") && response->contains("name") &&
                        response->contains("access_token") && response->contains("refresh_token")) {
                        int clientId = (*response)["id"].get<int>();
                        std::wstring fullName = utf8_to_wstring((*response)["name"].get<std::string>());
                        std::string accessToken = (*response)["access_token"].get<std::string>();
                        std::string refreshToken = (*response)["refresh_token"].get<std::string>();
                        int64_t expiresAt = response->contains("expires_at") && (*response)["expires_at"].is_number_integer()
                            ? (*response)["expires_at"].get<int64_t>()
                            : std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

                        g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt, wstring_to_utf8(m_normalizedPhone));
                        g_authManager.setLoggedIn(true, clientId, fullName, m_normalizedPhone);

                        m_clientId = clientId;
                        m_fullName = fullName;
                        m_loginSuccess = true;   // Устанавливаем флаг успешного входа

                        g_logger.info(L"LoginWindow: auto-login successful for worker " + m_normalizedPhone + L", clientId=" + std::to_wstring(clientId));
                        MessageBoxW(m_hWnd, L"Товаровед успешно зарегистрирован и авторизован!", L"Успех", MB_OK);
                        DestroyWindow(m_hWnd);   // Закрываем окно входа, приложение продолжит работу
                    }
                    else {
                        g_logger.error(L"LoginWindow: failed to auto-login after registration for " + m_normalizedPhone);
                        MessageBoxW(m_hWnd, L"Регистрация прошла, но не удалось выполнить вход. Попробуйте войти заново.", L"Ошибка", MB_OK);
                        // Оставляем окно открытым для повторного ввода
                        m_isAuthenticating = false;
                        EnableWindow(m_hSubmitBtn, TRUE);
                        for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                        SetWindowTextW(m_hStatusLabel, L"Ошибка автовхода. Введите номер повторно.");
                    }
                }
                else {
                    SetWindowTextW(m_hStatusLabel, L"Регистрация отменена или не удалась");
                    EnableWindow(m_hSubmitBtn, TRUE);
                    for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                    m_isAuthenticating = false;
                }
            }
            else {
                SetWindowTextW(m_hStatusLabel, L"Неверный пароль администратора или отмена");
                EnableWindow(m_hSubmitBtn, TRUE);
                for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                m_isAuthenticating = false;
            }
            break;
        }
        }
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ ДИАЛОГ ВВОДА ПАРОЛЯ АДМИНИСТРАТОРА
    // =========================================================================
    bool showAdminPasswordDialog() {
        g_logger.info(L"showAdminPasswordDialog: entered");

        // Регистрируем класс диалога, если ещё не зарегистрирован
        static bool dialogClassRegistered = false;
        if (!dialogClassRegistered) {
            WNDCLASSEXW dlgClass = {};
            dlgClass.cbSize = sizeof(WNDCLASSEX);
            dlgClass.style = CS_HREDRAW | CS_VREDRAW;
            dlgClass.lpfnWndProc = AdminPasswordDialogProc;   // Наша процедура
            dlgClass.hInstance = g_hInstance;
            dlgClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
            dlgClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            dlgClass.lpszClassName = L"AdminPasswordDialogClass";
            if (!RegisterClassExW(&dlgClass)) {
                g_logger.error(L"showAdminPasswordDialog: failed to register dialog class");
                return false;
            }
            dialogClassRegistered = true;
            g_logger.info(L"showAdminPasswordDialog: dialog class registered");
        }

        // Создаём окно диалога с расширенным стилем WS_EX_CONTROLPARENT
        HWND hDlg = CreateWindowExW(
            WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME,
            L"AdminPasswordDialogClass",
            L"Введите пароль администратора",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            (GetSystemMetrics(SM_CXSCREEN) - 400) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - 200) / 2,
            400, 200,
            m_hWnd,
            nullptr,
            g_hInstance,
            nullptr
        );

        if (!hDlg) {
            g_logger.error(L"showAdminPasswordDialog: CreateWindowExW failed");
            return false;
        }

        // Создаём элементы управления
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Пароль администратора:", WS_VISIBLE | WS_CHILD, 20, 30, 150, 30, hDlg, nullptr, g_hInstance, nullptr);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_PASSWORD, 180, 30, 180, 30, hDlg, (HMENU)1, g_hInstance, nullptr);
        HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 80, 100, 100, 40, hDlg, (HMENU)2, g_hInstance, nullptr);
        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Отмена", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 220, 100, 100, 40, hDlg, (HMENU)3, g_hInstance, nullptr);

        if (!hLabel || !hEdit || !hOk || !hCancel) {
            g_logger.error(L"showAdminPasswordDialog: failed to create controls");
            DestroyWindow(hDlg);
            return false;
        }

        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Инициализируем GWLP_USERDATA: 0 = активен, 1 = успех, 2 = отмена
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0);


        EnableWindow(m_hWnd, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        SetFocus(hEdit);

        bool passwordOk = false;
        bool dialogClosed = false;
        MSG msg;

        g_logger.info(L"showAdminPasswordDialog: entering message loop with IsDialogMessage");

        while (!dialogClosed && IsWindow(hDlg)) {
            if (GetMessage(&msg, nullptr, 0, 0) > 0) {
                // Передаём сообщение диалогу через IsDialogMessage
                if (!IsDialogMessage(hDlg, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }

                // Проверяем, не закрыт ли диалог (например, через DestroyWindow)
                if (!IsWindow(hDlg)) break;

                // Проверяем результат, сохранённый в GWLP_USERDATA
                LONG_PTR result = GetWindowLongPtr(hDlg, GWLP_USERDATA);
                if (result == 1) { // 0 = отмена, 1 = успех (установлено в процедуре)
                    passwordOk = true;
                    dialogClosed = true;
                    break;
                }
                if (result == 2) {          // отмена или WM_CLOSE
                    passwordOk = false;
                    dialogClosed = true;
                    break;
                }
            }
            else {
                g_logger.warning(L"showAdminPasswordDialog: GetMessage returned <= 0, exiting loop");
                break;
            }
        }

        if (!dialogClosed && !IsWindow(hDlg)) {
            g_logger.warning(L"showAdminPasswordDialog: dialog destroyed unexpectedly");
            passwordOk = false;
        }

        // Явно уничтожаем окно диалога после выхода из цикла, если оно ещё существует
        if (IsWindow(hDlg)) {
            DestroyWindow(hDlg);
        }


        DeleteObject(hFont);
        EnableWindow(m_hWnd, TRUE);
        SetForegroundWindow(m_hWnd);
        g_logger.info(L"showAdminPasswordDialog: loop finished, returning " + std::wstring(passwordOk ? L"true" : L"false"));
        return passwordOk;
    }

    std::wstring normalizePhone(const std::wstring& input) {
        std::wstring result;
        bool hasPlus = false;
        for (wchar_t ch : input) {
            if (ch == L'+') { if (!hasPlus && result.empty()) { hasPlus = true; result += ch; } }
            else if (ch >= L'0' && ch <= L'9') { result += ch; }
        }
        if (!hasPlus && !result.empty()) result = L"+" + result;
        if (result.length() < 11) return L"";
        return result;
    }

    std::wstring trim(const std::wstring& s) {
        size_t start = s.find_first_not_of(L" \t");
        size_t end = s.find_last_not_of(L" \t");
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

public:
    LoginWindow()
        : m_hWnd(nullptr), m_hPhoneEdit(nullptr), m_hStatusLabel(nullptr), m_hSubmitBtn(nullptr),
        m_hFontTitle(nullptr), m_hFontButton(nullptr), m_hFontLabel(nullptr), m_hFontEdit(nullptr),
        m_hGreenBrush(nullptr), m_hRedBrush(nullptr), m_hWhiteBrush(nullptr), m_hGrayBrush(nullptr),
        m_origEditProc(nullptr), m_resultCode(RESULT_NETWORK_ERROR), m_clientId(0),
        m_loginSuccess(false), m_isAuthenticating(false) {
        g_logger.info(L"LoginWindow: constructor");
    }

    bool show(HWND hParent) {
        g_logger.info(L"LoginWindow::show() entered");
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
            if (!IsWindow(m_hWnd)) break;
        }

        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        g_logger.info(L"LoginWindow: closed, success=" + std::wstring(m_loginSuccess ? L"true" : L"false"));
        return m_loginSuccess;
    }
};
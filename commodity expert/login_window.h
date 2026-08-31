// login_window.h (МОДИФИЦИРОВАННАЯ ВЕРСИЯ С ПОДДЕРЖКОЙ ПАРОЛЕЙ)
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
        ID_KEY_PLUS, ID_KEY_BACKSPACE, ID_KEY_CLEAR,
        ID_KEY_MODE, // Переключение между телефоном и паролем
        ID_KEY_CASE,    // переключение регистра (abc <-> ABC)
        ID_KEY_DIGITS,  // переключение цифры <-> буквы (цель НЕ меняется)
        // Буквы для пароля
        ID_KEY_A = 1100, ID_KEY_B, ID_KEY_C, ID_KEY_D, ID_KEY_E,
        ID_KEY_F, ID_KEY_G, ID_KEY_H, ID_KEY_I, ID_KEY_J,
        ID_KEY_K, ID_KEY_L, ID_KEY_M, ID_KEY_N, ID_KEY_O,
        ID_KEY_P, ID_KEY_Q, ID_KEY_R, ID_KEY_S, ID_KEY_T,
        ID_KEY_U, ID_KEY_V, ID_KEY_W, ID_KEY_X, ID_KEY_Y, ID_KEY_Z, ID_KEY_EYE = 1200   // кнопка-"глаз" (показать/скрыть пароль)
    };

    enum LoginResultCode {
        RESULT_NETWORK_ERROR = 0,
        RESULT_SUCCESS = 1,
        RESULT_NOT_FOUND = 2,
        RESULT_BLOCKED = 3,
        RESULT_INVALID_PASSWORD = 4  // НОВЫЙ: неверный пароль
    };

    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hPasswordEdit;  // поле для пароля

    HWND  m_hEyeBtn;          // кнопка-"глаз" показа/скрытия пароля
    bool  m_passwordVisible;  // true = пароль отображается открытым текстом
    WCHAR m_passwordChar;     // исходный маскирующий символ поля (для точного восстановления)
    
    HWND m_hStatusLabel;
    HWND m_hSubmitBtn;
    std::vector<HWND> m_allKeyButtons;
    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush, m_hWhiteBrush, m_hGrayBrush;
    WNDPROC m_origEditProc;
    WNDPROC m_origPhoneProc, m_origPasswordProc;
    int m_resultCode;
    int m_clientId;
    std::wstring m_fullName;
    std::wstring m_normalizedPhone;
    std::string m_enteredPassword;  // введенный пароль
    bool m_loginSuccess;
    std::atomic<bool> m_isAuthenticating;
    bool m_passwordMode;  // режим ввода пароля (true) или телефона (false)
    int m_keyLayout;      // раскладка: 0=цифры, 1=нижний регистр, 2=верхний регистр
    static constexpr int KEYBOARD_START_Y = 190; // единая Y-координата клавиатуры

    static constexpr const wchar_t* ADMIN_PASSWORD = L"123";
    static constexpr int KEY_WIDTH = 80;
    static constexpr int KEY_HEIGHT = 60;
    static constexpr int KEY_SPACING = 8;

    // ---- Оконная процедура для диалога ввода пароля администратора ----
    static LRESULT CALLBACK AdminPasswordDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                if (id == 2) { // OK
                    HWND hEdit = GetDlgItem(hDlg, 1);
                    wchar_t pwd[64];
                    GetWindowTextW(hEdit, pwd, 64);
                    std::wstring entered = pwd;
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
                        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)1);
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
                    SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0);
                    return TRUE;
                }
            }
            break;
        }
        case WM_CLOSE: {
            g_logger.info(L"AdminPasswordDialog: WM_CLOSE received");
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0);
            return TRUE;
        }
        }
        return DefWindowProcW(hDlg, msg, wParam, lParam);
    }

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
			g_logger.info(L"LoginWindow: WM_CREATE received, lpCreateParams=" + std::to_wstring((LONG_PTR)cs->lpCreateParams));
            pThis = reinterpret_cast<LoginWindow*>(cs->lpCreateParams);
			g_logger.info(L"LoginWindow: WM_CREATE, pThis=" + std::to_wstring((LONG_PTR)pThis));
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
			g_logger.info(L"LoginWindow: WM_CREATE, GWLP_USERDATA set to " + std::to_wstring((LONG_PTR)pThis));
            pThis->m_hWnd = hWnd;
			g_logger.info(L"LoginWindow: WM_CREATE, m_hWnd set to " + std::to_wstring((LONG_PTR)pThis->m_hWnd));
            pThis->createControls();
			g_logger.info(L"LoginWindow: createControls() completed");
            g_logger.info(L"LoginWindow: WM_CREATE processed");
            return 0;
        }
        pThis = reinterpret_cast<LoginWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		g_logger.info(L"LoginWindow: WndProc called, msg=" + std::to_wstring(msg) + L", pThis=" + std::to_wstring((LONG_PTR)pThis));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);
		g_logger.info(L"LoginWindow: WndProc, pThis is valid, processing message " + std::to_wstring(msg));
        switch (msg) {
			g_logger.info(L"LoginWindow: WndProc, processing message " + std::to_wstring(msg));
            case WM_COMMAND: 
                pThis->onCommand(wParam); 
				g_logger.info(L"LoginWindow: WM_COMMAND processed");
                return 0;
            case WM_CTLCOLORBTN: 
                return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);
				g_logger.log(LogLevel::DEBUG, L"LoginWindow: WM_CTLCOLORBTN processed");
            case WM_CTLCOLORSTATIC: 
                return pThis->onCtlColorStatic((HDC)wParam);
				g_logger.log(LogLevel::DEBUG, L"LoginWindow: WM_CTLCOLORSTATIC processed");
            case WM_CTLCOLOREDIT: 
                return pThis->onCtlColorEdit((HDC)wParam, (HWND)lParam);
				g_logger.log(LogLevel::DEBUG, L"LoginWindow: WM_CTLCOLOREDIT processed");
            case WM_LOGIN_RESULT: 
                pThis->onLoginResult(); 
				g_logger.log(LogLevel::DEBUG, L"LoginWindow: WM_LOGIN_RESULT processed");
                return 0;
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
        int startY = 20;
        // Шрифты и кисти — без изменений (создаются здесь же, как в текущей версии)
        m_hFontTitle = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontButton = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontLabel = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontEdit = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hGreenBrush = CreateSolidBrush(Config::PRIMARY_COLOR);
        m_hRedBrush = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        m_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        m_hGrayBrush = CreateSolidBrush(RGB(80, 80, 80));
        // 1) Заголовок (Y 20..70)
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Вход в систему", WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 150, startY, 300, 50, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
        startY += 60;                                   // = 80
        // 2) Телефон (Y 80..120)
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Номер телефона:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
            centerX - 200, startY, 180, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        m_hPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            centerX + 20, startY, 200, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hPhoneEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        m_origEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc)));
        SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(m_origEditProc));
        startY += 50;                                   // = 130
        // 3) Пароль (Y 130..170)
        HWND hPwdLabel = CreateWindowExW(0, L"STATIC", L"Пароль:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
            centerX - 200, startY, 180, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hPwdLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        m_hPasswordEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD,
            centerX + 20, startY, 150, 40, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hPasswordEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
                
        m_hEyeBtn = CreateWindowExW(0, L"BUTTON", L"👁",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY,
            centerX + 175, startY, 45, 40, m_hWnd, (HMENU)(INT_PTR)ID_KEY_EYE, g_hInstance, nullptr);
        SendMessageW(m_hEyeBtn, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        // Запоминаем ИСХОДНЫЙ маскирующий символ ES_PASSWORD, чтобы при скрытии
        // восстанавливать точно его, а не произвольный.
        m_passwordChar = (wchar_t)SendMessageW(m_hPasswordEdit, EM_GETPASSWORDCHAR, 0, 0);
        if (m_passwordChar == 0) m_passwordChar = L'*';
        g_logger.info(L"LoginWindow: eye-button created at x=" + std::to_wstring(centerX + 175) +
            L", y=" + std::to_wstring(startY) +
            L" (same row as password field, right side), original password char saved");

        startY += 60;                                   // = 190: клавиатура ПОСЛЕ полей
        // 4) Клавиатура на фиксированной Y; резервируем МАКСИМУМ 5 рядов
        //    (буквенная раскладка пароля = 5 рядов), чтобы статус и «Войти»
        //    не зависели от текущей раскладки.
        createKeyboard(centerX, KEYBOARD_START_Y);
        startY = KEYBOARD_START_Y + 5 * (KEY_HEIGHT + KEY_SPACING);   // 190+340 = 530
        startY += 8;                                                  // = 538
        // 5) статус-метка под клавиатурой (Y 538..568)
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 200, startY, 400, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        startY += 40;                                                 // = 578
        // 6) Кнопка «Войти» (Y 578..628)
        m_hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Войти", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 100, startY, 200, 50, m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(m_hSubmitBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        startY += 50;                                                 // = 628 — низ последней кнопки
        g_logger.info(L"LoginWindow: layout complete, required client height=" + std::to_wstring(startY));
        if (startY > (rc.bottom - rc.top)) {
            g_logger.warning(L"LoginWindow: required height " + std::to_wstring(startY) +
                L" exceeds current client height " + std::to_wstring(rc.bottom - rc.top) +
                L" — window will be resized by show()");
        }
    }

    void createKeyboard(int centerX, int startY) {
        struct KeyDef { int row, col; int id; const wchar_t* label; };
        // НОВОЕ: полные наборы обоих регистров (все 26 букв)
        static const wchar_t* UPPER[26] = { L"A",L"B",L"C",L"D",L"E",L"F",L"G",L"H",L"I",L"J",L"K",L"L",L"M",
            L"N",L"O",L"P",L"Q",L"R",L"S",L"T",L"U",L"V",L"W",L"X",L"Y",L"Z" };
        static const wchar_t* LOWER[26] = { L"a",L"b",L"c",L"d",L"e",L"f",L"g",L"h",L"i",L"j",L"k",L"l",L"m",
            L"n",L"o",L"p",L"q",L"r",L"s",L"t",L"u",L"v",L"w",L"x",L"y",L"z" };
        int keyW = KEY_WIDTH, sp = KEY_SPACING, cols = 4;
        std::vector<KeyDef> keys;

        if (!m_passwordMode) {
            // ЦЕЛЬ=ТЕЛЕФОН: прежняя цифровая сетка (поведение не меняется)
            keys = {
                {0,0,ID_KEY_1,L"1"},{0,1,ID_KEY_2,L"2"},{0,2,ID_KEY_3,L"3"},{0,3,ID_KEY_PLUS,L"+"},
                {1,0,ID_KEY_4,L"4"},{1,1,ID_KEY_5,L"5"},{1,2,ID_KEY_6,L"6"},{1,3,ID_KEY_BACKSPACE,L"⌫"},
                {2,0,ID_KEY_7,L"7"},{2,1,ID_KEY_8,L"8"},{2,2,ID_KEY_9,L"9"},{2,3,ID_KEY_CLEAR,L"✕"},
                {3,1,ID_KEY_0,L"0"},
                {3,3,ID_KEY_MODE,L"ABC"}        // переход к полю пароля
            };
        }
        else if (m_keyLayout == 0) {
            // ЦЕЛЬ=ПАРОЛЬ, РАСКЛАДКА=ЦИФРЫ: цифры идут В ПАРОЛЬ (курсор не прыгает)
            keys = {
                {0,0,ID_KEY_1,L"1"},{0,1,ID_KEY_2,L"2"},{0,2,ID_KEY_3,L"3"},{0,3,ID_KEY_BACKSPACE,L"⌫"},
                {1,0,ID_KEY_4,L"4"},{1,1,ID_KEY_5,L"5"},{1,2,ID_KEY_6,L"6"},{1,3,ID_KEY_CLEAR,L"✕"},
                {2,0,ID_KEY_7,L"7"},{2,1,ID_KEY_8,L"8"},{2,2,ID_KEY_9,L"9"},{2,3,ID_KEY_DIGITS,L"abc"},
                {3,1,ID_KEY_0,L"0"},
                {3,3,ID_KEY_MODE,L"Телефон"}    // явный переход к полю телефона
            };
        }
        else {
            // ЦЕЛЬ=ПАРОЛЬ, РАСКЛАДКА=БУКВЫ: ВСЕ 26 букв текущего регистра (7 колонок)
            keyW = 56; sp = 5; cols = 7;
            const wchar_t** set = (m_keyLayout == 2) ? UPPER : LOWER;
            for (int i = 0; i < 26; ++i) {
                keys.push_back({ i / 7, i % 7, ID_KEY_A + i, set[i] });
            }
            keys.push_back({ 3, 5, ID_KEY_BACKSPACE, L"⌫" });
            keys.push_back({ 3, 6, ID_KEY_CLEAR, L"✕" });
            keys.push_back({ 4, 0, ID_KEY_DIGITS, L"123" });                              // цифры БЕЗ смены цели
            keys.push_back({ 4, 1, ID_KEY_CASE, (m_keyLayout == 2) ? L"abc" : L"ABC" });  // регистр
            keys.push_back({ 4, 2, ID_KEY_MODE, L"Телефон" });                            // смена цели
        }

        const int totalWidth = cols * (keyW + sp) - sp;
        const int startX = centerX - totalWidth / 2;
        for (const auto& k : keys) {
            int x = startX + k.col * (keyW + sp);
            int y = startY + k.row * (KEY_HEIGHT + KEY_SPACING);
            HWND hBtn = CreateWindowExW(0, L"BUTTON", k.label,
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY,
                x, y, keyW, KEY_HEIGHT, m_hWnd, (HMENU)(INT_PTR)k.id, g_hInstance, nullptr);
            if (hBtn) {
                SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
                m_allKeyButtons.push_back(hBtn);
            }
        }
        g_logger.info(L"LoginWindow: keyboard rebuilt, target=" +
            std::wstring(m_passwordMode ? L"password" : L"phone") +
            L", layout=" + std::to_wstring(m_keyLayout) +
            L", keys=" + std::to_wstring(keys.size()));
    }

    // пересоздание клавиатуры на фиксированной позиции (устраняет рассинхрон Y=210 против Y=190 в прежнем коде)
    void rebuildKeyboard() {
        for (HWND hBtn : m_allKeyButtons) DestroyWindow(hBtn);
        m_allKeyButtons.clear();
        RECT rc; GetClientRect(m_hWnd, &rc);
        createKeyboard((rc.right - rc.left) / 2, KEYBOARD_START_Y);
    }

    void releaseResources() {
        g_logger.info(L"LoginWindow: releaseResources started");
        if (m_hPhoneEdit && m_origPhoneProc) {
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_origPhoneProc));
            SetWindowLongPtrW(m_hPhoneEdit, GWLP_USERDATA, 0);
            m_origPhoneProc = nullptr;
        }
        if (m_hPasswordEdit && m_origPasswordProc) {
            SetWindowLongPtrW(m_hPasswordEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_origPasswordProc));
            SetWindowLongPtrW(m_hPasswordEdit, GWLP_USERDATA, 0);
            m_origPasswordProc = nullptr;
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
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)m_hWhiteBrush;   // ИСПРАВЛЕНИЕ: было GetStockObject(NULL_BRUSH)
    }

    LRESULT onCtlColorEdit(HDC hdc, HWND hEdit) {
        if (hEdit == m_hPhoneEdit || hEdit == m_hPasswordEdit) {
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)m_hWhiteBrush;
        }
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
        // === кнопка-"глаз" — показать/скрыть пароль ===
        if (cmd == ID_KEY_EYE) { 
            onTogglePasswordVisibility(); 
            return; 
        }
        if ((cmd >= ID_KEY_1 && cmd <= ID_KEY_CLEAR) ||
            (cmd >= ID_KEY_A && cmd <= ID_KEY_Z) ||
            cmd == ID_KEY_MODE || cmd == ID_KEY_CASE || cmd == ID_KEY_DIGITS) {
            onKeyPress(cmd); return;
        }
        if (cmd == 1) { onSubmit(); return; }
    }

    void onTogglePasswordVisibility() {
        m_passwordVisible = !m_passwordVisible;
        g_logger.info(L"LoginWindow: eye-button pressed, password now " +
            std::wstring(m_passwordVisible ? L"VISIBLE" : L"HIDDEN"));
        // Показ открытым текстом (0) либо восстановление исходной маскировки
        SendMessageW(m_hPasswordEdit, EM_SETPASSWORDCHAR,
            m_passwordVisible ? 0 : (WPARAM)m_passwordChar, 0);
        // Каретка — строго в конец введённого текста (см. раздел II)
        const int len = GetWindowTextLengthW(m_hPasswordEdit);
        SendMessageW(m_hPasswordEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        InvalidateRect(m_hPasswordEdit, nullptr, TRUE);   // гарантированная перерисовка поля
        SetFocus(m_hPasswordEdit);                        // фокус остаётся в поле пароля
        // Смена пиктограммы: 👁 (скрыто) / 🙈 (показано)
        SetWindowTextW(m_hEyeBtn, m_passwordVisible ? L"🙈" : L"👁");
        g_logger.info(L"LoginWindow: eye-button icon updated, caret pinned to end (" +
            std::to_wstring(len) + L")");
    }

    void onKeyPress(int id) {
        if (id == ID_KEY_MODE) {
            // Смена ЦЕЛЕВОГО ПОЛЯ: телефон <-> пароль; фокус следует за целью
            m_passwordMode = !m_passwordMode;
            m_keyLayout = m_passwordMode ? 1 : 0;   // пароль стартует с букв, телефон с цифр
            rebuildKeyboard();
            HWND hTarget = (GetFocus() == m_hPasswordEdit) ? m_hPasswordEdit : m_hPhoneEdit;
            SetFocus(hTarget);
            // === ИСПРАВЛЕНИЕ: каретка в конец текста после возврата фокуса ===
            const int len = GetWindowTextLengthW(hTarget);
            SendMessageW(hTarget, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SetWindowTextW(m_hStatusLabel, m_passwordMode ? L"Режим ввода пароля" : L"Режим ввода телефона");
            return;
        }
        // переключение регистра — цель и фокус НЕ меняются
        if (id == ID_KEY_CASE) {
            m_keyLayout = (m_keyLayout == 2) ? 1 : 2;
            rebuildKeyboard();
            SetFocus(m_hPasswordEdit);
            // === ИСПРАВЛЕНИЕ: каретка в конец текста после возврата фокуса ===
            const int len = GetWindowTextLengthW(m_hPasswordEdit);
            SendMessageW(m_hPasswordEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            g_logger.info(L"LoginWindow: case switched, layout=" + std::to_wstring(m_keyLayout) +
                L", caret pinned to end (" + std::to_wstring(len) + L")");
            return;
        }
        // «123»/«abc» переключает РАСКЛАДКУ, а не целевое поле.
        // Курсор остаётся в поле пароля — цифры вводятся в пароль.
        if (id == ID_KEY_DIGITS) {
            m_keyLayout = (m_keyLayout == 0) ? 1 : 0;
            rebuildKeyboard();
            HWND hTarget = m_passwordMode ? m_hPasswordEdit : m_hPhoneEdit;
            SetFocus(hTarget);
            // === ИСПРАВЛЕНИЕ: каретка в конец текста после возврата фокуса ===
            const int len = GetWindowTextLengthW(hTarget);
            SendMessageW(hTarget, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            g_logger.info(L"LoginWindow: digits/letters switched, layout=" + std::to_wstring(m_keyLayout) +
                L", caret pinned to end (" + std::to_wstring(len) + L")");
            return;
        }
        HWND targetEdit = m_passwordMode ? m_hPasswordEdit : m_hPhoneEdit;
        wchar_t currentText[64] = {};
        GetWindowTextW(targetEdit, currentText, 64);
        std::wstring text = currentText;
        switch (id) {
        case ID_KEY_BACKSPACE:
            if (!text.empty()) {
                text.pop_back();
                SetWindowTextW(targetEdit, text.c_str());
            } break;
        case ID_KEY_CLEAR:
            text.clear();
            SetWindowTextW(targetEdit, L"");
            break;
        case ID_KEY_PLUS:
            if (!m_passwordMode && text.empty()) {
                text = L"+";
                SetWindowTextW(targetEdit, text.c_str());
            } break;
        default: {
            wchar_t ch = 0;
            if (id >= ID_KEY_1 && id <= ID_KEY_9) ch = L'0' + (id - ID_KEY_1 + 1);
            else if (id == ID_KEY_0) ch = L'0';
            else if (id >= ID_KEY_A && id <= ID_KEY_Z)
                // регистр берётся из раскладки (ранее всегда нижний)
                ch = (m_keyLayout == 2) ? L'A' + (id - ID_KEY_A) : L'a' + (id - ID_KEY_A);
            else return;
            int maxLen = m_passwordMode ? 32 : 15;
            if (text.length() < maxLen) {
                text += ch;
                SetWindowTextW(targetEdit, text.c_str());
            }
            break;
        }
        }
        // === ИСПРАВЛЕНИЕ: после ЛЮБОЙ модификации текста фиксируем каретку
        // === В КОНЦЕ текста, чтобы WM_SETTEXT не оставлял её в позиции 0.
        // === Тогда при последующем SetFocus() пользователь видит курсор
        // === в конце введённого пароля, а не в начале.
        SendMessageW(targetEdit, EM_SETSEL, (WPARAM)text.length(), (LPARAM)text.length());
        SetWindowTextW(m_hStatusLabel, L"");
        InvalidateRect(targetEdit, nullptr, TRUE);
    }

    void onSubmit() {
        if (m_isAuthenticating.exchange(true)) {
            g_logger.info(L"LoginWindow: onSubmit already in progress");
            return;
        }

        wchar_t phoneBuf[30] = {};
        GetWindowTextW(m_hPhoneEdit, phoneBuf, 30);
        std::wstring phone = trim(phoneBuf);

        wchar_t passwordBuf[64] = {};
        GetWindowTextW(m_hPasswordEdit, passwordBuf, 64);
        m_enteredPassword = wstring_to_utf8(passwordBuf);

        if (phone.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Введите номер телефона");
            m_isAuthenticating = false;
            return;
        }

        if (m_enteredPassword.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Введите пароль");
            m_isAuthenticating = false;
            return;
        }

        std::wstring normalized = normalizePhone(phone);
        if (normalized.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Неверный формат телефона");
            m_isAuthenticating = false;
            return;
        }

        g_authManager.setLoginAttempted(true, wstring_to_utf8(normalized));
        SetWindowTextW(m_hStatusLabel, L"Проверка данных...");
        EnableWindow(m_hSubmitBtn, FALSE);
        for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, FALSE);

        g_logger.info(L"LoginWindow: authentication started for " + normalized);

        HWND hWndCopy = m_hWnd;
        std::wstring phoneCopy = normalized;
        std::string passwordCopy = m_enteredPassword;

        std::thread([this, hWndCopy, phoneCopy, passwordCopy]() {
            g_logger.info(L"LoginWindow: background thread started for " + phoneCopy);

            nlohmann::json request;
            request["phone"] = wstring_to_utf8(phoneCopy);
            // НОВОЕ: читаем пароль из поля ввода и отправляем на сервер
            wchar_t pwdBuf[64] = {};
            GetWindowTextW(m_hPasswordEdit, pwdBuf, 64);
            request["password"] = wstring_to_utf8(std::wstring(pwdBuf));

            auto response = g_httpsClient.post(L"/api/v1/clients/by_phone", request, L"");

            if (!response) {
                g_logger.error(L"[LoginWindow] Network error for " + phoneCopy);
                m_resultCode = RESULT_NETWORK_ERROR;
                m_loginSuccess = false;
            }
            else if (response->contains("blocked") && (*response)["blocked"].get<bool>()) {
                g_logger.warning(L"[LoginWindow] Client is BLOCKED: " + phoneCopy);
                m_resultCode = RESULT_BLOCKED;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
            }
            else if (response->contains("error") && (*response)["error"].get<std::string>().find("Invalid password") != std::string::npos) {
                g_logger.warning(L"[LoginWindow] Invalid password for " + phoneCopy);
                m_resultCode = RESULT_INVALID_PASSWORD;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
            }
            else if (response->contains("id") && response->contains("name") &&
                response->contains("access_token") && response->contains("refresh_token")) {
                m_resultCode = RESULT_SUCCESS;
                m_loginSuccess = true;
                m_clientId = (*response)["id"].get<int>();
                m_fullName = utf8_to_wstring((*response)["name"].get<std::string>());
                m_normalizedPhone = phoneCopy;
                //сохраняем роль из ответа сервера
                if (response->contains("role") && (*response)["role"].is_string()) {
                    g_authManager.setRole((*response)["role"].get<std::string>());
                }

                std::string accessToken = (*response)["access_token"].get<std::string>();
                std::string refreshToken = (*response)["refresh_token"].get<std::string>();
                int64_t expiresAt = response->contains("expires_at") && (*response)["expires_at"].is_number_integer()
                    ? (*response)["expires_at"].get<int64_t>()
                    : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

                g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt, wstring_to_utf8(m_normalizedPhone));
                g_logger.info(L"LoginWindow: success, clientId=" + std::to_wstring(m_clientId));
            }
            // обработка неверного пароля
            else if (response->contains("invalid_password") && (*response)["invalid_password"].get<bool>()) {
                m_resultCode = RESULT_INVALID_PASSWORD;
                m_loginSuccess = false;
                m_normalizedPhone = phoneCopy;
                g_logger.warning(L"LoginWindow: invalid password for " + phoneCopy);
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
            break;

        case RESULT_BLOCKED:
            MessageBoxW(m_hWnd, L"Ваш аккаунт заблокирован.\n\nОбратитесь к администрации магазина.",
                L"Доступ запрещён", MB_OK | MB_ICONERROR);
            SetWindowTextW(m_hPhoneEdit, L"");
            SetWindowTextW(m_hPasswordEdit, L"");
            SetWindowTextW(m_hStatusLabel, L"Аккаунт заблокирован");
            EnableWindow(m_hSubmitBtn, TRUE);
            for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
            m_isAuthenticating = false;
            break;

        case RESULT_INVALID_PASSWORD:
            MessageBoxW(m_hWnd, L"Неверный пароль.\n\nПроверьте правильность ввода.",
                L"Ошибка входа", MB_OK | MB_ICONERROR);
            SetWindowTextW(m_hPasswordEdit, L"");
            SetWindowTextW(m_hStatusLabel, L"Неверный пароль");
            EnableWindow(m_hSubmitBtn, TRUE);
            for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
            SetFocus(m_hPasswordEdit);
            m_isAuthenticating = false;
            break;

        case RESULT_SUCCESS:
            g_authManager.setLoggedIn(true, m_clientId, m_fullName, m_normalizedPhone);
            g_authManager.setLoginAttempted(false);
            SetWindowTextW(m_hStatusLabel, L"Успешный вход!");
            SetTimer(m_hWnd, TIMER_ID_CLOSE, 500, nullptr);
            break;

        case RESULT_NOT_FOUND: {
            g_authManager.setLoggedIn(false, 0, L"", m_normalizedPhone);
            g_logger.info(L"LoginWindow: phone not found, showing admin password dialog");
            bool passwordOk = showAdminPasswordDialog();
            if (passwordOk) {
                g_logger.info(L"LoginWindow: password correct, creating WorkerRegistrationWindow for " + m_normalizedPhone);

                // НОВОЕ: Определяем роль регистрируемого пользователя.
                // Если введён специальный пароль администратора для директора,
                // регистрируем с ролью "director". Иначе — "worker".
                // Это позволяет использовать существующий диалог пароля без
                // добавления новых окон.
                std::string registrationRole = "worker";

                // Показываем диалог выбора роли
                int roleChoice = MessageBoxW(m_hWnd,
                    L"Выберите роль регистрируемого пользователя:\n\n"
                    L"ДА — Директор магазина\n"
                    L"НЕТ — Товаровед",
                    L"Роль пользователя",
                    MB_YESNO | MB_ICONQUESTION);

                if (roleChoice == IDYES) {
                    registrationRole = "director";
                    g_logger.info(L"LoginWindow: registering DIRECTOR for " + m_normalizedPhone);
                }
                else {
                    registrationRole = "worker";
                    g_logger.info(L"LoginWindow: registering WORKER for " + m_normalizedPhone);
                }

                WorkerRegistrationWindow regWnd(m_normalizedPhone, registrationRole);
                
                int result = regWnd.show(m_hWnd);
                if (result == IDOK) {
                    g_logger.info(L"LoginWindow: registration successful, performing auto-login for " + m_normalizedPhone);
                    // ИСПРАВЛЕНИЕ: берём СГЕНЕРИРОВАННЫЙ сервером пароль из окна регистрации,
                    // а не то, что набрано в поле пароля до регистрации (этого хеша в БД нет).
                    std::string password = regWnd.getGeneratedPassword();
                    if (password.empty()) {
                        wchar_t passwordBuf[64] = {};
                        GetWindowTextW(m_hPasswordEdit, passwordBuf, 64);
                        password = wstring_to_utf8(passwordBuf);
                    }
                    nlohmann::json request;
                    request["phone"] = wstring_to_utf8(m_normalizedPhone);
                    request["password"] = password;
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
                        // ИСПРАВЛЕНИЕ: сохраняем роль из ответа, иначе main.cpp после
                        // автовхода не отличит директора от товароведа
                        if (response->contains("role") && (*response)["role"].is_string()) {
                            g_authManager.setRole((*response)["role"].get<std::string>());
                        }
                        g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt, wstring_to_utf8(m_normalizedPhone));
                        g_authManager.setLoggedIn(true, clientId, fullName, m_normalizedPhone);
                        m_clientId = clientId;
                        m_fullName = fullName;
                        m_loginSuccess = true;
                        MessageBoxW(m_hWnd, L"Регистрация завершена, вход выполнен!", L"Успех", MB_OK);
                        DestroyWindow(m_hWnd);
                    }
                    else {
                        MessageBoxW(m_hWnd, L"Регистрация прошла, но не удалось выполнить вход. Войдите с выданным паролем.", L"Ошибка", MB_OK);
                        m_isAuthenticating = false;
                        EnableWindow(m_hSubmitBtn, TRUE);
                        for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                    }
                }
                else {
                    SetWindowTextW(m_hStatusLabel, L"Регистрация отменена");
                    EnableWindow(m_hSubmitBtn, TRUE);
                    for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                    m_isAuthenticating = false;
                }
            }
            else {
                SetWindowTextW(m_hStatusLabel, L"Неверный пароль администратора");
                EnableWindow(m_hSubmitBtn, TRUE);
                for (HWND hBtn : m_allKeyButtons) EnableWindow(hBtn, TRUE);
                m_isAuthenticating = false;
            }
            break;
        }
        }
    }

    bool showAdminPasswordDialog() {
        g_logger.info(L"showAdminPasswordDialog: entered");
        static bool dialogClassRegistered = false;
        if (!dialogClassRegistered) {
            WNDCLASSEXW dlgClass = {};
            dlgClass.cbSize = sizeof(WNDCLASSEX);
            dlgClass.style = CS_HREDRAW | CS_VREDRAW;
            dlgClass.lpfnWndProc = AdminPasswordDialogProc;
            dlgClass.hInstance = g_hInstance;
            dlgClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
            dlgClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            dlgClass.lpszClassName = L"AdminPasswordDialogClass";
            if (!RegisterClassExW(&dlgClass)) {
                g_logger.error(L"showAdminPasswordDialog: failed to register dialog class");
                return false;
            }
            dialogClassRegistered = true;
        }

        HWND hDlg = CreateWindowExW(
            WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME,
            L"AdminPasswordDialogClass",
            L"Пароль администратора",
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

        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Пароль администратора:", WS_VISIBLE | WS_CHILD, 20, 30, 150, 30, hDlg, nullptr, g_hInstance, nullptr);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_PASSWORD, 180, 30, 180, 30, hDlg, (HMENU)1, g_hInstance, nullptr);
        HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 80, 100, 100, 40, hDlg, (HMENU)2, g_hInstance, nullptr);
        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Отмена", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 220, 100, 100, 40, hDlg, (HMENU)3, g_hInstance, nullptr);

        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)0);
        EnableWindow(m_hWnd, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        SetFocus(hEdit);

        bool passwordOk = false;
        bool dialogClosed = false;
        MSG msg;

        while (!dialogClosed && IsWindow(hDlg)) {
            if (GetMessage(&msg, nullptr, 0, 0) > 0) {
                if (!IsDialogMessage(hDlg, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
                if (!IsWindow(hDlg)) break;
                LONG_PTR result = GetWindowLongPtr(hDlg, GWLP_USERDATA);
                if (result == 1) {
                    passwordOk = true;
                    dialogClosed = true;
                    break;
                }
                if (result == 2) {
                    passwordOk = false;
                    dialogClosed = true;
                    break;
                }
            }
            else {
                break;
            }
        }

        if (IsWindow(hDlg)) {
            DestroyWindow(hDlg);
        }
        DeleteObject(hFont);
        EnableWindow(m_hWnd, TRUE);
        SetForegroundWindow(m_hWnd);
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
        : m_hWnd(nullptr), m_hPhoneEdit(nullptr), m_hPasswordEdit(nullptr), m_hStatusLabel(nullptr), m_hSubmitBtn(nullptr),
        m_hFontTitle(nullptr), m_hFontButton(nullptr), m_hFontLabel(nullptr), m_hFontEdit(nullptr),
        m_hGreenBrush(nullptr), m_hRedBrush(nullptr), m_hWhiteBrush(nullptr), m_hGrayBrush(nullptr), m_origEditProc(nullptr),
        m_origPhoneProc(nullptr), m_origPasswordProc(nullptr), m_resultCode(RESULT_NETWORK_ERROR), m_clientId(0),
        m_loginSuccess(false), m_isAuthenticating(false), m_passwordMode(false), m_keyLayout(0), m_hEyeBtn(nullptr), m_passwordVisible(false), m_passwordChar(L'*') {
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
            if (!RegisterClassExW(&wcex)) { g_logger.error(L"LoginWindow: RegisterClassExW failed"); return false; }
            classRegistered = true;
        }

        // =========================================================================
        // ИСПРАВЛЕНИЕ: требуемая КЛИЕНТская высота = 570 (низ кнопки «Войти» 560 + запас 10),
        // ширина клиента 500. AdjustWindowRectW добавляет НЕклиентскую область
        // (заголовок, рамку) для ТОГО ЖЕ стиля, с которым создаётся окно.
        // Это исключает обрезание кнопки «Войти» при любой теме/DPI.
        // =========================================================================
        const DWORD dwStyle = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
        const int CLIENT_WIDTH = 500;
        const int CLIENT_HEIGHT = 670;
        const int ncWidth = 2 * (GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER));
        const int ncHeight = GetSystemMetrics(SM_CYCAPTION)
            + 2 * (GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER));
        int width = CLIENT_WIDTH + ncWidth;
        int height = CLIENT_HEIGHT + ncHeight;
        g_logger.info(L"LoginWindow: computed window size = " + std::to_wstring(width) +
            L"x" + std::to_wstring(height) +
            L" (client " + std::to_wstring(CLIENT_WIDTH) + L"x" + std::to_wstring(CLIENT_HEIGHT) + L")");

        int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, L"LoginWindowClass", L"Вход",
            dwStyle, x, y, width, height, hParent, nullptr, g_hInstance, this);
        if (!m_hWnd) { g_logger.error(L"LoginWindow: Failed to create window"); return false; }

        EnableWindow(hParent, FALSE);
        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) break;
        }
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
        return m_loginSuccess;
    }
};
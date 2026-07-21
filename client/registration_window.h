// registration_window.h
// Окно регистрации клиента с поддержкой перехода к TOTP-аутентификации для существующих пользователей
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "config.h"
#include "logger.h"
#include "database_local.h"
#include "https_client.h"
#include "auth_manager.h"
#include "string_utils.h"

using json = nlohmann::json;

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern AuthManager g_authManager;
extern HINSTANCE g_hInstance;

namespace RegUtils {
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

    inline bool isValidName(const std::wstring& name, size_t maxLen) {
        if (name.empty() || name.length() > maxLen) return false;
        bool hasLetter = false;
        for (wchar_t ch : name) {
            if (iswalpha(ch)) hasLetter = true;
            else if (ch != L' ' && ch != L'-' && ch != L'\'') return false;
        }
        return hasLetter;
    }

    inline bool isValidEmail(const std::wstring& email) {
        if (email.empty() || email.length() > 30) return false;
        auto atPos = email.find(L'@');
        if (atPos == std::wstring::npos || atPos == 0 || atPos == email.length() - 1) return false;
        auto dotPos = email.find(L'.', atPos);
        if (dotPos == std::wstring::npos || dotPos == email.length() - 1) return false;
        return true;
    }

    inline std::wstring trim(const std::wstring& s) {
        size_t start = s.find_first_not_of(L" \t");
        size_t end = s.find_last_not_of(L" \t");
        if (start == std::wstring::npos) return L"";
        return s.substr(start, end - start + 1);
    }
}

#define IDC_REG_TITLE           5001
#define IDC_REG_PHONE_LABEL     5002
#define IDC_REG_PHONE           5003
#define IDC_REG_LASTNAME_LABEL  5004
#define IDC_REG_LASTNAME        5005
#define IDC_REG_FIRSTNAME_LABEL 5006
#define IDC_REG_FIRSTNAME       5007
#define IDC_REG_MIDDLENAME_LABEL 5008
#define IDC_REG_MIDDLENAME      5009
#define IDC_REG_EMAIL_LABEL     5010
#define IDC_REG_EMAIL           5011
#define IDC_REG_STATUS          5012
#define IDC_REG_SUBMIT          5013
#define IDC_REG_BACK            5014
#define IDC_REG_CLOSE           5015
// ✅ ДОБАВЛЕНЫ ID для элементов TOTP-аутентификации
#define IDC_TOTP_CODE_LABEL     5016
#define IDC_TOTP_CODE_EDIT      5017
#define IDC_TOTP_VERIFY_BTN     5018

class RegistrationWindow {
private:
    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hLastNameEdit;
    HWND m_hFirstNameEdit;
    HWND m_hMiddleNameEdit;
    HWND m_hEmailEdit;
    HWND m_hStatusLabel;
    HWND m_hCloseBtn;

    // ✅ ДОБАВЛЕНЫ элементы для ввода TOTP кода
    HWND m_hTotpCodeLabel;
    HWND m_hTotpCodeEdit;
    HWND m_hTotpVerifyBtn;

    HFONT m_hFontTitle;
    HFONT m_hFontButton;
    HFONT m_hFontLabel;
    HFONT m_hFontEdit;
    HBRUSH m_hGreenBrush;
    HBRUSH m_hRedBrush;
    HBRUSH m_hWhiteBrush;
    std::vector<HWND> m_buttons;

    // ✅ ДОБАВЛЕНЫ переменные состояния
    bool m_isVerifyingTOTP;
    std::wstring m_currentPhone;

    static constexpr const wchar_t* CLASS_NAME = L"RegistrationWindowClass";

    void createFontsAndBrushes() {
        m_hFontTitle = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontButton = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontLabel = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontEdit = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hGreenBrush = CreateSolidBrush(Config::PRIMARY_COLOR);
        m_hRedBrush = CreateSolidBrush(Config::BACK_BUTTON_COLOR);
        m_hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    }

    void releaseFontsAndBrushes() {
        if (m_hFontTitle) { DeleteObject(m_hFontTitle);  m_hFontTitle = nullptr; }
        if (m_hFontButton) { DeleteObject(m_hFontButton); m_hFontButton = nullptr; }
        if (m_hFontLabel) { DeleteObject(m_hFontLabel);  m_hFontLabel = nullptr; }
        if (m_hFontEdit) { DeleteObject(m_hFontEdit);   m_hFontEdit = nullptr; }
        if (m_hGreenBrush) { DeleteObject(m_hGreenBrush); m_hGreenBrush = nullptr; }
        if (m_hRedBrush) { DeleteObject(m_hRedBrush);   m_hRedBrush = nullptr; }
        if (m_hWhiteBrush) { DeleteObject(m_hWhiteBrush); m_hWhiteBrush = nullptr; }
    }

    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Регистрация клиента", WS_VISIBLE | WS_CHILD | SS_CENTER, 0, 20, clientWidth, 100, m_hWnd, (HMENU)(INT_PTR)IDC_REG_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

        const int labelWidth = 200;
        const int editWidth = 400;
        const int editHeight = 40;
        const int rowHeight = 60;
        const int startX = (clientWidth - labelWidth - editWidth) / 2;
        const int startY = 140;

        auto createLabeledEdit = [&](int idLabel, int idEdit, const wchar_t* labelText, int row, DWORD editStyle, DWORD maxLen) -> HWND {
            HWND hLabel = CreateWindowExW(0, L"STATIC", labelText, WS_VISIBLE | WS_CHILD | SS_RIGHT, startX, startY + row * rowHeight, labelWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)idLabel, g_hInstance, nullptr);
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | editStyle, startX + labelWidth + 20, startY + row * rowHeight, editWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)idEdit, g_hInstance, nullptr);
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
            SendMessageW(hEdit, EM_SETLIMITTEXT, (WPARAM)maxLen, 0);
            return hEdit;
            };

        m_hPhoneEdit = createLabeledEdit(IDC_REG_PHONE_LABEL, IDC_REG_PHONE, L"Телефон:", 0, 0, 30);
        m_hLastNameEdit = createLabeledEdit(IDC_REG_LASTNAME_LABEL, IDC_REG_LASTNAME, L"Фамилия:", 1, 0, 40);
        m_hFirstNameEdit = createLabeledEdit(IDC_REG_FIRSTNAME_LABEL, IDC_REG_FIRSTNAME, L"Имя:", 2, 0, 20);
        m_hMiddleNameEdit = createLabeledEdit(IDC_REG_MIDDLENAME_LABEL, IDC_REG_MIDDLENAME, L"Отчество:", 3, 0, 20);
        m_hEmailEdit = createLabeledEdit(IDC_REG_EMAIL_LABEL, IDC_REG_EMAIL, L"E-mail:", 4, 0, 30);

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_CENTER, startX, startY + 5 * rowHeight + 10, labelWidth + editWidth + 20, 40, m_hWnd, (HMENU)(INT_PTR)IDC_REG_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        // ✅ СОЗДАНИЕ ЭЛЕМЕНТОВ TOTP (ИЗНАЧАЛЬНО СКРЫТЫ)
        m_hTotpCodeLabel = CreateWindowExW(0, L"STATIC", L"Код из приложения:", WS_VISIBLE | WS_CHILD | SS_RIGHT, startX, startY + 5 * rowHeight + 10, labelWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)IDC_TOTP_CODE_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hTotpCodeLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        m_hTotpCodeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, startX + labelWidth + 20, startY + 5 * rowHeight + 10, editWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)IDC_TOTP_CODE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hTotpCodeEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hTotpCodeEdit, EM_SETLIMITTEXT, (WPARAM)6, 0); // Ограничение до 6 символов

        m_hTotpVerifyBtn = CreateWindowExW(0, L"BUTTON", L"Подтвердить вход", WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX, startY + 6 * rowHeight + 10, 300, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_TOTP_VERIFY_BTN, g_hInstance, nullptr);
        SendMessageW(m_hTotpVerifyBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);

        // Скрываем элементы TOTP при инициализации
        ShowWindow(m_hTotpCodeLabel, SW_HIDE);
        ShowWindow(m_hTotpCodeEdit, SW_HIDE);
        ShowWindow(m_hTotpVerifyBtn, SW_HIDE);

        const int btnWidth = 300;
        const int btnY = startY + 5 * rowHeight + 20;

        HWND hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Зарегистрировать", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX, btnY, btnWidth, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_REG_SUBMIT, g_hInstance, nullptr);
        m_buttons.push_back(hSubmitBtn);

        HWND hBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX + labelWidth + editWidth + 20 - btnWidth, btnY, btnWidth, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_REG_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBackBtn);

        for (HWND btn : m_buttons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        }
    }

    void onSubmit() {
        // ✅ ВЕТВЛЕНИЕ ЛОГИКИ: Мы в режиме проверки TOTP или в режиме регистрации?
        if (m_isVerifyingTOTP) {
            wchar_t code[10];
			g_logger.info(L"Verifying TOTP code for existing user: " + m_currentPhone);
            GetWindowTextW(m_hTotpCodeEdit, code, 10);
            std::wstring codeStr(code);
			g_logger.log(LogLevel::INFO, L"User entered TOTP code: " + codeStr);

            if (codeStr.length() != 6) {
                SetWindowTextW(m_hStatusLabel, L"Код должен состоять из 6 цифр");
                SetFocus(m_hTotpCodeEdit);
                return;
            }

            SetWindowTextW(m_hStatusLabel, L"Проверка кода...");

            if (g_authManager.verifyTOTP(m_currentPhone, codeStr)) {
                SetWindowTextW(m_hStatusLabel, L"Успешная аутентификация!");
                g_logger.info(L"Existing user successfully authenticated via TOTP: " + m_currentPhone);
                Sleep(1000);
                DestroyWindow(m_hWnd);
            }
            else {
                SetWindowTextW(m_hStatusLabel, L"Неверный код TOTP. Попробуйте снова.");
                SetWindowTextW(m_hTotpCodeEdit, L""); // Очистить поле
                SetFocus(m_hTotpCodeEdit);
                g_logger.warning(L"TOTP verification failed for existing user: " + m_currentPhone);
            }
            return;
        }

        // --- ОБЫЧНАЯ ЛОГИКА РЕГИСТРАЦИИ ---
        wchar_t buf[128];
        GetWindowTextW(m_hPhoneEdit, buf, 128);     std::wstring phone = RegUtils::trim(buf);
        GetWindowTextW(m_hLastNameEdit, buf, 128);  std::wstring lastName = RegUtils::trim(buf);
        GetWindowTextW(m_hFirstNameEdit, buf, 128); std::wstring firstName = RegUtils::trim(buf);
        GetWindowTextW(m_hMiddleNameEdit, buf, 128); std::wstring middleName = RegUtils::trim(buf);
        GetWindowTextW(m_hEmailEdit, buf, 128);     std::wstring email = RegUtils::trim(buf);

        std::wstring normalizedPhone = RegUtils::normalizePhone(phone);
        if (normalizedPhone.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка: введите 10 или 11 цифр (например, +7-911-111-11-11)");
            SetFocus(m_hPhoneEdit);
            return;
        }
        if (!RegUtils::isValidName(lastName, 40)) {
            SetWindowTextW(m_hStatusLabel, L"Фамилия: от 1 до 40 букв");
            SetFocus(m_hLastNameEdit);
            return;
        }
        if (!RegUtils::isValidName(firstName, 20)) {
            SetWindowTextW(m_hStatusLabel, L"Имя: от 1 до 20 букв");
            SetFocus(m_hFirstNameEdit);
            return;
        }
        if (!middleName.empty() && !RegUtils::isValidName(middleName, 20)) {
            SetWindowTextW(m_hStatusLabel, L"Отчество: от 1 до 20 букв");
            SetFocus(m_hMiddleNameEdit);
            return;
        }
        if (!email.empty() && !RegUtils::isValidEmail(email)) {
            SetWindowTextW(m_hStatusLabel, L"E-mail: неверный формат");
            SetFocus(m_hEmailEdit);
            return;
        }

        EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), FALSE);
        SetWindowTextW(m_hStatusLabel, L"Обработка данных...");

        std::wstring fullName = lastName;
        if (!firstName.empty())  fullName += L" " + firstName;
        if (!middleName.empty()) fullName += L" " + middleName;

        bool localOk = LocalDB::addClient(normalizedPhone, fullName, email);
        if (!localOk) {
            g_logger.warning(L"LocalDB::addClient failed: " + normalizedPhone);
        }

        json request;
        request["phone"] = wstring_to_utf8(normalizedPhone);
        request["last_name"] = wstring_to_utf8(lastName);
        request["first_name"] = wstring_to_utf8(firstName);
        request["middle_name"] = wstring_to_utf8(middleName);
        request["email"] = wstring_to_utf8(email);
        request["items_submitted"] = 0;
        request["items_sold"] = 0;

        g_logger.info(L"Sending registration request to server for: " + normalizedPhone);
        auto response = g_httpsClient.post(L"/api/v1/clients/register", request);

        if (response && response->contains("success") && (*response)["success"].is_boolean() && (*response)["success"].get<bool>()) {

            bool alreadyExists = response->contains("already_exists") && (*response)["already_exists"].get<bool>();

            if (alreadyExists) {
                g_logger.info(L"User already exists: " + normalizedPhone + L". Switching to TOTP authentication...");

                // ✅ ИНИЦИИРУЕМ TOTP И ПЕРЕХОДИМ В РЕЖИМ ВВОДА КОДА
                auto totpResult = g_authManager.setupTOTP(normalizedPhone);
                if (totpResult.success) {
                    m_isVerifyingTOTP = true;
                    m_currentPhone = normalizedPhone;

                    // Скрываем поля регистрации и кнопку "Зарегистрировать"
                    ShowWindow(m_hPhoneEdit, SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_PHONE_LABEL), SW_HIDE);
                    ShowWindow(m_hLastNameEdit, SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_LASTNAME_LABEL), SW_HIDE);
                    ShowWindow(m_hFirstNameEdit, SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_FIRSTNAME_LABEL), SW_HIDE);
                    ShowWindow(m_hMiddleNameEdit, SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_MIDDLENAME_LABEL), SW_HIDE);
                    ShowWindow(m_hEmailEdit, SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_EMAIL_LABEL), SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), SW_HIDE);

                    // Показываем поля для ввода TOTP
                    ShowWindow(m_hTotpCodeLabel, SW_SHOW);
                    ShowWindow(m_hTotpCodeEdit, SW_SHOW);
                    ShowWindow(m_hTotpVerifyBtn, SW_SHOW);

                    SetWindowTextW(m_hStatusLabel, L"Пользователь уже зарегистрирован.\nВведите 6-значный код из вашего приложения-аутентификатора:");
                    SetWindowTextW(m_hTotpCodeEdit, L"");
                    SetFocus(m_hTotpCodeEdit);

                    g_logger.info(L"TOTP setup URI received for existing user. UI switched to code input.");
                }
                else {
                    SetWindowTextW(m_hStatusLabel, L"Ошибка инициализации TOTP. Попробуйте войти через главный экран.");
                    g_logger.error(L"TOTP setup failed for existing user: " + normalizedPhone);
                    EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), TRUE);
                }
            }
            else {
                // Это НОВЫЙ пользователь
                SetWindowTextW(m_hStatusLabel, L"Регистрация прошла успешно!");
                g_logger.info(L"New client registered on server: " + normalizedPhone);

                auto totpResult = g_authManager.setupTOTP(normalizedPhone);
                if (totpResult.success) {
                    g_logger.info(L"TOTP initialized successfully for new user: " + normalizedPhone);
                }
                Sleep(1500);
                DestroyWindow(m_hWnd);
            }
        }
        else {
            std::wstring errMsg = L"Ошибка регистрации на сервере";
            if (response && response->contains("error") && (*response)["error"].is_string()) {
                errMsg = utf8_to_wstring((*response)["error"].get<std::string>());
            }
            else if (!response) {
                errMsg = L"Нет связи с сервером. Проверьте подключение и настройки сети (порт 8443).";
            }
            SetWindowTextW(m_hStatusLabel, errMsg.c_str());
            g_logger.error(L"Server registration failed: " + normalizedPhone);
            EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), TRUE);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RegistrationWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<RegistrationWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createFontsAndBrushes();
            pThis->createControls();
            return 0;
        }
        pThis = reinterpret_cast<RegistrationWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND:
            return pThis->onCommand(LOWORD(wParam));
        case WM_CTLCOLORBTN:
            return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);
        case WM_CTLCOLORSTATIC:
            return pThis->onCtlColorStatic((HDC)wParam, (HWND)lParam);
        case WM_MOUSEMOVE:
            return pThis->onMouseMove(lParam);
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            delete pThis;
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    LRESULT onCommand(int cmd) {
        switch (cmd) {
        case IDC_REG_SUBMIT:
            onSubmit();
            break;
        case IDC_TOTP_VERIFY_BTN: // ✅ Обработка нажатия кнопки подтверждения TOTP
            onSubmit();
            break;
        case IDC_REG_BACK:
            DestroyWindow(m_hWnd);
            break;
        case IDC_REG_CLOSE:
            DestroyWindow(m_hWnd);
            break;
        }
        return 0;
    }

    LRESULT onCtlColorBtn(HDC hdc, HWND hBtn) {
        DWORD btnId = GetDlgCtrlID(hBtn);
        SetTextColor(hdc, RGB(255, 255, 255));
        if (btnId == IDC_REG_BACK || btnId == IDC_REG_CLOSE) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
            return (LRESULT)m_hRedBrush;
        }
        SetBkColor(hdc, Config::PRIMARY_COLOR);
        return (LRESULT)m_hGreenBrush;
    }

    LRESULT onCtlColorStatic(HDC hdc, HWND hStatic) {
        int id = GetDlgCtrlID(hStatic);
        if (id == IDC_REG_TITLE) {
            SetTextColor(hdc, Config::PRIMARY_COLOR);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (id == IDC_REG_STATUS) {
            SetTextColor(hdc, RGB(211, 47, 47));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    LRESULT onMouseMove(LPARAM lParam) {
        if (!IsWindow(m_hCloseBtn)) return 0;
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        bool inZone = (x >= rc.right - 100 && y <= 100);
        ShowWindow(m_hCloseBtn, inZone ? SW_SHOW : SW_HIDE);
        return 0;
    }

public:
    RegistrationWindow()
        : m_hWnd(nullptr), m_hPhoneEdit(nullptr), m_hLastNameEdit(nullptr), m_hFirstNameEdit(nullptr),
        m_hMiddleNameEdit(nullptr), m_hEmailEdit(nullptr), m_hStatusLabel(nullptr), m_hCloseBtn(nullptr),
        m_hTotpCodeLabel(nullptr), m_hTotpCodeEdit(nullptr), m_hTotpVerifyBtn(nullptr), // ✅ Инициализация новых полей
        m_hFontTitle(nullptr), m_hFontButton(nullptr), m_hFontLabel(nullptr), m_hFontEdit(nullptr),
        m_hGreenBrush(nullptr), m_hRedBrush(nullptr), m_hWhiteBrush(nullptr),
        m_isVerifyingTOTP(false), m_currentPhone(L"") {
    } // ✅ Инициализация состояния

    ~RegistrationWindow() {
        releaseFontsAndBrushes();
    }

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
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            RegisterClassExW(&wcex);
            classRegistered = true;
        }

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int wndWidth = 800;
        const int wndHeight = 600;
        int x = (screenWidth - wndWidth) / 2;
        int y = (screenHeight - wndHeight) / 2;

        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, CLASS_NAME, L"Регистрация клиента - Киоск",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME), x, y, wndWidth, wndHeight,
            hParent, nullptr, g_hInstance, this);

        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
            SetFocus(m_hPhoneEdit);
        }
    }
};
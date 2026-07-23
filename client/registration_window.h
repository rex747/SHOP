// registration_window.h
// Окно регистрации клиента с бесшовным переходом к Email OTP / TOTP аутентификации
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
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

    inline std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string str(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
        return str;
    }

    inline std::wstring utf8_to_wstring(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
        return wstr;
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
#define IDC_OTP_CODE_LABEL      5016
#define IDC_OTP_CODE_EDIT       5017
#define IDC_OTP_VERIFY_BTN      5018

class RegistrationWindow {
private:
    HWND m_hWnd, m_hPhoneEdit, m_hLastNameEdit, m_hFirstNameEdit, m_hMiddleNameEdit, m_hEmailEdit;
    HWND m_hStatusLabel, m_hCloseBtn;
    HWND m_hOtpCodeLabel, m_hOtpCodeEdit, m_hOtpVerifyBtn;
    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush, m_hWhiteBrush;
    std::vector<HWND> m_buttons;

    bool m_isOtpMode;
    std::wstring m_currentPhone;
    std::wstring m_currentEmail;

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
        if (m_hFontTitle) { DeleteObject(m_hFontTitle); m_hFontTitle = nullptr; }
        if (m_hFontButton) { DeleteObject(m_hFontButton); m_hFontButton = nullptr; }
        if (m_hFontLabel) { DeleteObject(m_hFontLabel); m_hFontLabel = nullptr; }
        if (m_hFontEdit) { DeleteObject(m_hFontEdit); m_hFontEdit = nullptr; }
        if (m_hGreenBrush) { DeleteObject(m_hGreenBrush); m_hGreenBrush = nullptr; }
        if (m_hRedBrush) { DeleteObject(m_hRedBrush); m_hRedBrush = nullptr; }
        if (m_hWhiteBrush) { DeleteObject(m_hWhiteBrush); m_hWhiteBrush = nullptr; }
    }

    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;

        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Регистрация клиента", WS_VISIBLE | WS_CHILD | SS_CENTER, 0, 20, clientWidth, 100, m_hWnd, (HMENU)(INT_PTR)IDC_REG_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

        const int labelWidth = 200, editWidth = 400, editHeight = 40, rowHeight = 60;
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

        // Элементы OTP (изначально скрыты)
        m_hOtpCodeLabel = CreateWindowExW(0, L"STATIC", L"Введите 6-ти значный код:", WS_CHILD | SS_RIGHT, startX, startY + 100, labelWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)IDC_OTP_CODE_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hOtpCodeLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        m_hOtpCodeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, startX + labelWidth + 20, startY + 100, editWidth, editHeight, m_hWnd, (HMENU)(INT_PTR)IDC_OTP_CODE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hOtpCodeEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hOtpCodeEdit, EM_SETLIMITTEXT, (WPARAM)6, 0);
        m_hOtpVerifyBtn = CreateWindowExW(0, L"BUTTON", L"Подтвердить вход", WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX, startY + 180, 300, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_OTP_VERIFY_BTN, g_hInstance, nullptr);
        SendMessageW(m_hOtpVerifyBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);

        ShowWindow(m_hOtpCodeLabel, SW_HIDE);
        ShowWindow(m_hOtpCodeEdit, SW_HIDE);
        ShowWindow(m_hOtpVerifyBtn, SW_HIDE);

        const int btnWidth = 300, btnY = startY + 5 * rowHeight + 20;
        HWND hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Зарегистрировать", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX, btnY, btnWidth, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_REG_SUBMIT, g_hInstance, nullptr);
        m_buttons.push_back(hSubmitBtn);
        HWND hBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY, startX + labelWidth + editWidth + 20 - btnWidth, btnY, btnWidth, Config::BUTTON_HEIGHT, m_hWnd, (HMENU)(INT_PTR)IDC_REG_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBackBtn);
        m_hCloseBtn = hBackBtn;

        for (HWND btn : m_buttons) SendMessageW(btn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
    }

    // -------------------------------------------------------------------------
    // Вспомогательный метод для установки токенов в AuthManager
    // -------------------------------------------------------------------------
    void setAuthTokensFromResponse(const json& response) {
        if (!response.contains("access_token") || !response["access_token"].is_string()) {
            g_logger.error(L"[setAuthTokensFromResponse] access_token missing in response");
            return;
        }
        if (!response.contains("refresh_token") || !response["refresh_token"].is_string()) {
            g_logger.error(L"[setAuthTokensFromResponse] refresh_token missing in response");
            return;
        }

        std::string accessToken = response["access_token"].get<std::string>();
        std::string refreshToken = response["refresh_token"].get<std::string>();

        auto now = std::chrono::system_clock::now();
        std::int64_t expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count() + 3600;

        // ✅ Используем публичный метод вместо прямого доступа к приватным членам
        g_authManager.setAuthTokens(accessToken, refreshToken, expiresAt,
            RegUtils::wstring_to_utf8(m_currentPhone));

        g_logger.info(L"[setAuthTokensFromResponse] Auth tokens successfully set for phone: " + m_currentPhone);
        g_logger.info(L"[setAuthTokensFromResponse] access_token: " + RegUtils::utf8_to_wstring(accessToken.substr(0, 20)) + L"...");
    }

    void onSubmit() {
        // ВЕТВЛЕНИЕ 1: Мы уже в режиме ввода OTP?
        if (m_isOtpMode) {
            g_logger.info(L"[onSubmit] OTP mode active. Verifying code...");
            wchar_t code[10]; GetWindowTextW(m_hOtpCodeEdit, code, 10);
            std::wstring codeStr(code);

            if (codeStr.length() != 6) {
                SetWindowTextW(m_hStatusLabel, L"Код должен состоять ровно из 6 цифр");
                SetFocus(m_hOtpCodeEdit); return;
            }

            SetWindowTextW(m_hStatusLabel, L"Проверка кода...");
            EnableWindow(m_hOtpVerifyBtn, FALSE);

            json request;
            request["phone"] = RegUtils::wstring_to_utf8(m_currentPhone);
            request["code"] = RegUtils::wstring_to_utf8(codeStr);

            auto response = g_httpsClient.post(L"/api/v1/auth/email_otp/verify", request);
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                // =============================================================
                // ✅ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Сохраняем токены в AuthManager
                // =============================================================
                setAuthTokensFromResponse(*response);

                SetWindowTextW(m_hStatusLabel, L"Успешная аутентификация!");
                g_logger.info(L"[onSubmit] Email OTP verified successfully. JWT acquired and stored in AuthManager.");
                Sleep(1000);
                DestroyWindow(m_hWnd);
            }
            else {
                SetWindowTextW(m_hStatusLabel, L"Неверный или истекший код. Попробуйте снова.");
                SetWindowTextW(m_hOtpCodeEdit, L"");
                SetFocus(m_hOtpCodeEdit);
                EnableWindow(m_hOtpVerifyBtn, TRUE);
                g_logger.warning(L"[onSubmit] Email OTP verification FAILED");
            }
            return;
        }

        // ВЕТВЛЕНИЕ 2: Обычная первичная регистрация
        g_logger.info(L"[onSubmit] Starting primary registration flow...");
        wchar_t buf[128];
        GetWindowTextW(m_hPhoneEdit, buf, 128);     std::wstring phone = RegUtils::trim(buf);
        GetWindowTextW(m_hLastNameEdit, buf, 128);  std::wstring lastName = RegUtils::trim(buf);
        GetWindowTextW(m_hFirstNameEdit, buf, 128); std::wstring firstName = RegUtils::trim(buf);
        GetWindowTextW(m_hMiddleNameEdit, buf, 128); std::wstring middleName = RegUtils::trim(buf);
        GetWindowTextW(m_hEmailEdit, buf, 128);     std::wstring email = RegUtils::trim(buf);

        std::wstring normalizedPhone = RegUtils::normalizePhone(phone);
        if (normalizedPhone.empty()) { SetWindowTextW(m_hStatusLabel, L"Ошибка: введите 10 или 11 цифр"); SetFocus(m_hPhoneEdit); return; }
        if (!RegUtils::isValidName(lastName, 40)) { SetWindowTextW(m_hStatusLabel, L"Фамилия: от 1 до 40 букв"); SetFocus(m_hLastNameEdit); return; }
        if (!RegUtils::isValidName(firstName, 20)) { SetWindowTextW(m_hStatusLabel, L"Имя: от 1 до 20 букв"); SetFocus(m_hFirstNameEdit); return; }
        if (!middleName.empty() && !RegUtils::isValidName(middleName, 20)) { SetWindowTextW(m_hStatusLabel, L"Отчество: от 1 до 20 букв"); SetFocus(m_hMiddleNameEdit); return; }
        if (!email.empty() && !RegUtils::isValidEmail(email)) { SetWindowTextW(m_hStatusLabel, L"E-mail: неверный формат"); SetFocus(m_hEmailEdit); return; }

        EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), FALSE);
        SetWindowTextW(m_hStatusLabel, L"Обработка данных...");

        LocalDB::addClient(normalizedPhone, lastName + (firstName.empty() ? L"" : L" " + firstName) + (middleName.empty() ? L"" : L" " + middleName), email);

        json request;
        request["phone"] = RegUtils::wstring_to_utf8(normalizedPhone);
        request["last_name"] = RegUtils::wstring_to_utf8(lastName);
        request["first_name"] = RegUtils::wstring_to_utf8(firstName);
        request["middle_name"] = RegUtils::wstring_to_utf8(middleName);
        request["email"] = RegUtils::wstring_to_utf8(email);
        request["items_submitted"] = 0;
        request["items_sold"] = 0;

        g_logger.info(L"Sending registration request for: " + normalizedPhone);
        auto response = g_httpsClient.post(L"/api/v1/clients/register", request);

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            bool alreadyExists = response->contains("already_exists") && (*response)["already_exists"].get<bool>();
            g_logger.info(L"Server response: success=true, already_exists=" + std::to_wstring(alreadyExists ? 1 : 0));

            if (alreadyExists) {
                // БЛОК ДЛЯ СУЩЕСТВУЮЩЕГО ПОЛЬЗОВАТЕЛЯ
                g_logger.info(L"User exists. Requesting Email OTP for: " + normalizedPhone);
                m_currentPhone = normalizedPhone;
                m_currentEmail = email;

                json otpRequest;
                otpRequest["phone"] = RegUtils::wstring_to_utf8(normalizedPhone);
                otpRequest["email"] = RegUtils::wstring_to_utf8(email);

                auto otpResponse = g_httpsClient.post(L"/api/v1/auth/email_otp/request", otpRequest);

                if (otpResponse && otpResponse->contains("success") && (*otpResponse)["success"].get<bool>()) {
                    m_isOtpMode = true;

                    // 1. Скрываем поля регистрации
                    ShowWindow(m_hPhoneEdit, SW_HIDE); ShowWindow(GetDlgItem(m_hWnd, IDC_REG_PHONE_LABEL), SW_HIDE);
                    ShowWindow(m_hLastNameEdit, SW_HIDE); ShowWindow(GetDlgItem(m_hWnd, IDC_REG_LASTNAME_LABEL), SW_HIDE);
                    ShowWindow(m_hFirstNameEdit, SW_HIDE); ShowWindow(GetDlgItem(m_hWnd, IDC_REG_FIRSTNAME_LABEL), SW_HIDE);
                    ShowWindow(m_hMiddleNameEdit, SW_HIDE); ShowWindow(GetDlgItem(m_hWnd, IDC_REG_MIDDLENAME_LABEL), SW_HIDE);
                    ShowWindow(m_hEmailEdit, SW_HIDE); ShowWindow(GetDlgItem(m_hWnd, IDC_REG_EMAIL_LABEL), SW_HIDE);
                    ShowWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), SW_HIDE);

                    // 2. Перестраиваем UI
                    RECT rc; GetClientRect(m_hWnd, &rc);
                    int clientWidth = rc.right - rc.left;
                    const int labelWidth = 200, editWidth = 400;
                    const int startX = (clientWidth - labelWidth - editWidth) / 2;
                    const int startY = 140;

                    SetWindowPos(m_hStatusLabel, NULL, startX, startY + 10, labelWidth + editWidth + 20, 60, SWP_NOZORDER);
                    SetWindowTextW(m_hStatusLabel, (L"Пользователь уже зарегистрирован.\nКод отправлен на: " + email).c_str());

                    SetWindowPos(m_hOtpCodeLabel, NULL, startX, startY + 80, labelWidth, 40, SWP_NOZORDER | SWP_SHOWWINDOW);
                    SetWindowPos(m_hOtpCodeEdit, NULL, startX + labelWidth + 20, startY + 80, editWidth, 40, SWP_NOZORDER | SWP_SHOWWINDOW);

                    const int btnWidth = 300, btnHeight = Config::BUTTON_HEIGHT, btnY = startY + 160;
                    SetWindowPos(m_hOtpVerifyBtn, NULL, startX, btnY, btnWidth, btnHeight, SWP_NOZORDER | SWP_SHOWWINDOW);
                    SetWindowPos(GetDlgItem(m_hWnd, IDC_REG_BACK), NULL, startX + labelWidth + editWidth + 20 - btnWidth, btnY, btnWidth, btnHeight, SWP_NOZORDER | SWP_SHOWWINDOW);

                    SetWindowTextW(m_hOtpCodeEdit, L"");
                    SetFocus(m_hOtpCodeEdit);
                    g_logger.info(L"UI successfully switched to Email OTP input mode");
                }
                else {
                    SetWindowTextW(m_hStatusLabel, L"Ошибка отправки кода на почту. Попробуйте позже.");
                    g_logger.error(L"Failed to request Email OTP");
                    EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), TRUE);
                }
            }
            else {
                // БЛОК ДЛЯ НОВОГО ПОЛЬЗОВАТЕЛЯ
                g_logger.info(L"New user registered successfully: " + normalizedPhone);
                SetWindowTextW(m_hStatusLabel, L"Регистрация прошла успешно!");

                // Инициализируем TOTP для нового пользователя
                auto totpResult = g_authManager.setupTOTP(normalizedPhone);
                if (totpResult.success) {
                    g_logger.info(L"TOTP initialized successfully for new user");
                }
                else {
                    g_logger.warning(L"TOTP setup failed for new user: " + normalizedPhone);
                }

                // =============================================================
                // ✅ ДОПОЛНИТЕЛЬНО: Для нового пользователя также устанавливаем
                //    токен, если сервер вернул его в ответе на регистрацию.
                //    (Предполагаем, что сервер может сразу вернуть токены)
                // =============================================================
                if (response->contains("access_token") && response->contains("refresh_token")) {
                    setAuthTokensFromResponse(*response);
                    g_logger.info(L"[onSubmit] Auth tokens set for new user from registration response.");
                }
                else {
                    g_logger.warning(L"[onSubmit] No tokens in registration response. User will need to login.");
                }

                Sleep(1500);
                DestroyWindow(m_hWnd);
            }
        }
        else {
            std::wstring errMsg = L"Ошибка регистрации";
            if (response && response->contains("error")) errMsg = RegUtils::utf8_to_wstring((*response)["error"].get<std::string>());
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
        case WM_COMMAND: return pThis->onCommand(LOWORD(wParam));
        case WM_CTLCOLORBTN: return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);
        case WM_CTLCOLORSTATIC: return pThis->onCtlColorStatic((HDC)wParam, (HWND)lParam);
        case WM_MOUSEMOVE: return pThis->onMouseMove(lParam);
        case WM_CLOSE: DestroyWindow(hWnd); return 0;
        case WM_DESTROY: delete pThis; return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    LRESULT onCommand(int cmd) {
        if (cmd == IDC_REG_SUBMIT || cmd == IDC_OTP_VERIFY_BTN) onSubmit();
        else if (cmd == IDC_REG_BACK || cmd == IDC_REG_CLOSE) DestroyWindow(m_hWnd);
        return 0;
    }

    LRESULT onCtlColorBtn(HDC hdc, HWND hBtn) {
        SetTextColor(hdc, RGB(255, 255, 255));
        if (GetDlgCtrlID(hBtn) == IDC_REG_BACK || GetDlgCtrlID(hBtn) == IDC_REG_CLOSE) {
            SetBkColor(hdc, Config::BACK_BUTTON_COLOR); return (LRESULT)m_hRedBrush;
        }
        SetBkColor(hdc, Config::PRIMARY_COLOR); return (LRESULT)m_hGreenBrush;
    }

    LRESULT onCtlColorStatic(HDC hdc, HWND hStatic) {
        int id = GetDlgCtrlID(hStatic);
        SetBkMode(hdc, TRANSPARENT);
        if (id == IDC_REG_TITLE) { SetTextColor(hdc, Config::PRIMARY_COLOR); return (LRESULT)GetStockObject(NULL_BRUSH); }
        if (id == IDC_REG_STATUS) { SetTextColor(hdc, RGB(211, 47, 47)); return (LRESULT)GetStockObject(NULL_BRUSH); }
        SetTextColor(hdc, RGB(0, 0, 0)); return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    LRESULT onMouseMove(LPARAM lParam) {
        if (!IsWindow(m_hCloseBtn)) return 0;
        RECT rc; GetClientRect(m_hWnd, &rc);
        ShowWindow(m_hCloseBtn, (LOWORD(lParam) >= rc.right - 100 && HIWORD(lParam) <= 100) ? SW_SHOW : SW_HIDE);
        return 0;
    }

public:
    RegistrationWindow() : m_hWnd(nullptr), m_hPhoneEdit(nullptr), m_hLastNameEdit(nullptr), m_hFirstNameEdit(nullptr),
        m_hMiddleNameEdit(nullptr), m_hEmailEdit(nullptr), m_hStatusLabel(nullptr), m_hCloseBtn(nullptr),
        m_hOtpCodeLabel(nullptr), m_hOtpCodeEdit(nullptr), m_hOtpVerifyBtn(nullptr),
        m_hFontTitle(nullptr), m_hFontButton(nullptr), m_hFontLabel(nullptr), m_hFontEdit(nullptr),
        m_hGreenBrush(nullptr), m_hRedBrush(nullptr), m_hWhiteBrush(nullptr), m_isOtpMode(false) {
    }

    ~RegistrationWindow() { releaseFontsAndBrushes(); }

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
        int x = (GetSystemMetrics(SM_CXSCREEN) - 800) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - 600) / 2;
        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, CLASS_NAME, L"Регистрация клиента - Киоск",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME), x, y, 800, 600, hParent, nullptr, g_hInstance, this);
        if (m_hWnd) { ShowWindow(m_hWnd, SW_SHOW); UpdateWindow(m_hWnd); SetFocus(m_hPhoneEdit); }
    }
};
// registration_dialog.h - полное исправление
#pragma once

#include <windows.h>
#include <string>
#include <regex>
#include "auth_manager.h"
#include "logger.h"

extern AuthManager g_authManager;
extern Logger g_logger;
extern HINSTANCE g_hInstance;

// ✅ ДОБАВИТЬ определения констант
#ifndef IDC_PHONE_EDIT
#define IDC_PHONE_EDIT 1001
#endif
#ifndef IDC_CODE_EDIT
#define IDC_CODE_EDIT 1002
#endif
#ifndef IDC_NAME_EDIT
#define IDC_NAME_EDIT 1003
#endif
#ifndef IDC_EMAIL_EDIT
#define IDC_EMAIL_EDIT 1004
#endif
#ifndef IDC_SEND_CODE_BTN
#define IDC_SEND_CODE_BTN 1005
#endif
#ifndef IDC_VERIFY_BTN
#define IDC_VERIFY_BTN 1006
#endif
#ifndef IDC_STATUS_LABEL
#define IDC_STATUS_LABEL 1007
#endif
#ifndef IDD_REGISTRATION_DIALOG
#define IDD_REGISTRATION_DIALOG 2001
#endif

class RegistrationDialog {
private:
    HWND m_hDlg;
    HWND m_hPhoneEdit;
    HWND m_hCodeEdit;
    HWND m_hNameEdit;
    HWND m_hEmailEdit;
    HWND m_hSendCodeBtn;
    HWND m_hVerifyBtn;
    HWND m_hStatusLabel;

    std::wstring m_phone;
    bool m_codeSent;

    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        RegistrationDialog* pThis = nullptr;

        if (msg == WM_INITDIALOG) {
            pThis = reinterpret_cast<RegistrationDialog*>(lParam);
            SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hDlg = hDlg;
            return pThis->onInitDialog();
        }
        else {
            pThis = reinterpret_cast<RegistrationDialog*>(
                GetWindowLongPtr(hDlg, GWLP_USERDATA));
            if (pThis) {
                return pThis->handleMessage(msg, wParam, lParam);
            }
        }

        return FALSE;
    }

    INT_PTR onInitDialog() {
        m_hPhoneEdit = GetDlgItem(m_hDlg, IDC_PHONE_EDIT);
        m_hCodeEdit = GetDlgItem(m_hDlg, IDC_CODE_EDIT);
        m_hNameEdit = GetDlgItem(m_hDlg, IDC_NAME_EDIT);
        m_hEmailEdit = GetDlgItem(m_hDlg, IDC_EMAIL_EDIT);
        m_hSendCodeBtn = GetDlgItem(m_hDlg, IDC_SEND_CODE_BTN);
        m_hVerifyBtn = GetDlgItem(m_hDlg, IDC_VERIFY_BTN);
        m_hStatusLabel = GetDlgItem(m_hDlg, IDC_STATUS_LABEL);

        m_codeSent = false;

        EnableWindow(m_hCodeEdit, FALSE);
        EnableWindow(m_hVerifyBtn, FALSE);

        SetWindowTextW(m_hStatusLabel, L"Введите номер телефона");

        return TRUE;
    }

    INT_PTR handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND:
            return onCommand(LOWORD(wParam));
        case WM_CLOSE:
            EndDialog(m_hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }

    INT_PTR onCommand(int cmd) {
        switch (cmd) {
        case IDC_SEND_CODE_BTN:
            onSendCode();
            break;
        case IDC_VERIFY_BTN:
            onVerify();
            break;
        case IDCANCEL:
            EndDialog(m_hDlg, IDCANCEL);
            break;
        }
        return TRUE;
    }

    void onSendCode() {
        wchar_t phone[20];
        GetWindowTextW(m_hPhoneEdit, phone, 20);

        std::wstring phoneStr(phone);
        if (!validatePhone(phoneStr)) {
            SetWindowTextW(m_hStatusLabel, L"Неверный формат телефона");
            return;
        }

        m_phone = phoneStr;

        if (g_authManager.sendSMSCode(m_phone)) {
            m_codeSent = true;
            EnableWindow(m_hCodeEdit, TRUE);
            EnableWindow(m_hVerifyBtn, TRUE);
            EnableWindow(m_hSendCodeBtn, FALSE);
            SetWindowTextW(m_hStatusLabel, L"Код отправлен. Введите код из SMS");
            SetFocus(m_hCodeEdit);
        }
        else {
            SetWindowTextW(m_hStatusLabel, L"Ошибка отправки SMS");
        }
    }

    void onVerify() {
        wchar_t code[10];
        GetWindowTextW(m_hCodeEdit, code, 10);

        wchar_t name[100];
        GetWindowTextW(m_hNameEdit, name, 100);

        wchar_t email[100];
        GetWindowTextW(m_hEmailEdit, email, 100);

        if (g_authManager.authenticate(m_phone, std::wstring(code))) {
            LocalDB::addClient(m_phone, std::wstring(name), std::wstring(email));
            g_logger.info(L"Client registered: " + m_phone);
            EndDialog(m_hDlg, IDOK);
        }
        else {
            SetWindowTextW(m_hStatusLabel, L"Неверный код или ошибка аутентификации");
        }
    }

    bool validatePhone(const std::wstring& phone) {
        // ✅ ЗАМЕНИТЬ std::regex на std::wregex для wchar_t
        std::wregex phoneRegex(L"^\\+?[0-9]{10,15}$");
        return std::regex_match(phone, phoneRegex);
    }

public:
    INT_PTR show(HWND hParent) {
        // ✅ ЗАМЕНИТЬ hInstance на g_hInstance
        return DialogBoxParamW(g_hInstance, MAKEINTRESOURCE(IDD_REGISTRATION_DIALOG),
            hParent, DialogProc, reinterpret_cast<LPARAM>(this));
    }
};
// login_window.h
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include "config.h"
#include "logger.h"
#include "auth_manager.h"
#include "https_client.h"
#include "string_utils.h"

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern AuthManager g_authManager;
extern HINSTANCE g_hInstance;

class LoginWindow {
private:
    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hStatusLabel;
    HWND m_hSubmitBtn;
    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush;

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
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND:
            pThis->onCommand(LOWORD(wParam));
            return 0;
        case WM_CTLCOLORBTN:
            return pThis->onCtlColorBtn((HDC)wParam, (HWND)lParam);
        case WM_CTLCOLORSTATIC:
            return pThis->onCtlColorStatic((HDC)wParam);
        case WM_CLOSE:
            // Закрытие без ввода – завершаем приложение
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            pThis->releaseResources();
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;
        int centerX = clientWidth / 2;
        int startY = 60;

        // Шрифты и кисти
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

        // Заголовок
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Вход в систему",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 150, startY, 300, 60,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
        startY += 80;

        // Метка и поле ввода телефона
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Номер телефона:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            centerX - 200, startY, 180, 40,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            centerX + 20, startY, 200, 40,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hPhoneEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        startY += 60;

        // Статус
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            centerX - 200, startY, 400, 30,
            m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        startY += 50;

        // Кнопка "Войти"
        m_hSubmitBtn = CreateWindowExW(0, L"BUTTON", L"Войти",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            centerX - 100, startY, 200, 50,
            m_hWnd, (HMENU)(INT_PTR)1, g_hInstance, nullptr);
        SendMessageW(m_hSubmitBtn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
    }

    void releaseResources() {
        if (m_hFontTitle) DeleteObject(m_hFontTitle);
        if (m_hFontButton) DeleteObject(m_hFontButton);
        if (m_hFontLabel) DeleteObject(m_hFontLabel);
        if (m_hFontEdit) DeleteObject(m_hFontEdit);
        if (m_hGreenBrush) DeleteObject(m_hGreenBrush);
        if (m_hRedBrush) DeleteObject(m_hRedBrush);
    }

    LRESULT onCtlColorBtn(HDC hdc, HWND hBtn) {
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, Config::PRIMARY_COLOR);
        return (LRESULT)m_hGreenBrush;
    }

    LRESULT onCtlColorStatic(HDC hdc) {
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    void onCommand(int cmd) {
        if (cmd == 1) onSubmit();
    }

    void onSubmit() {
        wchar_t buf[30];
        GetWindowTextW(m_hPhoneEdit, buf, 30);
        std::wstring phone = trim(buf);
        if (phone.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Введите номер телефона");
            return;
        }

        std::wstring normalized = normalizePhone(phone);
        if (normalized.empty()) {
            SetWindowTextW(m_hStatusLabel, L"Неверный формат номера");
            return;
        }

        SetWindowTextW(m_hStatusLabel, L"Проверка...");
        EnableWindow(m_hSubmitBtn, FALSE);

        nlohmann::json request;
        request["phone"] = wstring_to_utf8(normalized);
        auto response = g_httpsClient.post(L"/api/v1/clients/by_phone", request, L"");

        if (!response) {
            SetWindowTextW(m_hStatusLabel, L"Ошибка соединения с сервером");
            EnableWindow(m_hSubmitBtn, TRUE);
            return;
        }

        if (response->contains("id") && response->contains("name")) {
            int clientId = (*response)["id"].get<int>();
            std::string nameUtf8 = (*response)["name"].get<std::string>();
            std::wstring fullName = utf8_to_wstring(nameUtf8);

            g_authManager.setLoggedIn(true, clientId, fullName, normalized);
            SetWindowTextW(m_hStatusLabel, L"Успешный вход!");
            g_logger.info(L"Login successful for phone: " + normalized + L", clientId: " + std::to_wstring(clientId));
            Sleep(500);
            DestroyWindow(m_hWnd);
        }
        else {
            g_authManager.setLoggedIn(false, 0, L"", normalized);
            SetWindowTextW(m_hStatusLabel, L"Номер не найден. Вы будете перенаправлены.");
            g_logger.warning(L"Login failed: phone not found: " + normalized);
            Sleep(1000);
            DestroyWindow(m_hWnd);
        }
    }

    std::wstring normalizePhone(const std::wstring& input) {
        std::wstring digits;
        for (wchar_t ch : input) {
            if (ch >= L'0' && ch <= L'9') digits += ch;
        }
        if (digits.empty()) return L"";
        if (digits.length() == 11 && (digits[0] == L'7' || digits[0] == L'8'))
            return L"+7" + digits.substr(1);
        if (digits.length() == 10)
            return L"+7" + digits;
        if (digits.length() == 12 && digits.substr(0, 2) == L"77")
            return L"+" + digits;
        return L"";
    }

    std::wstring trim(const std::wstring& s) {
        size_t start = s.find_first_not_of(L" \t");
        size_t end = s.find_last_not_of(L" \t");
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

public:
    LoginWindow() : m_hWnd(nullptr), m_hPhoneEdit(nullptr), m_hStatusLabel(nullptr),
        m_hSubmitBtn(nullptr), m_hFontTitle(nullptr), m_hFontButton(nullptr),
        m_hFontLabel(nullptr), m_hFontEdit(nullptr), m_hGreenBrush(nullptr), m_hRedBrush(nullptr) {
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
            wcex.lpszClassName = L"LoginWindowClass";
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            RegisterClassExW(&wcex);
            classRegistered = true;
        }

        int x = (GetSystemMetrics(SM_CXSCREEN) - 500) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - 300) / 2;
        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, L"LoginWindowClass", L"Вход",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
            x, y, 500, 350, hParent, nullptr, g_hInstance, this);
        if (!m_hWnd) return;

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
    }
};

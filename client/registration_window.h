// registration_window.h
// Окно регистрации клиента в стилистике главного окна "ДОБРО КОМИССИОННЫЙ МАГАЗИН"
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

// ---------------------------------------------------------------------------
// Утилиты нормализации и валидации
// ---------------------------------------------------------------------------
namespace RegUtils {

    // Извлекает только цифры из строки
    inline std::wstring extractDigits(const std::wstring& input) {
        std::wstring digits;
        digits.reserve(input.size());
        for (wchar_t ch : input) {
            if (ch >= L'0' && ch <= L'9') {
                digits += ch;
            }
        }
        return digits;
    }

    // Нормализует российский номер телефона до формата +7XXXXXXXXXX (11 символов)
    // Принимает: +7-911-111-11-11, 8(911)111-11-11, 9111111111, +79111111111 и т.д.
    // Возвращает пустую строку, если количество цифр невалидно
    inline std::wstring normalizePhone(const std::wstring& input) {
        std::wstring digits = extractDigits(input);

        // 11 цифр: первая 7 или 8 -> +7 + последние 10
        if (digits.length() == 11) {
            if (digits[0] == L'7' || digits[0] == L'8') {
                return L"+7" + digits.substr(1);
            }
            return L""; // 11 цифр, но не 7/8 в начале — не российский номер
        }
        // 10 цифр: добавляем +7
        if (digits.length() == 10) {
            return L"+7" + digits;
        }
        return L""; // Невалидное количество цифр
    }

    // Валидация имени/фамилии/отчества: только буквы (кириллица/латиница), пробел, дефис, апостроф
    inline bool isValidName(const std::wstring& name, size_t maxLen) {
        if (name.empty() || name.length() > maxLen) return false;
        bool hasLetter = false;
        for (wchar_t ch : name) {
            if (iswalpha(ch)) {
                hasLetter = true;
            }
            else if (ch != L' ' && ch != L'-' && ch != L'\'') {
                return false;
            }
        }
        return hasLetter;
    }

    // Валидация e-mail (упрощённая, но достаточная)
    inline bool isValidEmail(const std::wstring& email) {
        if (email.empty() || email.length() > 30) return false;
        auto atPos = email.find(L'@');
        if (atPos == std::wstring::npos || atPos == 0 || atPos == email.length() - 1)
            return false;
        auto dotPos = email.find(L'.', atPos);
        if (dotPos == std::wstring::npos || dotPos == email.length() - 1)
            return false;
        return true;
    }

    // Trim пробелов по краям
    inline std::wstring trim(const std::wstring& s) {
        size_t start = s.find_first_not_of(L" \t");
        size_t end = s.find_last_not_of(L" \t");
        if (start == std::wstring::npos) return L"";
        return s.substr(start, end - start + 1);
    }

} // namespace RegUtils

// ---------------------------------------------------------------------------
// ID элементов управления (уникальные в рамках проекта, начинаются с 5000)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Класс окна регистрации
// ---------------------------------------------------------------------------
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

    // Шрифты и кисти (живут всё время жизни окна)
    HFONT m_hFontTitle;
    HFONT m_hFontButton;
    HFONT m_hFontLabel;
    HFONT m_hFontEdit;
    HBRUSH m_hGreenBrush;
    HBRUSH m_hRedBrush;
    HBRUSH m_hWhiteBrush;

    std::vector<HWND> m_buttons;

    static constexpr const wchar_t* CLASS_NAME = L"RegistrationWindowClass";

    // -----------------------------------------------------------------------
    // Создание шрифтов и кистей (в той же стилистике, что и главное окно)
    // -----------------------------------------------------------------------
    void createFontsAndBrushes() {
        // Заголовок 48pt Arial Bold (как в main_window.h)
        m_hFontTitle = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        // Кнопки 26pt Arial Bold (как в main_window.h)
        m_hFontButton = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        // Метки и поля ввода 20pt Arial
        m_hFontLabel = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        m_hFontEdit = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        // Кисти в цветовой гамме главного окна
        m_hGreenBrush = CreateSolidBrush(Config::PRIMARY_COLOR);   // RGB(76, 175, 80)
        m_hRedBrush = CreateSolidBrush(Config::BACK_BUTTON_COLOR); // RGB(211, 47, 47)
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

    // -----------------------------------------------------------------------
    // Создание элементов управления
    // -----------------------------------------------------------------------
    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;

        // Заголовок "ДОБРО КОМИССИОННЫЙ МАГАЗИН" (как в главном окне)
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"ДОБРО КОМИССИОННЫЙ МАГАЗИН",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 20, clientWidth, 100,
            m_hWnd, (HMENU)(INT_PTR)IDC_REG_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);
                
        // Параметры сетки
        const int labelWidth = 200;
        const int editWidth = 400;
        const int editHeight = 40;
        const int rowHeight = 60;
        const int startX = (clientWidth - labelWidth - editWidth) / 2;
        const int startY = 140;

        // Вспомогательная лямбда: метка + поле ввода
        auto createLabeledEdit = [&](int idLabel, int idEdit,
            const wchar_t* labelText, int row,
            DWORD editStyle, DWORD maxLen) -> HWND {

                HWND hLabel = CreateWindowExW(
                    0, L"STATIC", labelText,
                    WS_VISIBLE | WS_CHILD | SS_RIGHT,
                    startX, startY + row * rowHeight, labelWidth, editHeight,
                    m_hWnd, (HMENU)(INT_PTR)idLabel, g_hInstance, nullptr);
                SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

                HWND hEdit = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | editStyle,
                    startX + labelWidth + 20, startY + row * rowHeight,
                    editWidth, editHeight,
                    m_hWnd, (HMENU)(INT_PTR)idEdit, g_hInstance, nullptr);
                SendMessageW(hEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
                SendMessageW(hEdit, EM_SETLIMITTEXT, (WPARAM)maxLen, 0);

                return hEdit;
            };

        // Телефон
        m_hPhoneEdit = createLabeledEdit(
            IDC_REG_PHONE_LABEL, IDC_REG_PHONE, L"Телефон:", 0, 0, 30);

        // Фамилия
        m_hLastNameEdit = createLabeledEdit(
            IDC_REG_LASTNAME_LABEL, IDC_REG_LASTNAME, L"Фамилия:", 1, 0, 40);

        // Имя
        m_hFirstNameEdit = createLabeledEdit(
            IDC_REG_FIRSTNAME_LABEL, IDC_REG_FIRSTNAME, L"Имя:", 2, 0, 20);

        // Отчество
        m_hMiddleNameEdit = createLabeledEdit(
            IDC_REG_MIDDLENAME_LABEL, IDC_REG_MIDDLENAME, L"Отчество:", 3, 0, 20);

        // E-mail
        m_hEmailEdit = createLabeledEdit(
            IDC_REG_EMAIL_LABEL, IDC_REG_EMAIL, L"E-mail:", 4, 0, 30);

        // Статусная строка
        m_hStatusLabel = CreateWindowExW(
            0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            startX, startY + 5 * rowHeight + 10,
            labelWidth + editWidth + 20, 40,
            m_hWnd, (HMENU)(INT_PTR)IDC_REG_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        // Кнопки "ЗАРЕГИСТРИРОВАТЬ" и "НАЗАД"
        const int btnWidth = 300;
        const int btnHeight = Config::BUTTON_HEIGHT;
        const int btnY = startY + 5 * rowHeight + 20;

        HWND hSubmitBtn = CreateWindowExW(
            0, L"BUTTON", L"ЗАРЕГИСТРИРОВАТЬ",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY,
            startX, btnY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_REG_SUBMIT, g_hInstance, nullptr);
        m_buttons.push_back(hSubmitBtn);

        HWND hBackBtn = CreateWindowExW(
            0, L"BUTTON", L"НАЗАД",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_NOTIFY,
            startX + labelWidth + editWidth + 20 - btnWidth, btnY,
            btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_REG_BACK, g_hInstance, nullptr);
        m_buttons.push_back(hBackBtn);

        // Применяем шрифт ко всем кнопкам (как в main_window.h styleButtons)
        for (HWND btn : m_buttons) {
            SendMessageW(btn, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);
        }
    }

    // -----------------------------------------------------------------------
    // Обработчик отправки формы
    // -----------------------------------------------------------------------
    void onSubmit() {
        wchar_t buf[128];
        GetWindowTextW(m_hPhoneEdit, buf, 128);     std::wstring phone = RegUtils::trim(buf);
        GetWindowTextW(m_hLastNameEdit, buf, 128);  std::wstring lastName = RegUtils::trim(buf);
        GetWindowTextW(m_hFirstNameEdit, buf, 128); std::wstring firstName = RegUtils::trim(buf);
        GetWindowTextW(m_hMiddleNameEdit, buf, 128); std::wstring middleName = RegUtils::trim(buf);
        GetWindowTextW(m_hEmailEdit, buf, 128);     std::wstring email = RegUtils::trim(buf);

        // Валидация телефона
        std::wstring normalizedPhone = RegUtils::normalizePhone(phone);
        if (normalizedPhone.empty()) {
            SetWindowTextW(m_hStatusLabel,
                L"Телефон: введите 10 или 11 цифр (например, +7-911-111-11-11)");
            SetFocus(m_hPhoneEdit);
            return;
        }

        // Валидация фамилии
        if (!RegUtils::isValidName(lastName, 40)) {
            SetWindowTextW(m_hStatusLabel, L"Фамилия: от 1 до 40 букв");
            SetFocus(m_hLastNameEdit);
            return;
        }

        // Валидация имени
        if (!RegUtils::isValidName(firstName, 20)) {
            SetWindowTextW(m_hStatusLabel, L"Имя: от 1 до 20 букв");
            SetFocus(m_hFirstNameEdit);
            return;
        }

        // Валидация отчества
        if (!middleName.empty() && !RegUtils::isValidName(middleName, 20)) {
            SetWindowTextW(m_hStatusLabel, L"Отчество: от 1 до 20 букв");
            SetFocus(m_hMiddleNameEdit);
            return;
        }

        // Валидация e-mail
        if (!email.empty() && !RegUtils::isValidEmail(email)) {
            SetWindowTextW(m_hStatusLabel, L"E-mail: неверный формат");
            SetFocus(m_hEmailEdit);
            return;
        }

        // Блокируем кнопку и показываем статус
        EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), FALSE);
        SetWindowTextW(m_hStatusLabel, L"Отправка данных...");

        // Формируем полное имя
        std::wstring fullName = lastName;
        if (!firstName.empty())  fullName += L" " + firstName;
        if (!middleName.empty()) fullName += L" " + middleName;

        // Сохраняем локально
        bool localOk = LocalDB::addClient(normalizedPhone, fullName, email);
        if (!localOk) {
            g_logger.warning(L"LocalDB::addClient failed: " + normalizedPhone);
        }

        // Отправка на сервер
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

        if (response && response->contains("success")
            && (*response)["success"].is_boolean()
            && (*response)["success"].get<bool>())
        {
            SetWindowTextW(m_hStatusLabel, L"Регистрация успешна!");
            g_logger.info(L"Client registered on server: " + normalizedPhone);

            auto totpResult = g_authManager.setupTOTP(normalizedPhone);
            if (totpResult.success) {
                g_logger.info(L"TOTP initialized for: " + normalizedPhone);
            }
            else {
                g_logger.warning(L"TOTP initialization failed for: " + normalizedPhone);
            }

            Sleep(1500);
            DestroyWindow(m_hWnd);
        }
        else {
            std::wstring errMsg = L"Ошибка регистрации на сервере";
            if (response && response->contains("error")
                && (*response)["error"].is_string()) {
                errMsg = utf8_to_wstring((*response)["error"].get<std::string>());
            }
            else if (!response) {
                errMsg = L"Нет связи с сервером. Проверьте подключение.";
            }
            SetWindowTextW(m_hStatusLabel, errMsg.c_str());
            g_logger.error(L"Server registration failed: " + normalizedPhone);
            EnableWindow(GetDlgItem(m_hWnd, IDC_REG_SUBMIT), TRUE);
        }
    }

    // -----------------------------------------------------------------------
    // Window Procedure
    // -----------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        RegistrationWindow* pThis = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<RegistrationWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createFontsAndBrushes();
            pThis->createControls();
            return 0;
        }

        pThis = reinterpret_cast<RegistrationWindow*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));

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
            // releaseFontsAndBrushes() вызовется в деструкторе автоматически
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
            case IDC_REG_BACK:   
                DestroyWindow(m_hWnd); 
            break;
            case IDC_REG_CLOSE:  
                DestroyWindow(m_hWnd); 
            break;
        }
        return 0;
    }

    // Стилизация кнопок (как в main_window.h WM_CTLCOLORBTN)
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

    // Стилизация статических элементов (как в main_window.h WM_CTLCOLORSTATIC)
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

        // Метки полей — чёрный текст на белом фоне
        SetTextColor(hdc, RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    // Показ/скрытие кнопки закрытия при наведении (как в main_window.h)
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
        : m_hWnd(nullptr)
        , m_hPhoneEdit(nullptr)
        , m_hLastNameEdit(nullptr)
        , m_hFirstNameEdit(nullptr)
        , m_hMiddleNameEdit(nullptr)
        , m_hEmailEdit(nullptr)
        , m_hStatusLabel(nullptr)
        , m_hCloseBtn(nullptr)
        , m_hFontTitle(nullptr)
        , m_hFontButton(nullptr)
        , m_hFontLabel(nullptr)
        , m_hFontEdit(nullptr)
        , m_hGreenBrush(nullptr)
        , m_hRedBrush(nullptr)
        , m_hWhiteBrush(nullptr)
    {
    }

    ~RegistrationWindow() {
        releaseFontsAndBrushes();
    }

    void show(HWND hParent) {
        // Регистрация класса окна (один раз)
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

        m_hWnd = CreateWindowExW(
            WS_EX_WINDOWEDGE,
            CLASS_NAME,
            L"Регистрация клиента - ДОБРО",
            WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
            x, y, wndWidth, wndHeight,
            hParent, nullptr, g_hInstance, this);

        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
            SetFocus(m_hPhoneEdit);
        }
    }
};

// worker_registration_window.h (исправленная версия)
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <nlohmann/json.hpp>

#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "auth_manager.h"
#include "string_utils.h"

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;

using json = nlohmann::json;

#define IDC_WORKER_REG_TITLE        6001
#define IDC_WORKER_REG_PHONE_LABEL  6002
#define IDC_WORKER_REG_PHONE        6003
#define IDC_WORKER_REG_LASTNAME_LABEL 6004
#define IDC_WORKER_REG_LASTNAME     6005
#define IDC_WORKER_REG_FIRSTNAME_LABEL 6006
#define IDC_WORKER_REG_FIRSTNAME    6007
#define IDC_WORKER_REG_MIDDLENAME_LABEL 6008
#define IDC_WORKER_REG_MIDDLENAME   6009
#define IDC_WORKER_REG_EMAIL_LABEL  6010
#define IDC_WORKER_REG_EMAIL        6011
#define IDC_WORKER_REG_STATUS       6012
#define IDC_WORKER_REG_SUBMIT       6013
#define IDC_WORKER_REG_CANCEL       6014

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
        if (wstr.empty()) return {};
        // Без -1 → размер БЕЗ завершающего нуля
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
            static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (size_needed <= 0) return {};
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
            static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
        return result;
    }

    inline std::wstring utf8_to_wstring(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
        return wstr;
    }
}

class WorkerRegistrationWindow {
private:
    HWND m_hWnd;
    HWND m_hPhoneEdit;
    HWND m_hLastNameEdit;
    HWND m_hFirstNameEdit;
    HWND m_hMiddleNameEdit;
    HWND m_hEmailEdit;
    HWND m_hStatusLabel;

    HFONT m_hFontTitle, m_hFontButton, m_hFontLabel, m_hFontEdit;
    HBRUSH m_hGreenBrush, m_hRedBrush, m_hWhiteBrush;

    std::wstring m_phone;
    std::string m_role;
    std::string m_generatedPassword; // сгенерированный сервером пароль (нужен для автовхода)
    int m_dialogResult;

    static constexpr const wchar_t* CLASS_NAME = L"WorkerRegistrationWindowClass";


    void createFontsAndBrushes() {
        m_hFontTitle = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontButton = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontLabel = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontEdit = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

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
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int clientWidth = rc.right - rc.left;
        int clientHeight = rc.bottom - rc.top;

        int labelWidth = 150, editWidth = 300, editHeight = 32, rowHeight = 50;
        int startX = (clientWidth - labelWidth - editWidth) / 2;
        int startY = 80;

        // Заголовок в зависимости от роли
        const wchar_t* windowTitle = (m_role == "director")
            ? L"Регистрация директора магазина"
            : L"Регистрация товароведа";
        HWND hTitle = CreateWindowExW(0, L"STATIC", windowTitle,
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 20, clientWidth, 60,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_TITLE, g_hInstance, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

        // Телефон (нередактируемый)
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"Телефон:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, startY, labelWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_PHONE_LABEL, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hPhoneEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_READONLY,
            startX + labelWidth + 10, startY, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_PHONE, g_hInstance, nullptr);
        SendMessageW(m_hPhoneEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SetWindowTextW(m_hPhoneEdit, m_phone.c_str());
        startY += rowHeight;

        // Фамилия
        hLabel = CreateWindowExW(0, L"STATIC", L"Фамилия:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, startY, labelWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_LASTNAME_LABEL, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hLastNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            startX + labelWidth + 10, startY, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_LASTNAME, g_hInstance, nullptr);
        SendMessageW(m_hLastNameEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hLastNameEdit, EM_SETLIMITTEXT, 40, 0);
        startY += rowHeight;

        // Имя
        hLabel = CreateWindowExW(0, L"STATIC", L"Имя:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, startY, labelWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_FIRSTNAME_LABEL, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hFirstNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            startX + labelWidth + 10, startY, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_FIRSTNAME, g_hInstance, nullptr);
        SendMessageW(m_hFirstNameEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hFirstNameEdit, EM_SETLIMITTEXT, 40, 0);
        startY += rowHeight;

        // Отчество
        hLabel = CreateWindowExW(0, L"STATIC", L"Отчество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, startY, labelWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_MIDDLENAME_LABEL, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hMiddleNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            startX + labelWidth + 10, startY, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_MIDDLENAME, g_hInstance, nullptr);
        SendMessageW(m_hMiddleNameEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hMiddleNameEdit, EM_SETLIMITTEXT, 40, 0);
        startY += rowHeight;

        // E-mail
        hLabel = CreateWindowExW(0, L"STATIC", L"E-mail (опционально):",
            WS_VISIBLE | WS_CHILD | SS_RIGHT,
            startX, startY, labelWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_EMAIL_LABEL, g_hInstance, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);

        m_hEmailEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            startX + labelWidth + 10, startY, editWidth, editHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_EMAIL, g_hInstance, nullptr);
        SendMessageW(m_hEmailEdit, WM_SETFONT, (WPARAM)m_hFontEdit, TRUE);
        SendMessageW(m_hEmailEdit, EM_SETLIMITTEXT, 64, 0);
        startY += rowHeight + 10;

        // Статус с несколькими строками
     
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            startX, startY, labelWidth + editWidth + 10, 70,   // БЫЛО: 30
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_STATUS, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFontLabel, TRUE);
        startY += 80;   // БЫЛО: startY += 40
     
        // Кнопки
        int btnWidth = 180, btnHeight = 40, gap = 20;
        int totalBtns = 2;
        int startBtnX = (clientWidth - (btnWidth * totalBtns + gap * (totalBtns - 1))) / 2;

        HWND hSubmit = CreateWindowExW(0, L"BUTTON", L"Зарегистрировать",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            startBtnX, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_SUBMIT, g_hInstance, nullptr);
        SendMessageW(hSubmit, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);

        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Отмена",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            startBtnX + btnWidth + gap, startY, btnWidth, btnHeight,
            m_hWnd, (HMENU)(INT_PTR)IDC_WORKER_REG_CANCEL, g_hInstance, nullptr);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)m_hFontButton, TRUE);

        // После создания кнопок добавить:
        g_logger.info(L"WorkerRegistrationWindow: buttons created at Y=" +
            std::to_wstring(startY) + L", window height=" +
            std::to_wstring(clientHeight));

        // Если кнопки выходят за пределы окна — логируем предупреждение
        if (startY + btnHeight > clientHeight) {
            g_logger.warning(L"WorkerRegistrationWindow: buttons may be outside visible area! ");
            g_logger.warning(L"btnBottom=" + std::to_wstring(startY + btnHeight) +
                L", clientHeight=" + std::to_wstring(clientHeight));
        }

        // Устанавливаем фокус на фамилию
        SetFocus(m_hLastNameEdit);
    }
        
    void onSubmit() {
        g_logger.info(L"[onSubmit] Starting registration for commodity expert...");

        wchar_t buf[128];

        GetWindowTextW(m_hLastNameEdit, buf, 128);
        std::wstring lastName = std::wstring(buf);
        if (!RegUtils::isValidName(lastName, 40)) {
            SetWindowTextW(m_hStatusLabel, L"Фамилия: от 1 до 40 букв");
            SetFocus(m_hLastNameEdit);
            return;
        }

        GetWindowTextW(m_hFirstNameEdit, buf, 128);
        std::wstring firstName = std::wstring(buf);
        if (!RegUtils::isValidName(firstName, 40)) {
            SetWindowTextW(m_hStatusLabel, L"Имя: от 1 до 40 букв");
            SetFocus(m_hFirstNameEdit);
            return;
        }

        GetWindowTextW(m_hMiddleNameEdit, buf, 128);
        std::wstring middleName = std::wstring(buf);
        if (!RegUtils::trim(middleName).empty() && !RegUtils::isValidName(middleName, 40)) {
            SetWindowTextW(m_hStatusLabel, L"Отчество: от 1 до 40 букв");
            SetFocus(m_hMiddleNameEdit);
            return;
        }

        GetWindowTextW(m_hEmailEdit, buf, 128);
        std::wstring email = std::wstring(buf);
        if (!RegUtils::isValidEmail(email)) {
            SetWindowTextW(m_hStatusLabel, L"Некорректный e-mail");
            SetFocus(m_hEmailEdit);
            return;
        }

        // Отключаем кнопки во время запроса
        EnableWindow(GetDlgItem(m_hWnd, IDC_WORKER_REG_SUBMIT), FALSE);
        EnableWindow(GetDlgItem(m_hWnd, IDC_WORKER_REG_CANCEL), FALSE);
        SetWindowTextW(m_hStatusLabel, L"Отправка данных...");

        // Формируем запрос на регистрацию с ролью 'worker'
        json request;
        request["phone"] = wstring_to_utf8(m_phone);
        request["last_name"] = wstring_to_utf8(lastName);
        request["first_name"] = wstring_to_utf8(firstName);
        request["middle_name"] = wstring_to_utf8(middleName);
        request["email"] = wstring_to_utf8(email);
        request["items_submitted"] = 0;
        request["items_sold"] = 0;
        request["role"] = m_role;   // <-- роль 

        g_logger.info(L"WorkerRegistrationWindow: sending registration request for role=" +
            utf8_to_wstring(m_role) + L", phone=" + m_phone);   

        auto response = g_httpsClient.post(L"/api/v1/clients/register", request, L"");

        EnableWindow(GetDlgItem(m_hWnd, IDC_WORKER_REG_SUBMIT), TRUE);
        EnableWindow(GetDlgItem(m_hWnd, IDC_WORKER_REG_CANCEL), TRUE);

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            // Сохраняем JWT-токены (сервер возвращает их всегда)
            if (response->contains("access_token") && response->contains("refresh_token")) {
                std::string access = (*response)["access_token"].get<std::string>();
                std::string refresh = (*response)["refresh_token"].get<std::string>();
                int64_t expires = (*response)["expires_at"].get<int64_t>();
                g_authManager.setAuthTokens(access, refresh, expires, RegUtils::wstring_to_utf8(m_phone));
                g_logger.info(L"Tokens saved for " + RegUtils::normalizePhone(m_phone));
            }
            else {
                g_logger.warning(L"Server response success but no tokens returned");
            }
			// Показываем сгенерированный пароль в модальном окне
            if (response->contains("password") && (*response)["password"].is_string()) {
                std::string generatedPassword = (*response)["password"].get<std::string>();
                m_generatedPassword = generatedPassword;   // для автовхода в LoginWindow
                std::wstring firstLine = (m_role == "director")
                    ? L"Директор магазина успешно зарегистрирован!"
                    : L"Товаровед успешно зарегистрирован!";
                const wchar_t* whoToGive = (m_role == "director") ? L"директору" : L"товароведу";
                std::wstring passwordMsg = firstLine +
                    L"\n\nСгенерированный пароль для входа:\n\n"
                    L"    " + utf8_to_wstring(generatedPassword) + L"\n\n"
                    L"Передайте этот пароль " + whoToGive + L".\n"
                    L"Пароль показывается только один раз.";
                g_logger.info(L"WorkerRegistrationWindow: generated password displayed to admin for " + m_phone);
                MessageBoxW(m_hWnd, passwordMsg.c_str(),
                    (m_role == "director") ? L"Пароль для входа директора" : L"Пароль для входа товароведа",
                    MB_OK | MB_ICONINFORMATION);
            }
            // Регистрация успешна
            g_logger.info(L"WorkerRegistrationWindow: registration successful for " + m_phone);
            SetWindowTextW(m_hStatusLabel, L"Регистрация успешна!");
            // Даём пользователю увидеть сообщение, затем закрываем окно с IDOK
            m_dialogResult = IDOK;
            // Небольшая задержка перед закрытием
            SetTimer(m_hWnd, 1, 800, [](HWND h, UINT, UINT_PTR, DWORD) {
                KillTimer(h, 1);
                DestroyWindow(h);
            });
        }
        else {
            // Ошибка регистрации
            std::wstring errMsg = L"Ошибка регистрации.";
            if (response && response->contains("error")) {
                errMsg += L"\r\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            SetWindowTextW(m_hStatusLabel, errMsg.c_str());
            g_logger.error(L"WorkerRegistrationWindow: registration failed for " + m_phone + L" - " + errMsg);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WorkerRegistrationWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<WorkerRegistrationWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createFontsAndBrushes();
            pThis->createControls();
            return 0;
        }

        pThis = reinterpret_cast<WorkerRegistrationWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                if (id == IDC_WORKER_REG_SUBMIT) {
                    pThis->onSubmit();
                    return 0;
                }
                else if (id == IDC_WORKER_REG_CANCEL) {
                    pThis->m_dialogResult = IDCANCEL;
                    DestroyWindow(hWnd);
                    return 0;
                }
            }
            break;
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            HWND hBtn = (HWND)lParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            if (GetDlgCtrlID(hBtn) == IDC_WORKER_REG_CANCEL) {
                SetBkColor(hdc, Config::BACK_BUTTON_COLOR);
                return (LRESULT)pThis->m_hRedBrush;
            }
            else {
                SetBkColor(hdc, Config::PRIMARY_COLOR);
                return (LRESULT)pThis->m_hGreenBrush;
            }
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            int id = GetDlgCtrlID(hStatic);
            SetBkMode(hdc, TRANSPARENT);
            if (id == IDC_WORKER_REG_TITLE) {
                SetTextColor(hdc, Config::PRIMARY_COLOR);
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            if (id == IDC_WORKER_REG_STATUS) {
                SetTextColor(hdc, RGB(211, 47, 47));
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            HWND hEdit = (HWND)lParam;
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, RGB(255, 255, 255));
            return (LRESULT)pThis->m_hWhiteBrush;
        }
        case WM_DESTROY:
            pThis->releaseFontsAndBrushes();
            // delete pThis; // управление памятью вне класса
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    WorkerRegistrationWindow(const std::wstring& phone, const std::string& role = "worker")
        : m_hWnd(nullptr), m_phone(phone), m_role(role), m_dialogResult(IDCANCEL),
        m_hPhoneEdit(nullptr), m_hLastNameEdit(nullptr), m_hFirstNameEdit(nullptr),
        m_hMiddleNameEdit(nullptr), m_hEmailEdit(nullptr), m_hStatusLabel(nullptr),
        m_hFontTitle(nullptr), m_hFontButton(nullptr), m_hFontLabel(nullptr), m_hFontEdit(nullptr),
        m_hGreenBrush(nullptr), m_hRedBrush(nullptr), m_hWhiteBrush(nullptr)
    {
    }

    ~WorkerRegistrationWindow() {
        // Ресурсы освобождаются в WM_DESTROY
    }

    // возвращает пароль, сгенерированный сервером (пусто — если регистрация не удалась)
    const std::string& getGeneratedPassword() const {
        return m_generatedPassword;
    }

    /**
     * Показывает модальное окно регистрации товароведа.
     * @param hParent родительское окно (обычно LoginWindow)
     * @return IDOK, если регистрация успешна, иначе IDCANCEL
     */
    int show(HWND hParent) {
        g_logger.info(L"WorkerRegistrationWindow::show() called, phone=" + m_phone);
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
            if (!RegisterClassExW(&wcex)) { g_logger.error(L"WorkerRegistrationWindow: RegisterClassExW failed"); return IDCANCEL; }
            classRegistered = true;
        }

        // =========================================================================
        // ИСПРАВЛЕНИЕ: высота окна выводится из раскладки createControls():
        // низ кнопок «Зарегистрировать»/«Отмена» = 420, запас = 40 → клиент 460.
        // AdjustWindowRectW гарантирует, что кнопки НЕ будут обрезаны
        // неклиентской областью при любой теме/DPI (устраняет класс дефекта,
        // зафиксированный на скриншоте).
        // =========================================================================
        const DWORD dwStyle = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
        const int CLIENT_WIDTH = 550;
        const int CLIENT_HEIGHT = 500;

        const int ncWidth = 2 * (GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER));
        const int ncHeight = GetSystemMetrics(SM_CYCAPTION)
            + 2 * (GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER));
        int width = CLIENT_WIDTH + ncWidth;
        int height = CLIENT_HEIGHT + ncHeight;
        g_logger.info(L"WorkerRegistrationWindow: computed window size = " +
            std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L" (client " + std::to_wstring(CLIENT_WIDTH) + L"x" + std::to_wstring(CLIENT_HEIGHT) + L")");

        int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        // показываем в зависимости от роли
        const wchar_t* windowCaption = (m_role == "director")
            ? L"Регистрация директора магазина"
            : L"Регистрация товароведа";
        m_hWnd = CreateWindowExW(WS_EX_WINDOWEDGE, CLASS_NAME, windowCaption,
            dwStyle, x, y, width, height, hParent, nullptr, g_hInstance, this);

        if (!m_hWnd) { g_logger.error(L"WorkerRegistrationWindow: CreateWindowExW failed"); return IDCANCEL; }

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
        g_logger.info(L"WorkerRegistrationWindow: closed, result=" + std::to_wstring(m_dialogResult));
        return m_dialogResult;
    }
};

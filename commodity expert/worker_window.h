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
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "string_utils.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;

using json = nlohmann::json;

class WorkerWindow {
private:
    HWND m_hWnd;
    HWND m_hListBox;
    HWND m_hAcceptBtn;
    HWND m_hServeBtn;
    HWND m_hStatusLabel;
    HFONT m_hFont;
    HBRUSH m_hBrush;
    std::vector<json> m_tickets;
    std::mutex m_mutex;
    bool m_running;
    std::thread m_refreshThread;
    ISpVoice* m_pVoice = nullptr;

    static constexpr int REFRESH_INTERVAL_MS = 5000;

    void initializeTTS() {
        if (FAILED(CoInitialize(NULL))) {
            g_logger.error(L"CoInitialize failed for TTS");
            return;
        }
        HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (FAILED(hr)) {
            g_logger.error(L"Failed to create TTS voice");
        }
        else {
            g_logger.info(L"TTS initialized successfully");
        }
    }

    void speak(const std::wstring& ticketNumber) {
        if (m_pVoice) {
            std::wstring full = L"Клиент с номером " + ticketNumber + L" пройдите к окну номер 1";
            m_pVoice->Speak(full.c_str(), SPF_ASYNC, NULL);
            g_logger.info(L"TTS: " + full);
        }
        else {
            g_logger.warning(L"TTS not available");
        }
    }

    void refreshList() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto response = g_httpsClient.get(L"/api/v1/queue/first_time/waiting", L"");
        // Проверяем, что ответ есть и содержит поле "tickets"
        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            m_tickets = (*response)["tickets"].get<std::vector<json>>();
        }
        else {
            g_logger.warning(L"Failed to fetch waiting tickets");
            m_tickets.clear();
        }
        updateUI();
    }

    void updateUI() {
        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);
        for (const auto& ticket : m_tickets) {
            std::string number = ticket["ticket_number"].get<std::string>();
            std::wstring item = utf8_to_wstring(number);
            SendMessageW(m_hListBox, LB_ADDSTRING, 0, (LPARAM)item.c_str());
        }
        int count = (int)m_tickets.size();
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        SetWindowTextW(m_hStatusLabel, status.c_str());
        EnableWindow(m_hAcceptBtn, count > 0);
    }

    void onAccept() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон из списка", L"Информация", MB_OK);
            return;
        }
        wchar_t buf[64];
        SendMessageW(m_hListBox, LB_GETTEXT, sel, (LPARAM)buf);
        std::wstring ticketNumber = buf;

        json request;
        request["ticket_number"] = wstring_to_utf8(ticketNumber);
        auto response = g_httpsClient.post(L"/api/v1/queue/first_time/accept", request, L"");
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            speak(ticketNumber);
            // Удаляем из локального списка
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_tickets.begin(), m_tickets.end(),
                    [&](const json& j) { return utf8_to_wstring(j["ticket_number"].get<std::string>()) == ticketNumber; });
                if (it != m_tickets.end()) m_tickets.erase(it);
                updateUI();
            }
            // Через 5 минут обновим список (на случай, если сервер ещё не удалил)
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::minutes(5));
                refreshList();
                }).detach();
            g_logger.info(L"Accepted ticket: " + ticketNumber);
        }
        else {
            MessageBoxW(m_hWnd, L"Не удалось принять талон. Возможно, он уже принят.", L"Ошибка", MB_OK);
            refreshList();
        }
    }

    void onServe() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон", L"Информация", MB_OK);
            return;
        }
        wchar_t buf[64];
        SendMessageW(m_hListBox, LB_GETTEXT, sel, (LPARAM)buf);
        std::wstring ticketNumber = buf;

        json request;
        request["ticket_number"] = wstring_to_utf8(ticketNumber);
        auto response = g_httpsClient.post(L"/api/v1/queue/first_time/serve", request, L"");
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            g_logger.info(L"Served ticket: " + ticketNumber);
            // Удаляем из локального списка
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_tickets.begin(), m_tickets.end(),
                    [&](const json& j) { return utf8_to_wstring(j["ticket_number"].get<std::string>()) == ticketNumber; });
                if (it != m_tickets.end()) m_tickets.erase(it);
                updateUI();
            }
        }
        else {
            MessageBoxW(m_hWnd, L"Не удалось отметить как обслуженный.", L"Ошибка", MB_OK);
            refreshList();
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WorkerWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            pThis = (WorkerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            pThis->refreshList();
            pThis->m_running = true;
            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS));
                    pThis->refreshList();
                }
                });
            return 0;
        }
        pThis = (WorkerWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case 1: pThis->onAccept(); break;
            case 2: pThis->onServe(); break;
            case 3: PostQuitMessage(0); break;
            }
            return 0;
        case WM_DESTROY:
            pThis->m_running = false;
            if (pThis->m_refreshThread.joinable()) pThis->m_refreshThread.join();
            if (pThis->m_pVoice) { pThis->m_pVoice->Release(); pThis->m_pVoice = nullptr; }
            CoUninitialize();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        // Список
        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY,
            20, 20, width - 40, height - 150,
            m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Статус
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, height - 120, width - 40, 30,
            m_hWnd, (HMENU)101, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Кнопки
        int btnW = 250, btnH = 50;
        int x1 = (width - 3 * btnW - 20) / 2;
        int yBtn = height - 80;

        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, yBtn, btnW, btnH,
            m_hWnd, (HMENU)1, g_hInstance, nullptr);
        SendMessageW(m_hAcceptBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1 + btnW + 10, yBtn, btnW, btnH,
            m_hWnd, (HMENU)2, g_hInstance, nullptr);
        SendMessageW(m_hServeBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1 + 2 * (btnW + 10), yBtn, btnW, btnH,
            m_hWnd, (HMENU)3, g_hInstance, nullptr);
        SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    }

public:
    WorkerWindow() : m_hWnd(nullptr), m_hListBox(nullptr), m_hAcceptBtn(nullptr),
        m_hServeBtn(nullptr), m_hStatusLabel(nullptr), m_hFont(nullptr), m_hBrush(nullptr),
        m_running(false) {
        initializeTTS();
    }

    ~WorkerWindow() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hBrush) DeleteObject(m_hBrush);
    }

    void show() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"WorkerWindowClass";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        m_hWnd = CreateWindowExW(0, L"WorkerWindowClass", L"Товаровед - Очередь первых раз",
            WS_OVERLAPPEDWINDOW,
            0, 0, screenW, screenH,
            nullptr, nullptr, g_hInstance, this);
        if (!m_hWnd) return;

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};
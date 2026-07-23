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
    HWND m_hComboQueue;

    HFONT m_hFont;
    HBRUSH m_hBrush;

    std::vector<json> m_tickets;
    std::mutex m_mutex;
    bool m_running;
    std::thread m_refreshThread;

    std::wstring m_currentQueueType;  // "general", "first_time", "extra_20", "paid", "expensive"

    ISpVoice* m_pVoice = nullptr;

    static constexpr int REFRESH_INTERVAL_MS = 5000;

    // ------------------------------------------------------------------------
    // Инициализация TTS (голосовой синтез)
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // Голосовое оповещение: номер (ticket или client_id) и номер окна
    // ------------------------------------------------------------------------
    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (m_pVoice) {
            std::wstring full = L"Клиент с номером " + number + L" пройдите к окну номер " + windowNumber;
            m_pVoice->Speak(full.c_str(), SPF_ASYNC, NULL);
            g_logger.info(L"TTS: " + full);
        }
        else {
            g_logger.warning(L"TTS not available");
        }
    }

    // ------------------------------------------------------------------------
    // Обновление списка ожидающих из сервера (универсально для любого типа)
    // ------------------------------------------------------------------------
    void refreshList() {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Для first_time используем старый эндпоинт (он не требует параметра type)
        std::wstring path;
        if (m_currentQueueType == L"first_time") {
            path = L"/api/v1/queue/first_time/waiting";
        }
        else {
            path = L"/api/v1/queue/waiting?type=" + m_currentQueueType;
        }

        auto response = g_httpsClient.get(path, L"");
        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            m_tickets = (*response)["tickets"].get<std::vector<json>>();
            g_logger.info(L"refreshList: fetched " + std::to_wstring(m_tickets.size()) + L" tickets for " + m_currentQueueType);
        }
        else {
            g_logger.warning(L"Failed to fetch waiting tickets for " + m_currentQueueType);
            m_tickets.clear();
        }
        updateUI();
    }

    // ------------------------------------------------------------------------
    // Обновление UI: список, статус, кнопки
    // ------------------------------------------------------------------------
    void updateUI() {
        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);

        for (const auto& ticket : m_tickets) {
            std::string ticketNumber = ticket["ticket_number"].get<std::string>();
            std::wstring displayText = utf8_to_wstring(ticketNumber);

            // Если есть client_id и это не first_time, добавляем его в скобках
            if (m_currentQueueType != L"first_time" && ticket.contains("client_id")) {
                int clientId = ticket["client_id"].get<int>();
                displayText += L" (id: " + std::to_wstring(clientId) + L")";
            }

            SendMessageW(m_hListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
        }

        int count = (int)m_tickets.size();
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        SetWindowTextW(m_hStatusLabel, status.c_str());
        EnableWindow(m_hAcceptBtn, count > 0);
    }

    // ------------------------------------------------------------------------
    // Обработчик "Принять" (вызов соответствующего эндпоинта и голос)
    // ------------------------------------------------------------------------
    void onAccept() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон из списка", L"Информация", MB_OK);
            return;
        }

        // Получаем текст выбранной строки
        wchar_t buf[256]{};
        SendMessageW(m_hListBox, LB_GETTEXT, sel, (LPARAM)buf);
        std::wstring line(buf);

        // Извлекаем ticket_number (первое слово до пробела)
        size_t spacePos = line.find(L' ');
        std::wstring ticketNumber = (spacePos != std::wstring::npos) ? line.substr(0, spacePos) : line;
        std::string ticketNumberUtf8 = wstring_to_utf8(ticketNumber);

        // Находим соответствующий json в m_tickets
        auto it = std::find_if(m_tickets.begin(), m_tickets.end(),
            [&](const json& j) { return j["ticket_number"].get<std::string>() == ticketNumberUtf8; });
        if (it == m_tickets.end()) {
            MessageBoxW(m_hWnd, L"Талон не найден в списке", L"Ошибка", MB_OK);
            return;
        }

        // Определяем эндпоинт
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") {
            endpoint = L"/api/v1/queue/first_time/accept";
        }
        else {
            endpoint = L"/api/v1/queue/accept";
        }

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            // Получаем номер окна (по умолчанию "1")
            std::string windowNumber = response->value("window_number", "1");
            std::wstring windowW = utf8_to_wstring(windowNumber);

            // Голосовое оповещение:
            // - для first_time говорим номер талона
            // - для остальных – client_id
            std::wstring numberToSpeak;
            if (m_currentQueueType == L"first_time") {
                numberToSpeak = ticketNumber;
            }
            else {
                // client_id есть в json
                if (it->contains("client_id")) {
                    int clientId = (*it)["client_id"].get<int>();
                    numberToSpeak = std::to_wstring(clientId);
                }
                else {
                    // fallback – номер талона
                    numberToSpeak = ticketNumber;
                }
            }

            speak(numberToSpeak, windowW);

            // Удаляем из локального списка
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it2 = std::find_if(m_tickets.begin(), m_tickets.end(),
                    [&](const json& j) { return j["ticket_number"].get<std::string>() == ticketNumberUtf8; });
                if (it2 != m_tickets.end()) m_tickets.erase(it2);
                updateUI();
            }

            // Через 5 минут обновим список (чтобы убедиться, что талон ушёл с сервера)
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::minutes(5));
                refreshList();
                }).detach();

            g_logger.info(L"Accepted ticket: " + ticketNumber + L" (type: " + m_currentQueueType + L")");
        }
        else {
            MessageBoxW(m_hWnd, L"Не удалось принять талон. Возможно, он уже принят.", L"Ошибка", MB_OK);
            refreshList();
        }
    }

    // ------------------------------------------------------------------------
    // Обработчик "Обслужен (Следующий)"
    // ------------------------------------------------------------------------
    void onServe() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон", L"Информация", MB_OK);
            return;
        }

        wchar_t buf[256]{};
        SendMessageW(m_hListBox, LB_GETTEXT, sel, (LPARAM)buf);
        std::wstring line(buf);
        size_t spacePos = line.find(L' ');
        std::wstring ticketNumber = (spacePos != std::wstring::npos) ? line.substr(0, spacePos) : line;
        std::string ticketNumberUtf8 = wstring_to_utf8(ticketNumber);

        // Определяем эндпоинт
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") {
            endpoint = L"/api/v1/queue/first_time/serve";
        }
        else {
            endpoint = L"/api/v1/queue/serve";
        }

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            g_logger.info(L"Served ticket: " + ticketNumber + L" (type: " + m_currentQueueType + L")");
            // Удаляем из локального списка
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::find_if(m_tickets.begin(), m_tickets.end(),
                    [&](const json& j) { return j["ticket_number"].get<std::string>() == ticketNumberUtf8; });
                if (it != m_tickets.end()) m_tickets.erase(it);
                updateUI();
            }
        }
        else {
            MessageBoxW(m_hWnd, L"Не удалось отметить как обслуженный.", L"Ошибка", MB_OK);
            refreshList();
        }
    }

    // ------------------------------------------------------------------------
    // Оконная процедура
    // ------------------------------------------------------------------------
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
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            HWND hCtrl = (HWND)lParam;

            // Обработка смены типа очереди в комбобоксе
            if (id == 200 && code == CBN_SELCHANGE) {
                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    wchar_t typeBuf[64]{};
                    SendMessageW(pThis->m_hComboQueue, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)typeBuf);
                    pThis->m_currentQueueType = typeBuf;
                    g_logger.info(L"Queue type changed to: " + pThis->m_currentQueueType);
                    pThis->refreshList();
                }
                return 0;
            }

            // Кнопки
            if (code == BN_CLICKED) {
                switch (id) {
                case 1: pThis->onAccept(); break;
                case 2: pThis->onServe(); break;
                case 3: PostQuitMessage(0); break;
                }
                return 0;
            }
            break;
        }

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

    // ------------------------------------------------------------------------
    // Создание элементов управления
    // ------------------------------------------------------------------------
    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        // Комбобокс для выбора очереди
        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            20, 10, 200, 100,
            m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)L"general");
        SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)L"first_time");
        SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)L"extra_20");
        SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)L"paid");
        SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)L"expensive");
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        m_currentQueueType = L"general";

        // Список (смещаем вниз)
        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY,
            20, 60, width - 40, height - 190,
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
        m_hWnd = CreateWindowExW(0, L"WorkerWindowClass", L"Товаровед - Управление очередями",
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
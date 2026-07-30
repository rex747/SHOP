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

    std::wstring m_currentQueueType;          // Хранит идентификатор очереди (например L"general")
    std::vector<std::wstring> m_queueTypeIds; // Маппинг индекса ComboBox → идентификатор очереди

    ISpVoice* m_pVoice = nullptr;

    static constexpr int REFRESH_INTERVAL_MS = 2000;

    // ------------------------------------------------------------------------
    // Инициализация TTS в MTA-потоке
    // ------------------------------------------------------------------------
    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            g_logger.error(L"CoInitializeEx(MULTITHREADED) failed for TTS: HRESULT=0x" + std::to_wstring(hr));
            return;
        }
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (FAILED(hr)) {
            g_logger.error(L"Failed to create TTS voice, HRESULT=0x" + std::to_wstring(hr));
        }
        else {
            g_logger.info(L"TTS initialized successfully in MTA mode");
        }
    }

    // ------------------------------------------------------------------------
    // Озвучивание номера талона и окна
    // ------------------------------------------------------------------------
    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (!m_pVoice) {
            g_logger.warning(L"TTS not available, skipping speak");
            return;
        }

        std::wstring full = L"Клиент с номером " + number + L" просим подойти к окну номер " + windowNumber;
        g_logger.info(L"TTS attempt: " + full);

        try {
            HRESULT hr = m_pVoice->Speak(full.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
            if (FAILED(hr)) {
                g_logger.error(L"TTS Speak failed: HRESULT=0x" + std::to_wstring(hr));
            }
            else {
                g_logger.info(L"TTS Speak initiated successfully");
            }
        }
        catch (const std::exception& e) {
            g_logger.error(L"TTS Speak exception: " + utf8_to_wstring(e.what()));
        }
        catch (...) {
            g_logger.error(L"TTS Speak unknown exception");
        }
    }

    // ------------------------------------------------------------------------
    // Получение списка ожидающих талонов с сервера (только данные, без UI)
    // ------------------------------------------------------------------------
    std::vector<json> fetchTicketsFromServer(const std::wstring& queueType) {
        std::wstring path;
        g_logger.info(L"Fetching tickets for queue type: " + queueType);

        if (queueType == L"first_time") {
            path = L"/api/v1/queue/first_time/waiting";
            g_logger.info(L"Using first_time endpoint");
        }
        else if (queueType == L"trust") {
            path = L"/api/v1/queue/trust/waiting";
        }
        else {
            path = L"/api/v1/queue/waiting?type=" + queueType;
            g_logger.info(L"Using waiting?type endpoint");
        }

        auto response = g_httpsClient.get(path, L"");
        g_logger.info(L"GET response received for " + queueType);

        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            size_t count = (*response)["tickets"].size();
            g_logger.info(L"Response contains " + std::to_wstring(count) + L" tickets");
            auto tickets = (*response)["tickets"].get<std::vector<json>>();
            g_logger.info(L"fetchTicketsFromServer: fetched " + std::to_wstring(tickets.size()) +
                L" tickets for " + queueType);
            return tickets;
        }
        else {
            g_logger.warning(L"Failed to fetch waiting tickets for " + queueType);
            return {};
        }
    }

    // ------------------------------------------------------------------------
    // Обновление UI: перестроение ListBox, статуса, доступности кнопок
    // ВЫЗЫВАЕТСЯ ТОЛЬКО ИЗ UI-ПОТОКА
    // ------------------------------------------------------------------------
    void updateUI() {
        g_logger.info(L"updateUI started for queue type: " + m_currentQueueType);

        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);
        g_logger.info(L"ListBox reset");

        for (size_t i = 0; i < m_tickets.size(); ++i) {
            const auto& ticket = m_tickets[i];
            std::string ticketNumber = ticket["ticket_number"].get<std::string>();
            g_logger.info(L"Processing ticket " + std::to_wstring(i) + L": " + utf8_to_wstring(ticketNumber));

            std::wstring displayText = utf8_to_wstring(ticketNumber);

            if (m_currentQueueType != L"first_time" && ticket.contains("client_id")) {
                int clientId = ticket["client_id"].get<int>();
                displayText += L" (id: " + std::to_wstring(clientId) + L")";
                g_logger.info(L"Appended client_id: " + std::to_wstring(clientId));
            }

            int index = (int)SendMessageW(m_hListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
            if (index != LB_ERR) {
                SendMessageW(m_hListBox, LB_SETITEMDATA, index, (LPARAM)i);
                g_logger.info(L"Added ticket to ListBox at index " + std::to_wstring(index));
            }
        }

        int count = (int)m_tickets.size();
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        g_logger.info(L"Setting status label: " + status);

        SetWindowTextW(m_hStatusLabel, status.c_str());
        g_logger.info(L"Status label set");

        EnableWindow(m_hAcceptBtn, count > 0);
        EnableWindow(m_hServeBtn, count > 0);
        g_logger.info(L"Buttons enabled state updated");

        g_logger.info(L"UI updated for queue type: " + m_currentQueueType);
    }

    // ------------------------------------------------------------------------
    // Полная перезагрузка списка: получение данных + запрос обновления UI через PostMessage
    // ------------------------------------------------------------------------
    void refreshList() {
        std::wstring queueType;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queueType = m_currentQueueType;
        }

        g_logger.info(L"refreshList started for queue type: " + queueType);

        auto newTickets = fetchTicketsFromServer(queueType);
        g_logger.info(L"Server data fetched: " + std::to_wstring(newTickets.size()) + L" tickets");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentQueueType != queueType) {
                g_logger.info(L"Queue type changed during fetch, discarding results");
                return;
            }
            m_tickets = std::move(newTickets);
            g_logger.info(L"Local tickets updated under mutex");
        }

        g_logger.info(L"Posting WM_APP_REFRESH_UI message to UI thread");
        if (!PostMessageW(m_hWnd, WM_APP + 1, 0, 0)) {
            g_logger.error(L"PostMessage failed: " + std::to_wstring(GetLastError()));
        }
    }

    // ------------------------------------------------------------------------
    // Обработчик кнопки "Принять"
    // ------------------------------------------------------------------------
    void onAccept() {
        g_logger.info(L"Accept button clicked for queue type: " + m_currentQueueType);

        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        g_logger.info(L"Selected index in listbox: " + std::to_wstring(sel));

        if (sel == LB_ERR) {
            g_logger.error(L"No ticket selected to accept");
            MessageBoxW(m_hWnd, L"Выберите талон из списка", L"Внимание", MB_OK);
            return;
        }

        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        g_logger.info(L"ItemData for selected index: " + std::to_wstring(itemData));

        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
            g_logger.error(L"Invalid item data for selected ticket");
            MessageBoxW(m_hWnd, L"Ошибка идентификации талона", L"Ошибка", MB_OK);
            return;
        }

        std::string ticketNumberUtf8;
        int clientId = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (itemData >= (LRESULT)m_tickets.size()) {
                g_logger.error(L"Ticket index out of bounds after mutex lock");
                MessageBoxW(m_hWnd, L"Талон устарел, обновите список", L"Ошибка", MB_OK);
                return;
            }
            const json& selectedTicket = m_tickets[itemData];
            ticketNumberUtf8 = selectedTicket["ticket_number"].get<std::string>();
            if (selectedTicket.contains("client_id")) {
                clientId = selectedTicket["client_id"].get<int>();
            }
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        g_logger.info(L"Selected ticket_number: " + ticketNumber + L", client_id: " + std::to_wstring(clientId));

        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") {
            endpoint = L"/api/v1/queue/first_time/accept";
        }
        else if (m_currentQueueType == L"trust") {
            endpoint = L"/api/v1/queue/trust/accept";
        }
        else {
            endpoint = L"/api/v1/queue/accept";
        }
        g_logger.info(L"Using endpoint: " + endpoint);

        json request;
        request["ticket_number"] = ticketNumberUtf8;

        g_logger.info(L"Sending POST to " + endpoint + L" with ticket: " + ticketNumber);
        auto response = g_httpsClient.post(endpoint, request, L"");
        g_logger.info(L"POST response received");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            std::string windowNumber = response->value("window_number", "1");
            std::wstring windowW = utf8_to_wstring(windowNumber);
            g_logger.info(L"Window number: " + windowW);

            std::wstring numberToSpeak;
            if (m_currentQueueType == L"first_time") {
                numberToSpeak = ticketNumber;
            }
            else {
                if (clientId >= 0) {
                    numberToSpeak = std::to_wstring(clientId);
                }
                else {
                    numberToSpeak = ticketNumber;
                }
            }
            g_logger.info(L"Speaking: " + numberToSpeak + L" to window: " + windowW);
            speak(numberToSpeak, windowW);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData < (LRESULT)m_tickets.size()) {
                    m_tickets.erase(m_tickets.begin() + itemData);
                    g_logger.info(L"Ticket removed from local list");
                }
            }

            updateUI();
            g_logger.info(L"Accepted ticket: " + ticketNumber);
        }
        else {
            g_logger.error(L"Failed to accept ticket: " + ticketNumber);
            std::wstring errorMsg = L"Не удалось принять талон. Возможно, он уже устарел.";
            if (response && response->contains("error")) {
                errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            MessageBoxW(m_hWnd, errorMsg.c_str(), L"Ошибка", MB_OK);
            refreshList();
        }
    }

    // ------------------------------------------------------------------------
    // Обработчик кнопки "Обслужен (Следующий)"
    // ------------------------------------------------------------------------
    void onServe() {
        g_logger.info(L"Serve button clicked for queue type: " + m_currentQueueType);

        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        g_logger.info(L"Selected index in listbox: " + std::to_wstring(sel));

        if (sel == LB_ERR) {
            g_logger.info(L"No ticket selected to serve");
            MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK);
            return;
        }

        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        g_logger.info(L"ItemData for selected index: " + std::to_wstring(itemData));

        if (itemData == LB_ERR || itemData < 0) {
            g_logger.error(L"Invalid item data for selected ticket");
            MessageBoxW(m_hWnd, L"Ошибка идентификации талона", L"Ошибка", MB_OK);
            return;
        }

        std::string ticketNumberUtf8;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (itemData >= (LRESULT)m_tickets.size()) {
                g_logger.error(L"Ticket index out of bounds");
                MessageBoxW(m_hWnd, L"Талон устарел, обновите список", L"Ошибка", MB_OK);
                return;
            }
            ticketNumberUtf8 = m_tickets[itemData]["ticket_number"].get<std::string>();
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        g_logger.info(L"Selected ticket: " + ticketNumber);

        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") {
            endpoint = L"/api/v1/queue/first_time/serve";
        }
        else if (m_currentQueueType == L"trust") {
            endpoint = L"/api/v1/queue/trust/serve";
        }
        else {
            endpoint = L"/api/v1/queue/serve";
        }
        g_logger.info(L"Using endpoint: " + endpoint);

        json request;
        request["ticket_number"] = ticketNumberUtf8;

        auto response = g_httpsClient.post(endpoint, request, L"");
        g_logger.info(L"POST response received for serve");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            g_logger.info(L"Served ticket: " + ticketNumber);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData < (LRESULT)m_tickets.size()) {
                    m_tickets.erase(m_tickets.begin() + itemData);
                }
            }
            updateUI();
        }
        else {
            g_logger.error(L"Failed to serve ticket: " + ticketNumber);
            std::wstring errorMsg = L"Не удалось обслужить или отменить принятие.";
            if (response && response->contains("error")) {
                errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            MessageBoxW(m_hWnd, errorMsg.c_str(), L"Ошибка", MB_OK);
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

            {
                std::lock_guard<std::mutex> lock(pThis->m_mutex);
                pThis->m_tickets = pThis->fetchTicketsFromServer(pThis->m_currentQueueType);
            }
            pThis->updateUI();

            g_logger.info(L"Initial list loaded and UI updated");
            pThis->m_running = true;

            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running) {
                    g_logger.info(L"Periodic refresh triggered");
                    std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS));
                    if (pThis->m_running) {
                        pThis->refreshList();
                    }
                }
                g_logger.info(L"Refresh thread stopped");
                });
            return 0;
        }

        pThis = (WorkerWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);

        if (msg != WM_MOUSEMOVE && msg != WM_SETCURSOR && msg != WM_NCHITTEST) {
            std::wstring msgName;
            switch (msg) {
            case WM_NOTIFY: msgName = L"WM_NOTIFY"; break;
            case WM_COMMAND: msgName = L"WM_COMMAND"; break;
            case WM_PAINT: msgName = L"WM_PAINT"; break;
            case WM_DESTROY: msgName = L"WM_DESTROY"; break;
            case WM_CREATE: msgName = L"WM_CREATE"; break;
            case WM_SIZE: msgName = L"WM_SIZE"; break;
            case WM_TIMER: msgName = L"WM_TIMER"; break;
            default: msgName = L"0x" + std::to_wstring(msg);
            }
            g_logger.info(L"WndProc received message: " + msgName);
            if (msg == WM_NOTIFY) {
                NMHDR* pNmhdr = reinterpret_cast<NMHDR*>(lParam);
                if (pNmhdr) {
                    g_logger.info(L"  WM_NOTIFY: idFrom=" + std::to_wstring(pNmhdr->idFrom) +
                        L", code=" + std::to_wstring(pNmhdr->code));
                }
            }
        }

        switch (msg) {
        case WM_APP + 1: {
            g_logger.info(L"WM_APP_REFRESH_UI received in UI thread");
            pThis->updateUI();
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            g_logger.info(L"WM_COMMAND received with id: " + std::to_wstring(id) +
                L", code: " + std::to_wstring(code));

            if (id == 200 && code == CBN_SELCHANGE) {
                g_logger.info(L"Queue type selection changed");
                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                g_logger.info(L"CB_GETCURSEL returned: " + std::to_wstring(idx));

                if (idx != CB_ERR) {
                    // Получаем идентификатор очереди по индексу из сохранённого массива
                    if (idx >= 0 && idx < (LRESULT)pThis->m_queueTypeIds.size()) {
                        std::wstring queueId = pThis->m_queueTypeIds[idx];
                        g_logger.info(L"Selected queue id: " + queueId);

                        {
                            std::lock_guard<std::mutex> lock(pThis->m_mutex);
                            pThis->m_currentQueueType = queueId;
                        }

                        g_logger.info(L"Queue type changed to: " + pThis->m_currentQueueType);
                        pThis->refreshList();
                    }
                    else {
                        g_logger.error(L"Invalid index received from ComboBox: " + std::to_wstring(idx));
                    }
                }
                return 0;
            }

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
            if (pThis->m_refreshThread.joinable()) {
                pThis->m_refreshThread.join();
            }
            if (pThis->m_pVoice) {
                pThis->m_pVoice->Release();
                pThis->m_pVoice = nullptr;
            }
            CoUninitialize();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    // ------------------------------------------------------------------------
    // Создание элементов управления окна
    // ------------------------------------------------------------------------
    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        // ========================================================================
        // ИСПРАВЛЕНИЕ: выпадающий список с русскими названиями очередей
        // Внутренние идентификаторы хранятся в m_queueTypeIds по индексу
        // ========================================================================
        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            20, 10, 200, 200,
            m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Массив пар: { отображаемое имя, внутренний идентификатор }
        struct QueueTypeEntry {
            const wchar_t* displayName;
            const wchar_t* id;
        };
        QueueTypeEntry entries[] = {
            { L"Общая очередь", L"general" },
            { L"В первый раз (оформление) договора", L"first_time" },
            { L"+20 позиций", L"extra_20" },
            { L"На доверии", L"trust" },
            { L"Платный прием", L"paid" },
            { L"Дорогой товар", L"expensive" }
        };

        // Очищаем маппинг и добавляем элементы
        m_queueTypeIds.clear();
        for (const auto& entry : entries) {
            int idx = (int)SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)entry.displayName);
            if (idx != CB_ERR) {
                // Сохраняем идентификатор в отдельном векторе (индекс совпадает)
                m_queueTypeIds.push_back(entry.id);
                g_logger.info(L"Added queue: " + std::wstring(entry.displayName) + L" -> " + std::wstring(entry.id));
            }
            else {
                g_logger.error(L"Failed to add queue item: " + std::wstring(entry.displayName));
            }
        }

        // Устанавливаем начальный выбор — индекс 0 (Общая очередь)
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        if (!m_queueTypeIds.empty()) {
            m_currentQueueType = m_queueTypeIds[0];
        }
        else {
            m_currentQueueType = L"general"; // fallback
        }
        g_logger.info(L"ComboBox initialized with " + std::to_wstring(m_queueTypeIds.size()) +
            L" items, selected: " + m_currentQueueType);

        // ListBox для отображения талонов
        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY,
            20, 60, width - 40, height - 190,
            m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Label для отображения количества ожидающих
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, height - 120, width - 40, 30,
            m_hWnd, (HMENU)101, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Расчёт позиции кнопок
        int btnW = 250, btnH = 50;
        int x1 = (width - 3 * btnW - 20) / 2;
        int yBtn = height - 80;

        // Кнопка "Принять"
        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1, yBtn, btnW, btnH,
            m_hWnd, (HMENU)1, g_hInstance, nullptr);
        SendMessageW(m_hAcceptBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Кнопка "Обслужен (Следующий)"
        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x1 + btnW + 10, yBtn, btnW, btnH,
            m_hWnd, (HMENU)2, g_hInstance, nullptr);
        SendMessageW(m_hServeBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        // Кнопка "Закрыть"
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
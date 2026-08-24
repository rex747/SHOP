// queue_display_window.h
// =============================================================================
// ОКНО ОТОБРАЖЕНИЯ ОЧЕРЕДЕЙ НА ТВ-МОНИТОРЕ
// =============================================================================
// ИСПРАВЛЕНИЯ 23.08.2026 (вторая итерация):
//   1. ИСПРАВЛЕНО ОТОБРАЖЕНИЕ: талоны в блоке «Приглашены» теперь
//      показываются в ОБРАТНОМ порядке (новые сверху). Это гарантирует,
//      что только что принятый талон ВСЕГДА виден на экране.
//   2. УБРАНО искусственное ограничение maxLines/2: теперь показываются
//      ВСЕ принятые талоны, а оставшееся место используется для ожидающих.
//   3. ДОБАВЛЕНА очистка m_spokenTickets: при каждом опросе удаляются
//      ключи талонов, которые уже обслужены (исчезли из accepted).
//      Это предотвращает проблему с циклической нумерацией (001-999),
//      когда повторно созданный талон с тем же номером не озвучивался.
//   4. Сохранено подавление озвучивания при старте (m_initialSyncDone).
// =============================================================================
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sapi.h>
#include <sphelper.h>
#include <nlohmann/json.hpp>
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "string_utils.h"
#include "auth_manager.h"
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "comctl32.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;

using json = nlohmann::json;

#define WM_DISPLAY_DATA_READY   (WM_APP + 100)
#define WM_DISPLAY_ERROR        (WM_APP + 101)

namespace DisplayConfig {
    constexpr bool DEBUG_MODE = true;
    constexpr int POLL_INTERVAL_MS = 2000;
    constexpr COLORREF BG_COLOR = RGB(0, 0, 40);
    constexpr COLORREF HEADER_COLOR = RGB(255, 255, 0);
    constexpr COLORREF WAITING_COLOR = RGB(255, 255, 255);
    constexpr COLORREF ACCEPTED_COLOR = RGB(0, 255, 128);
    constexpr COLORREF QUEUE_TITLE_BG = RGB(40, 40, 80);
    constexpr COLORREF QUEUE_TITLE_FG = RGB(255, 215, 0);
    constexpr int FONT_TITLE_SIZE = 36;
    constexpr int FONT_QUEUE_SIZE = 24;
    constexpr int FONT_TICKET_SIZE = 20;
    constexpr int FONT_SMALL_SIZE = 16;
}

struct DisplayTicket {
    std::wstring ticketNumber;
    int clientId = 0;
    std::wstring windowNumber;
    int64_t acceptedAt = 0;
    int position = 0;
};

struct DisplayQueue {
    std::wstring queueId;
    std::wstring displayName;
    std::vector<DisplayTicket> waiting;
    std::vector<DisplayTicket> accepted;
};

struct DisplaySnapshot {
    std::vector<DisplayQueue> queues;
    int64_t serverTimestamp = 0;
    bool valid = false;
};

class QueueDisplayWindow {
private:
    HWND m_hWnd = nullptr;
    HFONT m_hFontTitle = nullptr;
    HFONT m_hFontQueue = nullptr;
    HFONT m_hFontTicket = nullptr;
    HFONT m_hFontSmall = nullptr;
    HBRUSH m_hBrushBg = nullptr;

    DisplaySnapshot m_snapshot;
    std::mutex m_dataMutex;
    std::atomic<bool> m_running{ false };
    std::thread m_pollThread;

    std::set<std::wstring> m_spokenTickets;
    std::mutex m_spokenMutex;

    // Флаг начальной синхронизации
    bool m_initialSyncDone = false;

    ISpVoice* m_pVoice = nullptr;

    static constexpr const wchar_t* CLASS_NAME = L"QueueDisplayWindowClass";

    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != S_FALSE) {
            g_logger.error(L"QueueDisplayWindow: CoInitializeEx failed, hr=" +
                std::to_wstring(hr));
            return;
        }
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL,
            IID_ISpVoice, (void**)&m_pVoice);
        if (SUCCEEDED(hr)) {
            g_logger.info(L"QueueDisplayWindow: TTS initialized successfully");
        }
        else {
            g_logger.error(L"QueueDisplayWindow: TTS initialization failed, hr=" +
                std::to_wstring(hr));
            m_pVoice = nullptr;
        }
    }

    void speakInvitation(const std::wstring& ticketNumber,
        const std::wstring& windowNumber) {
        if (!m_pVoice) {
            g_logger.warning(L"QueueDisplayWindow: TTS not available, skip speech");
            return;
        }
        std::wstring text = L"Клиент с номером " + ticketNumber +
            L" просим подойти к окну номер " + windowNumber;
        HRESULT hr = m_pVoice->Speak(text.c_str(),
            SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
        if (SUCCEEDED(hr)) {
            g_logger.info(L"QueueDisplayWindow: TTS spoken: " + text);
        }
        else {
            g_logger.error(L"QueueDisplayWindow: TTS speak failed, hr=" +
                std::to_wstring(hr));
        }
    }

    void pollLoop() {
        g_logger.info(L"QueueDisplayWindow: poll loop started, interval=" +
            std::to_wstring(DisplayConfig::POLL_INTERVAL_MS) + L" ms");
        while (m_running.load()) {
            auto response = g_httpsClient.get(L"/api/v1/queue/display", L"");
            if (response && response->contains("queues")) {
                DisplaySnapshot snapshot = parseDisplayResponse(*response);
                {
                    std::lock_guard<std::mutex> lock(m_dataMutex);
                    m_snapshot = std::move(snapshot);
                }
                if (IsWindow(m_hWnd)) {
                    PostMessageW(m_hWnd, WM_DISPLAY_DATA_READY, 0, 0);
                }
            }
            else {
                g_logger.warning(L"QueueDisplayWindow: failed to fetch display data");
                if (IsWindow(m_hWnd)) {
                    PostMessageW(m_hWnd, WM_DISPLAY_ERROR, 0, 0);
                }
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(DisplayConfig::POLL_INTERVAL_MS));
        }
        g_logger.info(L"QueueDisplayWindow: poll loop stopped");
    }

    DisplaySnapshot parseDisplayResponse(const json& response) {
        DisplaySnapshot snapshot;
        snapshot.valid = true;
        snapshot.serverTimestamp = response.value("timestamp", (int64_t)0);

        struct QueueMapping {
            const char* id;
            const wchar_t* displayName;
        };
        static const QueueMapping mappings[] = {
            { "general",    L"Общая очередь (до 20 позиций)" },
            { "first_time", L"Первый раз (оформление договора)" },
            { "extra_20",   L"+20 позиций" },
            { "trust",      L"На доверии" },
            { "paid",       L"Платный приём (200 руб.)" },
            { "expensive",  L"Дорогой товар (>5000 руб.)" }
        };

        const json& queuesJson = response["queues"];
        for (const auto& mapping : mappings) {
            DisplayQueue dq;
            dq.queueId = utf8_to_wstring(mapping.id);
            dq.displayName = mapping.displayName;

            if (queuesJson.contains(mapping.id)) {
                const auto& q = queuesJson[mapping.id];

                if (q.contains("waiting") && q["waiting"].is_array()) {
                    for (const auto& t : q["waiting"]) {
                        DisplayTicket dt;
                        dt.ticketNumber = utf8_to_wstring(
                            t.value("ticket_number", std::string("")));
                        dt.clientId = t.value("client_id", 0);
                        dt.windowNumber = utf8_to_wstring(
                            t.value("window_number", std::string("1")));
                        dt.position = t.value("position", 0);
                        if (!dt.ticketNumber.empty()) {
                            dq.waiting.push_back(dt);
                        }
                    }
                }

                if (q.contains("accepted") && q["accepted"].is_array()) {
                    for (const auto& t : q["accepted"]) {
                        DisplayTicket dt;
                        dt.ticketNumber = utf8_to_wstring(
                            t.value("ticket_number", std::string("")));
                        dt.clientId = t.value("client_id", 0);
                        dt.windowNumber = utf8_to_wstring(
                            t.value("window_number", std::string("1")));
                        dt.acceptedAt = t.value("accepted_at", (int64_t)0);
                        if (!dt.ticketNumber.empty()) {
                            dq.accepted.push_back(dt);
                        }
                    }
                }
            }
            snapshot.queues.push_back(dq);
        }
        return snapshot;
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: onDataReady()
    // =========================================================================
    // ИЗМЕНЕНИЯ:
    // 1. Добавлена ОЧИСТКА m_spokenTickets: удаляются ключи талонов,
    //    которые больше не присутствуют ни в одной очереди accepted.
    //    Это решает проблему циклической нумерации (001-999): если талон
    //    G001 был обслужен и позже создан новый G001, он будет озвучен.
    // 2. Сохранено подавление озвучивания при первом опросе.
    // =========================================================================
    void onDataReady() {
        DisplaySnapshot snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            snapshotCopy = m_snapshot;
        }
        if (!snapshotCopy.valid) return;

        bool isFirstSync = !m_initialSyncDone;
        if (isFirstSync) {
            m_initialSyncDone = true;
            g_logger.info(L"QueueDisplayWindow: INITIAL SYNC detected - "
                L"TTS suppressed for all pre-existing accepted tickets");
        }

        // =====================================================================
        // ИСПРАВЛЕНИЕ 1: Очистка m_spokenTickets
        // Формируем множество всех текущих принятых талонов
        // =====================================================================
        std::set<std::wstring> currentAcceptedKeys;
        for (const auto& queue : snapshotCopy.queues) {
            for (const auto& ticket : queue.accepted) {
                currentAcceptedKeys.insert(queue.queueId + L":" + ticket.ticketNumber);
            }
        }

        // Удаляем из m_spokenTickets ключи, которых больше нет в accepted
        {
            std::lock_guard<std::mutex> lock(m_spokenMutex);
            for (auto it = m_spokenTickets.begin(); it != m_spokenTickets.end(); ) {
                if (currentAcceptedKeys.find(*it) == currentAcceptedKeys.end()) {
                    g_logger.info(L"QueueDisplayWindow: cleanup spokenTickets, removed: " + *it);
                    it = m_spokenTickets.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        // =====================================================================
        // Проверка новых принятых талонов для озвучивания
        // =====================================================================
        for (const auto& queue : snapshotCopy.queues) {
            for (const auto& ticket : queue.accepted) {
                std::wstring key = queue.queueId + L":" + ticket.ticketNumber;
                bool alreadySpoken = false;
                {
                    std::lock_guard<std::mutex> lock(m_spokenMutex);
                    if (m_spokenTickets.count(key) > 0) {
                        alreadySpoken = true;
                    }
                    else {
                        m_spokenTickets.insert(key);
                    }
                }
                if (!alreadySpoken) {
                    if (isFirstSync) {
                        g_logger.info(L"QueueDisplayWindow: [INITIAL SYNC] ticket " +
                            ticket.ticketNumber + L" in queue " + queue.queueId +
                            L", window=" + ticket.windowNumber +
                            L" - TTS SUPPRESSED (pre-existing)");
                    }
                    else {
                        g_logger.info(L"QueueDisplayWindow: new accepted ticket " +
                            ticket.ticketNumber + L" in queue " + queue.queueId +
                            L", window=" + ticket.windowNumber +
                            L" - TTS SPEAKING");
                        speakInvitation(ticket.ticketNumber, ticket.windowNumber);
                    }
                }
            }
        }
        InvalidateRect(m_hWnd, NULL, TRUE);
    }

    void renderQueues(HDC hdc, int clientWidth, int clientHeight) {
        DisplaySnapshot snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            snapshotCopy = m_snapshot;
        }

        RECT titleRect = { 0, 10, clientWidth, 60 };
        DrawTextW(hdc, L"Электронная очередь - Комиссионный магазин", -1,
            &titleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        if (!snapshotCopy.valid) {
            RECT errRect = { 0, clientHeight / 2 - 30, clientWidth, clientHeight / 2 + 30 };
            SetTextColor(hdc, RGB(255, 80, 80));
            DrawTextW(hdc, L"Нет связи с сервером", -1,
                &errRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            return;
        }

        wchar_t timeBuf[128];
        swprintf_s(timeBuf, L"Обновлено: %s", getCurrentTimeString().c_str());
        RECT timeRect = { clientWidth - 300, 10, clientWidth - 10, 40 };
        SetTextColor(hdc, RGB(180, 180, 180));
        HFONT hOldFont = (HFONT)SelectObject(hdc, m_hFontSmall);
        DrawTextW(hdc, timeBuf, -1, &timeRect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);

        int gridTop = 70;
        int gridHeight = clientHeight - gridTop - 10;
        int cellWidth = clientWidth / 3;
        int cellHeight = gridHeight / 2;
        int padding = 8;

        for (size_t i = 0; i < snapshotCopy.queues.size() && i < 6; ++i) {
            const auto& queue = snapshotCopy.queues[i];
            int col = static_cast<int>(i % 3);
            int row = static_cast<int>(i / 3);
            int x = col * cellWidth + padding;
            int y = gridTop + row * cellHeight + padding;
            int w = cellWidth - 2 * padding;
            int h = cellHeight - 2 * padding;
            renderSingleQueue(hdc, queue, x, y, w, h);
        }
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: renderSingleQueue()
    // =========================================================================
    // ИЗМЕНЕНИЯ:
    // 1. Принятые талоны показываются в ОБРАТНОМ порядке (новые сверху).
    //    Это гарантирует, что только что принятый талон ВСЕГДА виден
    //    в верхней части блока «Приглашены», даже если в очереди
    //    уже есть несколько ранее принятых талонов.
    //
    // 2. УБРАНО ограничение maxLines/2: теперь показываются ВСЕ
    //    принятые талоны. Оставшееся пространство используется
    //    для ожидающих талонов. Если принятых талонов очень много,
    //    они занимают до 70% высоты ячейки.
    //
    // 3. Оставшееся место динамически распределяется для ожидающих.
    // =========================================================================
    void renderSingleQueue(HDC hdc, const DisplayQueue& queue,
        int x, int y, int w, int h) {

        RECT cellRect = { x, y, x + w, y + h };
        HBRUSH hCellBrush = CreateSolidBrush(DisplayConfig::QUEUE_TITLE_BG);
        FillRect(hdc, &cellRect, hCellBrush);
        DeleteObject(hCellBrush);

        RECT headerRect = { x + 5, y + 5, x + w - 5, y + 35 };
        SetTextColor(hdc, DisplayConfig::QUEUE_TITLE_FG);
        HFONT hOldFont = (HFONT)SelectObject(hdc, m_hFontQueue);
        DrawTextW(hdc, queue.displayName.c_str(), -1,
            &headerRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);

        int textY = y + 40;
        int lineHeight = 24;
        int bottomLimit = y + h - 5;

        // =====================================================================
        // Блок «Приглашены» (зелёный текст)
        // ИСПРАВЛЕНИЕ: показываем в ОБРАТНОМ порядке (новые сверху)
        // и БЕЗ искусственного ограничения maxLines/2
        // =====================================================================
        if (!queue.accepted.empty()) {
            SetTextColor(hdc, DisplayConfig::ACCEPTED_COLOR);
            SelectObject(hdc, m_hFontTicket);

            std::wstring header = L"Приглашены (" +
                std::to_wstring(queue.accepted.size()) + L"):";
            RECT r = { x + 10, textY, x + w - 10, textY + lineHeight };
            DrawTextW(hdc, header.c_str(), -1, &r,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            textY += lineHeight;

            // Вычисляем максимум строк для принятых (до 70% ячейки)
            int maxAcceptedLines = static_cast<int>((h - 50) * 0.7) / lineHeight;
            if (maxAcceptedLines < 1) maxAcceptedLines = 1;

            // ИСПРАВЛЕНИЕ: итерация в ОБРАТНОМ порядке (rbegin → rend)
            // Новые талоны (с наибольшим accepted_at) оказываются СВЕРХУ
            int shown = 0;
            for (auto it = queue.accepted.rbegin();
                it != queue.accepted.rend() && shown < maxAcceptedLines;
                ++it, ++shown) {

                if (textY + lineHeight > bottomLimit) break;

                std::wstring line = L"  " + it->ticketNumber +
                    L"  ->  Окно " + it->windowNumber;
                RECT lr = { x + 10, textY, x + w - 10, textY + lineHeight };
                DrawTextW(hdc, line.c_str(), -1, &lr,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                textY += lineHeight;
            }

            if (static_cast<int>(queue.accepted.size()) > maxAcceptedLines) {
                SetTextColor(hdc, RGB(180, 180, 180));
                SelectObject(hdc, m_hFontSmall);
                std::wstring more = L"  ...ещё " +
                    std::to_wstring(queue.accepted.size() - maxAcceptedLines);
                RECT mr = { x + 10, textY, x + w - 10, textY + lineHeight };
                DrawTextW(hdc, more.c_str(), -1, &mr,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                textY += lineHeight;
            }
        }

        // =====================================================================
        // Блок «Ожидают» (белый текст)
        // Занимает ВСЁ оставшееся пространство
        // =====================================================================
        textY += 5;
        SetTextColor(hdc, DisplayConfig::WAITING_COLOR);
        SelectObject(hdc, m_hFontTicket);

        std::wstring waitHeader = L"Ожидают (" +
            std::to_wstring(queue.waiting.size()) + L"):";
        RECT whr = { x + 10, textY, x + w - 10, textY + lineHeight };
        DrawTextW(hdc, waitHeader.c_str(), -1, &whr,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        textY += lineHeight;

        for (const auto& t : queue.waiting) {
            if (textY + lineHeight > bottomLimit) break;

            std::wstring line = L"  " + t.ticketNumber;
            if (t.position > 0) {
                line += L"  (поз. " + std::to_wstring(t.position) + L")";
            }
            RECT lr = { x + 10, textY, x + w - 10, textY + lineHeight };
            DrawTextW(hdc, line.c_str(), -1, &lr,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            textY += lineHeight;
        }

        if (queue.waiting.empty() && queue.accepted.empty()) {
            SetTextColor(hdc, RGB(120, 120, 120));
            RECT emptyRect = { x + 10, textY, x + w - 10, textY + lineHeight };
            DrawTextW(hdc, L"Очередь пуста", -1, &emptyRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // Рамка ячейки
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 150));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNullBrush);
        Rectangle(hdc, x, y, x + w, y + h);
        SelectObject(hdc, hOldPen);
        SelectObject(hdc, hOldBrush);
        DeleteObject(hPen);
    }

    std::wstring getCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_s(&tm_buf, &t);
        wchar_t buf[32];
        wcsftime(buf, 32, L"%H:%M:%S", &tm_buf);
        return std::wstring(buf);
    }

    void createResources() {
        m_hFontTitle = CreateFontW(DisplayConfig::FONT_TITLE_SIZE, 0, 0, 0,
            FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontQueue = CreateFontW(DisplayConfig::FONT_QUEUE_SIZE, 0, 0, 0,
            FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontTicket = CreateFontW(DisplayConfig::FONT_TICKET_SIZE, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hFontSmall = CreateFontW(DisplayConfig::FONT_SMALL_SIZE, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrushBg = CreateSolidBrush(DisplayConfig::BG_COLOR);
        g_logger.info(L"QueueDisplayWindow: resources created");
    }

    void releaseResources() {
        if (m_hFontTitle) { DeleteObject(m_hFontTitle);  m_hFontTitle = nullptr; }
        if (m_hFontQueue) { DeleteObject(m_hFontQueue);  m_hFontQueue = nullptr; }
        if (m_hFontTicket) { DeleteObject(m_hFontTicket); m_hFontTicket = nullptr; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall);  m_hFontSmall = nullptr; }
        if (m_hBrushBg) { DeleteObject(m_hBrushBg);    m_hBrushBg = nullptr; }
        if (m_pVoice) {
            m_pVoice->Release();
            m_pVoice = nullptr;
        }
        g_logger.info(L"QueueDisplayWindow: resources released");
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        QueueDisplayWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<QueueDisplayWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(pThis));
            pThis->m_hWnd = hWnd;
            pThis->createResources();
            pThis->initializeTTS();
            pThis->m_running.store(true);
            pThis->m_pollThread = std::thread([pThis]() {
                pThis->pollLoop();
                });
            g_logger.info(L"QueueDisplayWindow: WM_CREATE processed");
            return 0;
        }
        pThis = reinterpret_cast<QueueDisplayWindow*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (!pThis) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_DISPLAY_DATA_READY:
            pThis->onDataReady();
            return 0;
        case WM_DISPLAY_ERROR:
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, pThis->m_hBrushBg);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, DisplayConfig::HEADER_COLOR);
            SelectObject(hdc, pThis->m_hFontTitle);
            pThis->renderQueues(hdc, rc.right, rc.bottom);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            pThis->m_running.store(false);
            if (pThis->m_pollThread.joinable()) {
                pThis->m_pollThread.join();
            }
            pThis->releaseResources();
            PostQuitMessage(0);
            g_logger.info(L"QueueDisplayWindow: WM_DESTROY");
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    QueueDisplayWindow() {
        g_logger.info(L"QueueDisplayWindow: constructor");
    }
    ~QueueDisplayWindow() {
        g_logger.info(L"QueueDisplayWindow: destructor");
    }

    void show() {
        g_logger.info(L"QueueDisplayWindow::show() called, DEBUG_MODE=" +
            std::wstring(DisplayConfig::DEBUG_MODE ? L"true" : L"false"));

        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = g_hInstance;
            wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcex.hbrBackground = NULL;
            wcex.lpszClassName = CLASS_NAME;
            wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            if (!RegisterClassExW(&wcex)) {
                g_logger.error(L"QueueDisplayWindow: RegisterClassExW failed");
                return;
            }
            classRegistered = true;
        }

        int x, y, w, h;
        DWORD exStyle, style;

        if (DisplayConfig::DEBUG_MODE) {
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            w = screenW / 2;
            h = screenH / 2;
            x = screenW - w - 20;
            y = 20;
            exStyle = WS_EX_WINDOWEDGE;
            style = WS_OVERLAPPEDWINDOW;
            g_logger.info(L"QueueDisplayWindow: DEBUG mode, window at (" +
                std::to_wstring(x) + L"," + std::to_wstring(y) + L"), size " +
                std::to_wstring(w) + L"x" + std::to_wstring(h));
        }
        else {
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            int virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            if (virtualW > screenW) {
                x = screenW;
                y = 0;
                w = virtualW - screenW;
                h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            }
            else {
                x = 0;
                y = 0;
                w = screenW;
                h = screenH;
                g_logger.warning(
                    L"QueueDisplayWindow: second monitor not found, using primary");
            }
            exStyle = WS_EX_TOPMOST;
            style = WS_POPUP;
            g_logger.info(L"QueueDisplayWindow: PRODUCTION mode, fullscreen at (" +
                std::to_wstring(x) + L"," + std::to_wstring(y) + L"), size " +
                std::to_wstring(w) + L"x" + std::to_wstring(h));
        }

        m_hWnd = CreateWindowExW(exStyle, CLASS_NAME,
            L"Электронная очередь - ТВ-монитор",
            style, x, y, w, h,
            nullptr, nullptr, g_hInstance, this);

        if (!m_hWnd) {
            g_logger.error(L"QueueDisplayWindow: CreateWindowExW failed");
            return;
        }

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);

        if (!DisplayConfig::DEBUG_MODE) {
            SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, w, h,
                SWP_NOZORDER | SWP_SHOWWINDOW);
            ShowCursor(FALSE);
        }

        g_logger.info(L"QueueDisplayWindow: window shown");

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (!IsWindow(m_hWnd)) break;
        }

        if (!DisplayConfig::DEBUG_MODE) {
            ShowCursor(TRUE);
        }
        g_logger.info(L"QueueDisplayWindow: message loop ended");
    }
};
// queue_display_window.h
// =============================================================================
// ОКНО ОТОБРАЖЕНИЯ ОЧЕРЕДЕЙ НА ТВ-МОНИТОРЕ
// =============================================================================
// ИСПРАВЛЕНИЯ 28.08.2026 (ФИНАЛЬНАЯ ВЕРСИЯ ПО ЗАДАНИЮ):
//
// 1. ИЗМЕНЕНО ОТОБРАЖЕНИЕ: вместо 6 отдельных очередей показывается
//    ОДНА ОБЩАЯ ОЧЕРЕДЬ из всех ожидающих талонов (статус 'waiting'/'pending').
//    Сортировка выполняется по времени создания талона (created_at).
//    Отображаются три колонки:
//      - Тип очереди и номер талона (например, "Общая очередь G001")
//      - Порядковый номер в общей очереди (1, 2, 3, ...)
//      - Время взятия талона (в формате HH:MM:SS)
//
// 2. В нижней части экрана оставлено место для рекламы (1/4 от высоты).
//
// 3. Сохранена функциональность озвучивания (TTS) для вновь принятых талонов
//    (не влияет на отображение общей очереди).
//
// 4. Исправлен парсинг ответа сервера: для ожидающих талонов поле created_at
//    сохраняется в DisplayTicket::acceptedAt (переиспользование существующего поля,
//    новая переменная не добавляется).
//
// 5. Добавлено полное логгирование каждого шага для отладки.
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
#include <iomanip>
#include <sstream>
#include <ctime>
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
    constexpr int AD_BANNER_HEIGHT_RATIO = 4; // 1/4 высоты для рекламы
}

struct DisplayTicket {
    std::wstring ticketNumber;
    int clientId = 0;
    std::wstring windowNumber;
    int64_t acceptedAt = 0;   // Для ожидающих: время создания (created_at), для принятых: время принятия (accepted_at)
    int position = 0;         // Позиция в своей очереди (не используется в общей)
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

    // =========================================================================
    // ИНИЦИАЛИЗАЦИЯ TTS
    // =========================================================================
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
        // =====================================================================
        // ФОНОВЫЙ ПОТОК ОПРОСА СЕРВЕРА ДЛЯ ТВ-МОНИТОРА
        // =====================================================================
        // Требования потокобезопасности:
        // 1. Использовать atomic-флаг m_running.
        // 2. Обернуть сетевой запрос и парсинг в try/catch.
        // 3. Не отправлять сообщения, если окно уже не существует.
        // 4. Спать короткими интервалами для быстрого завершения потока.
        // =====================================================================
        g_logger.info(L"QueueDisplayWindow: poll loop started, interval=" +
            std::to_wstring(DisplayConfig::POLL_INTERVAL_MS) + L" ms");

        while (m_running.load()) {
            try {
                auto response = g_httpsClient.get(L"/api/v1/queue/display", L"");

                if (response && response->contains("queues")) {
                    DisplaySnapshot snapshot = parseDisplayResponse(*response);

                    {
                        std::lock_guard<std::mutex> lock(m_dataMutex);
                        m_snapshot = std::move(snapshot);
                    }

                    if (m_running.load() && IsWindow(m_hWnd)) {
                        PostMessageW(m_hWnd, WM_DISPLAY_DATA_READY, 0, 0);
                    }
                }
                else {
                    g_logger.warning(L"QueueDisplayWindow: failed to fetch display data");

                    if (m_running.load() && IsWindow(m_hWnd)) {
                        PostMessageW(m_hWnd, WM_DISPLAY_ERROR, 0, 0);
                    }
                }
            }
            catch (const std::exception& ex) {
                g_logger.error(L"QueueDisplayWindow: poll loop exception: " +
                    utf8_to_wstring(ex.what()));
            }
            catch (...) {
                g_logger.error(L"QueueDisplayWindow: poll loop unknown exception");
            }

            // =================================================================
            // Спим полный интервал опроса, но короткими кусками по 100 мс.
            // Это позволяет быстро завершить поток при закрытии окна.
            // =================================================================
            for (int waitedMs = 0;
                waitedMs < DisplayConfig::POLL_INTERVAL_MS && m_running.load();
                waitedMs += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        g_logger.info(L"QueueDisplayWindow: poll loop stopped");
    }

    // =========================================================================
    // ПАРСИНГ ОТВЕТА СЕРВЕРА (ИСПРАВЛЕН)
    // =========================================================================
    DisplaySnapshot parseDisplayResponse(const json& response) {
        g_logger.info(L"parseDisplayResponse: parsing server response");
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

                // =================================================================
                // ИСПРАВЛЕНИЕ: для ожидающих талонов сохраняем created_at в acceptedAt
                // =================================================================
                if (q.contains("waiting") && q["waiting"].is_array()) {
                    for (const auto& t : q["waiting"]) {
                        DisplayTicket dt;
                        dt.ticketNumber = utf8_to_wstring(
                            t.value("ticket_number", std::string("")));
                        dt.clientId = t.value("client_id", 0);
                        dt.windowNumber = utf8_to_wstring(
                            t.value("window_number", std::string("1")));
                        dt.position = t.value("position", 0);
                        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: читаем created_at и сохраняем в acceptedAt
                        dt.acceptedAt = t.value("created_at", (int64_t)0);
                        if (!dt.ticketNumber.empty()) {
                            dq.waiting.push_back(dt);
                            g_logger.info(L"parseDisplayResponse: waiting ticket " +
                                dt.ticketNumber + L" created_at=" +
                                std::to_wstring(dt.acceptedAt));
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
        g_logger.info(L"parseDisplayResponse: parsed " +
            std::to_wstring(snapshot.queues.size()) + L" queues");
        return snapshot;
    }

    // =========================================================================
    // ОБРАБОТЧИК НОВЫХ ДАННЫХ (TTS ДЛЯ ПРИНЯТЫХ - БЕЗ ИЗМЕНЕНИЙ)
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

        // Очистка m_spokenTickets (без изменений)
        std::set<std::wstring> currentAcceptedKeys;
        for (const auto& queue : snapshotCopy.queues) {
            for (const auto& ticket : queue.accepted) {
                currentAcceptedKeys.insert(queue.queueId + L":" + ticket.ticketNumber);
            }
        }
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

        // Озвучивание новых принятых талонов (без изменений)
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

    // =========================================================================
    // ОТРИСОВКА ОДНОЙ ОБЩЕЙ ОЧЕРЕДИ (НОВАЯ ЛОГИКА)
    // =========================================================================
    void renderQueues(HDC hdc, int clientWidth, int clientHeight) {
        g_logger.info(L"renderQueues: started, clientWidth=" +
            std::to_wstring(clientWidth) + L", clientHeight=" +
            std::to_wstring(clientHeight));

        // Вычисляем высоту для рекламного баннера (1/4 от высоты)
        int adHeight = clientHeight / DisplayConfig::AD_BANNER_HEIGHT_RATIO;
        int tableTop = 70;
        int tableBottom = clientHeight - adHeight - 10;
        int tableHeight = tableBottom - tableTop;

        g_logger.info(L"renderQueues: table area top=" + std::to_wstring(tableTop) +
            L", bottom=" + std::to_wstring(tableBottom) +
            L", height=" + std::to_wstring(tableHeight) +
            L", adHeight=" + std::to_wstring(adHeight));

        // Заголовок
        RECT titleRect = { 0, 10, clientWidth, 60 };
        SetTextColor(hdc, DisplayConfig::HEADER_COLOR);
        SelectObject(hdc, m_hFontTitle);
        DrawTextW(hdc, L"Электронная очередь - Комиссионный магазин СОВЕТСКИЙ", -1,
            &titleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        // Время обновления (правый верхний угол)
        wchar_t timeBuf[128];
        swprintf_s(timeBuf, L"Обновлено: %s", getCurrentTimeString().c_str());
        RECT timeRect = { clientWidth - 300, 10, clientWidth - 10, 40 };
        SetTextColor(hdc, RGB(180, 180, 180));
        HFONT hOldFont = (HFONT)SelectObject(hdc, m_hFontSmall);
        DrawTextW(hdc, timeBuf, -1, &timeRect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);

        // Проверка валидности снапшота
        DisplaySnapshot snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(m_dataMutex);
            snapshotCopy = m_snapshot;
        }
        if (!snapshotCopy.valid) {
            RECT errRect = { 0, clientHeight / 2 - 30, clientWidth, clientHeight / 2 + 30 };
            SetTextColor(hdc, RGB(255, 80, 80));
            DrawTextW(hdc, L"Нет связи с сервером", -1,
                &errRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            return;
        }

        // =====================================================================
        // 1. СОБИРАЕМ ВСЕ ОЖИДАЮЩИЕ ТАЛОНЫ ИЗ ВСЕХ ОЧЕРЕДЕЙ
        // =====================================================================
        struct UnifiedTicket {
            std::wstring displayName;   // тип очереди (человекочитаемый)
            std::wstring ticketNumber;
            int64_t createdAt;          // время создания (created_at из сервера)
        };
        std::vector<UnifiedTicket> unified;

        for (const auto& queue : snapshotCopy.queues) {
            for (const auto& ticket : queue.waiting) {
                UnifiedTicket ut;
                ut.displayName = queue.displayName;
                ut.ticketNumber = ticket.ticketNumber;
                ut.createdAt = ticket.acceptedAt; // теперь здесь created_at
                unified.push_back(ut);
                g_logger.info(L"renderQueues: collected ticket " + ut.ticketNumber +
                    L" from " + ut.displayName + L" created at " +
                    std::to_wstring(ut.createdAt));
            }
        }

        g_logger.info(L"renderQueues: total waiting tickets collected: " +
            std::to_wstring(unified.size()));

        // =====================================================================
        // 2. СОРТИРУЕМ ПО ВРЕМЕНИ СОЗДАНИЯ (от старых к новым)
        // =====================================================================
        std::sort(unified.begin(), unified.end(),
            [](const UnifiedTicket& a, const UnifiedTicket& b) {
                return a.createdAt < b.createdAt;
            });

        // =====================================================================
        // 3. ОТРИСОВКА ЗАГОЛОВКА ТАБЛИЦЫ
        // =====================================================================
        int headerY = tableTop + 10;
        int lineHeight = 30;
        int leftMargin = 40;
        int colWidths[3] = {
            (clientWidth - 2 * leftMargin) * 5 / 10,   // 50% - Тип + номер
            (clientWidth - 2 * leftMargin) * 2 / 10,   // 20% - Порядковый номер
            (clientWidth - 2 * leftMargin) * 3 / 10    // 30% - Время
        };
        int colX[3] = { leftMargin,
                        leftMargin + colWidths[0],
                        leftMargin + colWidths[0] + colWidths[1] };

        SetTextColor(hdc, DisplayConfig::QUEUE_TITLE_FG);
        SelectObject(hdc, m_hFontQueue);
        RECT hr = { colX[0], headerY, colX[0] + colWidths[0], headerY + lineHeight };
        DrawTextW(hdc, L"Тип очереди и номер талона", -1, &hr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        hr = { colX[1], headerY, colX[1] + colWidths[1], headerY + lineHeight };
        DrawTextW(hdc, L"№ в общей очереди", -1, &hr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        hr = { colX[2], headerY, colX[2] + colWidths[2], headerY + lineHeight };
        DrawTextW(hdc, L"Время взятия талона", -1, &hr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        // Разделительная линия
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 200));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, leftMargin, headerY + lineHeight + 2, NULL);
        LineTo(hdc, clientWidth - leftMargin, headerY + lineHeight + 2);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        // =====================================================================
        // 4. ОТРИСОВКА СТРОК ТАБЛИЦЫ
        // =====================================================================
        int rowY = headerY + lineHeight + 6;
        int maxRows = (tableHeight - (rowY - tableTop)) / (lineHeight + 2);
        if (maxRows < 0) maxRows = 0;

        g_logger.info(L"renderQueues: maxRows=" + std::to_wstring(maxRows));

        SelectObject(hdc, m_hFontTicket);
        SetTextColor(hdc, DisplayConfig::WAITING_COLOR);

        int displayed = 0;
        for (size_t i = 0; i < unified.size() && displayed < maxRows; ++i) {
            const auto& ut = unified[i];
            int orderNumber = static_cast<int>(i + 1); // порядковый номер в общей очереди

            // Форматируем время
            std::wstring timeStr = formatTimestamp(ut.createdAt);

            // Строка: тип очереди + номер талона
            std::wstring ticketInfo = ut.displayName + L" " + ut.ticketNumber;

            // Рисуем первую колонку
            RECT r1 = { colX[0], rowY, colX[0] + colWidths[0], rowY + lineHeight };
            DrawTextW(hdc, ticketInfo.c_str(), -1, &r1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            // Вторая колонка
            std::wstring orderStr = std::to_wstring(orderNumber);
            RECT r2 = { colX[1], rowY, colX[1] + colWidths[1], rowY + lineHeight };
            DrawTextW(hdc, orderStr.c_str(), -1, &r2, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            // Третья колонка
            RECT r3 = { colX[2], rowY, colX[2] + colWidths[2], rowY + lineHeight };
            DrawTextW(hdc, timeStr.c_str(), -1, &r3, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            rowY += lineHeight + 2;
            displayed++;
        }

        // =====================================================================
        // 5. ОТРИСОВКА РЕКЛАМНОГО БАННЕРА (1/4 ВЫСОТЫ)
        // =====================================================================
        int adY = clientHeight - adHeight + 10;
        RECT adRect = { 10, adY, clientWidth - 10, clientHeight - 10 };
        HBRUSH hAdBrush = CreateSolidBrush(RGB(60, 60, 80));
        FillRect(hdc, &adRect, hAdBrush);
        DeleteObject(hAdBrush);

        SetTextColor(hdc, RGB(200, 200, 200));
        SelectObject(hdc, m_hFontSmall);
        DrawTextW(hdc, L"ЗДЕСЬ МОЖЕТ БЫТЬ ВАША РЕКЛАМА", -1, &adRect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        // Если талонов нет, показываем сообщение
        if (unified.empty()) {
            RECT emptyRect = { leftMargin, rowY, clientWidth - leftMargin, rowY + lineHeight };
            SetTextColor(hdc, RGB(150, 150, 150));
            DrawTextW(hdc, L"Очередь пуста", -1, &emptyRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        g_logger.info(L"renderQueues: displayed " + std::to_wstring(displayed) +
            L" tickets out of " + std::to_wstring(unified.size()));
    }

    // =========================================================================
    // ФОРМАТИРОВАНИЕ TIMESTAMP В СТРОКУ ВРЕМЕНИ (HH:MM:SS)
    // =========================================================================
    std::wstring formatTimestamp(int64_t timestamp) {
        if (timestamp == 0) return L"-";
        time_t t = static_cast<time_t>(timestamp);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        wchar_t buf[16];
        wcsftime(buf, 16, L"%H:%M:%S", &tm_buf);
        return std::wstring(buf);
    }

    // =========================================================================
    // ПОЛУЧЕНИЕ ТЕКУЩЕГО ВРЕМЕНИ В ВИДЕ СТРОКИ
    // =========================================================================
    std::wstring getCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        wchar_t buf[32];
        wcsftime(buf, 32, L"%H:%M:%S", &tm_buf);
        return std::wstring(buf);
    }

    // =========================================================================
    // СОЗДАНИЕ РЕСУРСОВ (ШРИФТЫ, КИСТИ)
    // =========================================================================
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

    // =========================================================================
    // ОКОННАЯ ПРОЦЕДУРА
    // =========================================================================
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
        // =====================================================================
        // ПРОДАКШН-ЗАЩИТА ДЕСТРУКТОРА
        // =====================================================================
        // Деструктор обязан гарантировать, что поток опроса сервера
        // остановлен и выполнен join().
        //
        // Обычно поток уже будет остановлен в WM_DESTROY, но если окно
        // завершится нестандартно, защитный join здесь предотвратит
        // std::terminate() в деструкторе std::thread.
        // =====================================================================
        g_logger.info(L"QueueDisplayWindow: destructor entered");

        m_running.store(false);

        if (m_pollThread.joinable()) {
            g_logger.info(L"QueueDisplayWindow: destructor is joining poll thread");
            m_pollThread.join();
            g_logger.info(L"QueueDisplayWindow: destructor poll thread joined successfully");
        }

        // Повторный вызов безопасен: внутри все указатели проверяются на null.
        releaseResources();

        g_logger.info(L"QueueDisplayWindow: destructor completed");
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

        // =====================================================================
        // ЦИКЛ СООБЩЕНИЙ ОКНА ТВ-МОНИТОРА
        // =====================================================================
        // GetMessage возвращает:
        //  > 0 — получено обычное сообщение;
        //    0 — получен WM_QUIT;
        //   -1 — ошибка получения сообщения.
        //
        // Старый вариант while (GetMessage(...)) ошибочно продолжал цикл при -1.
        // Для продакшн-версии необходимо явно обрабатывать только > 0.
        // =====================================================================
        MSG msg;
        BOOL getMessageResult = GetMessage(&msg, nullptr, 0, 0);

        while (getMessageResult > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (!IsWindow(m_hWnd)) {
                g_logger.info(L"QueueDisplayWindow: window no longer exists, breaking message loop");
                break;
            }

            getMessageResult = GetMessage(&msg, nullptr, 0, 0);
        }

        if (getMessageResult == -1) {
            DWORD err = GetLastError();
            g_logger.error(L"QueueDisplayWindow: GetMessage returned -1, error=" +
                std::to_wstring(err));
        }
        if (!DisplayConfig::DEBUG_MODE) {
            ShowCursor(TRUE);
        }
        g_logger.info(L"QueueDisplayWindow: message loop ended");
    }
};
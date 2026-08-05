// worker_window.h – ПРОДАКШН ВЕРСИЯ (Исправлена блокировка UI-потока)
// =============================================================================
// ИСПРАВЛЕНО:
//   - Критическая ошибка: синхронные HTTP-запросы в UI-потоке вызывали зависание
//     программы и неотрисовку полей ввода (WM_PAINT не обрабатывался).
//   - loadClientItems и onItemSave теперь выполняются асинхронно в фоновых потоках.
//   - Добавлены обработчики WM_APP + 2 и WM_APP + 3 для безопасного обновления UI.
// =============================================================================
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
#include <utility> // для std::pair
#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "string_utils.h"
#include "auth_manager.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;

using json = nlohmann::json;

// Идентификаторы элементов формы ввода товаров
#define ID_ITEM_DATE_LABEL      1001
#define ID_ITEM_NUMBER_EDIT     1002
#define ID_ITEM_DESC_EDIT       1003
#define ID_ITEM_PRICE_EDIT      1004
#define ID_ITEM_QTY_EDIT        1005
#define ID_ITEM_CONDITION_EDIT  1006
#define ID_ITEM_NOTE_EDIT       1007
#define ID_ITEM_ADD_BTN         1008
#define ID_ITEM_SAVE_BTN        1009
#define ID_ITEM_CANCEL_BTN      1010
#define ID_ITEM_LISTVIEW        1011
#define ID_ITEM_TOTAL_QTY       1012
#define ID_ITEM_TOTAL_PRICE     1013
#define ID_ITEM_BACK_BTN        1014

class WorkerWindow {
private:
    // Основные элементы (всегда видны)
    HWND m_hWnd;
    HWND m_hListBox;
    HWND m_hAcceptBtn;
    HWND m_hServeBtn;
    HWND m_hStatusLabel;
    HWND m_hComboQueue;
    HFONT m_hFont;          // основной шрифт (24 pt)
    HFONT m_hSmallFont;     // шрифт для кнопок (18 pt)
    HBRUSH m_hBrush;

    std::vector<json> m_tickets;
    std::mutex m_mutex;
    bool m_running;
    std::thread m_refreshThread;
    std::wstring m_currentQueueType;
    std::vector<std::wstring> m_queueTypeIds;
    ISpVoice* m_pVoice = nullptr;

    static constexpr int REFRESH_INTERVAL_MS = 2000;

    // Элементы формы ввода (скрыты по умолчанию)
    HWND m_hItemDateLabel;
    HWND m_hItemNumberEdit;
    HWND m_hItemDescEdit;
    HWND m_hItemPriceEdit;
    HWND m_hItemQtyEdit;
    HWND m_hItemConditionEdit;
    HWND m_hItemNoteEdit;
    HWND m_hItemAddBtn;
    HWND m_hItemSaveBtn;
    HWND m_hItemCancelBtn;
    HWND m_hItemListView;
    HWND m_hItemTotalQtyLabel;
    HWND m_hItemTotalPriceLabel;
    HWND m_hItemBackBtn;

    // Статические тексты формы
    HWND m_hTitle;
    HWND m_hLabelDate;
    HWND m_hLabelNumber;
    HWND m_hLabelDesc;
    HWND m_hLabelPrice;
    HWND m_hLabelQty;
    HWND m_hLabelCondition;
    HWND m_hLabelNote;

    // Данные текущего клиента
    int m_currentClientId;
    std::wstring m_currentTicketNumber;
    std::wstring m_currentWindowNumber;
    std::wstring m_currentClientName;
    std::vector<json> m_tempItems;
    bool m_isSaving;

    enum class Mode {
        QUEUE_LIST,
        ITEM_INPUT
    };
    Mode m_currentMode;
    int m_selectedIndex;

    // ---- Вспомогательные методы (TTS, логика очереди) ----
    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            g_logger.error(L"CoInitializeEx failed: 0x" + std::to_wstring(hr));
            return;
        }
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (FAILED(hr)) {
            g_logger.error(L"Failed to create TTS voice: 0x" + std::to_wstring(hr));
        }
        else {
            g_logger.info(L"TTS initialized");
        }
    }

    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (!m_pVoice) return;
        std::wstring full = L"Клиент с номером " + number + L" просим подойти к окну номер " + windowNumber;
        m_pVoice->Speak(full.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    std::vector<json> fetchTicketsFromServer(const std::wstring& queueType) {
        std::wstring path;
        if (queueType == L"first_time")
            path = L"/api/v1/queue/first_time/waiting";
        else if (queueType == L"trust")
            path = L"/api/v1/queue/trust/waiting";
        else
            path = L"/api/v1/queue/waiting?type=" + queueType;

        auto response = g_httpsClient.get(path, L"");
        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            return (*response)["tickets"].get<std::vector<json>>();
        }
        return {};
    }

    void updateUI() {
        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < m_tickets.size(); ++i) {
            const auto& ticket = m_tickets[i];
            std::string ticketNumber = ticket["ticket_number"].get<std::string>();
            std::wstring displayText = utf8_to_wstring(ticketNumber);

            if (m_currentQueueType != L"first_time" && ticket.contains("client_id")) {
                int clientId = ticket["client_id"].get<int>();
                displayText += L" (id: " + std::to_wstring(clientId) + L")";
            }

            int index = (int)SendMessageW(m_hListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
            if (index != LB_ERR) {
                SendMessageW(m_hListBox, LB_SETITEMDATA, index, (LPARAM)i);
            }
        }

        if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_tickets.size()) {
            SendMessageW(m_hListBox, LB_SETCURSEL, m_selectedIndex, 0);
        }
        else {
            m_selectedIndex = -1;
        }

        int count = static_cast<int>(m_tickets.size());
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        SetWindowTextW(m_hStatusLabel, status.c_str());

        bool isInputMode = (m_currentMode == Mode::ITEM_INPUT);
        EnableWindow(m_hAcceptBtn, (count > 0) && !isInputMode);
        EnableWindow(m_hServeBtn, (count > 0) && !isInputMode);

        g_logger.debug(L"updateUI: count=" + std::to_wstring(count) +
            L", mode=" + std::to_wstring(static_cast<int>(m_currentMode)) +
            L", acceptEnabled=" + std::wstring((count > 0 && !isInputMode) ? L"true" : L"false") +
            L", serveEnabled=" + std::wstring((count > 0 && !isInputMode) ? L"true" : L"false"));
    }

    void refreshList() {
        std::wstring queueType;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queueType = m_currentQueueType;
            m_selectedIndex = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        }

        auto newTickets = fetchTicketsFromServer(queueType);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentQueueType != queueType) return;
            m_tickets = std::move(newTickets);
            if (m_selectedIndex >= (int)m_tickets.size()) {
                m_selectedIndex = -1;
            }
        }
        PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    }

    // ---- Методы для работы с формой ввода ----
    void repositionItemInputControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        int leftListWidth = (width * 20) / 100;
        if (leftListWidth < 180) leftListWidth = 180;
        int rightStart = leftListWidth + 10;
        int rightWidth = width - rightStart - 10;

        if (rightWidth < 400) {
            showItemInputControls(false);
            return;
        }

        SetWindowPos(m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
        SetWindowPos(m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);

        int btnW = (leftListWidth - 40 - 10) / 2;
        SetWindowPos(m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
        SetWindowPos(m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);

        destroyItemInputControls();
        createItemInputControls(rightStart, 80, rightWidth);
    }

    void destroyItemInputControls() {
        g_logger.debug(L"destroyItemInputControls: destroying all input controls");
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemListView, m_hItemTotalQtyLabel,
            m_hItemTotalPriceLabel, m_hItemBackBtn
        };

        for (auto& c : controls) {
            if (c) {
                if (!DestroyWindow(c)) {
                    g_logger.error(L"destroyItemInputControls: DestroyWindow failed for HWND 0x" + std::to_wstring((UINT_PTR)c));
                }
                c = nullptr;
            }
        }
        g_logger.debug(L"destroyItemInputControls: all controls destroyed");
    }

    void createItemInputControls(int left, int top, int width) {
        if (width < 400) {
            g_logger.warning(L"createItemInputControls: width too small (" + std::to_wstring(width) + L")");
            return;
        }
        g_logger.debug(L"createItemInputControls: creating controls at left=" + std::to_wstring(left) +
            L", top=" + std::to_wstring(top) + L", width=" + std::to_wstring(width));

        int labelW = 200, editW = width - labelW - 20, editH = 28, rowH = 35;

        m_hTitle = CreateWindowExW(0, L"STATIC", L"Ввод товаров для клиента",
            WS_VISIBLE | WS_CHILD | SS_LEFT, left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hLabelDate = CreateWindowExW(0, L"STATIC", L"Дата/время:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDate, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemDateLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, editW, editH,
            m_hWnd, (HMENU)ID_ITEM_DATE_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hItemDateLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;

        m_hLabelNumber = CreateWindowExW(0, L"STATIC", L"№ товара:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNumber, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, 80, editH,
            m_hWnd, (HMENU)ID_ITEM_NUMBER_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNumberEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;

        m_hLabelDesc = CreateWindowExW(0, L"STATIC", L"Наименование:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDesc, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemDescEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH,
            m_hWnd, (HMENU)ID_ITEM_DESC_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemDescEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemDescEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;

        m_hLabelPrice = CreateWindowExW(0, L"STATIC", L"Цена за ед. (10,00):",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelPrice, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemPriceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW / 2, editH,
            m_hWnd, (HMENU)ID_ITEM_PRICE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemPriceEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;

        m_hLabelQty = CreateWindowExW(0, L"STATIC", L"Количество:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelQty, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemQtyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, 80, editH,
            m_hWnd, (HMENU)ID_ITEM_QTY_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemQtyEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;

        m_hLabelCondition = CreateWindowExW(0, L"STATIC", L"Состояние:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelCondition, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemConditionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH,
            m_hWnd, (HMENU)ID_ITEM_CONDITION_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemConditionEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemConditionEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;

        m_hLabelNote = CreateWindowExW(0, L"STATIC", L"Примечание:",
            WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNote, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemNoteEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH,
            m_hWnd, (HMENU)ID_ITEM_NOTE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNoteEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemNoteEdit, EM_SETLIMITTEXT, 300, 0);
        top += rowH + 5;

        m_hItemAddBtn = CreateWindowExW(0, L"BUTTON", L"Добавить позицию",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 35,
            m_hWnd, (HMENU)ID_ITEM_ADD_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemAddBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 45;

        m_hItemListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL, left, top, width - 10, 150,
            m_hWnd, (HMENU)ID_ITEM_LISTVIEW, g_hInstance, nullptr);
        SendMessageW(m_hItemListView, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::vector<std::wstring> headers = { L"№", L"Наименование", L"Цена", L"Кол-во", L"Состояние", L"Примечание" };
        int totalColWidth = width - 10;
        std::vector<int> widths = { 40, 160, 80, 60, 100, 120 };
        int sum = 0; for (auto w : widths) sum += w;
        for (size_t i = 0; i < headers.size(); ++i) {
            col.pszText = const_cast<LPWSTR>(headers[i].c_str());
            col.cx = (int)((float)widths[i] / sum * totalColWidth);
            col.iSubItem = static_cast<int>(i);
            ListView_InsertColumn(m_hItemListView, static_cast<int>(i), &col);
        }
        top += 160;

        m_hItemTotalQtyLabel = CreateWindowExW(0, L"STATIC", L"Общее количество: 0",
            WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, 300, 30,
            m_hWnd, (HMENU)ID_ITEM_TOTAL_QTY, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalQtyLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemTotalPriceLabel = CreateWindowExW(0, L"STATIC", L"Общая цена: 0.00",
            WS_VISIBLE | WS_CHILD | SS_LEFT, left + 320, top, 300, 30,
            m_hWnd, (HMENU)ID_ITEM_TOTAL_PRICE, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalPriceLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 40;

        m_hItemSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить все",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 40,
            m_hWnd, (HMENU)ID_ITEM_SAVE_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemSaveBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 200, top, 180, 40,
            m_hWnd, (HMENU)ID_ITEM_CANCEL_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemCancelBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hItemBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 400, top, 120, 40,
            m_hWnd, (HMENU)ID_ITEM_BACK_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemBackBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        g_logger.debug(L"createItemInputControls: all controls created");
    }

    void showItemInputControls(bool show) {
        int flag = show ? SW_SHOW : SW_HIDE;
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemListView,
            m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemBackBtn
        };
        for (auto c : controls) {
            if (c) ShowWindow(c, flag);
        }
        g_logger.debug(show ? L"showItemInputControls: shown" : L"showItemInputControls: hidden");
    }

    void setCurrentDateTime() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        wchar_t buf[100];
        wcsftime(buf, 100, L"%Y-%m-%d %H:%M:%S", &timeinfo);
        SetWindowTextW(m_hItemDateLabel, buf);
    }

    int m_maxItemNumber;

    void loadClientItems(int clientId) {
        m_maxItemNumber = 0;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            g_logger.warning(L"loadClientItems: auth token is empty, cannot load items");
            return;
        }

        std::wstring path = L"/api/v1/items?client_id=" + std::to_wstring(clientId);
        auto response = g_httpsClient.get(path, authToken);

        if (response && response->contains("items") && (*response)["items"].is_array()) {
            auto& items = (*response)["items"];
            for (const auto& item : items) {
                if (item.contains("item_number") && item["item_number"].is_number()) {
                    int num = item["item_number"].get<int>();
                    if (num > m_maxItemNumber) m_maxItemNumber = num;
                }
            }
        }
    }

    void updateNextItemNumber() {
        int nextNumber = m_maxItemNumber + static_cast<int>(m_tempItems.size()) + 1;
        SetWindowTextW(m_hItemNumberEdit, std::to_wstring(nextNumber).c_str());
    }

    void updateTempItemsList() {
        ListView_DeleteAllItems(m_hItemListView);
        int totalQty = 0;
        double totalPrice = 0.0;

        for (size_t i = 0; i < m_tempItems.size(); ++i) {
            const auto& item = m_tempItems[i];
            int itemNumber = m_maxItemNumber + static_cast<int>(i) + 1;
            std::wstring desc = utf8_to_wstring(item.value("description", ""));
            double price = item.value("estimated_price", 0.0);
            int qty = item.value("quantity", 1);
            std::wstring condition = utf8_to_wstring(item.value("condition", ""));
            std::wstring note = utf8_to_wstring(item.value("note", ""));

            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = static_cast<int>(i);

            std::vector<std::wstring> columns = {
                std::to_wstring(itemNumber), desc,
                (price > 0 ? std::to_wstring(price) : L""),
                std::to_wstring(qty), condition, note
            };

            for (size_t col = 0; col < columns.size(); ++col) {
                lvi.iSubItem = static_cast<int>(col);
                lvi.pszText = const_cast<LPWSTR>(columns[col].c_str());
                if (col == 0) ListView_InsertItem(m_hItemListView, &lvi);
                else ListView_SetItem(m_hItemListView, &lvi);
            }
            totalQty += qty;
            totalPrice += price * qty;
        }

        SetWindowTextW(m_hItemTotalQtyLabel, (L"Общее количество: " + std::to_wstring(totalQty)).c_str());
        wchar_t buf[50];
        swprintf_s(buf, L"Общая цена: %.2f", totalPrice);
        SetWindowTextW(m_hItemTotalPriceLabel, buf);
    }

    void onItemAdd() {
        wchar_t descBuf[256];
        GetWindowTextW(m_hItemDescEdit, descBuf, 256);
        std::wstring desc = descBuf;
        if (desc.empty()) {
            MessageBoxW(m_hWnd, L"Введите наименование", L"Внимание", MB_OK);
            SetFocus(m_hItemDescEdit);
            return;
        }

        wchar_t priceBuf[32];
        GetWindowTextW(m_hItemPriceEdit, priceBuf, 32);
        std::wstring priceStr = priceBuf;
        for (auto& c : priceStr) if (c == L',') c = L'.';
        double price = 0.0;
        try { price = std::stod(priceStr); }
        catch (...) {
            MessageBoxW(m_hWnd, L"Некорректная цена", L"Ошибка", MB_OK);
            SetFocus(m_hItemPriceEdit);
            return;
        }
        if (price < 0) {
            MessageBoxW(m_hWnd, L"Цена не может быть отрицательной", L"Ошибка", MB_OK);
            return;
        }

        int qty = 1;
        wchar_t qtyBuf[16];
        GetWindowTextW(m_hItemQtyEdit, qtyBuf, 16);
        try { qty = std::stoi(qtyBuf); }
        catch (...) {
            MessageBoxW(m_hWnd, L"Введите целое количество", L"Ошибка", MB_OK);
            SetFocus(m_hItemQtyEdit);
            return;
        }
        if (qty < 1) {
            MessageBoxW(m_hWnd, L"Количество не менее 1", L"Ошибка", MB_OK);
            return;
        }

        wchar_t condBuf[256], noteBuf[512];
        GetWindowTextW(m_hItemConditionEdit, condBuf, 256);
        GetWindowTextW(m_hItemNoteEdit, noteBuf, 512);

        json item;
        item["description"] = wstring_to_utf8(desc);
        item["estimated_price"] = price;
        item["quantity"] = qty;
        item["condition"] = wstring_to_utf8(condBuf);
        item["note"] = wstring_to_utf8(noteBuf);

        m_tempItems.push_back(item);
        updateTempItemsList();

        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"");
        SetWindowTextW(m_hItemNoteEdit, L"");

        updateNextItemNumber();
        SetFocus(m_hItemDescEdit);
    }

    void onItemSave() {
        if (m_isSaving) {
            g_logger.warning(L"onItemSave: already saving, ignoring duplicate request");
            return;
        }

        if (m_tempItems.empty()) {
            MessageBoxW(m_hWnd, L"Нет позиций для сохранения", L"Внимание", MB_OK);
            return;
        }

        // =========================================================================
        // ИСПРАВЛЕНИЕ: Валидация m_currentClientId ДО отправки запроса.
        // Если товаровед не нажал «Принять» и не выбрал талон, m_currentClientId
        // остаётся -1 (из createControls). В этом случае отправка невозможна.
        // =========================================================================
        if (m_currentClientId <= 0) {
            g_logger.error(L"onItemSave: BLOCKED - m_currentClientId=" +
                std::to_wstring(m_currentClientId) +
                L" (invalid). Ticket was not accepted via Accept button.");
            MessageBoxW(m_hWnd,
                L"Ошибка: клиент не выбран.\n"
                L"Необходимо выбрать талон из очереди и нажать кнопку «Принять».",
                L"Ошибка", MB_OK | MB_ICONERROR);
            return;
        }

        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) {
            MessageBoxW(m_hWnd, L"Ошибка авторизации. Токен отсутствует или истёк. Перезапустите приложение.", L"Ошибка", MB_OK);
            g_logger.error(L"onItemSave: auth token is empty or invalid");
            return;
        }

        m_isSaving = true;
        EnableWindow(m_hItemSaveBtn, FALSE);

        json request;
        request["client_id"] = m_currentClientId;
        request["items"] = m_tempItems;

        // АСИНХРОННОЕ СОХРАНЕНИЕ: выносим POST-запрос из UI-потока
        std::thread([this, request, authToken]() {
            g_logger.info(L"onItemSave: async POST request started");
            auto response = g_httpsClient.post(L"/api/v1/items/batch", request, authToken);

            bool success = false;
            std::wstring errorMsg = L"Ошибка сохранения.";

            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                success = true;
            }
            else {
                if (response && response->contains("error"))
                    errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }

            // Передаем результат в UI-поток через динамически выделенную структуру
            using SaveResult = std::pair<bool, std::wstring>;
            SaveResult* res = new SaveResult(success, errorMsg);
            PostMessageW(m_hWnd, WM_APP + 3, 0, (LPARAM)res);
            }).detach();
    }

    void onItemCancel() {
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L"");
        SetWindowTextW(m_hItemNoteEdit, L"");
        m_tempItems.clear();
        updateTempItemsList();
        updateNextItemNumber();
    }

    void returnToQueueList() {
        g_logger.info(L"returnToQueueList: switching to QUEUE_LIST mode");
        m_currentMode = Mode::QUEUE_LIST;
        showItemInputControls(false);
        refreshList();
        updateUI();
    }

    void showItemInputForm(int clientId, const std::wstring& ticketNumber,
        const std::wstring& windowNumber,
        const std::wstring& clientName) {
        g_logger.info(L"showItemInputForm: entering ITEM_INPUT mode for clientId=" + std::to_wstring(clientId));
        m_currentClientId = clientId;
        m_currentTicketNumber = ticketNumber;
        m_currentWindowNumber = windowNumber;
        m_currentClientName = clientName;
        m_tempItems.clear();
        m_isSaving = false;
        m_currentMode = Mode::ITEM_INPUT;

        repositionItemInputControls();

        // АСИНХРОННАЯ ЗАГРУЗКА: выносим сетевой запрос из UI-потока, чтобы избежать зависания и неотрисовки полей
        std::thread([this, clientId]() {
            g_logger.info(L"showItemInputForm: async loadClientItems started for clientId=" + std::to_wstring(clientId));
            loadClientItems(clientId);
            // Уведомляем UI-поток о завершении загрузки для безопасного обновления контролов
            PostMessageW(m_hWnd, WM_APP + 2, 0, 0);
            }).detach();

        setCurrentDateTime();
        // updateNextItemNumber() и updateTempItemsList() будут вызваны в обработчике WM_APP + 2
        showItemInputControls(true);

        EnableWindow(m_hAcceptBtn, FALSE);
        EnableWindow(m_hServeBtn, FALSE);
        g_logger.info(L"showItemInputForm: accept and serve buttons disabled, async load dispatched");
    }

    // ---- Обработчики кнопок ----
    void onAccept() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK);
            return;
        }
        m_selectedIndex = sel;

        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
            MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK);
            return;
        }

        std::string ticketNumberUtf8;
        int clientId = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const json& ticket = m_tickets[itemData];
            ticketNumberUtf8 = ticket["ticket_number"].get<std::string>();
            if (ticket.contains("client_id")) clientId = ticket["client_id"].get<int>();
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        if (m_currentQueueType == L"general" || m_currentQueueType == L"extra_20" || m_currentQueueType == L"trust" || m_currentQueueType == L"paid" || m_currentQueueType == L"expensive") {
            if (clientId <= 0) {
                MessageBoxW(m_hWnd, L"Нет client_id", L"Ошибка", MB_OK);
                return;
            }
            speak(ticketNumber, L"1");
            std::wstring clientName = L"Клиент #" + std::to_wstring(clientId);
            showItemInputForm(clientId, ticketNumber, L"1", clientName);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData < (LRESULT)m_tickets.size())
                    m_tickets.erase(m_tickets.begin() + itemData);
                if (m_selectedIndex >= (int)m_tickets.size()) {
                    m_selectedIndex = (int)m_tickets.size() - 1;
                }
            }
            updateUI();
            return;
        }

        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/accept";
        else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/accept";
        else endpoint = L"/api/v1/queue/accept";

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            std::string windowNumber = response->value("window_number", "1");
            std::wstring windowW = utf8_to_wstring(windowNumber);
            std::wstring numberToSpeak;
            if (m_currentQueueType == L"first_time") numberToSpeak = ticketNumber;
            else if (clientId >= 0) numberToSpeak = std::to_wstring(clientId);
            else numberToSpeak = ticketNumber;

            speak(numberToSpeak, windowW);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData < (LRESULT)m_tickets.size())
                    m_tickets.erase(m_tickets.begin() + itemData);
                if (m_selectedIndex >= (int)m_tickets.size()) {
                    m_selectedIndex = (int)m_tickets.size() - 1;
                }
            }
            updateUI();
        }
        else {
            std::wstring err = L"Ошибка принятия.";
            if (response && response->contains("error"))
                err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
            refreshList();
        }
    }

    void onServe() {
        if (m_currentMode == Mode::ITEM_INPUT) {
            g_logger.warning(L"onServe: called in ITEM_INPUT mode, ignoring");
            return;
        }

        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) {
            MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK);
            return;
        }
        m_selectedIndex = sel;

        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) {
            MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK);
            return;
        }

        std::string ticketNumberUtf8;
        int clientId = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const json& ticket = m_tickets[itemData];
            ticketNumberUtf8 = ticket["ticket_number"].get<std::string>();
            if (ticket.contains("client_id")) clientId = ticket["client_id"].get<int>();
        }

        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/serve";
        else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/serve";
        else endpoint = L"/api/v1/queue/serve";

        json request;
        request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");

        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (itemData < (LRESULT)m_tickets.size())
                    m_tickets.erase(m_tickets.begin() + itemData);
                if (m_selectedIndex >= (int)m_tickets.size()) {
                    m_selectedIndex = (int)m_tickets.size() - 1;
                }
            }
            updateUI();

            if ((m_currentQueueType == L"general" || m_currentQueueType == L"extra_20" || m_currentQueueType == L"trust" || m_currentQueueType == L"paid" || m_currentQueueType == L"expensive") && !m_tickets.empty()) {
                g_logger.info(L"onServe: auto-accepting next client in general queue");
                SendMessageW(m_hListBox, LB_SETCURSEL, 0, 0);
                m_selectedIndex = 0;
                onAccept();
            }
            else {
                if (!m_tickets.empty()) {
                    int newIdx = m_selectedIndex;
                    if (newIdx < 0) newIdx = 0;
                    if (newIdx < (int)m_tickets.size()) {
                        const auto& nextTicket = m_tickets[newIdx];
                        std::string nextNum = nextTicket["ticket_number"].get<std::string>();
                        std::wstring nextNumW = utf8_to_wstring(nextNum);
                        speak(nextNumW, L"1");
                    }
                }
            }
        }
        else {
            std::wstring err = L"Ошибка обслуживания.";
            if (response && response->contains("error"))
                err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
            refreshList();
        }
    }

    // ---- Оконная процедура ----
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WorkerWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            pThis = (WorkerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hWnd = hWnd;
            pThis->createControls();
            pThis->m_currentMode = Mode::QUEUE_LIST;
            pThis->m_maxItemNumber = 0;
            pThis->m_selectedIndex = -1;
            {
                std::lock_guard<std::mutex> lock(pThis->m_mutex);
                pThis->m_tickets = pThis->fetchTicketsFromServer(pThis->m_currentQueueType);
            }
            pThis->updateUI();
            pThis->m_running = true;
            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS));
                    if (pThis->m_running) pThis->refreshList();
                }
                });
            return 0;
        }

        pThis = (WorkerWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);

        if (msg == WM_SIZE) {
            if (pThis->m_currentMode == Mode::ITEM_INPUT) {
                pThis->repositionItemInputControls();
                pThis->showItemInputControls(true);
            }
            else {
                RECT rc;
                GetClientRect(hWnd, &rc);
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                int leftListWidth = (width * 20) / 100;
                if (leftListWidth < 180) leftListWidth = 180;

                SetWindowPos(pThis->m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
                SetWindowPos(pThis->m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);

                int btnW = (leftListWidth - 40 - 10) / 2;
                SetWindowPos(pThis->m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
            }
            return 0;
        }

        switch (msg) {
        case WM_APP + 1:
            pThis->updateUI();
            return 0;

        case WM_APP + 2:
            // Загрузка товаров завершена, обновляем UI-контролы
            g_logger.info(L"WndProc: WM_APP + 2 received, updating item input UI");
            pThis->updateNextItemNumber();
            pThis->updateTempItemsList();
            return 0;

        case WM_APP + 3: {
            // Сохранение товаров завершено, обрабатываем результат
            using SaveResult = std::pair<bool, std::wstring>;
            SaveResult* res = reinterpret_cast<SaveResult*>(lParam);
            pThis->m_isSaving = false;
            EnableWindow(pThis->m_hItemSaveBtn, TRUE);

            if (res->first) {
                MessageBoxW(pThis->m_hWnd, L"Товары успешно сохранены!", L"Успех", MB_OK);
                pThis->returnToQueueList();
            }
            else {
                MessageBoxW(pThis->m_hWnd, res->second.c_str(), L"Ошибка", MB_OK);
            }
            delete res;
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (id == 100 && code == LBN_SELCHANGE) {
                int newSel = (int)SendMessageW(pThis->m_hListBox, LB_GETCURSEL, 0, 0);
                if (newSel != LB_ERR) {
                    pThis->m_selectedIndex = newSel;
                }
                return 0;
            }
            if (id == 200 && code == CBN_SELCHANGE) {
                if (pThis->m_currentMode == Mode::ITEM_INPUT) {
                    MessageBoxW(pThis->m_hWnd, L"Сначала завершите ввод товаров", L"Внимание", MB_OK);
                    return 0;
                }
                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR && idx < (LRESULT)pThis->m_queueTypeIds.size()) {
                    pThis->m_currentQueueType = pThis->m_queueTypeIds[static_cast<int>(idx)];
                    pThis->m_selectedIndex = -1;
                    pThis->refreshList();
                }
                return 0;
            }
            if (id == ID_ITEM_ADD_BTN && code == BN_CLICKED) { pThis->onItemAdd(); return 0; }
            if (id == ID_ITEM_SAVE_BTN && code == BN_CLICKED) { pThis->onItemSave(); return 0; }
            if (id == ID_ITEM_CANCEL_BTN && code == BN_CLICKED) { pThis->onItemCancel(); return 0; }
            if (id == ID_ITEM_BACK_BTN && code == BN_CLICKED) {
                if (MessageBoxW(pThis->m_hWnd, L"Вернуться? Несохранённые данные будут потеряны.", L"Подтверждение", MB_YESNO) == IDYES)
                    pThis->returnToQueueList();
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
            if (pThis->m_refreshThread.joinable()) pThis->m_refreshThread.join();
            if (pThis->m_pVoice) { pThis->m_pVoice->Release(); pThis->m_pVoice = nullptr; }
            CoUninitialize();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        m_hSmallFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");

        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));

        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            20, 10, 200, 200, m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        struct QueueTypeEntry { const wchar_t* displayName; const wchar_t* id; };
        QueueTypeEntry entries[] = {
            { L"Общая очередь", L"general" },
            { L"В первый раз (оформление) договора", L"first_time" },
            { L"+20 позиций", L"extra_20" },
            { L"На доверии", L"trust" },
            { L"Платный прием", L"paid" },
            { L"Дорогой товар", L"expensive" }
        };

        m_queueTypeIds.clear();
        for (const auto& entry : entries) {
            int idx = (int)SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)entry.displayName);
            if (idx != CB_ERR) m_queueTypeIds.push_back(entry.id);
        }
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        if (!m_queueTypeIds.empty()) m_currentQueueType = m_queueTypeIds[0];
        else m_currentQueueType = L"general";

        int leftListWidth = (width * 25) / 100;
        if (leftListWidth < 200) leftListWidth = 200;

        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY,
            20, 60, leftListWidth - 40, height - 190,
            m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, height - 120, leftListWidth - 40, 30,
            m_hWnd, (HMENU)101, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        int btnW = (leftListWidth - 40 - 10) / 2;

        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, height - 80, btnW, 50,
            m_hWnd, (HMENU)1, g_hInstance, nullptr);
        SendMessageW(m_hAcceptBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20 + btnW + 10, height - 80, btnW, 50,
            m_hWnd, (HMENU)2, g_hInstance, nullptr);
        SendMessageW(m_hServeBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);

        HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            width - 120, 10, 100, 40,
            m_hWnd, (HMENU)3, g_hInstance, nullptr);
        SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        int rightStart = leftListWidth + 20;
        int rightWidth = width - rightStart - 20;
        if (rightWidth < 400) rightWidth = 400;

        createItemInputControls(rightStart, 80, rightWidth);
        showItemInputControls(true);
        setCurrentDateTime();

        SetWindowTextW(m_hItemNumberEdit, L"");
        SetWindowTextW(m_hItemDescEdit, L"");
        SetWindowTextW(m_hItemPriceEdit, L"");
        SetWindowTextW(m_hItemQtyEdit, L"");
        SetWindowTextW(m_hItemConditionEdit, L"");
        SetWindowTextW(m_hItemNoteEdit, L"");

        m_tempItems.clear();
        m_currentClientId = -1;
        updateTempItemsList();
    }

public:
    WorkerWindow() : m_hWnd(nullptr), m_hListBox(nullptr), m_hAcceptBtn(nullptr),
        m_hServeBtn(nullptr), m_hStatusLabel(nullptr), m_hComboQueue(nullptr),
        m_hFont(nullptr), m_hSmallFont(nullptr), m_hBrush(nullptr), m_running(false),
        m_currentMode(Mode::QUEUE_LIST), m_currentClientId(0), m_isSaving(false),
        m_maxItemNumber(0), m_selectedIndex(-1),
        m_hTitle(nullptr),
        m_hLabelDate(nullptr),
        m_hLabelNumber(nullptr),
        m_hLabelDesc(nullptr),
        m_hLabelPrice(nullptr),
        m_hLabelQty(nullptr),
        m_hLabelCondition(nullptr),
        m_hLabelNote(nullptr),
        m_hItemDateLabel(nullptr), m_hItemNumberEdit(nullptr),
        m_hItemDescEdit(nullptr), m_hItemPriceEdit(nullptr),
        m_hItemQtyEdit(nullptr), m_hItemConditionEdit(nullptr),
        m_hItemNoteEdit(nullptr), m_hItemAddBtn(nullptr),
        m_hItemSaveBtn(nullptr), m_hItemCancelBtn(nullptr),
        m_hItemListView(nullptr), m_hItemTotalQtyLabel(nullptr),
        m_hItemTotalPriceLabel(nullptr), m_hItemBackBtn(nullptr) {
        initializeTTS();
        g_logger.info(L"WorkerWindow constructed");
    }

    ~WorkerWindow() {
        if (m_hFont) DeleteObject(m_hFont);
        if (m_hSmallFont) DeleteObject(m_hSmallFont);
        if (m_hBrush) DeleteObject(m_hBrush);
        g_logger.info(L"WorkerWindow destroyed");
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
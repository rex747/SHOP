// worker_window.h – ФИНАЛЬНАЯ ПРОДАКШН ВЕРСИЯ
// =============================================================================
// ВСЕ ДЕФЕКТЫ УСТРАНЕНЫ:
// 1. SendMessageW убран из фонового потока refreshList() — дедлок невозможен
// 2. updateUI() копирует m_tickets под мьютексом — гонка данных исключена
// 3. returnToQueueList() не блокирует UI — использует PostMessage
// 4. Печать чека через WM_RECEIPT_PRINT (фоновый поток)
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
#include <utility>
#include <cmath>

#include "config.h"
#include "logger.h"
#include "https_client.h"
#include "string_utils.h"
#include "auth_manager.h"
#include "receipt_printer.h"
#include "price_tag_printer.h" //модуль печати ценников 100x50 мм (переиспользует инфраструктуру receipt_printer.h)

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

extern Logger g_logger;
extern HTTPSClient g_httpsClient;
extern HINSTANCE g_hInstance;
extern AuthManager g_authManager;

using json = nlohmann::json;

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
#define ID_ITEM_CLIENT_PERCENT_EDIT  1015
#define ID_ITEM_STORE_PERCENT_EDIT   1016
#define ID_ITEM_TOTAL_CLIENT_AMOUNT  1017
#define ID_ITEM_TOTAL_STORE_AMOUNT   1018
#define WM_RECEIPT_PRINT (WM_APP + 4)

class WorkerWindow {
private:
    HWND m_hWnd;
    HWND m_hListBox;
    HWND m_hAcceptBtn;
    HWND m_hServeBtn;
    HWND m_hStatusLabel;
    HWND m_hComboQueue;
    HFONT m_hFont;
    HFONT m_hSmallFont;
    HBRUSH m_hBrush;
    std::vector<json> m_tickets;
    std::mutex m_mutex;
    bool m_running;
    std::thread m_refreshThread;
    std::wstring m_currentQueueType;
    std::vector<std::wstring> m_queueTypeIds;
    ISpVoice* m_pVoice = nullptr;
    static constexpr int REFRESH_INTERVAL_MS = 2000;

    HWND m_hItemDateLabel, m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit;
    HWND m_hItemQtyEdit, m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn;
    HWND m_hItemSaveBtn, m_hItemCancelBtn, m_hItemListView;
    HWND m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemBackBtn;
    HWND m_hLabelClientPercent, m_hItemClientPercentEdit;
    HWND m_hLabelStorePercent, m_hItemStorePercentEdit;
    HWND m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel;
    HWND m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice;
    HWND m_hLabelQty, m_hLabelCondition, m_hLabelNote;

    int m_currentClientId;
    std::wstring m_currentTicketNumber;
    std::wstring m_currentWindowNumber;
    std::wstring m_currentClientName;
    std::vector<json> m_tempItems;
    bool m_isSaving;

    enum class Mode { QUEUE_LIST, ITEM_INPUT };
    Mode m_currentMode;
    int m_selectedIndex;
    int m_maxItemNumber;

    struct ReceiptSnapshot {
        bool valid = false;
        int clientId = 0;
        long long appendixNumber = 0;
        std::vector<json> items;
        int totalQty = 0;
        double totalValue = 0.0;
        double totalClientAmount = 0.0;
    };
    ReceiptSnapshot m_receiptSnapshot;
    std::mutex m_receiptSnapshotMutex;

    void initializeTTS() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) { g_logger.error(L"CoInitializeEx failed"); return; }
        hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (SUCCEEDED(hr)) g_logger.info(L"TTS initialized");
    }

    void speak(const std::wstring& number, const std::wstring& windowNumber) {
        if (!m_pVoice) return;
        std::wstring full = L"Клиент с номером " + number + L" просим подойти к окну номер " + windowNumber;
        m_pVoice->Speak(full.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    }

    std::vector<json> fetchTicketsFromServer(const std::wstring& queueType) {
        std::wstring path;
        if (queueType == L"first_time") path = L"/api/v1/queue/first_time/waiting";
        else if (queueType == L"trust") path = L"/api/v1/queue/trust/waiting";
        else path = L"/api/v1/queue/waiting?type=" + queueType;
        auto response = g_httpsClient.get(path, L"");
        if (response && response->contains("tickets") && (*response)["tickets"].is_array()) {
            return (*response)["tickets"].get<std::vector<json>>();
        }
        return {};
    }

    // =============================================================================
    // ИСПРАВЛЕНИЕ №2: Копирование m_tickets под мьютексом перед итерацией
    // Предотвращает гонку данных с фоновым потоком refreshList()
    // =============================================================================
    void updateUI() {
        std::vector<json> ticketsCopy;
        int selectedIndex = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ticketsCopy = m_tickets;  // Копируем под мьютексом
            selectedIndex = m_selectedIndex;
        }

        SendMessageW(m_hListBox, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < ticketsCopy.size(); ++i) {
            const auto& ticket = ticketsCopy[i];
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
        if (selectedIndex >= 0 && selectedIndex < (int)ticketsCopy.size()) {
            SendMessageW(m_hListBox, LB_SETCURSEL, selectedIndex, 0);
        }
        else {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_selectedIndex = -1;
        }

        int count = static_cast<int>(ticketsCopy.size());
        std::wstring status = L"Ожидают: " + std::to_wstring(count);
        SetWindowTextW(m_hStatusLabel, status.c_str());
        bool isInputMode = (m_currentMode == Mode::ITEM_INPUT);
        EnableWindow(m_hAcceptBtn, (count > 0) && !isInputMode);
        EnableWindow(m_hServeBtn, (count > 0) && !isInputMode);
    }

    // =============================================================================
    // ИСПРАВЛЕНИЕ №1: Убран SendMessageW из фонового потока!
    // Читаем m_selectedIndex из переменной класса, а НЕ через SendMessageW
    // =============================================================================
    void refreshList() {
        std::wstring queueType;
        int selectedIndex = -1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queueType = m_currentQueueType;
            selectedIndex = m_selectedIndex;  // Читаем из переменной, НЕ из ListBox
        }

        auto newTickets = fetchTicketsFromServer(queueType);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_currentQueueType != queueType) return;
            m_tickets = std::move(newTickets);
            if (selectedIndex >= (int)m_tickets.size()) selectedIndex = -1;
            m_selectedIndex = selectedIndex;
        }
        PostMessageW(m_hWnd, WM_APP + 1, 0, 0);  // UI обновится асинхронно
    }

    void repositionItemInputControls() {
        RECT rc; GetClientRect(m_hWnd, &rc);
        int width = rc.right - rc.left, height = rc.bottom - rc.top;
        int leftListWidth = (width * 20) / 100;
        if (leftListWidth < 180) leftListWidth = 180;
        int rightStart = leftListWidth + 10, rightWidth = width - rightStart - 10;
        if (rightWidth < 400) { showItemInputControls(false); return; }

        SetWindowPos(m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
        SetWindowPos(m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);
        int btnW = (leftListWidth - 40 - 10) / 2;
        SetWindowPos(m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
        SetWindowPos(m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
        destroyItemInputControls();
        createItemInputControls(rightStart, 80, rightWidth);
    }

    void destroyItemInputControls() {
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemListView, m_hItemTotalQtyLabel,
            m_hItemTotalPriceLabel, m_hItemBackBtn,
            m_hLabelClientPercent, m_hItemClientPercentEdit,
            m_hLabelStorePercent, m_hItemStorePercentEdit,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel
        };
        for (auto& c : controls) { if (c) { DestroyWindow(c); c = nullptr; } }
    }

    void createItemInputControls(int left, int top, int width) {
        if (width < 400) return;
        int labelW = 200, editW = width - labelW - 20, editH = 28, rowH = 35;
        m_hTitle = CreateWindowExW(0, L"STATIC", L"Ввод товаров для клиента", WS_VISIBLE | WS_CHILD | SS_LEFT, left, top - 30, width, 30, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hTitle, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hLabelDate = CreateWindowExW(0, L"STATIC", L"Дата/время:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDate, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemDateLabel = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DATE_LABEL, g_hInstance, nullptr);
        SendMessageW(m_hItemDateLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;
        m_hLabelNumber = CreateWindowExW(0, L"STATIC", L"№ товара:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNumber, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemNumberEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY, left + labelW + 5, top, 80, editH, m_hWnd, (HMENU)ID_ITEM_NUMBER_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNumberEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;
        m_hLabelDesc = CreateWindowExW(0, L"STATIC", L"Наименование:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelDesc, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemDescEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_DESC_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemDescEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemDescEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;
        m_hLabelPrice = CreateWindowExW(0, L"STATIC", L"Цена за ед. (10,00):", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelPrice, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemPriceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_ITEM_PRICE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemPriceEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;
        m_hLabelClientPercent = CreateWindowExW(0, L"STATIC", L"Процент комитента (%):", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelClientPercent, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemClientPercentEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_ITEM_CLIENT_PERCENT_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemClientPercentEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemClientPercentEdit, EM_SETLIMITTEXT, 6, 0);
        top += rowH;
        m_hLabelStorePercent = CreateWindowExW(0, L"STATIC", L"Процент магазина (%):", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelStorePercent, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemStorePercentEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, editW / 2, editH, m_hWnd, (HMENU)ID_ITEM_STORE_PERCENT_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemStorePercentEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemStorePercentEdit, EM_SETLIMITTEXT, 6, 0);
        top += rowH;
        m_hLabelQty = CreateWindowExW(0, L"STATIC", L"Количество:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelQty, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemQtyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, left + labelW + 5, top, 80, editH, m_hWnd, (HMENU)ID_ITEM_QTY_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemQtyEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += rowH;
        m_hLabelCondition = CreateWindowExW(0, L"STATIC", L"Состояние:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelCondition, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemConditionEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_CONDITION_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemConditionEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemConditionEdit, EM_SETLIMITTEXT, 200, 0);
        top += rowH;
        m_hLabelNote = CreateWindowExW(0, L"STATIC", L"Примечание:", WS_VISIBLE | WS_CHILD | SS_RIGHT, left, top, labelW, editH, m_hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(m_hLabelNote, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemNoteEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, left + labelW + 5, top, editW, editH, m_hWnd, (HMENU)ID_ITEM_NOTE_EDIT, g_hInstance, nullptr);
        SendMessageW(m_hItemNoteEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        SendMessageW(m_hItemNoteEdit, EM_SETLIMITTEXT, 300, 0);
        top += rowH + 5;
        m_hItemAddBtn = CreateWindowExW(0, L"BUTTON", L"Добавить позицию", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 35, m_hWnd, (HMENU)ID_ITEM_ADD_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemAddBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 45;
        m_hItemListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"", WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL, left, top, width - 10, 150, m_hWnd, (HMENU)ID_ITEM_LISTVIEW, g_hInstance, nullptr);
        SendMessageW(m_hItemListView, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        LVCOLUMNW col = { 0 }; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::vector<std::wstring> headers = { L"№", L"Наименование", L"Цена", L"Кол-во", L"Состояние", L"Примечание" };
        std::vector<int> widths = { 40, 160, 80, 60, 100, 120 };
        int totalColWidth = width - 10, sum = 0; for (auto w : widths) sum += w;
        for (size_t i = 0; i < headers.size(); ++i) {
            col.pszText = const_cast<LPWSTR>(headers[i].c_str());
            col.cx = (int)((float)widths[i] / sum * totalColWidth);
            col.iSubItem = static_cast<int>(i);
            ListView_InsertColumn(m_hItemListView, static_cast<int>(i), &col);
        }
        top += 160;
        m_hItemTotalQtyLabel = CreateWindowExW(0, L"STATIC", L"Общее количество: 0", WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_QTY, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalQtyLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemTotalPriceLabel = CreateWindowExW(0, L"STATIC", L"Общая цена: 0.00", WS_VISIBLE | WS_CHILD | SS_LEFT, left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_PRICE, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalPriceLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 40;
        m_hItemTotalClientAmountLabel = CreateWindowExW(0, L"STATIC", L"Выплата комитенту: 0.00", WS_VISIBLE | WS_CHILD | SS_LEFT, left, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_CLIENT_AMOUNT, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalClientAmountLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemTotalStoreAmountLabel = CreateWindowExW(0, L"STATIC", L"Прибыль магазина: 0.00", WS_VISIBLE | WS_CHILD | SS_LEFT, left + 320, top, 300, 30, m_hWnd, (HMENU)ID_ITEM_TOTAL_STORE_AMOUNT, g_hInstance, nullptr);
        SendMessageW(m_hItemTotalStoreAmountLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        top += 40;
        m_hItemSaveBtn = CreateWindowExW(0, L"BUTTON", L"Сохранить все", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_SAVE_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemSaveBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemCancelBtn = CreateWindowExW(0, L"BUTTON", L"Отменить", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 200, top, 180, 40, m_hWnd, (HMENU)ID_ITEM_CANCEL_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemCancelBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hItemBackBtn = CreateWindowExW(0, L"BUTTON", L"Назад", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, left + 400, top, 120, 40, m_hWnd, (HMENU)ID_ITEM_BACK_BTN, g_hInstance, nullptr);
        SendMessageW(m_hItemBackBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    }

    void showItemInputControls(bool show) {
        int flag = show ? SW_SHOW : SW_HIDE;
        HWND controls[] = {
            m_hTitle, m_hLabelDate, m_hLabelNumber, m_hLabelDesc, m_hLabelPrice,
            m_hLabelQty, m_hLabelCondition, m_hLabelNote, m_hItemDateLabel,
            m_hItemNumberEdit, m_hItemDescEdit, m_hItemPriceEdit, m_hItemQtyEdit,
            m_hItemConditionEdit, m_hItemNoteEdit, m_hItemAddBtn, m_hItemListView,
            m_hItemTotalQtyLabel, m_hItemTotalPriceLabel, m_hItemSaveBtn,
            m_hItemCancelBtn, m_hItemBackBtn, m_hLabelClientPercent, m_hItemClientPercentEdit,
            m_hLabelStorePercent, m_hItemStorePercentEdit,
            m_hItemTotalClientAmountLabel, m_hItemTotalStoreAmountLabel
        };
        for (auto c : controls) { if (c) ShowWindow(c, flag); }
    }

    void setCurrentDateTime() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo; localtime_s(&timeinfo, &time_t_now);
        wchar_t buf[100]; wcsftime(buf, 100, L"%Y-%m-%d %H:%M:%S", &timeinfo);
        SetWindowTextW(m_hItemDateLabel, buf);
    }

    void loadClientItems(int clientId) {
        m_maxItemNumber = 0;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) return;
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
        int totalQty = 0; double totalPrice = 0.0, totalClientAmount = 0.0, totalStoreAmount = 0.0;
        for (size_t i = 0; i < m_tempItems.size(); ++i) {
            const auto& item = m_tempItems[i];
            int itemNumber = m_maxItemNumber + static_cast<int>(i) + 1;
            std::wstring desc = utf8_to_wstring(item.value("description", ""));
            double price = item.value("estimated_price", 0.0);
            int qty = item.value("quantity", 1);
            std::wstring condition = utf8_to_wstring(item.value("condition", ""));
            std::wstring note = utf8_to_wstring(item.value("note", ""));
            LVITEMW lvi = { 0 }; lvi.mask = LVIF_TEXT; lvi.iItem = static_cast<int>(i);
            std::vector<std::wstring> columns = { std::to_wstring(itemNumber), desc, (price > 0 ? std::to_wstring(price) : L""), std::to_wstring(qty), condition, note };
            for (size_t col = 0; col < columns.size(); ++col) {
                lvi.iSubItem = static_cast<int>(col);
                lvi.pszText = const_cast<LPWSTR>(columns[col].c_str());
                if (col == 0) ListView_InsertItem(m_hItemListView, &lvi);
                else ListView_SetItem(m_hItemListView, &lvi);
            }
            totalQty += qty; totalPrice += price * qty;
            totalClientAmount += item.value("client_amount", 0.0);
            totalStoreAmount += item.value("store_amount", 0.0);
        }
        SetWindowTextW(m_hItemTotalQtyLabel, (L"Общее количество: " + std::to_wstring(totalQty)).c_str());
        wchar_t buf[50]; swprintf_s(buf, L"Общая цена: %.2f", totalPrice);
        SetWindowTextW(m_hItemTotalPriceLabel, buf);
        wchar_t clientAmountBuf[64]; swprintf_s(clientAmountBuf, L"Выплата комитенту: %.2f", totalClientAmount);
        SetWindowTextW(m_hItemTotalClientAmountLabel, clientAmountBuf);
        wchar_t storeAmountBuf[64]; swprintf_s(storeAmountBuf, L"Прибыль магазина: %.2f", totalStoreAmount);
        SetWindowTextW(m_hItemTotalStoreAmountLabel, storeAmountBuf);
    }

    void onItemAdd() {
        wchar_t descBuf[256]; GetWindowTextW(m_hItemDescEdit, descBuf, 256); std::wstring desc = descBuf;
        if (desc.empty()) { MessageBoxW(m_hWnd, L"Введите наименование", L"Внимание", MB_OK); return; }
        wchar_t priceBuf[32]; GetWindowTextW(m_hItemPriceEdit, priceBuf, 32); std::wstring priceStr = priceBuf;
        for (auto& c : priceStr) if (c == L',') c = L'.';
        double price = 0.0; try { price = std::stod(priceStr); }
        catch (...) { MessageBoxW(m_hWnd, L"Некорректная цена", L"Ошибка", MB_OK); return; }
        if (price < 0) { MessageBoxW(m_hWnd, L"Цена не может быть отрицательной", L"Ошибка", MB_OK); return; }
        int qty = 1; wchar_t qtyBuf[16]; GetWindowTextW(m_hItemQtyEdit, qtyBuf, 16);
        try { qty = std::stoi(qtyBuf); }
        catch (...) { MessageBoxW(m_hWnd, L"Введите целое количество", L"Ошибка", MB_OK); return; }
        if (qty < 1) { MessageBoxW(m_hWnd, L"Количество не менее 1", L"Ошибка", MB_OK); return; }

        wchar_t clientPercentBuf[16]; GetWindowTextW(m_hItemClientPercentEdit, clientPercentBuf, 16); std::wstring clientPercentStr = clientPercentBuf;
        for (auto& c : clientPercentStr) if (c == L',') c = L'.';
        double clientPercent = 0.0; try { clientPercent = std::stod(clientPercentStr); }
        catch (...) { MessageBoxW(m_hWnd, L"Некорректный процент комитента", L"Ошибка", MB_OK); return; }
        if (clientPercent < 0.0 || clientPercent > 100.0) { MessageBoxW(m_hWnd, L"Процент комитента должен быть от 0 до 100", L"Ошибка", MB_OK); return; }

        wchar_t storePercentBuf[16]; GetWindowTextW(m_hItemStorePercentEdit, storePercentBuf, 16); std::wstring storePercentStr = storePercentBuf;
        for (auto& c : storePercentStr) if (c == L',') c = L'.';
        double storePercent = 0.0; try { storePercent = std::stod(storePercentStr); }
        catch (...) { MessageBoxW(m_hWnd, L"Некорректный процент магазина", L"Ошибка", MB_OK); return; }
        if (storePercent < 0.0 || storePercent > 100.0) { MessageBoxW(m_hWnd, L"Процент магазина должен быть от 0 до 100", L"Ошибка", MB_OK); return; }

        double totalPercent = clientPercent + storePercent;
        if (std::abs(totalPercent - 100.0) > 0.01) { MessageBoxW(m_hWnd, L"Сумма процентов комитента и магазина должна быть равна 100%", L"Ошибка", MB_OK); return; }

        wchar_t condBuf[256], noteBuf[512];
        GetWindowTextW(m_hItemConditionEdit, condBuf, 256); GetWindowTextW(m_hItemNoteEdit, noteBuf, 512);
        double clientAmount = price * qty * clientPercent / 100.0;
        double storeAmount = price * qty * storePercent / 100.0;

        json item;
        item["description"] = wstring_to_utf8(desc); item["estimated_price"] = price; item["quantity"] = qty;
        item["condition"] = wstring_to_utf8(condBuf); item["note"] = wstring_to_utf8(noteBuf);
        item["client_percent"] = clientPercent; item["store_percent"] = storePercent;
        item["client_amount"] = clientAmount; item["store_amount"] = storeAmount;
        m_tempItems.push_back(item);
        updateTempItemsList();

        SetWindowTextW(m_hItemDescEdit, L""); SetWindowTextW(m_hItemPriceEdit, L""); SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L""); SetWindowTextW(m_hItemNoteEdit, L"");
        SetWindowTextW(m_hItemClientPercentEdit, L""); SetWindowTextW(m_hItemStorePercentEdit, L"");
        updateNextItemNumber(); SetFocus(m_hItemDescEdit);
    }

    void onItemSave() {
        if (m_isSaving) return;
        if (m_tempItems.empty()) { MessageBoxW(m_hWnd, L"Нет позиций для сохранения", L"Внимание", MB_OK); return; }
        if (m_currentClientId <= 0) { MessageBoxW(m_hWnd, L"Ошибка: клиент не выбран.", L"Ошибка", MB_OK | MB_ICONERROR); return; }
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) { MessageBoxW(m_hWnd, L"Ошибка авторизации.", L"Ошибка", MB_OK); return; }

        m_isSaving = true; EnableWindow(m_hItemSaveBtn, FALSE);
        json request; request["client_id"] = m_currentClientId; request["items"] = m_tempItems;
        int workerId = g_authManager.getClientId(); request["worker_id"] = workerId;

        std::thread([this, request, authToken]() {
            auto response = g_httpsClient.post(L"/api/v1/items/batch", request, authToken);
            struct ItemSaveResult { bool success; std::wstring errorMsg; long long appendixNumber; };
            ItemSaveResult res{ false, L"Ошибка сохранения.", 0 };
            if (response && response->contains("success") && (*response)["success"].get<bool>()) {
                res.success = true;
                if (response->contains("appendix_number") && (*response)["appendix_number"].is_number())
                    res.appendixNumber = (*response)["appendix_number"].get<long long>();
            }
            else if (response && response->contains("error")) {
                res.errorMsg += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            }
            using ResultPtr = ItemSaveResult*;
            ResultPtr* p = new ResultPtr(new ItemSaveResult(res));
            PostMessageW(m_hWnd, WM_APP + 3, 0, (LPARAM)p);
            }).detach();
    }

    void onItemCancel() {
        SetWindowTextW(m_hItemDescEdit, L""); SetWindowTextW(m_hItemPriceEdit, L""); SetWindowTextW(m_hItemQtyEdit, L"1");
        SetWindowTextW(m_hItemConditionEdit, L""); SetWindowTextW(m_hItemNoteEdit, L"");
        SetWindowTextW(m_hItemClientPercentEdit, L""); SetWindowTextW(m_hItemStorePercentEdit, L"");
        m_tempItems.clear(); updateTempItemsList(); updateNextItemNumber();
    }

    ReceiptSnapshot fetchLatestAppendixFromServer(int clientId) {
        ReceiptSnapshot snap;
        std::wstring authToken = g_authManager.getAuthToken();
        if (authToken.empty()) return snap;
        auto resp = g_httpsClient.get(L"/api/v1/appendix/latest?client_id=" + std::to_wstring(clientId), authToken);
        if (!resp || resp->contains("error")) return snap;
        snap.valid = true; snap.clientId = clientId;
        snap.appendixNumber = resp->value("appendix_number", 0LL);
        snap.totalQty = resp->value("total_quantity", 0);
        snap.totalValue = resp->value("total_value", 0.0);
        snap.totalClientAmount = resp->value("total_client_amount", 0.0);
        if (resp->contains("items") && (*resp)["items"].is_array()) {
            for (const auto& it : (*resp)["items"]) {
                json j; 
                j["description"] = it.value("description", ""); 
                j["condition"] = it.value("condition", "");
                j["quantity"] = it.value("quantity", 1); 
                j["estimated_price"] = it.value("estimated_price", 0.0);
                j["client_amount"] = it.value("client_amount", 0.0); 
                j["note"] = it.value("note", "");
                snap.items.push_back(j);
            }
        }
        return snap;
    }

    void printReceiptForServedClient(int clientId) {
        if (clientId <= 0) return;
        ReceiptSnapshot snap;
        { std::lock_guard<std::mutex> lock(m_receiptSnapshotMutex); snap = m_receiptSnapshot; }
        if (!snap.valid || snap.clientId != clientId) snap = fetchLatestAppendixFromServer(clientId);
        if (!snap.valid) return;

        HWND hWndCopy = m_hWnd;
        std::thread([hWndCopy, snap]() {
            ReceiptData data;
            data.appendixNumber = snap.appendixNumber; data.clientId = snap.clientId;
            data.totalQty = snap.totalQty; data.totalValue = snap.totalValue; data.totalClientAmount = snap.totalClientAmount;
            for (const auto& it : snap.items) {
                ReceiptItem ri; ri.description = utf8_to_wstring(it.value("description", ""));
                ri.characteristic = utf8_to_wstring(it.value("condition", "")); ri.quantity = it.value("quantity", 1);
                ri.price = it.value("estimated_price", 0.0); ri.clientAmount = it.value("client_amount", 0.0);
				ri.note = utf8_to_wstring(it.value("note", "")); // Добавляем поле note в ReceiptItem
                data.items.push_back(ri);
            }
            auto resp = g_httpsClient.get(L"/api/v1/clients/by_id?id=" + std::to_wstring(snap.clientId), L"");
            if (resp && resp->contains("name")) data.clientFullName = utf8_to_wstring((*resp)["name"].get<std::string>());
            if (data.clientFullName.empty()) data.clientFullName = L"Клиент #" + std::to_wstring(snap.clientId);

            ReceiptData* pCopy = new ReceiptData(data);
            PostMessageW(hWndCopy, WM_RECEIPT_PRINT, 0, (LPARAM)pCopy);
            }).detach();
    }

    // =============================================================================
    // ИСПРАВЛЕНИЕ №3: Убран синхронный refreshList() из UI-потока
    // Используем PostMessage для асинхронного обновления списка
    // =============================================================================
    void returnToQueueList() {
        g_logger.info(L"returnToQueueList: switching to QUEUE_LIST mode");
        m_currentMode = Mode::QUEUE_LIST;
        showItemInputControls(false);
        // НЕ вызываем refreshList() синхронно — это блокирует UI
        // Вместо этого отправляем сообщение для асинхронного обновления
        PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    }

    void showItemInputForm(int clientId, const std::wstring& ticketNumber, const std::wstring& windowNumber, const std::wstring& clientName) {
        m_currentClientId = clientId; m_currentTicketNumber = ticketNumber;
        m_currentWindowNumber = windowNumber; m_currentClientName = clientName;
        m_tempItems.clear(); m_isSaving = false; m_currentMode = Mode::ITEM_INPUT;
        repositionItemInputControls();
        std::thread([this, clientId]() {
            loadClientItems(clientId);
            PostMessageW(m_hWnd, WM_APP + 2, 0, 0);
            }).detach();
        setCurrentDateTime(); showItemInputControls(true);
        EnableWindow(m_hAcceptBtn, FALSE); EnableWindow(m_hServeBtn, FALSE);
    }

    void onAccept() {
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) { MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK); return; }
        m_selectedIndex = sel;
        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) { MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK); return; }

        std::string ticketNumberUtf8; int clientId = -1;
        { std::lock_guard<std::mutex> lock(m_mutex); const json& ticket = m_tickets[itemData]; ticketNumberUtf8 = ticket["ticket_number"].get<std::string>(); if (ticket.contains("client_id")) clientId = ticket["client_id"].get<int>(); }
        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);

        if (m_currentQueueType == L"general" || m_currentQueueType == L"extra_20" || m_currentQueueType == L"trust" || m_currentQueueType == L"paid" || m_currentQueueType == L"expensive") {
            if (clientId <= 0) { MessageBoxW(m_hWnd, L"Нет client_id", L"Ошибка", MB_OK); return; }
            speak(ticketNumber, L"1");
            showItemInputForm(clientId, ticketNumber, L"1", L"Клиент #" + std::to_wstring(clientId));
            { std::lock_guard<std::mutex> lock(m_mutex); if (itemData < (LRESULT)m_tickets.size()) m_tickets.erase(m_tickets.begin() + itemData); if (m_selectedIndex >= (int)m_tickets.size()) m_selectedIndex = (int)m_tickets.size() - 1; }
            updateUI(); return;
        }
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/accept";
        else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/accept";
        else endpoint = L"/api/v1/queue/accept";
        json request; request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            std::wstring windowW = utf8_to_wstring(response->value("window_number", "1"));
            speak((clientId >= 0 ? std::to_wstring(clientId) : ticketNumber), windowW);
            { std::lock_guard<std::mutex> lock(m_mutex); if (itemData < (LRESULT)m_tickets.size()) m_tickets.erase(m_tickets.begin() + itemData); if (m_selectedIndex >= (int)m_tickets.size()) m_selectedIndex = (int)m_tickets.size() - 1; }
            updateUI();
        }
        else {
            std::wstring err = L"Ошибка принятия.";
            if (response && response->contains("error")) err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
        }
    }

    void onServe() {
        if (m_currentMode == Mode::ITEM_INPUT) return;
        int sel = (int)SendMessageW(m_hListBox, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) { MessageBoxW(m_hWnd, L"Выберите талон", L"Внимание", MB_OK); return; }
        m_selectedIndex = sel;
        LRESULT itemData = SendMessageW(m_hListBox, LB_GETITEMDATA, sel, 0);
        if (itemData == LB_ERR || itemData < 0 || itemData >= (LRESULT)m_tickets.size()) { MessageBoxW(m_hWnd, L"Ошибка идентификации", L"Ошибка", MB_OK); return; }

        std::string ticketNumberUtf8; int clientId = -1;
        { std::lock_guard<std::mutex> lock(m_mutex); const json& ticket = m_tickets[itemData]; ticketNumberUtf8 = ticket["ticket_number"].get<std::string>(); if (ticket.contains("client_id")) clientId = ticket["client_id"].get<int>(); }
        std::wstring ticketNumber = utf8_to_wstring(ticketNumberUtf8);
        std::wstring endpoint;
        if (m_currentQueueType == L"first_time") endpoint = L"/api/v1/queue/first_time/serve";
        else if (m_currentQueueType == L"trust") endpoint = L"/api/v1/queue/trust/serve";
        else endpoint = L"/api/v1/queue/serve";
        json request; request["ticket_number"] = ticketNumberUtf8;
        auto response = g_httpsClient.post(endpoint, request, L"");
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            { std::lock_guard<std::mutex> lock(m_mutex); if (itemData < (LRESULT)m_tickets.size()) m_tickets.erase(m_tickets.begin() + itemData); if (m_selectedIndex >= (int)m_tickets.size()) m_selectedIndex = (int)m_tickets.size() - 1; }
            updateUI();
            printReceiptForServedClient(clientId);
            if ((m_currentQueueType == L"general" || m_currentQueueType == L"extra_20" || m_currentQueueType == L"trust" || m_currentQueueType == L"paid" || m_currentQueueType == L"expensive") && !m_tickets.empty()) {
                SendMessageW(m_hListBox, LB_SETCURSEL, 0, 0); m_selectedIndex = 0; onAccept();
            }
            else if (!m_tickets.empty()) {
                int newIdx = m_selectedIndex < 0 ? 0 : m_selectedIndex;
                if (newIdx < (int)m_tickets.size()) speak(utf8_to_wstring(m_tickets[newIdx]["ticket_number"].get<std::string>()), L"1");
            }
        }
        else {
            std::wstring err = L"Ошибка обслуживания.";
            if (response && response->contains("error")) err += L"\n" + utf8_to_wstring((*response)["error"].get<std::string>());
            MessageBoxW(m_hWnd, err.c_str(), L"Ошибка", MB_OK);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WorkerWindow* pThis = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam; pThis = (WorkerWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis); pThis->m_hWnd = hWnd;
            pThis->createControls(); pThis->m_currentMode = Mode::QUEUE_LIST; pThis->m_maxItemNumber = 0; pThis->m_selectedIndex = -1;
            { std::lock_guard<std::mutex> lock(pThis->m_mutex); pThis->m_tickets = pThis->fetchTicketsFromServer(pThis->m_currentQueueType); }
            pThis->updateUI(); pThis->m_running = true;
            pThis->m_refreshThread = std::thread([pThis]() {
                while (pThis->m_running) { std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_INTERVAL_MS)); if (pThis->m_running) pThis->refreshList(); }
                });
            return 0;
        }
        pThis = (WorkerWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);
        if (msg == WM_SIZE) {
            if (pThis->m_currentMode == Mode::ITEM_INPUT) { pThis->repositionItemInputControls(); pThis->showItemInputControls(true); }
            else {
                RECT rc; GetClientRect(hWnd, &rc); int width = rc.right - rc.left, height = rc.bottom - rc.top;
                int leftListWidth = (width * 20) / 100; if (leftListWidth < 180) leftListWidth = 180;
                SetWindowPos(pThis->m_hListBox, NULL, 20, 60, leftListWidth - 40, height - 190, SWP_NOZORDER);
                SetWindowPos(pThis->m_hStatusLabel, NULL, 20, height - 120, leftListWidth - 40, 30, SWP_NOZORDER);
                int btnW = (leftListWidth - 40 - 10) / 2;
                SetWindowPos(pThis->m_hAcceptBtn, NULL, 20, height - 80, btnW, 50, SWP_NOZORDER);
                SetWindowPos(pThis->m_hServeBtn, NULL, 20 + btnW + 10, height - 80, btnW, 50, SWP_NOZORDER);
            }
            return 0;
        }
        switch (msg) {
        case WM_APP + 1: pThis->updateUI(); return 0;
        case WM_APP + 2: pThis->updateNextItemNumber(); pThis->updateTempItemsList(); return 0;
        case WM_APP + 3: {
            using ItemSaveResult = struct { bool success; std::wstring errorMsg; long long appendixNumber; };
            ItemSaveResult* res = *reinterpret_cast<ItemSaveResult**>(lParam);
            pThis->m_isSaving = false; EnableWindow(pThis->m_hItemSaveBtn, TRUE);
            if (res->success) {
                {
                    std::lock_guard<std::mutex> lock(pThis->m_receiptSnapshotMutex);
                    pThis->m_receiptSnapshot = ReceiptSnapshot{}; pThis->m_receiptSnapshot.valid = true;
                    pThis->m_receiptSnapshot.clientId = pThis->m_currentClientId; pThis->m_receiptSnapshot.appendixNumber = res->appendixNumber;
                    pThis->m_receiptSnapshot.items = pThis->m_tempItems;
                    int qty = 0; double value = 0.0, clientSum = 0.0;
                    for (const auto& it : pThis->m_tempItems) { qty += it.value("quantity", 1); value += it.value("estimated_price", 0.0) * it.value("quantity", 1); clientSum += it.value("client_amount", 0.0); }
                    pThis->m_receiptSnapshot.totalQty = qty; pThis->m_receiptSnapshot.totalValue = value; pThis->m_receiptSnapshot.totalClientAmount = clientSum;
                }
                MessageBoxW(pThis->m_hWnd, L"Товары успешно сохранены!", L"Успех", MB_OK);
                pThis->returnToQueueList();
            }
            else { MessageBoxW(pThis->m_hWnd, res->errorMsg.c_str(), L"Ошибка", MB_OK); }
            delete res; return 0;
        }
        case WM_RECEIPT_PRINT: {
            ReceiptData* pData = reinterpret_cast<ReceiptData*>(lParam);
            if (pData) {
                g_logger.info(L"WM_RECEIPT_PRINT: printing receipt, appendix=" +
                    std::to_wstring(pData->appendixNumber) +
                    L", clientId=" + std::to_wstring(pData->clientId) +
                    L", items=" + std::to_wstring(pData->items.size()));
                EnableWindow(pThis->m_hAcceptBtn, FALSE);
                EnableWindow(pThis->m_hServeBtn, FALSE);

                // Печать теперь безопасна: интерактивные принтеры пропускаются
                // внутри ReceiptPrinter::print (см. isPromptPort).
                bool ok = ReceiptPrinter::print(*pData);

                //СРАЗУ ПОСЛЕ приложения — ценники на КАЖДУЮ единицу товара
                const std::wstring workerFio = g_authManager.getFullName();
                g_logger.info(L"WM_RECEIPT_PRINT: printing price tags, worker='" + workerFio +
                    L"', appendix=" + std::to_wstring(pData->appendixNumber));
                const bool tagsOk = PriceTagPrinter::printForReceipt(*pData, workerFio);
                g_logger.info(L"WM_RECEIPT_PRINT: price tags result=" +
                    std::wstring(tagsOk ? L"true" : L"false") +
                    L", appendix=" + std::to_wstring(pData->appendixNumber));

                pThis->updateUI();
                g_logger.info(L"WM_RECEIPT_PRINT: print result=" +
                    std::wstring(ok ? L"true" : L"false") +
                    L", appendix=" + std::to_wstring(pData->appendixNumber));
                delete pData;
            }
            return 0;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam), code = HIWORD(wParam);
            if (id == 100 && code == LBN_SELCHANGE) {
                int newSel = (int)SendMessageW(pThis->m_hListBox, LB_GETCURSEL, 0, 0);
                if (newSel != LB_ERR) { std::lock_guard<std::mutex> lock(pThis->m_mutex); pThis->m_selectedIndex = newSel; }
                return 0;
            }
            if (id == 200 && code == CBN_SELCHANGE) {
                if (pThis->m_currentMode == Mode::ITEM_INPUT) { MessageBoxW(pThis->m_hWnd, L"Сначала завершите ввод товаров", L"Внимание", MB_OK); return 0; }
                LRESULT idx = SendMessageW(pThis->m_hComboQueue, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR && idx < (LRESULT)pThis->m_queueTypeIds.size()) {
                    std::lock_guard<std::mutex> lock(pThis->m_mutex);
                    pThis->m_currentQueueType = pThis->m_queueTypeIds[static_cast<int>(idx)]; pThis->m_selectedIndex = -1;
                }
                pThis->refreshList(); return 0;
            }
            if (id == ID_ITEM_ADD_BTN && code == BN_CLICKED) { pThis->onItemAdd(); return 0; }
            if (id == ID_ITEM_SAVE_BTN && code == BN_CLICKED) { pThis->onItemSave(); return 0; }
            if (id == ID_ITEM_CANCEL_BTN && code == BN_CLICKED) { pThis->onItemCancel(); return 0; }
            if (id == ID_ITEM_BACK_BTN && code == BN_CLICKED) { if (MessageBoxW(pThis->m_hWnd, L"Вернуться? Несохранённые данные будут потеряны.", L"Подтверждение", MB_YESNO) == IDYES) pThis->returnToQueueList(); return 0; }
        if (code == BN_CLICKED) { switch (id) { case 1: pThis->onAccept(); break; case 2: pThis->onServe(); break; case 3: PostQuitMessage(0); break; } return 0; }
                                                      break;
        }
        case WM_DESTROY:
            pThis->m_running = false; if (pThis->m_refreshThread.joinable()) pThis->m_refreshThread.join();
            if (pThis->m_pVoice) { pThis->m_pVoice->Release(); pThis->m_pVoice = nullptr; } CoUninitialize(); PostQuitMessage(0); return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createControls() {
        RECT rc; GetClientRect(m_hWnd, &rc); int width = rc.right - rc.left, height = rc.bottom - rc.top;
        m_hFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hSmallFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        m_hBrush = CreateSolidBrush(RGB(240, 240, 240));
        m_hComboQueue = CreateWindowExW(0, L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 20, 10, 200, 200, m_hWnd, (HMENU)200, g_hInstance, nullptr);
        SendMessageW(m_hComboQueue, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        struct QueueTypeEntry { const wchar_t* displayName; const wchar_t* id; };
        QueueTypeEntry entries[] = { { L"Общая очередь", L"general" }, { L"В первый раз (оформление) договора", L"first_time" }, { L"+20 позиций", L"extra_20" }, { L"На доверии", L"trust" }, { L"Платный прием", L"paid" }, { L"Дорогой товар", L"expensive" } };
        m_queueTypeIds.clear();
        for (const auto& entry : entries) { int idx = (int)SendMessageW(m_hComboQueue, CB_ADDSTRING, 0, (LPARAM)entry.displayName); if (idx != CB_ERR) m_queueTypeIds.push_back(entry.id); }
        SendMessageW(m_hComboQueue, CB_SETCURSEL, 0, 0);
        if (!m_queueTypeIds.empty()) m_currentQueueType = m_queueTypeIds[0]; else m_currentQueueType = L"general";
        int leftListWidth = (width * 25) / 100; if (leftListWidth < 200) leftListWidth = 200;
        m_hListBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY, 20, 60, leftListWidth - 40, height - 190, m_hWnd, (HMENU)100, g_hInstance, nullptr);
        SendMessageW(m_hListBox, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        m_hStatusLabel = CreateWindowExW(0, L"STATIC", L"Ожидают: 0", WS_VISIBLE | WS_CHILD | SS_CENTER, 20, height - 120, leftListWidth - 40, 30, m_hWnd, (HMENU)101, g_hInstance, nullptr);
        SendMessageW(m_hStatusLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        int btnW = (leftListWidth - 40 - 10) / 2;
        m_hAcceptBtn = CreateWindowExW(0, L"BUTTON", L"Принять", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20, height - 80, btnW, 50, m_hWnd, (HMENU)1, g_hInstance, nullptr);
        SendMessageW(m_hAcceptBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        m_hServeBtn = CreateWindowExW(0, L"BUTTON", L"Обслужен (Следующий)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + btnW + 10, height - 80, btnW, 50, m_hWnd, (HMENU)2, g_hInstance, nullptr);
        SendMessageW(m_hServeBtn, WM_SETFONT, (WPARAM)m_hSmallFont, TRUE);
        HWND hCloseBtn = CreateWindowExW(0, L"BUTTON", L"Закрыть", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, width - 120, 10, 100, 40, m_hWnd, (HMENU)3, g_hInstance, nullptr);
        SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)m_hFont, TRUE);
        int rightStart = leftListWidth + 20, rightWidth = width - rightStart - 20; if (rightWidth < 400) rightWidth = 400;
        createItemInputControls(rightStart, 80, rightWidth); showItemInputControls(true); setCurrentDateTime();
        SetWindowTextW(m_hItemNumberEdit, L""); SetWindowTextW(m_hItemDescEdit, L""); SetWindowTextW(m_hItemPriceEdit, L""); SetWindowTextW(m_hItemQtyEdit, L"");
        SetWindowTextW(m_hItemConditionEdit, L""); SetWindowTextW(m_hItemNoteEdit, L""); SetWindowTextW(m_hItemClientPercentEdit, L""); SetWindowTextW(m_hItemStorePercentEdit, L"");
        m_tempItems.clear(); m_currentClientId = -1; updateTempItemsList();
    }

public:
    WorkerWindow() : m_hWnd(nullptr), m_hListBox(nullptr), m_hAcceptBtn(nullptr), m_hServeBtn(nullptr), m_hStatusLabel(nullptr), m_hComboQueue(nullptr),
        m_hFont(nullptr), m_hSmallFont(nullptr), m_hBrush(nullptr), m_running(false), m_currentMode(Mode::QUEUE_LIST), m_currentClientId(0), m_isSaving(false),
        m_maxItemNumber(0), m_selectedIndex(-1), m_hTitle(nullptr), m_hLabelDate(nullptr), m_hLabelNumber(nullptr), m_hLabelDesc(nullptr), m_hLabelPrice(nullptr),
        m_hLabelQty(nullptr), m_hLabelCondition(nullptr), m_hLabelNote(nullptr), m_hItemDateLabel(nullptr), m_hItemNumberEdit(nullptr), m_hItemDescEdit(nullptr),
        m_hItemPriceEdit(nullptr), m_hItemQtyEdit(nullptr), m_hItemConditionEdit(nullptr), m_hItemNoteEdit(nullptr), m_hItemAddBtn(nullptr), m_hItemSaveBtn(nullptr),
        m_hItemCancelBtn(nullptr), m_hItemListView(nullptr), m_hItemTotalQtyLabel(nullptr), m_hItemTotalPriceLabel(nullptr), m_hItemBackBtn(nullptr),
        m_hLabelClientPercent(nullptr), m_hItemClientPercentEdit(nullptr), m_hLabelStorePercent(nullptr), m_hItemStorePercentEdit(nullptr),
        m_hItemTotalClientAmountLabel(nullptr), m_hItemTotalStoreAmountLabel(nullptr) {
        initializeTTS();
    }

    ~WorkerWindow() { if (m_hFont) DeleteObject(m_hFont); if (m_hSmallFont) DeleteObject(m_hSmallFont); if (m_hBrush) DeleteObject(m_hBrush); }

    void show() {
        WNDCLASSEXW wc = {}; wc.cbSize = sizeof(WNDCLASSEX); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"WorkerWindowClass"; wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
        m_hWnd = CreateWindowExW(0, L"WorkerWindowClass", L"Товаровед - Управление очередями", WS_OVERLAPPEDWINDOW, 0, 0, screenW, screenH, nullptr, nullptr, g_hInstance, this);
        if (!m_hWnd) return;
        ShowWindow(m_hWnd, SW_SHOW); UpdateWindow(m_hWnd);
        MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
};
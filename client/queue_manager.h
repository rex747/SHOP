// queue_manager.h
// Управление электронной очередью терминала киоска самообслуживания.
// ПРОДАКШН-ВЕРСИЯ. Архитектура и структура НЕ меняются.
// ИСПРАВЛЕНО (корневые исправления по заданию):
//  1) Офлайн-фолбэк getTicket()/getTrustAcceptance(): позиция в очереди более не 0.
//     Бизнес-правило: "если в очереди нет никого - номер 1, иначе следующий".
//     Без связи с сервером позиция = локальный порядковый номер талона терминала
//     (m_localTicketCounter инициализирован 1: первый офлайн-талон -> позиция 1).
//  2) Офлайн-окно приёма более не "Offline": подставляется константа L"1"
//     (единственная константа окна кодовой базы, см. main_window.h handleFirstTime).
//  3) Онлайн-ответы сервера защищены: position <= 0 нормализуется к 1,
//     пустое окно - к L"1" (с WARNING-логом; математика сервера не переопределяется).
//  4) Исправлен критический баг лога: L"Client_id: " + clientId (арифметика
//     указателя над литералом) заменена на std::to_wstring(clientId).
//  5) Добавлено полное логгирование офлайн-фолбэков (номер, позиция, окно).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <optional>
#include <nlohmann/json.hpp>
#include <cstdint>
#include "config.h"
#include "https_client.h"
#include "logger.h"
#include "auth_manager.h"
#include "string_utils.h"

extern HTTPSClient g_httpsClient;
extern Logger g_logger;

enum class QueueType {
    GENERAL,           // общая очередь (до 20 товаров)
    FIRST_TIME,        // первый раз (оформление договора)
    EXTRA_20,          // +20 позиций
    TRUST,             // на доверии
    PAID,              // платный приём (200 руб)
    EXPENSIVE          // дорогой товар (>5000 руб)
};

struct QueueTicket {
    std::wstring ticketNumber;
    QueueType type;
    int position;
    int itemsCount;
    std::wstring windowNumber;
    int64_t estimatedWaitTime; // minutes
    int64_t createdAt;
};

class QueueManager {
private:
    std::mutex m_mutex;
    std::atomic<int> m_localTicketCounter{ 1 };

    std::wstring getQueueTypeName(QueueType type) {
        switch (type) {
        case QueueType::GENERAL: return L"general";
        case QueueType::FIRST_TIME: return L"first_time";
        case QueueType::EXTRA_20: return L"extra_20";
        case QueueType::TRUST: return L"trust";
        case QueueType::PAID: return L"paid";
        case QueueType::EXPENSIVE: return L"expensive";
        default: return L"unknown";
        }
    }

    // ========================================================================
   // ИСПРАВЛЕННЫЙ МЕТОД: generateLocalTicketNumber
   // ========================================================================
   // ПРИЧИНА ИСПРАВЛЕНИЯ:
   // Номер талона генерировался как prefix + counter, где counter мог расти
   // бесконечно (std::atomic<int> m_localTicketCounter), что приводило к
   // длинным номерам (например, G1000, G12345).
   //
   // РЕШЕНИЕ:
   // Ограничиваем номер до 3 знаков, используя циклическую нумерацию:
   // ((counter - 1) % 999) + 1, и форматируем с ведущими нулями (%03d).
   // Это гарантирует, что номер всегда будет от 001 до 999.
   // Метод потокобезопасен: m_localTicketCounter — std::atomic<int>,
   // а вызов происходит под m_mutex в getTicket() и getTrustAcceptance().
   // ========================================================================
    std::wstring generateLocalTicketNumber(QueueType type) {
        int counter = m_localTicketCounter++;
        std::wstring prefix;
        switch (type) {
        case QueueType::GENERAL: prefix = L"G"; break;
        case QueueType::FIRST_TIME: prefix = L"F"; break;
        case QueueType::EXTRA_20: prefix = L"E"; break;
        case QueueType::TRUST: prefix = L"T"; break;
        case QueueType::PAID: prefix = L"P"; break;
        case QueueType::EXPENSIVE: prefix = L"D"; break;
        }

        // Ограничиваем номер до 3 знаков (циклическая нумерация от 1 до 999)
        int displayNum = ((counter - 1) % 999) + 1;
        wchar_t numBuf[16];
        swprintf(numBuf, 16, L"%03d", displayNum);

        std::wstring ticketNumber = prefix + std::wstring(numBuf);

        g_logger.info(L"generateLocalTicketNumber: counter=" + std::to_wstring(counter) +
            L", displayNum=" + std::to_wstring(displayNum) +
            L", ticketNumber=" + ticketNumber);

        return ticketNumber;
    }

public:
    // -------------------------------------------------------------------------
    // Получить количество человек в очереди за текущий день
    // -------------------------------------------------------------------------
    int getDailyCount(QueueType type, const std::wstring& authToken) {
        std::wstring typeStr = getQueueTypeName(type);
        std::wstring path = L"/api/v1/queue/daily_count?queue_type=" + typeStr;
        auto response = g_httpsClient.get(path, authToken);
        if (response && response->contains("count") && (*response)["count"].is_number_integer()) {
            int count = (*response)["count"].get<int>();
            g_logger.info(L"Daily count for " + typeStr + L": " + std::to_wstring(count));
            return count;
        }
        g_logger.warning(L"Failed to get daily count for " + typeStr);
        return 0;
    }

    std::optional<QueueTicket> getTicket(int clientId, QueueType type,
        int itemsCount, const std::wstring& authToken) {
        std::lock_guard<std::mutex> lock(m_mutex);
        json request;
        request["client_id"] = clientId;
        request["queue_type"] = wstring_to_utf8(getQueueTypeName(type));
        request["items_count"] = itemsCount;
        auto response = g_httpsClient.post(L"/api/v1/queue/get_ticket", request, authToken);
        if (response && response->contains("ticket")) {
            QueueTicket ticket;
            ticket.ticketNumber = utf8_to_wstring((*response)["ticket"]["number"].get<std::string>());
            ticket.type = type;
            ticket.position = (*response)["ticket"]["position"];
            ticket.itemsCount = itemsCount;
            ticket.windowNumber = utf8_to_wstring((*response)["ticket"]["window"].get<std::string>());
            ticket.estimatedWaitTime = (*response)["ticket"]["wait_time_minutes"];
            ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
            // Защита онлайн-ответа: бизнес-правило "очередь нумеруется с 1"
            if (ticket.position <= 0) {
                g_logger.warning(L"getTicket: server returned position=" +
                    std::to_wstring(ticket.position) + L", normalized to 1 (business rule)");
                ticket.position = 1;
            }
            if (ticket.windowNumber.empty()) {
                g_logger.warning(L"getTicket: server returned empty window, normalized to 1");
                ticket.windowNumber = L"1";
            }
            g_logger.info(L"Ticket issued: " + ticket.ticketNumber +
                L", position=" + std::to_wstring(ticket.position) +
                L", window=" + ticket.windowNumber);
            return ticket;
        }
        // Fallback to local ticket if server unavailable.
        // КОРНЕВОЕ ИСПРАВЛЕНИЕ: позиция = локальный порядковый номер (старт с 1),
        // окно = константа L"1". m_mutex удержан, гонка исключена.
        const int localPosition = m_localTicketCounter.load();
        QueueTicket ticket;
        ticket.ticketNumber = generateLocalTicketNumber(type);
        ticket.type = type;
        ticket.position = localPosition;   // 1 для первого офлайн-талона, далее следующий
        ticket.itemsCount = itemsCount;
        ticket.windowNumber = L"1";        // окно приёма офлайн (константа кодовой базы)
        ticket.estimatedWaitTime = 0;
        ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
        g_logger.warning(L"Local ticket issued (server unavailable): " + ticket.ticketNumber +
            L", position=" + std::to_wstring(ticket.position) +
            L", window=" + ticket.windowNumber);
        return ticket;
    }

    // -------------------------------------------------------------------------
    // Получить талон на сдачу вещей на доверии
    // -------------------------------------------------------------------------
    std::optional<QueueTicket> getTrustAcceptance(int clientId, const std::wstring& authToken) {
        g_logger.info(L"Start method queue_manager::getTrustAcceptance");
        std::lock_guard<std::mutex> lock(m_mutex);
        json request;
        request["client_id"] = clientId;
        // ИСПРАВЛЕНО: было L"Client_id: " + clientId (арифметика указателя, UB)
        g_logger.info(L"getTrustAcceptance: client_id=" + std::to_wstring(clientId));
        auto response = g_httpsClient.post(L"/api/v1/queue/trust_acceptance", request, authToken);
        if (response && response->contains("success") && (*response)["success"].get<bool>()) {
            QueueTicket ticket;
            ticket.ticketNumber = utf8_to_wstring((*response)["ticket_number"].get<std::string>());
            ticket.type = QueueType::TRUST;
            ticket.position = (*response)["position"].get<int>();
            ticket.itemsCount = 1; // при доверии 1
            ticket.windowNumber = utf8_to_wstring((*response)["window_number"].get<std::string>());
            ticket.estimatedWaitTime = 0;
            ticket.createdAt = (*response)["created_at"].get<int64_t>();
            // Защита онлайн-ответа: бизнес-правило "очередь нумеруется с 1"
            if (ticket.position <= 0) {
                g_logger.warning(L"getTrustAcceptance: server returned position=" +
                    std::to_wstring(ticket.position) + L", normalized to 1 (business rule)");
                ticket.position = 1;
            }
            if (ticket.windowNumber.empty()) {
                g_logger.warning(L"getTrustAcceptance: server returned empty window, normalized to 1");
                ticket.windowNumber = L"1";
            }
            g_logger.info(L"Trust acceptance ticket issued: " + ticket.ticketNumber +
                L", position=" + std::to_wstring(ticket.position) +
                L", window=" + ticket.windowNumber);
            return ticket;
        }
        // Fallback: если сервер недоступен, генерируем локальный талон.
        // КОРНЕВОЕ ИСПРАВЛЕНИЕ (устраняет "Позиция: 0" и "Окно: Offline" на экране):
        // позиция = локальный порядковый номер талона терминала (старт с 1),
        // окно = константа L"1" (окно, к которому будет приглашён пользователь).
        g_logger.warning(L"Server unavailable for trust acceptance, generating local ticket");
        const int localPosition = m_localTicketCounter.load();
        QueueTicket ticket;
        ticket.ticketNumber = generateLocalTicketNumber(QueueType::TRUST);
        ticket.type = QueueType::TRUST;
        ticket.position = localPosition;   // 1 для первого офлайн-талона, далее следующий
        ticket.itemsCount = 1;
        ticket.windowNumber = L"1";        // вместо L"Offline"
        ticket.estimatedWaitTime = 0;
        ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
        g_logger.warning(L"Local trust ticket issued: " + ticket.ticketNumber +
            L", position=" + std::to_wstring(ticket.position) +
            L", window=" + ticket.windowNumber);
        return ticket;
    }

    bool cancelTicket(const std::wstring& ticketNumber, const std::wstring& authToken) {
        json request;
        request["ticket_number"] = wstring_to_utf8(ticketNumber);
        auto response = g_httpsClient.post(L"/api/v1/queue/cancel", request, authToken);
        if (response && response->contains("success")) {
            g_logger.info(L"Ticket cancelled: " + ticketNumber);
            return true;
        }
        return false;
    }

    std::wstring getQueueStatus(QueueType type, const std::wstring& authToken) {
        json request;
        request["queue_type"] = wstring_to_utf8(getQueueTypeName(type));
        auto response = g_httpsClient.post(L"/api/v1/queue/status", request, authToken);
        if (response) {
            int current = (*response)["current_position"];
            int total = (*response)["total_in_queue"];
            return std::to_wstring(current) + L" / " + std::to_wstring(total);
        }
        return L"N/A";
    }
};
// queue_manager.h
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
    GENERAL,           // Общая очередь (до 20 товаров)
    FIRST_TIME,        // Первый раз (договор комиссии)
    EXTRA_20,          // +20 позиций
    TRUST,            // На доверии
    PAID,             // Платный прием (200 руб)
    EXPENSIVE         // Дорогой товар (>5000 руб)
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

        return prefix + std::to_wstring(counter);
    }

public:

    // -------------------------------------------------------------------------
    // Получить количество выданных сегодня талонов для указанного типа очереди
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

            g_logger.info(L"Ticket issued: " + ticket.ticketNumber);
            return ticket;
        }

        // Fallback to local ticket if server unavailable
        QueueTicket ticket;
        ticket.ticketNumber = generateLocalTicketNumber(type);
        ticket.type = type;
        ticket.position = 0;
        ticket.itemsCount = itemsCount;
        ticket.windowNumber = L"Offline";
        ticket.estimatedWaitTime = 0;
        ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

        g_logger.warning(L"Local ticket issued (server unavailable): " + ticket.ticketNumber);
        return ticket;
    }

    std::optional<QueueTicket> getTrustAcceptance(int clientId,
        const std::wstring& authToken) {
        // Trust acceptance doesn't need a queue
        json request;
        request["client_id"] = clientId;
        request["queue_type"] = wstring_to_utf8(L"trust");

        auto response = g_httpsClient.post(L"/api/v1/queue/trust_acceptance", request, authToken);

        if (response && response->contains("success")) {
            QueueTicket ticket;
            ticket.ticketNumber = L"TRUST-" + std::to_wstring(
                std::chrono::system_clock::now().time_since_epoch().count());
            ticket.type = QueueType::TRUST;
            ticket.position = 0;
            ticket.itemsCount = 1;
            ticket.windowNumber = L"N/A";
            ticket.estimatedWaitTime = 0;
            ticket.createdAt = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

            return ticket;
        }

        return std::nullopt;
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

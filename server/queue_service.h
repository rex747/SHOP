// queue_service.h
#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "database.h"

using json = nlohmann::json;

class QueueService {
private:
    std::shared_ptr<Database> db_;

public:
    // Внедрение зависимости через конструктор
    explicit QueueService(std::shared_ptr<Database> db) : db_(std::move(db)) {}

    bool initialize() {
        return true; // Инициализация таблиц теперь полностью в Database::initialize()
    }

    json getTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket = db_->createTicket(clientId, queueType, itemsCount);
        json j;
        j["id"] = ticket.id;
        j["number"] = ticket.number;
        j["client_id"] = ticket.clientId;
        j["queue_type"] = ticket.queueType;
        j["position"] = ticket.position;
        j["items_count"] = ticket.itemsCount;
        j["window"] = ticket.windowNumber;
        j["wait_time_minutes"] = ticket.estimatedWaitTime;
        j["created_at"] = ticket.createdAt;
        j["status"] = ticket.status;
        return j;
    }

    bool createTrustAcceptance(int clientId) {
        return db_->createTrustAcceptance(clientId);
    }

    bool serveTicket(const std::string& ticketNumber) {
        return db_->serveTicket(ticketNumber);
    }

    json getQueueStatus(const std::string& queueType) {
        return db_->getQueueStatus(queueType);
    }
};
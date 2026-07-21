// queue_service.h
#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>

#include "database.h"
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;
using json = nlohmann::json;

class QueueService {
private:
    std::map<std::string, int> m_queueCounts;
    std::mutex m_mutex;
    std::atomic<int> m_ticketCounter{ 1 };
    std::shared_ptr<Database> db_;

    int getMaxQueueSize(const std::string& queueType) {
        if (queueType == "general") return Config::MAX_QUEUE_SIZE_GENERAL;
        if (queueType == "first_time") return Config::MAX_QUEUE_SIZE_FIRST_TIME;
        if (queueType == "extra_20") return Config::MAX_QUEUE_SIZE_EXTRA_20;
        if (queueType == "paid") return Config::MAX_QUEUE_SIZE_PAID;
        if (queueType == "expensive") return Config::MAX_QUEUE_SIZE_EXPENSIVE;
        return 100;
    }

public:
    explicit QueueService(std::shared_ptr<Database> db) : db_(db) {}


    bool initialize() {
        return true;
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

// queue_service.h
#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include "database.h"
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

class QueueService {
private:
    std::map<std::string, int> m_queueCounts;
    std::mutex m_mutex;
    std::atomic<int> m_ticketCounter{ 1 };

    int getMaxQueueSize(const std::string& queueType) {
        if (queueType == "general") return Config::MAX_QUEUE_SIZE_GENERAL;
        if (queueType == "first_time") return Config::MAX_QUEUE_SIZE_FIRST_TIME;
        if (queueType == "extra_20") return Config::MAX_QUEUE_SIZE_EXTRA_20;
        if (queueType == "paid") return Config::MAX_QUEUE_SIZE_PAID;
        if (queueType == "expensive") return Config::MAX_QUEUE_SIZE_EXPENSIVE;
        return 100;
    }

public:
    bool initialize() {
        return true;
    }

    json getTicket(int clientId, const std::string& queueType, int itemsCount) {
        // Check queue size limit
        int maxSize = getMaxQueueSize(queueType);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            int currentSize = m_queueCounts[queueType];
            if (currentSize >= maxSize) {
                g_serverLogger.warning("Queue " + queueType + " is full");
                return json{ {"error", "Queue is full"} };
            }
            m_queueCounts[queueType]++;
        }

        // Create ticket in database
        QueueTicket ticket = Database::createTicket(clientId, queueType, itemsCount);

        return Database::ticketToJson(ticket);
    }

    bool createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *g_dbConnection };
            txn.exec(
                "INSERT INTO trust_acceptances (client_id) VALUES ($1)",
                clientId
            );
            txn.commit();

            g_serverLogger.info("Trust acceptance created for client " + std::to_string(clientId));
            return true;

        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("createTrustAcceptance error: ") + e.what());
            return false;
        }
    }

    void serveTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *g_dbConnection };
            int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;

            auto result = txn.exec(
                "UPDATE queue_tickets SET status = 'served', served_at = $1 "
                "WHERE number = $2 RETURNING queue_type",
                pqxx::params{ now, ticketNumber }
            );

            if (!result.empty()) {
                std::string queueType = result[0][0].as<std::string>();
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_queueCounts[queueType] > 0) {
                    m_queueCounts[queueType]--;
                }
            }

            txn.commit();

        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("serveTicket error: ") + e.what());
        }
    }

    json getQueueStatus(const std::string& queueType) {
        try {
            pqxx::work txn{ *g_dbConnection };
            auto result = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );

            int count = result[0][0].as<int>();

            json status;
            status["queue_type"] = queueType;
            status["current_position"] = count;
            status["total_in_queue"] = count;
            status["max_size"] = getMaxQueueSize(queueType);

            return status;

        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("getQueueStatus error: ") + e.what());
            return json{ {"error", e.what()} };
        }
    }
};

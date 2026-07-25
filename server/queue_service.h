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
    // ¬недрение зависимости через конструктор
    explicit QueueService(std::shared_ptr<Database> db) : db_(std::move(db)) {}

    bool initialize() {
        return true; // »нициализаци€ таблиц теперь полностью в Database::initialize()
    }
	// ћетод дл€ получени€ номерка в очереди
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
	// ћетод дл€ создани€ номерка дл€ тех, кто приходит впервые
    std::string createFirstTimeTicket(const std::string& window = "1") {
        return db_->createFirstTimeTicket(window);
    }
	// ћетод дл€ получени€ списка ожидающих номерков дл€ тех, кто приходит впервые
    std::vector<json> getWaitingFirstTimeTickets() {
        return db_->getWaitingFirstTimeTickets();
    }
	// ћетод дл€ прин€ти€ номерка дл€ тех, кто приходит впервые
    bool acceptFirstTimeTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptFirstTimeTicket(ticketNumber, windowNumber);
    }
	// ћетод дл€ обслуживани€ номерка дл€ тех, кто приходит впервые
    bool serveFirstTimeTicket(const std::string& ticketNumber) {
        return db_->serveFirstTimeTicket(ticketNumber);
    }
	// ћетод дл€ обслуживани€ номерка дл€ очереди +20 товаров
    std::vector<json> getWaitingTickets(const std::string& queueType) {
        return db_->getWaitingTickets(queueType);
    }
	// ћетод дл€ прин€ти€ номерка дл€ очереди +20 товаров
    bool acceptTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptTicket(ticketNumber, windowNumber);
    }
    // ћетоды дл€ сдачи вещей на доверии
    std::vector<json> getWaitingTrustTickets() {
        return db_->getWaitingTrustTickets();
    }
    bool acceptTrustTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptTrustTicket(ticketNumber, windowNumber);
    }
    bool serveTrustTicket(const std::string& ticketNumber) {
        return db_->serveTrustTicket(ticketNumber);
    }
	// ћетод дл€ обслуживани€ номерка
    bool serveTicket(const std::string& ticketNumber) {
        return db_->serveTicket(ticketNumber);
    }
	// ћетод дл€ получени€ статуса очереди
    json getQueueStatus(const std::string& queueType) {
        return db_->getQueueStatus(queueType);
    }
};
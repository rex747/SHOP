// queue_service.h
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "database.h"
#include "logger_server.h"

using json = nlohmann::json;

class QueueService {
private:
    std::shared_ptr<Database> db_;

public:
    // Конструктор: принимает разделяемый указатель на Database
    explicit QueueService(std::shared_ptr<Database> db) : db_(std::move(db)) {}

    bool initialize() {
        return true; // Инициализация выполняется внутри Database::initialize()
    }

    // Получить талон для обычной очереди
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
        g_serverLogger.info("QueueService::getTicket: clientId=" + std::to_string(clientId) +
            ", queueType=" + queueType +
            ", ticketNumber=" + ticket.number +
            ", position=" + std::to_string(ticket.position));
        return j;
    }

    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: createFirstTimeTicket
    // ========================================================================
    // ПРИЧИНА ОШИБКИ [json.exception.type_error.302]:
    // Метод Database::createFirstTimeTicket() теперь возвращает json-объект
    // (с полями ticket_number, position, window_number, wait_time_minutes,
    // created_at), а данный метод был объявлен как возвращающий std::string.
    //
    // Библиотека nlohmann::json содержит шаблонный оператор неявного
    // преобразования operator ValueType(), поэтому код компилировался,
    // но в рантайме при попытке преобразовать json-ОБЪЕКТ в std::string
    // выбрасывалось исключение type_error.302 "type must be string, but is object".
    //
    // РЕШЕНИЕ:
    // Возвращаемый тип изменён со std::string на json. Теперь json-объект,
    // сформированный в Database::createFirstTimeTicket(), передаётся в
    // server.cpp::handleFirstTimeCreate() БЕЗ какого-либо преобразования,
    // и клиент получает полную информацию о талоне: номер, позицию, окно,
    // время ожидания.
    // ========================================================================
    json createFirstTimeTicket(const std::string& window = "1") {
        g_serverLogger.info("QueueService::createFirstTimeTicket: delegating to Database, window=" + window);
        return db_->createFirstTimeTicket(window);
    }

    // Получить список ожидающих талонов для очереди "Первый раз"
    std::vector<json> getWaitingFirstTimeTickets() {
        return db_->getWaitingFirstTimeTickets();
    }

    // Принять талон для очереди "Первый раз"
    bool acceptFirstTimeTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptFirstTimeTicket(ticketNumber, windowNumber);
    }

    // Обслужить талон для очереди "Первый раз"
    bool serveFirstTimeTicket(const std::string& ticketNumber) {
        return db_->serveFirstTimeTicket(ticketNumber);
    }

    // Получить ожидающие талоны для обычных очередей (+20 позиций и др.)
    std::vector<json> getWaitingTickets(const std::string& queueType) {
        return db_->getWaitingTickets(queueType);
    }

    // Принять талон обычной очереди
    bool acceptTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptTicket(ticketNumber, windowNumber);
    }

    // Получить ожидающие талоны очереди "На доверии"
    std::vector<json> getWaitingTrustTickets() {
        return db_->getWaitingTrustTickets();
    }

    // Принять талон очереди "На доверии"
    bool acceptTrustTicket(const std::string& ticketNumber, std::string& windowNumber) {
        return db_->acceptTrustTicket(ticketNumber, windowNumber);
    }

    // Обслужить талон очереди "На доверии"
    bool serveTrustTicket(const std::string& ticketNumber) {
        return db_->serveTrustTicket(ticketNumber);
    }

    // Обслужить талон обычной очереди
    bool serveTicket(const std::string& ticketNumber) {
        return db_->serveTicket(ticketNumber);
    }

    // Получить статус очереди
    json getQueueStatus(const std::string& queueType) {
        return db_->getQueueStatus(queueType);
    }
};
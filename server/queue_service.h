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

    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getTicket
    // ========================================================================
    //
    // ВЫЯВЛЕННЫЙ ДЕФЕКТ (подтверждён логами 2026-08-23):
    //
    //   Метод сериализовал результат Database::createTicket в JSON БЕЗ
    //   проверки успешности создания талона. Если createTicket выбрасывал
    //   исключение (конфликт уникальности номера) и возвращал
    //   неинициализированную структуру с мусорными значениями,
    //   данный метод сериализовал этот мусор в JSON и возвращал его
    //   как валидный талон. Клиент получал:
    //     {"success":true,"ticket":{"position":-1100584656,
    //      "wait_time_minutes":105497289322101,"number":"",...}}
    //   Подтверждение: лог терминала 17:10:48.636.
    //
    // ИСПРАВЛЕНИЕ:
    //   Добавлена ЗАЩИТНАЯ ПРОВЕРКА: если Database::createTicket вернул
    //   структуру с пустым полем `status`, значит талон НЕ был создан.
    //   В этом случае метод возвращает JSON с полем "error" вместо
    //   мусорных данных. Вызывающий код (server.cpp::handleGetTicket)
    //   обнаруживает это поле и возвращает клиенту ошибку.
    //
    //   Если `ticket.status == "waiting"` — талон создан успешно,
    //   сериализуем в JSON как раньше (формат ответа НЕ МЕНЯЕТСЯ).
    //
    //   Бизнес-логика НЕ МЕНЯЕТСЯ: клиент получает либо валидный талон,
    //   либо ошибку. Ранее клиент получал мусорные данные с `success: true`.
    //
    // ========================================================================
    json getTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket = db_->createTicket(clientId, queueType, itemsCount);

        // ЗАЩИТНАЯ ПРОВЕРКА: пустой `status` означает, что талон не был
        // создан (исключение в createTicket, все номера заняты, ошибка БД).
        // Возвращаем ошибку вместо мусорных данных.
        if (ticket.status.empty()) {
            g_serverLogger.error("QueueService::getTicket: FAILED to create ticket - "
                "createTicket returned empty status. "
                "clientId=" + std::to_string(clientId) +
                ", queueType=" + queueType +
                ", itemsCount=" + std::to_string(itemsCount));
            json errorResponse;
            errorResponse["error"] = "Failed to create ticket";
            return errorResponse;
        }

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
        g_serverLogger.info("QueueService::getTicket: SUCCESS - "
            "clientId=" + std::to_string(clientId) +
            ", queueType=" + queueType +
            ", ticketNumber=" + ticket.number +
            ", position=" + std::to_string(ticket.position) +
            ", waitTime=" + std::to_string(ticket.estimatedWaitTime) +
            ", createdAt=" + std::to_string(ticket.createdAt));
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

    // =========================================================================
    // МЕТОДЫ ДЛЯ ТВ-ДИСПЛЕЯ ОЧЕРЕДЕЙ (обёртки над Database)
    // =========================================================================

    /**
     * @brief Возвращает принятые талоны для очередей из таблицы queue_tickets
     */
    std::vector<json> getAcceptedTickets(const std::string& queueType) {
        return db_->getAcceptedTickets(queueType);
    }

    /**
     * @brief Возвращает принятые талоны очереди "Первый раз"
     */
    std::vector<json> getAcceptedFirstTimeTickets() {
        return db_->getAcceptedFirstTimeTickets();
    }

    /**
     * @brief Возвращает принятые талоны очереди "Доверие"
     */
    std::vector<json> getAcceptedTrustTickets() {
        return db_->getAcceptedTrustTickets();
    }

    // =========================================================================
    // НОВЫЕ МЕТОДЫ: УДАЛЕНИЕ ТАЛОНОВ ИЗ ОЧЕРЕДЕЙ
    // =========================================================================
    //
    // Обёртки над методами Database для удаления талонов, по которым
    // комитент не пришёл к окну товароведа в течение 2 минут.
    // Вызываются из server.cpp::handleQueueDeleteTicket().
    //
    // Потокобезопасность: каждый метод делегирует выполнение в Database,
    // где удаление выполняется атомарно в одной транзакции.
    // =========================================================================

    /**
     * @brief Удаляет талон из общих очередей (general, extra_20, paid, expensive)
     * @param ticketNumber номер талона
     * @param queueType тип очереди
     * @return true если талон удалён
     */
    bool deleteWaitingTicket(const std::string& ticketNumber, const std::string& queueType) {
        return db_->deleteWaitingTicket(ticketNumber, queueType);
    }

    /**
     * @brief Удаляет талон из очереди "Первый раз"
     * @param ticketNumber номер талона
     * @return true если талон удалён
     */
    bool deleteFirstTimeTicket(const std::string& ticketNumber) {
        return db_->deleteFirstTimeTicket(ticketNumber);
    }

    /**
     * @brief Удаляет талон из очереди "На доверии"
     * @param ticketNumber номер талона
     * @return true если талон удалён
     */
    bool deleteTrustTicket(const std::string& ticketNumber) {
        return db_->deleteTrustTicket(ticketNumber);
    }

};
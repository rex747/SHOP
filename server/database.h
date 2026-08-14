// server/database.h
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

#include "config_server.h"
#include "crypto_utils.h"
#include "logger_server.h"

using json = nlohmann::json;

struct Client {
    int id;
    std::string phone;
    std::string name;
    std::string email;
    std::string role; // роль клиент / товаровед
    int64_t createdAt;
    bool active;
};

struct QueueTicket {
    int id;
    std::string number;
    int clientId;
    std::string queueType;
    int position;
    int itemsCount;
    std::string windowNumber;
    int64_t estimatedWaitTime;
    int64_t createdAt;
    std::string status;
};

class Database {
private:
    std::shared_ptr<pqxx::connection> conn_;
    std::string encryption_key_;

public:
    Database(std::shared_ptr<pqxx::connection> conn, std::string enc_key)
        : conn_(std::move(conn)), encryption_key_(std::move(enc_key)) {
    }

    bool initialize() {
        try {
            pqxx::work txn{ *conn_ };

            // ========================================================================
            // Создание таблиц с полной структурой через CREATE TABLE IF NOT EXISTS
            // ========================================================================
            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS clients (
                id SERIAL PRIMARY KEY,
                phone VARCHAR(20) UNIQUE NOT NULL,
                last_name VARCHAR(64) NOT NULL,
                first_name VARCHAR(64) NOT NULL,
                middle_name VARCHAR(64),
                email VARCHAR(64),
                totp_secret_encrypted TEXT,
                items_submitted INTEGER DEFAULT 0,
                items_sold INTEGER DEFAULT 0,
                created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                updated_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW())
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS auth_tokens (
                id SERIAL PRIMARY KEY,
                client_id INTEGER REFERENCES clients(id),
                access_token_hash VARCHAR(256) NOT NULL,
                refresh_token_hash VARCHAR(256) NOT NULL,
                created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                expires_at INTEGER NOT NULL,
                revoked BOOLEAN DEFAULT FALSE
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS first_time_tickets (
                id SERIAL PRIMARY KEY,
                ticket_number VARCHAR(20) UNIQUE NOT NULL,
                window_number VARCHAR(10) DEFAULT '1',
                status VARCHAR(20) DEFAULT 'waiting',
                created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                accepted_at BIGINT,
                served_at BIGINT
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS queue_tickets (
                id SERIAL PRIMARY KEY,
                number VARCHAR(20) UNIQUE NOT NULL,
                client_id INTEGER REFERENCES clients(id),
                queue_type VARCHAR(20) NOT NULL,
                position INTEGER DEFAULT 0,
                items_count INTEGER DEFAULT 0,
                window_number VARCHAR(10) DEFAULT '1',
                estimated_wait_time INTEGER DEFAULT 0,
                created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                status VARCHAR(20) DEFAULT 'waiting',
                accepted_at BIGINT,
                served_at BIGINT
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS trust_acceptances (
                id SERIAL PRIMARY KEY,
                client_id INTEGER REFERENCES clients(id),
                ticket_number VARCHAR(20) UNIQUE NOT NULL,
                window_number VARCHAR(10) DEFAULT '1',
                status VARCHAR(20) DEFAULT 'pending',
                created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                accepted_at BIGINT,
                served_at BIGINT
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS sync_log (
                id SERIAL PRIMARY KEY,
                sync_type VARCHAR(50) NOT NULL,
                records_count INTEGER DEFAULT 0,
                status VARCHAR(20) NOT NULL,
                error_message TEXT,
                created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())
            )
        )");

            txn.exec(R"(
            CREATE TABLE IF NOT EXISTS items (
                id SERIAL PRIMARY KEY,
                client_id INTEGER REFERENCES clients(id),
                description TEXT NOT NULL,
                estimated_price DECIMAL(10,2) DEFAULT 0,
                synced_to_1c BOOLEAN DEFAULT FALSE,
                created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())
            )
        )");
            // ========================================================================
            // МИГРАЦИЯ: добавление колонки role
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS role VARCHAR(20) DEFAULT 'client';
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");

            // ========================================================================
            // Миграции — добавляем отсутствующие колонки БЕЗОПАСНО
            // ========================================================================
            // МИГРАЦИЯ: добавление новых колонок в таблицу items
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS item_number INTEGER;
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS quantity INTEGER DEFAULT 1;
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS sale_date BIGINT;
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS condition TEXT;
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS note TEXT;
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS status VARCHAR(20) DEFAULT 'pending';
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
        )");
            // Миграция для queue_tickets
            txn.exec(R"(
            DO $$
            BEGIN
                ALTER TABLE queue_tickets ADD COLUMN IF NOT EXISTS status VARCHAR(20) DEFAULT 'waiting';
                ALTER TABLE queue_tickets ADD COLUMN IF NOT EXISTS accepted_at BIGINT;
                ALTER TABLE queue_tickets ADD COLUMN IF NOT EXISTS served_at BIGINT;
            EXCEPTION WHEN duplicate_column THEN
                -- колонка уже существует, ничего не делаем
            END $$;
        )");

            // Миграция для first_time_tickets
            txn.exec(R"(
            DO $$
            BEGIN
                ALTER TABLE first_time_tickets ADD COLUMN IF NOT EXISTS status VARCHAR(20) DEFAULT 'waiting';
                ALTER TABLE first_time_tickets ADD COLUMN IF NOT EXISTS accepted_at BIGINT;
                ALTER TABLE first_time_tickets ADD COLUMN IF NOT EXISTS served_at BIGINT;
            EXCEPTION WHEN duplicate_column THEN
                -- колонка уже существует, ничего не делаем
            END $$;
        )");

            // ========================================================================
            // ИСПРАВЛЕННАЯ МИГРАЦИЯ для trust_acceptances (безопасное создание ограничений)
            // ========================================================================
            txn.exec(R"(
            DO $$
            BEGIN
                -- Добавляем колонку ticket_number, если её нет
                ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS ticket_number VARCHAR(20);
    
                -- Заполняем уникальными значениями для существующих записей (если NULL)
                UPDATE trust_acceptances SET ticket_number = 'TR-' || id::text WHERE ticket_number IS NULL;
    
                -- Делаем колонку NOT NULL
                ALTER TABLE trust_acceptances ALTER COLUMN ticket_number SET NOT NULL;
    
                -- Добавляем UNIQUE ограничение, ТОЛЬКО ЕСЛИ ЕГО НЕТ
                IF NOT EXISTS (
                    SELECT 1 FROM pg_constraint 
                    WHERE conname = 'trust_acceptances_ticket_number_unique' 
                      AND conrelid = 'trust_acceptances'::regclass
                ) THEN
                    ALTER TABLE trust_acceptances ADD CONSTRAINT trust_acceptances_ticket_number_unique UNIQUE (ticket_number);
                END IF;
    
                -- Остальные колонки (добавляем, если отсутствуют)
                ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS window_number VARCHAR(10) DEFAULT '1';
                ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS status VARCHAR(20) DEFAULT 'pending';
                ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS accepted_at BIGINT;
                ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS served_at BIGINT;
    
            EXCEPTION 
                WHEN duplicate_column THEN
                    -- колонка уже существует (этот блок перехватывает ошибки ADD COLUMN, если вдруг не сработает IF NOT EXISTS)
                    NULL;
            END $$;
            )");

            // ========================================================================
            // МИГРАЦИЯ: добавление колонки is_blocked для блокировки комитентов
            // ========================================================================
            txn.exec(R"(
             DO $$
             BEGIN
                 ALTER TABLE clients ADD COLUMN IF NOT EXISTS is_blocked BOOLEAN DEFAULT FALSE;
             EXCEPTION WHEN duplicate_column THEN
                 NULL;
             END $$;
             )");

            // ========================================================================
            // МИГРАЦИЯ: добавление колонки worker_id для отслеживания товароведа,
            // который внёс товар (для оценки эффективности товароведов)
            // ========================================================================
            txn.exec(R"(
             DO $$
             BEGIN
                 ALTER TABLE items ADD COLUMN IF NOT EXISTS worker_id INTEGER REFERENCES clients(id);
             EXCEPTION WHEN duplicate_column THEN
                 NULL;
             END $$;
             )");

            // ========================================================================
            // МИГРАЦИЯ: добавление колонки blocked_at для фиксации времени блокировки
            // ========================================================================
            txn.exec(R"(
             DO $$
             BEGIN
                 ALTER TABLE clients ADD COLUMN IF NOT EXISTS blocked_at BIGINT;
             EXCEPTION WHEN duplicate_column THEN
                 NULL;
             END $$;
             )");

            // ========================================================================
            // МИГРАЦИЯ: добавление колонки blocked_by для фиксации кто заблокировал
            // ========================================================================
            txn.exec(R"(
             DO $$
             BEGIN
                 ALTER TABLE clients ADD COLUMN IF NOT EXISTS blocked_by INTEGER REFERENCES clients(id);
             EXCEPTION WHEN duplicate_column THEN
                 NULL;
             END $$;
             )");

            // ========================================================================
            // Индексы
            // ========================================================================
            txn.exec("CREATE INDEX IF NOT EXISTS idx_clients_phone ON clients(phone)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_first_time_status ON first_time_tickets(status)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_queue_status_type ON queue_tickets(queue_type, status)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_trust_acceptances_client ON trust_acceptances(client_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_trust_acceptances_status ON trust_acceptances(status)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_queue_tickets_status ON queue_tickets(status)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_first_time_tickets_status ON first_time_tickets(status)");
            // ========================================================================
            // ИНДЕКС для быстрого поиска заблокированных клиентов
            // ========================================================================
            txn.exec("CREATE INDEX IF NOT EXISTS idx_clients_is_blocked ON clients(is_blocked)");

            // ========================================================================
            // ИНДЕКС для быстрого поиска товаров по товароведу
            // ========================================================================
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_worker_id ON items(worker_id)");
            // ИНДЕКС для быстрого поиска товаров по статусу (low_quality)
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_status ON items(status)");
            g_serverLogger.info("Migration: idx_items_status index created");

            txn.commit();
            g_serverLogger.info("Database initialized successfully");
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Database initialization error: " + std::string(e.what()));
            std::cerr << "Database initialization error: " << e.what() << std::endl;
            return false;
        }
    }

    // ---- Получение клиента по телефону с ролью ----
    std::optional<Client> getClientByPhone(const std::string& phone) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT id, phone, last_name, first_name, middle_name, email, role, created_at FROM clients WHERE phone = $1",
                pqxx::params{ phone }
            );
            if (result.empty()) return std::nullopt;
            Client client;
            client.id = result[0]["id"].as<int>();
            client.phone = result[0]["phone"].as<std::string>();
            std::string last = result[0]["last_name"].as<std::string>();
            std::string first = result[0]["first_name"].as<std::string>();
            std::string mid = result[0]["middle_name"].is_null() ? "" : result[0]["middle_name"].as<std::string>();
            client.name = last + " " + first + (mid.empty() ? "" : " " + mid);
            client.email = result[0]["email"].is_null() ? "" : result[0]["email"].as<std::string>();
            client.role = result[0]["role"].is_null() ? "client" : result[0]["role"].as<std::string>(); // <-- ДОБАВЛЕНО
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            return client;
        }
        catch (const std::exception& e) {
            std::cerr << "getClientByPhone error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    // ---- Регистрация клиента (с ролью по умолчанию 'client') ----
    std::pair<bool, bool> registerClient(const std::string& phone, const std::string& last_name,
        const std::string& first_name, const std::string& middle_name,
        const std::string& email, int items_submitted, int items_sold,
        const std::string& role = "client") {  // <-- ДОБАВЛЕН ПАРАМЕТР role
        try {
            pqxx::work txn{ *conn_ };
            std::optional<std::string> mid_opt = middle_name.empty() ? std::nullopt : std::optional<std::string>(middle_name);
            std::optional<std::string> email_opt = email.empty() ? std::nullopt : std::optional<std::string>(email);

            auto exist = txn.exec("SELECT id FROM clients WHERE phone = $1", pqxx::params{ phone });
            if (!exist.empty()) {
                // Обновляем существующего клиента (роль не меняем)
                txn.exec(
                    "UPDATE clients SET last_name=$2, first_name=$3, middle_name=$4, "
                    "email=$5, items_submitted=$6, items_sold=$7, updated_at=EXTRACT(EPOCH FROM NOW()) "
                    "WHERE phone=$1",
                    pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
                );
                txn.commit();
                return { true, true };
            }
            // Вставляем нового клиента с ролью
            txn.exec(
                "INSERT INTO clients (phone, last_name, first_name, middle_name, email, items_submitted, items_sold, role) "
                "VALUES ($1,$2,$3,$4,$5,$6,$7,$8)",
                pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold, role }
            );
            txn.commit();
            return { true, false };
        }
        catch (const std::exception& e) {
            std::cerr << "registerClient error: " << e.what() << std::endl;
            return { false, false };
        }
    }


    bool setTOTPSecret(const std::string& phone, const std::string& secret) {
        try {
            std::string encrypted = CryptoUtils::encryptAES256CBC(secret, encryption_key_);
            pqxx::work txn{ *conn_ };
            txn.exec(
                "UPDATE clients SET totp_secret_encrypted = $1 WHERE phone = $2",
                pqxx::params{ encrypted, phone }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "setTOTPSecret error: " << e.what() << std::endl;
            return false;
        }
    }

    std::optional<std::string> getTOTPSecret(const std::string& phone) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT totp_secret_encrypted FROM clients WHERE phone = $1",
                pqxx::params{ phone }
            );
            if (!result.empty() && !result[0]["totp_secret_encrypted"].is_null()) {
                std::string encrypted = result[0]["totp_secret_encrypted"].as<std::string>();
                return CryptoUtils::decryptAES256CBC(encrypted, encryption_key_);
            }
            return std::nullopt;
        }
        catch (const std::exception& e) {
            std::cerr << "getTOTPSecret error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    bool saveAuthTokens(int client_id, const std::string& access_token_hash,
        const std::string& refresh_token_hash, int64_t expires_at) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "INSERT INTO auth_tokens (client_id, access_token_hash, refresh_token_hash, expires_at) "
                "VALUES ($1, $2, $3, $4)",
                pqxx::params{ client_id, access_token_hash, refresh_token_hash, expires_at }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "saveAuthTokens DB error: " << e.what() << std::endl;
            return false;
        }
    }

    // ---- Получение клиента по id с ролью ----
    std::optional<Client> getClientById(int id) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT id, phone, last_name, first_name, middle_name, email, role, created_at FROM clients WHERE id = $1",
                pqxx::params{ id }
            );
            if (result.empty()) return std::nullopt;
            Client client;
            client.id = result[0]["id"].as<int>();
            client.phone = result[0]["phone"].as<std::string>();
            std::string last = result[0]["last_name"].as<std::string>();
            std::string first = result[0]["first_name"].as<std::string>();
            std::string mid = result[0]["middle_name"].is_null() ? "" : result[0]["middle_name"].as<std::string>();
            client.name = last + " " + first + (mid.empty() ? "" : " " + mid);
            client.email = result[0]["email"].is_null() ? "" : result[0]["email"].as<std::string>();
            client.role = result[0]["role"].is_null() ? "client" : result[0]["role"].as<std::string>(); // <-- ДОБАВЛЕНО
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            return client;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientById error: " + std::string(e.what()));
            return std::nullopt;
        }
    }


    int getDailyTicketCount(const std::string& queueType) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets "
                "WHERE queue_type = $1 AND created_at >= EXTRACT(EPOCH FROM date_trunc('day', NOW() AT TIME ZONE 'UTC'))",
                pqxx::params{ queueType }
            );
            return result[0][0].as<int>();
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getDailyTicketCount error: " + std::string(e.what()));
            return 0;
        }
    }

    QueueTicket createTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket;
        try {
            pqxx::work txn{ *conn_ };
            std::string prefix = (queueType == "general") ? "G" :
                (queueType == "first_time") ? "F" :
                (queueType == "extra_20") ? "E" :
                (queueType == "paid") ? "P" :
                (queueType == "expensive") ? "D" : "X";

            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );
            int position = countResult[0][0].as<int>() + 1;
            std::string number = prefix + "-" + std::to_string(time(nullptr) % 10000) + "-" + std::to_string(position);
            std::string window = "1";
            int waitTime = position * 5;

            auto result = txn.exec(
                "INSERT INTO queue_tickets (number, client_id, queue_type, position, items_count, window_number, estimated_wait_time) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                "RETURNING id, number, position, window_number, estimated_wait_time, created_at",
                pqxx::params{ number, clientId, queueType, position, itemsCount, window, waitTime }
            );
            ticket.id = result[0]["id"].as<int>();
            ticket.number = result[0]["number"].as<std::string>();
            ticket.clientId = clientId;
            ticket.queueType = queueType;
            ticket.position = result[0]["position"].as<int>();
            ticket.itemsCount = itemsCount;
            ticket.windowNumber = result[0]["window_number"].as<std::string>();
            ticket.estimatedWaitTime = result[0]["estimated_wait_time"].as<int>();
            ticket.createdAt = result[0]["created_at"].as<int64_t>();
            ticket.status = "waiting";
            txn.commit();
        }
        catch (const std::exception& e) {
            std::cerr << "createTicket error: " << e.what() << std::endl;
        }
        return ticket;
    }
    // Обработчики очереди "На доверии"
    // ===== ДОПОЛНЕНИЯ ДЛЯ ОЧЕРЕДИ trust =====

    // Получить список ожидающих талонов trust
    std::vector<json> getWaitingTrustTickets() {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, ticket_number, client_id, window_number, created_at "
                "FROM trust_acceptances WHERE status = 'pending' ORDER BY created_at"
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["ticket_number"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getWaitingTrustTickets: " + std::to_string(result.size()) + " tickets");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getWaitingTrustTickets error: " + std::string(e.what()));
        }
        return result;
    }

    // Принять талон trust
    bool acceptTrustTicket(const std::string& ticketNumber, std::string& windowNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE trust_acceptances SET status = 'accepted', accepted_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE ticket_number = $1 AND status = 'pending' RETURNING window_number",
                pqxx::params{ ticketNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptTrustTicket: ticket not found or already accepted: " + ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("Accepted TRUST ticket: " + ticketNumber + ", window: " + windowNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("acceptTrustTicket error: " + std::string(e.what()));
            return false;
        }
    }

    // Обслужить талон trust
    bool serveTrustTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE trust_acceptances SET status = 'served', served_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE ticket_number = $1 AND status = 'accepted'",
                pqxx::params{ ticketNumber }
            );
            if (res.affected_rows() == 0) {
                g_serverLogger.warning("serveTrustTicket: ticket not accepted or already served: " + ticketNumber);
                return false;
            }
            txn.commit();
            g_serverLogger.info("Served TRUST ticket: " + ticketNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("serveTrustTicket error: " + std::string(e.what()));
            return false;
        }
    }

    std::optional<std::string> createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            auto now = std::chrono::system_clock::now();
            auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::string ticketNumber = "TR-" + std::to_string(ts) + "-" +
                std::to_string(rand() % 10000);
            g_serverLogger.info("Creating TRUST acceptance ticket: " + ticketNumber + " for client " + std::to_string(clientId));

            auto result = txn.exec(
                "INSERT INTO trust_acceptances (client_id, ticket_number) "
                "VALUES ($1, $2) RETURNING ticket_number",
                pqxx::params{ clientId, ticketNumber }
            );
            g_serverLogger.info("Inserted TRUST acceptance ticket into database: " + ticketNumber + " for client " + std::to_string(clientId));
            txn.commit();
            g_serverLogger.info("Created TRUST acceptance ticket: " + ticketNumber + " for client " + std::to_string(clientId));
            return result[0]["ticket_number"].as<std::string>();
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createTrustAcceptance error: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::optional<json> getTrustTicketInfo(const std::string& ticketNumber) {
        try {
            g_serverLogger.info("Get Trust Ticket info");
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT ticket_number, window_number, created_at FROM trust_acceptances WHERE ticket_number = $1",
                pqxx::params{ ticketNumber }
            );
            g_serverLogger.info("Ticket number:" + ticketNumber);
            if (res.empty()) return std::nullopt;
            json info;
            info["ticket_number"] = res[0]["ticket_number"].as<std::string>();
            info["window_number"] = res[0]["window_number"].as<std::string>();
            info["created_at"] = res[0]["created_at"].as<int64_t>();
            return info;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getTrustTicketInfo error: " + std::string(e.what()));
            return std::nullopt;
        }
    }

    std::string createFirstTimeTicket(const std::string& windowNumber = "1") {
        try {
            pqxx::work txn{ *conn_ };
            auto now = std::chrono::system_clock::now();
            auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::string ticketNumber = "F-" + std::to_string(ts) + "-" +
                std::to_string(rand() % 10000);

            auto result = txn.exec(
                "INSERT INTO first_time_tickets (ticket_number, window_number) "
                "VALUES ($1, $2) RETURNING id",
                pqxx::params{ ticketNumber, windowNumber }
            );
            txn.commit();
            g_serverLogger.info("Created FIRST_TIME ticket: " + ticketNumber);
            return ticketNumber;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createFirstTimeTicket error: " + std::string(e.what()));
            return "";
        }
    }

    std::vector<json> getWaitingFirstTimeTickets() {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, ticket_number, window_number, created_at "
                "FROM first_time_tickets WHERE status = 'waiting' ORDER BY created_at"
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["ticket_number"].as<std::string>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                result.push_back(item);
            }
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getWaitingFirstTimeTickets error: " + std::string(e.what()));
        }
        return result;
    }

    bool acceptFirstTimeTicket(const std::string& ticketNumber, std::string& windowNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE first_time_tickets SET status = 'accepted', accepted_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE ticket_number = $1 AND status = 'waiting' "
                "RETURNING window_number",
                pqxx::params{ ticketNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptFirstTimeTicket: ticket not found or already accepted: " + ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("Accepted FIRST_TIME ticket: " + ticketNumber + ", window: " + windowNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("acceptFirstTimeTicket error: " + std::string(e.what()));
            return false;
        }
    }

    bool serveFirstTimeTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE first_time_tickets SET status = 'served', served_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE ticket_number = $1 AND status = 'accepted'",
                pqxx::params{ ticketNumber }
            );
            if (res.affected_rows() == 0) {
                g_serverLogger.warning("serveFirstTimeTicket: ticket not accepted or already served: " + ticketNumber);
                return false;
            }
            txn.commit();
            g_serverLogger.info("Served FIRST_TIME ticket: " + ticketNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("serveFirstTimeTicket error: " + std::string(e.what()));
            return false;
        }
    }

    bool serveTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "UPDATE queue_tickets SET status = 'served', served_at = EXTRACT(EPOCH FROM NOW()) WHERE number = $1",
                pqxx::params{ ticketNumber }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "serveTicket error: " << e.what() << std::endl;
            return false;
        }
    }

    json getQueueStatus(const std::string& queueType) {
        json status;
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT COUNT(*) as total, MIN(position) as min_pos FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );
            status["total_waiting"] = result[0]["total"].as<int>();
            status["min_position"] = result[0]["min_pos"].is_null() ? 0 : result[0]["min_pos"].as<int>();
        }
        catch (const std::exception& e) {
            std::cerr << "getQueueStatus error: " << e.what() << std::endl;
        }
        return status;
    }

    std::vector<json> getUnsyncedItems() {
        std::vector<json> items;
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT i.id, i.client_id, i.description, i.estimated_price, "
                "c.phone, c.last_name, c.first_name, c.middle_name "
                "FROM items i "
                "JOIN clients c ON i.client_id = c.id "
                "WHERE i.synced_to_1c = FALSE"
            );
            for (const auto& row : result) {
                json item;
                item["id"] = row["id"].as<int>();
                item["client_id"] = row["client_id"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["client_phone"] = row["phone"].as<std::string>();
                std::string last = row["last_name"].as<std::string>();
                std::string first = row["first_name"].as<std::string>();
                std::string mid = row["middle_name"].is_null() ? "" : row["middle_name"].as<std::string>();
                item["client_name"] = last + " " + first + (mid.empty() ? "" : " " + mid);
                items.push_back(item);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "getUnsyncedItems error: " << e.what() << std::endl;
        }
        return items;
    }

    bool updateItemSyncStatus(int itemId, bool synced) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec("UPDATE items SET synced_to_1c = $1 WHERE id = $2", pqxx::params{ synced, itemId });
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "updateItemSyncStatus error: " << e.what() << std::endl;
            return false;
        }
    }

    void logSync(const std::string& syncType, int recordsCount, const std::string& status, const std::string& errorMessage) {
        try {
            pqxx::work txn{ *conn_ };
            std::optional<std::string> err_opt = errorMessage.empty() ? std::nullopt : std::optional<std::string>(errorMessage);
            txn.exec(
                "INSERT INTO sync_log (sync_type, records_count, status, error_message) VALUES ($1, $2, $3, $4)",
                pqxx::params{ syncType, recordsCount, status, err_opt }
            );
            txn.commit();
        }
        catch (const std::exception& e) {
            std::cerr << "logSync error: " << e.what() << std::endl;
        }
    }

    std::vector<json> getWaitingTickets(const std::string& queueType) {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, number, client_id, queue_type, position, items_count, window_number, estimated_wait_time, created_at "
                "FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting' ORDER BY created_at",
                pqxx::params{ queueType }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["number"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["queue_type"] = row["queue_type"].as<std::string>();
                item["position"] = row["position"].as<int>();
                item["items_count"] = row["items_count"].as<int>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["estimated_wait_time"] = row["estimated_wait_time"].as<int>();
                item["created_at"] = row["created_at"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getWaitingTickets for " + queueType + ": " + std::to_string(result.size()) + " tickets");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getWaitingTickets error: " + std::string(e.what()));
        }
        return result;
    }

    bool acceptTicket(const std::string& ticketNumber, std::string& windowNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE queue_tickets SET status = 'accepted', accepted_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE number = $1 AND status = 'waiting' RETURNING window_number",
                pqxx::params{ ticketNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptTicket: ticket not found or already accepted: " + ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("Accepted ticket: " + ticketNumber + ", window: " + windowNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("acceptTicket error: " + std::string(e.what()));
            return false;
        }
    }
    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ РАБОТЫ С ТОВАРАМИ КЛИЕНТА (ОБЩАЯ ОЧЕРЕДЬ)
    // =========================================================================

    /**
     * Получить список всех товаров клиента (включая непроданные)
     * @param clientId ID клиента
     * @return вектор JSON-объектов с полями: id, item_number, description,
     *         estimated_price, quantity, sale_date, condition, note, created_at, status
     */
    std::vector<json> getClientItems(int clientId) {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, item_number, description, estimated_price, quantity, "
                "sale_date, condition, note, created_at, status "
                "FROM items WHERE client_id = $1 ORDER BY item_number",
                pqxx::params{ clientId }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                if (!row["sale_date"].is_null())
                    item["sale_date"] = row["sale_date"].as<int64_t>();
                else
                    item["sale_date"] = nullptr;
                item["condition"] = row["condition"].is_null() ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null() ? "" : row["note"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["status"] = row["status"].is_null() ? "" : row["status"].as<std::string>();
                result.push_back(item);
            }
            g_serverLogger.info("getClientItems: " + std::to_string(result.size()) +
                " items for client " + std::to_string(clientId));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientItems error: " + std::string(e.what()));
        }
        return result;
    }

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getNextItemNumber
    // =========================================================================
    // ПРИЧИНА ОШИБКИ: Метод создавал собственную pqxx::work (транзакцию),
    // но вызывался из addItemsBatch, где уже была открыта транзакция на том
    // же соединении conn_. pqxx не позволяет иметь две активные pqxx::work
    // на одном соединении одновременно.
    //
    // РЕШЕНИЕ: Метод принимает ссылку на pqxx::transaction_base и выполняет
    // запрос в рамках уже существующей транзакции, не создавая новую.
    // =========================================================================
    /**
     * Получить следующий порядковый номер товара для клиента (начиная с 1).
     * Выполняется в рамках переданной транзакции txn.
     * @param clientId ID клиента
     * @param txn активная транзакция, в которой выполняется запрос
     * @return следующий номер товара
     */
    int getNextItemNumber(int clientId, pqxx::transaction_base& txn) {
        try {
            auto res = txn.exec(
                "SELECT COALESCE(MAX(item_number), 0) + 1 FROM items WHERE client_id = $1",
                pqxx::params{ clientId }
            );
            int nextNum = res[0][0].as<int>();
            g_serverLogger.info("getNextItemNumber: clientId=" + std::to_string(clientId) +
                ", nextNumber=" + std::to_string(nextNum));
            return nextNum;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getNextItemNumber error: " + std::string(e.what()));
            return 1;
        }
    }

    /**
     * Добавить один товар (используется внутри пакетного метода)
     * ИСПРАВЛЕНИЕ: Метод также принимает ссылку на транзакцию.
     */
    bool addItem(int clientId, int itemNumber, const std::string& description,
        double price, int quantity, const std::string& condition,
        const std::string& note, pqxx::transaction_base& txn) {
        try {
            txn.exec(
                "INSERT INTO items (client_id, item_number, description, estimated_price, "
                "quantity, condition, note) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7)",
                pqxx::params{ clientId, itemNumber, description, price, quantity,
                              condition.empty() ? std::optional<std::string>{} : condition,
                              note.empty() ? std::optional<std::string>{} : note }
            );
            g_serverLogger.info("Added item " + std::to_string(itemNumber) +
                " for client " + std::to_string(clientId));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("addItem error: " + std::string(e.what()));
            return false;
        }
    }

    /**
  * Пакетное добавление нескольких товаров для одного клиента.
  *
  * ИСПРАВЛЕНИЕ E0140: Добавлен параметр workerId (по умолчанию 0).
  * Теперь сигнатура: addItemsBatch(clientId, items, workerId)
  *
  * workerId — ID товароведа, который вносит товары.
  * Сохраняется для ВСЕХ очередей (общая, +20, доверие, платный, дорогой),
  * т.к. вызов идёт через единый эндпоинт /api/v1/items/batch.
  *
  * @param clientId ID клиента (комитента)
  * @param items массив JSON-объектов с полями: description, estimated_price,
  *              quantity, condition, note
  * @param workerId ID товароведа, который вносит товары (0 = не указан)
  * @return true если все добавлены успешно
  */
    bool addItemsBatch(int clientId, const json& items, int workerId = 0) {
        if (!items.is_array() || items.empty()) {
            g_serverLogger.warning("addItemsBatch: empty or invalid items array");
            return false;
        }
        try {
            pqxx::work txn{ *conn_ };
            // getNextItemNumber выполняется в рамках текущей транзакции txn
            int nextNumber = getNextItemNumber(clientId, txn);

            g_serverLogger.info("addItemsBatch: clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(items.size()) +
                ", startingNumber=" + std::to_string(nextNumber) +
                ", workerId=" + std::to_string(workerId));

            for (const auto& item : items) {
                std::string desc = item.value("description", "");
                double price = item.value("estimated_price", 0.0);
                int quantity = item.value("quantity", 1);
                std::string condition = item.value("condition", "");
                std::string note = item.value("note", "");

                // =====================================================================
                // Вставляем товар с worker_id для отслеживания эффективности
                // товароведа. Работает для ВСЕХ типов очередей.
                // =====================================================================
                if (workerId > 0) {
                    txn.exec(
                        "INSERT INTO items (client_id, item_number, description, estimated_price, "
                        "quantity, condition, note, worker_id) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
                        pqxx::params{ clientId, nextNumber++, desc, price, quantity,
                                      condition.empty() ? std::optional<std::string>{} : condition,
                                      note.empty() ? std::optional<std::string>{} : note,
                                      workerId }
                    );
                }
                else {
                    // Если workerId не указан (для обратной совместимости)
                    txn.exec(
                        "INSERT INTO items (client_id, item_number, description, estimated_price, "
                        "quantity, condition, note) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
                        pqxx::params{ clientId, nextNumber++, desc, price, quantity,
                                      condition.empty() ? std::optional<std::string>{} : condition,
                                      note.empty() ? std::optional<std::string>{} : note }
                    );
                }
            }

            txn.commit();

            g_serverLogger.info("addItemsBatch: successfully added " +
                std::to_string(items.size()) + " items for client " +
                std::to_string(clientId) + " by worker " + std::to_string(workerId));

            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("addItemsBatch error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ ПАНЕЛИ ДИРЕКТОРА
    
    // =========================================================================

    /**
     * @brief Проверяет, заблокирован ли клиент по ID
     * @param clientId ID клиента
     * @return true если клиент заблокирован
     */
    bool isClientBlocked(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT is_blocked FROM clients WHERE id = $1",
                pqxx::params{ clientId }
            );
            if (result.empty()) return false;
            bool blocked = result[0]["is_blocked"].as<bool>();
            g_serverLogger.info("isClientBlocked: clientId=" + std::to_string(clientId) +
                ", blocked=" + std::to_string(blocked));
            return blocked;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("isClientBlocked error: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * @brief Проверяет, заблокирован ли клиент по телефону
     * @param phone Телефон клиента
     * @return true если клиент заблокирован
     */
    bool isClientBlockedByPhone(const std::string& phone) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT is_blocked FROM clients WHERE phone = $1",
                pqxx::params{ phone }
            );
            if (result.empty()) return false;
            bool blocked = result[0]["is_blocked"].as<bool>();
            g_serverLogger.info("isClientBlockedByPhone: phone=" + phone +
                ", blocked=" + std::to_string(blocked));
            return blocked;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("isClientBlockedByPhone error: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * @brief Блокирует или разблокирует клиента
     * @param clientId ID клиента для блокировки
     * @param blocked true - заблокировать, false - разблокировать
     * @param directorId ID директора, выполняющего блокировку
     * @return true при успешном выполнении
     */
    bool setClientBlocked(int clientId, bool blocked, int directorId) {
        try {
            pqxx::work txn{ *conn_ };

            if (blocked) {
                // Блокировка: устанавливаем is_blocked = TRUE, blocked_at, blocked_by
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                txn.exec(
                    "UPDATE clients SET is_blocked = TRUE, blocked_at = $2, blocked_by = $3, "
                    "updated_at = EXTRACT(EPOCH FROM NOW()) WHERE id = $1",
                    pqxx::params{ clientId, now, directorId }
                );
                g_serverLogger.info("setClientBlocked: BLOCKED clientId=" + std::to_string(clientId) +
                    " by directorId=" + std::to_string(directorId));
            }
            else {
                // Разблокировка: устанавливаем is_blocked = FALSE, очищаем blocked_at, blocked_by
                txn.exec(
                    "UPDATE clients SET is_blocked = FALSE, blocked_at = NULL, blocked_by = NULL, "
                    "updated_at = EXTRACT(EPOCH FROM NOW()) WHERE id = $1",
                    pqxx::params{ clientId }
                );
                g_serverLogger.info("setClientBlocked: UNBLOCKED clientId=" + std::to_string(clientId) +
                    " by directorId=" + std::to_string(directorId));
            }

            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("setClientBlocked error: " + std::string(e.what()));
            return false;
        }
    }

    /**
     * @brief Получает полную статистику для панели директора
     * Включает: данные о комитентах, эффективность товароведов, не проданные товары
     * @return JSON объект со статистикой
     */
    json getDirectorStats() {
        json result;
        try {
            pqxx::work txn{ *conn_ };

            // =====================================================================
            // 1. СТАТИСТИКА ПО ВСЕМ КОМИТЕНТАМ
            // Для каждого комитента: сколько всего сдал товаров, на какую сумму
            // =====================================================================
            json clientsArray = json::array();
            auto clientsResult = txn.exec(
                "SELECT c.id, c.phone, c.last_name, c.first_name, c.middle_name, "
                "c.email, c.items_submitted, c.items_sold, c.is_blocked, c.blocked_at, "
                "COALESCE(SUM(i.quantity), 0) as total_items_count, "
                "COALESCE(SUM(i.estimated_price * i.quantity), 0) as total_items_value, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.quantity ELSE 0 END), 0) as sold_items_count, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as sold_items_value, "
                "COALESCE(SUM(CASE WHEN i.status = 'low_quality' OR i.status = 'unsold_quality' THEN i.quantity ELSE 0 END), 0) as low_quality_count, "
                "COALESCE(SUM(CASE WHEN i.status = 'low_quality' OR i.status = 'unsold_quality' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as low_quality_value "
                "FROM clients c "
                "LEFT JOIN items i ON c.id = i.client_id "
                "WHERE c.role = 'client' OR c.role IS NULL "
                "GROUP BY c.id, c.phone, c.last_name, c.first_name, c.middle_name, "
                "c.email, c.items_submitted, c.items_sold, c.is_blocked, c.blocked_at "
                "ORDER BY c.last_name, c.first_name"
            );

            for (const auto& row : clientsResult) {
                json client;
                client["id"] = row["id"].as<int>();
                client["phone"] = row["phone"].as<std::string>();

                std::string last = row["last_name"].as<std::string>();
                std::string first = row["first_name"].as<std::string>();
                std::string mid = row["middle_name"].is_null() ? "" : row["middle_name"].as<std::string>();
                client["full_name"] = last + " " + first + (mid.empty() ? "" : " " + mid);

                client["email"] = row["email"].is_null() ? "" : row["email"].as<std::string>();
                client["items_submitted"] = row["items_submitted"].as<int>();
                client["items_sold"] = row["items_sold"].as<int>();
                client["is_blocked"] = row["is_blocked"].as<bool>();

                if (!row["blocked_at"].is_null()) {
                    client["blocked_at"] = row["blocked_at"].as<int64_t>();
                }
                else {
                    client["blocked_at"] = nullptr;
                }

                client["total_items_count"] = row["total_items_count"].as<int>();
                client["total_items_value"] = row["total_items_value"].as<double>();
                client["sold_items_count"] = row["sold_items_count"].as<int>();
                client["sold_items_value"] = row["sold_items_value"].as<double>();
                client["low_quality_count"] = row["low_quality_count"].as<int>();
                client["low_quality_value"] = row["low_quality_value"].as<double>();

                // Вычисляем количество не проданных товаров (не продано из-за низкого качества)
                int totalItems = row["total_items_count"].as<int>();
                int soldItems = row["sold_items_count"].as<int>();
                int lowQualityItems = row["low_quality_count"].as<int>();
                client["unsold_count"] = totalItems - soldItems - lowQualityItems;

                clientsArray.push_back(client);

                g_serverLogger.info("getDirectorStats: client id=" + std::to_string(row["id"].as<int>()) +
                    ", phone=" + row["phone"].as<std::string>() +
                    ", total_items=" + std::to_string(row["total_items_count"].as<int>()) +
                    ", total_value=" + std::to_string(row["total_items_value"].as<double>()) +
                    ", sold_value=" + std::to_string(row["sold_items_value"].as<double>()) +
                    ", low_quality=" + std::to_string(row["low_quality_count"].as<int>()));
            }
            result["clients"] = clientsArray;

            // =====================================================================
            // 2. ЭФФЕКТИВНОСТЬ ТОВАРОВЕДОВ
            // Для каждого товароведа: на какую сумму вносит товары в БД,
            // на какую сумму продано товаров
            // =====================================================================
            json workersArray = json::array();
            auto workersResult = txn.exec(
                "SELECT w.id, w.phone, w.last_name, w.first_name, w.middle_name, "
                "w.email, "
                "COALESCE(SUM(i.estimated_price * i.quantity), 0) as total_entered_value, "
                "COALESCE(COUNT(i.id), 0) as total_entered_count, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as total_sold_value, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.quantity ELSE 0 END), 0) as total_sold_count "
                "FROM clients w "
                "LEFT JOIN items i ON w.id = i.worker_id "
                "WHERE w.role = 'worker' "
                "GROUP BY w.id, w.phone, w.last_name, w.first_name, w.middle_name, w.email "
                "ORDER BY total_entered_value DESC"
            );

            for (const auto& row : workersResult) {
                json worker;
                worker["id"] = row["id"].as<int>();
                worker["phone"] = row["phone"].as<std::string>();

                std::string last = row["last_name"].as<std::string>();
                std::string first = row["first_name"].as<std::string>();
                std::string mid = row["middle_name"].is_null() ? "" : row["middle_name"].as<std::string>();
                worker["full_name"] = last + " " + first + (mid.empty() ? "" : " " + mid);

                worker["email"] = row["email"].is_null() ? "" : row["email"].as<std::string>();
                worker["total_entered_value"] = row["total_entered_value"].as<double>();
                worker["total_entered_count"] = row["total_entered_count"].as<int>();
                worker["total_sold_value"] = row["total_sold_value"].as<double>();
                worker["total_sold_count"] = row["total_sold_count"].as<int>();

                // Вычисляем эффективность (процент проданного)
                double enteredValue = row["total_entered_value"].as<double>();
                double soldValue = row["total_sold_value"].as<double>();
                double efficiency = (enteredValue > 0) ? (soldValue / enteredValue * 100.0) : 0.0;
                worker["efficiency_percent"] = efficiency;

                workersArray.push_back(worker);

                g_serverLogger.info("getDirectorStats: worker id=" + std::to_string(row["id"].as<int>()) +
                    ", phone=" + row["phone"].as<std::string>() +
                    ", entered_value=" + std::to_string(row["total_entered_value"].as<double>()) +
                    ", sold_value=" + std::to_string(row["total_sold_value"].as<double>()) +
                    ", efficiency=" + std::to_string(efficiency) + "%");
            }
            result["workers"] = workersArray;

            // =====================================================================
            // 3. ТОВАРЫ НЕ ПРОДАННЫЕ ИЗ-ЗА НИЗКОГО КАЧЕСТВА
            // =====================================================================
            json lowQualityArray = json::array();
            auto lowQualityResult = txn.exec(
                "SELECT i.id, i.item_number, i.description, i.estimated_price, "
                "i.quantity, i.condition, i.note, i.created_at, i.status, "
                "c.id as client_id, c.phone as client_phone, "
                "c.last_name || ' ' || c.first_name as client_name, "
                "w.id as worker_id, w.last_name || ' ' || w.first_name as worker_name "
                "FROM items i "
                "JOIN clients c ON i.client_id = c.id "
                "LEFT JOIN clients w ON i.worker_id = w.id "
                "WHERE i.status = 'low_quality' OR i.status = 'unsold_quality' "
                "ORDER BY i.created_at DESC"
            );

            for (const auto& row : lowQualityResult) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                item["condition"] = row["condition"].is_null() ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null() ? "" : row["note"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["status"] = row["status"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["client_phone"] = row["client_phone"].as<std::string>();
                item["client_name"] = row["client_name"].as<std::string>();
                item["worker_name"] = row["worker_name"].is_null() ? "" : row["worker_name"].as<std::string>();

                lowQualityArray.push_back(item);

                g_serverLogger.info("getDirectorStats: low_quality item id=" +
                    std::to_string(row["id"].as<int>()) +
                    ", desc=" + row["description"].as<std::string>() +
                    ", price=" + std::to_string(row["estimated_price"].as<double>()));
            }
            result["low_quality_items"] = lowQualityArray;

            // =====================================================================
            // 4. ОБЩАЯ СВОДКА
            // =====================================================================
            json summary;

            // Общие суммы по всем комитентам
            auto summaryResult = txn.exec(
                "SELECT "
                "COALESCE(SUM(i.quantity), 0) as total_items, "
                "COALESCE(SUM(i.estimated_price * i.quantity), 0) as total_value, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.quantity ELSE 0 END), 0) as total_sold, "
                "COALESCE(SUM(CASE WHEN i.status = 'sold' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as total_sold_value, "
                "COALESCE(SUM(CASE WHEN i.status = 'low_quality' OR i.status = 'unsold_quality' THEN i.quantity ELSE 0 END), 0) as total_low_quality, "
                "COALESCE(SUM(CASE WHEN i.status = 'low_quality' OR i.status = 'unsold_quality' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as total_low_quality_value, "
                "COUNT(DISTINCT c.id) as total_clients, "
                "COUNT(DISTINCT CASE WHEN c.is_blocked = TRUE THEN c.id END) as blocked_clients_count "
                "FROM items i "
                "LEFT JOIN clients c ON i.client_id = c.id"
            );

            if (!summaryResult.empty()) {
                summary["total_items"] = summaryResult[0]["total_items"].as<int>();
                summary["total_value"] = summaryResult[0]["total_value"].as<double>();
                summary["total_sold"] = summaryResult[0]["total_sold"].as<int>();
                summary["total_sold_value"] = summaryResult[0]["total_sold_value"].as<double>();
                summary["total_low_quality"] = summaryResult[0]["total_low_quality"].as<int>();
                summary["total_low_quality_value"] = summaryResult[0]["total_low_quality_value"].as<double>();
                summary["total_clients"] = summaryResult[0]["total_clients"].as<int>();
                summary["blocked_clients_count"] = summaryResult[0]["blocked_clients_count"].as<int>();

                g_serverLogger.info("getDirectorStats: SUMMARY - total_items=" +
                    std::to_string(summary["total_items"].get<int>()) +
                    ", total_value=" + std::to_string(summary["total_value"].get<double>()) +
                    ", total_sold=" + std::to_string(summary["total_sold"].get<int>()) +
                    ", total_sold_value=" + std::to_string(summary["total_sold_value"].get<double>()) +
                    ", low_quality=" + std::to_string(summary["total_low_quality"].get<int>()));
            }
            result["summary"] = summary;

            g_serverLogger.info("getDirectorStats: completed successfully, clients=" +
                std::to_string(clientsArray.size()) +
                ", workers=" + std::to_string(workersArray.size()) +
                ", low_quality_items=" + std::to_string(lowQualityArray.size()));

        }
        catch (const std::exception& e) {
            g_serverLogger.error("getDirectorStats error: " + std::string(e.what()));
            result["error"] = e.what();
        }

        return result;
    }

    /**
     * @brief Получает список всех клиентов с возможностью фильтрации
     * @param includeBlocked включать заблокированных клиентов
     * @return JSON массив клиентов
     */
    json getAllClients(bool includeBlocked = true) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };

            std::string query =
                "SELECT id, phone, last_name, first_name, middle_name, email, "
                "role, is_blocked, blocked_at, items_submitted, items_sold, created_at "
                "FROM clients WHERE (role = 'client' OR role IS NULL)";

            if (!includeBlocked) {
                query += " AND is_blocked = FALSE";
            }

            query += " ORDER BY last_name, first_name";

            auto res = txn.exec(query);

            for (const auto& row : res) {
                json client;
                client["id"] = row["id"].as<int>();
                client["phone"] = row["phone"].as<std::string>();

                std::string last = row["last_name"].as<std::string>();
                std::string first = row["first_name"].as<std::string>();
                std::string mid = row["middle_name"].is_null() ? "" : row["middle_name"].as<std::string>();
                client["full_name"] = last + " " + first + (mid.empty() ? "" : " " + mid);

                client["email"] = row["email"].is_null() ? "" : row["email"].as<std::string>();
                client["role"] = row["role"].is_null() ? "client" : row["role"].as<std::string>();
                client["is_blocked"] = row["is_blocked"].as<bool>();
                client["items_submitted"] = row["items_submitted"].as<int>();
                client["items_sold"] = row["items_sold"].as<int>();
                client["created_at"] = row["created_at"].as<int64_t>();

                if (!row["blocked_at"].is_null()) {
                    client["blocked_at"] = row["blocked_at"].as<int64_t>();
                }
                else {
                    client["blocked_at"] = nullptr;
                }

                result.push_back(client);
            }

            g_serverLogger.info("getAllClients: returned " + std::to_string(result.size()) +
                " clients, includeBlocked=" + std::to_string(includeBlocked));

        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAllClients error: " + std::string(e.what()));
        }

        return result;
    }

};
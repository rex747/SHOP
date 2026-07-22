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
                );
                CREATE TABLE IF NOT EXISTS auth_tokens (
                    id SERIAL PRIMARY KEY,
                    client_id INTEGER REFERENCES clients(id),
                    access_token_hash VARCHAR(256) NOT NULL,
                    refresh_token_hash VARCHAR(256) NOT NULL,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    expires_at INTEGER NOT NULL,
                    revoked BOOLEAN DEFAULT FALSE
                );
                -- ✅ НОВАЯ ТАБЛИЦА ДЛЯ EMAIL OTP
                CREATE TABLE IF NOT EXISTS email_otp (
                    id SERIAL PRIMARY KEY,
                    phone VARCHAR(20) NOT NULL,
                    code_hash VARCHAR(256) NOT NULL,
                    expires_at BIGINT NOT NULL,   -- <-- ИСПРАВЛЕНИЕ: BIGINT вместо INTEGER
                    used BOOLEAN DEFAULT FALSE,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW())
                );
                CREATE INDEX IF NOT EXISTS idx_clients_phone ON clients(phone);
                CREATE INDEX IF NOT EXISTS idx_email_otp_phone ON email_otp(phone);
            )");
            txn.commit();
            g_serverLogger.info("Database initialized successfully (email_otp.expires_at is BIGINT)");
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Database initialization error: " + std::string(e.what()));
            std::cerr << "Database initialization error: " << e.what() << std::endl;
            return false;
        }
    }

    std::optional<Client> getClientByPhone(const std::string& phone) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT id, phone, last_name, first_name, middle_name, email, created_at FROM clients WHERE phone = $1",
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
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            return client;
        }
        catch (const std::exception& e) {
            std::cerr << "getClientByPhone error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

    std::pair<bool, bool> registerClient(const std::string& phone, const std::string& last_name,
        const std::string& first_name, const std::string& middle_name,
        const std::string& email, int items_submitted, int items_sold) {
        try {
            pqxx::work txn{ *conn_ };
            std::optional<std::string> mid_opt = middle_name.empty() ? std::nullopt : std::optional<std::string>(middle_name);
            std::optional<std::string> email_opt = email.empty() ? std::nullopt : std::optional<std::string>(email);

            auto exist = txn.exec("SELECT id FROM clients WHERE phone = $1", pqxx::params{ phone });
            if (!exist.empty()) {
                txn.exec(
                    "UPDATE clients SET last_name=$2, first_name=$3, middle_name=$4, "
                    "email=$5, items_submitted=$6, items_sold=$7, updated_at=EXTRACT(EPOCH FROM NOW()) "
                    "WHERE phone=$1",
                    pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
                );
                txn.commit();
                return { true, true }; // Успех, уже существует
            }
            txn.exec(
                "INSERT INTO clients (phone, last_name, first_name, middle_name, email, items_submitted, items_sold) "
                "VALUES ($1,$2,$3,$4,$5,$6,$7)",
                pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
            );
            txn.commit();
            return { true, false }; // Успех, новый пользователь
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
    
    bool saveEmailOTP(const std::string& phone, const std::string& code_hash, int64_t expires_at) {
        try {
            pqxx::work txn{ *conn_ };
            // Сначала аннулируем старые неиспользованные коды для этого телефона
            txn.exec("UPDATE email_otp SET used = TRUE WHERE phone = $1 AND used = FALSE", pqxx::params{ phone });
            txn.exec(
                "INSERT INTO email_otp (phone, code_hash, expires_at) VALUES ($1, $2, $3)",
                pqxx::params{ phone, code_hash, expires_at }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "saveEmailOTP error: " << e.what() << std::endl;
            return false;
        }
    }

    bool verifyAndConsumeEmailOTP(const std::string& phone, const std::string& code_hash) {
        try {
            pqxx::work txn{ *conn_ };
            int64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000;
            auto result = txn.exec(
                "SELECT id FROM email_otp WHERE phone = $1 AND code_hash = $2 AND expires_at > $3 AND used = FALSE",
                pqxx::params{ phone, code_hash, now }
            );
            if (result.empty()) {
                return false; // Код неверный, истек или уже использован
            }
            // Помечаем как использованный
            txn.exec("UPDATE email_otp SET used = TRUE WHERE id = $1", pqxx::params{ result[0]["id"].as<int>() });
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "verifyAndConsumeEmailOTP error: " << e.what() << std::endl;
            return false;
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

    bool createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "INSERT INTO trust_acceptances (client_id) VALUES ($1)",
                pqxx::params{ clientId }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "createTrustAcceptance error: " << e.what() << std::endl;
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

    // ЕДИНСТВЕННОЕ объявление и определение этого метода (дубликат удален)
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
};
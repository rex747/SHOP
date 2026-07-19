// database.h
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

#include "config_server.h"
#include "logger_server.h"

extern pqxx::connection* g_dbConnection;
extern Logger g_serverLogger;

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
public:
    static bool initialize() {
        try {
            pqxx::work txn{ *g_dbConnection };

            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS clients (
                    id SERIAL PRIMARY KEY,
                    phone VARCHAR(20) UNIQUE NOT NULL,
                    last_name VARCHAR(64) NOT NULL,
                    first_name VARCHAR(64) NOT NULL,
                    middle_name VARCHAR(64),
                    email VARCHAR(64),
                    items_submitted INTEGER DEFAULT 0,
                    items_sold INTEGER DEFAULT 0,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    updated_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW())
                );

                CREATE TABLE IF NOT EXISTS queue_tickets (
                    id SERIAL PRIMARY KEY,
                    number VARCHAR(50) UNIQUE NOT NULL,
                    client_id INTEGER REFERENCES clients(id),
                    queue_type VARCHAR(50) NOT NULL,
                    position INTEGER NOT NULL,
                    items_count INTEGER DEFAULT 1,
                    window_number VARCHAR(10),
                    estimated_wait_time INTEGER DEFAULT 0,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    status VARCHAR(20) DEFAULT 'waiting',
                    served_at INTEGER
                );

                CREATE TABLE IF NOT EXISTS items (
                    id SERIAL PRIMARY KEY,
                    client_id INTEGER REFERENCES clients(id),
                    description TEXT NOT NULL,
                    estimated_price DECIMAL(10,2),
                    actual_price DECIMAL(10,2),
                    status VARCHAR(50) DEFAULT 'pending',
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    sold_at INTEGER,
                    synced_to_1c BOOLEAN DEFAULT FALSE
                );

                CREATE TABLE IF NOT EXISTS auth_sms_codes (
                    id SERIAL PRIMARY KEY,
                    phone VARCHAR(20) NOT NULL,
                    code VARCHAR(10) NOT NULL,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    used BOOLEAN DEFAULT FALSE
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

                CREATE TABLE IF NOT EXISTS trust_acceptances (
                    id SERIAL PRIMARY KEY,
                    client_id INTEGER REFERENCES clients(id),
                    items_description TEXT,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW()),
                    processed BOOLEAN DEFAULT FALSE,
                    processed_at INTEGER
                );

                CREATE TABLE IF NOT EXISTS sync_log (
                    id SERIAL PRIMARY KEY,
                    sync_type VARCHAR(50) NOT NULL,
                    records_count INTEGER,
                    status VARCHAR(20),
                    error_message TEXT,
                    created_at INTEGER DEFAULT EXTRACT(EPOCH FROM NOW())
                );

                CREATE INDEX IF NOT EXISTS idx_clients_phone ON clients(phone);
                CREATE INDEX IF NOT EXISTS idx_queue_tickets_status ON queue_tickets(status);
                CREATE INDEX IF NOT EXISTS idx_queue_tickets_created ON queue_tickets(created_at);
                CREATE INDEX IF NOT EXISTS idx_items_client ON items(client_id);
                CREATE INDEX IF NOT EXISTS idx_items_synced ON items(synced_to_1c);
            )");

            txn.commit();
            g_serverLogger.info("Database initialized successfully");
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("Database initialization error: ") + e.what());
            return false;
        }
    }

    static std::optional<Client> getClientByPhone(const std::string& phone) {
        try {
            pqxx::work txn{ *g_dbConnection };
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
            g_serverLogger.error(std::string("getClientByPhone error: ") + e.what());
            return std::nullopt;
        }
    }

    // ИСПРАВЛЕНИЕ 2: Запросы теперь обращаются к таблице 'clients', а не к несуществующей 'clients_extended'
    static bool registerClient(const std::string& phone, const std::string& last_name,
        const std::string& first_name, const std::string& middle_name,
        const std::string& email, int items_submitted, int items_sold)
    {
        try {
            pqxx::work txn{ *g_dbConnection };

            std::optional<std::string> mid_opt = middle_name.empty() ? std::nullopt : std::optional<std::string>(middle_name);
            std::optional<std::string> email_opt = email.empty() ? std::nullopt : std::optional<std::string>(email);

            auto exist = txn.exec(
                "SELECT id FROM clients WHERE phone = $1",
                pqxx::params{ phone }
            );

            if (!exist.empty()) {
                txn.exec(
                    "UPDATE clients SET last_name=$2, first_name=$3, middle_name=$4, "
                    "email=$5, items_submitted=$6, items_sold=$7, updated_at=EXTRACT(EPOCH FROM NOW()) "
                    "WHERE phone=$1",
                    pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
                );
                txn.commit();
                g_serverLogger.info("Client updated: " + phone);
                return true;
            }

            txn.exec(
                "INSERT INTO clients (phone, last_name, first_name, middle_name, email, items_submitted, items_sold) "
                "VALUES ($1,$2,$3,$4,$5,$6,$7)",
                pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
            );

            txn.commit();
            g_serverLogger.info("New client registered: " + phone);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("registerClient error: ") + e.what());
            return false;
        }
    }

    static QueueTicket createTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket;
        try {
            pqxx::work txn{ *g_dbConnection };
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
            g_serverLogger.info("Ticket created: " + number);
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("createTicket error: ") + e.what());
        }
        return ticket;
    }

    static json ticketToJson(const QueueTicket& ticket) {
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

    static bool updateItemSyncStatus(int itemId, bool synced) {
        try {
            pqxx::work txn{ *g_dbConnection };
            txn.exec(
                "UPDATE items SET synced_to_1c = $1 WHERE id = $2",
                pqxx::params{ synced, itemId }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("updateItemSyncStatus error: ") + e.what());
            return false;
        }
    }

    static std::vector<json> getUnsyncedItems() {
        std::vector<json> items;
        try {
            pqxx::work txn{ *g_dbConnection };
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
            g_serverLogger.error(std::string("getUnsyncedItems error: ") + e.what());
        }
        return items;
    }
};
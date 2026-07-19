// database_local.h
#pragma once

#include </Users/dpc/Desktop/SHOP/server/client/packages/vcpkg/packages/sqlite3_x64-windows/include/sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include "config.h"
#include "logger.h"
#include "string_utils.h"

extern Logger g_logger;

struct LocalClient {
    int id;
    std::wstring phone;
    std::wstring name;
    std::wstring email;
    int64_t createdAt;
    bool synced;
};

struct LocalItem {
    int id;
    int clientId;
    std::wstring description;
    double estimatedPrice;
    int64_t createdAt;
    bool synced;
};

class LocalDB {
private:
    static sqlite3* s_db;
    static std::mutex s_mutex;

    static int callback(void* data, int argc, char** argv, char** azColName) {
        return 0;
    }

public:
    static bool initialize() {
        std::lock_guard<std::mutex> lock(s_mutex);

        std::wstring dbPath = Config::getDatabasePath();

        int rc = sqlite3_open16(dbPath.c_str(), &s_db);
        if (rc != SQLITE_OK) {
            const char* errMsg = sqlite3_errmsg(s_db); // получаем сообщение от самой БД
            g_logger.error(L"SQL error: " + utf8_to_wstring(errMsg));
            if (s_db) sqlite3_close(s_db); // закрываем, если открыта
            s_db = nullptr;
            return false;
        }

        // Create tables
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS clients (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                phone TEXT UNIQUE NOT NULL,
                name TEXT,
                email TEXT,
                created_at INTEGER DEFAULT (strftime('%s', 'now')),
                synced INTEGER DEFAULT 0
            );
            
            CREATE TABLE IF NOT EXISTS items (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                client_id INTEGER NOT NULL,
                description TEXT NOT NULL,
                estimated_price REAL,
                created_at INTEGER DEFAULT (strftime('%s', 'now')),
                synced INTEGER DEFAULT 0,
                FOREIGN KEY (client_id) REFERENCES clients(id)
            );
            
            CREATE TABLE IF NOT EXISTS offline_queue (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                client_id INTEGER,
                queue_type TEXT NOT NULL,
                ticket_number TEXT,
                items_count INTEGER,
                created_at INTEGER DEFAULT (strftime('%s', 'now')),
                synced INTEGER DEFAULT 0
            );
            
            CREATE INDEX IF NOT EXISTS idx_clients_phone ON clients(phone);
            CREATE INDEX IF NOT EXISTS idx_items_client ON items(client_id);
        )";

        char* errMsg = nullptr;
        rc = sqlite3_exec(s_db, sql, callback, nullptr, &errMsg);

        if (rc != SQLITE_OK) {
            g_logger.error(L"SQL error: " + utf8_to_wstring(errMsg));
            sqlite3_free(errMsg);
            return false;
        }

        g_logger.info(L"Local database initialized successfully");
        return true;
    }

    static bool addClient(const std::wstring& phone, const std::wstring& name,
        const std::wstring& email = L"") {
        std::lock_guard<std::mutex> lock(s_mutex);

        const wchar_t* sql = L"INSERT OR REPLACE INTO clients (phone, name, email) VALUES (?, ?, ?)";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare16_v2(s_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text16(stmt, 1, phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);

        bool success = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);

        return success;
    }

    static std::optional<LocalClient> getClientByPhone(const std::wstring& phone) {
        std::lock_guard<std::mutex> lock(s_mutex);

        const wchar_t* sql = L"SELECT id, phone, name, email, created_at, synced FROM clients WHERE phone = ?";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare16_v2(s_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::nullopt;
        }

        sqlite3_bind_text16(stmt, 1, phone.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            LocalClient client;
            client.id = sqlite3_column_int(stmt, 0);
            client.phone = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            client.name = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
            client.email = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 3));
            client.createdAt = sqlite3_column_int64(stmt, 4);
            client.synced = sqlite3_column_int(stmt, 5) != 0;

            sqlite3_finalize(stmt);
            return client;
        }

        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    static bool addItem(int clientId, const std::wstring& description,
        double estimatedPrice) {
        std::lock_guard<std::mutex> lock(s_mutex);

        const wchar_t* sql = L"INSERT INTO items (client_id, description, estimated_price) VALUES (?, ?, ?)";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare16_v2(s_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_int(stmt, 1, clientId);
        sqlite3_bind_text16(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, estimatedPrice);

        bool success = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);

        return success;
    }

    static bool addOfflineQueue(int clientId, const std::wstring& queueType,
        const std::wstring& ticketNumber, int itemsCount) {
        std::lock_guard<std::mutex> lock(s_mutex);

        const wchar_t* sql = L"INSERT INTO offline_queue (client_id, queue_type, ticket_number, items_count) VALUES (?, ?, ?, ?)";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare16_v2(s_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_int(stmt, 1, clientId);
        sqlite3_bind_text16(stmt, 2, queueType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 3, ticketNumber.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, itemsCount);

        bool success = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);

        return success;
    }

    static void close() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_db) {
            sqlite3_close(s_db);
            s_db = nullptr;
        }
    }
};

// Static member initialization
sqlite3* LocalDB::s_db = nullptr;
std::mutex LocalDB::s_mutex;

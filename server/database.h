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
    std::string birth_date;
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

            // НОВАЯ МИГРАЦИЯ: добавление колонок для учёта распределения выручки
            // между комитентом и магазином.
            // client_percent  - процент от продажи, который получает комитент
            // store_percent   - процент от продажи, который получает магазин
            // client_amount   - сумма выплаты комитенту (цена * кол-во * процент / 100)
            // store_amount    - сумма прибыли магазина (цена * кол-во * процент / 100)
            txn.exec(R"(
             DO $$
             BEGIN
                 ALTER TABLE items ADD COLUMN IF NOT EXISTS client_percent DECIMAL(5,2) DEFAULT 0;
                 ALTER TABLE items ADD COLUMN IF NOT EXISTS store_percent DECIMAL(5,2) DEFAULT 0;
                 ALTER TABLE items ADD COLUMN IF NOT EXISTS client_amount DECIMAL(10,2) DEFAULT 0;
                 ALTER TABLE items ADD COLUMN IF NOT EXISTS store_amount DECIMAL(10,2) DEFAULT 0;
             EXCEPTION WHEN duplicate_column THEN
                 NULL;
             END $$;
             )");
            g_serverLogger.info("Migration: items commission columns added successfully");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПРИЛОЖЕНИЯ К ДОГОВОРУ (чеки комитентов)
            // Хранит порядковый номер приложения (как 263216 в образце чека),
            // чтобы по нему можно было найти весь перечень сданных вещей,
            // количество проданных и возвращённых. Безопасный стиль миграций,
            // как во всём файле: не ломает работающую БД.
            // ========================================================================
            txn.exec(R"(
                CREATE SEQUENCE IF NOT EXISTS contract_appendices_number_seq
                START 1;
            )");
            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS contract_appendices (
                    id SERIAL PRIMARY KEY,
                    appendix_number BIGINT UNIQUE NOT NULL,
                    client_id INTEGER REFERENCES clients(id),
                    worker_id INTEGER REFERENCES clients(id),
                    created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                    valid_until BIGINT,
                    total_quantity INTEGER DEFAULT 0,
                    total_value DECIMAL(10,2) DEFAULT 0,
                    total_client_amount DECIMAL(10,2) DEFAULT 0
                )
            )");
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS appendix_id BIGINT;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПОДДЕРЖКА ПОШТУЧНОЙ ПРОДАЖИ ТОВАРОВ
            // Безопасное добавление колонки sold_quantity в таблицу items.
            // sold_quantity — сколько единиц из партии УЖЕ ПРОДАНО.
            // Доступный остаток = quantity - sold_quantity.
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS sold_quantity INTEGER DEFAULT 0;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: items.sold_quantity added successfully");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ТАБЛИЦА ЛОГА ПРОДАЖ (item_sales)
            // Каждая запись = одна проданная единица товара.
            // Позволяет отслеживать: когда продано, кем (касса/1С), по какой цене.
            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS item_sales (
                    id SERIAL PRIMARY KEY,
                    item_id INTEGER REFERENCES items(id),
                    client_id INTEGER REFERENCES clients(id),
                    quantity_sold INTEGER DEFAULT 1,
                    sale_price DECIMAL(10,2) DEFAULT 0,
                    client_amount DECIMAL(10,2) DEFAULT 0,
                    store_amount DECIMAL(10,2) DEFAULT 0,
                    sold_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                    source VARCHAR(50) DEFAULT '1C',
                    receipt_number VARCHAR(100),
                    barcode_payload TEXT
                )
            )");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПОЛЕ expired_at В items
            // Фиксирует момент, когда товар был помечен как "выбывший из продажи"
            // по истечении 15-дневного срока. NULL = товар ещё активен.
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS expired_at BIGINT;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: items.expired_at added successfully");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПОЛЕ expired_processed В contract_appendices
            // Флаг для фонового процесса: TRUE = приложение уже обработано
            // как просроченное (предотвращает повторную обработку).
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE contract_appendices ADD COLUMN IF NOT EXISTS expired_processed BOOLEAN DEFAULT FALSE;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: contract_appendices.expired_processed added successfully");

            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ДОПОЛНИТЕЛЬНЫЕ ПОЛЯ КОМИТЕНТА (для first_time)
            // birth_date, passport_type, passport_series, passport_number, address
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS birth_date VARCHAR(10);
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS passport_type VARCHAR(10) DEFAULT 'rf';
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS passport_series VARCHAR(10);
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS passport_number VARCHAR(20);
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS address TEXT;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: committee additional fields added (birth_date, passport_*, address)");

            // ========================================================================
            // МИГРАЦИЯ: Замена UNIQUE ограничений на частичные уникальные индексы
            // для поддержки циклических коротких номеров талонов (не более 3 знаков).
            //
            // ПРИЧИНА:
            // Полные UNIQUE ограничения на number / ticket_number не позволяют
            // повторно использовать короткие номера (001-999) после обслуживания
            // талонов, так как старые записи остаются в таблице со статусом served.
            //
            // РЕШЕНИЕ:
            // Удаляем полные UNIQUE ограничения и создаём частичные уникальные
            // индексы, которые гарантируют уникальность номера ТОЛЬКО среди
            // активных талонов (waiting/accepted для очередей, pending/accepted
            // для доверия). Обслуженные талоны могут иметь повторяющиеся номера,
            // что позволяет использовать циклическую нумерацию 001-999.
            //
            // Это НЕ ЛОМАЕТ бизнес-логику, так как все методы поиска/приёма/
            // обслуживания талона всегда фильтруют по активному статусу.
            // ========================================================================
            txn.exec(R"(
                DO $$
                DECLARE r RECORD;
                BEGIN
                    -- Удаляем UNIQUE ограничения с queue_tickets.number
                    FOR r IN (SELECT constraint_name FROM information_schema.table_constraints 
                              WHERE table_name = 'queue_tickets' AND constraint_type = 'UNIQUE')
                    LOOP
                        EXECUTE 'ALTER TABLE queue_tickets DROP CONSTRAINT ' || quote_ident(r.constraint_name);
                    END LOOP;

                    -- Удаляем UNIQUE ограничения с first_time_tickets.ticket_number
                    FOR r IN (SELECT constraint_name FROM information_schema.table_constraints 
                              WHERE table_name = 'first_time_tickets' AND constraint_type = 'UNIQUE')
                    LOOP
                        EXECUTE 'ALTER TABLE first_time_tickets DROP CONSTRAINT ' || quote_ident(r.constraint_name);
                    END LOOP;

                    -- Удаляем UNIQUE ограничения с trust_acceptances.ticket_number
                    FOR r IN (SELECT constraint_name FROM information_schema.table_constraints 
                              WHERE table_name = 'trust_acceptances' AND constraint_type = 'UNIQUE')
                    LOOP
                        EXECUTE 'ALTER TABLE trust_acceptances DROP CONSTRAINT ' || quote_ident(r.constraint_name);
                    END LOOP;
                END $$;
            )");

            // Создаём частичные уникальные индексы для активных талонов
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_queue_tickets_number_active ON queue_tickets(number) WHERE status IN ('waiting', 'accepted')");
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_first_time_tickets_number_active ON first_time_tickets(ticket_number) WHERE status IN ('waiting', 'accepted')");
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_trust_acceptances_number_active ON trust_acceptances(ticket_number) WHERE status IN ('pending', 'accepted')");
            g_serverLogger.info("Migration: UNIQUE constraints replaced with partial unique indexes for cyclic ticket numbers (001-999)");

            txn.exec("CREATE INDEX IF NOT EXISTS idx_contract_appendices_client ON contract_appendices(client_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_contract_appendices_number ON contract_appendices(appendix_number)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_appendix_id ON items(appendix_id)");
            g_serverLogger.info("Migration: contract_appendices table, sequence and items.appendix_id added successfully");

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
			// Индекс для быстрого поиска проданных товаров по дате продажи и по источнику (1С/касса)
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_item_id ON item_sales(item_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_client_id ON item_sales(client_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_sold_at ON item_sales(sold_at)");

            g_serverLogger.info("Migration: item_sales table created successfully");
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
                "SELECT id, phone, last_name, first_name, middle_name, email, role, birth_date, created_at FROM clients WHERE phone = $1",
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
            client.role = result[0]["role"].is_null() ? "client" : result[0]["role"].as<std::string>();
            client.birth_date = result[0]["birth_date"].is_null() ? "" : result[0]["birth_date"].as<std::string>();
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            g_serverLogger.info("getClientByPhone: phone=" + phone + ", birth_date=" + client.birth_date);
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
                "SELECT id, phone, last_name, first_name, middle_name, email, role, birth_date, created_at FROM clients WHERE id = $1",
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
            client.role = result[0]["role"].is_null() ? "client" : result[0]["role"].as<std::string>();
            client.birth_date = result[0]["birth_date"].is_null() ? "" : result[0]["birth_date"].as<std::string>();
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            g_serverLogger.info("getClientById: id=" + std::to_string(id) + ", birth_date=" + client.birth_date);
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

    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: createTicket
    // ========================================================================
    // ПРИЧИНА ИСПРАВЛЕНИЯ:
    // Номер талона генерировался длинным: prefix + "-" + time%10000 + "-" + position.
    // Например: G-1234-56, что превышает 3 знака.
    //
    // РЕШЕНИЕ:
    // Генерируем короткий номер талона (не более 3 знаков): prefix + %03d(position).
    // Например: G001, G002, ..., G999.
    // Позиция вычисляется как количество ожидающих талонов + 1 (без изменений).
    // Благодаря частичному уникальному индексу (см. миграцию выше), короткие
    // номера могут использоваться циклически без конфликта уникальности.
    // ========================================================================
    QueueTicket createTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket;
        try {
            pqxx::work txn{ *conn_ };

            // Определяем префикс очереди (без изменений)
            std::string prefix = (queueType == "general") ? "G" :
                (queueType == "first_time") ? "F" :
                (queueType == "extra_20") ? "E" :
                (queueType == "paid") ? "P" :
                (queueType == "expensive") ? "D" : "X";

            // Вычисляем позицию: количество ожидающих талонов в данной очереди + 1
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );
            int position = countResult[0][0].as<int>() + 1;

            // =====================================================================
            // ИСПРАВЛЕНИЕ: Генерируем короткий номер талона (не более 3 знаков).
            // Используем позицию в очереди с форматированием %03d (001, 002, ..., 999).
            // Если позиция > 999 (что невозможно из-за лимитов очередей в config_server.h:
            // MAX_QUEUE_SIZE_GENERAL=100, MAX_QUEUE_SIZE_EXTRA_20=50, MAX_QUEUE_SIZE_PAID=30,
            // MAX_QUEUE_SIZE_EXPENSIVE=20), ограничиваем до 999 для безопасности.
            // =====================================================================
            int displayNum = std::min(position, 999);
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string number = prefix + std::string(numBuf);

            std::string window = "1";
            int waitTime = position * 5; // 5 минут на человека (математика без изменений)

            g_serverLogger.info("createTicket: queueType=" + queueType +
                ", position=" + std::to_string(position) +
                ", ticketNumber=" + number +
                ", clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(itemsCount));

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

            g_serverLogger.info("createTicket: SUCCESS - ticketNumber=" + ticket.number +
                ", position=" + std::to_string(ticket.position) +
                ", waitTime=" + std::to_string(ticket.estimatedWaitTime));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createTicket error: " + std::string(e.what()));
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

    // ========================================================================
   // ИСПРАВЛЕННЫЙ МЕТОД: createTrustAcceptance
   // ========================================================================
   // ПРИЧИНА ИСПРАВЛЕНИЯ:
   // Номер талона генерировался длинным: "TR-" + timestamp + "-" + rand%10000.
   //
   // РЕШЕНИЕ:
   // Вычисляем количество ожидающих trust талонов и генерируем короткий номер
   // (не более 3 знаков): "T" + %03d(position). Например: T001, T002, ..., T999.
   // Благодаря частичному уникальному индексу (см. миграцию выше), короткие
   // номера могут использоваться циклически без конфликта уникальности.
   // ========================================================================
    std::optional<std::string> createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *conn_ };

            // =====================================================================
            // Вычисляем количество ожидающих trust талонов для генерации
            // короткого номера (не более 3 знаков).
            // =====================================================================
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM trust_acceptances WHERE status = 'pending'"
            );
            int position = countResult[0][0].as<int>() + 1;

            // Генерируем короткий номер талона (не более 3 знаков): "T" + %03d(position)
            int displayNum = std::min(position, 999);
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string ticketNumber = "T" + std::string(numBuf);

            g_serverLogger.info("Creating TRUST acceptance ticket: " + ticketNumber +
                " for client " + std::to_string(clientId) +
                ", position=" + std::to_string(position));

            auto result = txn.exec(
                "INSERT INTO trust_acceptances (client_id, ticket_number) "
                "VALUES ($1, $2) RETURNING ticket_number",
                pqxx::params{ clientId, ticketNumber }
            );

            g_serverLogger.info("Inserted TRUST acceptance ticket into database: " + ticketNumber +
                " for client " + std::to_string(clientId));

            txn.commit();

            g_serverLogger.info("Created TRUST acceptance ticket: " + ticketNumber +
                " for client " + std::to_string(clientId));

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

    // ========================================================================
   // ИСПРАВЛЕННЫЙ МЕТОД: createFirstTimeTicket
   // ========================================================================
   // ПРИЧИНА ИСПРАВЛЕНИЯ:
   // 1. Метод не вычислял позицию в очереди (количество ожидающих талонов),
   //    поэтому клиент всегда получал позицию 1 (hardcoded в main_window.h).
   // 2. Номер талона генерировался длинным: "F-" + timestamp + "-" + rand%10000.
   // 3. Метод возвращал только std::string (номер), не возвращая позицию,
   //    окно и время ожидания.
   //
   // РЕШЕНИЕ:
   // 1. Вычисляем позицию как COUNT(*) ожидающих талонов + 1.
   // 2. Генерируем короткий номер талона (не более 3 знаков): "F" + %03d(position).
   // 3. Возвращаем JSON с полями ticket_number, position, window_number,
   //    wait_time_minutes, created_at, чтобы клиент мог отобразить корректную позицию.
   // 4. Благодаря частичному уникальному индексу (см. миграцию выше), короткие
   //    номера могут использоваться циклически без конфликта уникальности.
   // ========================================================================
    json createFirstTimeTicket(const std::string& windowNumber = "1") {
        try {
            pqxx::work txn{ *conn_ };

            // =====================================================================
            // Вычисляем позицию: количество ожидающих талонов в очереди "Первый раз" + 1.
            // Это обеспечивает корректное запоминание последовательно взятых талонов.
            // =====================================================================
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM first_time_tickets WHERE status = 'waiting'"
            );
            int position = countResult[0][0].as<int>() + 1;

            // =====================================================================
            // Генерируем короткий номер талона (не более 3 знаков).
            // Используем позицию в очереди с форматированием %03d (001, 002, ..., 999).
            // Если позиция > 999 (что невозможно из-за лимита MAX_QUEUE_SIZE_FIRST_TIME=20
            // в config_server.h), ограничиваем до 999 для безопасности.
            // =====================================================================
            int displayNum = std::min(position, 999);
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string ticketNumber = "F" + std::string(numBuf);

            // Время ожидания: 5 минут на человека (математика как в createTicket)
            int waitTime = position * 5;

            g_serverLogger.info("createFirstTimeTicket: position=" + std::to_string(position) +
                ", ticketNumber=" + ticketNumber +
                ", window=" + windowNumber +
                ", waitTime=" + std::to_string(waitTime));

            auto result = txn.exec(
                "INSERT INTO first_time_tickets (ticket_number, window_number) "
                "VALUES ($1, $2) RETURNING id, created_at",
                pqxx::params{ ticketNumber, windowNumber }
            );

            txn.commit();

            // Формируем JSON ответ с полной информацией о талоне
            json response;
            response["ticket_number"] = ticketNumber;
            response["position"] = position;
            response["window_number"] = windowNumber;
            response["wait_time_minutes"] = waitTime;
            response["created_at"] = result[0]["created_at"].as<int64_t>();

            g_serverLogger.info("createFirstTimeTicket: SUCCESS - ticketNumber=" + ticketNumber +
                ", position=" + std::to_string(position) +
                ", window=" + windowNumber +
                ", waitTime=" + std::to_string(waitTime));

            return response;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createFirstTimeTicket error: " + std::string(e.what()));
            json error;
            error["error"] = e.what();
            return error;
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
    /**
     * Получить список всех товаров клиента (включая непроданные)
     * ДОПОЛНЕНИЕ: Теперь возвращает также поля распределения выручки.
     * @param clientId ID клиента
     * @return вектор JSON-объектов с полями: id, item_number, description,
     *         estimated_price, quantity, sale_date, condition, note, created_at,
     *         status, client_percent, store_percent, client_amount, store_amount
     */
    std::vector<json> getClientItems(int clientId) {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, item_number, description, estimated_price, quantity, "
                "sale_date, condition, note, created_at, status, "
                "client_percent, store_percent, client_amount, store_amount "
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

                // =================================================================
                // НОВЫЕ ПОЛЯ РАСПРЕДЕЛЕНИЯ ВЫРУЧКИ
                // Если значения NULL (старые записи), возвращаем 0.
                // =================================================================
                item["client_percent"] = row["client_percent"].is_null() ?
                    0.0 : row["client_percent"].as<double>();
                item["store_percent"] = row["store_percent"].is_null() ?
                    0.0 : row["store_percent"].as<double>();
                item["client_amount"] = row["client_amount"].is_null() ?
                    0.0 : row["client_amount"].as<double>();
                item["store_amount"] = row["store_amount"].is_null() ?
                    0.0 : row["store_amount"].as<double>();

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
 * Пакетное добавление товаров + создание «приложения к договору».
 * ДОПОЛНЕНИЕ (не меняет бизнес-логику):
 *  - в той же транзакции создаётся запись contract_appendices
 *    (номер из последовательности contract_appendices_number_seq);
 *  - каждая вставляемая вещь получает ссылку appendix_id;
 *  - номер приложения возвращается через outAppendixNumber
 *    (параметр по умолчанию = nullptr → старые вызовы работают без изменений).
 * Математика сумм НЕ пересчитывается: totals берутся из переданных
 * клиентом полей (quantity, estimated_price, client_amount), которые
 * посчитаны в worker_window.h::onItemAdd() по существующей формуле.
 */
    bool addItemsBatch(int clientId, const json& items, int workerId = 0,
        long long* outAppendixNumber = nullptr) {
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

            // =====================================================================
            // НОВОЕ: ГЕНЕРАЦИЯ НОМЕРА ПРИЛОЖЕНИЯ (атомарно, в той же транзакции)
            // =====================================================================
            long long appendixNumber =
                txn.exec("SELECT nextval('contract_appendices_number_seq')")
                .at(0).at(0).as<long long>();
            g_serverLogger.info("addItemsBatch: generated appendix_number=" +
                std::to_string(appendixNumber));

            // =====================================================================
            // НОВОЕ: АГРЕГАТНЫЕ ИТОГИ ДЛЯ ТАБЛИЦЫ contract_appendices.
            // Только суммирование клиентских значений — формулы не трогаем.
            // =====================================================================
            int totalQuantity = 0;
            double totalValue = 0.0;
            double totalClientAmount = 0.0;
            for (const auto& item : items) {
                int quantity = item.value("quantity", 1);
                double price = item.value("estimated_price", 0.0);
                totalQuantity += quantity;
                totalValue += price * quantity;
                totalClientAmount += item.value("client_amount", 0.0);
            }
            // Срок действия приложения: +15 календарных дней (Ваше решение)
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t validUntil = nowSec + 15 * 86400;

            std::optional<int> workerIdOpt = (workerId > 0) ?
                std::optional<int>(workerId) : std::nullopt;

            // =====================================================================
            // НОВОЕ: СОЗДАНИЕ ЗАПИСИ ПРИЛОЖЕНИЯ (до вставки вещей, чтобы
            // получить id для ссылки items.appendix_id)
            // =====================================================================
            auto appRes = txn.exec(
                "INSERT INTO contract_appendices "
                "(appendix_number, client_id, worker_id, valid_until, "
                " total_quantity, total_value, total_client_amount) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                pqxx::params{ appendixNumber, clientId, workerIdOpt,
                              validUntil, totalQuantity, totalValue,
                              totalClientAmount });
            long long appendixId = appRes.at(0).at(0).as<long long>();
            g_serverLogger.info("addItemsBatch: contract_appendices row created, id=" +
                std::to_string(appendixId) + ", number=" +
                std::to_string(appendixNumber) + ", totalQty=" +
                std::to_string(totalQuantity) + ", totalValue=" +
                std::to_string(totalValue) + ", totalClientAmount=" +
                std::to_string(totalClientAmount));

            for (const auto& item : items) {
                std::string desc = item.value("description", "");
                double price = item.value("estimated_price", 0.0);
                int quantity = item.value("quantity", 1);
                std::string condition = item.value("condition", "");
                std::string note = item.value("note", "");
                double clientPercent = item.value("client_percent", 0.0);
                double storePercent = item.value("store_percent", 0.0);
                double clientAmount = item.value("client_amount", 0.0);
                double storeAmount = item.value("store_amount", 0.0);
                g_serverLogger.info("addItemsBatch: item desc=" + desc +
                    ", price=" + std::to_string(price) +
                    ", qty=" + std::to_string(quantity) +
                    ", clientPercent=" + std::to_string(clientPercent) +
                    ", storePercent=" + std::to_string(storePercent) +
                    ", clientAmount=" + std::to_string(clientAmount) +
                    ", storeAmount=" + std::to_string(storeAmount) +
                    ", appendixId=" + std::to_string(appendixId));
                // ВСТАВКА ТОВАРА + ссылка на приложение (13-й параметр)
                txn.exec(
                    "INSERT INTO items (client_id, item_number, description, estimated_price, "
                    "quantity, condition, note, worker_id, client_percent, store_percent, "
                    "client_amount, store_amount, appendix_id) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13)",
                    pqxx::params{ clientId, nextNumber++, desc, price, quantity,
                                  condition.empty() ? std::optional<std::string>{} : condition,
                                  note.empty() ? std::optional<std::string>{} : note,
                                  workerIdOpt, clientPercent, storePercent,
                                  clientAmount, storeAmount, appendixId });
            }
            txn.commit();
            if (outAppendixNumber) *outAppendixNumber = appendixNumber;
            g_serverLogger.info("addItemsBatch: successfully added " +
                std::to_string(items.size()) + " items for client " +
                std::to_string(clientId) + " by worker " + std::to_string(workerId) +
                ", appendix_number=" + std::to_string(appendixNumber));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("addItemsBatch error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЕ МЕТОДЫ: ВЫБОРКА «ПРИЛОЖЕНИЕ К ДОГОВОРУ»
    // Позволяют по номеру приложения найти весь перечень вещей,
    // сколько продано и сколько возвращено (по существующим статусам).
    // =========================================================================
    json getAppendixItemsAndCounters(long long appendixId) {
        json itemsArray = json::array();
        int soldCount = 0, returnedCount = 0, pendingCount = 0;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT item_number, description, quantity, estimated_price, "
                "condition, note, status, client_amount "
                "FROM items WHERE appendix_id = $1 ORDER BY item_number",
                pqxx::params{ appendixId });
            for (const auto& row : res) {
                json it;
                it["item_number"] = row["item_number"].as<int>();
                it["description"] = row["description"].as<std::string>();
                it["quantity"] = row["quantity"].as<int>();
                it["estimated_price"] = row["estimated_price"].as<double>();
                it["condition"] = row["condition"].is_null() ? "" : row["condition"].as<std::string>();
                it["note"] = row["note"].is_null() ? "" : row["note"].as<std::string>();
                it["status"] = row["status"].is_null() ? "" : row["status"].as<std::string>();
                it["client_amount"] = row["client_amount"].is_null() ? 0.0 : row["client_amount"].as<double>();
                int qty = row["quantity"].as<int>();
                std::string st = it["status"].get<std::string>();
                if (st == "sold") soldCount += qty;                       // продано
                else if (st == "low_quality" || st == "unsold_quality")
                    returnedCount += qty;                                 // возвращено (не продано по качеству)
                else pendingCount += qty;                                 // ещё в продаже
                itemsArray.push_back(it);
            }
            g_serverLogger.info("getAppendixItemsAndCounters: appendixId=" +
                std::to_string(appendixId) + ", items=" + std::to_string(itemsArray.size()) +
                ", sold=" + std::to_string(soldCount) +
                ", returned=" + std::to_string(returnedCount) +
                ", pending=" + std::to_string(pendingCount));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAppendixItemsAndCounters error: " + std::string(e.what()));
        }
        json out;
        out["items"] = itemsArray;
        out["sold_count"] = soldCount;
        out["returned_count"] = returnedCount;
        out["pending_count"] = pendingCount;
        return out;
    }

    /**
     * Полная выборка приложения по его номеру (для будущего быстрого поиска).
     */
    json getAppendixByNumber(long long appendixNumber) {
        json result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT a.id, a.appendix_number, a.client_id, a.worker_id, a.created_at, "
                "a.valid_until, a.total_quantity, a.total_value, a.total_client_amount, "
                "c.last_name || ' ' || c.first_name || COALESCE(' ' || c.middle_name, '') AS client_name "
                "FROM contract_appendices a "
                "LEFT JOIN clients c ON a.client_id = c.id "
                "WHERE a.appendix_number = $1",
                pqxx::params{ appendixNumber });
            if (res.empty()) {
                result["error"] = "Appendix not found";
                return result;
            }
            const auto& row = res.at(0);
            result["appendix_number"] = row["appendix_number"].as<long long>();
            result["client_id"] = row["client_id"].is_null() ? 0 : row["client_id"].as<int>();
            result["worker_id"] = row["worker_id"].is_null() ? 0 : row["worker_id"].as<int>();
            result["created_at"] = row["created_at"].is_null() ? 0 : row["created_at"].as<int64_t>();
            result["valid_until"] = row["valid_until"].is_null() ? 0 : row["valid_until"].as<int64_t>();
            result["total_quantity"] = row["total_quantity"].as<int>();
            result["total_value"] = row["total_value"].as<double>();
            result["total_client_amount"] = row["total_client_amount"].as<double>();
            result["client_name"] = row["client_name"].is_null() ? "" : row["client_name"].as<std::string>();
            json itemsBlock = getAppendixItemsAndCounters(row["id"].as<long long>());
            result["items"] = itemsBlock["items"];
            result["sold_count"] = itemsBlock["sold_count"];
            result["returned_count"] = itemsBlock["returned_count"];
            result["pending_count"] = itemsBlock["pending_count"];
            g_serverLogger.info("getAppendixByNumber: number=" + std::to_string(appendixNumber) +
                " returned successfully");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAppendixByNumber error: " + std::string(e.what()));
            result["error"] = e.what();
        }
        return result;
    }

    /**
     * Последнее приложение клиента (fallback для клиентской печати,
     * если снапшот в клиенте потерян, например после перезапуска программы).
     */
    json getLatestAppendix(int clientId) {
        json result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, appendix_number, created_at, valid_until, total_quantity, "
                "total_value, total_client_amount "
                "FROM contract_appendices WHERE client_id = $1 "
                "ORDER BY created_at DESC, id DESC LIMIT 1",
                pqxx::params{ clientId });
            if (res.empty()) {
                result["error"] = "No appendices for client";
                return result;
            }
            const auto& row = res.at(0);
            result["appendix_number"] = row["appendix_number"].as<long long>();
            result["client_id"] = clientId;
            result["created_at"] = row["created_at"].is_null() ? 0 : row["created_at"].as<int64_t>();
            result["valid_until"] = row["valid_until"].is_null() ? 0 : row["valid_until"].as<int64_t>();
            result["total_quantity"] = row["total_quantity"].as<int>();
            result["total_value"] = row["total_value"].as<double>();
            result["total_client_amount"] = row["total_client_amount"].as<double>();
            json itemsBlock = getAppendixItemsAndCounters(row["id"].as<long long>());
            result["items"] = itemsBlock["items"];
            g_serverLogger.info("getLatestAppendix: clientId=" + std::to_string(clientId) +
                ", appendix_number=" + std::to_string(result["appendix_number"].get<long long>()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getLatestAppendix error: " + std::string(e.what()));
            result["error"] = e.what();
        }
        return result;
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
                "COALESCE(SUM(CASE WHEN i.status = 'low_quality' OR i.status = 'unsold_quality' THEN i.estimated_price * i.quantity ELSE 0 END), 0) as low_quality_value, "
                "COALESCE(SUM(i.client_amount), 0) as total_client_amount, "
                "COALESCE(SUM(i.store_amount), 0) as total_store_amount "
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

                // =====================================================================
                // НОВЫЕ ПОЛЯ: СУММАРНЫЕ ВЫПЛАТЫ КОМИТЕНТУ И ПРИБЫЛЬ МАГАЗИНА
                // =====================================================================
                client["total_client_amount"] = row["total_client_amount"].as<double>();
                client["total_store_amount"] = row["total_store_amount"].as<double>();

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

            // =====================================================================
            // 5. ТОВАРЫ, ВЫБЫВШИЕ ИЗ ПРОДАЖИ ПО ИСТЕЧЕНИИ 15-ДНЕВНОГО СРОКА
            // Вызываем существующий метод getExpiredItems() (добавлен ранее).
            // Возвращаем массив товаров со статусом 'expired' + счётчик.
            // =====================================================================
            json expiredArray = getExpiredItems();
            result["expired_items"] = expiredArray;
            result["expired_items_count"] = static_cast<int>(expiredArray.size());
            g_serverLogger.info("getDirectorStats: expired_items_count=" +
                std::to_string(expiredArray.size()));

            // =====================================================================
            // 6. ДОПОЛНЕНИЕ СВОДКИ: СЧЁТЧИК ПРОСРОЧЕННЫХ ТОВАРОВ
            // Добавляем в summary поле expired_items_count, чтобы директор
            // видел общую цифру прямо в нижней строке сводки.
            // =====================================================================
            summary["expired_items_count"] = static_cast<int>(expiredArray.size());
            result["summary"] = summary;

            g_serverLogger.info("getDirectorStats: completed successfully, clients=" +
                std::to_string(clientsArray.size()) +
                ", workers=" + std::to_string(workersArray.size()) +
                ", low_quality_items=" + std::to_string(lowQualityArray.size()) +
                ", expired_items=" + std::to_string(expiredArray.size()));
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

    // =========================================================================
    // НОВЫЙ МЕТОД: РЕГИСТРАЦИЯ ПРОДАЖИ ОДНОЙ ЕДИНИЦЫ ТОВАРА
    // Вызывается при получении уведомления от кассы Атол 77Ф через 1С.
    // Атомарно: увеличивает sold_quantity, создаёт запись в item_sales,
    // обновляет статус если все единицы проданы.
    //
    // @param itemId       — ID товара в таблице items
    // @param salePrice    — цена продажи (из чека кассы)
    // @param source       — источник продажи ("1C", "ATOL_77F", и т.д.)
    // @param receiptNumber— номер чека из кассы
    // @param barcodePayload — содержимое штрих-кода (для аудита)
    // @return true при успешной регистрации продажи
    // =========================================================================
    bool registerItemSale(int itemId, double salePrice, const std::string& source,
        const std::string& receiptNumber, const std::string& barcodePayload) {
        try {
            pqxx::work txn{ *conn_ };

            // =================================================================
            // ШАГ 1: Блокируем строку товара для атомарного обновления.
            // FOR UPDATE предотвращает гонку при одновременных продажах.
            // =================================================================
            auto itemRes = txn.exec(
                "SELECT id, client_id, quantity, sold_quantity, estimated_price, "
                "client_percent, store_percent, status "
                "FROM items WHERE id = $1 FOR UPDATE",
                pqxx::params{ itemId }
            );

            if (itemRes.empty()) {
                g_serverLogger.error("registerItemSale: item not found, id=" + std::to_string(itemId));
                return false;
            }

            int currentQty = itemRes[0]["quantity"].as<int>();
            int currentSold = itemRes[0]["sold_quantity"].as<int>();
            int clientId = itemRes[0]["client_id"].as<int>();
            double estimatedPrice = itemRes[0]["estimated_price"].as<double>();
            double clientPercent = itemRes[0]["client_percent"].is_null() ? 0.0 :
                itemRes[0]["client_percent"].as<double>();
            double storePercent = itemRes[0]["store_percent"].is_null() ? 0.0 :
                itemRes[0]["store_percent"].as<double>();
            std::string currentStatus = itemRes[0]["status"].is_null() ? "pending" :
                itemRes[0]["status"].as<std::string>();

            int availableQty = currentQty - currentSold;

            g_serverLogger.info("registerItemSale: itemId=" + std::to_string(itemId) +
                ", clientId=" + std::to_string(clientId) +
                ", qty=" + std::to_string(currentQty) +
                ", sold=" + std::to_string(currentSold) +
                ", available=" + std::to_string(availableQty) +
                ", price=" + std::to_string(salePrice) +
                ", source=" + source);

            // =================================================================
            // ШАГ 2: Проверка — есть ли доступные единицы для продажи.
            // =================================================================
            if (availableQty <= 0) {
                g_serverLogger.warning("registerItemSale: no available units for item " +
                    std::to_string(itemId) + ", available=" + std::to_string(availableQty));
                return false;
            }

            // =================================================================
            // ШАГ 3: Проверка статуса — нельзя продавать expired/sold товары.
            // =================================================================
            if (currentStatus == "expired" || currentStatus == "sold" ||
                currentStatus == "low_quality" || currentStatus == "unsold_quality") {
                g_serverLogger.warning("registerItemSale: item " + std::to_string(itemId) +
                    " has status '" + currentStatus + "', sale REJECTED");
                return false;
            }

            // =================================================================
            // ШАГ 4: Вычисление сумм распределения выручки.
            // Математика: client_amount = price * client_percent / 100
            //             store_amount  = price * store_percent / 100
            // Это ЕДИНИЧНАЯ сумма (за 1 штуку), не за всю партию.
            // =================================================================
            double unitClientAmount = salePrice * clientPercent / 100.0;
            double unitStoreAmount = salePrice * storePercent / 100.0;

            // =================================================================
            // ШАГ 5: Увеличиваем sold_quantity на 1.
            // =================================================================
            int newSoldQty = currentSold + 1;
            std::string newStatus = (newSoldQty >= currentQty) ? "sold" : "pending";

            txn.exec(
                "UPDATE items SET sold_quantity = $2, status = $3, "
                "sale_date = CASE WHEN $3 = 'sold' THEN EXTRACT(EPOCH FROM NOW()) ELSE sale_date END "
                "WHERE id = $1",
                pqxx::params{ itemId, newSoldQty, newStatus }
            );

            g_serverLogger.info("registerItemSale: item " + std::to_string(itemId) +
                " updated: sold_quantity=" + std::to_string(newSoldQty) +
                ", newStatus=" + newStatus);

            // =================================================================
            // ШАГ 6: Создаём запись в логе продаж item_sales.
            // =================================================================
            txn.exec(
                "INSERT INTO item_sales (item_id, client_id, quantity_sold, sale_price, "
                "client_amount, store_amount, source, receipt_number, barcode_payload) "
                "VALUES ($1, $2, 1, $3, $4, $5, $6, $7, $8)",
                pqxx::params{ itemId, clientId, salePrice, unitClientAmount,
                              unitStoreAmount, source,
                              receiptNumber.empty() ? std::optional<std::string>{} : receiptNumber,
                              barcodePayload.empty() ? std::optional<std::string>{} : barcodePayload }
            );

            // =================================================================
            // ШАГ 7: Обновляем счётчики клиента (items_sold).
            // =================================================================
            txn.exec(
                "UPDATE clients SET items_sold = items_sold + 1, "
                "updated_at = EXTRACT(EPOCH FROM NOW()) WHERE id = $1",
                pqxx::params{ clientId }
            );

            txn.commit();
            g_serverLogger.info("registerItemSale: SUCCESS - itemId=" + std::to_string(itemId) +
                ", newSoldQty=" + std::to_string(newSoldQty) +
                ", clientAmount=" + std::to_string(unitClientAmount) +
                ", storeAmount=" + std::to_string(unitStoreAmount));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("registerItemSale error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ПОИСК ТОВАРА ПО ШТРИХ-КОДУ (для кассы Атол 77Ф)
    // Штрих-код ценника содержит: FIO=...;ID=...;NAME=...;PRICE=...;PAY=...
    // Для идентификации конкретной единицы используем appendix_number + ordinal
    // из тега "<appendix_number>+<ordinal>" на ценнике.
    //
    // @param appendixNumber — номер приложения к договору
    // @param ordinal        — порядковый номер единицы в приложении
    // @return JSON с данными товара или error
    // =========================================================================
    json findItemByBarcode(int clientId, double price) {
        try {
            pqxx::work txn{ *conn_ };
            // Ищем товар клиента с matching price и доступным остатком
            auto res = txn.exec(
                "SELECT id, item_number, description, estimated_price, quantity, "
                "sold_quantity, status, client_percent, store_percent, "
                "client_amount, store_amount, appendix_id "
                "FROM items WHERE client_id = $1 AND estimated_price = $2 "
                "AND status = 'pending' AND (quantity - sold_quantity) > 0 "
                "ORDER BY id LIMIT 1",
                pqxx::params{ clientId, price }
            );

            if (res.empty()) {
                g_serverLogger.warning("findItemByBarcode: no matching item for clientId=" +
                    std::to_string(clientId) + ", price=" + std::to_string(price));
                json err;
                err["error"] = "Item not found or no available units";
                return err;
            }

            json item;
            item["id"] = res[0]["id"].as<int>();
            item["item_number"] = res[0]["item_number"].as<int>();
            item["description"] = res[0]["description"].as<std::string>();
            item["estimated_price"] = res[0]["estimated_price"].as<double>();
            item["quantity"] = res[0]["quantity"].as<int>();
            item["sold_quantity"] = res[0]["sold_quantity"].as<int>();
            item["available"] = res[0]["quantity"].as<int>() - res[0]["sold_quantity"].as<int>();
            item["status"] = res[0]["status"].as<std::string>();
            item["client_percent"] = res[0]["client_percent"].is_null() ? 0.0 :
                res[0]["client_percent"].as<double>();
            item["store_percent"] = res[0]["store_percent"].is_null() ? 0.0 :
                res[0]["store_percent"].as<double>();
            item["appendix_id"] = res[0]["appendix_id"].is_null() ? 0 :
                res[0]["appendix_id"].as<long long>();

            g_serverLogger.info("findItemByBarcode: found itemId=" +
                std::to_string(item["id"].get<int>()) + " for clientId=" +
                std::to_string(clientId) + ", price=" + std::to_string(price));
            return item;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("findItemByBarcode error: " + std::string(e.what()));
            json err;
            err["error"] = e.what();
            return err;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ОБРАБОТКА ПРОСРОЧЕННЫХ ПРИЛОЖЕНИЙ (15-дневный SLA)
    // Вызывается фоновым процессом. Помечает товары как "выбывшие из продажи"
    // если valid_until приложения истёк.
    //
    // @return количество обработанных приложений
    // =========================================================================
    int processExpiredAppendices() {
        int processedCount = 0;
        try {
            pqxx::work txn{ *conn_ };
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // =================================================================
            // Находим все приложения, у которых истёк срок действия
            // и которые ещё не были обработаны как просроченные.
            // =================================================================
            auto expiredRes = txn.exec(
                "SELECT id, appendix_number, client_id, valid_until, total_quantity "
                "FROM contract_appendices "
                "WHERE valid_until < $1 AND expired_processed = FALSE",
                pqxx::params{ nowSec }
            );

            g_serverLogger.info("processExpiredAppendices: found " +
                std::to_string(expiredRes.size()) + " expired appendices to process");

            for (const auto& row : expiredRes) {
                long long appendixId = row["id"].as<long long>();
                long long appendixNumber = row["appendix_number"].as<long long>();
                int clientId = row["client_id"].as<int>();

                g_serverLogger.info("processExpiredAppendices: processing appendix " +
                    std::to_string(appendixNumber) + " (id=" + std::to_string(appendixId) +
                    ", clientId=" + std::to_string(clientId) + ")");

                // =================================================================
                // Помечаем все товары данного приложения как "expired",
                // НО ТОЛЬКО те, которые ещё не проданы (status = 'pending').
                // Проданные товары остаются со статусом 'sold'.
                // =================================================================
                auto updateRes = txn.exec(
                    "UPDATE items SET status = 'expired', expired_at = $2 "
                    "WHERE appendix_id = $1 AND status = 'pending' "
                    "AND (quantity - sold_quantity) > 0",
                    pqxx::params{ appendixId, nowSec }
                );

                int expiredItemsCount = static_cast<int>(updateRes.affected_rows());
                g_serverLogger.info("processExpiredAppendices: marked " +
                    std::to_string(expiredItemsCount) + " items as expired for appendix " +
                    std::to_string(appendixNumber));

                // =================================================================
                // Помечаем приложение как обработанное.
                // =================================================================
                txn.exec(
                    "UPDATE contract_appendices SET expired_processed = TRUE WHERE id = $1",
                    pqxx::params{ appendixId }
                );

                processedCount++;
            }

            txn.commit();
            g_serverLogger.info("processExpiredAppendices: completed, processed " +
                std::to_string(processedCount) + " appendices");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("processExpiredAppendices error: " + std::string(e.what()));
        }
        return processedCount;
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ПОЛУЧЕНИЕ ПРОСРОЧЕННЫХ ТОВАРОВ ДЛЯ ПАНЕЛИ ДИРЕКТОРА
    // Возвращает все товары со статусом 'expired' для отображения
    // в DirectorWindow (вкладка "Выбывшие из продажи по сроку").
    // =========================================================================
    json getExpiredItems() {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT i.id, i.item_number, i.description, i.estimated_price, "
                "i.quantity, i.sold_quantity, i.condition, i.note, i.created_at, "
                "i.expired_at, i.status, "
                "c.id as client_id, c.phone as client_phone, "
                "c.last_name || ' ' || c.first_name || COALESCE(' ' || c.middle_name, '') as client_name, "
                "a.appendix_number, a.valid_until "
                "FROM items i "
                "JOIN clients c ON i.client_id = c.id "
                "LEFT JOIN contract_appendices a ON i.appendix_id = a.id "
                "WHERE i.status = 'expired' "
                "ORDER BY i.expired_at DESC"
            );

            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                item["sold_quantity"] = row["sold_quantity"].as<int>();
                item["unsold_quantity"] = row["quantity"].as<int>() - row["sold_quantity"].as<int>();
                item["condition"] = row["condition"].is_null() ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null() ? "" : row["note"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["expired_at"] = row["expired_at"].is_null() ? 0 : row["expired_at"].as<int64_t>();
                item["status"] = row["status"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["client_phone"] = row["client_phone"].as<std::string>();
                item["client_name"] = row["client_name"].as<std::string>();
                item["appendix_number"] = row["appendix_number"].is_null() ? 0 :
                    row["appendix_number"].as<long long>();
                item["valid_until"] = row["valid_until"].is_null() ? 0 :
                    row["valid_until"].as<int64_t>();
                result.push_back(item);
            }

            g_serverLogger.info("getExpiredItems: returned " + std::to_string(result.size()) +
                " expired items");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getExpiredItems error: " + std::string(e.what()));
        }
        return result;
    }

    // ========================================================================
    // НОВЫЙ МЕТОД: РЕГИСТРАЦИЯ КОМИТЕНТА С ДОПОЛНИТЕЛЬНЫМИ ДАННЫМИ
    // Вызывается из worker_window.h при выборе очереди "first_time"
    // ========================================================================
    std::pair<bool, int> registerCommittee(const std::string& phone,
        const std::string& last_name, const std::string& first_name,
        const std::string& middle_name, const std::string& birth_date,
        const std::string& passport_type, const std::string& passport_series,
        const std::string& passport_number, const std::string& address) {
        try {
            pqxx::work txn{ *conn_ };
            // Проверяем, существует ли клиент с таким телефоном
            auto exist = txn.exec("SELECT id FROM clients WHERE phone = $1", pqxx::params{ phone });
            int clientId = 0;
            if (!exist.empty()) {
                // Обновляем существующего клиента
                clientId = exist[0]["id"].as<int>();
                txn.exec(
                    "UPDATE clients SET last_name=$2, first_name=$3, middle_name=$4, "
                    "birth_date=$5, passport_type=$6, passport_series=$7, passport_number=$8, "
                    "address=$9, updated_at=EXTRACT(EPOCH FROM NOW()) "
                    "WHERE id=$1",
                    pqxx::params{ clientId, last_name, first_name,
                        middle_name.empty() ? std::optional<std::string>{} : std::optional<std::string>(middle_name),
                        birth_date, passport_type, passport_series, passport_number, address }
                );
            }
            else {
                // Создаем нового клиента с ролью 'client'
                auto result = txn.exec(
                    "INSERT INTO clients (phone, last_name, first_name, middle_name, "
                    "birth_date, passport_type, passport_series, passport_number, address, role) "
                    "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,'client') "
                    "RETURNING id",
                    pqxx::params{ phone, last_name, first_name,
                        middle_name.empty() ? std::optional<std::string>{} : std::optional<std::string>(middle_name),
                        birth_date, passport_type, passport_series, passport_number, address }
                );
                clientId = result[0]["id"].as<int>();
            }
            txn.commit();
            g_serverLogger.info("registerCommittee: success, clientId=" + std::to_string(clientId) +
                ", phone=" + phone);
            return { true, clientId };
        }
        catch (const std::exception& e) {
            g_serverLogger.error("registerCommittee error: " + std::string(e.what()));
            return { false, 0 };
        }
    }

};
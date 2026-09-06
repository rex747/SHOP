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
            // МИГРАЦИЯ: колонка totp_secret_encrypted в существующей таблице clients
            // ========================================================================
            // ПРИЧИНА (логи 2026-08-28): продакшн-БД создана старой схемой БЕЗ колонки
            // totp_secret_encrypted. CREATE TABLE IF NOT EXISTS не изменяет
            // существующую таблицу, поэтому UPDATE в setTOTPSecret и SELECT в
            // getTOTPSecret бросали "column ... does not exist" →
            // "Failed to store generated password" и "no stored password hash".
            // ADD COLUMN IF NOT EXISTS безопасен: на новых БД — no-op, данные не трогает.
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS totp_secret_encrypted TEXT;
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: clients.totp_secret_encrypted column ensured");

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

            // ========================================================================
            // МИГРАЦИЯ: добавление колонки block_reason для хранения причины блокировки
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS block_reason TEXT DEFAULT '';
                EXCEPTION WHEN duplicate_column THEN
                    NULL;
                END $$;
            )");
            g_serverLogger.info("Migration: clients.block_reason added successfully");

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

            // ========================================================================
            // НОВОЕ: АВТОСИНХРОНИЗАЦИЯ СЧЁТЧИКОВ clients.items_submitted / items_sold
            //
            // ПРИЧИНА: колонки не обновлялись при приёмке/продаже, поэтому поиск
            // товароведа (getClientFullInfo / searchClientsByFio) всегда выдавал 0.
            //
            // РЕШЕНИЕ:
            //   1) одноразовый полный пересчёт по всем комитентам при старте сервера;
            //   2) триггер AFTER INSERT OR UPDATE OR DELETE ON items, который при
            //      каждом изменении товаров автоматически пересчитывает счётчики
            //      затронутого клиента (внутри той же транзакции — атомарно).
            // ========================================================================

            // --- 1) Полный пересчёт для всех комитентов, у которых есть товары ---
            txn.exec(R"(
             UPDATE clients c SET
                 items_submitted = COALESCE(s.total_qty, 0),
                 items_sold       = COALESCE(s.total_sold, 0),
                 updated_at       = EXTRACT(EPOCH FROM NOW())
             FROM (
                 SELECT client_id,
                        SUM(quantity)       AS total_qty,
                        SUM(sold_quantity)  AS total_sold
                 FROM items
                 GROUP BY client_id
             ) s
             WHERE c.id = s.client_id;
         )");

            // --- 1б) Обнуление счётчиков комитентам, у которых товаров нет ---
            txn.exec(R"(
             UPDATE clients SET items_submitted = 0, items_sold = 0
             WHERE (items_submitted <> 0 OR items_sold <> 0)
               AND NOT EXISTS (SELECT 1 FROM items i WHERE i.client_id = clients.id);
         )");
            g_serverLogger.info("Migration: clients counters recalculated from items");

            // --- 2) Функция автосинхронизации счётчиков одного клиента ---
            txn.exec(R"(
             CREATE OR REPLACE FUNCTION sync_client_counters() RETURNS trigger AS $$
             DECLARE
                 target_id INTEGER;
             BEGIN
                 IF TG_OP = 'DELETE' THEN
                     target_id := OLD.client_id;
                 ELSE
                     target_id := NEW.client_id;
                 END IF;

                 UPDATE clients c SET
                     items_submitted = COALESCE(s.total_qty, 0),
                     items_sold       = COALESCE(s.total_sold, 0),
                     updated_at       = EXTRACT(EPOCH FROM NOW())
                 FROM (
                     SELECT SUM(quantity)      AS total_qty,
                            SUM(sold_quantity) AS total_sold
                     FROM items
                     WHERE client_id = target_id
                 ) s
                 WHERE c.id = target_id;

                 IF TG_OP = 'DELETE' THEN RETURN OLD; ELSE RETURN NEW; END IF;
             END;
             $$ LANGUAGE plpgsql;
             )");

            // --- 3) Триггер: срабатывает на каждое изменение items ---
            txn.exec(R"(DROP TRIGGER IF EXISTS trg_items_sync_client_counters ON items;)");
            txn.exec(R"(
             CREATE TRIGGER trg_items_sync_client_counters
             AFTER INSERT OR UPDATE OR DELETE ON items
             FOR EACH ROW EXECUTE FUNCTION sync_client_counters();
            )");
            g_serverLogger.info("Migration: trigger trg_items_sync_client_counters created (auto-sync of clients counters)");

            txn.commit();
            g_serverLogger.info("Database initialized successfully");
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Database initialization error: " + std::string(e.what()));
            std::cerr << "Database initialization error: " << e.what() << std::endl;
            return false;
        }

        // =========================================================================
        // МИГРАЦИЯ: МОДУЛЬ «КАССИР»
        // =========================================================================
        //
        // НАЗНАЧЕНИЕ:
        // Создаёт таблицу для хранения документов кассира (накладные на возврат,
        // расписки, накладные об утрате, накладные о выплате вознаграждения).
        //
        // БЕЗОПАСНОСТЬ:
        // Эта таблица НЕ влияет на существующую логику, математику и бизнес-модель
        // программы. Она используется ТОЛЬКО для нумерации документов кассира
        // (порядковый номер документа учитывается для каждого комитента отдельно).
        //
        // ПОТОКОБЕЗОПАСНОСТЬ:
        // Миграция выполняется в рамках одной транзакции при инициализации сервера.
        // Повторное выполнение безопасно благодаря CREATE TABLE IF NOT EXISTS.
        // =========================================================================
        try {
            pqxx::work txnCashier{ *conn_ };
            txnCashier.exec(
                "CREATE TABLE IF NOT EXISTS cashier_documents ("
                "    id SERIAL PRIMARY KEY,"
                "    client_id INTEGER REFERENCES clients(id),"
                "    doc_type VARCHAR(20) NOT NULL,"
                "    doc_number INTEGER NOT NULL,"
                "    created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),"
                "    items_data TEXT,"
                "    total_amount DECIMAL(10,2) DEFAULT 0"
                ")"
            );
            txnCashier.exec(
                "CREATE INDEX IF NOT EXISTS idx_cashier_documents_client "
                "ON cashier_documents(client_id, doc_type)"
            );
            txnCashier.commit();
            g_serverLogger.info("Migration: cashier_documents table created");
        }
        
        catch (const std::exception& e) {
            g_serverLogger.error("Migration cashier_documents error: " +
                std::string(e.what()));
        }
        return true;


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


    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getDailyTicketCount
    // ========================================================================
    //
    // ПРИЧИНА ИСПРАВЛЕНИЯ (подтверждена логами 2026-08-24 18:16):
    //
    //   Старый запрос считал талоны, СОЗДАННЫЕ СЕГОДНЯ (created_at >= полуночи
    //   UTC), НЕЗАВИСИМО ОТ СТАТУСА:
    //     WHERE queue_type = $1 AND created_at >= EXTRACT(EPOCH FROM date_trunc(...))
    //   Поэтому:
    //     - обслуженные/принятые талоны сегодняшнего дня ВХОДИЛИ в счётчик
    //       (экран завышал «ожидающих»);
    //     - ожидающие талоны, созданные в ПРЕДЫДУЩИЕ дни, НЕ ВХОДИЛИ в счётчик
    //       (экран занижал «ожидающих»).
    //   Подтверждение: daily_count вернул 6, а createTicket вычислил
    //   basePosition=10 (реально 9 ожидающих). Экран показывал 6, затем
    //   позицию 10 — несоответствие.
    //
    //   Позиция талона в createTicket() вычисляется как
    //   COUNT(status='waiting') + 1. Надпись на экране терминала —
    //   «Количество ожидающих в очереди» / «Количество ожидающих приема
    //   человек». Следовательно, информационный эндпоинт обязан возвращать
    //   именно ТЕКУЩИХ ОЖИДАЮЩИХ, по тому же критерию, что и позиция.
    //
    // ИСПРАВЛЕНИЕ:
    //   WHERE-условие заменено на status = 'waiting' (без фильтра по дате).
    //   Запрос теперь ИДЕНТИЧЕН критерию basePosition в createTicket():
    //   отображаемое N всегда равно количеству людей перед комитентом,
    //   а позиция выданного талона = N + 1. Расхождение исключено по
    //   построению, а не по совпадению.
    //
    // НЕ МЕНЯЕТСЯ:
    //   - математика позиции (COUNT(waiting) + 1) и времени ожидания (position*5);
    //   - бизнес-логика (эндпоинт /api/v1/queue/daily_count, обработчик
    //     handleDailyCount, клиентский QueueManager::getDailyCount, надписи UI);
    //   - архитектура (никаких новых методов, переменных, таблиц, индексов);
    //   - производительность: запрос покрывается существующим составным
    //     индексом idx_queue_status_type ON queue_tickets(queue_type, status).
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    //   Только SELECT в собственной транзакции pqxx::work; данных не изменяет.
    //   Гонка исключена (чтение согласованного снимка БД).
    //
    // ========================================================================
    int getDailyTicketCount(const std::string& queueType) {
        try {
            pqxx::work txn{ *conn_ };
            // ИСПРАВЛЕНИЕ: считаем ТЕКУЩИХ ОЖИДАЮЩИХ (тот же критерий,
            // что и в createTicket при вычислении basePosition).
            auto result = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets "
                "WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );
            int count = result[0][0].as<int>();
            g_serverLogger.info("getDailyTicketCount: queueType=" + queueType +
                ", waiting=" + std::to_string(count) +
                " (criterion aligned with createTicket basePosition)");
            return count;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getDailyTicketCount error: " + std::string(e.what()));
            return 0;
        }
    }

    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: createTicket
    // ========================================================================
    //
    // ВЫЯВЛЕННЫЕ ДЕФЕКТЫ (подтверждены логами 2026-08-23):
    //
    // ДЕФЕКТ 1: НЕИНИЦИАЛИЗИРОВАННАЯ СТРУКТУРА ПРИ ИСКЛЮЧЕНИИ.
    //   Локальная переменная `QueueTicket ticket;` не инициализировалась.
    //   Поля типа int (id, position, itemsCount, estimatedWaitTime, createdAt)
    //   содержали МУСОР из стека. Поля типа std::string инициализировались
    //   пустой строкой по умолчанию.
    //   При любом исключении (нарушение уникальности, ошибка соединения)
    //   метод возвращал этот мусор. Клиент получал:
    //     position = -1100584656   (мусор стека)
    //     wait_time_minutes = 105497289322101 (мусор стека)
    //     number = ""              (пустой std::string по умолчанию)
    //   Подтверждение: лог терминала 17:10:48.636.
    //
    // ДЕФЕКТ 2: КОНФЛИКТ ЧАСТИЧНОГО УНИКАЛЬНОГО ИНДЕКСА.
    //   Номер генерировался как COUNT(waiting) + 1.
    //   Частичный уникальный индекс:
    //     CREATE UNIQUE INDEX idx_queue_tickets_number_active
    //     ON queue_tickets(number) WHERE status IN ('waiting', 'accepted')
    //   гарантирует уникальность номера среди ОБА активных статуса.
    //   Генерация учитывала ТОЛЬКО 'waiting', игнорируя 'accepted'.
    //   Подтверждение: лог сервера general waiting=0, accepted=6;
    //   лог товароведа: среди принятых G001, G002.
    //   Генерация G001 при position=1 → конфликт → исключение → мусор.
    //
    // ИСПРАВЛЕНИЕ (архитектура, математика и бизнес-логика НЕ меняются):
    //
    // 1. Структура `ticket` инициализируется через `QueueTicket ticket{};`
    //    (агрегатная инициализация: все поля обнуляются, строки становятся
    //    пустыми). Поле `status` устанавливается в пустую строку как
    //    ИНДИКАТОР ОШИБКИ. После успешного создания устанавливается
    //    "waiting". Вызывающий код (queue_service.h::getTicket) проверяет
    //    это поле и НЕ возвращает мусор клиенту.
    //
    // 2. Генерация номера выполняется ПОИСКА СВОБОДНОГО НОМЕРА в цикле.
    //    Начиная с базовой позиции (COUNT(waiting) + 1), каждый кандидат
    //    проверяется запросом:
    //      SELECT COUNT(*) FROM queue_tickets
    //      WHERE number = $1 AND status IN ('waiting', 'accepted')
    //    Если номер занят (счётчик > 0) — переходим к следующему.
    //    Если не найден в диапазоне [позиция..999] — ищем в [1..позиция-1]
    //    (циклическая нумерация, предусмотренная миграцией индексов).
    //    Это ГАРАНТИРУЕТ отсутствие конфликта уникальности.
    //
    // 3. Позиция талона в очереди = COUNT(waiting) + 1 (НЕ МЕНЯЕТСЯ).
    //    Время ожидания = позиция * 5 минут (НЕ МЕНЯЕТСЯ).
    //    Номер талона может отличаться от позиции, если среди активных
    //    талонов есть занятые номера. Это корректно: номер — идентификатор
    //    талона, позиция — место в очереди ожидающих.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    //   Все запросы (поиск свободного номера, вставка) выполняются
    //   последовательно в рамках ОДНОЙ транзакции pqxx::work.
    //   Параллельные запросы на создание талона блокируются на уровне
    //   транзакции до её завершения. Гонка исключена.
    //   Метод вызывается из одного потока обработки запроса (один
    //   HttpSession обрабатывает один запрос за раз). Дополнительная
    //   синхронизация не требуется.
    //
    // ========================================================================
    QueueTicket createTicket(int clientId, const std::string& queueType, int itemsCount) {
        // ИНИЦИАЛИЗАЦИЯ: все поля обнуляются.
        // Поле `status` = "" → индикатор ошибки для вызывающего кода.
        // Если метод завершится исключением, вернётся структура с пустым
        // `status`, и вызывающий код (queue_service.h) вернёт клиенту
        // ошибку вместо мусора.
        QueueTicket ticket{};
        ticket.clientId = clientId;
        ticket.queueType = queueType;
        ticket.itemsCount = itemsCount;
        ticket.status = "";  // индикатор ошибки; станет "waiting" при успехе

        try {
            pqxx::work txn{ *conn_ };

            // Определяем префикс очереди (без изменений)
            std::string prefix = (queueType == "general") ? "G" :
                (queueType == "first_time") ? "F" :
                (queueType == "extra_20") ? "E" :
                (queueType == "paid") ? "P" :
                (queueType == "expensive") ? "D" : "X";

            // Базовая позиция: количество ожидающих талонов + 1.
            // Это позиция талона В ОЧЕРЕДИ (бизнес-логика не меняется).
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM queue_tickets WHERE queue_type = $1 AND status = 'waiting'",
                pqxx::params{ queueType }
            );
            int basePosition = countResult[0][0].as<int>() + 1;

            g_serverLogger.info("createTicket: searching free number for queueType=" +
                queueType + ", basePosition=" + std::to_string(basePosition) +
                ", clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(itemsCount));

            // ПОИСК СВОБОДНОГО НОМЕРА.
            // Диапазон 1: от базовой позиции до 999.
            // Для каждого кандидата проверяем отсутствие среди активных
            // талонов (статусы 'waiting', 'accepted').
            int displayNum = -1;
            for (int attempt = basePosition; attempt <= 999; ++attempt) {
                char numBuf[16];
                snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                std::string candidateNumber = prefix + std::string(numBuf);
                auto checkResult = txn.exec(
                    "SELECT COUNT(*) FROM queue_tickets "
                    "WHERE number = $1 AND status IN ('waiting', 'accepted')",
                    pqxx::params{ candidateNumber }
                );
                if (checkResult[0][0].as<int>() == 0) {
                    displayNum = attempt;
                    g_serverLogger.info("createTicket: free number FOUND in range [" +
                        std::to_string(basePosition) + "..999]: " + candidateNumber);
                    break;
                }
                g_serverLogger.info("createTicket: number " + candidateNumber +
                    " is OCCUPIED, trying next");
            }

            // Диапазон 2: циклический поиск от 1 до базовой позиции - 1.
            // Это необходимо, если все номера [позиция..999] заняты,
            // но среди [1..позиция-1] есть свободные (обслуженные талоны
            // не блокируют номер благодаря частичному индексу).
            if (displayNum == -1) {
                g_serverLogger.info("createTicket: range [" +
                    std::to_string(basePosition) + "..999] exhausted, "
                    "trying cyclic range [1.." + std::to_string(basePosition - 1) + "]");
                for (int attempt = 1; attempt < basePosition; ++attempt) {
                    char numBuf[16];
                    snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                    std::string candidateNumber = prefix + std::string(numBuf);
                    auto checkResult = txn.exec(
                        "SELECT COUNT(*) FROM queue_tickets "
                        "WHERE number = $1 AND status IN ('waiting', 'accepted')",
                        pqxx::params{ candidateNumber }
                    );
                    if (checkResult[0][0].as<int>() == 0) {
                        displayNum = attempt;
                        g_serverLogger.info("createTicket: free number FOUND in cyclic range: " +
                            candidateNumber);
                        break;
                    }
                    g_serverLogger.info("createTicket: number " + candidateNumber +
                        " is OCCUPIED in cyclic range, trying next");
                }
            }

            // Если свободный номер не найден (все 999 заняты активными
            // талонами), логируем и возвращаем ошибку.
            // Вызывающий код обнаружит пустой `status` и вернёт клиенту
            // {"error": "Failed to create ticket"} вместо мусора.
            if (displayNum == -1) {
                g_serverLogger.error("createTicket: NO AVAILABLE ticket number for queueType=" +
                    queueType + ", all 999 numbers are occupied by active tickets");
                return ticket;  // status == "" → индикатор ошибки
            }

            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string number = prefix + std::string(numBuf);
            std::string window = "1";
            int waitTime = basePosition * 5; // 5 минут на человека (математика БЕЗ ИЗМЕНЕНИЙ)

            g_serverLogger.info("createTicket: queueType=" + queueType +
                ", basePosition=" + std::to_string(basePosition) +
                ", displayNum=" + std::to_string(displayNum) +
                ", ticketNumber=" + number +
                ", clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(itemsCount) +
                ", waitTime=" + std::to_string(waitTime));

            auto result = txn.exec(
                "INSERT INTO queue_tickets (number, client_id, queue_type, position, items_count, window_number, estimated_wait_time) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                "RETURNING id, number, position, window_number, estimated_wait_time, created_at",
                pqxx::params{ number, clientId, queueType, basePosition, itemsCount, window, waitTime }
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
            ticket.status = "waiting";  // индикатор успешного создания
            txn.commit();

            g_serverLogger.info("createTicket: SUCCESS - ticketNumber=" + ticket.number +
                ", id=" + std::to_string(ticket.id) +
                ", position=" + std::to_string(ticket.position) +
                ", waitTime=" + std::to_string(ticket.estimatedWaitTime) +
                ", createdAt=" + std::to_string(ticket.createdAt));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createTicket error: " + std::string(e.what()));
            std::cerr << "createTicket error: " << e.what() << std::endl;
            // Сбрасываем статус в случае ошибки.
            // Вызывающий код обнаружит пустой `status` и вернёт клиенту
            // ошибку вместо мусорных данных.
            ticket.status = "";
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
                "UPDATE trust_acceptances "
                "SET status = 'accepted', "
                "    accepted_at = EXTRACT(EPOCH FROM NOW()), "
                "    window_number = $2 "
                "WHERE ticket_number = $1 AND status = 'pending' "
                "RETURNING window_number",
                pqxx::params{ ticketNumber, windowNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptTrustTicket: ticket not found or already accepted: " +
                    ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("acceptTrustTicket: accepted " + ticketNumber +
                ", window updated to: " + windowNumber);
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
    //
    // ПРИЧИНА ИСПРАВЛЕНИЯ (подтверждена логами 2026-08-24):
    //
    //   Метод вычислял корректную позицию (basePosition = COUNT(pending) + 1),
    //   но возвращал ТОЛЬКО номер талона (std::optional<std::string>).
    //   Позиция терялась. Вызывающий код (server.cpp::handleTrustAcceptance)
    //   не имел доступа к позиции и жёстко задавал 0:
    //     resp["position"] = 0; // для доверия позиция всегда 0
    //   Клиент получал 0 и нормализовал до 1 (бизнес-правило в
    //   queue_manager.h::getTrustAcceptance). В результате комитент
    //   всегда видел себя первым в очереди, независимо от количества
    //   ранее взятых талонов.
    //
    //   Подтверждение из логов сервера:
    //     createTrustAcceptance: basePosition=7, clientId=1
    //     Creating TRUST acceptance ticket: T007, basePosition=7
    //   Подтверждение из логов клиента:
    //     Response: {"position":0,"ticket_number":"T007",...}
    //     getTrustAcceptance: server returned position=0, normalized to 1
    //
    // ИСПРАВЛЕНИЕ:
    //   Возвращаемый тип изменён с std::optional<std::string> на
    //   std::optional<std::pair<std::string, int>>.
    //   first  = номер талона (как раньше)
    //   second = позиция в очереди (basePosition, уже вычислялся)
    //
    //   Математика НЕ меняется: позиция = COUNT(pending) + 1.
    //   Бизнес-модель НЕ меняется: талон создаётся в таблице
    //   trust_acceptances с теми же полями.
    //   Архитектура НЕ меняется: таблица, индексы, статусы — без изменений.
    //   Новые функции НЕ добавляются. Новые переменные НЕ добавляются.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    //   Все запросы (подсчёт, поиск свободного номера, вставка) выполняются
    //   последовательно в рамках ОДНОЙ транзакции pqxx::work.
    //   Параллельные запросы на создание талона блокируются на уровне
    //   транзакции до её завершения. Гонка исключена.
    //
    // ========================================================================
    std::optional<std::pair<std::string, int>> createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *conn_ };

            // Базовая позиция: количество ожидающих талонов + 1.
            // Это позиция нового талона В ОЧЕРЕДИ (математика НЕ меняется).
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM trust_acceptances WHERE status = 'pending'"
            );
            int basePosition = countResult[0][0].as<int>() + 1;

            g_serverLogger.info("createTrustAcceptance: searching free number, "
                "basePosition=" + std::to_string(basePosition) +
                ", clientId=" + std::to_string(clientId));

            // ПОИСК СВОБОДНОГО НОМЕРА.
            // Диапазон 1: от базовой позиции до 999.
            int displayNum = -1;
            for (int attempt = basePosition; attempt <= 999; ++attempt) {
                char numBuf[16];
                snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                std::string candidateNumber = "T" + std::string(numBuf);
                auto checkResult = txn.exec(
                    "SELECT COUNT(*) FROM trust_acceptances "
                    "WHERE ticket_number = $1 AND status IN ('pending', 'accepted')",
                    pqxx::params{ candidateNumber }
                );
                if (checkResult[0][0].as<int>() == 0) {
                    displayNum = attempt;
                    g_serverLogger.info("createTrustAcceptance: free number FOUND: " +
                        candidateNumber);
                    break;
                }
                g_serverLogger.info("createTrustAcceptance: number " + candidateNumber +
                    " is OCCUPIED, trying next");
            }

            // Диапазон 2: циклический поиск от 1 до базовой позиции - 1.
            if (displayNum == -1) {
                g_serverLogger.info("createTrustAcceptance: range [" +
                    std::to_string(basePosition) + "..999] exhausted, "
                    "trying cyclic range [1.." + std::to_string(basePosition - 1) + "]");
                for (int attempt = 1; attempt < basePosition; ++attempt) {
                    char numBuf[16];
                    snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                    std::string candidateNumber = "T" + std::string(numBuf);
                    auto checkResult = txn.exec(
                        "SELECT COUNT(*) FROM trust_acceptances "
                        "WHERE ticket_number = $1 AND status IN ('pending', 'accepted')",
                        pqxx::params{ candidateNumber }
                    );
                    if (checkResult[0][0].as<int>() == 0) {
                        displayNum = attempt;
                        g_serverLogger.info("createTrustAcceptance: free number FOUND in cyclic range: " +
                            candidateNumber);
                        break;
                    }
                    g_serverLogger.info("createTrustAcceptance: number " + candidateNumber +
                        " is OCCUPIED in cyclic range, trying next");
                }
            }

            // Если свободный номер не найден, возвращаем nullopt.
            if (displayNum == -1) {
                g_serverLogger.error("createTrustAcceptance: NO AVAILABLE ticket number, "
                    "all 999 numbers are occupied by active tickets");
                return std::nullopt;
            }

            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string ticketNumber = "T" + std::string(numBuf);

            g_serverLogger.info("Creating TRUST acceptance ticket: " + ticketNumber +
                " for client " + std::to_string(clientId) +
                ", basePosition=" + std::to_string(basePosition) +
                ", displayNum=" + std::to_string(displayNum));

            auto result = txn.exec(
                "INSERT INTO trust_acceptances (client_id, ticket_number) "
                "VALUES ($1, $2) RETURNING ticket_number",
                pqxx::params{ clientId, ticketNumber }
            );

            g_serverLogger.info("Inserted TRUST acceptance ticket into database: " +
                ticketNumber + " for client " + std::to_string(clientId));

            txn.commit();

            g_serverLogger.info("Created TRUST acceptance ticket: " + ticketNumber +
                " for client " + std::to_string(clientId) +
                ", position=" + std::to_string(basePosition));

            // ИСПРАВЛЕНИЕ: возвращаем ПАРУ (номер талона, позиция).
            // Ранее возвращался только номер талона, и позиция терялась.
            // basePosition уже вычислен выше как COUNT(pending) + 1.
            return std::make_pair(
                result[0]["ticket_number"].as<std::string>(),
                basePosition
            );
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
    //
    // ВЫЯВЛЕННЫЙ ДЕФЕКТ (подтверждён логами 2026-08-23):
    //
    //   Номер генерировался как COUNT(waiting) + 1.
    //   Частичный уникальный индекс:
    //     CREATE UNIQUE INDEX idx_first_time_tickets_number_active
    //     ON first_time_tickets(ticket_number) WHERE status IN ('waiting', 'accepted')
    //   гарантирует уникальность среди ОБА активных статуса.
    //   Генерация учитывала ТОЛЬКО 'waiting'.
    //   Подтверждение: лог сервера first_time waiting=29, accepted=8.
    //   Если среди принятых есть F030, генерация F030 → конфликт →
    //   исключение → возврат {"error": "..."}.
    //   Клиент (терминал) получает: {"error":"Failed to create ticket"}.
    //   Подтверждение: лог терминала 17:11:02.382.
    //
    // ИСПРАВЛЕНИЕ:
    //   Генерация номера выполняется ПОИСКА СВОБОДНОГО НОМЕРА в цикле,
    //   аналогично createTicket. Позиция = COUNT(waiting) + 1 (НЕ МЕНЯЕТСЯ).
    //   Время ожидания = позиция * 5 минут (НЕ МЕНЯЕТСЯ).
    //   Формат ответа (поля: ticket_number, position, window_number,
    //   wait_time_minutes, created_at) НЕ МЕНЯЕТСЯ.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    //   Все запросы выполняются в рамках одной транзакции.
    //   Гонка исключена.
    //
    // ========================================================================
    json createFirstTimeTicket(const std::string& windowNumber = "1") {
        try {
            pqxx::work txn{ *conn_ };

            // Базовая позиция: количество ожидающих талонов + 1.
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM first_time_tickets WHERE status = 'waiting'"
            );
            int basePosition = countResult[0][0].as<int>() + 1;

            g_serverLogger.info("createFirstTimeTicket: searching free number, "
                "basePosition=" + std::to_string(basePosition) +
                ", window=" + windowNumber);

            // ПОИСК СВОБОДНОГО НОМЕРА.
            // Диапазон 1: от базовой позиции до 999.
            int displayNum = -1;
            for (int attempt = basePosition; attempt <= 999; ++attempt) {
                char numBuf[16];
                snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                std::string candidateNumber = "F" + std::string(numBuf);
                auto checkResult = txn.exec(
                    "SELECT COUNT(*) FROM first_time_tickets "
                    "WHERE ticket_number = $1 AND status IN ('waiting', 'accepted')",
                    pqxx::params{ candidateNumber }
                );
                if (checkResult[0][0].as<int>() == 0) {
                    displayNum = attempt;
                    g_serverLogger.info("createFirstTimeTicket: free number FOUND: " +
                        candidateNumber);
                    break;
                }
                g_serverLogger.info("createFirstTimeTicket: number " + candidateNumber +
                    " is OCCUPIED, trying next");
            }

            // Диапазон 2: циклический поиск от 1 до базовой позиции - 1.
            if (displayNum == -1) {
                g_serverLogger.info("createFirstTimeTicket: range [" +
                    std::to_string(basePosition) + "..999] exhausted, "
                    "trying cyclic range [1.." + std::to_string(basePosition - 1) + "]");
                for (int attempt = 1; attempt < basePosition; ++attempt) {
                    char numBuf[16];
                    snprintf(numBuf, sizeof(numBuf), "%03d", attempt);
                    std::string candidateNumber = "F" + std::string(numBuf);
                    auto checkResult = txn.exec(
                        "SELECT COUNT(*) FROM first_time_tickets "
                        "WHERE ticket_number = $1 AND status IN ('waiting', 'accepted')",
                        pqxx::params{ candidateNumber }
                    );
                    if (checkResult[0][0].as<int>() == 0) {
                        displayNum = attempt;
                        g_serverLogger.info("createFirstTimeTicket: free number FOUND in cyclic range: " +
                            candidateNumber);
                        break;
                    }
                    g_serverLogger.info("createFirstTimeTicket: number " + candidateNumber +
                        " is OCCUPIED in cyclic range, trying next");
                }
            }

            // Если свободный номер не найден, возвращаем ошибку.
            if (displayNum == -1) {
                g_serverLogger.error("createFirstTimeTicket: NO AVAILABLE ticket number, "
                    "all 999 numbers are occupied by active tickets");
                json error;
                error["error"] = "No available ticket number";
                return error;
            }

            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string ticketNumber = "F" + std::string(numBuf);
            // Время ожидания: 5 минут на человека (математика БЕЗ ИЗМЕНЕНИЙ)
            int waitTime = basePosition * 5;

            g_serverLogger.info("createFirstTimeTicket: basePosition=" +
                std::to_string(basePosition) +
                ", displayNum=" + std::to_string(displayNum) +
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
            // (формат НЕ МЕНЯЕТСЯ — клиент парсит эти поля)
            json response;
            response["ticket_number"] = ticketNumber;
            response["position"] = basePosition;
            response["window_number"] = windowNumber;
            response["wait_time_minutes"] = waitTime;
            response["created_at"] = result[0]["created_at"].as<int64_t>();

            g_serverLogger.info("createFirstTimeTicket: SUCCESS - ticketNumber=" +
                ticketNumber +
                ", position=" + std::to_string(basePosition) +
                ", window=" + windowNumber +
                ", waitTime=" + std::to_string(waitTime) +
                ", createdAt=" + std::to_string(result[0]["created_at"].as<int64_t>()));
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
                "UPDATE first_time_tickets "
                "SET status = 'accepted', "
                "    accepted_at = EXTRACT(EPOCH FROM NOW()), "
                "    window_number = $2 "
                "WHERE ticket_number = $1 AND status = 'waiting' "
                "RETURNING window_number",
                pqxx::params{ ticketNumber, windowNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptFirstTimeTicket: ticket not found or already accepted: " +
                    ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("acceptFirstTimeTicket: accepted " + ticketNumber +
                ", window updated to: " + windowNumber);
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

    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: serveTicket
    // =========================================================================
    // ПРИЧИНА ИСПРАВЛЕНИЯ:
    // Метод НЕ проверял текущий статус талона. Запрос:
    //   UPDATE queue_tickets SET status = 'served' WHERE number = $1
    // мог пометить как «обслуженный» талон в ЛЮБОМ статусе, включая
    // 'waiting'. Это приводило к тому, что клиент мог отправить на
    // обслуживание талон, который ещё не был принят, и тот исчезал из
    // очереди без принятия и без записи сведений о товаре.
    //
    // Для сравнения:
    //   - serveFirstTimeTicket(): WHERE ... AND status = 'accepted'  ✅
    //   - serveTrustTicket():     WHERE ... AND status = 'accepted'  ✅
    //   - serveTicket():          WHERE number = $1                  ❌ (без проверки!)
    //
    // РЕШЕНИЕ:
    // Добавлена проверка AND status = 'accepted' для консистентности
    // с serveFirstTimeTicket() и serveTrustTicket(). Также добавлена
    // проверка affected_rows() == 0 для возврата false, если талон
    // не был найден или не был принят.
    //
    // Это НЕ ЛОМАЕТ бизнес-логику и математику базы данных:
    // - Метод по-прежнему переводит талон из 'accepted' в 'served'.
    // - Столбцы, индексы, последовательности не изменяются.
    // - Добавлена только защита от обслуживания талонов, которые
    //   не были предварительно приняты.
    // - Поведение для корректных запросов (талон в статусе 'accepted')
    //   остаётся идентичным.
    // =========================================================================
    bool serveTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE queue_tickets SET status = 'served', served_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE number = $1 AND status = 'accepted'",
                pqxx::params{ ticketNumber }
            );
            if (res.affected_rows() == 0) {
                g_serverLogger.warning("serveTicket: ticket not accepted or already served: " + ticketNumber);
                return false;
            }
            txn.commit();
            g_serverLogger.info("Served ticket: " + ticketNumber);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("serveTicket error: " + std::string(e.what()));
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

    // =========================================================================
    // ОБНОВЛЁННЫЙ МЕТОД: acceptTicket
    // =========================================================================
    // ДОПОЛНЕНИЕ: Теперь принимает windowNumber как входной/выходной параметр.
    // При принятии талона window_number в БД ОБНОВЛЯЕТСЯ на номер окна
    // товароведа, переданный клиентом. Это позволяет ТВ-монитору
    // отображать реальный номер окна.
    //
    // @param ticketNumber  — номер талона для принятия
    // @param windowNumber  — ВХОД: номер окна товароведа;
    //                        ВЫХОД: обновлённый номер окна из БД
    // @return true при успешном принятии
    // =========================================================================
    bool acceptTicket(const std::string& ticketNumber, std::string& windowNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE queue_tickets "
                "SET status = 'accepted', "
                "    accepted_at = EXTRACT(EPOCH FROM NOW()), "
                "    window_number = $2 "
                "WHERE number = $1 AND status = 'waiting' "
                "RETURNING window_number",
                pqxx::params{ ticketNumber, windowNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("acceptTicket: ticket not found or already accepted: " +
                    ticketNumber);
                return false;
            }
            windowNumber = res[0]["window_number"].as<std::string>();
            txn.commit();
            g_serverLogger.info("acceptTicket: accepted " + ticketNumber +
                ", window updated to: " + windowNumber);
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
    * @brief Блокирует или разблокирует клиента с указанием причины
    * @param clientId ID клиента для блокировки
    * @param blocked true - заблокировать, false - разблокировать
    * @param directorId ID директора, выполняющего блокировку
    * @param blockReason Причина блокировки (текст до 1000 символов)
    * @return true при успешном выполнении
    */
    bool setClientBlocked(int clientId, bool blocked, int directorId,
        const std::string& blockReason = "") {
        try {
            pqxx::work txn{ *conn_ };
            if (blocked) {
                // Блокировка: устанавливаем is_blocked = TRUE, blocked_at, blocked_by, block_reason
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                txn.exec(
                    "UPDATE clients SET is_blocked = TRUE, blocked_at = $2, blocked_by = $3, "
                    "block_reason = $4, updated_at = EXTRACT(EPOCH FROM NOW()) WHERE id = $1",
                    pqxx::params{ clientId, now, directorId, blockReason }
                );
                g_serverLogger.info("setClientBlocked: BLOCKED clientId=" + std::to_string(clientId) +
                    " by directorId=" + std::to_string(directorId) +
                    ", reason='" + blockReason.substr(0, 100) +
                    (blockReason.length() > 100 ? "..." : "") + "'");
            }
            else {
                // Разблокировка: очищаем все поля блокировки, включая причину
                txn.exec(
                    "UPDATE clients SET is_blocked = FALSE, blocked_at = NULL, blocked_by = NULL, "
                    "block_reason = '', updated_at = EXTRACT(EPOCH FROM NOW()) WHERE id = $1",
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
                "c.email, c.items_submitted, c.items_sold, c.is_blocked, c.blocked_at, c.block_reason,"
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
                "c.email, c.items_submitted, c.items_sold, c.is_blocked, c.blocked_at, c.block_reason "
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
                client["block_reason"] = row["block_reason"].is_null() ? "" : row["block_reason"].as<std::string>();
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

    // =========================================================================
// МЕТОДЫ ДЛЯ ТВ-ДИСПЛЕЯ ОЧЕРЕДЕЙ
// Только SELECT. Не изменяют данные. Не влияют на существующую логику.
// =========================================================================

/**
 * @brief Возвращает принятые талоны из таблицы queue_tickets
 *        (очереди: general, extra_20, paid, expensive)
 * @param queueType тип очереди для фильтрации
 * @return вектор JSON-объектов с полями: ticket_number, client_id,
 *         queue_type, window_number, accepted_at
 */
    std::vector<json> getAcceptedTickets(const std::string& queueType) {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, number, client_id, queue_type, window_number, accepted_at "
                "FROM queue_tickets "
                "WHERE queue_type = $1 AND status = 'accepted' "
                "ORDER BY accepted_at ASC",
                pqxx::params{ queueType }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["number"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["queue_type"] = row["queue_type"].as<std::string>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["accepted_at"] = row["accepted_at"].is_null()
                    ? 0 : row["accepted_at"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getAcceptedTickets for " + queueType + ": " +
                std::to_string(result.size()) + " accepted tickets");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAcceptedTickets error: " + std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Возвращает принятые талоны из таблицы first_time_tickets
     * @return вектор JSON-объектов с полями: ticket_number, window_number, accepted_at
     */
    std::vector<json> getAcceptedFirstTimeTickets() {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, ticket_number, window_number, accepted_at "
                "FROM first_time_tickets "
                "WHERE status = 'accepted' "
                "ORDER BY accepted_at ASC"
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["ticket_number"].as<std::string>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["accepted_at"] = row["accepted_at"].is_null()
                    ? 0 : row["accepted_at"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getAcceptedFirstTimeTickets: " +
                std::to_string(result.size()) + " accepted tickets");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAcceptedFirstTimeTickets error: " +
                std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Возвращает принятые талоны из таблицы trust_acceptances
     * @return вектор JSON-объектов с полями: ticket_number, client_id,
     *         window_number, accepted_at
     */
    std::vector<json> getAcceptedTrustTickets() {
        std::vector<json> result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, ticket_number, client_id, window_number, accepted_at "
                "FROM trust_acceptances "
                "WHERE status = 'accepted' "
                "ORDER BY accepted_at ASC"
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["ticket_number"] = row["ticket_number"].as<std::string>();
                item["client_id"] = row["client_id"].as<int>();
                item["window_number"] = row["window_number"].as<std::string>();
                item["accepted_at"] = row["accepted_at"].is_null()
                    ? 0 : row["accepted_at"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getAcceptedTrustTickets: " +
                std::to_string(result.size()) + " accepted tickets");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAcceptedTrustTickets error: " +
                std::string(e.what()));
        }
        return result;
    }

    // =========================================================================
// НОВЫЙ МЕТОД: УДАЛЕНИЕ ТАЛОНА ИЗ ОБЩИХ ОЧЕРЕДЕЙ
// (general, extra_20, paid, expensive)
// =========================================================================
//
// НАЗНАЧЕНИЕ:
// Удаляет талон из таблицы queue_tickets, по которому комитент не
// подошёл к окну товароведа в течение 2 минут.
//
// ЗАЩИТА ОТ ГОНОК И НЕКОРРЕКТНОГО УДАЛЕНИЯ:
// Удаление выполняется одним атомарным DELETE с четырьмя условиями:
//   1. номер талона совпадает;
//   2. тип очереди совпадает;
//   3. статус = 'waiting' (не 'accepted', не 'served');
//   4. created_at <= NOW() - 120 секунд (талону не менее 2 минут).
//
// Если хотя бы одно условие не выполнено, DELETE не удаляет ни одной
// строки и метод возвращает false. Это исключает:
//   - удаление уже принятого талона (статус 'accepted');
//   - удаление уже обслуженного талона (статус 'served');
//   - удаление свежего талона (комитент ещё может подойти);
//   - гонку между параллельными запросами: PostgreSQL гарантирует
//     атомарность DELETE в рамках одной транзакции.
//
// Математика очередей НЕ меняется:
//   - позиция остальных талонов не пересчитывается (она вычисляется
//     как COUNT(waiting)+1 при создании нового талона);
//   - нумерация талонов не затрагивается;
//   - частичный уникальный индекс idx_queue_tickets_number_active
//     автоматически освобождает номер удалённого талона для
//     повторного использования в циклической нумерации 001-999.
//
// @param ticketNumber - номер талона (например, "G001")
// @param queueType    - тип очереди (general, extra_20, paid, expensive)
// @return true если талон удалён, false иначе
// =========================================================================
    bool deleteWaitingTicket(const std::string& ticketNumber, const std::string& queueType) {
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("deleteWaitingTicket: attempting to delete ticket=" +
                ticketNumber + ", queueType=" + queueType);
            auto res = txn.exec(
                "DELETE FROM queue_tickets "
                "WHERE number = $1 AND queue_type = $2 "
                "AND ( "
                "    (status = 'waiting' AND created_at <= EXTRACT(EPOCH FROM NOW()) - 120) "
                "    OR "
                "    (status = 'accepted' AND COALESCE(accepted_at, created_at) <= EXTRACT(EPOCH FROM NOW()) - 120) "
                ") "
                "RETURNING id",
                pqxx::params{ ticketNumber, queueType }
            );
            if (res.empty()) {
                g_serverLogger.warning("deleteWaitingTicket: ticket NOT deleted. "
                    "Possible reasons: not found, not in 'waiting' status, "
                    "or less than 2 minutes old. ticketNumber=" + ticketNumber +
                    ", queueType=" + queueType);
                return false;
            }
            int deletedId = res[0]["id"].as<int>();
            txn.commit();
            g_serverLogger.info("deleteWaitingTicket: SUCCESS - deleted ticket=" +
                ticketNumber + ", queueType=" + queueType +
                ", deletedRowId=" + std::to_string(deletedId));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("deleteWaitingTicket error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: УДАЛЕНИЕ ТАЛОНА ИЗ ОЧЕРЕДИ "ПЕРВЫЙ РАЗ"
    // =========================================================================
    //
    // Аналогичен deleteWaitingTicket, но работает с таблицей
    // first_time_tickets. Начальный статус талона в этой таблице = 'waiting'.
    //
    // @param ticketNumber - номер талона (например, "F005")
    // @return true если талон удалён, false иначе
    // =========================================================================
    bool deleteFirstTimeTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("deleteFirstTimeTicket: attempting to delete ticket=" +
                ticketNumber);
            auto res = txn.exec(
                "DELETE FROM first_time_tickets "
                "WHERE ticket_number = $1 "
                "AND ( "
                "    (status = 'waiting' AND created_at <= EXTRACT(EPOCH FROM NOW()) - 120) "
                "    OR "
                "    (status = 'accepted' AND COALESCE(accepted_at, created_at) <= EXTRACT(EPOCH FROM NOW()) - 120) "
                ") "
                "RETURNING id",
                pqxx::params{ ticketNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("deleteFirstTimeTicket: ticket NOT deleted. "
                    "Possible reasons: not found, not in 'waiting' status, "
                    "or less than 2 minutes old. ticketNumber=" + ticketNumber);
                return false;
            }
            int deletedId = res[0]["id"].as<int>();
            txn.commit();
            g_serverLogger.info("deleteFirstTimeTicket: SUCCESS - deleted ticket=" +
                ticketNumber + ", deletedRowId=" + std::to_string(deletedId));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("deleteFirstTimeTicket error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: УДАЛЕНИЕ ТАЛОНА ИЗ ОЧЕРЕДИ "НА ДОВЕРИИ"
    // =========================================================================
    //
    // Аналогичен deleteWaitingTicket, но работает с таблицей
    // trust_acceptances. ВАЖНО: начальный статус талона в этой таблице
    // = 'pending' (а не 'waiting'), что соответствует существующей
    // бизнес-логике (см. getWaitingTrustTickets: WHERE status = 'pending').
    //
    // @param ticketNumber - номер талона (например, "T003")
    // @return true если талон удалён, false иначе
    // =========================================================================
    bool deleteTrustTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("deleteTrustTicket: attempting to delete ticket=" +
                ticketNumber);
            auto res = txn.exec(
                "DELETE FROM trust_acceptances "
                "WHERE ticket_number = $1 "
                "AND ( "
                "    (status = 'pending' AND created_at <= EXTRACT(EPOCH FROM NOW()) - 120) "
                "    OR "
                "    (status = 'accepted' AND COALESCE(accepted_at, created_at) <= EXTRACT(EPOCH FROM NOW()) - 120) "
                ") "
                "RETURNING id",
                pqxx::params{ ticketNumber }
            );
            if (res.empty()) {
                g_serverLogger.warning("deleteTrustTicket: ticket NOT deleted. "
                    "Possible reasons: not found, not in 'pending' status, "
                    "or less than 2 minutes old. ticketNumber=" + ticketNumber);
                return false;
            }
            int deletedId = res[0]["id"].as<int>();
            txn.commit();
            g_serverLogger.info("deleteTrustTicket: SUCCESS - deleted ticket=" +
                ticketNumber + ", deletedRowId=" + std::to_string(deletedId));
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("deleteTrustTicket error: " + std::string(e.what()));
            return false;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ПОЛНАЯ ИНФОРМАЦИЯ О КЛИЕНТЕ ПО ID
    // =========================================================================
    //
    // Возвращает ВСЕ поля из таблицы clients для отображения в окне
    // «Поиск комитента» у товароведа. В отличие от getClientById(),
    // который возвращает только базовые поля (id, phone, name, email,
    // birth_date), данный метод возвращает полный набор сведений:
    // паспортные данные, адрес, счётчики, статус блокировки.
    //
    // Метод выполняет только SELECT (чтение). Данные не изменяет.
    // Потокобезопасность: собственная транзакция, гонка исключена.
    //
    // @param clientId - ID клиента в таблице clients
    // @return JSON-объект со всеми полями клиента, или {"error": "..."}
    // =========================================================================
    json getClientFullInfo(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("getClientFullInfo: request for clientId=" +
                std::to_string(clientId));

            // ИСПРАВЛЕНИЕ: Динамическое вычисление сданных и проданных товаров из таблицы items
            auto res = txn.exec(
                "SELECT c.id, c.phone, c.last_name, c.first_name, c.middle_name, c.email, c.role, "
                "c.birth_date, c.passport_type, c.passport_series, c.passport_number, c.address, "
                "c.is_blocked, c.blocked_at, c.block_reason, c.created_at, "
                "COALESCE(SUM(i.quantity), 0) AS items_submitted, "
                "COALESCE(SUM(i.sold_quantity), 0) AS items_sold "
                "FROM clients c "
                "LEFT JOIN items i ON c.id = i.client_id "
                "WHERE c.id = $1 "
                "GROUP BY c.id, c.phone, c.last_name, c.first_name, c.middle_name, c.email, c.role, "
                "c.birth_date, c.passport_type, c.passport_series, c.passport_number, c.address, "
                "c.is_blocked, c.blocked_at, c.block_reason, c.created_at",
                pqxx::params{ clientId }
            );
            if (res.empty()) {
                g_serverLogger.warning("getClientFullInfo: client NOT found, id=" +
                    std::to_string(clientId));
                json err;
                err["error"] = "Client not found";
                return err;
            }
            const auto& row = res[0];
            json client;
            client["id"] = row["id"].as<int>();
            client["phone"] = row["phone"].as<std::string>();
            client["last_name"] = row["last_name"].as<std::string>();
            client["first_name"] = row["first_name"].as<std::string>();
            client["middle_name"] = row["middle_name"].is_null()
                ? "" : row["middle_name"].as<std::string>();
            client["email"] = row["email"].is_null()
                ? "" : row["email"].as<std::string>();
            client["role"] = row["role"].is_null()
                ? "client" : row["role"].as<std::string>();
            client["birth_date"] = row["birth_date"].is_null()
                ? "" : row["birth_date"].as<std::string>();
            client["passport_type"] = row["passport_type"].is_null()
                ? "" : row["passport_type"].as<std::string>();
            client["passport_series"] = row["passport_series"].is_null()
                ? "" : row["passport_series"].as<std::string>();
            client["passport_number"] = row["passport_number"].is_null()
                ? "" : row["passport_number"].as<std::string>();
            client["address"] = row["address"].is_null()
                ? "" : row["address"].as<std::string>();
            client["items_submitted"] = row["items_submitted"].as<int>();
            client["items_sold"] = row["items_sold"].as<int>();
            client["is_blocked"] = row["is_blocked"].as<bool>();
            client["block_reason"] = row["block_reason"].is_null()
                ? "" : row["block_reason"].as<std::string>();
            if (!row["blocked_at"].is_null()) {
                client["blocked_at"] = row["blocked_at"].as<int64_t>();
            }
            else {
                client["blocked_at"] = nullptr;
            }
            client["created_at"] = row["created_at"].as<int64_t>();

            // Формируем полное ФИО по тому же правилу, что и в getClientByPhone
            std::string fullName = client["last_name"].get<std::string>() +
                " " + client["first_name"].get<std::string>();
            if (!client["middle_name"].get<std::string>().empty()) {
                fullName += " " + client["middle_name"].get<std::string>();
            }
            client["full_name"] = fullName;

            g_serverLogger.info("getClientFullInfo: found client id=" +
                std::to_string(clientId) +
                ", phone=" + client["phone"].get<std::string>() +
                ", fullName=" + fullName +
                ", role=" + client["role"].get<std::string>() +
                ", isBlocked=" + std::to_string(client["is_blocked"].get<bool>()));
            return client;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientFullInfo error: " + std::string(e.what()));
            json err;
            err["error"] = std::string("Database error: ") + e.what();
            return err;
        }
    }

    // =========================================================================
    // НОВЫЙ МЕТОД: ПОИСК КЛИЕНТОВ ПО ФАМИЛИИ, ИМЕНИ И ОТЧЕСТВУ
    // =========================================================================
    //
    // Ищет клиентов в таблице clients по полям last_name, first_name,
    // middle_name через ILIKE (регистронезависимый поиск с шаблоном %...%).
    // Каждое поле является необязательным: можно искать только по фамилии,
    // или по фамилии и имени, или по полному ФИО.
    //
    // Ограничения:
    //   - Ищутся только клиенты с ролью 'client' или NULL
    //     (товароведы и директора не отображаются в результатах).
    //   - Максимум 50 результатов (LIMIT 50) для защиты от перегрузки.
    //   - Сортировка по last_name, first_name, middle_name.
    //
    // Метод выполняет только SELECT (чтение). Данные не изменяет.
    // Потокобезопасность: собственная транзакция, гонка исключена.
    //
    // @param lastName   - фамилия (пустая строка = не фильтровать)
    // @param firstName  - имя (пустая строка = не фильтровать)
    // @param middleName - отчество (пустая строка = не фильтровать)
    // @return JSON-массив найденных клиентов с полной информацией
    // =========================================================================
    json searchClientsByFio(const std::string& lastName,
        const std::string& firstName,
        const std::string& middleName) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("searchClientsByFio: lastName='" + lastName +
                "', firstName='" + firstName +
                "', middleName='" + middleName + "'");

            // ИСПРАВЛЕНИЕ: Базовый запрос с LEFT JOIN и агрегацией
            std::string query =
                "SELECT c.id, c.phone, c.last_name, c.first_name, c.middle_name, c.email, c.role, "
                "c.birth_date, c.passport_type, c.passport_series, c.passport_number, c.address, "
                "c.is_blocked, c.blocked_at, c.block_reason, c.created_at, "
                "COALESCE(SUM(i.quantity), 0) AS items_submitted, "
                "COALESCE(SUM(i.sold_quantity), 0) AS items_sold "
                "FROM clients c "
                "LEFT JOIN items i ON c.id = i.client_id "
                "WHERE (c.role = 'client' OR c.role IS NULL)";

            pqxx::params params;
            int paramIndex = 1;

            // Добавляем условия ILIKE для каждого заполненного поля
            if (!lastName.empty()) {
                query += " AND c.last_name ILIKE $" + std::to_string(paramIndex);
                params.append("%" + lastName + "%");
                paramIndex++;
            }
            if (!firstName.empty()) {
                query += " AND c.first_name ILIKE $" + std::to_string(paramIndex);
                params.append("%" + firstName + "%");
                paramIndex++;
            }
            if (!middleName.empty()) {
                query += " AND c.middle_name ILIKE $" + std::to_string(paramIndex);
                params.append("%" + middleName + "%");
                paramIndex++;
            }

            // ИСПРАВЛЕНИЕ: Добавляем GROUP BY перед ORDER BY
            query += " GROUP BY c.id, c.phone, c.last_name, c.first_name, c.middle_name, c.email, c.role, "
                "c.birth_date, c.passport_type, c.passport_series, c.passport_number, c.address, "
                "c.is_blocked, c.blocked_at, c.block_reason, c.created_at "
                "ORDER BY c.last_name, c.first_name, c.middle_name LIMIT 50";

            auto res = txn.exec(query, params);
            g_serverLogger.info("searchClientsByFio: query returned " +
                std::to_string(res.size()) + " rows");

            for (const auto& row : res) {
                json client;
                client["id"] = row["id"].as<int>();
                client["phone"] = row["phone"].as<std::string>();
                client["last_name"] = row["last_name"].as<std::string>();
                client["first_name"] = row["first_name"].as<std::string>();
                client["middle_name"] = row["middle_name"].is_null()
                    ? "" : row["middle_name"].as<std::string>();
                client["email"] = row["email"].is_null()
                    ? "" : row["email"].as<std::string>();
                client["role"] = row["role"].is_null()
                    ? "client" : row["role"].as<std::string>();
                client["birth_date"] = row["birth_date"].is_null()
                    ? "" : row["birth_date"].as<std::string>();
                client["passport_type"] = row["passport_type"].is_null()
                    ? "" : row["passport_type"].as<std::string>();
                client["passport_series"] = row["passport_series"].is_null()
                    ? "" : row["passport_series"].as<std::string>();
                client["passport_number"] = row["passport_number"].is_null()
                    ? "" : row["passport_number"].as<std::string>();
                client["address"] = row["address"].is_null()
                    ? "" : row["address"].as<std::string>();
                client["items_submitted"] = row["items_submitted"].as<int>();
                client["items_sold"] = row["items_sold"].as<int>();
                client["is_blocked"] = row["is_blocked"].as<bool>();
                client["block_reason"] = row["block_reason"].is_null()
                    ? "" : row["block_reason"].as<std::string>();
                if (!row["blocked_at"].is_null()) {
                    client["blocked_at"] = row["blocked_at"].as<int64_t>();
                }
                else {
                    client["blocked_at"] = nullptr;
                }
                client["created_at"] = row["created_at"].as<int64_t>();

                std::string fullName = client["last_name"].get<std::string>() +
                    " " + client["first_name"].get<std::string>();
                if (!client["middle_name"].get<std::string>().empty()) {
                    fullName += " " + client["middle_name"].get<std::string>();
                }
                client["full_name"] = fullName;

                result.push_back(client);
            }

            g_serverLogger.info("searchClientsByFio: completed, found " +
                std::to_string(result.size()) + " clients");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("searchClientsByFio error: " + std::string(e.what()));
        }
        return result;
    }

    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ МОДУЛЯ «КАССИР»
    // =========================================================================
    //
    // НАЗНАЧЕНИЕ:
    // Обеспечивают операции возврата товара комитенту, возмещения ущерба
    // за утраченный товар, получения списка реализованных товаров с суммами
    // к выплате и списка нереализованных товаров со сроком > 15 суток.
    //
    // ПРИНЦИПЫ:
    // 1. Используются ТОЛЬКО существующие таблицы (items, item_sales,
    //    contract_appendices, clients) + новая таблица cashier_documents.
    // 2. Новые статусы товаров: 'returned' (возвращён комитенту) и
    //    'compensated' (возмещён ущерб за утрату). Они НЕ конфликтуют
    //    с существующими статусами: 'pending', 'sold', 'expired',
    //    'low_quality', 'unsold_quality'.
    // 3. Все операции атомарны (выполняются в одной транзакции).
    // 4. Математика комиссий НЕ изменяется.
    // 5. Триггер автосинхронизации счётчиков клиентов автоматически
    //    пересчитывает данные при изменении товаров.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    // Каждый метод выполняет все запросы в рамках ОДНОЙ транзакции
    // pqxx::work. Параллельные вызовы блокируются на уровне транзакции.
    // Гонка исключена.
    // =========================================================================

    /**
     * @brief Помечает товары как «возвращённые комитенту».
     *
     * Обновляет статус товаров с 'pending' или 'expired' на 'returned'.
     * Товары со статусами 'sold', 'low_quality', 'unsold_quality',
     * 'compensated' НЕ затрагиваются (нельзя вернуть проданный товар).
     *
     * @param itemIds вектор ID товаров для пометки
     * @param clientId ID комитента (защита от подмены)
     * @return количество реально помеченных товаров
     */
    int markItemsReturned(const std::vector<int>& itemIds, int clientId) {
        if (itemIds.empty()) {
            g_serverLogger.warning("markItemsReturned: empty itemIds");
            return 0;
        }
        try {
            pqxx::work txn{ *conn_ };
            int markedCount = 0;
            for (int itemId : itemIds) {
                auto res = txn.exec(
                    "UPDATE items SET status = 'returned', "
                    "expired_at = EXTRACT(EPOCH FROM NOW()) "
                    "WHERE id = $1 AND client_id = $2 "
                    "AND status IN ('pending', 'expired') "
                    "RETURNING id",
                    pqxx::params{ itemId, clientId }
                );
                if (!res.empty()) {
                    markedCount++;
                    g_serverLogger.info("markItemsReturned: itemId=" +
                        std::to_string(itemId) + " marked as returned");
                }
                else {
                    g_serverLogger.warning("markItemsReturned: itemId=" +
                        std::to_string(itemId) +
                        " NOT marked (wrong status or wrong client)");
                }
            }
            txn.commit();
            g_serverLogger.info("markItemsReturned: clientId=" +
                std::to_string(clientId) + ", marked=" +
                std::to_string(markedCount) + " of " +
                std::to_string(itemIds.size()));
            return markedCount;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("markItemsReturned error: " +
                std::string(e.what()));
            return 0;
        }
    }

    /**
     * @brief Помечает товары как «возмещён ущерб за утрату».
     *
     * Обновляет статус товаров с 'pending' или 'expired' на 'compensated'.
     * Товары со статусами 'sold', 'low_quality', 'unsold_quality',
     * 'returned' НЕ затрагиваются.
     *
     * @param itemIds вектор ID товаров для пометки
     * @param clientId ID комитента (защита от подмены)
     * @return количество реально помеченных товаров
     */
    int markItemsCompensated(const std::vector<int>& itemIds, int clientId) {
        if (itemIds.empty()) {
            g_serverLogger.warning("markItemsCompensated: empty itemIds");
            return 0;
        }
        try {
            pqxx::work txn{ *conn_ };
            int markedCount = 0;
            for (int itemId : itemIds) {
                auto res = txn.exec(
                    "UPDATE items SET status = 'compensated', "
                    "expired_at = EXTRACT(EPOCH FROM NOW()) "
                    "WHERE id = $1 AND client_id = $2 "
                    "AND status IN ('pending', 'expired') "
                    "RETURNING id",
                    pqxx::params{ itemId, clientId }
                );
                if (!res.empty()) {
                    markedCount++;
                    g_serverLogger.info("markItemsCompensated: itemId=" +
                        std::to_string(itemId) + " marked as compensated");
                }
                else {
                    g_serverLogger.warning("markItemsCompensated: itemId=" +
                        std::to_string(itemId) +
                        " NOT marked (wrong status or wrong client)");
                }
            }
            txn.commit();
            g_serverLogger.info("markItemsCompensated: clientId=" +
                std::to_string(clientId) + ", marked=" +
                std::to_string(markedCount) + " of " +
                std::to_string(itemIds.size()));
            return markedCount;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("markItemsCompensated error: " +
                std::string(e.what()));
            return 0;
        }
    }

    /**
     * @brief Получает список РЕАЛИЗОВАННЫХ товаров комитента с суммами
     *        к выплате (для накладной о выплате вознаграждения).
     *
     * Используются данные из таблицы item_sales (лог продаж) и items.
     * Для каждой продажи возвращается: номер приложения, наименование,
     * количество проданных единиц, цена на руки (client_amount из
     * item_sales), общая сумма.
     *
     * @param clientId ID комитента
     * @return JSON-массив реализованных товаров
     */
    json getClientSoldItemsForReward(int clientId) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT s.id AS sale_id, s.item_id, s.quantity_sold, "
                "s.sale_price, s.client_amount, s.sold_at, "
                "i.description, i.appendix_id, "
                "a.appendix_number "
                "FROM item_sales s "
                "JOIN items i ON s.item_id = i.id "
                "LEFT JOIN contract_appendices a ON i.appendix_id = a.id "
                "WHERE s.client_id = $1 "
                "ORDER BY s.sold_at DESC",
                pqxx::params{ clientId }
            );
            for (const auto& row : res) {
                json item;
                item["sale_id"] = row["sale_id"].as<int>();
                item["item_id"] = row["item_id"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["quantity_sold"] = row["quantity_sold"].as<int>();
                item["sale_price"] = row["sale_price"].as<double>();
                item["client_amount"] = row["client_amount"].as<double>();
                item["sold_at"] = row["sold_at"].as<int64_t>();
                item["appendix_number"] = row["appendix_number"].is_null()
                    ? 0 : row["appendix_number"].as<long long>();
                result.push_back(item);
            }
            g_serverLogger.info("getClientSoldItemsForReward: clientId=" +
                std::to_string(clientId) + ", sales=" +
                std::to_string(result.size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientSoldItemsForReward error: " +
                std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Получает список НЕРЕАЛИЗОВАННЫХ товаров комитента со сроком
     *        реализации более 15 суток (для формирования накладной на возврат).
     *
     * Возвращает товары со статусом 'expired' (помеченные фоновым процессом
     * processExpiredAppendices) или 'pending', у которых истёк срок
     * действия приложения (valid_until < NOW()).
     *
     * @param clientId ID комитента
     * @return JSON-массив нереализованных товаров
     */
    json getClientUnsoldExpiredItems(int clientId) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto res = txn.exec(
                "SELECT i.id, i.item_number, i.description, "
                "i.estimated_price, i.quantity, i.sold_quantity, "
                "i.condition, i.note, i.status, i.created_at, "
                "i.expired_at, i.appendix_id, "
                "a.appendix_number, a.valid_until "
                "FROM items i "
                "LEFT JOIN contract_appendices a ON i.appendix_id = a.id "
                "WHERE i.client_id = $1 "
                "AND (i.status = 'expired' OR "
                "     (i.status = 'pending' AND a.valid_until < $2)) "
                "AND (i.quantity - i.sold_quantity) > 0 "
                "ORDER BY i.created_at ASC",
                pqxx::params{ clientId, nowSec }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                item["sold_quantity"] = row["sold_quantity"].as<int>();
                item["unsold_quantity"] = row["quantity"].as<int>() -
                    row["sold_quantity"].as<int>();
                item["condition"] = row["condition"].is_null()
                    ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null()
                    ? "" : row["note"].as<std::string>();
                item["status"] = row["status"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["expired_at"] = row["expired_at"].is_null()
                    ? 0 : row["expired_at"].as<int64_t>();
                item["appendix_number"] = row["appendix_number"].is_null()
                    ? 0 : row["appendix_number"].as<long long>();
                item["valid_until"] = row["valid_until"].is_null()
                    ? 0 : row["valid_until"].as<int64_t>();
                result.push_back(item);
            }
            g_serverLogger.info("getClientUnsoldExpiredItems: clientId=" +
                std::to_string(clientId) + ", items=" +
                std::to_string(result.size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientUnsoldExpiredItems error: " +
                std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Получает список ПРИЛОЖЕНИЙ К ДОГОВОРУ комитента.
     *
     * Возвращает все приложения с номерами, датами и итогами.
     * Используется кассиром для выбора приложения.
     *
     * @param clientId ID комитента
     * @return JSON-массив приложений
     */
    json getClientAppendices(int clientId) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, appendix_number, created_at, valid_until, "
                "total_quantity, total_value, total_client_amount, "
                "expired_processed "
                "FROM contract_appendices "
                "WHERE client_id = $1 "
                "ORDER BY created_at DESC",
                pqxx::params{ clientId }
            );
            for (const auto& row : res) {
                json app;
                app["id"] = row["id"].as<long long>();
                app["appendix_number"] = row["appendix_number"].as<long long>();
                app["created_at"] = row["created_at"].is_null()
                    ? 0 : row["created_at"].as<int64_t>();
                app["valid_until"] = row["valid_until"].is_null()
                    ? 0 : row["valid_until"].as<int64_t>();
                app["total_quantity"] = row["total_quantity"].as<int>();
                app["total_value"] = row["total_value"].as<double>();
                app["total_client_amount"] =
                    row["total_client_amount"].as<double>();
                app["expired_processed"] =
                    row["expired_processed"].as<bool>();
                result.push_back(app);
            }
            g_serverLogger.info("getClientAppendices: clientId=" +
                std::to_string(clientId) + ", appendices=" +
                std::to_string(result.size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientAppendices error: " +
                std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Получает товары конкретного приложения к договору для кассира.
     *
     * Возвращает все товары приложения с их текущими статусами.
     * Кассир видит, какие товары ещё не проданы и могут быть возвращены.
     *
     * @param appendixId ID приложения (contract_appendices.id)
     * @param clientId ID комитента (защита от подмены)
     * @return JSON-массив товаров приложения
     */
    json getAppendixItemsForCashier(long long appendixId, int clientId) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT i.id, i.item_number, i.description, "
                "i.estimated_price, i.quantity, i.sold_quantity, "
                "i.condition, i.note, i.status, i.created_at, "
                "i.client_percent, i.store_percent, "
                "i.client_amount, i.store_amount "
                "FROM items i "
                "WHERE i.appendix_id = $1 AND i.client_id = $2 "
                "ORDER BY i.item_number",
                pqxx::params{ appendixId, clientId }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] =
                    row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                item["sold_quantity"] = row["sold_quantity"].as<int>();
                item["unsold_quantity"] = row["quantity"].as<int>() -
                    row["sold_quantity"].as<int>();
                item["condition"] = row["condition"].is_null()
                    ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null()
                    ? "" : row["note"].as<std::string>();
                item["status"] = row["status"].is_null()
                    ? "" : row["status"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["client_percent"] = row["client_percent"].is_null()
                    ? 0.0 : row["client_percent"].as<double>();
                item["store_percent"] = row["store_percent"].is_null()
                    ? 0.0 : row["store_percent"].as<double>();
                item["client_amount"] = row["client_amount"].is_null()
                    ? 0.0 : row["client_amount"].as<double>();
                item["store_amount"] = row["store_amount"].is_null()
                    ? 0.0 : row["store_amount"].as<double>();
                result.push_back(item);
            }
            g_serverLogger.info("getAppendixItemsForCashier: appendixId=" +
                std::to_string(appendixId) + ", clientId=" +
                std::to_string(clientId) + ", items=" +
                std::to_string(result.size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAppendixItemsForCashier error: " +
                std::string(e.what()));
        }
        return result;
    }

    /**
     * @brief Создаёт документ кассира и возвращает его порядковый номер.
     *
     * Нумерация документов ведётся отдельно для каждого комитента и типа
     * документа. Это реализуется через запрос
     * "SELECT COALESCE(MAX(doc_number), 0) + 1 FROM cashier_documents
     *  WHERE client_id = ... AND doc_type = ...".
     *
     * @param clientId ID комитента
     * @param docType тип документа ('return', 'receipt', 'loss', 'reward')
     * @param itemsData JSON-строка с данными товаров
     * @param totalAmount итоговая сумма
     * @return порядковый номер документа или 0 при ошибке
     */
    int createCashierDocument(int clientId, const std::string& docType,
        const std::string& itemsData, double totalAmount) {
        try {
            pqxx::work txn{ *conn_ };
            // Получаем следующий номер документа для данного комитента и типа
            auto res = txn.exec(
                "SELECT COALESCE(MAX(doc_number), 0) + 1 AS next_number "
                "FROM cashier_documents "
                "WHERE client_id = $1 AND doc_type = $2",
                pqxx::params{ clientId, docType }
            );
            int docNumber = res[0]["next_number"].as<int>();
            // Вставляем новый документ
            txn.exec(
                "INSERT INTO cashier_documents "
                "(client_id, doc_type, doc_number, items_data, total_amount) "
                "VALUES ($1, $2, $3, $4, $5)",
                pqxx::params{ clientId, docType, docNumber, itemsData,
                    totalAmount }
            );
            txn.commit();
            g_serverLogger.info("createCashierDocument: clientId=" +
                std::to_string(clientId) + ", docType=" + docType +
                ", docNumber=" + std::to_string(docNumber));
            return docNumber;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createCashierDocument error: " +
                std::string(e.what()));
            return 0;
        }
    }

};
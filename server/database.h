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
#include "shop_barcode.h"
using json = nlohmann::json;
// =================================================================
// ФУНКЦИЯ: Санитизация строк для удаления некорректных UTF-8 символов
// ====== ===========================================================
inline std::string sanitizeUTF8(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = input[i];
        // Определяем длину UTF-8 последовательности
        size_t seqLen = 0;
        if (c < 0x80) seqLen = 1;
        else if (c < 0xC0) { i++; continue; } // Неправильный байт, пропускаем
        else if (c < 0xE0) seqLen = 2;
        else if (c < 0xF0) seqLen = 3;
        else if (c < 0xF8) seqLen = 4;
        else { i++; continue; } // Неправильный байт, пропускаем
        // Проверяем, что все continuation bytes корректны
        bool valid = true;
        if (i + seqLen > input.size()) {
            valid = false;
        }
        else {
            for (size_t j = 1; j < seqLen; j++) {
                if ((input[i + j] & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid) {
            result.append(input, i, seqLen);
        }
        // Иначе пропускаем некорректный байт
        i += seqLen > 0 ? seqLen : 1;
    }
    return result;
}
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
    std::shared_ptr<pqxx::connection>  conn_;
    std::string encryption_key_;
public:
    Database(std::shared_ptr<pqxx::connection>  conn, std::string enc_key)
        : conn_(std::move(conn)), encryption_key_(std::move(enc_key)) {}
    bool initialize() {
        try {
            pqxx::work txn{ *conn_ };
            // ========================================================================
            // Создание таблиц с полной структурой через CREATE TABLE IF NOT EXISTS
            // ==================== ====================================================
            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS clients(
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
                    CREATE TABLE IF NOT EXISTS auth_tokens(
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
                    CREATE TABLE IF NOT EXISTS first_time_tickets(
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
                    CREATE TABLE IF NOT EXISTS queue_tickets(
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
                    CREATE TABLE IF NOT EXISTS trust_acceptances(
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
                    CREATE TABLE IF NOT EXISTS sync_log(
                        id SERIAL PRIMARY KEY,
                        sync_type VARCHAR(50) NOT NULL,
                        records_count INTEGER DEFAULT 0,
                        status VARCHAR(20) NOT NULL,
                        error_message TEXT,
                        created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())
                    )
                )");
                txn.exec(R"(
                    CREATE TABLE IF NOT EXISTS items(
                        id SERIAL PRIMARY KEY,
                        client_id INTEGER REFERENCES clients(id),
                        description TEXT NOT NULL,
                        estimated_price DECIMAL(10, 2) DEFAULT 0,
                        synced_to_1c BOOLEAN DEFAULT FALSE,
                        created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())
                    )
                )");
                // ========================================================================
                // МИГРАЦИЯ: добавление колонки role
                // ============================================================ ============
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
                // ======================= =================================================
                txn.exec(R"(
                    DO $$
                    BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS totp_secret_encrypted TEXT;
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                g_serverLogger.info("Migration: clients.totp_secret_encrypted column ensured ");
            // ========================================================================
            // Миграции — добавляем отсутствующие колонки БЕЗОПАСНО
            // ========================================= ===============================
            // МИГРАЦИЯ: добавление новых колонок в таблицу items
            txn.exec(R"(
                DO $$
                BEGIN
                ALTER TABLE items ADD COLUMN IF NOT EXISTS item_number INTEGER;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS quantity INTEGER DEFAULT 1;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS sale_date BIGINT;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS condition TEXT;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS note TEXT;
            ALTER TABLE items ADD COLUMN IF NOT  EXISTS status VARCHAR(20) DEFAULT 'pending';
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
            ALTER  TABLE queue_tickets ADD COLUMN IF NOT EXISTS served_at BIGINT;
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
                // ================ ========================================================
                txn.exec(R"(
                    DO $$
                    BEGIN
                    -- Добавляем колонку ticket_number, если её нет
                    ALTER TABLE trust_acceptances ADD COLUMN IF NOT EXISTS ticket_number VARCHAR(20);
            --Заполняем NULL - значения
                UPDATE trust_acceptances t
                SET ticket_number = 'TR-' || id::text
                WHERE ticket_number IS NULL OR ticket_number = '';
            --Устраняем существующие дубликаты(оставляем запись с минимальным id)
                UPDATE trust_acceptances t
                SET ticket_number = 'TR-DUP-' || t.id::text
                WHERE t.id NOT IN(
                    SELECT MIN(id)
                    FROM trust_acceptances
                    GROUP BY ticket_number
                );
            --Делаем колонку NOT NULL
                ALTER TABLE trust_acceptances ALTER COLUMN ticket_number SET NOT NULL;
            --Добавляем UNIQUE ограничение, только если его ещё нет
                IF NOT EXISTS(
                    SELECT 1 FROM pg_constraint
                    WHERE conname = 'trust_acceptances_ticket_number_unique'
                    AND conrelid = 'trust_acceptances'::regclass
                ) THEN
                ALTER TABLE trust_acceptances
                ADD CONSTRAINT trust_acceptances_ticket_number_unique UNIQUE(ticket_number);
            END IF;
            EXCEPTION
                WHEN duplicate_column THEN
                NULL;
            WHEN others THEN
                -- на всякий случай не падаем
                RAISE NOTICE 'Migration trust_acceptances: %', SQLERRM;
            END $$;
                )");
                // ========================================================================
                // МИГРАЦИЯ: добавление колонки is_blocked для блокировки комитентов
                // ============================ ============================================
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
                // который внёс товар (для оц енки эффективности товароведов)
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
                // ====================== ==================================================
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
                // ======================== ================================================
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
                // sto re_percent   - процент от продажи, который получает магазин
                // client_amount   - сумма выплаты комитенту (цена * кол-во * процент / 100)
                // store_amount    - сумма прибыли магазина  (цена * кол-во * процент / 100)
                txn.exec(R"(
                    DO $$
                    BEGIN
                    ALTER TABLE items ADD COLUMN IF NOT EXISTS client_percent DECIMAL(5, 2) DEFAULT 0;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS store_percent DECIMAL(5, 2) DEFAULT 0;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS client_amount DECIMAL(10, 2) DEFAULT 0;
            ALTER TABLE items ADD COLUMN IF NOT EXISTS store_amount DECIMAL(10, 2) DEFAULT 0;
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                g_serverLogger.info("Migration: items commission columns added successfully ");
            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПРИЛОЖЕНИЯ К ДОГОВОРУ (чеки комитентов)
            // Хранит порядковый номер приложения (ка к 263216 в образце чека),
            // чтобы по нему можно было найти весь перечень сданных вещей,
            // количество проданных и возвращённых. Безопасный стиль миграций,
            // как во всём файле: не  ломает работающую БД.
            // ========================================================================
            txn.exec(R"(
                CREATE SEQUENCE IF NOT EXISTS contract_appendices_number_seq
                START 1;
            )");
                // ========================================================================
                // ИСПРАВЛЕННАЯ МИГРАЦИЯ: ПРИЛОЖЕНИЯ К ДОГОВОРУ
                // Изменено ограничение: было appendix_number BIGINT  UNIQUE (глобальное),
                // стало UNIQUE(client_id, appendix_number) — уникальность в пределах комитента.
                // ========================================================================
                txn.exec(R"(
                    DO $$
                    BEGIN
                    -- Удаляем старое глобальное ограничение уникальности, если оно существует
                    IF EXISTS(
                        SELECT 1 FROM information_schema.table_constraints
                        WHERE table_name = 'contract_appendices'
                        AND constraint_type = 'UNIQUE'
                        AND constraint_name = 'contract_appendices_appendix_number_key'
                    ) THEN
                    ALTER TABLE contract_appendices
                    DROP CONSTRAINT contract_appendices_appendix_number_key;
            END IF;
            END $$;
                )");
                txn.exec(R"(
                    CREATE TABLE IF NOT EXISTS contract_appendices(
                        id SERIAL PRIMARY KEY,
                        appendix_number BIGINT NOT NULL,
                        client_id INTEGER REFERENCES clients(id),
                        worker_id INTEGER REFERENCES clients(id),
                        created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                        valid_until BIGINT,
                        total_quantity INTEGER DEFAULT 0,
                        total_value DECIMAL(10, 2) DEFAULT 0,
                        total_client_amount DECIMAL(10, 2) DEFAULT 0
                    )
                )");
                //========================================================================
                // ИСПРАВЛЕНИЕ МИГРАЦИИ: ГЛОБАЛЬНАЯ УНИКАЛЬНОСТЬ НОМЕРА ПРИЛОЖЕНИЯ
                //================================ ========================================
                // Требуется, чтобы номер приложения к договору был уникальным и не
                // повторялся ни у одного комитента. Заменяем составной индекс
                // UNIQU E(client_id, appendix_number) на глобальный UNIQUE(appendix_number).
                //
                // КРИТИЧЕСКИ ВАЖНО: перед созданием глобального индекса необходимо
                // устранить дубликаты, унаследованные о т старой пер-клиентной
                // нумерации. Каждый дубликат (кроме первого вхождения) получает
                // новый уникальный номер через nextval(). После дедупликации
                // последовательность синхрони зируется с максимальным значением.
                //
                // Номер имеет тип BIGINT, что автоматически гарантирует отсутствие
                // букв и специальных символов (только цифры от 0 до 9).
                //=============== =========================================================
                txn.exec(R"(
                    DO $$
                    DECLARE
                    dup_record RECORD;
            new_number BIGINT;
            max_num BIGINT;
            BEGIN
                -- 1. Удаляем старое составное ограничение, если оно существует
                IF EXISTS(
                    SELECT 1 FROM pg_constraint
                    WHERE conname = 'uq_contract_appendices_client_number'
                    AND conrelid = 'contract_appendices'::regclass
                ) THEN
                ALTER TABLE contract_appendices
                DROP CONSTRAINT uq_contract_appendices_client_number;
            END IF;
            --2. Удаляем возможное старое глобальное ограничение(для чистоты)
                IF EXISTS(
                    SELECT 1 FROM pg_constraint
                    WHERE conname = 'contract_appendices_appendix_number_key'
                    AND conrelid = 'contract_appendices'::regclass
                ) THEN
                ALTER TABLE contract_appendices
                DROP CONSTRAINT contract_appendices_appendix_number_key;
            END IF;
            --3. Удаляем возмо жное старое глобальное ограничение с новым именем
                IF EXISTS(
                    SELECT 1 FROM pg_constraint
                    WHERE conname = 'uq_contract_appendices_number_global'
                    AND conrelid = 'contract_appendices'::regclass
                ) THEN
                ALTER TABLE contract_appendices
                DROP CONSTRAINT uq_contract_appendices_number_global;
            END IF;
            --4. ДЕДУПЛИКАЦИЯ: перенумеровываем дубликаты через nextval.
                --Дл я каждого appendix_number, который встречается более одного раза,
                --все записи КРОМЕ первой(с наименьшим id) получают новый номер
                -- из последовательности.Первая запись сохраняе т свой номер.
                FOR dup_record IN
                SELECT ca.id
                FROM contract_appendices ca
                WHERE EXISTS(
                    SELECT 1 FROM contract_appendices ca2
                    WHERE ca2.appendix_number = ca.appendix_number
                    AND ca2.id < ca.id
                )
                ORDER BY ca.id
                LOOP
                new_number := nextval('contract_appendices_number_seq');
            UPDATE contract_appendices
                SET appendix_number = new_number
                WHERE id = dup_record.id;
            END LOOP;
            --5. СИНХРОНИЗАЦИЯ последовательности с максимальным значением.
                --Это гарантирует, что следующий nextval() вернёт номер,
                --больший всех существующих.
                SELECT COALESCE(MAX(appendix_number), 0) INTO max_num
                FROM contract_appendices;
            IF max_num > 0 THEN
                PERFORM setval('contract_appendices_number_seq', max_num);
            END IF;
            --6. Добавляем ГЛОБАЛЬНОЕ ограничение уникальности.
                --После дедупликации дубликатов не существует, инд екс создаётся успешно.
                ALTER TABLE contract_appendices
                ADD CONSTRAINT uq_contract_appendices_number_global
                UNIQUE(appendix_number);
            END $$;
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
                // Безопасное добавление колонки sold_quantit y в таблицу items.
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
                g_serverLogger.info("Migration: items.sold_quantity added successfully ");
            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ТАБЛИЦА ЛОГА ПРОДАЖ (item_sales)
            // Каждая запись = одна проданная единица товара .
            // Позволяет отслеживать: когда продано, кем (касса/1С), по какой цене.
            txn.exec(R"(
                CREATE TABLE IF NOT EXISTS item_sales(
                    id SERIAL PRIMARY KEY,
                    item_id INTEGER REFERENCES items(id),
                    client_id INTEGER REFERENCES clients(id),
                    quantity_sold INTEGER DEFAULT 1,
                    sale_price DECIMAL(10, 2) DEFAULT 0,
                    client_amount DECIMAL(10, 2) DEFAULT 0,
                    store_amount DECIMAL(10, 2) DEFAULT 0,
                    sold_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                    source VARCHAR(50)  DEFAULT '1C',
                    receipt_number VARCHAR(100),
                    barcode_payload TEXT
                )
            )");
                txn.exec(R"(
                    DO $$
                    BEGIN
                    ALTER TABLE item_sales
                    ADD COLUMN IF NOT EXISTS idempotency_key VARCHAR(128);
            ALTER TABLE item_sales
                ADD COLUMN IF NOT EXISTS fiscal_document_number VARCHAR(128);
            ALTER TABLE item_sales
                ADD COLUMN IF NOT EXISTS fiscal_document_uuid VARCHAR(128);
            ALTER TABLE item_sales
                ADD COLUMN IF NOT EXISTS fiscal_sign VARCHAR(128);
            ALTER TABLE item_sales
                ADD COLUMN IF NOT EXISTS fiscalized_at BIGINT;
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                txn.exec(R"(
                    CREATE UNIQUE INDEX IF NOT EXISTS
                    idx_item_sales_idempotency
                    ON item_sales(idempotency_key)
                    WHERE idempotency_key IS NOT NULL
                    AND idempotency_key <> '';
                )");
                // ========================================================================
                // НОВАЯ МИГРАЦИЯ: ПОЛЕ expired_at В items
                // Фиксирует момент, когда товар был помечен как  "выбывший из продажи "
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
                g_serverLogger.info("Migration: items.expired_at added successfully ");
            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПОЛЕ expired_processed В contract_appendices
            // Флаг для фонового процесса: TRUE  = приложение уже обработано
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
                g_serverLogger.info("Migration: contract_appendices.expired_processed added successfully ");
            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ДОПОЛНИТЕЛЬНЫЕ ПОЛЯ КОМИТЕНТА (для first_time)
            // birth_date, passport_type, passport_series, passport_number, address
            // ========================================================================
            txn.exec(R"(
                DO $$
                BEGIN
                ALTER TABLE clients ADD COLUMN IF NOT EXISTS birth_date VARCHAR(10);
            ALTER TABLE clients ADD COLUMN IF NOT EXISTS passport_type VARCHAR(10) DEFAULT 'rf';
            ALTER TABLE  clients ADD COLUMN IF NOT EXISTS passport_series VARCHAR(10);
            ALTER TABLE clients ADD COLUMN IF NOT EXISTS passport_number VARCHAR(20);
            ALTER TABLE clients ADD COLUMN IF NOT EXISTS address TEXT;
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                g_serverLogger.info("Migration: committee additional fields added (birth_date, passport_ , address)");
            // ========================================================================
            // МИГРАЦИЯ: Замена UNIQUE ограничений на частичные уникальные индексы
            // для поддержки циклических  коротких номеров талонов (не более 3 знаков).
            //
            // ПРИЧИНА:
            // Полные UNIQUE ограничения на number / ticket_number не позволяют
            // повторно использовать короткие номера (001-999)  после обслуживания
            // талонов, так как старые записи остаются в таблице со статусом served.
            //
            // РЕШЕНИЕ:
            // Удаляем полные UNIQUE ограничения и создаём частичные уникальные
            // ин дексы, которые гарантируют уникальность номера ТОЛЬКО среди
            // активных талонов (waiting/accepted для очередей, pending/accepted
            // для доверия). Обслуженные талоны могут иметь пов торяющиеся номера,
            // что позволяет использовать циклическую нумерацию 001-999.
            //
            // Это НЕ ЛОМАЕТ бизнес-логику, так как все методы поиска/приёма/
            // обслуживания талона всегда ф ильтруют по активному статусу.
            // ========================================================================
            txn.exec(R"(
                DO $$
                DECLARE r RECORD;
            BEGIN
                -- Удаляем UNIQUE ограничения с queue_tickets.number
                FOR r IN(SELECT constraint_name FROM information_schema.table_constraints
                    WHERE table_name = 'queue_tickets' AND constraint_type = 'UNIQUE')
                LOOP
                EXECUTE 'ALTER TABLE queue_tickets DROP CONSTRAINT ' || quote_ident(r.constraint_name);
            END LOOP;
            --Удаляем UNIQUE ограничения  с first_time_tickets.ticket_number
                FOR r IN(SELECT constraint_name FROM information_schema.table_constraints
                    WHERE table_name = 'first_time_tickets' AND constraint_type = 'UNIQUE')
                LOOP
                EXECUTE 'ALTER TABLE first_time_tickets DROP CONSTRAINT ' || quote_ident(r.constraint_name);
            END LOOP;
            --Удаляем UNIQUE ограничения с trust_acceptances.ticket_number
                FOR r  IN(SELECT constraint_name FROM information_schema.table_constraints
                    WHERE table_name = 'trust_acceptances' AND constraint_type = 'UNIQUE')
                LOOP
                EXECUTE 'ALTER TABLE trust_acceptances DROP CONSTRAINT ' || quote_ident(r.constraint_name);
            END LOOP;
            END $$;
                )");
                // ========================================================================
                // МИГРАЦИЯ: добавление колонки block_reason для хранения причины блокировки
                // ==================== ====================================================
                txn.exec(R"(
                    DO $$
                    BEGIN
                    ALTER TABLE clients ADD COLUMN IF NOT EXISTS block_reason TEXT DEFAULT '';
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                g_serverLogger.info("Migration: clients.block_reason added successfully ");
            // ========================================================================
            // МИГРАЦИЯ: стабильный штрих-код товара.
            //
            // Формат:
            //     SHOP + items.id
            //
            // Пример:
            //      items.id = 123
            //     barcode = SHOP123
            //
            // barcode является постоянным идентификатором товара.
            // Цена, ФИО комитента и описание в barcode НЕ записываются.
            // ================== ======================================================
            txn.exec(R"(
                DO $$
                BEGIN
                ALTER TABLE items
                ADD COLUMN IF NOT EXISTS barcode VARCHAR(64);
            ALTER TABLE items
                ADD COLUMN IF NOT EXISTS onec_ref VARCHAR(128);
            ALTER TABLE items
                ADD COLUMN IF NOT  EXISTS sync_status VARCHAR(32)
                DEFAULT 'pending';
            ALTER TABLE items
                ADD COLUMN IF NOT EXISTS sync_error TEXT;
            ALTER TABLE items
                ADD COLUMN IF NOT EXISTS synced_at BIGINT;
            EXCEPTION WHEN duplicate_column THEN
                NULL;
            END $$;
                )");
                // ========================================================================
                // НОВАЯ МИГРАЦИЯ: ТАБЛИЦА ЗАКАЗОВ ЭКВАЙРИНГА ВТБ
                // =============================================== =========================
                txn.exec(R"(
                    CREATE TABLE IF NOT EXISTS acquiring_orders(
                        id SERIAL PRIMARY KEY,
                        order_number VARCHAR(64) UNIQUE NOT NULL,
                        vtb_order_id VARCHAR(128),
                        client_id INTEGER REFERENCES clients(id),
                        amount DECIMAL(10, 2) NOT NULL,
                        amount_cop BIGINT NOT NULL,
                        status VARCHAR(32) DEFAULT 'created',
                        doc_type VARCHAR(20),
                        doc_id INTEGER,
                        created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()),
                        updated_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())
                    )
                )");
                txn.exec("CREATE INDEX IF NOT EXISTS idx_acquiring_orders_vtb_id ON acquiring_orders(vtb_order_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_acquiring_orders_client ON acquiring_orders(client_id)");
            g_serverLogger.info("Migration: acquiring_orders table created for VTB acquiring ");
            // ========================================================================
            // НОВАЯ МИГРАЦИЯ: ПРИНУДИТЕЛЬНЫЙ ПЕРЕВОД ШТРИХ-КОДОВ В ФОРМАТ SHOP + id
            //
            // 1. Удаляем все пробел ьные символы из уже сохранённых значений.
            // 2. Конвертируем внутренние 13-значные коды формата 200XXXXXXXXXX
            //    в старый формат SHOP + items.id.
            // 3. Сбрасываем признак синхро низации с 1С для изменённых записей,
            //    чтобы 1С получила новые штрих-коды через getUnsyncedItems().
            //
            // Формат после миграции:
            //   items.id = 123
            //   barcode = SHOP123
            //
            // Пробелы внутри значения не допускаются.
                // ========================================================================
                txn.exec(R"(
                    UPDATE items
                    SET barcode = regexp_replace(barcode, '[[:space:]]', '', 'g'),
                    synced_to_1c = FALSE,
                    sync_status = 'pending',
                    sync_error = NULL,
                    synced_at = NULL
                    WHERE barcode IS NOT NULL
                    AND barcode IS DISTINCT FROM regexp_replace(barcode, '[[:space:]]', '', 'g');
                )");
                txn.exec(R"(
                    UPDATE items
                    SET barcode = 'SHOP' || id::text,
                    synced_to_1c = FALSE,
                    sync_status = 'pending',
                    sync_error = NULL,
                    synced_at = NULL
                    WHERE barcode ~'^200[0-9]{10}$';
                )");
                // Заполняем barcode старых товаров.
                txn.exec(R"(
                    UPDATE items
                    SET barcode = 'SHOP' || id::text
                    WHERE barcode IS NULL
                    OR barcode = '';
                )");
                // ========================================================================
                // НОВОЕ ОГРАНИЧЕНИЕ: ЗАПРЕТ НА ЛЮБОЙ ФОРМАТ ШТРИХ-КОДА, КРОМЕ SHOP + цифры
                // ===================== ===================================================
                txn.exec(R"(
                    DO $$
                    BEGIN
                    ALTER TABLE items
                    ADD CONSTRAINT chk_items_barcode_shop_format
                    CHECK(barcode IS NULL OR barcode ~'^SHOP[0-9]+$');
            EXCEPTION WHEN duplicate_object THEN
                NULL;
            END $$;
                )");
                // Уникальность barcode.
                txn.exec(R"(
                    CREATE UNIQUE INDEX IF NOT EXISTS
                    idx_items_barcode_unique
                    ON items(barcode);
                )");
                // Быстрый поиск по 1С external ID.
                txn.exec(R"(
                    CREATE UNIQUE INDEX IF NOT EXISTS
                    idx_items_onec_ref_unique
                    ON items(onec_ref)
                    WHERE onec_ref IS NOT NULL
                    AND onec_ref <> '';
                )");
                g_serverLogger.info(
                    "Migration: items barcode and 1C synchronization fields ensured "
                );
            //
            // Создаём частичные уникальные индексы для активных талонов
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_queue_tickets_number_active ON queue_tickets(number) WHERE status IN ('waiting', 'accepted')");
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_first_time_tickets_number_active ON first_time_tickets(ticket_number) WHERE status IN ('waiting', 'accepted')");
            txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_trust_acceptances_number_active ON trust_acceptances(ticket_number) WHERE status IN ('pending', 'accepted')");
            g_serverLogger.info("Migration: UNIQUE constraints replaced with partial unique indexes for cyclic ticket numbers (001-999)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_contract_appendices_client ON contract_appendices(client_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_contract_appendices_number ON contract_appendices(appendix_number)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_appendix_id ON items(appendix_id)");
            g_serverLogger.info("Migration: contract_appendices table, sequence and items.appendix_id added successfully ");
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
            // ========================================== ==============================
            txn.exec("CREATE INDEX IF NOT EXISTS idx_clients_is_blocked ON clients(is_blocked)");
            // ========================================================================
            // ИНДЕКС для быстрого поиска товаров по товароведу
            // ============================================= ===========================
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_worker_id ON items(worker_id)");
            // ИНДЕКС для быстрого поиска товаров по статусу (low_quality)
            txn.exec("CREATE INDEX IF NOT EXISTS idx_items_status ON items(status)");
            // Индекс для быстрого поиска проданных товаров по дате продажи и по источнику (1С/касса)
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_item_id ON item_sales(item_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_client_id ON item_sales(client_id)");
            txn.exec("CREATE INDEX IF NOT EXISTS idx_item_sales_sold_at ON item_sales(sold_at)");
            g_serverLogger.info("Migration: item_sales table created successfully ");
            g_serverLogger.info("Migration: idx_items_status index created ");
            // ========================================================================
            // НОВОЕ: АВТОСИНХРОНИЗАЦИЯ СЧЁТЧИКОВ clients.items_submitted / items_sold
            //
            // ПРИЧИНА: колонки не  обновлялись при приёмке/продаже, поэтому поиск
            // товароведа (getClientFullInfo / searchClientsByFio) всегда выдавал 0.
            //
            // РЕШЕНИЕ:
            //   1) одноразовый полный пересчёт по всем  комитентам при старте сервера;
            //   2) триггер AFTER INSERT OR UPDATE OR DELETE ON items, который при
            //      каждом изменении товаров автоматически пересчитывает счётчики
            //       затронутого клиента (внутри той же транзакции — атомарно).
            // ========================================================================
            // --- 1) Полный пересчёт для всех комитентов , у которых есть товары ---
            txn.exec(R"(
                UPDATE clients c SET
                items_submitted = COALESCE(s.total_qty, 0),
                items_sold = COALESCE(s.total_sold, 0),
                updated_at = EXTRACT(EPOCH FROM NOW())
                FROM(
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
                    WHERE(items_submitted <> 0 OR items_sold <> 0)
                    AND NOT EXISTS(SELECT 1 FROM items i WHERE i.client_id = clients.id);
                )");
                g_serverLogger.info("Migration: clients counters recalculated from items ");
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
                items_sold = COALESCE(s.total_sold, 0),
                updated_at = EXTRACT(EPOCH FROM NOW())
                FROM(
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
            g_serverLogger.info("Database initialized successfully ");
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Database initialization error:  " + std::string(e.what()));
            std::cerr << "Database initialization error:  " << e.what() << std::endl;
            return false;
        }
        // =========================================================================
        // МИГРАЦИЯ: МОДУЛЬ «КАССИР»
        // ========================================== ===============================
        //
        // НАЗНАЧЕНИЕ:
        // Создаёт таблицу для хранения документов кассира (накладные на возврат,
        // расписки, накладные об утрате, накладные о выплате во знаграждения).
        //
        // БЕЗОПАСНОСТЬ:
        // Эта таблица НЕ влияет на существующую логику, математику и бизнес-модель
        // программы. Она используется ТОЛЬКО для нумерации документов кассир а
        // (порядковый номер документа учитывается для каждого комитента отдельно).
        //
        // ПОТОКОБЕЗОПАСНОСТЬ:
        // Миграция выполняется в рамках одной транзакции при инициализации сервера. 
        // Повторное выполнение безопасно благодаря CREATE TABLE IF NOT EXISTS.
        // =========================================================================
        try {
            pqxx::work txnCashier{ *conn_ };
            txnCashier.exec(
                "CREATE TABLE IF NOT EXISTS cashier_documents ( "
                "    id SERIAL PRIMARY KEY, "
                "    client_id INTEGER REFERENCES clients(id), "
                "    doc_type VARCHAR(20) NOT NULL, "
                "    doc_number INTEGER NOT NULL, "
                "    created_at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW()), "
                "    items_data TEXT, "
                "    total_amount DECIMAL(10,2) DEFAULT 0 "
                ") "
            );
            txnCashier.exec(
                "CREATE INDEX IF NOT EXISTS idx_cashier_documents_client  "
                "ON cashier_documents(client_id, doc_type) "
            );
            txnCashier.commit();
            g_serverLogger.info("Migration: cashier_documents table created ");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Migration cashier_documents error:  " +
                std::string(e.what()));
        }
        return true;
    }
    // ---- Получение клиента по телефону с ролью ----
    std::optional <Client > getClientByPhone(const std::string& phone) {
        try {
            // ИСПРАВЛЕНИЕ: Используем nontransaction для операции только чтения,
            // чтобы избежать ошибок  "Started new transaction while transaction was still active "
            pqxx::nontransaction txn{ *conn_ };
            auto result = txn.exec(
                "SELECT id, phone, last_name, first_name, middle_name, email, role, birth_date, created_at FROM clients WHERE phone = $1 ",
                pqxx::params{ phone }
            );
            if (result.empty()) return std::nullopt;
            Client client;
            client.id = result[0]["id"].as<int>();
            client.phone = result[0]["phone"].as<std::string>();
            std::string last = result[0]["last_name"].as<std::string>();
            std::string first = result[0]["first_name"].as<std::string>();
            std::string mid = result[0]["middle_name"].is_null() ? " " : result[0]["middle_name"].as<std::string>();
            client.name = last + "  " + first + (mid.empty() ? " " : "  " + mid);
            client.email = result[0]["email"].is_null() ? " " : result[0]["email"].as<std::string>();
            client.role = result[0]["role"].is_null() ? "client " : result[0]["role"].as<std::string>();
            client.birth_date = result[0]["birth_date"].is_null() ? " " : result[0]["birth_date"].as<std::string>();
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            g_serverLogger.info("getClientByPhone: phone= " + phone + ", birth_date= " + client.birth_date);
            return client;
        }
        catch (const std::exception& e) {
            std::cerr << "getClientByPhone error:  " << e.what() << std::endl;
            return std::nullopt;
        }
    }
    // ---- Регистрация клиента (с ролью по умолчанию 'client') ----
    std::pair <bool, bool > registerClient(const std::string& phone, const std::string& last_name,
        const std::string& first_name, const std::string& middle_name,
        const std::string& email, int items_submitted, int items_sold,
        const std::string& role = "client ") {
        try {
            pqxx::work txn{ *conn_ };
            std::optional<std::string>  mid_opt = middle_name.empty() ? std::nullopt : std::optional<std::string> (middle_name);
            std::optional<std::string>  email_opt = email.empty() ? std::nullopt : std::optional<std::string> (email);
            auto exist = txn.exec("SELECT id FROM clients WHERE phone = $1 ", pqxx::params{ phone });
            if (!exist.empty()) {
                // Обновляем существующего клиента (роль не меняем)
                txn.exec(
                    "UPDATE clients SET last_name=$2, first_name=$3, middle_name=$4,  "
                    "email=$5, items_submitted=$6, items_sold=$7, updated_at=EXTRACT(EPOCH FROM NOW())  "
                    "WHERE phone=$1 ",
                    pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold }
                );
                txn.commit();
                return { true, true };
            }
            // Вставляем нового клиента с ролью
            txn.exec(
                "INSERT INTO clients (phone, last_name, first_name, middle_name, email, items_submitted, items_sold, role)  "
                "VALUES ($1,$2,$3,$4,$5,$6,$7,$8) ",
                pqxx::params{ phone, last_name, first_name, mid_opt, email_opt, items_submitted, items_sold, role }
            );
            txn.commit();
            return { true, false };
        }
        catch (const std::exception& e) {
            std::cerr << "registerClient error:  " << e.what() << std::endl;
            return { false, false };
        }
    }
    bool setTOTPSecret(const std::string& phone, const std::string& secret) {
        try {
            std::string encrypted = CryptoUtils::encryptAES256CBC(secret, encryption_key_);
            pqxx::work txn{ *conn_ };
            txn.exec(
                "UPDATE clients SET totp_secret_encrypted = $1 WHERE phone = $2 ",
                pqxx::params{ encrypted, phone }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "setTOTPSecret error:  " << e.what() << std::endl;
            return false;
        }
    }
    std::optional<std::string>  getTOTPSecret(const std::string& phone) {
        try {
            pqxx::work txn{ *conn_ };
            auto result = txn.exec(
                "SELECT totp_secret_encrypted FROM clients WHERE phone = $1 ",
                pqxx::params{ phone }
            );
            if (!result.empty() && !result[0]["totp_secret_encrypted"].is_null()) {
                std::string encrypted = result[0]["totp_secret_encrypted"].as<std::string>();
                return CryptoUtils::decryptAES256CBC(encrypted, encryption_key_);
            }
            return std::nullopt;
        }
        catch (const std::exception& e) {
            std::cerr << "getTOTPSecret error:  " << e.what() << std::endl;
            return std::nullopt;
        }
    }
    bool saveAuthTokens(int client_id, const std::string& access_token_hash,
        const std::string& refresh_token_hash, int64_t expires_at) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "INSERT INTO auth_tokens (client_id, access_token_hash, refresh_token_hash, expires_at)  "
                "VALUES ($1, $2, $3, $4) ",
                pqxx::params{ client_id, access_token_hash, refresh_token_hash, expires_at }
            );
            txn.commit();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "saveAuthTokens DB error:  " << e.what() << std::endl;
            return false;
        }
    }
    // ---- Получение клиента по id с ролью ----
    std::optional <Client > getClientById(int id) {
        try {
            // ИСПРАВЛЕНИЕ: Используем nontransaction для операции только чтения
            pqxx::nontransaction txn{ *conn_ };
            auto result = txn.exec(
                "SELECT id, phone, last_name, first_name, middle_name, email, role, birth_date, created_at FROM clients WHERE id = $1 ",
                pqxx::params{ id }
            );
            if (result.empty()) return std::nullopt;
            Client client;
            client.id = result[0]["id"].as<int>();
            client.phone = result[0]["phone"].as<std::string>();
            std::string last = result[0]["last_name"].as<std::string>();
            std::string first = result[0]["first_name"].as<std::string>();
            std::string mid = result[0]["middle_name"].is_null() ? " " : result[0]["middle_name"].as<std::string>();
            client.name = last + "  " + first + (mid.empty() ? " " : "  " + mid);
            client.email = result[0]["email"].is_null() ? " " : result[0]["email"].as<std::string>();
            client.role = result[0]["role"].is_null() ? "client " : result[0]["role"].as<std::string>();
            client.birth_date = result[0]["birth_date"].is_null() ? " " : result[0]["birth_date"].as<std::string>();
            client.createdAt = result[0]["created_at"].as<int64_t>();
            client.active = true;
            g_serverLogger.info("getClientById: id= " + std::to_string(id) + ", birth_date= " + client.birth_date);
            return client;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientById error:  " + std::string(e.what()));
            return std::nullopt;
        }
    }
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getDailyTicketCount
    // ИСПРА ВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    int getDailyTicketCount(const std::string& queueType) {
        try {
            pqxx::nontransaction txn{ *conn_ };
            auto result = txn.exec(
                "SELECT COUNT( ) FROM queue_tickets  "
                "WHERE queue_type = $1 AND status = 'waiting' ",
                pqxx::params{ queueType }
            );
            int count = result[0][0].as<int>();
            g_serverLogger.info("getDailyTicketCount: queueType= " + queueType +
                ", waiting= " + std::to_string(count) +
                " (criterion aligned with createTicket basePosition)");
            return count;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getDailyTicketCount error:  " + std::string(e.what()));
            return 0;
        }
    }
    // ========================================================================
     // ИСПРАВЛЕННЫЙ МЕТОД: createTicket
     // ========================================================================
    QueueTicket createTicket(int clientId, const std::string& queueType, int itemsCount) {
        QueueTicket ticket{};
        ticket.clientId = clientId;
        ticket.queueType = queueType;
        ticket.itemsCount = itemsCount;
        ticket.status = "";
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
            int basePosition = countResult[0][0].as<int>() + 1;
            g_serverLogger.info("createTicket: searching free number for queueType=" +
                queueType + ", basePosition=" + std::to_string(basePosition) +
                ", clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(itemsCount));
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
            if (displayNum == -1) {
                g_serverLogger.error("createTicket: NO AVAILABLE ticket number for queueType=" +
                    queueType + ", all 999 numbers are occupied by active tickets");
                return ticket;
            }
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%03d", displayNum);
            std::string number = prefix + std::string(numBuf);
            std::string window = "1";
            int waitTime = basePosition * 5;
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
            ticket.status = "waiting";
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
            ticket.status = "";
        }
        return ticket;
    }
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getWaitingTrustTickets
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    std::vector<json> getWaitingTrustTickets() {
        std::vector<json> result;
        try {
            pqxx::nontransaction txn{ *conn_ };
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
                g_serverLogger.warning("serveTrustTicket: ticket not accepted or already served: " +
                    ticketNumber);
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
    std::optional<std::pair<std::string, int>> createTrustAcceptance(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM trust_acceptances WHERE status = 'pending'"
            );
            int basePosition = countResult[0][0].as<int>() + 1;
            g_serverLogger.info("createTrustAcceptance: searching free number, "
                "basePosition=" + std::to_string(basePosition) +
                ", clientId=" + std::to_string(clientId));
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getTrustTicketInfo
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    std::optional<json> getTrustTicketInfo(const std::string& ticketNumber) {
        try {
            g_serverLogger.info("Get Trust Ticket info");
            pqxx::nontransaction txn{ *conn_ };
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
    json createFirstTimeTicket(const std::string& windowNumber = "1") {
        try {
            pqxx::work txn{ *conn_ };
            auto countResult = txn.exec(
                "SELECT COUNT(*) FROM first_time_tickets WHERE status = 'waiting'"
            );
            int basePosition = countResult[0][0].as<int>() + 1;
            g_serverLogger.info("createFirstTimeTicket: searching free number, "
                "basePosition=" + std::to_string(basePosition) +
                ", window=" + windowNumber);
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getWaitingFirstTimeTickets
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    std::vector<json> getWaitingFirstTimeTickets() {
        std::vector<json> result;
        try {
            pqxx::nontransaction txn{ *conn_ };
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
                g_serverLogger.warning("serveFirstTimeTicket: ticket not accepted or already served: " +
                    ticketNumber);
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
    bool serveTicket(const std::string& ticketNumber) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE queue_tickets SET status = 'served', served_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE number = $1 AND status = 'accepted'",
                pqxx::params{ ticketNumber }
            );
            if (res.affected_rows() == 0) {
                g_serverLogger.warning("serveTicket: ticket not accepted or already served: " +
                    ticketNumber);
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getQueueStatus
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    json getQueueStatus(const std::string& queueType) {
        json status;
        try {
            pqxx::nontransaction txn{ *conn_ };
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getUnsyncedItems
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    std::vector<json> getUnsyncedItems() {
        std::vector<json> items;
        try {
            pqxx::nontransaction txn{ *conn_ };
            auto result = txn.exec(
                "SELECT i.id, i.client_id, i.description, i.estimated_price, "
                "i.quantity, i.barcode, i.sold_quantity, "
                "c.phone, c.last_name, c.first_name, c.middle_name "
                "FROM items i "
                "JOIN clients c ON i.client_id = c.id "
                "WHERE i.synced_to_1c = FALSE"
            );
            for (const auto& row : result) {
                json item;
                item["id"] = row["id"].as<int>();
                item["client_id"] = row["client_id"].as<int>();
                item["description"] = sanitizeUTF8(row["description"].as<std::string>());
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["client_phone"] = sanitizeUTF8(row["phone"].as<std::string>());
                std::string last = sanitizeUTF8(row["last_name"].as<std::string>());
                std::string first = sanitizeUTF8(row["first_name"].as<std::string>());
                std::string mid = row["middle_name"].is_null() ? "" :
                    sanitizeUTF8(row["middle_name"].as<std::string>());
                item["client_name"] = last + " " + first + (mid.empty() ? "" : " " + mid);
                item["quantity"] = row["quantity"].as<int>();
                item["barcode"] = row["barcode"].is_null()
                    ? ""
                    : row["barcode"].as<std::string>();
                item["sold_quantity"] = row["sold_quantity"].as<int>();
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getWaitingTickets
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    std::vector<json> getWaitingTickets(const std::string& queueType) {
        std::vector<json> result;
        try {
            pqxx::nontransaction txn{ *conn_ };
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
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getClientItems()
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    // =========================================================================
    // ДОПОЛНЕНИЕ (2026-10-13): в выборку добавлены поля appendix_id и
    // appendix_number (через LEFT JOIN contract_appendices). Это необходимо
    // клиентской части терминала для фильтрации товаров по приложению к
    // договору в окне «Мои продажи → Выбрать товары по приложению».
    //
    // Логика, математика и бизнес-модель НЕ изменены:
    //   - LEFT JOIN не отбрасывает товары без приложения (appendix_id = NULL);
    //   - COALESCE(ca.appendix_number, 0) возвращает 0 для товаров без
    //     приложения, что корректно обрабатывается клиентом;
    //   - существующие поля и порядок сортировки сохранены;
    //   - операция только чтения (nontransaction), потокобезопасна.
    //
    // БД НЕ изменена: используются существующие таблицы items и
    // contract_appendices и существующий индекс idx_items_appendix_id.
    // =========================================================================
    std::vector<json> getClientItems(int clientId) {
        std::vector<json> result;
        try {
            pqxx::nontransaction txn{ *conn_ };
            auto res = txn.exec(
                "SELECT i.id, i.item_number, i.barcode, i.description, i.estimated_price, i.quantity, "
                "i.sale_date, i.condition, i.note, i.created_at, "
                "i.appendix_id, COALESCE(ca.appendix_number, 0) AS appendix_number, "
                "CASE "
                "  WHEN EXISTS ("
                "    SELECT 1 FROM item_sales s "
                "    WHERE s.item_id = i.id "
                "    AND s.quantity_sold > 0"
                "  ) THEN 'sold' "
                "  ELSE i.status "
                "END as status, "
                "i.client_percent, i.store_percent, i.client_amount, i.store_amount "
                "FROM items i "
                "LEFT JOIN contract_appendices ca ON ca.id = i.appendix_id "
                "WHERE i.client_id = $1 "
                "ORDER BY i.item_number",
                pqxx::params{ clientId }
            );
            for (const auto& row : res) {
                json item;
                item["id"] = row["id"].as<int>();
                item["item_number"] = row["item_number"].as<int>();
                item["barcode"] = row["barcode"].is_null()
                    ? "" : row["barcode"].as<std::string>();
                item["description"] = row["description"].as<std::string>();
                item["estimated_price"] = row["estimated_price"].as<double>();
                item["quantity"] = row["quantity"].as<int>();
                if (!row["sale_date"].is_null())
                    item["sale_date"] = row["sale_date"].as<int64_t>();
                else
                    item["sale_date"] = nullptr;
                item["condition"] = row["condition"].is_null()
                    ? "" : row["condition"].as<std::string>();
                item["note"] = row["note"].is_null()
                    ? "" : row["note"].as<std::string>();
                item["created_at"] = row["created_at"].as<int64_t>();
                item["status"] = row["status"].is_null()
                    ? "" : row["status"].as<std::string>();
                item["client_percent"] = row["client_percent"].is_null()
                    ? 0.0 : row["client_percent"].as<double>();
                item["store_percent"] = row["store_percent"].is_null()
                    ? 0.0 : row["store_percent"].as<double>();
                item["client_amount"] = row["client_amount"].is_null()
                    ? 0.0 : row["client_amount"].as<double>();
                item["store_amount"] = row["store_amount"].is_null()
                    ? 0.0 : row["store_amount"].as<double>();
                // =====================================================================
                // НОВОЕ: передача appendix_id и appendix_number клиенту.
                // appendix_id = 0 означает, что товар не привязан к приложению
                // (например, товары, сданные до введения приложений).
                // appendix_number = 0 — аналогично.
                // =====================================================================
                item["appendix_id"] = row["appendix_id"].is_null()
                    ? 0 : row["appendix_id"].as<long long>();
                item["appendix_number"] = row["appendix_number"].as<long long>();
                result.push_back(item);
            }
            g_serverLogger.info("getClientItems: " + std::to_string(result.size()) +
                " items for client " + std::to_string(clientId) +
                " (status computed from item_sales for accuracy, "
                "appendix_id/appendix_number included)");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getClientItems error: " + std::string(e.what()));
        }
        return result;
    }
    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: getNextItemNumber
    // =========================================================================
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
     * НОВОЕ: Метод всегда вставляет товар со штрих-кодом в формате SHOP + items.id.
     */
    bool addItem(int clientId, int itemNumber, const std::string& description,
        double price, int quantity, const std::string& condition,
        const std::string& note, pqxx::transaction_base& txn) {
        try {
            auto res = txn.exec(
                "INSERT INTO items (client_id, item_number, description, estimated_price, "
                "quantity, condition, note) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                "RETURNING id",
                pqxx::params{ clientId, itemNumber, description, price, quantity,
                              condition.empty() ? std::optional<std::string>{} : condition,
                              note.empty() ? std::optional<std::string>{} : note }
            );
            const int newItemId = res.at(0).at("id").as<int>();
            const std::string barcode = ShopBarcode::make(newItemId);
            if (barcode.empty()) {
                throw std::runtime_error(
                    "Failed to generate barcode for item id=" +
                    std::to_string(newItemId)
                );
            }
            txn.exec(
                "UPDATE items "
                "SET barcode = $1 "
                "WHERE id = $2",
                pqxx::params{
                    barcode,
                    newItemId
                }
            );
            g_serverLogger.info("Added item " + std::to_string(itemNumber) +
                " for client " + std::to_string(clientId) +
                ", barcode=" + barcode);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("addItem error: " + std::string(e.what()));
            return false;
        }
    }
    bool addItemsBatch(int clientId, const json& items, int workerId = 0,
        long long* outAppendixNumber = nullptr, long long reservedAppendixNumber = 0) {
        if (!items.is_array() || items.empty()) {
            g_serverLogger.warning("addItemsBatch: empty or invalid items array");
            return false;
        }
        try {
            pqxx::work txn{ *conn_ };
            int nextNumber = getNextItemNumber(clientId, txn);
            g_serverLogger.info("addItemsBatch: clientId=" + std::to_string(clientId) +
                ", itemsCount=" + std::to_string(items.size()) +
                ", startingNumber=" + std::to_string(nextNumber) +
                ", workerId=" + std::to_string(workerId));
            long long appendixNumber;
            if (reservedAppendixNumber > 0) {
                appendixNumber = reservedAppendixNumber;
                g_serverLogger.info("addItemsBatch: using RESERVED globally unique appendix_number=" +
                    std::to_string(appendixNumber) +
                    " for clientId=" + std::to_string(clientId));
            }
            else {
                auto numRes = txn.exec("SELECT nextval('contract_appendices_number_seq')");
                appendixNumber = numRes.at(0).at(0).as<long long>();
                g_serverLogger.info("addItemsBatch: generated globally unique appendix_number=" +
                    std::to_string(appendixNumber) +
                    " for clientId=" + std::to_string(clientId));
            }
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
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t validUntil = nowSec + 15 * 86400;
            std::optional<int> workerIdOpt = (workerId > 0) ?
                std::optional<int>(workerId) : std::nullopt;
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
                auto itemRes = txn.exec(
                    "INSERT INTO items ("
                    "client_id, "
                    "item_number, "
                    "description, "
                    "estimated_price, "
                    "quantity, "
                    "condition, "
                    "note, "
                    "worker_id, "
                    "client_percent, "
                    "store_percent, "
                    "client_amount, "
                    "store_amount, "
                    "appendix_id, "
                    "barcode, "
                    "sync_status"
                    ") "
                    "VALUES ("
                    "$1, $2, $3, $4, $5, $6, $7, $8, "
                    "$9, $10, $11, $12, $13, NULL, 'pending'"
                    ") "
                    "RETURNING id",
                    pqxx::params{
                        clientId,
                        nextNumber++,
                        desc,
                        price,
                        quantity,
                        condition.empty()
                            ? std::optional<std::string>{}
                            : condition,
                        note.empty()
                            ? std::optional<std::string>{}
                            : note,
                        workerIdOpt,
                        clientPercent,
                        storePercent,
                        clientAmount,
                        storeAmount,
                        appendixId
                    }
                );
                const int itemId =
                    itemRes.at(0).at("id").as<int>();
                const std::string barcode = ShopBarcode::make(itemId);
                if (barcode.empty()) {
                    throw std::runtime_error(
                        "Failed to generate barcode for item id=" +
                        std::to_string(itemId)
                    );
                }
                txn.exec(
                    "UPDATE items "
                    "SET barcode = $1 "
                    "WHERE id = $2",
                    pqxx::params{
                        barcode,
                        itemId
                    }
                );
                g_serverLogger.info(
                    "addItemsBatch: created item id=" +
                    std::to_string(itemId) +
                    ", barcode=" +
                    barcode +
                    ", clientId=" +
                    std::to_string(clientId)
                );
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
    // ИСПРАВЛЕННЫЙ МЕТОД: ВЫБОРКА ТОВАРОВ ПРИЛОЖЕНИЯ + СЧЁТЧИКИ
    // =========================================================================
    json getAppendixItemsAndCounters(long long appendixId,
        pqxx::transaction_base& txn) {
        json itemsArray = json::array();
        int soldCount = 0, returnedCount = 0, pendingCount = 0;
        try {
            g_serverLogger.info(
                "getAppendixItemsAndCounters: querying items "
                "for appendixId=" + std::to_string(appendixId) +
                " using caller transaction");
            auto res = txn.exec(
                "SELECT "
                "id, "
                "COALESCE(item_number, 0) AS item_number, "
                "COALESCE(barcode, 'SHOP' || id::text) AS barcode, "
                "COALESCE(description, '') AS description, "
                "COALESCE(quantity, 1) AS quantity, "
                "COALESCE(estimated_price, 0) AS estimated_price, "
                "COALESCE(condition, '') AS condition, "
                "COALESCE(note, '') AS note, "
                "COALESCE(status, 'pending') AS status, "
                "COALESCE(client_amount, 0) AS client_amount "
                "FROM items "
                "WHERE appendix_id = $1 "
                "ORDER BY item_number ASC, id ASC",
                pqxx::params{ appendixId }
            );
            g_serverLogger.info(
                "getAppendixItemsAndCounters: SQL returned " +
                std::to_string(res.size()) + " rows for appendixId=" +
                std::to_string(appendixId));
            for (const auto& row : res) {
                json it;
                it["id"] = row["id"].as<int>();
                it["item_number"] = row["item_number"].as<int>();
                it["barcode"] = row["barcode"].as<std::string>();
                it["description"] = row["description"].as<std::string>();
                it["quantity"] = row["quantity"].as<int>();
                it["estimated_price"] = row["estimated_price"].as<double>();
                it["condition"] = row["condition"].as<std::string>();
                it["note"] = row["note"].as<std::string>();
                it["status"] = row["status"].as<std::string>();
                it["client_amount"] = row["client_amount"].as<double>();
                int qty = row["quantity"].as<int>();
                std::string st = it["status"].get<std::string>();
                if (st == "sold") soldCount += qty;
                else if (st == "low_quality" || st == "unsold_quality")
                    returnedCount += qty;
                else pendingCount += qty;
                itemsArray.push_back(it);
                g_serverLogger.info(
                    "getAppendixItemsAndCounters: item id=" +
                    std::to_string(it["id"].get<int>()) +
                    ", barcode=" + it["barcode"].get<std::string>() +
                    ", desc=" + it["description"].get<std::string>() +
                    ", qty=" + std::to_string(qty) +
                    ", status=" + st);
            }
            g_serverLogger.info(
                "getAppendixItemsAndCounters: appendixId=" +
                std::to_string(appendixId) +
                ", items=" + std::to_string(itemsArray.size()) +
                ", sold=" + std::to_string(soldCount) +
                ", returned=" + std::to_string(returnedCount) +
                ", pending=" + std::to_string(pendingCount));
        }
        catch (const std::exception& e) {
            g_serverLogger.error(
                "getAppendixItemsAndCounters error: " +
                std::string(e.what()));
        }
        json out;
        out["items"] = itemsArray;
        out["sold_count"] = soldCount;
        out["returned_count"] = returnedCount;
        out["pending_count"] = pendingCount;
        return out;
    }
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: ВЫБОРКА ПРИЛОЖЕНИЯ ПО НОМЕРУ
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    json getAppendixByNumber(long long appendixNumber) {
        json result;
        try {
            // ОДНА рабочая транзакция на всё чтение — нет наложения nontransaction + work
            pqxx::work txn{ *conn_ };
            g_serverLogger.info(
                "getAppendixByNumber: querying appendix_number=" +
                std::to_string(appendixNumber));
            auto res = txn.exec(
                "SELECT a.id, a.appendix_number, a.client_id, "
                "a.worker_id, a.created_at, "
                "a.valid_until, a.total_quantity, a.total_value, "
                "a.total_client_amount, "
                "c.last_name || ' ' || c.first_name || "
                "COALESCE(' ' || c.middle_name, '') AS client_name "
                "FROM contract_appendices a "
                "LEFT JOIN clients c ON a.client_id = c.id "
                "WHERE a.appendix_number = $1",
                pqxx::params{ appendixNumber });
            if (res.empty()) {
                g_serverLogger.warning(
                    "getAppendixByNumber: appendix not found, number=" +
                    std::to_string(appendixNumber));
                result["error"] = "Appendix not found";
                return result;   // txn.abort() неявно через деструктор
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
            g_serverLogger.info(
                "getAppendixByNumber: found appendix_id=" +
                std::to_string(row["id"].as<long long>()) +
                " for number=" + std::to_string(appendixNumber));
            // ВАЖНО: getAppendixItemsAndCounters получает ТУ ЖЕ транзакцию.
            // Это исключает наложение двух транзакций на одном connection,
            // которое приводило к потере массива items и itemId=0 в PriceTagPrinter.
            json itemsBlock = getAppendixItemsAndCounters(
                row["id"].as<long long>(), txn);
            result["items"] = itemsBlock["items"];
            result["sold_count"] = itemsBlock["sold_count"];
            result["returned_count"] = itemsBlock["returned_count"];
            result["pending_count"] = itemsBlock["pending_count"];
            txn.commit();
            g_serverLogger.info(
                "getAppendixByNumber: number=" +
                std::to_string(appendixNumber) +
                ", items=" + std::to_string(itemsBlock["items"].size()) +
                " returned successfully");
        }
        catch (const std::exception& e) {
            g_serverLogger.error(
                "getAppendixByNumber error: " + std::string(e.what()));
            result["error"] = e.what();
        }
        return result;
    }
    // ========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: ПОСЛЕДНЕЕ ПРИЛОЖЕНИЕ КЛИЕНТА
    // ИСПРАВЛЕНИЕ: pqxx::work -> pqxx::nontransaction для операции только чтения
    // ========================================================================
    json getLatestAppendix(int clientId, int workerId) {
        json result;
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("getLatestAppendix: querying latest appendix for clientId=" +
                std::to_string(clientId) + ", workerId=" + std::to_string(workerId));
            auto res = txn.exec(
                "SELECT id, appendix_number, created_at, valid_until, "
                "total_quantity, total_value, total_client_amount "
                "FROM contract_appendices WHERE client_id = $1 AND worker_id = $2 "
                "ORDER BY created_at DESC, id DESC LIMIT 1",
                pqxx::params{ clientId, workerId });
            if (res.empty()) {
                g_serverLogger.warning("getLatestAppendix: no appendices found for clientId=" +
                    std::to_string(clientId) + ", workerId=" + std::to_string(workerId));
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
            g_serverLogger.info("getLatestAppendix: found appendix_number=" +
                std::to_string(result["appendix_number"].get<long long>()) +
                ", appendix_id=" + std::to_string(row["id"].as<long long>()) +
                " for clientId=" + std::to_string(clientId) +
                ", workerId=" + std::to_string(workerId));
            // =====================================================================
            // ИСПРАВЛЕНИЕ КОМПИЛЯЦИИ: используем ТУ ЖЕ транзакцию txn.
            // Ранее здесь стоял вызов с переменной subTxn, объявление которой
            // удалено. Теперь getAppendixItemsAndCounters получает txn —
            // никаких вложенных транзакций на одном pqxx::connection нет.
            // =====================================================================
            json itemsBlock = getAppendixItemsAndCounters(row["id"].as<long long>(), txn);
            result["items"] = itemsBlock["items"];
            txn.commit();
            g_serverLogger.info("getLatestAppendix: clientId=" + std::to_string(clientId) +
                ", appendix_number=" + std::to_string(result["appendix_number"].get<long long>()) +
                ", items=" + std::to_string(itemsBlock["items"].size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getLatestAppendix error: " + std::string(e.what()));
            result["error"] = e.what();
        }
        return result;
    }
    long long getNextAppendixNumber(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec("SELECT nextval('contract_appendices_number_seq')");
            long long nextNumber = res.at(0).at(0).as<long long>();
            txn.commit();
            g_serverLogger.info("getNextAppendixNumber: globally unique number=" +
                std::to_string(nextNumber) + " reserved for clientId=" +
                std::to_string(clientId));
            return nextNumber;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getNextAppendixNumber error: " + std::string(e.what()));
            return -1;
        }
    }
    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ ПАНЕЛИ ДИРЕКТОРА
    // =========================================================================
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
    bool setClientBlocked(int clientId, bool blocked, int directorId,
        const std::string& blockReason = "") {
        try {
            pqxx::work txn{ *conn_ };
            if (blocked) {
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
    json getDirectorStats() {
        json result;
        try {
            pqxx::work txn{ *conn_ };
            // 1. СТАТИСТИКА ПО ВСЕМ КОМИТЕНТАМ
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
                client["total_client_amount"] = row["total_client_amount"].as<double>();
                client["total_store_amount"] = row["total_store_amount"].as<double>();
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
            // 2. ЭФФЕКТИВНОСТЬ ТОВАРОВЕДОВ
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
            // 3. ТОВАРЫ НЕ ПРОДАННЫЕ ИЗ-ЗА НИЗКОГО КАЧЕСТВА
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
            // 4. ОБЩАЯ СВОДКА
            json summary;
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
            // 5. ТОВАРЫ, ВЫБЫВШИЕ ИЗ ПРОДАЖИ ПО ИСТЕЧЕНИИ 15-ДНЕВНОГО СРОКА
            json expiredArray = getExpiredItems(txn);
            result["expired_items"] = expiredArray;
            result["expired_items_count"] = static_cast<int>(expiredArray.size());
            g_serverLogger.info("getDirectorStats: expired_items_count=" +
                std::to_string(expiredArray.size()));
            // 6. ДОПОЛНЕНИЕ СВОДКИ: СЧЁТЧИК ПРОСРОЧЕННЫХ ТОВАРОВ
            summary["expired_items_count"] = static_cast<int>(expiredArray.size());
            result["summary"] = summary;
            g_serverLogger.info("getDirectorStats: completed successfully, clients=" +
                std::to_string(clientsArray.size()) +
                ", workers=" + std::to_string(workersArray.size()) +
                ", low_quality_items=" + std::to_string(lowQualityArray.size()) +
                ", expired_items=" + std::to_string(expiredArray.size()));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getDirectorStats error: " + std::string(e.what()));
            result["error"] = e.what();
        }
        return result;
    }
    json getAllClients(bool includeBlocked = true) {
        json result = json::array();
        try {
            // ИСПРАВЛЕНИЕ: Используем nontransaction для операции только чтения
            pqxx::nontransaction txn{ *conn_ };
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
    // ИСПРАВЛЕННЫЙ МЕТОД: РЕГИСТРАЦИЯ ПРОДАЖИ ОДНОЙ ЕДИНИЦЫ ТОВАРА
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Явное приведение типа параметра $3 к VARCHAR
    // =========================================================================
    json registerItemSale(
        int itemId,
        double salePrice,
        const std::string& source,
        const std::string& receiptNumber,
        const std::string& barcodePayload,
        const std::string& idempotencyKey,
        const std::string& fiscalDocumentNumber = "",
        const std::string& fiscalDocumentUuid = "",
        const std::string& fiscalSign = "")
    {
        json result;
        try {
            // 1. Базовая валидация входных параметров
            if (itemId <= 0) {
                result["success"] = false;
                result["error"] = "Invalid item_id";
                return result;
            }
            if (salePrice < 0.0) {
                result["success"] = false;
                result["error"] = "Invalid sale price";
                return result;
            }
            if (idempotencyKey.empty()) {
                result["success"] = false;
                result["error"] = "idempotency_key is required";
                return result;
            }
            g_serverLogger.info("registerItemSale: starting process for itemId=" + std::to_string(itemId) +
                ", salePrice=" + std::to_string(salePrice) + ", source=" + source +
                ", idempotencyKey=" + idempotencyKey);
            pqxx::work txn{ *conn_ };
            // 2. Проверка на дубликаты по idempotency_key (защита от повторных пакетов 1С при таймаутах)
            auto existing = txn.exec(
                "SELECT id, item_id, client_id, quantity_sold, sale_price, receipt_number "
                "FROM item_sales "
                "WHERE idempotency_key = $1 "
                "LIMIT 1",
                pqxx::params{ idempotencyKey }
            );
            if (!existing.empty()) {
                g_serverLogger.warning("registerItemSale: duplicate idempotency_key detected: " + idempotencyKey);
                result["success"] = true;
                result["duplicate"] = true;
                result["sale_id"] = existing[0]["id"].as<int>();
                result["item_id"] = existing[0]["item_id"].as<int>();
                result["client_id"] = existing[0]["client_id"].as<int>();
                result["sale_price"] = existing[0]["sale_price"].as<double>();
                result["quantity_sold"] = existing[0]["quantity_sold"].as<int>();
                result["receipt_number"] = existing[0]["receipt_number"].is_null()
                    ? "" : existing[0]["receipt_number"].as<std::string>();
                // ИСПРАВЛЕНИЕ: Явно фиксируем транзакцию перед досрочным возвратом успешного дубликата
                txn.commit();
                return result;
            }
            // 3. Блокировка строки товара для предотвращения гонок (FOR UPDATE)
            auto itemRes = txn.exec(
                "SELECT id, client_id, quantity, sold_quantity, estimated_price, "
                "client_percent, store_percent, status, barcode "
                "FROM items "
                "WHERE id = $1 "
                "FOR UPDATE",
                pqxx::params{ itemId }
            );
            if (itemRes.empty()) {
                g_serverLogger.error("registerItemSale: item NOT FOUND for itemId=" + std::to_string(itemId));
                result["success"] = false;
                result["error"] = "Item not found";
                // ИСПРАВЛЕНИЕ: Явно отменяем транзакцию при ошибке
                txn.abort();
                return result;
            }
            const auto& row = itemRes.at(0);
            const int clientId = row["client_id"].as<int>();
            const int quantity = row["quantity"].as<int>();
            const int soldQuantity = row["sold_quantity"].as<int>();
            const int available = quantity - soldQuantity;
            const std::string status = row["status"].is_null()
                ? "pending" : row["status"].as<std::string>();
            const std::string storedBarcode = row["barcode"].is_null()
                ? "" : row["barcode"].as<std::string>();
            g_serverLogger.info("registerItemSale: item locked. itemId=" + std::to_string(itemId) +
                ", clientId=" + std::to_string(clientId) + ", available=" + std::to_string(available) +
                ", currentStatus=" + status);
            // 4. Проверка соответствия физического штрих-кода
            if (!storedBarcode.empty() && !barcodePayload.empty() &&
                storedBarcode != barcodePayload) {
                g_serverLogger.error("registerItemSale: barcode mismatch. expected=" + storedBarcode +
                    ", received=" + barcodePayload);
                result["success"] = false;
                result["error"] = "Barcode does not match item";
                result["expected_barcode"] = storedBarcode;
                result["received_barcode"] = barcodePayload;
                txn.abort();
                return result;
            }
            // 5. Проверка блокировки комитента (защита от продаж заблокированным клиентам)
            auto clientRes = txn.exec(
                "SELECT is_blocked FROM clients WHERE id = $1",
                pqxx::params{ clientId }
            );
            if (!clientRes.empty() && clientRes[0]["is_blocked"].as<bool>()) {
                g_serverLogger.warning("registerItemSale: REJECTED. Client " + std::to_string(clientId) + " is blocked");
                result["success"] = false;
                result["error"] = "Client is blocked";
                result["client_id"] = clientId;
                txn.abort();
                return result;
            }
            // 6. Проверка доступного остатка
            if (available <= 0) {
                // ========================================================================
                // ИСПРАВЛЕНИЕ (2026-10-13): фиксируем idempotency_key служебной записью
                // в item_sales (quantity_sold = 0), чтобы предотвратить бесконечный цикл
                // «skipped historical sale» на каждом проходе syncSalesFrom1C.
                //
                // Записи с quantity_sold = 0:
                //   - НЕ влияют на статус товара (getClientItems фильтрует quantity_sold > 0);
                //   - НЕ влияют на математику продаж (registerItemSale больше не вызывается
                //     для этой пары receipt+sku — срабатывает проверка дубликата выше);
                //   - НЕ изменяют структуру БД и бизнес-модель;
                //   - служат исключительно маркером «эта физическая продажа уже обработана».
                // ========================================================================
                g_serverLogger.warning("registerItemSale: REJECTED. No available units for itemId="
                    + std::to_string(itemId) + ", фиксируем idempotency_key служебной записью");
                // Сохраняем маркер (нулевая продажа), чтобы на следующем цикле
                // сработала проверка дубликата в начале метода и не было повторного
                // обращения к items FOR UPDATE.
                txn.exec(
                    "INSERT INTO item_sales ("
                    "  item_id, client_id, quantity_sold, sale_price, client_amount, "
                    "  store_amount, source, receipt_number, barcode_payload, "
                    "  idempotency_key"
                    ") VALUES ($1, $2, 0, $3, 0, 0, $4, $5, $6, $7)",
                    pqxx::params{
                        itemId,
                        clientId,
                        salePrice,
                        source,
                        receiptNumber.empty()
                            ? std::optional<std::string>{}
                            : std::optional<std::string>(receiptNumber),
                        barcodePayload.empty()
                            ? std::optional<std::string>{}
                            : std::optional<std::string>(barcodePayload),
                        idempotencyKey
                    }
                );
                txn.commit();
                result["success"] = true;   // ← теперь это не ошибка, а «ранее обработано»
                result["duplicate"] = true;
                result["error"] = "No available units";
                result["item_id"] = itemId;
                result["available"] = 0;
                return result;
            }
            // 7. Проверка статуса товара (защита от продажи бракованных или уже проданных вещей)
            if (status == "sold" ||
                status == "low_quality" ||
                status == "unsold_quality") {
                g_serverLogger.warning("registerItemSale: sale REJECTED for itemId=" +
                    std::to_string(itemId) + ", status=" + status +
                    ", idempotencyKey=" + idempotencyKey +
                    " — фиксируем маркер, товар уже в конечном статусе");
                txn.exec(
                    "INSERT INTO item_sales ("
                    "  item_id, client_id, quantity_sold, sale_price, client_amount, "
                    "  store_amount, source, receipt_number, barcode_payload, "
                    "  idempotency_key"
                    ") VALUES ($1, $2, 0, $3, 0, 0, $4, $5, $6, $7)",
                    pqxx::params{
                        itemId, clientId, salePrice, source,
                        receiptNumber.empty() ? std::optional<std::string>{}
                                              : std::optional<std::string>(receiptNumber),
                        barcodePayload.empty() ? std::optional<std::string>{}
                                               : std::optional<std::string>(barcodePayload),
                        idempotencyKey
                    }
                );
                txn.commit();
                result["success"] = true;
                result["duplicate"] = true;
                result["error"] = "Item is not available for sale";
                result["status"] = status;
                return result;
            }
            if (status == "expired") {
                g_serverLogger.info("registerItemSale: registering sale for EXPIRED item "
                    "itemId=" + std::to_string(itemId) +
                    ", clientId=" + std::to_string(clientId) +
                    ", salePrice=" + std::to_string(salePrice) +
                    " (expired status overridden by actual sale fact)");
            }
            // 8. Математика распределения выручки
            const double clientPercent = row["client_percent"].is_null()
                ? 0.0 : row["client_percent"].as<double>();
            const double storePercent = row["store_percent"].is_null()
                ? 0.0 : row["store_percent"].as<double>();
            const double clientAmount = salePrice * clientPercent / 100.0;
            const double storeAmount = salePrice * storePercent / 100.0;
            const int newSoldQuantity = soldQuantity + 1;
            const std::string newStatus = (newSoldQuantity >= quantity)
                ? "sold" : "pending";
            g_serverLogger.info("registerItemSale: executing UPDATE for itemId=" + std::to_string(itemId) +
                ", newSoldQuantity=" + std::to_string(newSoldQuantity) +
                ", newStatus='" + newStatus + "'");
            // ========================================================================
            // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ ОШИБКИ PostgreSQL: "inconsistent types deduced for parameter $3"
            // ========================================================================
            // ПРОБЛЕМА:
            // Параметр $3 (newStatus типа std::string) передаётся из C++ как тип 'text'.
            // В контексте 'status = $3' PostgreSQL видит колонку VARCHAR(20) и пытается
            // привести $3 к character varying. В контексте '$3 = sold::VARCHAR' PostgreSQL
            // видит сравнение с character varying, но сам $3 всё ещё text.
            // Планировщик не может согласовать типы и выбрасывает ошибку.
            //
            // РЕШЕНИЕ:
            // Явно приводим тип параметра $3 к VARCHAR в самом SQL-запросе:
            // - status = $3::VARCHAR (приведение при присваивании)
            // - WHEN $3::VARCHAR = 'sold' (приведение при сравнении)
            // Теперь PostgreSQL использует character varying для $3 во всех местах.
            // ========================================================================
            txn.exec(
                "UPDATE items "
                "SET sold_quantity = $2, "
                "    status = $3::VARCHAR, "
                "    sale_date = CASE "
                "        WHEN $3::VARCHAR = 'sold' THEN EXTRACT(EPOCH FROM NOW()) "
                "        ELSE sale_date END "
                "WHERE id = $1",
                pqxx::params{ itemId, newSoldQuantity, newStatus }
            );
            // 9. Регистрация факта продажи в таблице логов item_sales
            auto saleRes = txn.exec(
                "INSERT INTO item_sales ("
                "item_id, client_id, quantity_sold, sale_price, client_amount, "
                "store_amount, source, receipt_number, barcode_payload, "
                "idempotency_key, fiscal_document_number, fiscal_document_uuid, "
                "fiscal_sign, fiscalized_at"
                ") VALUES ("
                "$1, $2, 1, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, "
                "EXTRACT(EPOCH FROM NOW())"
                ") RETURNING id",
                pqxx::params{
                    itemId, clientId, salePrice, clientAmount, storeAmount,
                    source,
                    receiptNumber.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>(receiptNumber),
                    barcodePayload.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>(barcodePayload),
                    idempotencyKey,
                    fiscalDocumentNumber.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>(fiscalDocumentNumber),
                    fiscalDocumentUuid.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>(fiscalDocumentUuid),
                    fiscalSign.empty()
                        ? std::optional<std::string>{}
                        : std::optional<std::string>(fiscalSign)
                }
            );
            const int saleId = saleRes.at(0).at("id").as<int>();
            // 10. Фиксация транзакции
            txn.commit();
            // 11. Формирование успешного ответа
            result["success"] = true;
            result["duplicate"] = false;
            result["sale_id"] = saleId;
            result["item_id"] = itemId;
            result["client_id"] = clientId;
            result["barcode"] = storedBarcode;
            result["sale_price"] = salePrice;
            result["client_amount"] = clientAmount;
            result["store_amount"] = storeAmount;
            result["remaining_units"] = quantity - newSoldQuantity;
            result["status"] = newStatus;
            result["idempotency_key"] = idempotencyKey;
            g_serverLogger.info("registerItemSale: SUCCESS saleId=" + std::to_string(saleId) +
                ", itemId=" + std::to_string(itemId) +
                ", clientId=" + std::to_string(clientId) +
                ", newStatus=" + newStatus +
                ", wasExpired=" + std::to_string(status == "expired"));
            return result;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("registerItemSale error: " + std::string(e.what()));
            result["success"] = false;
            result["error"] = e.what();
            return result;
        }
    }
    // =========================================================================
    // ПОИСК ТОВАРА ПО ФИЗИЧЕСКОМУ ШТРИХ-КОДУ
    // =========================================================================
    json findItemByBarcode(const std::string& barcode) {
        json result;
        try {
            std::string normalizedBarcode = barcode;
            normalizedBarcode.erase(std::remove(normalizedBarcode.begin(), normalizedBarcode.end(), ' '), normalizedBarcode.end());
            if (!ShopBarcode::isValid(barcode)) {
                result["error"] = "Invalid SHOP barcode";
                result["barcode"] = barcode;
                return result;
            }
            // ИСПРАВЛЕНИЕ: Используем nontransaction для операции только чтения
            pqxx::nontransaction txn{ *conn_ };
            auto res = txn.exec(
                "SELECT "
                "i.id, "
                "i.barcode, "
                "i.item_number, "
                "i.client_id, "
                "i.description, "
                "i.estimated_price, "
                "i.quantity, "
                "i.sold_quantity, "
                "i.status, "
                "i.client_percent, "
                "i.store_percent, "
                "i.client_amount, "
                "i.store_amount, "
                "i.appendix_id, "
                "COALESCE(ca.appendix_number, 0) AS appendix_number, "
                "c.is_blocked "
                "FROM items i "
                "LEFT JOIN clients c ON c.id = i.client_id "
                "LEFT JOIN contract_appendices ca ON ca.id = i.appendix_id "
                "WHERE REPLACE(i.barcode, ' ', '') = $1 "
                "LIMIT 1",
                pqxx::params{ normalizedBarcode }
            );
            if (res.empty()) {
                g_serverLogger.warning(
                    "findItemByBarcode: item not found, barcode=" + barcode);
                result["error"] = "Item not found";
                result["barcode"] = barcode;
                return result;
            }
            const auto& row = res.at(0);
            const int itemId = row["id"].as<int>();
            const int clientId = row["client_id"].as<int>();
            const int quantity = row["quantity"].as<int>();
            const int soldQuantity = row["sold_quantity"].as<int>();
            const int available = quantity - soldQuantity;
            result["id"] = itemId;
            result["barcode"] = row["barcode"].is_null() ? "" : row["barcode"].as<std::string>();
            result["item_number"] = row["item_number"].is_null() ? 0 : row["item_number"].as<int>();
            result["client_id"] = clientId;
            result["description"] = row["description"].as<std::string>();
            result["estimated_price"] = row["estimated_price"].as<double>();
            result["quantity"] = quantity;
            result["sold_quantity"] = soldQuantity;
            result["available"] = available;
            result["status"] = row["status"].is_null() ? "pending" : row["status"].as<std::string>();
            result["client_percent"] = row["client_percent"].is_null() ? 0.0 : row["client_percent"].as<double>();
            result["store_percent"] = row["store_percent"].is_null() ? 0.0 : row["store_percent"].as<double>();
            result["client_amount"] = row["client_amount"].is_null() ? 0.0 : row["client_amount"].as<double>();
            result["store_amount"] = row["store_amount"].is_null() ? 0.0 : row["store_amount"].as<double>();
            result["appendix_id"] = row["appendix_id"].is_null() ? 0 : row["appendix_id"].as<long long>();
            result["appendix_number"] = row["appendix_number"].as<long long>();
            result["client_blocked"] = row["is_blocked"].is_null() ? false : row["is_blocked"].as<bool>();
            g_serverLogger.info(
                "findItemByBarcode: FOUND "
                "barcode=" + barcode +
                ", itemId=" + std::to_string(itemId) +
                ", clientId=" + std::to_string(clientId) +
                ", appendixId=" + std::to_string(result["appendix_id"].get<long long>()) +
                ", appendixNumber=" + std::to_string(result["appendix_number"].get<long long>()) +
                ", available=" + std::to_string(available));
            return result;
        }
        catch (const std::exception& e) {
            g_serverLogger.error(
                "findItemByBarcode error: " +
                std::string(e.what())
            );
            result["error"] =
                e.what();
            return result;
        }
    }
    // =========================================================================
    // НОВЫЙ МЕТОД: ОБРАБОТКА ПРОСРОЧЕННЫХ ПРИЛОЖЕНИЙ (15-дневный SLA)
    // =========================================================================
    int processExpiredAppendices() {
        int processedCount = 0;
        try {
            pqxx::work txn{ *conn_ };
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
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
    // МЕТОД-ОБЁРТКА: ПОЛУЧЕНИЕ ПРОСРОЧЕННЫХ ТОВАРОВ ДЛЯ ПАНЕЛИ ДИРЕКТОРА
    // =========================================================================
    json getExpiredItems() {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info(
                "getExpiredItems(): created own transaction, "
                "delegating to getExpiredItems(txn)");
            result = getExpiredItems(txn);
            g_serverLogger.info("getExpiredItems(): completed, returned " +
                std::to_string(result.size()) + " expired items");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getExpiredItems() error: " +
                std::string(e.what()));
        }
        return result;
    }
    // =========================================================================
    // ИСПРАВЛЕННЫЙ МЕТОД: ПОЛУЧЕНИЕ ПРОСРОЧЕННЫХ ТОВАРОВ ДЛЯ ПАНЕЛИ ДИРЕКТОРА
    // (основная реализация, выполняется в переданной транзакции)
    // =========================================================================
    json getExpiredItems(pqxx::transaction_base& txn) {
        json result = json::array();
        try {
            g_serverLogger.info(
                "getExpiredItems(txn): querying expired items "
                "using caller transaction");
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
            g_serverLogger.info(
                "getExpiredItems(txn): SQL returned " +
                std::to_string(res.size()) + " rows");
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
                g_serverLogger.info(
                    "getExpiredItems(txn): item id=" +
                    std::to_string(item["id"].get<int>()) +
                    ", desc=" + item["description"].get<std::string>() +
                    ", expired_at=" + std::to_string(item["expired_at"].get<int64_t>()));
            }
            g_serverLogger.info("getExpiredItems(txn): returned " + std::to_string(result.size()) +
                " expired items");
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getExpiredItems(txn) error: " + std::string(e.what()));
        }
        return result;
    }
    // ========================================================================
    // НОВЫЙ МЕТОД: РЕГИСТРАЦИЯ КОМИТЕНТА С ДОПОЛНИТЕЛЬНЫМИ ДАННЫМИ
    // ========================================================================
    std::pair<bool, int> registerCommittee(const std::string& phone,
        const std::string& last_name, const std::string& first_name,
        const std::string& middle_name, const std::string& birth_date,
        const std::string& passport_type, const std::string& passport_series,
        const std::string& passport_number, const std::string& address) {
        try {
            pqxx::work txn{ *conn_ };
            auto exist = txn.exec("SELECT id FROM clients WHERE phone = $1", pqxx::params{ phone });
            int clientId = 0;
            if (!exist.empty()) {
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
    // =========================================================================
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
    json getClientFullInfo(int clientId) {
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("getClientFullInfo: request for clientId=" +
                std::to_string(clientId));
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
    json searchClientsByFio(const std::string& lastName,
        const std::string& firstName,
        const std::string& middleName) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            g_serverLogger.info("searchClientsByFio: lastName='" + lastName +
                "', firstName='" + firstName +
                "', middleName='" + middleName + "'");
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
    json getClientSoldItemsForReward(int clientId) {
        json result = json::array();
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT s.id AS sale_id, s.item_id, s.quantity_sold, "
                "s.sale_price, s.client_amount, s.sold_at, "
                "i.description, i.appendix_id, i.barcode,"
                "a.appendix_number "
                "FROM item_sales s "
                "JOIN items i ON s.item_id = i.id "
                "LEFT JOIN contract_appendices a ON i.appendix_id = a.id "
                "WHERE s.client_id = $1 AND s.quantity_sold > 0 "
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
                item["barcode"] = row["barcode"].is_null()
                    ? "" : row["barcode"].as<std::string>();
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
    // =========================================================================
    // МЕТОД: getClientAppendices(int clientId)
    // Возвращает список приложений к договору для конкретного комитента.
    // Используется:
    //   - кассиром через /api/v1/cashier/appendices;
    //   - клиентом через НОВЫЙ роут /api/v1/clients/my_appendices
    //     (для окна «Мои продажи → Выбрать товары по приложению»).
    //
    // Потокобезопасность: pqxx::work — отдельная транзакция на вызов.
    // Операция только чтения (SELECT), данные не изменяет.
    // =========================================================================
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
    int createCashierDocument(int clientId, const std::string& docType,
        const std::string& itemsData, double totalAmount) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT COALESCE(MAX(doc_number), 0) + 1 AS next_number "
                "FROM cashier_documents "
                "WHERE client_id = $1 AND doc_type = $2",
                pqxx::params{ clientId, docType }
            );
            int docNumber = res[0]["next_number"].as<int>();
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
    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ ПОДДЕРЖКИ REFRESH TOKEN
    // =========================================================================
    std::optional<int> findActiveRefreshToken(const std::string& refresh_token_hash) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT client_id FROM auth_tokens "
                "WHERE refresh_token_hash = $1 "
                "  AND revoked = FALSE "
                "  AND expires_at > EXTRACT(EPOCH FROM NOW()) "
                "LIMIT 1",
                pqxx::params{ refresh_token_hash }
            );
            if (res.empty()) {
                g_serverLogger.warning("findActiveRefreshToken: not found or revoked/expired");
                return std::nullopt;
            }
            int clientId = res[0]["client_id"].as<int>();
            g_serverLogger.info("findActiveRefreshToken: found for clientId=" + std::to_string(clientId));
            return clientId;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("findActiveRefreshToken error: " + std::string(e.what()));
            return std::nullopt;
        }
    }
    bool revokeRefreshToken(const std::string& refresh_token_hash) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "UPDATE auth_tokens SET revoked = TRUE "
                "WHERE refresh_token_hash = $1",
                pqxx::params{ refresh_token_hash }
            );
            txn.commit();
            g_serverLogger.info("revokeRefreshToken: revoked=" + std::to_string(res.affected_rows()) + " rows");
            return res.affected_rows() > 0;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("revokeRefreshToken error: " + std::string(e.what()));
            return false;
        }
    }
    // =========================================================================
    // НОВЫЕ МЕТОДЫ ДЛЯ ЭКВАЙРИНГА ВТБ
    // =========================================================================
    int createAcquiringOrder(const std::string& orderNumber, int clientId, double amount,
        long long amountCop, const std::string& docType, int docId) {
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "INSERT INTO acquiring_orders (order_number, client_id, amount, amount_cop, doc_type, doc_id) "
                "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id",
                pqxx::params{ orderNumber, clientId, amount, amountCop, docType, docId }
            );
            int id = res[0]["id"].as<int>();
            txn.commit();
            g_serverLogger.info("createAcquiringOrder: orderNumber=" + orderNumber +
                ", clientId=" + std::to_string(clientId) +
                ", amount=" + std::to_string(amount));
            return id;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("createAcquiringOrder error: " + std::string(e.what()));
            return 0;
        }
    }
    bool updateAcquiringOrderVtbId(const std::string& orderNumber, const std::string& vtbOrderId) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "UPDATE acquiring_orders SET vtb_order_id = $1, updated_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE order_number = $2",
                pqxx::params{ vtbOrderId, orderNumber }
            );
            txn.commit();
            g_serverLogger.info("updateAcquiringOrderVtbId: orderNumber=" + orderNumber +
                ", vtbOrderId=" + vtbOrderId);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("updateAcquiringOrderVtbId error: " + std::string(e.what()));
            return false;
        }
    }
    bool updateAcquiringOrderStatus(const std::string& orderNumber, const std::string& status) {
        try {
            pqxx::work txn{ *conn_ };
            txn.exec(
                "UPDATE acquiring_orders SET status = $1, updated_at = EXTRACT(EPOCH FROM NOW()) "
                "WHERE order_number = $2",
                pqxx::params{ status, orderNumber }
            );
            txn.commit();
            g_serverLogger.info("updateAcquiringOrderStatus: orderNumber=" + orderNumber +
                ", status=" + status);
            return true;
        }
        catch (const std::exception& e) {
            g_serverLogger.error("updateAcquiringOrderStatus error: " + std::string(e.what()));
            return false;
        }
    }
    json getAcquiringOrderByNumber(const std::string& orderNumber) {
        json result;
        try {
            pqxx::work txn{ *conn_ };
            auto res = txn.exec(
                "SELECT id, order_number, vtb_order_id, client_id, amount, amount_cop, status, doc_type, doc_id, created_at, updated_at "
                "FROM acquiring_orders WHERE order_number = $1",
                pqxx::params{ orderNumber }
            );
            if (res.empty()) return result;
            result["id"] = res[0]["id"].as<int>();
            result["order_number"] = res[0]["order_number"].as<std::string>();
            result["vtb_order_id"] = res[0]["vtb_order_id"].is_null() ? "" : res[0]["vtb_order_id"].as<std::string>();
            result["client_id"] = res[0]["client_id"].as<int>();
            result["amount"] = res[0]["amount"].as<double>();
            result["amount_cop"] = res[0]["amount_cop"].as<long long>();
            result["status"] = res[0]["status"].as<std::string>();
            result["doc_type"] = res[0]["doc_type"].is_null() ? "" : res[0]["doc_type"].as<std::string>();
            result["doc_id"] = res[0]["doc_id"].is_null() ? 0 : res[0]["doc_id"].as<int>();
            result["created_at"] = res[0]["created_at"].as<long long>();
            result["updated_at"] = res[0]["updated_at"].as<long long>();
        }
        catch (const std::exception& e) {
            g_serverLogger.error("getAcquiringOrderByNumber error: " + std::string(e.what()));
        }
        return result;
    }
};
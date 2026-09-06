// server.cpp
// Terminal Kiosk Server - Main Entry Point
// C++20, Boost.Beast, PostgreSQL, Ubuntu 26.04 LTS

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <functional>
#include <map>
#include <exception>
#include <sstream>
#include <random>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ssl/error.hpp>

#include "config_server.h"
#include "database.h"
#include "auth_service.h"
#include "queue_service.h"
#include "onec_integration.h"
#include "rate_limiter.h"
#include "logger_server.h"


using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

// Глобальный экземпляр логгера 

Logger g_serverLogger("/var/log/kiosk/server.log");

class HttpSession : public std::enable_shared_from_this<HttpSession> {
    ssl::stream<tcp::socket> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;

    std::shared_ptr<Database> db_;
    std::shared_ptr<AuthService> auth_;
    std::shared_ptr<QueueService> queue_;
    std::shared_ptr<OneCIntegration> onec_;
    std::shared_ptr<RateLimiter> rate_limiter_;

public:
    explicit HttpSession(tcp::socket&& socket, ssl::context& ctx,
        std::shared_ptr<Database> db,
        std::shared_ptr<AuthService> auth,
        std::shared_ptr<QueueService> queue,
        std::shared_ptr<OneCIntegration> onec,
        std::shared_ptr<RateLimiter> rate_limiter)
        : stream_(std::move(socket), ctx),
        db_(std::move(db)),
        auth_(std::move(auth)),
        queue_(std::move(queue)),
        onec_(std::move(onec)),
        rate_limiter_(std::move(rate_limiter)) {
    }

    void run() {
        stream_.async_handshake(ssl::stream_base::server,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    self->onFail(ec, "handshake");
                    return;
                }
                self->doRead();
            });
    }

private:
    void doRead() {
        http::async_read(stream_, buffer_, request_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) {
                    self->onFail(ec, "read");
                    return;
                }
                self->handleRequest();
            });
    }

    std::string getClientIP() {
        return stream_.lowest_layer().remote_endpoint().address().to_string();
    }

    void handleRequest() {
        auto target = request_.target();
        auto method = request_.method();
        std::string client_ip = getClientIP();

        g_serverLogger.info("Received request: " + std::string(http::to_string(method)) + " " + std::string(target) + " from " + client_ip);

        response_.version(request_.version());
        response_.keep_alive(false);

        try {
            if (target == "/api/v1/clients/register" && method == http::verb::post) {
                handleClientRegister(client_ip);
            }
            else if (target == "/api/v1/clients/by_phone" && method == http::verb::post) {
                handleClientByPhone(client_ip);
            }
            else if (target == "/api/v1/clients/me" && method == http::verb::get) {
                handleClientMe(client_ip);
            }
            // получаем данные о продажах пользователя
            else if (target.find("/api/v1/clients/sales") == 0 && method == http::verb::get) {
                handleClientSales(client_ip);
            }
            // получаем данные об общей очереди (ограничение на 20 единиц товара)
            else if (target.find("/api/v1/queue/daily_count") == 0 && method == http::verb::get) {
                handleDailyCount(client_ip);
            }
			// получаем данные о текущем пользователе в очереди по id
            else if (target.find("/api/v1/clients/by_id") == 0 && method == http::verb::get) {
                handleClientById(client_ip);
            }
			// получаем талон для текущего пользователя в очереди
            else if (target == "/api/v1/queue/get_ticket" && method == http::verb::post) {
                handleGetTicket(client_ip);
            }
			// получаем талон если пользователь пришел впервые и его нет в базе
            else if (target == "/api/v1/queue/first_time/create" && method == http::verb::post) {
                handleFirstTimeCreate(client_ip);
            }
			// получаем статус талона если пользователь пришел впервые и его нет в базе
            else if (target == "/api/v1/queue/first_time/waiting" && method == http::verb::get) {
                handleFirstTimeWaiting(client_ip);
            }
			// принимаем талон (товаровед вносит в базу) если пользователь пришел впервые и его нет в базе
            else if (target == "/api/v1/queue/first_time/accept" && method == http::verb::post) {
                handleFirstTimeAccept(client_ip);
            }
            // внесли в базу пользователя и помечаем талон как обслуженный 
            else if (target == "/api/v1/queue/first_time/serve" && method == http::verb::post) {
                handleFirstTimeServe(client_ip);
            }
            // получаем талон для пользователя, который есть в базе и принес +20 товаров
            else if (target.find("/api/v1/queue/waiting") == 0 && method == http::verb::get) {
                g_serverLogger.info("Routing to handleQueueWaiting for target: " + std::string(target));
                handleQueueWaiting(client_ip);
            }
            else if (target == "/api/v1/queue/accept" && method == http::verb::post) {
                handleQueueAccept(client_ip);
            }
            else if (target == "/api/v1/queue/serve" && method == http::verb::post) {
                handleQueueServe(client_ip);
            }
            // ========================================================================
            // ДОБАВЛЕННЫЙ РОУТ: /api/v1/queue/trust_acceptance
            // ========================================================================
            //
            // ВЫЯВЛЕННЫЙ ДЕФЕКТ (подтверждён логами 2026-08-23):
            //
            //   Клиент (queue_manager.h::getTrustAcceptance) отправляет запрос:
            //     POST /api/v1/queue/trust_acceptance
            //   Однако в списке роутов server.cpp этот путь ОТСУТСТВОВАЛ.
            //   Метод-обработчик handleTrustAcceptance существовал, но не был
            //   привязан к роуту.
            //   В результате сервер возвращал стандартный ответ "Not found":
            //     {"error":"Not found"}
            //   Подтверждение: лог терминала 17:11:26.293.
            //   Клиент интерпретировал это как недоступность сервера и
            //   генерировал локальный талон T001.
            //   Подтверждение: лог терминала 17:11:26.294:
            //   "Server unavailable for trust acceptance, generating local ticket"
            //
            // ИСПРАВЛЕНИЕ:
            //   Добавлен роут, привязывающий запрос
            //   /api/v1/queue/trust_acceptance к существующему обработчику
            //   handleTrustAcceptance. Метод-обработчик НЕ МЕНЯЕТСЯ.
            //   Клиентская часть НЕ МЕНЯЕТСЯ.
            //
            // ========================================================================
            // получаем талон для пользователя, который решил оставить товар на доверии
            // (роут добавлен: ранее отсутствовал, что приводило к ответу "Not found")
            else if (target == "/api/v1/queue/trust_acceptance" && method == http::verb::post) {
                handleTrustAcceptance(client_ip);
            }
            // получаем талон если пользователь решил оставить товар на доверии
            else if (target == "/api/v1/queue/trust/waiting" && method == http::verb::get) {
                handleTrustWaiting(client_ip);
            }
            else if (target == "/api/v1/queue/trust/accept" && method == http::verb::post) {
                handleTrustAccept(client_ip);
            }
            else if (target == "/api/v1/queue/trust/serve" && method == http::verb::post) {
                handleTrustServe(client_ip);
            }
            // =========================================================================
            // УДАЛЕНИЕ ТАЛОНА, ПО КОТОРОМУ КОМИТЕНТ НЕ ПРИШЁЛ
            // Вызывается товароведом, когда комитент не подошёл к окну в течение
            // 2 минут. Сервер проверяет возраст талона и его статус перед удалением.
            // =========================================================================
            else if (target == "/api/v1/queue/delete_ticket" && method == http::verb::post) {
                handleQueueDeleteTicket(client_ip);
            }
            // =========================================================================
            // ПОИСК КОМИТЕНТА ПО ИЛИ ИЛИ ФАМИЛИИ/ИМЕНИ/ОТЧЕСТВУ
            // Вызывается товароведом из окна «Поиск комитента».
            // Возвращает все сведения о найденном комитенте(ах) из БД сервера.
            // =========================================================================
            else if (target.find("/api/v1/clients/search") == 0 && method == http::verb::get) {
                handleClientSearch(client_ip);
            }
            else if (target == "/api/v1/onec/sync" && method == http::verb::post) {
                handleOneCSync(client_ip);
            }
            // получаем товароведу сдаваемые вещи (Общая очередь)
            else if (target.find("/api/v1/items") == 0 && method == http::verb::get) {
                handleGetItems(client_ip);
            }
            // получаем товароведу партию вещей (Общая очередь)
            else if (target == "/api/v1/items/batch" && method == http::verb::post) {
                handleAddItemsBatch(client_ip);
            }
            // =========================================================================
            // НОВЫЕ РОУТЫ ДЛЯ ПАНЕЛИ ДИРЕКТОРА МАГАЗИНА
            // =========================================================================

            // GET /api/v1/director/stats - получение полной статистики для директора
            else if (target.find("/api/v1/director/stats") == 0 && method == http::verb::get) {
                handleDirectorStats(client_ip);
            }
            // POST /api/v1/director/block_client - блокировка/разблокировка клиента
            else if (target == "/api/v1/director/block_client" && method == http::verb::post) {
                handleDirectorBlockClient(client_ip);
            }
            // GET /api/v1/director/clients - получение списка всех клиентов
            else if (target.find("/api/v1/director/clients") == 0 && method == http::verb::get) {
                handleDirectorGetClients(client_ip);
            }
            // =========================================================================
            // НОВЫЙ РОУТ: РЕГИСТРАЦИЯ ПРОДАЖИ ТОВАРА (от кассы Атол 77Ф через 1С)
            // POST /api/v1/sale/register
            // Тело запроса:
            // {
            //   "client_id": 123,          // ID комитента (из штрих-кода)
            //   "item_price": 1500.00,     // Цена продажи (из чека)
            //   "source": "ATOL_77F",      // Источник (касса)
            //   "receipt_number": "ЧК-001",// Номер чека из кассы
            //   "barcode_payload": "FIO=...;ID=...;NAME=...;PRICE=...;PAY=..."
            // }
            // =========================================================================
            else if (target == "/api/v1/sale/register" && method == http::verb::post) {
                handleSaleRegister(client_ip);
            }
            // =========================================================================
            // НОВЫЙ РОУТ: ПОЛУЧЕНИЕ ПРОСРОЧЕННЫХ ТОВАРОВ ДЛЯ ДИРЕКТОРА
            // GET /api/v1/director/expired_items
            // ДОСТУП: только директор (+79914869324)
            // =========================================================================
            else if (target.find("/api/v1/director/expired_items") == 0 && method == http::verb::get) {
                handleDirectorExpiredItems(client_ip);
            }
            // =========================================================================
            // НОВЫЕ РОУТЫ: ПРИЛОЖЕНИЯ К ДОГОВОРУ (чеки комитентов)
            // =========================================================================
            else if (target.find("/api/v1/appendix/by_number") == 0 && method == http::verb::get) {
                handleAppendixByNumber(client_ip);
            }
            else if (target.find("/api/v1/appendix/latest") == 0 && method == http::verb::get) {
                handleAppendixLatest(client_ip);
            }
            // =========================================================================
            // НОВЫЙ РОУТ: РЕГИСТРАЦИЯ КОМИТЕНТА С ДОПОЛНИТЕЛЬНЫМИ ДАННЫМИ
            // POST /api/v1/clients/register_committee
            // =========================================================================
            else if (target == "/api/v1/clients/register_committee" && method == http::verb::post) {
                handleRegisterCommittee(client_ip);
            }
            // =========================================================================
            // НОВЫЙ РОУТ: СОСТОЯНИЕ ВСЕХ ОЧЕРЕДЕЙ ДЛЯ ТВ-ДИСПЛЕЯ
            // GET /api/v1/queue/display
            // Возвращает ожидающие + принятые талоны для всех 6 очередей.
            // Только чтение (SELECT). Не изменяет данные. Не требует авторизации
            // (ТВ-монитор работает без логина товароведа).
            // =========================================================================
            else if (target == "/api/v1/queue/display" && method == http::verb::get) {
                handleQueueDisplay(client_ip);
            }

            // =========================================================================
            // НОВЫЕ РОУТЫ ДЛЯ МОДУЛЯ «КАССИР»
            // =========================================================================
            // Получение списка приложений к договору комитента
            else if (target.find("/api/v1/cashier/appendices") == 0 &&
                method == http::verb::get) {
                handleCashierGetAppendices(client_ip);
                }
                // Получение товаров приложения к договору
            else if (target.find("/api/v1/cashier/appendix_items") == 0 &&
                method == http::verb::get) {
                handleCashierGetAppendixItems(client_ip);
                }
                // Пометка товаров как «возвращённые комитенту»
            else if (target == "/api/v1/cashier/mark_returned" &&
                method == http::verb::post) {
                handleCashierMarkReturned(client_ip);
                }
                // Пометка товаров как «возмещён ущерб за утрату»
            else if (target == "/api/v1/cashier/mark_compensated" &&
                method == http::verb::post) {
                handleCashierMarkCompensated(client_ip);
                }
                // Получение реализованных товаров для выплаты вознаграждения
            else if (target.find("/api/v1/cashier/sold_items") == 0 &&
                method == http::verb::get) {
                handleCashierGetSoldItems(client_ip);
                }
                // Получение нереализованных товаров со сроком > 15 суток
            else if (target.find("/api/v1/cashier/unsold_items") == 0 &&
                method == http::verb::get) {
                handleCashierGetUnsoldItems(client_ip);
                }
                // Создание документа кассира (возврат, расписка, утрата, вознаграждение)
            else if (target == "/api/v1/cashier/create_document" &&
                method == http::verb::post) {
                handleCashierCreateDocument(client_ip);
                }

            else {
                g_serverLogger.warning("Routing request: " + std::string(http::to_string(method)) + " " + std::string(target) + " -> Not found");
                response_.result(http::status::not_found);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", "Not found"} }.dump();
                response_.prepare_payload();
            }
            
        }
        catch (const exception& e) {
            g_serverLogger.error("Exception during request handling: " + std::string(e.what()));
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Internal server error"} }.dump();
            response_.prepare_payload();
        }



        doWrite();
    }

    // =========================================================================
    // МЕТОД РЕГИСТРАЦИИ КЛИЕНТА/ТОВАРОВЕДА
    // =========================================================================
    void handleClientRegister(const std::string& client_ip) {
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("Client registration payload: " + body.dump());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Client registration failed: Invalid JSON payload from " + client_ip);
            return;
        }

        if (!body.contains("phone") || !body["phone"].is_string() ||
            !body.contains("last_name") || !body["last_name"].is_string() ||
            !body.contains("first_name") || !body["first_name"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing required fields"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Client registration failed: Missing required fields from " + client_ip);
            return;
        }

        std::string phone = body["phone"].get<std::string>();
        std::string last_name = body["last_name"].get<std::string>();
        std::string first_name = body["first_name"].get<std::string>();
        std::string middle_name = body.value("middle_name", std::string{});
        std::string email = body.value("email", std::string{});
        int items_submitted = body.value("items_submitted", 0);
        int items_sold = body.value("items_sold", 0);

        // === Явно извлекаем роль из запроса ===
        std::string role = body.value("role", "client");
        g_serverLogger.info("Client registration role extracted from payload: " + role);

        std::string digits;
        for (char c : phone) if (c >= '0' && c <= '9') digits += c;
        if (digits.length() != 11 || (digits[0] != '7' && digits[0] != '8')) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid phone number"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Client registration failed: Invalid phone number from " + client_ip + " -> " + phone);
            return;
        }
        phone = "+7" + digits.substr(1);
        g_serverLogger.info("Client registration attempt for: " + phone + " from " + client_ip);

        // === Передаем параметр role в метод БД ===
     // === Передаем параметр role в метод БД (БЕЗ ИЗМЕНЕНИЙ) ===
        auto [ok, already_exists] = db_->registerClient(phone, last_name, first_name, middle_name, email, items_submitted, items_sold, role);
        if (!ok) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Database error during registration"} }.dump();
            response_.prepare_payload();
            g_serverLogger.log(LogLevel::ERROR, "Client registration failed for: " + phone + " from " + client_ip + " -> Database error");
            return;
        }

        // =========================================================================
        // НОВОЕ: ГЕНЕРАЦИЯ И СОХРАНЕНИЕ ПАРОЛЯ ДЛЯ ТОВАРОВЕДА/ДИРЕКТОРА/КАССИРА.
        // Для роли "client" пароль НЕ генерируется (клиенты входят по телефону).
        //
        // СХЕМА:
        //   1. Генерируем случайный пароль через auth_->generateRandomPassword().
        //   2. Хешируем пароль через auth_->hashString() (SHA-256).
        //   3. Сохраняем ХЕШ через db_->setTOTPSecret(), который УЖЕ шифрует
        //      значение через CryptoUtils::encryptAES256CBC и пишет в
        //      totp_secret_encrypted. Структура БД НЕ меняется.
        //   4. Возвращаем ПЛАИНТЕКС-пароль администратору в ответе, чтобы он
        //      мог передать его товароведу. Пароль показывается один раз.
        //
        // Потокобезопасность: регистрация выполняется в рамках одного запроса,
        // setTOTPSecret использует отдельную транзакцию. При ошибке сохранения
        // пароля возвращаем ошибку, чтобы администратор повторил регистрацию.
        // =========================================================================
        std::string generatedPassword;
        if (role == "worker" || role == "director" || role == "cashier") {
            try {
                generatedPassword = auth_->generateRandomPassword();
                std::string passwordHash = auth_->hashString(generatedPassword);
                if (!db_->setTOTPSecret(phone, passwordHash)) {
                    response_.result(http::status::internal_server_error);
                    response_.set(http::field::content_type, "application/json");
                    response_.body() = json{ {"error", "Failed to store generated password"} }.dump();
                    response_.prepare_payload();
                    g_serverLogger.error("Client registration failed: password storage error for " + phone);
                    return;
                }
                g_serverLogger.info("Generated password for role=" + role + " phone=" + phone +
                    " (password returned to admin, hash stored in totp_secret_encrypted)");
            }
            catch (const std::exception& e) {
                response_.result(http::status::internal_server_error);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", std::string("Password generation failed: ") + e.what()} }.dump();
                response_.prepare_payload();
                g_serverLogger.error("Password generation exception for " + phone + ": " + e.what());
                return;
            }
        }

        // Генерация токенов для любого успешного случая (новый или существующий) — НЕ МЕНЯЕТСЯ
        auto tokens = auth_->generateTokens(phone);
        auto now = std::chrono::system_clock::now();
        int64_t expiresAt = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() + Config::JWT_ACCESS_EXPIRY_SECONDS;
        json resp;
        resp["success"] = ok;
        resp["phone"] = phone;
        resp["already_exists"] = already_exists;
        resp["role"] = role;
        resp["access_token"] = tokens.first;
        resp["refresh_token"] = tokens.second;
        resp["expires_at"] = expiresAt;

        // НОВОЕ: возвращаем сгенерированный пароль администратору.
        // Поле присутствует ТОЛЬКО для ролей worker/director и только при
        // успешной генерации. Для роли "client" поле отсутствует.
        if (!generatedPassword.empty()) {
            resp["password"] = generatedPassword;
        }

        g_serverLogger.info("Client registration attempt for: " + phone +
            " | Success: " + std::to_string(ok) +
            " | Already exists: " + std::to_string(already_exists) +
            " | Role saved: " + role +
            " | Password generated: " + std::string(generatedPassword.empty() ? "no" : "yes"));
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();
        g_serverLogger.info("Client registration successful for: " + phone + " from " + client_ip + " | Already exists: " + std::to_string(already_exists));
    }
        
    // -------------------------------------------------------------------------
    // POST /api/v1/clients/by_phone (post-метод)
    // ДОПОЛНЕНО: поддержка индивидуальных паролей для ролей worker/director.
    // Для роли client поведение НЕ МЕНЯЕТСЯ (вход по телефону без пароля).
    // -------------------------------------------------------------------------
    void handleClientByPhone(const std::string& client_ip) {
        json body;
        try {
            body = json::parse(request_.body());
        }
        catch (const std::exception& e) {
            g_serverLogger.error("JSON parse error in /clients/by_phone: " + std::string(e.what()));
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("phone") || !body["phone"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "phone field required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Missing phone in /clients/by_phone from " + client_ip);
            return;
        }
        std::string phone = body["phone"].get<std::string>();

        // НОВОЕ: извлекаем пароль, если он передан клиентом.
        // Для роли client пароль может отсутствовать — это штатная ситуация.
        // Для ролей worker/director пароль ОБЯЗАТЕЛЕН.
        std::string password = body.value("password", std::string(""));

        g_serverLogger.info("Checking client by phone: " + phone + " from " + client_ip +
            " (password provided: " + std::string(password.empty() ? "no" : "yes") + ")");

        auto clientOpt = db_->getClientByPhone(phone);
        if (!clientOpt) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Client not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Client not found for phone: " + phone);
            return;
        }

        // =====================================================================
        // ПРОВЕРКА БЛОКИРОВКИ КЛИЕНТА (ЛОГИКА НЕ МЕНЯЕТСЯ)
        // =====================================================================
        if (db_->isClientBlockedByPhone(phone)) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            json blockedResponse;
            blockedResponse["error"] = "Ваш аккаунт заблокирован. Обратитесь к администрации магазина";
            blockedResponse["blocked"] = true;
            response_.body() = blockedResponse.dump();
            response_.prepare_payload();
            g_serverLogger.warning("BLOCKED client attempted login: phone=" + phone + " from " + client_ip);
            return;
        }

        // =========================================================================
        // НОВОЕ: ПРОВЕРКА ПАРОЛЯ ДЛЯ РОЛЕЙ worker И director И cashier.
        // Для роли client этот блок пропускается (сохраняется прежний вход).
        //
        // ВАЛИДАЦИЯ ПОРЯДКА: сначала проверяем наличие пароля, затем его
        // корректность. Это исключает попытку входа товароведа/директора
        // без пароля и даёт клиенту однозначные коды ошибок:
        //   - "password_required": пароль не передан;
        //   - "invalid_password":   пароль неверен (клиент → RESULT_INVALID_PASSWORD).
        //
        // Потокобезопасность: verifyPassword выполняет только чтение из БД
        // через потокобезопасный getTOTPSecret. Состояние сессии не меняет.
        // =========================================================================
        if (clientOpt->role == "worker" || clientOpt->role == "director" ||
            clientOpt->role == "cashier") {
            if (password.empty()) {
                response_.result(http::status::unauthorized);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{
                    {"error", "Password required for staff login"},
                    {"password_required", true}
                }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("Staff login without password: role=" + clientOpt->role +
                    " phone=" + phone + " from " + client_ip);
                return;
            }
            if (!auth_->verifyPassword(phone, password)) {
                response_.result(http::status::unauthorized);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{
                    {"error", "Invalid password"},
                    {"invalid_password", true}
                }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("INVALID PASSWORD for role=" + clientOpt->role +
                    " phone=" + phone + " from " + client_ip);
                return;
            }
            g_serverLogger.info("Password verified OK for role=" + clientOpt->role + " phone=" + phone);
        }

        // =========================================================================
        // ИЗМЕНЕНО: проверка роли дополнена ролью "director".
        // Ранее допускались только "worker" и "client". Теперь директор также
        // проходит через этот эндпоинт, но его доступ к директорским эндпоинтам
        // контролируется отдельно по роли (см. примечание ниже).
        // =========================================================================
        if (clientOpt->role != "worker" && clientOpt->role != "client" && clientOpt->role != "director" &&
            clientOpt->role != "cashier") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: not a worker"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Access denied for non-worker phone: " + phone);
            return;
        }

        // Генерируем токены (теперь с ролью в payload JWT) — НЕ МЕНЯЕТСЯ
        auto tokens = auth_->generateTokens(phone);
        json resp;
        resp["id"] = clientOpt->id;
        resp["name"] = clientOpt->name;
        resp["phone"] = clientOpt->phone;
        // НОВОЕ: возвращаем роль клиенту. Клиент сохраняет её в AuthManager
        // и использует в main.cpp / director_window.h вместо хардкода телефона.
        resp["role"] = clientOpt->role;
        resp["access_token"] = tokens.first;
        resp["refresh_token"] = tokens.second;
        g_serverLogger.info("access_token: " + tokens.first);
        g_serverLogger.info("refresh_token:" + tokens.second);

        auto now = std::chrono::system_clock::now();
        int64_t expiresAt = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count() + Config::JWT_ACCESS_EXPIRY_SECONDS;
        resp["expires_at"] = expiresAt;
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();
        g_serverLogger.info("User" + clientOpt->role + "data returned for phone: " + phone + " (id = " + std::to_string(clientOpt->id) + ")");
    }

    //--------------------------------------------------------------------------
    // Обработчик получения данных из 1С о продажах пользователя
    //--------------------------------------------------------------------------
    void handleClientSales(const std::string& client_ip) {
        // 1. Извлекаем токен из заголовка Authorization
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) {
            
            authHeader = it->value();
        }
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Sales request missing token from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7); // отрезаем "Bearer "

        // 2. Проверяем токен
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Sales request invalid token from " + client_ip);
            return;
        }

        // 3. Получаем client_id из параметра запроса
        std::string query = request_.target();
        std::string clientIdStr;
        size_t pos = query.find("?client_id=");
        if (pos != std::string::npos) {
            clientIdStr = query.substr(pos + 11);
            size_t end = clientIdStr.find('&');
            if (end != std::string::npos) clientIdStr = clientIdStr.substr(0, end);
        }
        if (clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing client_id parameter"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Sales request missing client_id from " + client_ip);
            return;
        }
        int clientId = 0;
        try { clientId = std::stoi(clientIdStr); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid client_id"} }.dump();
            response_.prepare_payload();
            return;
        }

        // 4. Проверяем, что phone из токена соответствует запрашиваемому client_id
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || clientOpt->id != clientId) {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: client_id does not match token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Sales request access denied for client " + std::to_string(clientId) + " from " + client_ip);
            return;
        }

        // 5. Запрашиваем данные у 1С
        json salesData = onec_->getClientSales(clientId);
        // === FALLBACK: если 1С недоступна (возврат пустого объекта) — читаем из локальной PostgreSQL ===
        if (salesData.empty()) {
            g_serverLogger.info("1C integration returned empty/invalid data (connection failure or parse error), "
                "activating local DB fallback for client_id=" + std::to_string(clientId));

            auto dbItems = db_->getClientItems(clientId);
            json salesArray = json::array();

            // Лямбда для форматирования Unix timestamp → строка "YYYY-MM-DD HH:MM:SS"
                        // Лямбда для форматирования Unix timestamp → строка "YYYY-MM-DD HH:MM:SS"
            auto formatTimestamp = [](int64_t ts) -> std::string {
                std::time_t t = static_cast<std::time_t>(ts);
                struct tm tm_info;
#ifdef _WIN32
                // MSVC (Visual Studio 2022): используем localtime_s
                // Сигнатура: errno_t localtime_s(struct tm* _tm, const time_t* _time);
                if (localtime_s(&tm_info, &t) != 0) {
                    return "";
                }
#else
                // Linux (Ubuntu 26.04 LTS, GCC/Clang): используем POSIX localtime_r
                if (localtime_r(&t, &tm_info) == nullptr) {
                    return "";
                }
#endif
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
                return std::string(buf);
                };

            for (const auto& dbItem : dbItems) {
                json item;
                // Маппинг полей БД → ожидаемый терминалом формат (main_window.h :: onSalesDataReady)
                item["item_number"] = std::to_string(dbItem.value("item_number", 0));
                item["item_name"] = dbItem.value("description", "");
                item["price"] = dbItem.value("estimated_price", 0.0);
                item["quantity"] = dbItem.value("quantity", 1);
                item["status"] = dbItem.value("status", "");
                item["note"] = dbItem.value("note", "");

                // Преобразование created_at (report_date) и sale_date
                if (dbItem.contains("created_at") && !dbItem["created_at"].is_null()) {
                    item["report_date"] = formatTimestamp(dbItem["created_at"].get<int64_t>());
                }
                else {
                    item["report_date"] = "";
                }

                if (dbItem.contains("sale_date") && !dbItem["sale_date"].is_null()) {
                    item["sale_date"] = formatTimestamp(dbItem["sale_date"].get<int64_t>());
                }
                else {
                    item["sale_date"] = "";
                }

                salesArray.push_back(item);

                g_serverLogger.info("Fallback item mapped: item_number=" + item["item_number"].get<std::string>() +
                    ", item_name=" + item["item_name"].get<std::string>() +
                    ", price=" + std::to_string(item["price"].get<double>()) +
                    ", quantity=" + std::to_string(item["quantity"].get<int>()) +
                    ", status=" + item["status"].get<std::string>());
            } // конец цикла маппинга fallback-данных из локальной БД
            salesData = json{ {"sales", salesArray} };
            g_serverLogger.info("Local DB fallback completed: prepared " + std::to_string(salesArray.size()) +
                " records for client_id=" + std::to_string(clientId));
        }

        // 6. Отдаём результат
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = salesData.dump();
        response_.prepare_payload();
        g_serverLogger.info("Sales data returned for client " + std::to_string(clientId));
    }

    // Обработчик получения номера комитента, если пользователь авторизован
    void handleClientMe(const std::string& client_ip) {
        g_serverLogger.info("handleClientMe: request from " + client_ip);

        // 1. Извлекаем токен из заголовка Authorization
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) {
            authHeader = it->value();
        }
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientMe: missing or invalid token from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7); // отрезаем "Bearer "

        // 2. Проверяем токен через AuthService
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientMe: invalid or expired token from " + client_ip);
            return;
        }
        g_serverLogger.info("handleClientMe: token verified for phone " + *phoneOpt);

        // 3. Получаем клиента из БД по телефону
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Client not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientMe: client not found for phone " + *phoneOpt);
            return;
        }

        // 4. Формируем ответ
        json resp;
        resp["id"] = clientOpt->id;
        resp["phone"] = clientOpt->phone;
        resp["name"] = clientOpt->name;   // полное имя (сформировано в getClientByPhone)
        resp["email"] = clientOpt->email;

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();

        g_serverLogger.info("handleClientMe: data returned for client id " + std::to_string(clientOpt->id) +
            " (phone " + *phoneOpt + ") from " + client_ip);
    }

    // -------------------------------------------------------------------------
    // Обработчик GET /api/v1/queue/daily_count?queue_type=...
    // -------------------------------------------------------------------------
    void handleDailyCount(const std::string& client_ip) {
        std::string query = request_.target();
        std::string queueType;
        size_t pos = query.find("?queue_type=");
        if (pos != std::string::npos) {
            queueType = query.substr(pos + 12);
            size_t end = queueType.find('&');
            if (end != std::string::npos) queueType = queueType.substr(0, end);
        }
        if (queueType.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "queue_type required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("DailyCount request missing queue_type from " + client_ip);
            return;
        }

        int count = db_->getDailyTicketCount(queueType);
        g_serverLogger.info("Daily count for " + queueType + ": " + std::to_string(count) + " (from " + client_ip + ")");

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"count", count} }.dump();
        response_.prepare_payload();
    }

    // -------------------------------------------------------------------------
    // Обработчик GET /api/v1/clients/by_id?id=...
    // -------------------------------------------------------------------------
    void handleClientById(const std::string& client_ip) {
        std::string query = request_.target();
        std::string idStr;
        size_t pos = query.find("?id=");
        if (pos != std::string::npos) {
            idStr = query.substr(pos + 4);
            size_t end = idStr.find('&');
            if (end != std::string::npos) idStr = idStr.substr(0, end);
        }
        if (idStr.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "id required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("ClientById request missing id from " + client_ip);
            return;
        }

        int id = 0;
        try { id = std::stoi(idStr); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid id"} }.dump();
            response_.prepare_payload();
            return;
        }

        auto clientOpt = db_->getClientById(id);
        if (!clientOpt) {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Client not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Client not found by id " + std::to_string(id) + " from " + client_ip);
            return;
        }

        json resp;
        resp["id"] = clientOpt->id;
        resp["phone"] = clientOpt->phone;
        resp["name"] = clientOpt->name;
        resp["email"] = clientOpt->email;
        resp["birth_date"] = clientOpt->birth_date;

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();

        g_serverLogger.info("Client data returned for id " + std::to_string(id) +
            " (" + clientOpt->phone + ", birth_date=" + clientOpt->birth_date + ") from " + client_ip);
    }

    // ========================================================================
    // ИСПРАВЛЕННЫЙ ОБРАБОТЧИК: handleGetTicket
    // ========================================================================
    //
    // ВЫЯВЛЕННЫЙ ДЕФЕКТ (подтверждён логами 2026-08-23):
    //
    //   Обработчик ВСЕГДА возвращал {"success": true, "ticket": {...}},
    //   даже если QueueService::getTicket вернул мусорные данные
    //   (из-за неинициализированной структуры в Database::createTicket)
    //   или ошибку (поле "error" в JSON).
    //   Клиент получал мусор с `success: true` и не мог отличить
    //   успешное создание талона от ошибки.
    //   Подтверждение: лог терминала 17:10:48.636.
    //
    // ИСПРАВЛЕНИЕ:
    //   Добавлена ЗАЩИТНАЯ ПРОВЕРКА: если QueueService::getTicket вернул
    //   JSON с полем "error", значит талон НЕ был создан.
    //   В этом случае обработчик возвращает HTTP 500 с ошибкой клиенту.
    //   Если поле "error" отсутствует — талон создан успешно, возвращаем
    //   {"success": true, "ticket": {...}} как раньше.
    //
    //   Также добавлена ВАЛИДАЦИЯ обязательных полей запроса
    //   (client_id, queue_type, items_count) для предотвращения
    //   необработанных исключений при некорректном запросе.
    //
    //   Формат успешного ответа НЕ МЕНЯЕТСЯ:
    //     {"success": true, "ticket": {"id":..., "number":..., ...}}
    //   Клиентская часть НЕ МЕНЯЕТСЯ.
    //
    // ========================================================================
    void handleGetTicket(const std::string& client_ip) {
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("handleGetTicket: parsed request body from " + client_ip);
        }
        catch (const json::parse_error& e) {
            g_serverLogger.warning("handleGetTicket: JSON parse error from " + client_ip +
                ": " + std::string(e.what()));
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            return;
        }

        // ВАЛИДАЦИЯ обязательных полей запроса.
        // Предотвращает необработанные исключения при некорректном запросе.
        if (!body.contains("client_id") || !body["client_id"].is_number()) {
            g_serverLogger.warning("handleGetTicket: missing or invalid client_id from " + client_ip);
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "client_id required and must be a number"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("queue_type") || !body["queue_type"].is_string()) {
            g_serverLogger.warning("handleGetTicket: missing or invalid queue_type from " + client_ip);
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "queue_type required and must be a string"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("items_count") || !body["items_count"].is_number()) {
            g_serverLogger.warning("handleGetTicket: missing or invalid items_count from " + client_ip);
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "items_count required and must be a number"} }.dump();
            response_.prepare_payload();
            return;
        }

        int clientId = body["client_id"].get<int>();
        std::string queueType = body["queue_type"].get<std::string>();
        int itemsCount = body["items_count"].get<int>();

        g_serverLogger.info("handleGetTicket: request from " + client_ip +
            ", clientId=" + std::to_string(clientId) +
            ", queueType=" + queueType +
            ", itemsCount=" + std::to_string(itemsCount));

        json ticket = queue_->getTicket(clientId, queueType, itemsCount);

        // ЗАЩИТНАЯ ПРОВЕРКА: если талон содержит поле "error", значит
        // талон НЕ был создан (конфликт номера, ошибка БД, все номера
        // заняты). Возвращаем ошибку клиенту вместо мусорных данных.
        if (ticket.contains("error")) {
            g_serverLogger.error("handleGetTicket: ticket creation FAILED for clientId=" +
                std::to_string(clientId) +
                ", queueType=" + queueType +
                ", error=" + ticket["error"].dump() +
                ", client=" + client_ip);
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Failed to create ticket"} }.dump();
            response_.prepare_payload();
            return;
        }

        g_serverLogger.info("handleGetTicket: SUCCESS - ticket created for clientId=" +
            std::to_string(clientId) +
            ", ticketNumber=" + ticket.value("number", std::string("UNKNOWN")) +
            ", position=" + std::to_string(ticket.value("position", 0)) +
            ", client=" + client_ip);

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", true}, {"ticket", ticket} }.dump();
        response_.prepare_payload();
    }
	// --- методы для работы с очередью для пользователей, пришедших впервые (first_time) ---
// ========================================================================
// ОБРАБОТЧИК: handleFirstTimeCreate
// POST /api/v1/queue/first_time/create
// ========================================================================
// СОГЛАСОВАННОСТЬ ТИПОВ:
// queue_->createFirstTimeTicket(window) возвращает json-объект.
// Данный метод работает ИМЕННО С json, не выполняя никаких преобразований
// в std::string. Это исключает ошибку type_error.302.
//
// ВОЗВРАЩАЕТ КЛИЕНТУ:
// {
//   "ticket_number":     "F028",   // короткий номер (не более 3 знаков)
//   "position":          28,        // корректная позиция в очереди
//   "window_number":     "1",       // окно приёма
//   "wait_time_minutes": 140,       // расчётное время ожидания
//   "created_at":        1787654321 // метка времени создания
// }
// ========================================================================
    void handleFirstTimeCreate(const std::string& client_ip) {
        // ------------------------------------------------------------------
        // ШАГ 1: Разбор тела запроса. Тело может отсутствовать или быть
        // некорректным — в этом случае используем окно по умолчанию "1".
        // ------------------------------------------------------------------
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("handleFirstTimeCreate: parsed body: " + body.dump());
        }
        catch (const std::exception& e) {
            // Не прерываем обработку: отсутствие тела не является ошибкой,
            // так как окно по умолчанию "1" допустимо бизнес-логикой.
            g_serverLogger.warning("handleFirstTimeCreate: body parse skipped (" +
                std::string(e.what()) + "), using default window=1");
        }

        // ------------------------------------------------------------------
        // ШАГ 2: Извлекаем номер окна (по умолчанию "1").
        // ------------------------------------------------------------------
        std::string window = body.value("window", "1");

        g_serverLogger.info("handleFirstTimeCreate: request from " + client_ip +
            ", window=" + window);

        // ------------------------------------------------------------------
        // ШАГ 3: Создаём талон. queue_->createFirstTimeTicket() возвращает
        // json-объект. НИКАКОГО преобразования в std::string здесь нет.
        // ------------------------------------------------------------------
        json ticket = queue_->createFirstTimeTicket(window);

        // ------------------------------------------------------------------
        // ШАГ 4: Проверяем результат. Если Database вернул json с полем
        // "error" — значит, создание талона не удалось.
        // ------------------------------------------------------------------
        if (ticket.contains("error")) {
            g_serverLogger.error("handleFirstTimeCreate: failed to create ticket: " +
                ticket["error"].dump());
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Failed to create ticket"} }.dump();
        }
        else {
            // ------------------------------------------------------------------
            // ШАГ 5: Успешное создание. Логируем с безопасным извлечением полей
            // (метод value() с fallback не бросает исключений).
            // ------------------------------------------------------------------
            g_serverLogger.info("handleFirstTimeCreate: ticket created: number=" +
                ticket.value("ticket_number", std::string("UNKNOWN")) +
                ", position=" + std::to_string(ticket.value("position", 0)) +
                ", window=" + ticket.value("window_number", std::string("1")) +
                ", waitTime=" + std::to_string(ticket.value("wait_time_minutes", 0)) +
                ", client=" + client_ip);

            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            // Возвращаем ВЕСЬ json-объект как есть. Клиент (main_window.h
            // handleFirstTime) извлечёт из него ticket_number, position,
            // window_number, wait_time_minutes.
            response_.body() = ticket.dump();
        }
        response_.prepare_payload();
    }

    void handleFirstTimeWaiting(const std::string& client_ip) {
        auto tickets = queue_->getWaitingFirstTimeTickets();
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"tickets", tickets} }.dump();
        response_.prepare_payload();
    }

    void handleFirstTimeAccept(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }

        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }

        // НОВОЕ: Извлекаем номер окна товароведа
        std::string windowNumber = body.value("window_number", std::string("1"));

        g_serverLogger.info("handleFirstTimeAccept: ticket=" + ticketNumber +
            ", window=" + windowNumber + ", from=" + client_ip);

        if (queue_->acceptFirstTimeTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{
                {"success", true},
                {"window_number", windowNumber}
            }.dump();
            g_serverLogger.info("handleFirstTimeAccept: accepted " + ticketNumber +
                " at window " + windowNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{
                {"error", "Ticket not found or already accepted"}
            }.dump();
        }
        response_.prepare_payload();
    }

    void handleFirstTimeServe(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (queue_->serveFirstTimeTicket(ticketNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true} }.dump();
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or not accepted"} }.dump();
        }
        response_.prepare_payload();
    }
	// -------------------------------------------------------------------------

	// --- методы для пользователей, которые уже есть в базе и пришли с +20 товарами ---
    void handleQueueWaiting(const std::string& client_ip) {
        std::string query = request_.target();
        std::string queueType;
        size_t pos = query.find("?type=");
        if (pos != std::string::npos) {
            queueType = query.substr(pos + 6);
            size_t end = queueType.find('&');
            if (end != std::string::npos) queueType = queueType.substr(0, end);
        }
        if (queueType.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "type required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueWaiting: missing type from " + client_ip);
            return;
        }
        auto tickets = queue_->getWaitingTickets(queueType);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"tickets", tickets} }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleQueueWaiting: returned " + std::to_string(tickets.size()) + " tickets for " + queueType);
    }

    // =========================================================================
    // ОБНОВЛЁННЫЙ ОБРАБОТЧИК: handleQueueAccept
    // POST /api/v1/queue/accept
    // =========================================================================
    // ДОПОЛНЕНИЕ: Теперь принимает поле "window_number" из тела запроса.
    // Номер окна товароведа передаётся клиентом и обновляется в БД
    // при принятии талона. Это позволяет ТВ-монитору отображать
    // реальный номер окна, к которому приглашён комитент.
    //
    // Формат запроса:
    // {
    //   "ticket_number": "G001",
    //   "window_number": "3"       // ← НОВОЕ: номер окна товароведа
    // }
    //
    // Формат ответа:
    // {
    //   "success": true,
    //   "window_number": "3"       // обновлённый номер окна
    // }
    // =========================================================================
    void handleQueueAccept(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }

        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }

        // =====================================================================
        // НОВОЕ: Извлекаем номер окна товароведа из запроса
        // Если не передан — используем "1" (обратная совместимость)
        // =====================================================================
        std::string windowNumber = body.value("window_number", std::string("1"));

        g_serverLogger.info("handleQueueAccept: ticket=" + ticketNumber +
            ", window=" + windowNumber + ", from=" + client_ip);

        if (queue_->acceptTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{
                {"success", true},
                {"window_number", windowNumber}
            }.dump();
            g_serverLogger.info("handleQueueAccept: accepted " + ticketNumber +
                " at window " + windowNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{
                {"error", "Ticket not found or already accepted"}
            }.dump();
            g_serverLogger.warning("handleQueueAccept: failed to accept " + ticketNumber);
        }
        response_.prepare_payload();
    }

    void handleQueueServe(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (queue_->serveTicket(ticketNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true} }.dump();
            g_serverLogger.info("handleQueueServe: served " + ticketNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or not accepted"} }.dump();
            g_serverLogger.warning("handleQueueServe: failed to serve " + ticketNumber);
        }
        response_.prepare_payload();
    }
    //-------------------------------------------------------------------------

// =========================================================================
// ИСПРАВЛЕННЫЙ ОБРАБОТЧИК: handleTrustAcceptance
// POST /api/v1/queue/trust_acceptance
// =========================================================================
//
// ПРИЧИНА ИСПРАВЛЕНИЯ (подтверждена логами 2026-08-24):
//
//   Обработчик жёстко задавал позицию 0:
//     resp["position"] = 0; // для доверия позиция всегда 0
//   Клиент (queue_manager.h::getTrustAcceptance) получал 0 и
//   нормализовал до 1 по бизнес-правилу. В результате комитент
//   всегда видел себя первым в очереди.
//
//   Подтверждение из логов:
//     Сервер: createTrustAcceptance: basePosition=7 (позиция вычислена!)
//     Клиент: Response: {"position":0} → normalized to 1
//
// ИСПРАВЛЕНИЕ:
//   Метод db_->createTrustAcceptance() теперь возвращает
//   std::optional<std::pair<std::string, int>>:
//     first  = номер талона
//     second = позиция в очереди (уже вычисленная в Database)
//   Обработчик использует ticketResult->second вместо жёсткого 0.
//
//   Формат ответа клиенту НЕ МЕНЯЕТСЯ:
//     {"success":true,"ticket_number":"T007","position":7,
//      "window_number":"1","created_at":1787580012}
//   Клиентская часть НЕ МЕНЯЕТСЯ.
//
// ПОТОКОБЕЗОПАСНОСТЬ:
//   Метод вызывается из одного потока обработки HTTP-запроса.
//   Все запросы к БД выполняются внутри одной транзакции в
//   createTrustAcceptance. Гонка исключена.
//
// =========================================================================
    void handleTrustAcceptance(const std::string& client_ip) {
        g_serverLogger.info("Start method handleTrustAcceptance");

        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("Parsed body: " + body.dump());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            g_serverLogger.info("Parse error!");
            return;
        }

        if (!body.contains("client_id") || !body["client_id"].is_number()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "client_id required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.info("Body does not contain client_id");
            return;
        }

        int clientId = body["client_id"].get<int>();

        // ИСПРАВЛЕНИЕ: создаём талон и получаем ПАРУ (номер, позиция).
        // Ранее: auto ticketNumberOpt = db_->createTrustAcceptance(clientId);
        // возвращал только номер талона (std::optional<std::string>).
        // Теперь: возвращает std::optional<std::pair<std::string, int>>,
        // где second = реальная позиция в очереди (COUNT(pending) + 1).
        auto ticketResult = db_->createTrustAcceptance(clientId);

        if (!ticketResult) {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to create trust acceptance"} }.dump();
            response_.prepare_payload();
            g_serverLogger.info("ticketResult is empty");
            return;
        }

        // ИСПРАВЛЕНИЕ: извлекаем номер талона из пары (first).
        // Ранее: db_->getTrustTicketInfo(*ticketNumberOpt)
        // Теперь: db_->getTrustTicketInfo(ticketResult->first)
        auto info = db_->getTrustTicketInfo(ticketResult->first);

        if (!info) {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to retrieve ticket info"} }.dump();
            response_.prepare_payload();
            return;
        }

        json resp;
        resp["success"] = true;
        // ИСПРАВЛЕНИЕ: извлекаем номер талона из пары (first).
        resp["ticket_number"] = ticketResult->first;
        resp["window_number"] = (*info)["window_number"];
        // =====================================================================
        // ИСПРАВЛЕНИЕ: используем реальную позицию из пары (second).
        // Ранее: resp["position"] = 0; // для доверия позиция всегда 0
        // Теперь: позиция = COUNT(pending) + 1, вычисленная в
        // database.h::createTrustAcceptance и возвращённая в паре.
        // =====================================================================
        resp["position"] = ticketResult->second;
        resp["created_at"] = (*info)["created_at"];

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();

        g_serverLogger.info("Trust acceptance created for client " + std::to_string(clientId) +
            ", ticket: " + ticketResult->first +
            ", position: " + std::to_string(ticketResult->second) +
            ", from " + client_ip);
    }

    void handleTrustWaiting(const std::string& client_ip) {
        auto tickets = queue_->getWaitingTrustTickets();
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"tickets", tickets} }.dump();
        response_.prepare_payload();
    }

    void handleTrustAccept(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }

        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }

        // НОВОЕ: Извлекаем номер окна товароведа
        std::string windowNumber = body.value("window_number", std::string("1"));

        g_serverLogger.info("handleTrustAccept: ticket=" + ticketNumber +
            ", window=" + windowNumber + ", from=" + client_ip);

        if (queue_->acceptTrustTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{
                {"success", true},
                {"window_number", windowNumber}
            }.dump();
            g_serverLogger.info("handleTrustAccept: accepted " + ticketNumber +
                " at window " + windowNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{
                {"error", "Ticket not found or already accepted"}
            }.dump();
        }
        response_.prepare_payload();
    }

    void handleTrustServe(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string ticketNumber = body.value("ticket_number", "");
        if (ticketNumber.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "ticket_number required"} }.dump();
            response_.prepare_payload();
            return;
        }
        if (queue_->serveTrustTicket(ticketNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true} }.dump();
            g_serverLogger.info("handleTrustServe: served " + ticketNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or not accepted"} }.dump();
        }
        response_.prepare_payload();
    }
    // методы для товароведа (Общая очередь)
    // =========================================================================
    // GET /api/v1/items?client_id=...  (получение товаров клиента)
    // =========================================================================
    void handleGetItems(const std::string& client_ip) {
        // 1. Проверка авторизации (извлекаем токен)
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid token"} }.dump();
            response_.prepare_payload();
            return;
        }

        // 2. Получаем client_id из параметра
        std::string query = request_.target();
        std::string clientIdStr;
        size_t pos = query.find("?client_id=");
        if (pos != std::string::npos) {
            clientIdStr = query.substr(pos + 11);
            size_t end = clientIdStr.find('&');
            if (end != std::string::npos) clientIdStr = clientIdStr.substr(0, end);
        }
        if (clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing client_id"} }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = std::stoi(clientIdStr);

        // 3. Проверяем аутентификацию и роль
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt) {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: user not found"} }.dump();
            response_.prepare_payload();
            return;
        }

        // === ИСПРАВЛЕНИЕ: товаровед (worker) может просматривать товары ЛЮБОГО клиента,
        //    т.к. он принимает товар от клиентов в очереди. Обычный клиент — только свои. ===
        if (clientOpt->role != "worker") {
            // Если это не товаровед, проверяем, что он запрашивает только свои данные
            if (clientOpt->id != clientId) {
                response_.result(http::status::forbidden);
                response_.body() = json{ {"error", "Access denied"} }.dump();
                response_.prepare_payload();
                return;
            }
        }
        // Для worker'а проверка clientOpt->id != clientId НЕ выполняется — доступ разрешен

        // 4. Получаем товары
        auto items = db_->getClientItems(clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"items", items} }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleGetItems: returned " + std::to_string(items.size()) +
            " items for client " + std::to_string(clientId));
    }

    // =========================================================================
    // POST /api/v1/items/batch  (массовое сохранение товаров)
    // ДОПОЛНЕНИЕ: Добавлена валидация полей распределения выручки
    // (client_percent, store_percent, client_amount, store_amount)
    // =========================================================================
    void handleAddItemsBatch(const std::string& client_ip) {
        // 1. Проверка авторизации
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: missing auth from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: invalid token from " + client_ip);
            return;
        }

        // 2. Разбор JSON
        json body;
        try { body = json::parse(request_.body()); }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: JSON parse error from " + client_ip);
            return;
        }

        if (!body.contains("client_id") || !body["client_id"].is_number() ||
            !body.contains("items") || !body["items"].is_array()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing client_id or items array"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: missing fields from " + client_ip);
            return;
        }

        int clientId = body["client_id"].get<int>();
        json items = body["items"];

        // =========================================================================
        // ВАЛИДАЦИЯ clientId: должен быть положительным
        // =========================================================================
        if (clientId <= 0) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid client_id: must be positive"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("handleAddItemsBatch: received invalid clientId=" +
                std::to_string(clientId) + " from " + client_ip +
                " (worker phone: " + *phoneOpt + ")");
            return;
        }

        // =========================================================================
        // НОВОЕ: ВАЛИДАЦИЯ ПОЛЕЙ РАСПРЕДЕЛЕНИЯ ВЫРУЧКИ В КАЖДОМ ТОВАРЕ
        // Проверяем наличие, диапазон и сумму процентов.
        // Это гарантирует целостность данных в БД.
        // =========================================================================
        for (const auto& item : items) {
            // Проверка наличия обязательных полей распределения
            if (!item.contains("client_percent") || !item["client_percent"].is_number() ||
                !item.contains("store_percent") || !item["store_percent"].is_number()) {
                response_.result(http::status::bad_request);
                response_.body() = json{ {"error", "Missing client_percent or store_percent in item"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: missing commission fields from " + client_ip);
                return;
            }

            double clientPercent = item["client_percent"].get<double>();
            double storePercent = item["store_percent"].get<double>();

            // Проверка диапазона процентов
            if (clientPercent < 0.0 || clientPercent > 100.0) {
                response_.result(http::status::bad_request);
                response_.body() = json{ {"error", "client_percent must be between 0 and 100"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: invalid client_percent=" +
                    std::to_string(clientPercent) + " from " + client_ip);
                return;
            }
            if (storePercent < 0.0 || storePercent > 100.0) {
                response_.result(http::status::bad_request);
                response_.body() = json{ {"error", "store_percent must be between 0 and 100"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: invalid store_percent=" +
                    std::to_string(storePercent) + " from " + client_ip);
                return;
            }

            // Проверка суммы процентов (должна быть ровно 100%)
            double totalPercent = clientPercent + storePercent;
            if (std::abs(totalPercent - 100.0) > 0.01) {
                response_.result(http::status::bad_request);
                response_.body() = json{ {"error", "Sum of client_percent and store_percent must be 100"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: percent sum=" +
                    std::to_string(totalPercent) + " != 100 from " + client_ip);
                return;
            }

            // Проверка наличия сумм выплат (опционально, но рекомендуется)
            if (!item.contains("client_amount") || !item["client_amount"].is_number() ||
                !item.contains("store_amount") || !item["store_amount"].is_number()) {
                response_.result(http::status::bad_request);
                response_.body() = json{ {"error", "Missing client_amount or store_amount in item"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: missing amount fields from " + client_ip);
                return;
            }
        }

        g_serverLogger.info("handleAddItemsBatch: clientId=" + std::to_string(clientId) +
            ", itemsCount=" + std::to_string(items.size()) +
            ", worker=" + *phoneOpt + ", from=" + client_ip);

        // 3. Проверяем аутентификацию и роль
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt) {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: user not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: user not found for phone " + *phoneOpt);
            return;
        }

        // Товаровед (worker) может добавлять товары для ЛЮБОГО клиента
        if (clientOpt->role != "worker") {
            if (clientOpt->id != clientId) {
                response_.result(http::status::forbidden);
                response_.body() = json{ {"error", "Access denied"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleAddItemsBatch: non-worker access denied for " + *phoneOpt);
                return;
            }
        }

        // Проверка роли: только worker может использовать batch
        if (clientOpt->role != "worker") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: worker role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAddItemsBatch: non-worker tried to add items: " + *phoneOpt);
            return;
        }

        int workerId = clientOpt->id;
        g_serverLogger.info("handleAddItemsBatch: clientId=" + std::to_string(clientId) +
            ", itemsCount=" + std::to_string(items.size()) +
            ", workerId=" + std::to_string(workerId) +
            ", workerPhone=" + *phoneOpt + ", from=" + client_ip);

        // 4. Сохраняем товары + создаём приложение к договору (атомарно)
        long long appendixNumber = 0;
        if (db_->addItemsBatch(clientId, items, workerId, &appendixNumber)) {
            response_.result(http::status::ok);
            json okResp;
            okResp["success"] = true;
            okResp["appendix_number"] = appendixNumber; // <-- НОВОЕ: номер приложения для чека
            response_.body() = okResp.dump();
            g_serverLogger.info("handleAddItemsBatch: saved " + std::to_string(items.size()) +
                " items for client " + std::to_string(clientId) +
                ", appendix_number=" + std::to_string(appendixNumber));
        }
        else {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to save items"} }.dump();
            g_serverLogger.error("handleAddItemsBatch: FAILED for client " + std::to_string(clientId));
        }
        response_.prepare_payload();
    }


    void handleOneCSync(const std::string& client_ip) {
        bool success = onec_->syncData();
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", success} }.dump();
        response_.prepare_payload();
    }

    // =========================================================================
    // ОБРАБОТЧИКИ ДЛЯ ПАНЕЛИ ДИРЕКТОРА МАГАЗИНА
    // =========================================================================

    /**
     * @brief Обработчик GET /api/v1/director/stats
     * Возвращает полную статистику для панели директора:
     * - Данные о всех комитентах (сдано товаров, суммы)
     * - Эффективность товароведов (внесено в БД, продано)
     * - Не проданные товары из-за низкого качества
     *
     * ДОСТУП: только для роли 'director' (проверка по телефону +79914869324)
     */
    void handleDirectorStats(const std::string& client_ip) {
        g_serverLogger.info("handleDirectorStats: request from " + client_ip);

        // 1. Проверка авторизации (извлекаем токен)
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();

        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorStats: missing auth from " + client_ip);
            return;
        }

        std::string token = authHeader.substr(7); // отрезаем "Bearer "

        // 2. Проверяем токен через AuthService
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorStats: invalid token from " + client_ip);
            return;
        }

        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА ЧЕРЕЗ БД (вместо хардкода телефона)
        // Запрашиваем запись клиента по телефону из JWT и проверяем поле role.
        auto directorOpt = db_->getClientByPhone(*phoneOpt);
        if (!directorOpt || directorOpt->role != "director") {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorStats: access denied for phone=" + *phoneOpt +
                " from " + client_ip + " (role is not director)");
            return;
        }
        g_serverLogger.info("handleDirectorStats: director authenticated, phone=" + *phoneOpt);

        // 4. Получаем статистику из БД
        json stats = db_->getDirectorStats();

        // 5. Проверяем, что статистика получена успешно
        if (stats.contains("error")) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Failed to get director stats"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("handleDirectorStats: failed to get stats from DB");
            return;
        }

        // 6. Отправляем успешный ответ
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = stats.dump();
        response_.prepare_payload();

        g_serverLogger.info("handleDirectorStats: stats returned successfully to director from " + client_ip);
    }

    /**
     * @brief Обработчик POST /api/v1/director/block_client
     * Блокирует или разблокирует клиента по ID с указанием причины
     *
     * Тело запроса:
     * {
     *   "client_id": 123,
     *   "blocked": true,
     *   "block_reason": "Нарушение правил магазина"  // до 1000 символов
     * }
     *
     * ДОСТУП: только для роли 'director' (проверка по телефону +79914869324)
     */
    void handleDirectorBlockClient(const std::string& client_ip) {
        g_serverLogger.info("handleDirectorBlockClient: request from " + client_ip);
        // 1. Проверка авторизации
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: missing auth from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7);
        // 2. Проверяем токен
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: invalid token from " + client_ip);
            return;
        }
        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА ЧЕРЕЗ БД + получение данных директора
        // Один запрос к БД решает обе задачи: проверку роли и получение directorId.
        // Это исключает двойной запрос, который был бы при раздельных шагах 3 и 4.
        auto directorOpt = db_->getClientByPhone(*phoneOpt);
        if (!directorOpt || directorOpt->role != "director") {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: access denied for phone=" + *phoneOpt +
                " from " + client_ip + " (role is not director)");
            return;
        }
        int directorId = directorOpt->id;

        // 4. Разбор JSON тела запроса
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("handleDirectorBlockClient: parsed body: " + body.dump());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: JSON parse error from " + client_ip);
            return;
        }
        // 5. Валидация обязательных полей
        if (!body.contains("client_id") || !body["client_id"].is_number()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid client_id"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: missing client_id from " + client_ip);
            return;
        }
        if (!body.contains("blocked") || !body["blocked"].is_boolean()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid blocked field"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: missing blocked field from " + client_ip);
            return;
        }
        int clientId = body["client_id"].get<int>();
        bool blocked = body["blocked"].get<bool>();
        // =====================================================================
        // НОВОЕ: Извлечение и валидация причины блокировки
        // =====================================================================
        std::string blockReason = body.value("block_reason", std::string(""));
        // Валидация длины: не более 1000 символов
        if (blockReason.length() > 1000) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Block reason must not exceed 1000 characters"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: block_reason too long (" +
                std::to_string(blockReason.length()) + " chars) from " + client_ip);
            return;
        }
        g_serverLogger.info("handleDirectorBlockClient: clientId=" + std::to_string(clientId) +
            ", blocked=" + std::to_string(blocked) +
            ", directorId=" + std::to_string(directorId) +
            ", blockReason='" + blockReason.substr(0, 100) +
            (blockReason.length() > 100 ? "..." : "") + "'");
        // 7. Проверяем, что клиент существует
        auto clientOpt = db_->getClientById(clientId);
        if (!clientOpt) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Client not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: client not found, id=" +
                std::to_string(clientId));
            return;
        }
        // 8. Проверяем, что не пытаемся заблокировать самого директора или товароведа
        if (clientOpt->role == "director" || clientOpt->role == "worker") {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Cannot block director or worker account"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: attempted to block non-client role=" +
                clientOpt->role + ", id=" + std::to_string(clientId));
            return;
        }
        // 9. Выполняем блокировку/разблокировку с причиной
        if (db_->setClientBlocked(clientId, blocked, directorId, blockReason)) {
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            json successResponse;
            successResponse["success"] = true;
            successResponse["client_id"] = clientId;
            successResponse["blocked"] = blocked;
            successResponse["block_reason"] = blockReason;
            successResponse["message"] = blocked ?
                "Клиент успешно заблокирован" :
                "Клиент успешно разблокирован";
            response_.body() = successResponse.dump();
            response_.prepare_payload();
            g_serverLogger.info("handleDirectorBlockClient: SUCCESS - client " +
                std::to_string(clientId) + (blocked ? " BLOCKED" : " UNBLOCKED") +
                " by director " + std::to_string(directorId) + " from " + client_ip);
        }
        else {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Failed to update client blocked status"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("handleDirectorBlockClient: FAILED to update blocked status for client " +
                std::to_string(clientId));
        }
    }

    /**
     * @brief Обработчик GET /api/v1/director/clients
     * Возвращает список всех клиентов с возможностью фильтрации
     *
     * Параметры запроса:
     * - include_blocked=true|false (по умолчанию true)
     *
     * ДОСТУП: только для роли 'director' (проверка по телефону +79914869324)
     */
    void handleDirectorGetClients(const std::string& client_ip) {
        g_serverLogger.info("handleDirectorGetClients: request from " + client_ip);

        // 1. Проверка авторизации
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();

        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorGetClients: missing auth from " + client_ip);
            return;
        }

        std::string token = authHeader.substr(7);

        // 2. Проверяем токен
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorGetClients: invalid token from " + client_ip);
            return;
        }

        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА ЧЕРЕЗ БД (вместо хардкода телефона)
        auto directorOpt = db_->getClientByPhone(*phoneOpt);
        if (!directorOpt || directorOpt->role != "director") {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorGetClients: access denied for phone=" + *phoneOpt +
                " from " + client_ip + " (role is not director)");
            return;
        }

        // 4. Парсим параметр include_blocked из query string
        std::string query = request_.target();
        bool includeBlocked = true; // по умолчанию включаем заблокированных

        size_t pos = query.find("?include_blocked=");
        if (pos != std::string::npos) {
            std::string value = query.substr(pos + 17);
            size_t end = value.find('&');
            if (end != std::string::npos) value = value.substr(0, end);

            includeBlocked = (value == "true" || value == "1");
        }

        g_serverLogger.info("handleDirectorGetClients: includeBlocked=" +
            std::to_string(includeBlocked));

        // 5. Получаем список клиентов из БД
        json clients = db_->getAllClients(includeBlocked);

        // 6. Отправляем ответ
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"clients", clients}, {"count", clients.size()} }.dump();
        response_.prepare_payload();

        g_serverLogger.info("handleDirectorGetClients: returned " +
            std::to_string(clients.size()) + " clients to director from " + client_ip);
    }

    // =========================================================================
    // GET /api/v1/appendix/by_number?number=...
    // Выборка приложения к договору по номеру: перечень вещей, продано, возвращено.
    // ДОСТУП: только роль worker (товаровед) — как в handleGetItems.
    // =========================================================================
    void handleAppendixByNumber(const std::string& client_ip) {
        g_serverLogger.info("handleAppendixByNumber: request from " + client_ip);
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization"} }.dump();
            response_.prepare_payload();
            return;
        }
        auto phoneOpt = auth_->verifyJWT(authHeader.substr(7));
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid token"} }.dump();
            response_.prepare_payload();
            return;
        }
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || clientOpt->role != "worker") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: worker role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleAppendixByNumber: access denied for " + *phoneOpt);
            return;
        }
        std::string query = request_.target();
        long long number = 0;
        size_t pos = query.find("?number=");
        if (pos != std::string::npos) {
            try { number = std::stoll(query.substr(pos + 8)); }
            catch (...) { number = 0; }
        }
        if (number <= 0) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing or invalid number"} }.dump();
            response_.prepare_payload();
            return;
        }
        json data = db_->getAppendixByNumber(number);
        response_.result(data.contains("error") ? http::status::not_found : http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = data.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleAppendixByNumber: number=" + std::to_string(number) + " done");
    }

    // =========================================================================
    // GET /api/v1/appendix/latest?client_id=...
    // Последнее приложение клиента (fallback для печати чека).
    // ДОСТУП: только роль worker.
    // =========================================================================
    void handleAppendixLatest(const std::string& client_ip) {
        g_serverLogger.info("handleAppendixLatest: request from " + client_ip);
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization"} }.dump();
            response_.prepare_payload();
            return;
        }
        auto phoneOpt = auth_->verifyJWT(authHeader.substr(7));
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid token"} }.dump();
            response_.prepare_payload();
            return;
        }
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || clientOpt->role != "worker") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: worker role required"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string query = request_.target();
        int clientId = 0;
        size_t pos = query.find("?client_id=");
        if (pos != std::string::npos) {
            try { clientId = std::stoi(query.substr(pos + 11)); }
            catch (...) { clientId = 0; }
        }
        if (clientId <= 0) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing or invalid client_id"} }.dump();
            response_.prepare_payload();
            return;
        }
        json data = db_->getLatestAppendix(clientId);
        response_.result(data.contains("error") ? http::status::not_found : http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = data.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleAppendixLatest: client_id=" + std::to_string(clientId) + " done");
    }

    // =========================================================================
    // ОБРАБОТЧИК POST /api/v1/sale/register
    // Регистрация продажи одной единицы товара.
    // Вызывается из 1С после пробития чека на кассе Атол 77Ф.
    //
    // Логика:
    // 1. Парсим штрих-код или принимаем client_id + item_price
    // 2. Находим товар в БД по client_id и цене
    // 3. Атомарно списываем одну единицу (sold_quantity += 1)
    // 4. Фиксируем продажу в item_sales
    // 5. Если все единицы проданы → status = 'sold'
    // =========================================================================
    void handleSaleRegister(const std::string& client_ip) {
        g_serverLogger.info("handleSaleRegister: request from " + client_ip);

        // =====================================================================
        // ШАГ 1: Разбор JSON тела запроса
        // =====================================================================
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("handleSaleRegister: parsed body: " + body.dump());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: JSON parse error from " + client_ip);
            return;
        }

        // =====================================================================
        // ШАГ 2: Валидация обязательных полей
        // =====================================================================
        if (!body.contains("client_id") || !body["client_id"].is_number()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid client_id"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: missing client_id from " + client_ip);
            return;
        }

        if (!body.contains("item_price") || !body["item_price"].is_number()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid item_price"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: missing item_price from " + client_ip);
            return;
        }

        int clientId = body["client_id"].get<int>();
        double itemPrice = body["item_price"].get<double>();
        std::string source = body.value("source", std::string("1C"));
        std::string receiptNumber = body.value("receipt_number", std::string(""));
        std::string barcodePayload = body.value("barcode_payload", std::string(""));

        g_serverLogger.info("handleSaleRegister: clientId=" + std::to_string(clientId) +
            ", itemPrice=" + std::to_string(itemPrice) +
            ", source=" + source +
            ", receiptNumber=" + receiptNumber);

        // =====================================================================
        // ШАГ 3: Проверка, что клиент существует и не заблокирован
        // =====================================================================
        auto clientOpt = db_->getClientById(clientId);
        if (!clientOpt) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Client not found"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: client not found, id=" +
                std::to_string(clientId));
            return;
        }

        if (db_->isClientBlocked(clientId)) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Client is blocked, sale rejected"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: BLOCKED client " +
                std::to_string(clientId) + " attempted sale");
            return;
        }

        // =====================================================================
        // ШАГ 4: Поиск товара по client_id и цене
        // =====================================================================
        json itemData = db_->findItemByBarcode(clientId, itemPrice);
        if (itemData.contains("error")) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = itemData.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleSaleRegister: item not found for clientId=" +
                std::to_string(clientId) + ", price=" + std::to_string(itemPrice));
            return;
        }

        int itemId = itemData["id"].get<int>();
        g_serverLogger.info("handleSaleRegister: matched itemId=" + std::to_string(itemId) +
            ", available=" + std::to_string(itemData["available"].get<int>()));

        // =====================================================================
        // ШАГ 5: Регистрация продажи (атомарная операция в БД)
        // =====================================================================
        bool saleSuccess = db_->registerItemSale(itemId, itemPrice, source,
            receiptNumber, barcodePayload);

        if (saleSuccess) {
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            json successResp;
            successResp["success"] = true;
            successResp["item_id"] = itemId;
            successResp["client_id"] = clientId;
            successResp["sale_price"] = itemPrice;
            successResp["remaining_units"] = itemData["available"].get<int>() - 1;
            response_.body() = successResp.dump();
            g_serverLogger.info("handleSaleRegister: SALE REGISTERED SUCCESSFULLY - itemId=" +
                std::to_string(itemId) + ", clientId=" + std::to_string(clientId));
        }
        else {
            response_.result(http::status::conflict);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Sale registration failed: no available units or item expired"} }.dump();
            g_serverLogger.error("handleSaleRegister: SALE REGISTRATION FAILED for itemId=" +
                std::to_string(itemId));
        }
        response_.prepare_payload();
    }

    // =========================================================================
    // ОБРАБОТЧИК GET /api/v1/director/expired_items
    // Возвращает список товаров, выбывших из продажи по истечении 15 дней.
    // ДОСТУП: только директор (+79914869324)
    // =========================================================================
    void handleDirectorExpiredItems(const std::string& client_ip) {
        g_serverLogger.info("handleDirectorExpiredItems: request from " + client_ip);

        // Проверка авторизации
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            return;
        }

        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            return;
        }

        // Проверка роли директора через БД (вместо хардкода телефона)
        auto directorOpt = db_->getClientByPhone(*phoneOpt);
        if (!directorOpt || directorOpt->role != "director") {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorExpiredItems: access denied for phone=" + *phoneOpt +
                " from " + client_ip + " (role is not director)");
            return;
        }
        // Получение просроченных товаров
        json expiredItems = db_->getExpiredItems();

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"expired_items", expiredItems},
            {"count", expiredItems.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleDirectorExpiredItems: returned " +
            std::to_string(expiredItems.size()) + " expired items to director");
    }

    // =========================================================================
    // ОБРАБОТЧИК POST /api/v1/clients/register_committee
    // Регистрация комитента с дополнительными данными (first_time)
    // =========================================================================
    void handleRegisterCommittee(const std::string& client_ip) {
        g_serverLogger.info("handleRegisterCommittee: request from " + client_ip);
        // Проверка авторизации
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Missing or invalid Authorization"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.body() = json{ {"error", "Invalid token"} }.dump();
            response_.prepare_payload();
            return;
        }
        // Проверка роли: только worker
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || clientOpt->role != "worker") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: worker role required"} }.dump();
            response_.prepare_payload();
            return;
        }
        // Разбор JSON
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            return;
        }
        // Валидация
        if (!body.contains("phone") || !body["phone"].is_string() ||
            !body.contains("last_name") || !body["last_name"].is_string() ||
            !body.contains("first_name") || !body["first_name"].is_string()) {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Missing required fields"} }.dump();
            response_.prepare_payload();
            return;
        }
        std::string phone = body["phone"].get<std::string>();
        std::string last_name = body["last_name"].get<std::string>();
        std::string first_name = body["first_name"].get<std::string>();
        std::string middle_name = body.value("middle_name", std::string{});
        std::string birth_date = body.value("birth_date", std::string{});
        std::string passport_type = body.value("passport_type", "rf");
        std::string passport_series = body.value("passport_series", std::string{});
        std::string passport_number = body.value("passport_number", std::string{});
        std::string address = body.value("address", std::string{});
        // Нормализация телефона
        std::string digits;
        for (char c : phone) if (c >= '0' && c <= '9') digits += c;
        if (digits.length() == 11 && (digits[0] == '7' || digits[0] == '8')) {
            phone = "+7" + digits.substr(1);
        }
        else if (digits.length() == 10) {
            phone = "+7" + digits;
        }
        else {
            response_.result(http::status::bad_request);
            response_.body() = json{ {"error", "Invalid phone number"} }.dump();
            response_.prepare_payload();
            return;
        }
        // Регистрация
        auto [ok, clientId] = db_->registerCommittee(phone, last_name, first_name,
            middle_name, birth_date, passport_type, passport_series, passport_number, address);
        if (ok) {
            response_.result(http::status::ok);
            json resp;
            resp["success"] = true;
            resp["client_id"] = clientId;
            resp["phone"] = phone;
            response_.body() = resp.dump();
            g_serverLogger.info("handleRegisterCommittee: SUCCESS, clientId=" +
                std::to_string(clientId) + ", phone=" + phone);
        }
        else {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to register committee"} }.dump();
        }
        response_.prepare_payload();
    }

    // =========================================================================
    // ОБРАБОТЧИК GET /api/v1/queue/display
    // Возвращает полное состояние всех 6 очередей для ТВ-дисплея:
    // - Ожидающие талоны (статус 'waiting' / 'pending')
    // - Принятые талоны (статус 'accepted') с номером окна
    // Обслуженные талоны (статус 'served') НЕ возвращаются.
    //
    // Формат ответа:
    // {
    //   "queues": {
    //     "general":    { "waiting": [...], "accepted": [...] },
    //     "first_time": { "waiting": [...], "accepted": [...] },
    //     "extra_20":   { "waiting": [...], "accepted": [...] },
    //     "trust":      { "waiting": [...], "accepted": [...] },
    //     "paid":       { "waiting": [...], "accepted": [...] },
    //     "expensive":  { "waiting": [...], "accepted": [...] }
    //   },
    //   "timestamp": 1234567890
    // }
    // =========================================================================
    void handleQueueDisplay(const std::string& client_ip) {
        g_serverLogger.info("handleQueueDisplay: request from " + client_ip);
        try {
            json queues;

            // === Очередь "Общая" (general) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingTickets("general");
                q["accepted"] = queue_->getAcceptedTickets("general");
                queues["general"] = q;
                g_serverLogger.info("handleQueueDisplay: general waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // === Очередь "Первый раз" (first_time) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingFirstTimeTickets();
                q["accepted"] = queue_->getAcceptedFirstTimeTickets();
                queues["first_time"] = q;
                g_serverLogger.info("handleQueueDisplay: first_time waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // === Очередь "+20 позиций" (extra_20) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingTickets("extra_20");
                q["accepted"] = queue_->getAcceptedTickets("extra_20");
                queues["extra_20"] = q;
                g_serverLogger.info("handleQueueDisplay: extra_20 waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // === Очередь "Доверие" (trust) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingTrustTickets();
                q["accepted"] = queue_->getAcceptedTrustTickets();
                queues["trust"] = q;
                g_serverLogger.info("handleQueueDisplay: trust waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // === Очередь "Платный приём" (paid) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingTickets("paid");
                q["accepted"] = queue_->getAcceptedTickets("paid");
                queues["paid"] = q;
                g_serverLogger.info("handleQueueDisplay: paid waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // === Очередь "Дорогой товар" (expensive) ===
            {
                json q;
                q["waiting"] = queue_->getWaitingTickets("expensive");
                q["accepted"] = queue_->getAcceptedTickets("expensive");
                queues["expensive"] = q;
                g_serverLogger.info("handleQueueDisplay: expensive waiting=" +
                    std::to_string(q["waiting"].size()) +
                    ", accepted=" + std::to_string(q["accepted"].size()));
            }

            // Формируем итоговый ответ
            json resp;
            resp["queues"] = queues;
            resp["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            response_.body() = resp.dump();
            response_.prepare_payload();

            g_serverLogger.info("handleQueueDisplay: response sent to " + client_ip);
        }
        catch (const std::exception& e) {
            g_serverLogger.error("handleQueueDisplay error: " + std::string(e.what()));
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Queue display failed"} }.dump();
            response_.prepare_payload();
        }
    }

    // =========================================================================
    // НОВЫЙ ОБРАБОТЧИК: УДАЛЕНИЕ ТАЛОНА, ПО КОТОРОМУ КОМИТЕНТ НЕ ПРИШЁЛ
    // POST /api/v1/queue/delete_ticket
    // =========================================================================
    //
    // Тело запроса (обязательные поля):
    // {
    //   "ticket_number": "G001",
    //   "queue_type": "general"
    // }
    //
    // Допустимые значения queue_type:
    //   "general", "first_time", "extra_20", "trust", "paid", "expensive"
    //
    // Логика работы:
    //   1. Проверка авторизации: JWT токен из заголовка Authorization.
    //   2. Проверка роли: только 'worker' или 'director'.
    //   3. Валидация тела запроса.
    //   4. Делегирование удаления в QueueService -> Database.
    //   5. Database выполняет атомарный DELETE с проверкой:
    //      - статус = 'waiting' (или 'pending' для trust);
    //      - возраст талона >= 120 секунд (2 минуты).
    //   6. Возврат результата клиенту.
    //
    // Формат ответа при успехе:
    // { "success": true, "deleted_ticket": "G001", "queue_type": "general" }
    //
    // Формат ответа при ошибке:
    // { "error": "Ticket not found, already accepted/served, or less than 2 minutes old" }
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    // Удаление выполняется в одной транзакции БД. Проверка статуса и
    // возраста талона в условии DELETE исключает удаление уже принятых,
    // обслуженных или свежих талонов. Параллельные запросы на удаление
    // одного и того же талона безопасны: первый DELETE удалит строку,
    // второй вернёт 0 строк и метод вернёт false.
    // =========================================================================
    void handleQueueDeleteTicket(const std::string& client_ip) {
        g_serverLogger.info("handleQueueDeleteTicket: request received from " + client_ip);

        // =================================================================
        // ШАГ 1: Проверка авторизации (JWT токен)
        // =================================================================
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) {
            authHeader = it->value();
        }
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: missing or invalid Authorization "
                "header from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: invalid or expired token from " +
                client_ip);
            return;
        }

        // =================================================================
        // ШАГ 2: Проверка роли - только 'worker' или 'director'
        // =================================================================
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || (clientOpt->role != "worker" && clientOpt->role != "director")) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: worker or director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: access denied for phone=" +
                *phoneOpt + ", role=" +
                (clientOpt ? clientOpt->role : "unknown") +
                " from " + client_ip);
            return;
        }

        // =================================================================
        // ШАГ 3: Разбор и валидация тела запроса
        // =================================================================
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("handleQueueDeleteTicket: parsed body: " + body.dump());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: JSON parse error from " +
                client_ip + ": " + std::string(e.what()));
            return;
        }

        if (!body.contains("ticket_number") || !body["ticket_number"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "ticket_number is required and must be a string"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: missing or invalid ticket_number "
                "from " + client_ip);
            return;
        }
        if (!body.contains("queue_type") || !body["queue_type"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "queue_type is required and must be a string"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: missing or invalid queue_type "
                "from " + client_ip);
            return;
        }

        std::string ticketNumber = body["ticket_number"].get<std::string>();
        std::string queueType = body["queue_type"].get<std::string>();

        g_serverLogger.info("handleQueueDeleteTicket: ticketNumber=" + ticketNumber +
            ", queueType=" + queueType +
            ", workerPhone=" + *phoneOpt +
            ", workerId=" + std::to_string(clientOpt->id) +
            ", from=" + client_ip);

        // =================================================================
        // ШАГ 4: Выполняем удаление в зависимости от типа очереди
        // =================================================================
        bool deleted = false;
        if (queueType == "first_time") {
            g_serverLogger.info("handleQueueDeleteTicket: delegating to "
                "queue_->deleteFirstTimeTicket()");
            deleted = queue_->deleteFirstTimeTicket(ticketNumber);
        }
        else if (queueType == "trust") {
            g_serverLogger.info("handleQueueDeleteTicket: delegating to "
                "queue_->deleteTrustTicket()");
            deleted = queue_->deleteTrustTicket(ticketNumber);
        }
        else {
            // general, extra_20, paid, expensive
            g_serverLogger.info("handleQueueDeleteTicket: delegating to "
                "queue_->deleteWaitingTicket()");
            deleted = queue_->deleteWaitingTicket(ticketNumber, queueType);
        }

        // =================================================================
        // ШАГ 5: Формируем ответ клиенту
        // =================================================================
        if (deleted) {
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"success", true},
                {"deleted_ticket", ticketNumber},
                {"queue_type", queueType}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.info("handleQueueDeleteTicket: SUCCESS - ticket " + ticketNumber +
                " deleted from queue " + queueType +
                " by worker " + *phoneOpt + " (id=" +
                std::to_string(clientOpt->id) + ")");
        }
        else {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Ticket not found, already accepted/served, or less than 2 minutes old"}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleQueueDeleteTicket: FAILED - ticket " + ticketNumber +
                " could NOT be deleted from queue " + queueType +
                ". Possible reasons: not found, not in waiting status, "
                "or less than 2 minutes old.");
        }
    }

    // =========================================================================
    // НОВЫЙ ОБРАБОТЧИК: ПОИСК КОМИТЕНТА ПО ИЛИ ИЛИ ФАМИЛИИ/ИМЕНИ/ОТЧЕСТВУ
    // GET /api/v1/clients/search
    // =========================================================================
    //
    // Параметры запроса (один из вариантов):
    //
    // Вариант 1 - поиск по:
    //   GET /api/v1/clients/search?id=42
    //
    // Вариант 2 - поиск по (все три поля необязательные):
    //   GET /api/v1/clients/search?last_name=Иванов&first_name=Иван&middle_name=Иванович
    //
    // Приоритет: если указан параметр 'id', поиск выполняется по.
    // Параметры ФИО в этом случае игнорируются.
    //
    // Возвращает:
    // {
    //   "clients": [ { ...все поля клиента... }, ... ],
    //   "count": 1
    // }
    //
    // ДОСТУП: только для роли 'worker' или 'director'.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    // Метод выполняет только SELECT (чтение) из БД. Данные не изменяет.
    // Каждый запрос выполняется в собственной транзакции.
    // =========================================================================
    void handleClientSearch(const std::string& client_ip) {
        g_serverLogger.info("handleClientSearch: request received from " + client_ip);

        // =================================================================
        // ШАГ 1: Проверка авторизации ( токен)
        // =================================================================
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) {
            authHeader = it->value();
        }
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing or invalid Authorization header"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientSearch: missing or invalid Authorization "
                "header from " + client_ip);
            return;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid or expired token"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientSearch: invalid or expired token from " +
                client_ip);
            return;
        }

        // =================================================================
        // ШАГ 2: Проверка роли - только 'worker' или 'director'
        // =================================================================
        auto clientOpt = db_->getClientByPhone(*phoneOpt);
        if (!clientOpt || (clientOpt->role != "worker" && clientOpt->role != "director" && clientOpt->role !="cashier")) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: worker or director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientSearch: access denied for phone=" +
                *phoneOpt + ", role=" +
                (clientOpt ? clientOpt->role : "unknown") +
                " from " + client_ip);
            return;
        }

        // =================================================================
        // ШАГ 3: Извлечение параметров запроса из
        // =================================================================
        std::string query = request_.target();

        // Лямбда для извлечения параметра из query string
        auto extractParam = [&query](const std::string& paramName) -> std::string {
            std::string searchStr = "?" + paramName + "=";
            size_t pos = query.find(searchStr);
            if (pos == std::string::npos) {
                searchStr = "&" + paramName + "=";
                pos = query.find(searchStr);
            }
            if (pos == std::string::npos) {
                return "";
            }
            std::string value = query.substr(pos + searchStr.length());
            size_t end = value.find('&');
            if (end != std::string::npos) {
                value = value.substr(0, end);
            }
            return value;
            };

        std::string idStr = extractParam("id");
        std::string lastName = extractParam("last_name");
        std::string firstName = extractParam("first_name");
        std::string middleName = extractParam("middle_name");

        g_serverLogger.info("handleClientSearch: params - id='" + idStr +
            "', last_name='" + lastName +
            "', first_name='" + firstName +
            "', middle_name='" + middleName + "'");

        json clientsArray = json::array();

        // =================================================================
        // ШАГ 4: Поиск по (приоритет над ФИО)
        // =================================================================
        if (!idStr.empty()) {
            int clientId = 0;
            try {
                clientId = std::stoi(idStr);
            }
            catch (...) {
                response_.result(http::status::bad_request);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", "Invalid id parameter: must be an integer"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleClientSearch: invalid id parameter '" +
                    idStr + "' from " + client_ip);
                return;
            }

            if (clientId <= 0) {
                response_.result(http::status::bad_request);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", "id must be a positive integer"} }.dump();
                response_.prepare_payload();
                g_serverLogger.warning("handleClientSearch: non-positive id=" +
                    std::to_string(clientId) + " from " + client_ip);
                return;
            }

            g_serverLogger.info("handleClientSearch: searching by id=" +
                std::to_string(clientId));
            json clientInfo = db_->getClientFullInfo(clientId);
            if (!clientInfo.contains("error")) {
                clientsArray.push_back(clientInfo);
                g_serverLogger.info("handleClientSearch: client found by id=" +
                    std::to_string(clientId));
            }
            else {
                g_serverLogger.warning("handleClientSearch: client NOT found by id=" +
                    std::to_string(clientId));
            }
        }
        // =================================================================
        // ШАГ 5: Поиск по фамилии, имени и отчеству
        // =================================================================
        else if (!lastName.empty() || !firstName.empty() || !middleName.empty()) {
            g_serverLogger.info("handleClientSearch: searching by FIO - "
                "lastName='" + lastName + "', firstName='" + firstName +
                "', middleName='" + middleName + "'");
            clientsArray = db_->searchClientsByFio(lastName, firstName, middleName);
            g_serverLogger.info("handleClientSearch: FIO search returned " +
                std::to_string(clientsArray.size()) + " clients");
        }
        // =================================================================
        // ШАГ 6: Ни один параметр поиска не указан
        // =================================================================
        else {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Provide search parameters: id, or last_name/first_name/middle_name"}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleClientSearch: no search parameters provided "
                "from " + client_ip);
            return;
        }

        // =================================================================
        // ШАГ 7: Формируем успешный ответ
        // =================================================================
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"clients", clientsArray},
            {"count", clientsArray.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleClientSearch: SUCCESS - returned " +
            std::to_string(clientsArray.size()) +
            " client(s) to worker " + *phoneOpt +
            " (id=" + std::to_string(clientOpt->id) + ")" +
            " from " + client_ip);
    }

    // =========================================================================
    // ОБРАБОТЧИКИ МОДУЛЯ «КАССИР»
    // =========================================================================
    //
    // ДОСТУП: только роль 'cashier' или 'director'.
    // Проверка выполняется через JWT → getClientByPhone → role.
    //
    // ПОТОКОБЕЗОПАСНОСТЬ:
    // Все обработчики выполняют операции в рамках одной транзакции БД.
    // Параллельные запросы на пометку одного и того же товара безопасны:
    // первый UPDATE изменит статус, второй вернёт 0 строк.
    // =========================================================================

    /**
     * @brief Проверка роли кассира или директора.
     * @return ID пользователя или 0 при отказе.
     */
    int verifyCashierRole(const std::string& client_ip,
        const std::string& handlerName) {
        std::string authHeader;
        auto it = request_.find(http::field::authorization);
        if (it != request_.end()) authHeader = it->value();
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Missing or invalid Authorization header"}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.warning(handlerName +
                ": missing auth from " + client_ip);
            return 0;
        }
        std::string token = authHeader.substr(7);
        auto phoneOpt = auth_->verifyJWT(token);
        if (!phoneOpt) {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid or expired token"}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.warning(handlerName +
                ": invalid token from " + client_ip);
            return 0;
        }
        auto userOpt = db_->getClientByPhone(*phoneOpt);
        if (!userOpt ||
            (userOpt->role != "cashier" && userOpt->role != "director")) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Access denied: cashier or director role required"}
            }.dump();
            response_.prepare_payload();
            g_serverLogger.warning(handlerName +
                ": access denied for phone=" + *phoneOpt +
                " from " + client_ip);
            return 0;
        }
        return userOpt->id;
    }

    /**
     * @brief Извлечение параметра из query string для кассира.
     */
    std::string extractCashierQueryParam(const std::string& paramName) {
        std::string query = request_.target();
        std::string searchStr = "?" + paramName + "=";
        size_t pos = query.find(searchStr);
        if (pos == std::string::npos) {
            searchStr = "&" + paramName + "=";
            pos = query.find(searchStr);
        }
        if (pos == std::string::npos) return "";
        std::string value = query.substr(pos + searchStr.length());
        size_t end = value.find('&');
        if (end != std::string::npos) value = value.substr(0, end);
        return value;
    }

    // -------------------------------------------------------------------------
    // GET /api/v1/cashier/appendices?client_id=...
    // Возвращает список приложений к договору комитента.
    // -------------------------------------------------------------------------
    void handleCashierGetAppendices(const std::string& client_ip) {
        g_serverLogger.info("handleCashierGetAppendices: from " + client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierGetAppendices");
        if (userId == 0) return;

        std::string clientIdStr = extractCashierQueryParam("client_id");
        if (clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id parameter required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = 0;
        try { clientId = std::stoi(clientIdStr); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid client_id"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        json appendices = db_->getClientAppendices(clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"appendices", appendices},
            {"count", appendices.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierGetAppendices: returned " +
            std::to_string(appendices.size()) +
            " appendices for clientId=" + std::to_string(clientId));
    }

    // -------------------------------------------------------------------------
    // GET /api/v1/cashier/appendix_items?appendix_id=...&client_id=...
    // Возвращает товары конкретного приложения.
    // -------------------------------------------------------------------------
    void handleCashierGetAppendixItems(const std::string& client_ip) {
        g_serverLogger.info("handleCashierGetAppendixItems: from " +
            client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierGetAppendixItems");
        if (userId == 0) return;

        std::string appendixIdStr = extractCashierQueryParam("appendix_id");
        std::string clientIdStr = extractCashierQueryParam("client_id");
        if (appendixIdStr.empty() || clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "appendix_id and client_id required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        long long appendixId = 0;
        int clientId = 0;
        try {
            appendixId = std::stoll(appendixIdStr);
            clientId = std::stoi(clientIdStr);
        }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid appendix_id or client_id"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        json items = db_->getAppendixItemsForCashier(appendixId, clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"items", items},
            {"count", items.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierGetAppendixItems: returned " +
            std::to_string(items.size()) + " items for appendixId=" +
            std::to_string(appendixId));
    }

    // -------------------------------------------------------------------------
    // POST /api/v1/cashier/mark_returned
    // Тело: {"client_id": 42, "item_ids": [1, 2, 3]}
    // Помечает товары как «возвращённые комитенту».
    // -------------------------------------------------------------------------
    void handleCashierMarkReturned(const std::string& client_ip) {
        g_serverLogger.info("handleCashierMarkReturned: from " + client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierMarkReturned");
        if (userId == 0) return;

        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid JSON payload"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("client_id") || !body["client_id"].is_number() ||
            !body.contains("item_ids") || !body["item_ids"].is_array()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id and item_ids array required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = body["client_id"].get<int>();
        std::vector<int> itemIds;
        for (const auto& id : body["item_ids"]) {
            if (id.is_number()) itemIds.push_back(id.get<int>());
        }
        if (itemIds.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "item_ids array is empty"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int markedCount = db_->markItemsReturned(itemIds, clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"success", true},
            {"marked_count", markedCount},
            {"requested_count", static_cast<int>(itemIds.size())}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierMarkReturned: clientId=" +
            std::to_string(clientId) + ", marked=" +
            std::to_string(markedCount) + " by cashier=" +
            std::to_string(userId));
    }

    // -------------------------------------------------------------------------
    // POST /api/v1/cashier/mark_compensated
    // Тело: {"client_id": 42, "item_ids": [1, 2, 3]}
    // Помечает товары как «возмещён ущерб за утрату».
    // -------------------------------------------------------------------------
    void handleCashierMarkCompensated(const std::string& client_ip) {
        g_serverLogger.info("handleCashierMarkCompensated: from " +
            client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierMarkCompensated");
        if (userId == 0) return;

        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid JSON payload"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("client_id") || !body["client_id"].is_number() ||
            !body.contains("item_ids") || !body["item_ids"].is_array()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id and item_ids array required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = body["client_id"].get<int>();
        std::vector<int> itemIds;
        for (const auto& id : body["item_ids"]) {
            if (id.is_number()) itemIds.push_back(id.get<int>());
        }
        if (itemIds.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "item_ids array is empty"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int markedCount = db_->markItemsCompensated(itemIds, clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"success", true},
            {"marked_count", markedCount},
            {"requested_count", static_cast<int>(itemIds.size())}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierMarkCompensated: clientId=" +
            std::to_string(clientId) + ", marked=" +
            std::to_string(markedCount) + " by cashier=" +
            std::to_string(userId));
    }

    // -------------------------------------------------------------------------
    // GET /api/v1/cashier/sold_items?client_id=...
    // Возвращает реализованные товары для выплаты вознаграждения.
    // -------------------------------------------------------------------------
    void handleCashierGetSoldItems(const std::string& client_ip) {
        g_serverLogger.info("handleCashierGetSoldItems: from " + client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierGetSoldItems");
        if (userId == 0) return;

        std::string clientIdStr = extractCashierQueryParam("client_id");
        if (clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id parameter required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = 0;
        try { clientId = std::stoi(clientIdStr); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid client_id"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        json soldItems = db_->getClientSoldItemsForReward(clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"sold_items", soldItems},
            {"count", soldItems.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierGetSoldItems: returned " +
            std::to_string(soldItems.size()) +
            " sold items for clientId=" + std::to_string(clientId));
    }

    // -------------------------------------------------------------------------
    // GET /api/v1/cashier/unsold_items?client_id=...
    // Возвращает нереализованные товары со сроком > 15 суток.
    // -------------------------------------------------------------------------
    void handleCashierGetUnsoldItems(const std::string& client_ip) {
        g_serverLogger.info("handleCashierGetUnsoldItems: from " +
            client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierGetUnsoldItems");
        if (userId == 0) return;

        std::string clientIdStr = extractCashierQueryParam("client_id");
        if (clientIdStr.empty()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id parameter required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = 0;
        try { clientId = std::stoi(clientIdStr); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid client_id"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        json unsoldItems = db_->getClientUnsoldExpiredItems(clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{
            {"unsold_items", unsoldItems},
            {"count", unsoldItems.size()}
        }.dump();
        response_.prepare_payload();
        g_serverLogger.info("handleCashierGetUnsoldItems: returned " +
            std::to_string(unsoldItems.size()) +
            " unsold items for clientId=" + std::to_string(clientId));
    }

    // -------------------------------------------------------------------------
    // POST /api/v1/cashier/create_document
    // Тело: {"client_id": 42, "doc_type": "return", "items_data": "...",
    //        "total_amount": 5000.0}
    // Создаёт документ кассира и возвращает его порядковый номер.
    // -------------------------------------------------------------------------
    void handleCashierCreateDocument(const std::string& client_ip) {
        g_serverLogger.info("handleCashierCreateDocument: from " + client_ip);
        int userId = verifyCashierRole(client_ip,
            "handleCashierCreateDocument");
        if (userId == 0) return;

        json body;
        try { body = json::parse(request_.body()); }
        catch (...) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid JSON payload"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        if (!body.contains("client_id") || !body["client_id"].is_number() ||
            !body.contains("doc_type") || !body["doc_type"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "client_id and doc_type required"}
            }.dump();
            response_.prepare_payload();
            return;
        }
        int clientId = body["client_id"].get<int>();
        std::string docType = body["doc_type"].get<std::string>();
        std::string itemsData = body.value("items_data", std::string(""));
        double totalAmount = body.value("total_amount", 0.0);

        // Валидация типа документа
        if (docType != "return" && docType != "receipt" &&
            docType != "loss" && docType != "reward") {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Invalid doc_type: must be return, receipt, loss, or reward"}
            }.dump();
            response_.prepare_payload();
            return;
        }

        int docNumber = db_->createCashierDocument(clientId, docType,
            itemsData, totalAmount);
        if (docNumber > 0) {
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"success", true},
                {"doc_number", docNumber},
                {"doc_type", docType},
                {"client_id", clientId}
            }.dump();
            g_serverLogger.info("handleCashierCreateDocument: created doc #" +
                std::to_string(docNumber) + " type=" + docType +
                " for clientId=" + std::to_string(clientId));
        }
        else {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{
                {"error", "Failed to create document"}
            }.dump();
            g_serverLogger.error("handleCashierCreateDocument: failed for clientId=" +
                std::to_string(clientId));
        }
        response_.prepare_payload();
    }

    void doWrite() {
        http::async_write(stream_, response_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) {
                    self->onFail(ec, "write");
                    return;
                }
                self->onComplete();
            });
    }

    void onComplete() {
        stream_.async_shutdown(
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    // Игнорируем ошибки shutdown, так как клиент мог уже разорвать соединение
                }
                beast::error_code ec2;
                self->stream_.lowest_layer().shutdown(tcp::socket::shutdown_send, ec2);
            });
    }

    void onFail(beast::error_code ec, std::string const& what) {
        // ИНТЕЛЛЕКТУАЛЬНОЕ ЛОГГИРОВАНИЕ: 
        // Ошибки рукопожатия SSL часто вызваны ботами, сканирующими порты plaintext HTTP запросами.
        // Мы понижаем уровень логирования до WARNING, чтобы не засорять журнал ERROR.
        if (what == "handshake" && ec.category() == boost::asio::error::get_ssl_category()) {
            g_serverLogger.warning("SSL handshake failed (likely bot scan or incompatible client): " + ec.message());
        }
        else {
            g_serverLogger.error(what + ": " + ec.message());
        }
    }
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;

    std::shared_ptr<Database> db_;
    std::shared_ptr<AuthService> auth_;
    std::shared_ptr<QueueService> queue_;
    std::shared_ptr<OneCIntegration> onec_;
    std::shared_ptr<RateLimiter> rate_limiter_;

public:
    HttpListener(net::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint,
        std::shared_ptr<Database> db, std::shared_ptr<AuthService> auth,
        std::shared_ptr<QueueService> queue, std::shared_ptr<OneCIntegration> onec,
        std::shared_ptr<RateLimiter> rate_limiter)
        : ioc_(ioc), ctx_(ctx), acceptor_(ioc, endpoint),
        db_(std::move(db)), auth_(std::move(auth)), queue_(std::move(queue)),
        onec_(std::move(onec)), rate_limiter_(std::move(rate_limiter)) {
    }

    void run() {
        doAccept();
    }

private:
    void doAccept() {
        acceptor_.async_accept(
            ioc_,
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<HttpSession>(std::move(socket), self->ctx_, self->db_, self->auth_, self->queue_, self->onec_, self->rate_limiter_)->run();
                }
                self->doAccept();
            });
    }
};

void runServer(std::shared_ptr<Database> db, std::shared_ptr<AuthService> auth,
    std::shared_ptr<QueueService> queue, std::shared_ptr<OneCIntegration> onec,
    std::shared_ptr<RateLimiter> rate_limiter) {
    try {
        auto const address = net::ip::make_address("0.0.0.0");
        unsigned short port = static_cast<unsigned short>(Config::SERVER_PORT);
        net::io_context ioc{ 1 };

        // УЖЕСТЧЕННАЯ КОНФИГУРАЦИЯ SSL ДЛЯ ЗАЩИТЫ ОТ УЯЗВИМОСТЕЙ И НЕКОРРЕКТНЫХ КЛИЕНТОВ
        ssl::context ctx{ ssl::context::tlsv12_server }; // Разрешаем только TLS 1.2 и выше
        ctx.set_options(ssl::context::default_workarounds |
            ssl::context::no_sslv2 |
            ssl::context::no_sslv3 |
            ssl::context::no_tlsv1 |
            ssl::context::no_tlsv1_1 |
            ssl::context::single_dh_use);

        // Установка современных, безопасных наборов шифров
        SSL_CTX_set_cipher_list(ctx.native_handle(), "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384");

        ctx.use_certificate_chain_file(Config::SSL_CERT_PATH);
        ctx.use_private_key_file(Config::SSL_KEY_PATH, ssl::context::pem);

        g_serverLogger.info("SSL context configured with TLS 1.2+ and strong ciphers.");

        auto listener = std::make_shared<HttpListener>(ioc, ctx, tcp::endpoint{ address, port }, db, auth, queue, onec, rate_limiter);
        g_serverLogger.info("Server listening on port " + std::to_string(port));
        listener->run();
        ioc.run();
    }
    catch (const std::exception& e) {
        g_serverLogger.error("Server startup error: " + std::string(e.what()));
    }
}

// =========================================================================
// ФОНОВЫЙ ПОТОК: ПРОВЕРКА 15-ДНЕВНОГО SLA
// Запускается в main() после инициализации всех сервисов.
// Каждый час проверяет, есть ли приложения с истёкшим valid_until,
// и помечает непроданные товары как "выбывшие из продажи".
// =========================================================================
void startExpiredItemsMonitor(std::shared_ptr<Database> db) {
    std::thread([db]() {
        g_serverLogger.info("ExpiredItemsMonitor: background thread started");

        while (true) {
            // Проверяем каждые 3600 секунд (1 час)
            std::this_thread::sleep_for(std::chrono::seconds(3600));

            g_serverLogger.info("ExpiredItemsMonitor: periodic check triggered");

            try {
                int processed = db->processExpiredAppendices();
                if (processed > 0) {
                    g_serverLogger.info("ExpiredItemsMonitor: processed " +
                        std::to_string(processed) + " expired appendices");
                }
                else {
                    g_serverLogger.info("ExpiredItemsMonitor: no expired appendices found");
                }
            }
            catch (const std::exception& e) {
                g_serverLogger.error("ExpiredItemsMonitor error: " + std::string(e.what()));
            }
        }
        }).detach();

    g_serverLogger.info("ExpiredItemsMonitor: thread launched (interval=3600s)");
}

int main() {
    try {
        g_serverLogger.info("=== Server starting ===");

        std::string connStr = "host=" + std::string(Config::DB_HOST) +
            " port=" + std::to_string(Config::DB_PORT) +
            " dbname=" + std::string(Config::DB_NAME) +
            " user=" + std::string(Config::DB_USER) +
            " password=" + std::string(Config::DB_PASSWORD);
        auto conn = std::make_shared<pqxx::connection>(connStr);
        if (!conn->is_open()) {
            g_serverLogger.error("Database connection failed");
            return 1;
        }

        auto db = std::make_shared<Database>(conn, Config::ENCRYPTION_KEY);
        auto auth = std::make_shared<AuthService>(db);
        auto queue = std::make_shared<QueueService>(db);
        auto onec = std::make_shared<OneCIntegration>(db);
        auto rate_limiter = std::make_shared<RateLimiter>(5, std::chrono::minutes(1));

        if (!auth->initialize()) {
            g_serverLogger.error("Auth service initialization failed");
            return 1;
        }

        runServer(db, auth, queue, onec, rate_limiter);

		startExpiredItemsMonitor(db); // Запуск фонового потока мониторинга просроченных приложений
        
        g_serverLogger.info("=== Server shutdown ===");
        return 0;
    }
    catch (const std::exception& e) {
        g_serverLogger.error("Fatal error: " + std::string(e.what()));
        return 1;
    }
}
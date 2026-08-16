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
            // НОВЫЕ РОУТЫ: ПРИЛОЖЕНИЯ К ДОГОВОРУ (чеки комитентов)
            // =========================================================================
            else if (target.find("/api/v1/appendix/by_number") == 0 && method == http::verb::get) {
                handleAppendixByNumber(client_ip);
            }
            else if (target.find("/api/v1/appendix/latest") == 0 && method == http::verb::get) {
                handleAppendixLatest(client_ip);
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
        auto [ok, already_exists] = db_->registerClient(phone, last_name, first_name, middle_name, email, items_submitted, items_sold, role);

        if (!ok) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Database error during registration"} }.dump();
            response_.prepare_payload();
            g_serverLogger.log(LogLevel::ERROR, "Client registration failed for: " + phone + " from " + client_ip + " -> Database error");
            return;
        }

        // Генерация токенов для любого успешного случая (новый или существующий)
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

        g_serverLogger.info("Client registration attempt for: " + phone +
            " | Success: " + std::to_string(ok) +
            " | Already exists: " + std::to_string(already_exists) +
            " | Role saved: " + role);

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();
        g_serverLogger.info("Client registration successful for: " + phone + " from " + client_ip + " | Already exists: " + std::to_string(already_exists));
    }

   
    
    // -------------------------------------------------------------------------
    // POST /api/v1/clients/by_phone (post-метод)
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
        g_serverLogger.info("Checking client by phone: " + phone + " from " + client_ip);

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
        // ПРОВЕРКА БЛОКИРОВКИ КЛИЕНТА
        // Если клиент заблокирован директором, возвращаем ошибку с сообщением
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

        // === проверка роли клиент или товаровед ===
        if (clientOpt->role != "worker" && clientOpt->role != "client") {
            response_.result(http::status::forbidden);
            response_.body() = json{ {"error", "Access denied: not a worker"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("Access denied for non-worker phone: " + phone);
            return;
        }

        // Генерируем токены
        auto tokens = auth_->generateTokens(phone);

        json resp;
        resp["id"] = clientOpt->id;
        resp["name"] = clientOpt->name;
        resp["phone"] = clientOpt->phone;
        resp["access_token"] = tokens.first;
        resp["refresh_token"] = tokens.second;
        g_serverLogger.info("access_token: "+ tokens.first);
        g_serverLogger.info("refresh_token:" + tokens.second);

        // Срок действия (как в generateTokens) – 3600 секунд для access
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
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();
        g_serverLogger.info("Client data returned for id " + std::to_string(id) + " (" + clientOpt->phone + ")");
    }

    void handleGetTicket(const std::string& client_ip) {
        json body;
        try {
            body = json::parse(request_.body());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON payload"} }.dump();
            response_.prepare_payload();
            return;
        }

        int clientId = body["client_id"].get<int>();
        std::string queueType = body["queue_type"].get<std::string>();
        int itemsCount = body["items_count"].get<int>();

        json ticket = queue_->getTicket(clientId, queueType, itemsCount);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", true}, {"ticket", ticket} }.dump();
        response_.prepare_payload();
    }
	// --- методы для работы с очередью для пользователей, пришедших впервые (first_time) ---
    void handleFirstTimeCreate(const std::string& client_ip) {
        json body;
        try { body = json::parse(request_.body()); }
        catch (...) { /* ignore */ }
        std::string window = body.value("window", "1");
        std::string ticketNumber = queue_->createFirstTimeTicket(window);
        if (ticketNumber.empty()) {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to create ticket"} }.dump();
        }
        else {
            response_.result(http::status::ok);
            response_.body() = json{ {"ticket_number", ticketNumber} }.dump();
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
        std::string windowNumber;
        if (queue_->acceptFirstTimeTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true}, {"window_number", windowNumber} }.dump();
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or already accepted"} }.dump();
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
        std::string windowNumber;
        if (queue_->acceptTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true}, {"window_number", windowNumber} }.dump();
            g_serverLogger.info("handleQueueAccept: accepted " + ticketNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or already accepted"} }.dump();
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

	// --- метод для пользователей, которые пришли оставить товар на доверии ---
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
        auto ticketNumberOpt = db_->createTrustAcceptance(clientId);

        if (!ticketNumberOpt) {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to create trust acceptance"} }.dump();
            response_.prepare_payload();
            g_serverLogger.info("ticketNumberOpt is empty");
            return;
        }

        // Получаем информацию о созданном талоне (окно)
        auto info = db_->getTrustTicketInfo(*ticketNumberOpt);
        if (!info) {
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Failed to retrieve ticket info"} }.dump();
            response_.prepare_payload();
            return;
        }

        json resp;
        resp["success"] = true;
        resp["ticket_number"] = *ticketNumberOpt;
        resp["window_number"] = (*info)["window_number"];
        resp["position"] = 0; // для доверия позиция всегда 0
        resp["created_at"] = (*info)["created_at"];

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();

        g_serverLogger.info("Trust acceptance created for client " + std::to_string(clientId) +
            ", ticket: " + *ticketNumberOpt + " from " + client_ip);
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
        std::string windowNumber;
        if (queue_->acceptTrustTicket(ticketNumber, windowNumber)) {
            response_.result(http::status::ok);
            response_.body() = json{ {"success", true}, {"window_number", windowNumber} }.dump();
            g_serverLogger.info("handleTrustAccept: accepted " + ticketNumber + " from " + client_ip);
        }
        else {
            response_.result(http::status::not_found);
            response_.body() = json{ {"error", "Ticket not found or already accepted"} }.dump();
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

        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА
        // Директор магазина определяется по телефону +79914869324
        // Это захардкоженная проверка согласно требованиям задания
        constexpr const char* DIRECTOR_PHONE = "+79914869324";
        if (*phoneOpt != DIRECTOR_PHONE) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorStats: access denied for phone=" + *phoneOpt +
                " from " + client_ip + " (director phone required: " + DIRECTOR_PHONE + ")");
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
     * Блокирует или разблокирует клиента по ID
     *
     * Тело запроса:
     * {
     *   "client_id": 123,
     *   "blocked": true  // true - заблокировать, false - разблокировать
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

        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА
        constexpr const char* DIRECTOR_PHONE = "+79914869324";
        if (*phoneOpt != DIRECTOR_PHONE) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorBlockClient: access denied for phone=" + *phoneOpt +
                " from " + client_ip);
            return;
        }

        // 4. Получаем ID директора из БД
        auto directorOpt = db_->getClientByPhone(*phoneOpt);
        if (!directorOpt) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Director not found in database"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("handleDirectorBlockClient: director not found in DB, phone=" + *phoneOpt);
            return;
        }
        int directorId = directorOpt->id;

        // 5. Разбор JSON тела запроса
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

        // 6. Валидация обязательных полей
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

        g_serverLogger.info("handleDirectorBlockClient: clientId=" + std::to_string(clientId) +
            ", blocked=" + std::to_string(blocked) +
            ", directorId=" + std::to_string(directorId));

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

        // 9. Выполняем блокировку/разблокировку
        if (db_->setClientBlocked(clientId, blocked, directorId)) {
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            json successResponse;
            successResponse["success"] = true;
            successResponse["client_id"] = clientId;
            successResponse["blocked"] = blocked;
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

        // 3. ПРОВЕРКА РОЛИ ДИРЕКТОРА
        constexpr const char* DIRECTOR_PHONE = "+79914869324";
        if (*phoneOpt != DIRECTOR_PHONE) {
            response_.result(http::status::forbidden);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Access denied: director role required"} }.dump();
            response_.prepare_payload();
            g_serverLogger.warning("handleDirectorGetClients: access denied for phone=" + *phoneOpt +
                " from " + client_ip);
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

        g_serverLogger.info("=== Server shutdown ===");
        return 0;
    }
    catch (const std::exception& e) {
        g_serverLogger.error("Fatal error: " + std::string(e.what()));
        return 1;
    }
}
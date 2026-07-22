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

// Глобальный экземпляр логгера (согласно вашей архитектуре)

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
            else if (target == "/api/v1/auth/email_otp/request" && method == http::verb::post) {
                handleEmailOTPRequest(client_ip);
            }
            else if (target == "/api/v1/auth/email_otp/verify" && method == http::verb::post) {
                handleEmailOTPVerify(client_ip);
            }
            else if (target == "/api/v1/auth/totp/setup" && method == http::verb::post) {
                handleTOTPSetup(client_ip);
            }
            else if (target == "/api/v1/auth/totp/verify" && method == http::verb::post) {
                handleTOTPVerify(client_ip);
            }
            else if (target == "/api/v1/queue/get_ticket" && method == http::verb::post) {
                handleGetTicket(client_ip);
            }
            else if (target == "/api/v1/queue/trust_acceptance" && method == http::verb::post) {
                handleTrustAcceptance(client_ip);
            }
            else if (target == "/api/v1/onec/sync" && method == http::verb::post) {
                handleOneCSync(client_ip);
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

        //Получаем пару <успех, уже_существует>
        auto [ok, already_exists] = db_->registerClient(phone, last_name, first_name, middle_name, email, items_submitted, items_sold);
        
        if (!ok) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Database error during registration"} }.dump();
            response_.prepare_payload();
			g_serverLogger.log(LogLevel::ERROR, "Client registration failed for: " + phone + " from " + client_ip + " -> Database error");
            return;
        }

        g_serverLogger.info("Client registration attempt for: " + phone +
            " | Success: " + std::to_string(ok) +
            " | Already exists: " + std::to_string(already_exists));

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");

        // Добавляем флаг already_exists в ответ клиенту
        json resp = { {"success", ok}, {"phone", phone}, {"already_exists", already_exists} };
        response_.body() = resp.dump();
        response_.prepare_payload();
		g_serverLogger.info("Client registration successful for: " + phone + " from " + client_ip + " | Already exists: " + std::to_string(already_exists));
    }

    void handleTOTPSetup(const std::string& client_ip) {
        if (!rate_limiter_->allow(client_ip + "_setup")) {
            response_.result(http::status::too_many_requests);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Rate limit exceeded"} }.dump();
            response_.prepare_payload();
            return;
        }

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

        std::string phone = body["phone"].get<std::string>();
        std::string digits;
        for (char c : phone) if (c >= '0' && c <= '9') digits += c;
        if (digits.length() == 11 && (digits[0] == '7' || digits[0] == '8')) {
            phone = "+7" + digits.substr(1);
        }

        auto secretOpt = db_->getTOTPSecret(phone);
        std::string secret;
        if (secretOpt && !secretOpt->empty()) {
            secret = *secretOpt;
        }
        else {
            secret = auth_->generateTOTPSecret();
            db_->setTOTPSecret(phone, secret);
        }

        std::string uri = auth_->generateTOTPURI(secret, phone);

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", true}, {"secret", secret}, {"uri", uri} }.dump();
        response_.prepare_payload();
    }

    void handleTOTPVerify(const std::string& client_ip) {
        if (!rate_limiter_->allow(client_ip + "_verify")) {
            response_.result(http::status::too_many_requests);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Rate limit exceeded"} }.dump();
            response_.prepare_payload();
            return;
        }

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

        std::string phone = body["phone"].get<std::string>();
        std::string code = body["code"].get<std::string>();

        std::string digits;
        for (char c : phone) if (c >= '0' && c <= '9') digits += c;
        if (digits.length() == 11 && (digits[0] == '7' || digits[0] == '8')) {
            phone = "+7" + digits.substr(1);
        }

        if (auth_->verifyTOTP(phone, code)) {
            auto tokens = auth_->generateTokens(phone);
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"access_token", tokens.first}, {"refresh_token", tokens.second}, {"success", true} }.dump();
            g_serverLogger.info("TOTP verification successful for: " + phone);
        }
        else {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid TOTP code or replay detected"} }.dump();
            g_serverLogger.warning("TOTP verification FAILED for: " + phone);
        }
        response_.prepare_payload();
    }

    void handleEmailOTPRequest(const std::string& client_ip) {
        g_serverLogger.info("[handleEmailOTPRequest] === НАЧАЛО ОБРАБОТКИ ЗАПРОСА ===");
        g_serverLogger.info("[handleEmailOTPRequest] client_ip: " + client_ip);

        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("[handleEmailOTPRequest] JSON body успешно распарсен: " + body.dump());
        }
        catch (const json::parse_error& e) {
            g_serverLogger.error("[handleEmailOTPRequest] JSON parse error: " + std::string(e.what()));
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            doWrite();
            return;
        }

        std::string phone = body.value("phone", "");
        std::string email = body.value("email", "");

        g_serverLogger.info("[handleEmailOTPRequest] Извлечённые поля: phone=[" + phone + "], email=[" + email + "]");

        if (phone.empty() || email.empty()) {
            g_serverLogger.warning("[handleEmailOTPRequest] ОШИБКА ВАЛИДАЦИИ: phone.empty()=" + std::string(phone.empty() ? "true" : "false") +
                ", email.empty()=" + std::string(email.empty() ? "true" : "false"));
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Phone and email are required"} }.dump();
            response_.prepare_payload();
            doWrite();
            return;
        }

        g_serverLogger.info("[handleEmailOTPRequest] Валидация пройдена. Вызываем auth_->requestEmailOTP(phone, email)...");

        bool emailSent = false;
        try {
            emailSent = auth_->requestEmailOTP(phone, email);
            g_serverLogger.info("[handleEmailOTPRequest] auth_->requestEmailOTP() вернула: " + std::string(emailSent ? "true" : "false"));
        }
        catch (const std::exception& e) {
            g_serverLogger.error("[handleEmailOTPRequest] ИСКЛЮЧЕНИЕ при вызове requestEmailOTP: " + std::string(e.what()));
            // ИСПРАВЛЕНИЕ: Не прячем исключение под fallback — пробрасываем дальше для обработки в catch-блоке handleRequest
            throw;
        }

        // ИСПРАВЛЕНИЕ: Убран fallback, который имитировал успех при emailSent == false
        // Теперь при emailSent == false возвращаем клиенту реальную ошибку
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");

        if (emailSent) {
            response_.body() = json{ {"success", true}, {"message", "OTP sent to email"} }.dump();
            g_serverLogger.info("[handleEmailOTPRequest] Email OTP УСПЕШНО отправлен для: " + phone);
        }
        else {
            // ИСПРАВЛЕНИЕ: УДАЛЕН TEST FALLBACK. При сбое SMTP клиент получает реальную ошибку.
            // Вызывающий UI ОБЯЗАН обработать success=false и показать пользователю сообщение.
            g_serverLogger.error("[handleEmailOTPRequest] Email OTP НЕ ОТПРАВЛЕН для: " + phone + ". Причина: auth_->requestEmailOTP() вернула false.");
            g_serverLogger.error("[handleEmailOTPRequest] Возможные причины:");
            g_serverLogger.error("  1. SMTP-сервер недоступен или отверг аутентификацию");
            g_serverLogger.error("  2. Неверный пароль приложения Яндекс");
            g_serverLogger.error("  3. Брандмауэр блокирует исходящий порт 465");
            g_serverLogger.error("  4. Email клиента не совпадает с email в базе данных");
            g_serverLogger.error("  5. Клиент с указанным phone не найден в БД");
            g_serverLogger.error("  6. Внутренняя ошибка генерации OTP-кода");
            response_.body() = json{ {"success", false}, {"message", "Failed to send OTP email. Please try again later or contact support."} }.dump();
        }

        response_.prepare_payload();
        g_serverLogger.info("[handleEmailOTPRequest] === ЗАВЕРШЕНИЕ ОБРАБОТКИ ===");
        doWrite();
    }

    void handleEmailOTPVerify(const std::string& client_ip) {
        g_serverLogger.info("Received request: POST /api/v1/auth/email_otp/verify from " + client_ip);
        json body;
        try {
            body = json::parse(request_.body());
        }
        catch (const json::parse_error& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            doWrite();
            return;
        }

        std::string phone = body.value("phone", "");
        std::string code = body.value("code", "");

        if (phone.empty() || code.length() != 6) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Valid phone and 6-digit code required"} }.dump();
            response_.prepare_payload();
            doWrite();
            return;
        }

        try {
            auto [success, tokens] = auth_->verifyEmailOTP(phone, code);
            if (success) {
                response_.result(http::status::ok);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"success", true}, {"access_token", tokens.first}, {"refresh_token", tokens.second} }.dump();
                g_serverLogger.info("Email OTP verification successful for: " + phone);
            }
            else {
                response_.result(http::status::unauthorized);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", "Invalid or expired OTP code"} }.dump();
                g_serverLogger.warning("Email OTP verification FAILED for: " + phone);
            }
        }
        catch (const std::exception& e) {
            g_serverLogger.error("Exception in verifyEmailOTP: " + std::string(e.what()));
            response_.result(http::status::internal_server_error);
            response_.body() = json{ {"error", "Internal server error"} }.dump();
        }

        response_.prepare_payload();
        doWrite();
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

    void handleTrustAcceptance(const std::string& client_ip) {
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
        bool success = queue_->createTrustAcceptance(clientId);

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", success} }.dump();
        response_.prepare_payload();
    }

    void handleOneCSync(const std::string& client_ip) {
        bool success = onec_->syncData();
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", success} }.dump();
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
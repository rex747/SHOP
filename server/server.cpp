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
#include "config_server.h"
#include "database.h"
#include "auth_service.h"
#include "queue_service.h"
#include "onec_integration.h"
#include "rate_limiter.h"

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
    ssl::stream<tcp::socket> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;

    // Внедренные зависимости (вместо глобальных переменных)
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
                if (ec) return;
                self->doRead();
            });
    }

private:
    void doRead() {
        http::async_read(stream_, buffer_, request_,
            [self = shared_from_this()](beast::error_code ec, std::size_t) {
                if (ec) return;
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

        response_.version(request_.version());
        response_.keep_alive(false);

        try {
            if (target == "/api/v1/clients/register" && method == http::verb::post) {
                handleClientRegister(client_ip);
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
                response_.result(http::status::not_found);
                response_.set(http::field::content_type, "application/json");
                response_.body() = json{ {"error", "Not found"} }.dump();
                response_.prepare_payload();
            }
        }
        catch (const exception& e) {
            response_.result(http::status::internal_server_error);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", e.what()} }.dump();
            response_.prepare_payload();
        }

        doWrite();
    }

    void handleClientRegister(const std::string& client_ip) {
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

        if (!body.contains("phone") || !body["phone"].is_string() ||
            !body.contains("last_name") || !body["last_name"].is_string() ||
            !body.contains("first_name") || !body["first_name"].is_string()) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Missing required fields"} }.dump();
            response_.prepare_payload();
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
            return;
        }
        phone = "+7" + digits.substr(1);

        bool ok = db_->registerClient(phone, last_name, first_name, middle_name, email, items_submitted, items_sold);

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", ok}, {"phone", phone} }.dump();
        response_.prepare_payload();
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
        }
        else {
            response_.result(http::status::unauthorized);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"error", "Invalid TOTP code or replay detected"} }.dump();
        }
        response_.prepare_payload();
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
                if (ec) return;
                self->onComplete();
            });
    }

    void onComplete() {
        stream_.async_shutdown(
            [self = shared_from_this()](beast::error_code) {
                beast::error_code ec2;
                self->stream_.lowest_layer().shutdown(tcp::socket::shutdown_send, ec2);
            });
    }
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;

    // Внедренные зависимости
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
                    // Исправление: передача всех зависимостей в HttpSession
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

        ssl::context ctx{ ssl::context::tlsv12_server };
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::single_dh_use);

        ctx.use_certificate_chain_file(Config::SSL_CERT_PATH);
        ctx.use_private_key_file(Config::SSL_KEY_PATH, ssl::context::pem);

        // Исправление: передача всех зависимостей в HttpListener
        auto listener = std::make_shared<HttpListener>(ioc, ctx, tcp::endpoint{ address, port }, db, auth, queue, onec, rate_limiter);
        listener->run();
        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Server startup error: " << e.what() << std::endl;
    }
}

int main() {
    try {
        std::string connStr = "host=" + std::to_string(Config::DB_PORT) + " port=" + std::to_string(Config::DB_PORT) +
            " dbname=" + Config::DB_NAME + " user=" + Config::DB_USER +
            " password=" + Config::DB_PASSWORD;

        // 1. Создаем подключение к БД
        auto conn = std::make_shared<pqxx::connection>(connStr);
        if (!conn->is_open()) {
            std::cerr << "Database connection failed" << std::endl;
            return 1;
        }

        // 2. Внедрение зависимостей (Dependency Injection): создание экземпляров сервисов
        auto db = std::make_shared<Database>(conn, Config::ENCRYPTION_KEY);
        auto auth = std::make_shared<AuthService>(db);
        auto queue = std::make_shared<QueueService>(db);
        auto onec = std::make_shared<OneCIntegration>(db);
        auto rate_limiter = std::make_shared<RateLimiter>(5, std::chrono::minutes(1)); // 5 попыток в минуту на IP

        // 3. Инициализация
        if (!auth->initialize()) {
            std::cerr << "Auth service initialization failed" << std::endl;
            return 1;
        }

        std::cout << "Server starting on port " << Config::SERVER_PORT << std::endl;

        // 4. Запуск сервера с передачей зависимостей
        runServer(db, auth, queue, onec, rate_limiter);

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
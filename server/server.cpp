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
#include "logger_server.h"
#include "monitoring.h"
#include "backup_service.h"

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

Logger g_serverLogger("/var/log/kiosk/server.log");
pqxx::connection* g_dbConnection = nullptr;
AuthService g_authService;
QueueService g_queueService;
OneCIntegration g_onec;
Monitoring g_monitoring;
BackupService g_backup;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
    ssl::stream<tcp::socket> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    http::response<http::string_body> response_;

public:
    explicit HttpSession(tcp::socket&& socket, ssl::context& ctx)
        : stream_(std::move(socket), ctx) {
    }

    void run() {
        stream_.async_handshake(ssl::stream_base::server,
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) return self->onFail(ec, "handshake");
                self->doRead();
            });
    }

private:
    void doRead() {
        http::async_read(stream_, buffer_, request_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) return self->onFail(ec, "read");
                self->handleRequest();
            });
    }

    void handleRequest() {
        g_serverLogger.info("Received request: " + std::string(request_.method_string()) + " " + std::string(request_.target()));
        response_.version(request_.version());
        response_.keep_alive(false);

        auto target = request_.target();
        auto method = request_.method();
        g_serverLogger.info("Routing request: " + std::string(request_.method_string()) + " " + std::string(target));

        try {
            if (target == "/api/v1/clients/register" && method == http::verb::post) {
                handleClientRegister();
            }
            else if (target == "/api/v1/auth/sms" && method == http::verb::post) {
                handleAuthSMS();
            }
            else if (target == "/api/v1/auth/verify" && method == http::verb::post) {
                handleAuthVerify();
            }
            else if (target == "/api/v1/queue/get_ticket" && method == http::verb::post) {
                handleGetTicket();
            }
            else if (target == "/api/v1/queue/trust_acceptance" && method == http::verb::post) {
                handleTrustAcceptance();
            }
            else if (target == "/api/v1/onec/sync" && method == http::verb::post) {
                handleOneCSync();
            }
            else {
                response_.result(http::status::not_found);
                response_.set(http::field::content_type, "application/json");
                json error = { {"error", "Not found"} };
                response_.body() = error.dump();
                response_.prepare_payload();
            }
        }
        catch (const exception& e) {
            g_serverLogger.error(std::string("Request handling error: ") + e.what());
            response_.result(http::status::internal_server_error);
            json error = { {"error", e.what()} };
            response_.body() = error.dump();
            response_.prepare_payload();
        }

        doWrite();
    }

    void handleClientRegister()
    {
        g_serverLogger.info("Request ClientRegister: " + std::string(request_.method_string()) + " " + std::string(request_.target()));
        json body;
        try {
            body = json::parse(request_.body());
            g_serverLogger.info("Parsed JSON body: " + body.dump());
        }
        catch (const json::exception& e) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Invalid JSON"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error(std::string("JSON parse error: ") + e.what());
            return;
        }

        if (!body.contains("phone") || !body["phone"].is_string() ||
            !body.contains("last_name") || !body["last_name"].is_string() ||
            !body.contains("first_name") || !body["first_name"].is_string())
        {
            g_serverLogger.warning("Missing required fields in registration request: " + body.dump());
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Missing required fields"} }.dump();
            response_.prepare_payload();
            return;
        }

        std::string phone = body["phone"].get<std::string>();
        std::string last_name = body["last_name"].get<std::string>();
        std::string first_name = body["first_name"].get<std::string>();
        std::string middle_name = body.value("middle_name", std::string{});
        std::string email = body.value("email", std::string{});
        g_serverLogger.info("Registration data: phone=" + phone + ", last_name=" + last_name + ", first_name=" + first_name + ", middle_name=" + middle_name + ", email=" + email);

        int items_submitted = body.value("items_submitted", 0);
        int items_sold = body.value("items_sold", 0);

        std::string digits;
        for (char c : phone) if (c >= '0' && c <= '9') digits += c;
        if (digits.length() != 11 || (digits[0] != '7' && digits[0] != '8')) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Invalid phone number"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("Invalid phone number in registration request: " + phone);
            return;
        }
        phone = "+7" + digits.substr(1);

        if (last_name.empty() || last_name.length() > 64 ||
            first_name.empty() || first_name.length() > 64) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Name length violation"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("Name length violation in registration request: last_name=" + last_name + ", first_name=" + first_name);
            return;
        }
        if (!middle_name.empty() && middle_name.length() > 64) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Middle name too long"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("Middle name too long in registration request: middle_name=" + middle_name);
            return;
        }
        if (!email.empty() && email.length() > 64) {
            response_.result(http::status::bad_request);
            response_.set(http::field::content_type, "application/json");
            response_.body() = json{ {"success", false}, {"error", "Email too long"} }.dump();
            response_.prepare_payload();
            g_serverLogger.error("Email too long in registration request: email=" + email);
            return;
        }

        bool ok = Database::registerClient(phone, last_name, first_name, middle_name, email, items_submitted, items_sold);
        g_serverLogger.info("Client registration result for phone " + phone + ": " + (ok ? "success" : "failure"));

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = json{ {"success", ok}, {"phone", phone} }.dump();
        response_.prepare_payload();
        g_serverLogger.info("Response sent for registration request: " + response_.body());
    }

    void handleAuthSMS() {
        g_serverLogger.info("Request: " + std::string(request_.method_string()) + " " + std::string(request_.target()));
        json body = json::parse(request_.body());
        std::string phone = body["phone"];
        g_serverLogger.info("Parsed JSON body: " + body.dump());

        std::string code = generateSMSCode();
        bool sent = sendSMS(phone, code);
        g_serverLogger.info("SMS code sent to phone " + phone + ": " + (sent ? "success" : "failure"));
        json resp;
        resp["success"] = sent;
        if (sent) {
            g_authService.storeSMSCode(phone, code);
            g_serverLogger.info("SMS code stored for phone " + phone);
        }

        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        response_.body() = resp.dump();
        response_.prepare_payload();
        g_serverLogger.info("Response sent for SMS request: " + response_.body());
    }

    void handleAuthVerify() {
        json body = json::parse(request_.body());
        std::string phone = body["phone"];
        std::string code = body["sms_code"];

        if (g_authService.verifySMSCode(phone, code)) {
            auto tokens = g_authService.generateTokens(phone);
            json resp;
            resp["access_token"] = tokens.first;
            resp["refresh_token"] = tokens.second;
            resp["success"] = true;
            response_.result(http::status::ok);
            response_.set(http::field::content_type, "application/json");
            response_.body() = resp.dump();
        }
        else {
            response_.result(http::status::unauthorized);
            json error = { {"error", "Invalid code"} };
            response_.body() = error.dump();
        }
        response_.prepare_payload();
    }

    void handleGetTicket() {
        json body = json::parse(request_.body());
        int clientId = body["client_id"];
        std::string queueType = body["queue_type"];
        int itemsCount = body["items_count"];

        auto ticket = g_queueService.getTicket(clientId, queueType, itemsCount);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        json resp = { {"ticket", ticket} };
        response_.body() = resp.dump();
        response_.prepare_payload();
    }

    void handleTrustAcceptance() {
        json body = json::parse(request_.body());
        int clientId = body["client_id"];
        bool success = g_queueService.createTrustAcceptance(clientId);
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        json resp = { {"success", success} };
        response_.body() = resp.dump();
        response_.prepare_payload();
    }

    void handleOneCSync() {
        bool success = g_onec.syncData();
        response_.result(http::status::ok);
        response_.set(http::field::content_type, "application/json");
        json resp = { {"success", success} };
        response_.body() = resp.dump();
        response_.prepare_payload();
    }

    void doWrite() {
        g_serverLogger.info("Sending response: " + response_.body());
        http::async_write(stream_, response_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) return self->onFail(ec, "write");
                self->onComplete();
            });
    }

    void onComplete() {
        g_serverLogger.info("Request completed successfully");
        stream_.async_shutdown(
            [self = shared_from_this()](beast::error_code ec) {
                if (ec) {
                    g_serverLogger.error("SSL shutdown: " + ec.message());
                }
                else {
                    g_serverLogger.info("SSL shutdown completed successfully");
                }
                beast::error_code ec2;
                self->stream_.lowest_layer().shutdown(tcp::socket::shutdown_send, ec2);
                g_serverLogger.info("TCP socket shutdown completed");
            });
    }

    void onFail(beast::error_code ec, std::string const& what) {
        g_serverLogger.error(what + ": " + ec.message());
    }

    std::string generateSMSCode() {
        g_serverLogger.info("Generating SMS code");
        std::string code = to_string(100000 + (rand() % 900000));
        g_serverLogger.info("Generated SMS code: " + code);
        return code;
    }

    bool sendSMS(const std::string& phone, const std::string& code) {
        g_serverLogger.info("SMS to " + phone + ": " + code);
        return true;
    }
};

class HttpListener : public std::enable_shared_from_this<HttpListener> {
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;

public:
    HttpListener(net::io_context& ioc, ssl::context& ctx, tcp::endpoint endpoint)
        : ioc_(ioc), ctx_(ctx), acceptor_(ioc, endpoint) {
    }

    void run() {
        doAccept();
    }

private:
    void doAccept() {
        acceptor_.async_accept(
            [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
                if (ec) {
                    g_serverLogger.error("accept: " + ec.message());
                }
                else {
                    std::make_shared<HttpSession>(std::move(socket), self->ctx_)->run();
                }
                self->doAccept();
            });
    }
};

void runServer() {
    try {
        g_serverLogger.info("Starting server on port " + std::to_string(Config::SERVER_PORT));
        auto const address = net::ip::make_address("0.0.0.0");
        unsigned short port = static_cast<unsigned short>(Config::SERVER_PORT);
        net::io_context ioc{ 1 };

        ssl::context ctx{ ssl::context::tlsv12_server };
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::single_dh_use);
        g_serverLogger.info("SSL context initialized with TLSv1.2 server settings");
        try {
            ctx.use_certificate_chain_file(Config::SSL_CERT_PATH);
            ctx.use_private_key_file(Config::SSL_KEY_PATH, ssl::context::pem);
            g_serverLogger.info("SSL certificate and private key loaded successfully");
        }
        catch (const std::exception& e) {
            g_serverLogger.critical(std::string("SSL setup error: ") + e.what());
            return;
        }

        auto listener = std::make_shared<HttpListener>(ioc, ctx, tcp::endpoint{ address, port });
        listener->run();
        g_serverLogger.info("Server started on port " + to_string(port));

        ioc.run();
        g_serverLogger.info("I/O service stopped, server shutting down");
    }
    catch (const std::exception& e) {
        g_serverLogger.critical(std::string("Server startup error: ") + e.what());
    }
}

int main(int argc, char* argv[]) {
    try {
        g_serverLogger.info("=== Server starting ===");

        // ИСПРАВЛЕНИЕ: Использование std::ostringstream для безопасной конкатенации.
        // Это полностью исключает ошибки арифметики указателей (E2140) при сложении 
        // строковых литералов с переменными конфигурации любого типа.
        std::ostringstream connStrStream;
        connStrStream << "host=" << Config::DB_HOST
            << " port=" << Config::DB_PORT
            << " dbname=" << Config::DB_NAME
            << " user=" << Config::DB_USER
            << " password=" << Config::DB_PASSWORD;
        std::string connStr = connStrStream.str();

        g_serverLogger.info("Connecting to database with connection string: " + connStr);

        g_dbConnection = new pqxx::connection(connStr);
        if (!g_dbConnection->is_open()) {
            g_serverLogger.critical("Database connection failed");
            return 1;
        }

        g_serverLogger.info("Database connected");

        if (!g_authService.initialize()) {
            g_serverLogger.critical("Auth service initialization failed");
            return 1;
        }

        if (!g_queueService.initialize()) {
            g_serverLogger.critical("Queue service initialization failed");
            return 1;
        }

        if (!g_onec.initialize()) {
            g_serverLogger.error("1C integration initialization failed");
        }

        g_monitoring.start();
        g_backup.start();

        runServer();

        g_monitoring.stop();
        g_backup.stop();

        delete g_dbConnection;

        g_serverLogger.info("=== Server shutdown ===");
        return 0;
    }
    catch (const std::exception& e) {
        g_serverLogger.critical(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
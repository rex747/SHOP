// monitoring.h
#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct Metrics {
    std::atomic<int64_t> totalRequests{ 0 };
    std::atomic<int64_t> successfulRequests{ 0 };
    std::atomic<int64_t> failedRequests{ 0 };
    std::atomic<int64_t> activeConnections{ 0 };
    std::atomic<int64_t> totalTicketsIssued{ 0 };
    std::atomic<int64_t> totalClientsRegistered{ 0 };
    std::map<std::string, std::atomic<int64_t>> queueLengths;
    std::chrono::steady_clock::time_point startTime;

    Metrics() : startTime(std::chrono::steady_clock::now()) {}
};

class Monitoring {
private:
    net::io_context m_ioc;
    tcp::acceptor m_acceptor;
    Metrics m_metrics;
    std::thread m_thread;
    bool m_running;

    std::string generateMetricsPage() {
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_metrics.startTime).count();

        std::stringstream ss;
        ss << "# HELP kiosk_uptime_seconds Server uptime in seconds\n";
        ss << "# TYPE kiosk_uptime_seconds gauge\n";
        ss << "kiosk_uptime_seconds " << uptime << "\n\n";

        ss << "# HELP kiosk_requests_total Total number of requests\n";
        ss << "# TYPE kiosk_requests_total counter\n";
        ss << "kiosk_requests_total " << m_metrics.totalRequests.load() << "\n\n";

        ss << "# HELP kiosk_requests_success Successful requests\n";
        ss << "# TYPE kiosk_requests_success counter\n";
        ss << "kiosk_requests_success " << m_metrics.successfulRequests.load() << "\n\n";

        ss << "# HELP kiosk_requests_failed Failed requests\n";
        ss << "# TYPE kiosk_requests_failed counter\n";
        ss << "kiosk_requests_failed " << m_metrics.failedRequests.load() << "\n\n";

        ss << "# HELP kiosk_active_connections Active connections\n";
        ss << "# TYPE kiosk_active_connections gauge\n";
        ss << "kiosk_active_connections " << m_metrics.activeConnections.load() << "\n\n";

        ss << "# HELP kiosk_tickets_issued_total Total tickets issued\n";
        ss << "# TYPE kiosk_tickets_issued_total counter\n";
        ss << "kiosk_tickets_issued_total " << m_metrics.totalTicketsIssued.load() << "\n\n";

        ss << "# HELP kiosk_clients_registered_total Total clients registered\n";
        ss << "# TYPE kiosk_clients_registered_total counter\n";
        ss << "kiosk_clients_registered_total " << m_metrics.totalClientsRegistered.load() << "\n";

        return ss.str();
    }

    void handleSession(tcp::socket socket) {
        m_metrics.activeConnections++;

        try {
            beast::flat_buffer buffer;
            http::request<http::string_body> req;

            http::read(socket, buffer, req);

            http::response<http::string_body> res;
            res.version(req.version());
            res.keep_alive(false);

            if (req.target() == "/metrics") {
                res.result(http::status::ok);
                res.set(http::field::content_type, "text/plain; version=0.0.4");
                res.body() = generateMetricsPage();
            }
            else if (req.target() == "/health") {
                res.result(http::status::ok);
                res.set(http::field::content_type, "application/json");
                res.body() = "{\"status\":\"healthy\"}";
            }
            else {
                res.result(http::status::not_found);
                res.body() = "{\"error\":\"Not found\"}";
            }

            res.prepare_payload();
            http::write(socket, res);

            m_metrics.successfulRequests++;

        }
        catch (const std::exception& e) {
            g_serverLogger.error(std::string("Monitoring session error: ") + e.what());
            m_metrics.failedRequests++;
        }

        m_metrics.activeConnections--;
    }

    void run() {
        while (m_running) {
            try {
                tcp::socket socket(m_ioc);
                m_acceptor.accept(socket);

                std::thread([this, socket = std::move(socket)]() mutable {
                    handleSession(std::move(socket));
                    }).detach();

            }
            catch (const std::exception& e) {
                if (m_running) {
                    g_serverLogger.error(std::string("Monitoring accept error: ") + e.what());
                }
            }
        }
    }

public:
    Monitoring()
        : m_acceptor(m_ioc, tcp::endpoint(tcp::v4(), Config::MONITORING_PORT))
        , m_running(false) {
    }

    void start() {
        m_running = true;
        m_thread = std::thread([this]() { run(); });
        g_serverLogger.info("Monitoring started on port " +
            std::to_string(Config::MONITORING_PORT));
    }

    void stop() {
        m_running = false;
        m_acceptor.close();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    Metrics& getMetrics() { return m_metrics; }

    void recordRequest() { m_metrics.totalRequests++; }
    void recordTicketIssued() { m_metrics.totalTicketsIssued++; }
    void recordClientRegistered() { m_metrics.totalClientsRegistered++; }
};

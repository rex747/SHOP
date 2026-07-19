// config_server.h
#pragma once

#include <string>
#include <filesystem>

namespace Config {
    // Server settings
    constexpr int SERVER_PORT = 8080;
    constexpr int SERVER_WORKER_THREADS = 4;

    // Database
    constexpr const char* DB_HOST = "127.0.0.1"; // <-- Áûכמ "localhost"
    constexpr int DB_PORT = 5432;
    constexpr const char* DB_NAME = "kiosk_db";
    constexpr const char* DB_USER = "kiosk_user";
    constexpr const char* DB_PASSWORD = "123";
    constexpr int DB_POOL_SIZE = 10;

    // 1C Integration
    constexpr const char* ONEC_URL = "http://1c-server:1550/hs/exchange";
    constexpr const char* ONEC_USER = "1c_user";
    constexpr const char* ONEC_PASSWORD = "1c_password";
    constexpr int ONEC_SYNC_INTERVAL_MINUTES = 15;

    // SSL
    constexpr const char* SSL_CERT_PATH = "/etc/ssl/certs/kiosk.crt";
    constexpr const char* SSL_KEY_PATH = "/etc/ssl/private/kiosk.key";

    // JWT
    constexpr const char* JWT_SECRET = "your-super-secret-jwt-key-change-in-production";
    constexpr int JWT_ACCESS_EXPIRY_SECONDS = 3600;
    constexpr int JWT_REFRESH_EXPIRY_SECONDS = 86400 * 7;

    // SMS Provider
    constexpr const char* SMS_API_URL = "https://sms-provider.com/api";
    constexpr const char* SMS_API_KEY = "your-sms-api-key";

    // Logging
    constexpr const char* LOG_PATH = "/var/log/kiosk";
    constexpr int LOG_MAX_SIZE_MB = 100;
    constexpr int LOG_MAX_FILES = 10;

    // Backup
    constexpr const char* BACKUP_PATH = "/var/backups/kiosk";
    constexpr int BACKUP_INTERVAL_HOURS = 24;
    constexpr int BACKUP_RETENTION_DAYS = 30;

    // Monitoring
    constexpr int MONITORING_PORT = 9090;
    constexpr const char* METRICS_PATH = "/metrics";

    // Queue settings
    constexpr int MAX_QUEUE_SIZE_GENERAL = 100;
    constexpr int MAX_QUEUE_SIZE_FIRST_TIME = 20;
    constexpr int MAX_QUEUE_SIZE_EXTRA_20 = 50;
    constexpr int MAX_QUEUE_SIZE_PAID = 30;
    constexpr int MAX_QUEUE_SIZE_EXPENSIVE = 20;

    // Paths
    inline void createDirectories() {
        std::filesystem::create_directories(LOG_PATH);
        std::filesystem::create_directories(BACKUP_PATH);
    }
}

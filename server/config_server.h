// config_server.h
#pragma once

#include <string>
#include <filesystem>

namespace Config {
    // Server settings
    inline const int SERVER_PORT = 8443;
    constexpr int SERVER_WORKER_THREADS = 4;

    // Database
    constexpr const char* DB_HOST = "127.0.0.1"; // <-- Ѕыло "localhost"
    inline const int DB_PORT = 5432;
    constexpr const char* DB_NAME = "kiosk_db";
    constexpr const char* DB_USER = "kiosk_user";
    constexpr const char* DB_PASSWORD = "123";
    constexpr int DB_POOL_SIZE = 10;

    // 1C Integration
    constexpr const char* ONEC_URL = "http://100.108.154.106/Rozniza_base/hs/kiosk_sync";
    constexpr const char* ONEC_USER = "onec_sync";
    constexpr const char* ONEC_PASSWORD = "123456";
    constexpr int ONEC_SYNC_INTERVAL_MINUTES = 15;

    // SSL
    constexpr const char* SSL_CERT_PATH = "/etc/ssl/certs/kiosk.crt";
    constexpr const char* SSL_KEY_PATH = "/etc/ssl/private/kiosk.key";

    // JWT
    constexpr const char* JWT_SECRET = "your-super-secret-jwt-key-change-in-production";
    constexpr int JWT_ACCESS_EXPIRY_SECONDS = 3600;
    constexpr int JWT_REFRESH_EXPIRY_SECONDS = 86400 * 7;

    //  люч шифровани€ (будет хэширован SHA-256 дл€ гарантии длины 32 байта)
    inline const std::string ENCRYPTION_KEY = "123";

    // ============================================================
    // OAuth 2.0 дл€ яндекс.ѕочты
    // ============================================================
    inline std::string OAUTH_ACCESS_TOKEN = "y0__wgBEKDHtP4IGJGeRiD-1fu4GDCnx7T-CDa5kEWiDpM8K9FdQmn4oIVzUcIq";
    inline std::string OAUTH_REFRESH_TOKEN = "2:AAA:AAAAAI_NI6A:1:-8VDe9YI7tk1ac_W:7VuW...";

    // ѕароль приложени€ Shop812 яндекс почты
    inline const std::string YANDEX_APP_PASSWORD = "pnwynckeepbkwxck";

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

    // =========================================================================
    // »Ќ“≈√–ј÷»я Ё ¬ј…–»Ќ√ј ¬“Ѕ
    // =========================================================================
    // Ѕазовый URL API ¬“Ѕ (тестовый контур)
    inline const std::string VTB_API_URL = "https://test-pay.vtb.ru/payment/rest/";
    // ”четные данные API (выдаютс€ банком)
    inline const std::string VTB_USERNAME = "vtb_api_user";
    inline const std::string VTB_PASSWORD = "vtb_api_password";
    // URL дл€ возврата после успешной/неуспешной оплаты (используютс€ дл€ веб-форм)
    inline const std::string VTB_RETURN_URL = "https://77.222.32.209:8443/acquiring/success";
    inline const std::string VTB_FAIL_URL = "https://77.222.32.209:8443/acquiring/fail";

    // =========================================================================
    //  ќЌ‘»√”–ј÷»я —≈“» ћј√ј«»Ќј ƒЋя “¬-ƒ»—ѕЋ≈я
    // =========================================================================
    // ¬нешний статичный IP-адрес магазина (через NAT)
    // VPS использует этот IP дл€ идентификации подключений из магазина
    inline const std::string SHOP_EXTERNAL_IP = "188.227.49.137";

    // ¬нутренний IP-адрес Android TV в локальной сети магазина
    // VPS провер€ет этот IP при подключении к /queue-display
    // и логирует подключение как "TV connected"
    inline const std::string TV_INTERNAL_IP = "192.168.3.62";


    // Paths
    inline void createDirectories() {
        std::filesystem::create_directories(LOG_PATH);
        std::filesystem::create_directories(BACKUP_PATH);
    }


}

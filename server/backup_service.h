// backup_service.h
#pragma once

#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <sstream>      // std::stringstream
#include <iomanip>      // std::put_time
#include <ctime>        // std::localtime, std::strftime, gmtime_r
#include <vector>       // std::vector
#include <algorithm>

#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

class BackupService {
private:
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::chrono::hours m_interval;

    void run() {
        std::string lastBackupDate; // "YYYY-MM-DD" в МСК

        while (m_running) {
            try {
                // --- 1. Текущее время по Москве (UTC+3) ---
                auto now = std::chrono::system_clock::now();
                std::time_t nowUtc = std::chrono::system_clock::to_time_t(now);
                std::time_t nowMsk = nowUtc + 3 * 3600;

                std::tm tmMsk{};
#ifdef _WIN32
                gmtime_s(&tmMsk, &nowMsk);
#else
                gmtime_r(&nowMsk, &tmMsk);
#endif

                char dateBuf[11] = { 0 };
                std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmMsk);
                const std::string todayMsk(dateBuf);

                // --- 2. Окно 22:00–22:59:59 МСК, ещё не делали сегодня ---
                if (tmMsk.tm_hour == 22 && lastBackupDate != todayMsk) {
                    g_serverLogger.info(
                        "BackupService: 22:00 MSK window reached, starting daily backup");

                    // Защита от повторного запуска в том же окне
                    // (например, если сервер перезапускали в 22:00–23:00).
                    bool alreadyDone = false;
                    try {
                        char dateNoSep[9] = { 0 };
                        std::strftime(dateNoSep, sizeof(dateNoSep), "%Y%m%d", &tmMsk);
                        const std::string prefix =
                            std::string("kiosk_backup_") + dateNoSep + "_";
                        std::error_code ec;
                        for (const auto& entry :
                            std::filesystem::directory_iterator(Config::BACKUP_PATH, ec)) {
                            if (ec) break;
                            const std::string name = entry.path().filename().string();
                            if (name.rfind(prefix, 0) == 0 &&
                                entry.path().extension() == ".gz") {
                                alreadyDone = true;
                                break;
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        g_serverLogger.warning(
                            std::string("BackupService: cannot scan BACKUP_PATH: ") +
                            e.what());
                    }

                    if (!alreadyDone) {
                        try {
                            performBackup();
                            g_serverLogger.info(
                                "BackupService: daily backup completed for " + todayMsk);
                        }
                        catch (const std::exception& e) {
                            g_serverLogger.error(
                                std::string("BackupService: performBackup failed: ") +
                                e.what());
                        }
                    }
                    else {
                        g_serverLogger.info(
                            "BackupService: backup for " + todayMsk +
                            " already exists, skipping");
                    }

                    // Фиксируем дату в любом случае, чтобы не спамить лог каждую минуту
                    lastBackupDate = todayMsk;
                }
            }
            catch (const std::exception& e) {
                g_serverLogger.error(
                    std::string("BackupService: scheduler error: ") + e.what());
            }

            // --- 3. Спим по 1 секунде (быстрый отклик на stop()) ---
            for (int i = 0; i < 60 && m_running; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    void performBackup() {
        g_serverLogger.info("Starting database backup");

        namespace fs = std::filesystem;

        // Create backup directory if not exists
        fs::create_directories(Config::BACKUP_PATH);

        // backup at 22:00 by MSK
        auto now = std::chrono::system_clock::now();
        std::time_t time_t_now = std::chrono::system_clock::to_time_t(now);
        std::time_t time_t_msk = time_t_now + 3 * 3600;

        std::tm tm_msk{};
#ifdef _WIN32
        gmtime_s(&tm_msk, &time_t_msk);
#else
        gmtime_r(&time_t_msk, &tm_msk);
#endif

        char tsBuf[32] = { 0 };
        std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", &tm_msk);

        std::string backupFile = std::string(Config::BACKUP_PATH) +
            "/kiosk_backup_" + tsBuf + ".sql";

        // Run pg_dump
        std::string cmd = "PGPASSWORD=" + std::string(Config::DB_PASSWORD) +
            " pg_dump -h " + std::string(Config::DB_HOST) +
            " -p " + std::to_string(Config::DB_PORT) +
            " -U " + std::string(Config::DB_USER) +
            " " + std::string(Config::DB_NAME) +
            " > " + backupFile;

        int result = std::system(cmd.c_str());

        if (result == 0) {
            g_serverLogger.info("Backup completed: " + backupFile);

            // Compress backup
            std::string compressCmd = "gzip " + backupFile;
            int sys_ret = std::system(compressCmd.c_str());
            if (sys_ret != 0) {
                g_serverLogger.warning("Backup compression command failed with code: " + std::to_string(sys_ret));
            }

            // Cleanup old backups
            cleanupOldBackups();

        }
        else {
            g_serverLogger.error("Backup failed with code: " + std::to_string(result));
        }
    }

    void cleanupOldBackups() {
        namespace fs = std::filesystem;

        std::vector<fs::directory_entry> backups;
        fs::path backupDir(Config::BACKUP_PATH);

        if (!fs::exists(backupDir)) return;

        for (const auto& entry : fs::directory_iterator(backupDir)) {
            if (entry.path().extension() == ".gz") {
                backups.push_back(entry);
            }
        }

        // Sort by modification time
        std::sort(backups.begin(), backups.end(),
            [](const auto& a, const auto& b) {
                return fs::last_write_time(a) > fs::last_write_time(b);
            });

        // Remove backups older than retention period
        auto now = std::chrono::system_clock::now();
        auto retentionDays = Config::BACKUP_RETENTION_DAYS;

        for (const auto& backup : backups) {
            auto mtime = fs::last_write_time(backup);
            auto mtime_sys = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
            auto diff = std::chrono::system_clock::now() - mtime_sys;
            auto days = std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24;

            if (days > retentionDays) {
                fs::remove(backup.path());
                g_serverLogger.info("Removed old backup: " + backup.path().string());
            }
        }
    }

public:
    BackupService()
        : m_running(false)
        , m_interval(Config::BACKUP_INTERVAL_HOURS) {
    }
    ~BackupService() {
        stop();
    }

    void start() {
        m_running = true;
        m_thread = std::thread([this]() { run(); });
        g_serverLogger.info("Backup service started");
    }

    void stop() {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        g_serverLogger.info("Backup service stopped");
    }

    // Manual backup trigger
    void triggerBackup() {
        performBackup();
    }
};

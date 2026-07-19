// backup_service.h
#pragma once

#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include "config_server.h"
#include "logger_server.h"

extern Logger g_serverLogger;

class BackupService {
private:
    std::thread m_thread;
    bool m_running;
    std::chrono::hours m_interval;

    void run() {
        while (m_running) {
            try {
                performBackup();
            }
            catch (const std::exception& e) {
                g_serverLogger.error(std::string("Backup error: ") + e.what());
            }

            // Sleep until next backup
            for (int i = 0; i < static_cast<int>(m_interval.count()) * 3600 && m_running; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    void performBackup() {
        g_serverLogger.info("Starting database backup");

        namespace fs = std::filesystem;

        // Create backup directory if not exists
        fs::create_directories(Config::BACKUP_PATH);

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream timestamp;
        timestamp << std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S");

        std::string backupFile = std::string(Config::BACKUP_PATH) +
            "/kiosk_backup_" + timestamp.str() + ".sql";

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

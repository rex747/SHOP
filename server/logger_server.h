// logger_server.h
#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

#ifdef ERROR
#undef ERROR
#endif
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
private:
    std::string m_logFile;
    std::mutex m_mutex;
    LogLevel m_minLevel;
    std::ofstream m_file;

    std::string getLogLevelString(LogLevel level) {
        switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
        }
    }

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    void rotateLogsIfNeeded() {
        namespace fs = std::filesystem;
        fs::path logPath(m_logFile);

        if (fs::exists(logPath)) {
            auto size = fs::file_size(logPath);
            if (size > static_cast<std::uintmax_t>(Config::LOG_MAX_SIZE_MB) * 1024ULL * 1024ULL) {
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::system_clock::to_time_t(now);
                std::stringstream backupName;
                // Исправлена ошибка E1776 (wchar_t* -> string)
                backupName << logPath.parent_path().string() << "/"
                    << logPath.stem().string() << "_"
                    << timestamp << ".log";
                fs::rename(logPath, backupName.str());
                cleanupOldLogs();
            }
        }
    }

    void cleanupOldLogs() {
        namespace fs = std::filesystem;
        fs::path logDir = fs::path(m_logFile).parent_path();

        std::vector<fs::directory_entry> logs;
        for (const auto& entry : fs::directory_iterator(logDir)) {
            if (entry.path().extension() == ".log") {
                logs.push_back(entry);
            }
        }

        // Sort by modification time
        std::sort(logs.begin(), logs.end(),
            [](const auto& a, const auto& b) {
                return fs::last_write_time(a) > fs::last_write_time(b);
            });

        // Remove old logs
        int maxFiles = Config::LOG_MAX_FILES;
        for (size_t i = maxFiles; i < logs.size(); i++) {
            fs::remove(logs[i].path());
        }
    }

public:
    Logger(const std::string& logFile, LogLevel minLevel = LogLevel::INFO)
        : m_logFile(logFile), m_minLevel(minLevel) {
        // Ensure directory exists
        std::filesystem::create_directories(std::filesystem::path(logFile).parent_path());

        m_file.open(logFile, std::ios::app);
        if (!m_file.is_open()) {
            std::cerr << "Failed to open log file: " << logFile << std::endl;
        }
    }

    ~Logger() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

    void log(LogLevel level, const std::string& message) {
        if (level < m_minLevel) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        rotateLogsIfNeeded();

        std::string timestamp = getCurrentTimestamp();
        std::string levelStr = getLogLevelString(level);

        std::string logLine = "[" + timestamp + "] [" + levelStr + "] " + message + "\n";

        if (m_file.is_open()) {
            m_file << logLine;
            m_file.flush();
        }

        // Also output to stderr for ERROR and CRITICAL
        if (level >= LogLevel::ERROR) {
            std::cerr << logLine;
        }

        // Output to stdout for INFO and above in production
        if (level >= LogLevel::INFO) {
            std::cout << logLine;
        }
    }

    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, msg); }
    void warning(const std::string& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void critical(const std::string& msg) { log(LogLevel::CRITICAL, msg); }
};

extern Logger g_serverLogger;
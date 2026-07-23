// logger.h
#pragma once

#include<Windows.h>
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
    std::wstring m_logFile;
    std::mutex m_mutex;
    LogLevel m_minLevel;

    std::wstring getLogLevelString(LogLevel level) const {
        switch (level) {
        case LogLevel::DEBUG: return L"DEBUG";
        case LogLevel::INFO: return L"INFO";
        case LogLevel::WARNING: return L"WARNING";
        case LogLevel::ERROR: return L"ERROR";
        case LogLevel::CRITICAL: return L"CRITICAL";
        default: return L"UNKNOWN";
        }
    }

    std::wstring getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::wstringstream ss;

        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        ss << std::put_time(&timeinfo, L"%Y-%m-%d %H:%M:%S");

        ss << L"." << std::setfill(L'0') << std::setw(3) << ms.count();
        return ss.str();
    }

    void rotateLogsIfNeeded() {
        namespace fs = std::filesystem;
        fs::path logPath(m_logFile);
        std::error_code ec;

        // ѕровер€ем существование без исключений
        if (fs::exists(logPath, ec)) {
            // ≈сли ошибка при проверке Ц пропускаем ротацию
            if (ec) return;

            auto size = fs::file_size(logPath, ec);
            if (ec) return; // ошибка Ц пропускаем

            if (size > 10 * 1024 * 1024) {
                auto now = std::chrono::system_clock::now();
                auto timestamp = std::chrono::system_clock::to_time_t(now);
                std::wstringstream backupName;
                backupName << logPath.parent_path() << L"\\"
                    << logPath.stem().c_str() << L"_"
                    << timestamp << L".log";
                fs::rename(logPath, backupName.str(), ec);
                // ќшибка переименовани€ игнорируетс€ Ц лог останетс€ старым
            }
        }
    }

public:
    Logger(const std::wstring& logFile, LogLevel minLevel = LogLevel::INFO)
        : m_logFile(logFile), m_minLevel(minLevel) {
        // Ensure directory exists Ц не бросаем исключени€
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(logFile).parent_path(), ec);
        // ec игнорируем Ц если не удалось создать, запись в файл просто не сработает
    }

    void log(LogLevel level, const std::wstring& message) {
        if (level < m_minLevel) return;

        std::lock_guard<std::mutex> lock(m_mutex);
        rotateLogsIfNeeded();

        std::wofstream file(m_logFile, std::ios::app);
        if (file.is_open()) {
            file << L"[" << getCurrentTimestamp() << L"] "
                << L"[" << getLogLevelString(level) << L"] "
                << message << std::endl;
        }

        // Also output to debug console
#ifdef _DEBUG
        OutputDebugStringW((getCurrentTimestamp() + L" " +
            getLogLevelString(level) + L" " +
            message + L"\n").c_str());
#endif
    }

    void debug(const std::wstring& msg) { log(LogLevel::DEBUG, msg); }
    void info(const std::wstring& msg) { log(LogLevel::INFO, msg); }
    void warning(const std::wstring& msg) { log(LogLevel::WARNING, msg); }
    void error(const std::wstring& msg) { log(LogLevel::ERROR, msg); }
    void critical(const std::wstring& msg) { log(LogLevel::CRITICAL, msg); }
};

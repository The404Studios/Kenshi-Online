#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <windows.h>

namespace ReKenshi {
namespace Logging {

/**
 * Log levels
 */
enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5,
    None = 6
};

/**
 * Log output targets
 */
enum class LogOutput {
    None = 0,
    DebugString = 1 << 0,      // OutputDebugString
    File = 1 << 1,              // File output
    Console = 1 << 2,           // Console window
    All = DebugString | File | Console
};

inline LogOutput operator|(LogOutput a, LogOutput b) {
    return static_cast<LogOutput>(static_cast<int>(a) | static_cast<int>(b));
}

inline LogOutput operator&(LogOutput a, LogOutput b) {
    return static_cast<LogOutput>(static_cast<int>(a) & static_cast<int>(b));
}

/**
 * Thread-safe logger singleton
 */
class Logger {
public:
    static Logger& GetInstance();

    // Configuration
    void SetLogLevel(LogLevel level);
    void SetOutputTargets(LogOutput targets);
    void SetLogFile(const std::string& filePath);
    void EnableTimestamps(bool enable);
    void EnableThreadIds(bool enable);

    // Logging methods
    void Trace(const std::string& message, const char* file = nullptr, int line = 0);
    void Debug(const std::string& message, const char* file = nullptr, int line = 0);
    void Info(const std::string& message, const char* file = nullptr, int line = 0);
    void Warning(const std::string& message, const char* file = nullptr, int line = 0);
    void Error(const std::string& message, const char* file = nullptr, int line = 0);
    void Critical(const std::string& message, const char* file = nullptr, int line = 0);

    // Formatted logging
    template<typename... Args>
    void TraceF(const char* format, Args... args) {
        LogFormatted(LogLevel::Trace, format, args...);
    }

    template<typename... Args>
    void DebugF(const char* format, Args... args) {
        LogFormatted(LogLevel::Debug, format, args...);
    }

    template<typename... Args>
    void InfoF(const char* format, Args... args) {
        LogFormatted(LogLevel::Info, format, args...);
    }

    template<typename... Args>
    void WarningF(const char* format, Args... args) {
        LogFormatted(LogLevel::Warning, format, args...);
    }

    template<typename... Args>
    void ErrorF(const char* format, Args... args) {
        LogFormatted(LogLevel::Error, format, args...);
    }

    template<typename... Args>
    void CriticalF(const char* format, Args... args) {
        LogFormatted(LogLevel::Critical, format, args...);
    }

    // Utility methods
    void Flush();
    void Clear();
    std::string GetLogFilePath() const { return m_logFilePath; }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Log(LogLevel level, const std::string& message, const char* file, int line);

    template<typename... Args>
    void LogFormatted(LogLevel level, const char* format, Args... args) {
        char buffer[2048];
        snprintf(buffer, sizeof(buffer), format, args...);
        Log(level, std::string(buffer), nullptr, 0);
    }

    std::string FormatMessage(LogLevel level, const std::string& message, const char* file, int line);
    std::string GetTimestamp();
    std::string GetLevelString(LogLevel level);
    void WriteToFile(const std::string& message);
    void WriteToDebugString(const std::string& message);
    void WriteToConsole(const std::string& message);

    LogLevel m_currentLevel;
    LogOutput m_outputTargets;
    std::string m_logFilePath;
    std::ofstream m_logFile;
    std::mutex m_mutex;
    bool m_enableTimestamps;
    bool m_enableThreadIds;
};

/**
 * Scoped logger for tracking function entry/exit
 */
class ScopedLogger {
public:
    ScopedLogger(const std::string& functionName, const char* file = nullptr, int line = 0)
        : m_functionName(functionName)
        , m_file(file)
        , m_line(line)
        , m_startTime(std::chrono::high_resolution_clock::now())
    {
        std::ostringstream msg;
        msg << "ENTER " << m_functionName;
        Logger::GetInstance().Trace(msg.str(), file, line);
    }

    ~ScopedLogger() {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(endTime - m_startTime).count();

        std::ostringstream msg;
        msg << "EXIT  " << m_functionName << " (took " << duration << " ms)";
        Logger::GetInstance().Trace(msg.str(), m_file, m_line);
    }

private:
    std::string m_functionName;
    const char* m_file;
    int m_line;
    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Logging
} // namespace ReKenshi

// Convenience macros
#define LOG_TRACE(msg) ReKenshi::Logging::Logger::GetInstance().Trace(msg, __FILE__, __LINE__)
#define LOG_DEBUG(msg) ReKenshi::Logging::Logger::GetInstance().Debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg) ReKenshi::Logging::Logger::GetInstance().Info(msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) ReKenshi::Logging::Logger::GetInstance().Warning(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) ReKenshi::Logging::Logger::GetInstance().Error(msg, __FILE__, __LINE__)
#define LOG_CRITICAL(msg) ReKenshi::Logging::Logger::GetInstance().Critical(msg, __FILE__, __LINE__)

#define LOG_TRACE_F(...) ReKenshi::Logging::Logger::GetInstance().TraceF(__VA_ARGS__)
#define LOG_DEBUG_F(...) ReKenshi::Logging::Logger::GetInstance().DebugF(__VA_ARGS__)
#define LOG_INFO_F(...) ReKenshi::Logging::Logger::GetInstance().InfoF(__VA_ARGS__)
#define LOG_WARNING_F(...) ReKenshi::Logging::Logger::GetInstance().WarningF(__VA_ARGS__)
#define LOG_ERROR_F(...) ReKenshi::Logging::Logger::GetInstance().ErrorF(__VA_ARGS__)
#define LOG_CRITICAL_F(...) ReKenshi::Logging::Logger::GetInstance().CriticalF(__VA_ARGS__)

#define LOG_FUNCTION() ReKenshi::Logging::ScopedLogger _scopedLogger_##__LINE__(__FUNCTION__, __FILE__, __LINE__)

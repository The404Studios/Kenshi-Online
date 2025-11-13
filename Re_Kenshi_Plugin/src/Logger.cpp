#include "Logger.h"
#include <iomanip>
#include <ctime>

namespace ReKenshi {
namespace Logging {

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_currentLevel(LogLevel::Info)
    , m_outputTargets(LogOutput::DebugString)
    , m_logFilePath("re_kenshi.log")
    , m_enableTimestamps(true)
    , m_enableThreadIds(false)
{
}

Logger::~Logger() {
    Flush();
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

void Logger::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentLevel = level;
}

void Logger::SetOutputTargets(LogOutput targets) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputTargets = targets;
}

void Logger::SetLogFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Close existing file
    if (m_logFile.is_open()) {
        m_logFile.close();
    }

    m_logFilePath = filePath;

    // Open new file
    if ((m_outputTargets & LogOutput::File) != LogOutput::None) {
        m_logFile.open(m_logFilePath, std::ios::out | std::ios::app);
    }
}

void Logger::EnableTimestamps(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enableTimestamps = enable;
}

void Logger::EnableThreadIds(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enableThreadIds = enable;
}

void Logger::Trace(const std::string& message, const char* file, int line) {
    Log(LogLevel::Trace, message, file, line);
}

void Logger::Debug(const std::string& message, const char* file, int line) {
    Log(LogLevel::Debug, message, file, line);
}

void Logger::Info(const std::string& message, const char* file, int line) {
    Log(LogLevel::Info, message, file, line);
}

void Logger::Warning(const std::string& message, const char* file, int line) {
    Log(LogLevel::Warning, message, file, line);
}

void Logger::Error(const std::string& message, const char* file, int line) {
    Log(LogLevel::Error, message, file, line);
}

void Logger::Critical(const std::string& message, const char* file, int line) {
    Log(LogLevel::Critical, message, file, line);
}

void Logger::Log(LogLevel level, const std::string& message, const char* file, int line) {
    // Check if we should log this level
    if (level < m_currentLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string formattedMessage = FormatMessage(level, message, file, line);

    // Output to debug string
    if ((m_outputTargets & LogOutput::DebugString) != LogOutput::None) {
        WriteToDebugString(formattedMessage);
    }

    // Output to file
    if ((m_outputTargets & LogOutput::File) != LogOutput::None) {
        WriteToFile(formattedMessage);
    }

    // Output to console
    if ((m_outputTargets & LogOutput::Console) != LogOutput::None) {
        WriteToConsole(formattedMessage);
    }
}

std::string Logger::FormatMessage(LogLevel level, const std::string& message, const char* file, int line) {
    std::ostringstream oss;

    // Timestamp
    if (m_enableTimestamps) {
        oss << "[" << GetTimestamp() << "] ";
    }

    // Thread ID
    if (m_enableThreadIds) {
        oss << "[Thread:" << GetCurrentThreadId() << "] ";
    }

    // Log level
    oss << "[" << GetLevelString(level) << "] ";

    // Message
    oss << message;

    // File and line (if provided)
    if (file != nullptr && line > 0) {
        // Extract just the filename from the full path
        const char* filename = file;
        const char* lastSlash = strrchr(file, '\\');
        if (lastSlash) {
            filename = lastSlash + 1;
        }
        oss << " (" << filename << ":" << line << ")";
    }

    return oss.str();
}

std::string Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << now_ms.count();

    return oss.str();
}

std::string Logger::GetLevelString(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:    return "TRACE";
    case LogLevel::Debug:    return "DEBUG";
    case LogLevel::Info:     return "INFO ";
    case LogLevel::Warning:  return "WARN ";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "CRIT ";
    default:                 return "UNKN ";
    }
}

void Logger::WriteToFile(const std::string& message) {
    if (!m_logFile.is_open()) {
        m_logFile.open(m_logFilePath, std::ios::out | std::ios::app);
    }

    if (m_logFile.is_open()) {
        m_logFile << message << std::endl;
    }
}

void Logger::WriteToDebugString(const std::string& message) {
    std::string outputMessage = message + "\n";
    OutputDebugStringA(outputMessage.c_str());
}

void Logger::WriteToConsole(const std::string& message) {
    // Allocate console if not already present
    static bool consoleAllocated = false;
    if (!consoleAllocated) {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        consoleAllocated = true;
    }

    std::cout << message << std::endl;
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_logFile.is_open()) {
        m_logFile.flush();
    }
}

void Logger::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_logFile.is_open()) {
        m_logFile.close();
    }

    // Reopen file in truncate mode
    m_logFile.open(m_logFilePath, std::ios::out | std::ios::trunc);

    if (m_logFile.is_open()) {
        m_logFile.close();
        m_logFile.open(m_logFilePath, std::ios::out | std::ios::app);
    }
}

} // namespace Logging
} // namespace ReKenshi

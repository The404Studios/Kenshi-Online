/**
 * Logger System Example for Re_Kenshi Plugin
 *
 * This file demonstrates the comprehensive logging system.
 */

#include "../include/Logger.h"
#include <thread>
#include <chrono>

using namespace ReKenshi::Logging;

//=============================================================================
// Example 1: Basic Logging
//=============================================================================

void Example_BasicLogging() {
    auto& logger = Logger::GetInstance();

    // Log at different levels
    logger.Info("Application started");
    logger.Debug("Debug information here");
    logger.Warning("This is a warning message");
    logger.Error("An error occurred");
    logger.Critical("Critical system failure!");

    // Trace messages (typically for very detailed debugging)
    logger.Trace("Entering function");
    logger.Trace("Exiting function");
}

//=============================================================================
// Example 2: Formatted Logging
//=============================================================================

void Example_FormattedLogging() {
    auto& logger = Logger::GetInstance();

    int playerCount = 42;
    float fps = 60.5f;
    const char* mapName = "The Great Desert";

    // Printf-style formatting
    logger.InfoF("Server started with %d players", playerCount);
    logger.DebugF("Current FPS: %.2f", fps);
    logger.InfoF("Loading map: %s", mapName);

    // Complex formatting
    logger.InfoF("Player %s (ID: %d) joined at position (%.1f, %.1f, %.1f)",
                 "John", 12345, 100.5f, 50.2f, 30.8f);
}

//=============================================================================
// Example 3: Log Levels and Filtering
//=============================================================================

void Example_LogLevels() {
    auto& logger = Logger::GetInstance();

    // Set minimum log level to Warning (will ignore Debug and Trace)
    logger.SetLogLevel(LogLevel::Warning);

    logger.Trace("This won't appear");      // Filtered out
    logger.Debug("This won't appear");      // Filtered out
    logger.Info("This won't appear");       // Filtered out
    logger.Warning("This will appear");     // Shown
    logger.Error("This will appear");       // Shown
    logger.Critical("This will appear");    // Shown

    // Reset to Info level
    logger.SetLogLevel(LogLevel::Info);
}

//=============================================================================
// Example 4: Output Targets
//=============================================================================

void Example_OutputTargets() {
    auto& logger = Logger::GetInstance();

    // Output to debug string only (default)
    logger.SetOutputTargets(LogOutput::DebugString);
    logger.Info("This goes to OutputDebugString");

    // Output to file only
    logger.SetLogFile("my_game.log");
    logger.SetOutputTargets(LogOutput::File);
    logger.Info("This goes to file");

    // Output to console (allocates console window)
    logger.SetOutputTargets(LogOutput::Console);
    logger.Info("This goes to console window");

    // Output to all targets
    logger.SetOutputTargets(LogOutput::All);
    logger.Info("This goes everywhere!");

    // Output to multiple specific targets
    logger.SetOutputTargets(LogOutput::DebugString | LogOutput::File);
    logger.Info("This goes to debug string and file");
}

//=============================================================================
// Example 5: Convenience Macros
//=============================================================================

void Example_Macros() {
    // These macros automatically include file name and line number

    LOG_TRACE("Entering critical section");
    LOG_DEBUG("Variable value: 42");
    LOG_INFO("System initialized successfully");
    LOG_WARNING("Low memory warning");
    LOG_ERROR("Failed to load configuration");
    LOG_CRITICAL("Unrecoverable error detected");

    // Formatted versions
    int value = 100;
    LOG_INFO_F("Processing %d items", value);
    LOG_ERROR_F("Failed to open file: %s", "config.json");
}

//=============================================================================
// Example 6: Scoped Logging (Function Tracking)
//=============================================================================

void ExpensiveOperation() {
    LOG_FUNCTION();  // Automatically logs entry and exit with duration

    // Simulate work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    LOG_INFO("Performing expensive operation...");

    // More work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // ScopedLogger will log exit when function returns
}

void Example_ScopedLogging() {
    LOG_INFO("Starting scoped logging example");

    ExpensiveOperation();

    LOG_INFO("Scoped logging example complete");
}

//=============================================================================
// Example 7: Multi-threaded Logging
//=============================================================================

void ThreadWorker(int threadId) {
    auto& logger = Logger::GetInstance();
    logger.EnableThreadIds(true);

    for (int i = 0; i < 5; i++) {
        LOG_INFO_F("Thread %d: Processing item %d", threadId, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Example_MultiThreaded() {
    auto& logger = Logger::GetInstance();

    LOG_INFO("Starting multi-threaded logging example");

    // Create multiple threads
    std::thread t1(ThreadWorker, 1);
    std::thread t2(ThreadWorker, 2);
    std::thread t3(ThreadWorker, 3);

    t1.join();
    t2.join();
    t3.join();

    LOG_INFO("Multi-threaded logging example complete");

    logger.EnableThreadIds(false);
}

//=============================================================================
// Example 8: Timestamps
//=============================================================================

void Example_Timestamps() {
    auto& logger = Logger::GetInstance();

    // Enable timestamps (enabled by default)
    logger.EnableTimestamps(true);
    LOG_INFO("This message has a timestamp");

    // Disable timestamps
    logger.EnableTimestamps(false);
    LOG_INFO("This message has no timestamp");

    // Re-enable for consistency
    logger.EnableTimestamps(true);
}

//=============================================================================
// Example 9: Game System Logger Class
//=============================================================================

class GameSystem {
public:
    GameSystem(const std::string& name) : m_name(name) {
        LOG_INFO_F("GameSystem '%s' created", m_name.c_str());
    }

    ~GameSystem() {
        LOG_INFO_F("GameSystem '%s' destroyed", m_name.c_str());
    }

    void Initialize() {
        LOG_FUNCTION();

        LOG_INFO_F("[%s] Initializing...", m_name.c_str());

        // Simulate initialization
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        LOG_INFO_F("[%s] Initialized successfully", m_name.c_str());
    }

    void Update(float deltaTime) {
        LOG_TRACE_F("[%s] Update (dt: %.3f)", m_name.c_str(), deltaTime);

        // System update logic
        if (deltaTime > 0.1f) {
            LOG_WARNING_F("[%s] High delta time detected: %.3f", m_name.c_str(), deltaTime);
        }
    }

    void ProcessEvent(const std::string& eventType) {
        LOG_DEBUG_F("[%s] Processing event: %s", m_name.c_str(), eventType.c_str());

        // Event handling logic
        if (eventType == "error") {
            LOG_ERROR_F("[%s] Error event received", m_name.c_str());
        }
    }

private:
    std::string m_name;
};

void Example_GameSystemLogger() {
    LOG_INFO("=== Game System Logger Example ===");

    {
        GameSystem physics("Physics");
        physics.Initialize();

        for (int frame = 0; frame < 5; frame++) {
            physics.Update(0.016f);  // 60 FPS
        }

        physics.ProcessEvent("collision");
        physics.ProcessEvent("error");
    }

    LOG_INFO("=== Game System Logger Example Complete ===");
}

//=============================================================================
// Example 10: Error Handling with Logging
//=============================================================================

bool LoadConfiguration(const std::string& filename) {
    LOG_FUNCTION();

    LOG_INFO_F("Loading configuration from: %s", filename.c_str());

    // Simulate file operation
    bool success = (filename == "valid_config.json");

    if (!success) {
        LOG_ERROR_F("Failed to load configuration: File not found: %s", filename.c_str());
        return false;
    }

    LOG_INFO("Configuration loaded successfully");
    return true;
}

void Example_ErrorHandling() {
    LOG_INFO("=== Error Handling Example ===");

    // Successful operation
    if (LoadConfiguration("valid_config.json")) {
        LOG_INFO("System configured successfully");
    } else {
        LOG_CRITICAL("Failed to configure system");
    }

    // Failed operation
    if (!LoadConfiguration("missing_config.json")) {
        LOG_WARNING("Using default configuration");
    }

    LOG_INFO("=== Error Handling Example Complete ===");
}

//=============================================================================
// Example 11: Log File Management
//=============================================================================

void Example_LogFileManagement() {
    auto& logger = Logger::GetInstance();

    // Set custom log file
    logger.SetLogFile("game_session_2024.log");
    logger.SetOutputTargets(LogOutput::File);

    LOG_INFO("Session started");
    LOG_INFO("Player connected");
    LOG_INFO("World loaded");

    // Flush to ensure everything is written
    logger.Flush();

    LOG_INFO("Session data saved");

    // Clear log file (start fresh)
    logger.Clear();
    LOG_INFO("Log cleared - new session started");

    // Get current log file path
    std::string logPath = logger.GetLogFilePath();
    LOG_INFO_F("Logging to: %s", logPath.c_str());
}

//=============================================================================
// Example 12: Complete Logging Setup
//=============================================================================

void Example_CompleteSetup() {
    auto& logger = Logger::GetInstance();

    // Configure logger for production use
    logger.SetLogLevel(LogLevel::Info);              // Don't show debug/trace in production
    logger.SetOutputTargets(LogOutput::File | LogOutput::DebugString);
    logger.SetLogFile("re_kenshi_production.log");
    logger.EnableTimestamps(true);
    logger.EnableThreadIds(true);

    LOG_INFO("===================================");
    LOG_INFO("Re_Kenshi Plugin - Production Mode");
    LOG_INFO("===================================");

    // Simulate plugin lifecycle
    LOG_INFO("Initializing plugin...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("Plugin initialized");

    LOG_INFO("Scanning for game structures...");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    LOG_INFO("Found 15 game structures");

    LOG_INFO("Connecting to IPC server...");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    LOG_INFO("Connected to IPC server");

    LOG_INFO("Setting up event hooks...");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    LOG_INFO("Event hooks installed");

    LOG_INFO("Plugin ready!");

    // Flush all logs
    logger.Flush();
}

//=============================================================================
// Main Example Runner
//=============================================================================

int main() {
    auto& logger = Logger::GetInstance();

    // Default setup
    logger.SetLogLevel(LogLevel::Trace);
    logger.SetOutputTargets(LogOutput::Console);
    logger.EnableTimestamps(true);

    std::cout << "========== Re_Kenshi Logger Examples ==========\n\n";

    Example_BasicLogging();
    std::cout << "\n";

    Example_FormattedLogging();
    std::cout << "\n";

    Example_LogLevels();
    std::cout << "\n";

    Example_OutputTargets();
    std::cout << "\n";

    Example_Macros();
    std::cout << "\n";

    Example_ScopedLogging();
    std::cout << "\n";

    Example_MultiThreaded();
    std::cout << "\n";

    Example_Timestamps();
    std::cout << "\n";

    Example_GameSystemLogger();
    std::cout << "\n";

    Example_ErrorHandling();
    std::cout << "\n";

    Example_LogFileManagement();
    std::cout << "\n";

    Example_CompleteSetup();
    std::cout << "\n";

    std::cout << "========================================\n";

    return 0;
}

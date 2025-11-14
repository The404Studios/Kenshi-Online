#pragma once

#include <string>
#include <cstdint>

namespace ReKenshi {
namespace Config {

/**
 * IPC Configuration
 */
struct IPCConfig {
    std::string pipeName = "ReKenshi_IPC";
    uint32_t connectTimeout = 5000;        // ms
    uint32_t readTimeout = 1000;           // ms
    uint32_t writeTimeout = 1000;          // ms
    uint32_t reconnectInterval = 5000;     // ms
    bool autoReconnect = true;
    uint32_t maxMessageSize = 1048576;     // 1 MB
};

/**
 * Multiplayer Synchronization Configuration
 */
struct MultiplayerConfig {
    float syncRate = 10.0f;                // Hz (updates per second)
    float positionThreshold = 0.5f;        // units
    float healthThreshold = 1.0f;          // HP
    float rotationThreshold = 0.1f;        // radians
    bool syncPosition = true;
    bool syncRotation = true;
    bool syncHealth = true;
    bool syncInventory = false;
    bool syncStats = false;
    uint32_t maxPlayers = 64;
};

/**
 * Event System Configuration
 */
struct EventConfig {
    float pollRate = 10.0f;                // Hz (polls per second)
    bool trackCharacterEvents = true;
    bool trackWorldEvents = true;
    bool trackCombatEvents = true;
    bool trackInventoryEvents = false;
    bool trackQuestEvents = false;
    uint32_t maxEventHistory = 100;
};

/**
 * Performance Profiling Configuration
 */
struct PerformanceConfig {
    bool enabled = false;
    bool trackFrameTime = true;
    bool trackMemory = true;
    bool trackCPU = false;
    uint32_t reportInterval = 60;         // seconds
    bool printToDebugOutput = true;
    bool saveToFile = false;
    std::string reportFilePath = "performance_report.txt";
};

/**
 * Input Handler Configuration
 */
struct InputConfig {
    uint32_t toggleOverlayKey = 0x70;     // F1 key (VK_F1)
    uint32_t toggleDebugKey = 0x71;       // F2 key (VK_F2)
    bool enableHotkeys = true;
    bool blockGameInput = false;          // Block input to game when overlay is open
};

/**
 * Rendering Configuration
 */
struct RenderingConfig {
    bool useD3D11Hook = true;
    bool useOGREOverlay = false;          // Fallback if D3D11 fails
    bool useImGui = true;
    float overlayScale = 1.0f;
    uint32_t overlayWidth = 800;
    uint32_t overlayHeight = 600;
    bool vsync = true;
};

/**
 * Debug Configuration
 */
struct DebugConfig {
    enum class LogLevel {
        None = 0,
        Error = 1,
        Warning = 2,
        Info = 3,
        Debug = 4,
        Trace = 5
    };

    LogLevel logLevel = LogLevel::Info;
    bool outputToDebugString = true;
    bool outputToFile = false;
    std::string logFilePath = "re_kenshi.log";
    bool printDiagnostics = false;
    uint32_t diagnosticInterval = 10;     // seconds
};

/**
 * Memory Scanner Configuration
 */
struct MemoryScannerConfig {
    bool enablePatternCache = true;
    bool validateAddresses = true;
    uint32_t scanTimeout = 5000;          // ms
    bool rescanOnFailure = true;
};

/**
 * Main Configuration Class
 */
class Configuration {
public:
    static Configuration& GetInstance();

    // Load/Save
    bool LoadFromFile(const std::string& filePath = "re_kenshi_config.json");
    bool SaveToFile(const std::string& filePath = "re_kenshi_config.json") const;
    void LoadDefaults();

    // Config access
    IPCConfig& IPC() { return m_ipcConfig; }
    const IPCConfig& IPC() const { return m_ipcConfig; }

    MultiplayerConfig& Multiplayer() { return m_multiplayerConfig; }
    const MultiplayerConfig& Multiplayer() const { return m_multiplayerConfig; }

    EventConfig& Events() { return m_eventConfig; }
    const EventConfig& Events() const { return m_eventConfig; }

    PerformanceConfig& Performance() { return m_performanceConfig; }
    const PerformanceConfig& Performance() const { return m_performanceConfig; }

    InputConfig& Input() { return m_inputConfig; }
    const InputConfig& Input() const { return m_inputConfig; }

    RenderingConfig& Rendering() { return m_renderingConfig; }
    const RenderingConfig& Rendering() const { return m_renderingConfig; }

    DebugConfig& Debug() { return m_debugConfig; }
    const DebugConfig& Debug() const { return m_debugConfig; }

    MemoryScannerConfig& MemoryScanner() { return m_memoryScannerConfig; }
    const MemoryScannerConfig& MemoryScanner() const { return m_memoryScannerConfig; }

    // Validation
    bool Validate() const;
    std::string GetValidationErrors() const;

private:
    Configuration() { LoadDefaults(); }
    ~Configuration() = default;
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;

    // Helper functions
    bool ParseJSON(const std::string& jsonContent);
    std::string SerializeToJSON() const;

    // Config sections
    IPCConfig m_ipcConfig;
    MultiplayerConfig m_multiplayerConfig;
    EventConfig m_eventConfig;
    PerformanceConfig m_performanceConfig;
    InputConfig m_inputConfig;
    RenderingConfig m_renderingConfig;
    DebugConfig m_debugConfig;
    MemoryScannerConfig m_memoryScannerConfig;
};

/**
 * RAII Configuration Loader
 * Automatically loads config on construction, saves on destruction
 */
class ConfigurationGuard {
public:
    explicit ConfigurationGuard(const std::string& filePath = "re_kenshi_config.json")
        : m_filePath(filePath)
    {
        Configuration::GetInstance().LoadFromFile(m_filePath);
    }

    ~ConfigurationGuard() {
        Configuration::GetInstance().SaveToFile(m_filePath);
    }

private:
    std::string m_filePath;
};

} // namespace Config
} // namespace ReKenshi

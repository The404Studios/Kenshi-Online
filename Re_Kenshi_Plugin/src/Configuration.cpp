#include "Configuration.h"
#include <fstream>
#include <sstream>
#include <windows.h>

namespace ReKenshi {
namespace Config {

Configuration& Configuration::GetInstance() {
    static Configuration instance;
    return instance;
}

void Configuration::LoadDefaults() {
    // Defaults are already set in the struct definitions
    OutputDebugStringA("[Configuration] Loaded default configuration\n");
}

bool Configuration::LoadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::ostringstream log;
        log << "[Configuration] Failed to open config file: " << filePath << ", using defaults\n";
        OutputDebugStringA(log.str().c_str());
        LoadDefaults();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    if (!ParseJSON(content)) {
        std::ostringstream log;
        log << "[Configuration] Failed to parse config file: " << filePath << ", using defaults\n";
        OutputDebugStringA(log.str().c_str());
        LoadDefaults();
        return false;
    }

    if (!Validate()) {
        std::ostringstream log;
        log << "[Configuration] Config validation failed: " << GetValidationErrors() << "\n";
        OutputDebugStringA(log.str().c_str());
        return false;
    }

    std::ostringstream log;
    log << "[Configuration] Successfully loaded config from: " << filePath << "\n";
    OutputDebugStringA(log.str().c_str());
    return true;
}

bool Configuration::SaveToFile(const std::string& filePath) const {
    std::string json = SerializeToJSON();

    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::ostringstream log;
        log << "[Configuration] Failed to save config file: " << filePath << "\n";
        OutputDebugStringA(log.str().c_str());
        return false;
    }

    file << json;
    file.close();

    std::ostringstream log;
    log << "[Configuration] Successfully saved config to: " << filePath << "\n";
    OutputDebugStringA(log.str().c_str());
    return true;
}

bool Configuration::ParseJSON(const std::string& jsonContent) {
    // Simple JSON parser (for production, use RapidJSON or similar)
    // This is a simplified implementation for demonstration

    // Parse IPC settings
    if (jsonContent.find("\"pipeName\"") != std::string::npos) {
        size_t start = jsonContent.find("\"pipeName\"");
        start = jsonContent.find("\"", start + 10);
        size_t end = jsonContent.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            m_ipcConfig.pipeName = jsonContent.substr(start + 1, end - start - 1);
        }
    }

    // Parse multiplayer settings
    if (jsonContent.find("\"syncRate\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"syncRate\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            m_multiplayerConfig.syncRate = std::stof(jsonContent.substr(pos + 1));
        }
    }

    if (jsonContent.find("\"positionThreshold\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"positionThreshold\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            m_multiplayerConfig.positionThreshold = std::stof(jsonContent.substr(pos + 1));
        }
    }

    // Parse event settings
    if (jsonContent.find("\"pollRate\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"pollRate\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            m_eventConfig.pollRate = std::stof(jsonContent.substr(pos + 1));
        }
    }

    // Parse performance settings
    if (jsonContent.find("\"performanceEnabled\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"performanceEnabled\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            std::string value = jsonContent.substr(pos + 1, 4);
            m_performanceConfig.enabled = (value.find("true") != std::string::npos);
        }
    }

    // Parse input settings
    if (jsonContent.find("\"toggleOverlayKey\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"toggleOverlayKey\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            m_inputConfig.toggleOverlayKey = std::stoul(jsonContent.substr(pos + 1), nullptr, 16);
        }
    }

    // Parse debug settings
    if (jsonContent.find("\"logLevel\"") != std::string::npos) {
        size_t pos = jsonContent.find("\"logLevel\"");
        pos = jsonContent.find(":", pos);
        if (pos != std::string::npos) {
            int level = std::stoi(jsonContent.substr(pos + 1));
            m_debugConfig.logLevel = static_cast<DebugConfig::LogLevel>(level);
        }
    }

    OutputDebugStringA("[Configuration] Parsed JSON configuration\n");
    return true;
}

std::string Configuration::SerializeToJSON() const {
    std::ostringstream json;

    json << "{\n";

    // IPC Config
    json << "  \"ipc\": {\n";
    json << "    \"pipeName\": \"" << m_ipcConfig.pipeName << "\",\n";
    json << "    \"connectTimeout\": " << m_ipcConfig.connectTimeout << ",\n";
    json << "    \"readTimeout\": " << m_ipcConfig.readTimeout << ",\n";
    json << "    \"writeTimeout\": " << m_ipcConfig.writeTimeout << ",\n";
    json << "    \"reconnectInterval\": " << m_ipcConfig.reconnectInterval << ",\n";
    json << "    \"autoReconnect\": " << (m_ipcConfig.autoReconnect ? "true" : "false") << ",\n";
    json << "    \"maxMessageSize\": " << m_ipcConfig.maxMessageSize << "\n";
    json << "  },\n";

    // Multiplayer Config
    json << "  \"multiplayer\": {\n";
    json << "    \"syncRate\": " << m_multiplayerConfig.syncRate << ",\n";
    json << "    \"positionThreshold\": " << m_multiplayerConfig.positionThreshold << ",\n";
    json << "    \"healthThreshold\": " << m_multiplayerConfig.healthThreshold << ",\n";
    json << "    \"rotationThreshold\": " << m_multiplayerConfig.rotationThreshold << ",\n";
    json << "    \"syncPosition\": " << (m_multiplayerConfig.syncPosition ? "true" : "false") << ",\n";
    json << "    \"syncRotation\": " << (m_multiplayerConfig.syncRotation ? "true" : "false") << ",\n";
    json << "    \"syncHealth\": " << (m_multiplayerConfig.syncHealth ? "true" : "false") << ",\n";
    json << "    \"syncInventory\": " << (m_multiplayerConfig.syncInventory ? "true" : "false") << ",\n";
    json << "    \"syncStats\": " << (m_multiplayerConfig.syncStats ? "true" : "false") << ",\n";
    json << "    \"maxPlayers\": " << m_multiplayerConfig.maxPlayers << "\n";
    json << "  },\n";

    // Event Config
    json << "  \"events\": {\n";
    json << "    \"pollRate\": " << m_eventConfig.pollRate << ",\n";
    json << "    \"trackCharacterEvents\": " << (m_eventConfig.trackCharacterEvents ? "true" : "false") << ",\n";
    json << "    \"trackWorldEvents\": " << (m_eventConfig.trackWorldEvents ? "true" : "false") << ",\n";
    json << "    \"trackCombatEvents\": " << (m_eventConfig.trackCombatEvents ? "true" : "false") << ",\n";
    json << "    \"trackInventoryEvents\": " << (m_eventConfig.trackInventoryEvents ? "true" : "false") << ",\n";
    json << "    \"trackQuestEvents\": " << (m_eventConfig.trackQuestEvents ? "true" : "false") << ",\n";
    json << "    \"maxEventHistory\": " << m_eventConfig.maxEventHistory << "\n";
    json << "  },\n";

    // Performance Config
    json << "  \"performance\": {\n";
    json << "    \"enabled\": " << (m_performanceConfig.enabled ? "true" : "false") << ",\n";
    json << "    \"trackFrameTime\": " << (m_performanceConfig.trackFrameTime ? "true" : "false") << ",\n";
    json << "    \"trackMemory\": " << (m_performanceConfig.trackMemory ? "true" : "false") << ",\n";
    json << "    \"trackCPU\": " << (m_performanceConfig.trackCPU ? "true" : "false") << ",\n";
    json << "    \"reportInterval\": " << m_performanceConfig.reportInterval << ",\n";
    json << "    \"printToDebugOutput\": " << (m_performanceConfig.printToDebugOutput ? "true" : "false") << ",\n";
    json << "    \"saveToFile\": " << (m_performanceConfig.saveToFile ? "true" : "false") << ",\n";
    json << "    \"reportFilePath\": \"" << m_performanceConfig.reportFilePath << "\"\n";
    json << "  },\n";

    // Input Config
    json << "  \"input\": {\n";
    json << "    \"toggleOverlayKey\": \"0x" << std::hex << m_inputConfig.toggleOverlayKey << std::dec << "\",\n";
    json << "    \"toggleDebugKey\": \"0x" << std::hex << m_inputConfig.toggleDebugKey << std::dec << "\",\n";
    json << "    \"enableHotkeys\": " << (m_inputConfig.enableHotkeys ? "true" : "false") << ",\n";
    json << "    \"blockGameInput\": " << (m_inputConfig.blockGameInput ? "true" : "false") << "\n";
    json << "  },\n";

    // Rendering Config
    json << "  \"rendering\": {\n";
    json << "    \"useD3D11Hook\": " << (m_renderingConfig.useD3D11Hook ? "true" : "false") << ",\n";
    json << "    \"useOGREOverlay\": " << (m_renderingConfig.useOGREOverlay ? "true" : "false") << ",\n";
    json << "    \"useImGui\": " << (m_renderingConfig.useImGui ? "true" : "false") << ",\n";
    json << "    \"overlayScale\": " << m_renderingConfig.overlayScale << ",\n";
    json << "    \"overlayWidth\": " << m_renderingConfig.overlayWidth << ",\n";
    json << "    \"overlayHeight\": " << m_renderingConfig.overlayHeight << ",\n";
    json << "    \"vsync\": " << (m_renderingConfig.vsync ? "true" : "false") << "\n";
    json << "  },\n";

    // Debug Config
    json << "  \"debug\": {\n";
    json << "    \"logLevel\": " << static_cast<int>(m_debugConfig.logLevel) << ",\n";
    json << "    \"outputToDebugString\": " << (m_debugConfig.outputToDebugString ? "true" : "false") << ",\n";
    json << "    \"outputToFile\": " << (m_debugConfig.outputToFile ? "true" : "false") << ",\n";
    json << "    \"logFilePath\": \"" << m_debugConfig.logFilePath << "\",\n";
    json << "    \"printDiagnostics\": " << (m_debugConfig.printDiagnostics ? "true" : "false") << ",\n";
    json << "    \"diagnosticInterval\": " << m_debugConfig.diagnosticInterval << "\n";
    json << "  },\n";

    // Memory Scanner Config
    json << "  \"memoryScanner\": {\n";
    json << "    \"enablePatternCache\": " << (m_memoryScannerConfig.enablePatternCache ? "true" : "false") << ",\n";
    json << "    \"validateAddresses\": " << (m_memoryScannerConfig.validateAddresses ? "true" : "false") << ",\n";
    json << "    \"scanTimeout\": " << m_memoryScannerConfig.scanTimeout << ",\n";
    json << "    \"rescanOnFailure\": " << (m_memoryScannerConfig.rescanOnFailure ? "true" : "false") << "\n";
    json << "  }\n";

    json << "}\n";

    return json.str();
}

bool Configuration::Validate() const {
    // Validate IPC settings
    if (m_ipcConfig.pipeName.empty()) {
        return false;
    }
    if (m_ipcConfig.connectTimeout == 0 || m_ipcConfig.connectTimeout > 60000) {
        return false;
    }

    // Validate multiplayer settings
    if (m_multiplayerConfig.syncRate <= 0.0f || m_multiplayerConfig.syncRate > 120.0f) {
        return false;
    }
    if (m_multiplayerConfig.maxPlayers == 0 || m_multiplayerConfig.maxPlayers > 256) {
        return false;
    }

    // Validate event settings
    if (m_eventConfig.pollRate <= 0.0f || m_eventConfig.pollRate > 120.0f) {
        return false;
    }

    // Validate performance settings
    if (m_performanceConfig.reportInterval == 0) {
        return false;
    }

    // Validate rendering settings
    if (m_renderingConfig.overlayWidth == 0 || m_renderingConfig.overlayHeight == 0) {
        return false;
    }

    return true;
}

std::string Configuration::GetValidationErrors() const {
    std::ostringstream errors;

    if (m_ipcConfig.pipeName.empty()) {
        errors << "IPC pipe name cannot be empty; ";
    }
    if (m_ipcConfig.connectTimeout == 0 || m_ipcConfig.connectTimeout > 60000) {
        errors << "IPC connect timeout out of range (1-60000ms); ";
    }
    if (m_multiplayerConfig.syncRate <= 0.0f || m_multiplayerConfig.syncRate > 120.0f) {
        errors << "Multiplayer sync rate out of range (0.1-120Hz); ";
    }
    if (m_multiplayerConfig.maxPlayers == 0 || m_multiplayerConfig.maxPlayers > 256) {
        errors << "Max players out of range (1-256); ";
    }
    if (m_eventConfig.pollRate <= 0.0f || m_eventConfig.pollRate > 120.0f) {
        errors << "Event poll rate out of range (0.1-120Hz); ";
    }
    if (m_performanceConfig.reportInterval == 0) {
        errors << "Performance report interval cannot be 0; ";
    }
    if (m_renderingConfig.overlayWidth == 0 || m_renderingConfig.overlayHeight == 0) {
        errors << "Overlay dimensions cannot be 0; ";
    }

    return errors.str();
}

} // namespace Config
} // namespace ReKenshi

#include "../include/PatternCoordinator.h"
#include "../include/Logger.h"
#include "../include/Configuration.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <mutex>

using namespace ReKenshi;
using namespace ReKenshi::Patterns;
using namespace ReKenshi::Logging;
using namespace ReKenshi::Config;

//=============================================================================
// Simple IPC Client for Named Pipes
//=============================================================================

class SimpleIPCClient {
public:
    SimpleIPCClient(const std::string& pipeName) : m_pipeName(pipeName), m_pipe(INVALID_HANDLE_VALUE) {}

    ~SimpleIPCClient() {
        Disconnect();
    }

    bool Connect() {
        std::string fullPipeName = "\\\\.\\pipe\\" + m_pipeName;

        m_pipe = CreateFileA(
            fullPipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (m_pipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr);

        return true;
    }

    void Disconnect() {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    bool IsConnected() const {
        return m_pipe != INVALID_HANDLE_VALUE;
    }

    bool Send(const std::string& json) {
        if (!IsConnected()) return false;

        std::string message = json + "\n";
        DWORD bytesWritten;

        return WriteFile(m_pipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, nullptr);
    }

    bool Receive(std::string& json) {
        if (!IsConnected()) return false;

        char buffer[8192];
        DWORD bytesRead;

        if (ReadFile(m_pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            buffer[bytesRead] = '\0';
            json = std::string(buffer, bytesRead);
            return true;
        }

        return false;
    }

private:
    std::string m_pipeName;
    HANDLE m_pipe;
};

//=============================================================================
// Remote Player Data
//=============================================================================

struct RemotePlayer {
    std::string playerId;
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float health = 100.0f;
    bool isAlive = true;
    uint64_t lastUpdate = 0;
};

//=============================================================================
// Re_Kenshi Multiplayer Plugin
//=============================================================================

class ReKenshiPlugin {
public:
    static ReKenshiPlugin& GetInstance() {
        static ReKenshiPlugin instance;
        return instance;
    }

    bool Initialize() {
        if (m_initialized) return true;

        LOG_INFO("╔════════════════════════════════════════════════════════╗");
        LOG_INFO("║          Re_Kenshi Multiplayer Plugin v1.0            ║");
        LOG_INFO("╚════════════════════════════════════════════════════════╝");

        // Initialize logger
        auto& logger = Logger::GetInstance();
        logger.SetLogLevel(LogLevel::Info);
        logger.SetOutputTargets(LogOutput::DebugString | LogOutput::File);
        logger.SetLogFile("ReKenshi.log");
        logger.EnableTimestamps(true);

        LOG_INFO("Initializing pattern coordinator...");

        // Initialize pattern coordinator (does all the heavy lifting!)
        auto& coordinator = PatternCoordinator::GetInstance();
        if (!coordinator.Initialize()) {
            LOG_ERROR("Failed to initialize pattern coordinator!");
            return false;
        }

        LOG_INFO_F("Resolved %d/%d patterns",
                   coordinator.GetResolvedPatternCount(),
                   coordinator.GetTotalPatternCount());

        // Enable auto-update
        coordinator.EnableAutoUpdate(true);
        coordinator.SetUpdateRate(10.0f);  // 10 Hz

        LOG_INFO("Connecting to client service via IPC...");

        // Connect to IPC (client service)
        m_ipcClient = std::make_unique<SimpleIPCClient>("ReKenshi_IPC");
        if (!m_ipcClient->Connect()) {
            LOG_WARNING("Failed to connect to client service. Make sure ReKenshiClientService is running!");
            LOG_WARNING("You can still use the plugin, but multiplayer won't work.");
        } else {
            LOG_INFO("✓ Connected to client service");
        }

        m_initialized = true;
        LOG_INFO("Plugin initialized successfully!");

        return true;
    }

    void Update(float deltaTime) {
        if (!m_initialized) return;

        m_timeSinceLastUpdate += deltaTime;

        // Update pattern coordinator
        auto& coordinator = PatternCoordinator::GetInstance();
        coordinator.Update(deltaTime);

        // Send player updates (10 Hz)
        if (m_timeSinceLastUpdate >= 0.1f) {
            SendPlayerUpdate();
            ReceiveRemotePlayers();
            m_timeSinceLastUpdate = 0.0f;
        }
    }

    void Shutdown() {
        if (!m_initialized) return;

        LOG_INFO("Shutting down plugin...");

        m_ipcClient->Disconnect();
        m_ipcClient.reset();

        m_initialized = false;
        LOG_INFO("Plugin shut down successfully");
    }

    const std::unordered_map<std::string, RemotePlayer>& GetRemotePlayers() const {
        return m_remotePlayers;
    }

private:
    ReKenshiPlugin() = default;

    void SendPlayerUpdate() {
        if (!m_ipcClient || !m_ipcClient->IsConnected()) {
            // Try reconnecting
            if (m_ipcClient) {
                m_ipcClient->Connect();
            }
            return;
        }

        auto& coordinator = PatternCoordinator::GetInstance();

        // Read player character data
        Kenshi::CharacterData playerData;
        if (coordinator.GetCharacterData(PatternNames::PLAYER_CHARACTER, playerData)) {
            // Build JSON message
            std::ostringstream json;
            json << "{";
            json << "\"Type\":\"player_update\",";
            json << "\"Data\":{";
            json << "\"posX\":" << playerData.position.x << ",";
            json << "\"posY\":" << playerData.position.y << ",";
            json << "\"posZ\":" << playerData.position.z << ",";
            json << "\"health\":" << playerData.health << ",";
            json << "\"isAlive\":" << (playerData.isAlive ? "true" : "false");
            json << "}}";

            m_ipcClient->Send(json.str());
        }
    }

    void ReceiveRemotePlayers() {
        if (!m_ipcClient || !m_ipcClient->IsConnected()) return;

        std::string json;
        while (m_ipcClient->Receive(json)) {
            ParseRemotePlayerMessage(json);
        }
    }

    void ParseRemotePlayerMessage(const std::string& json) {
        // Very simple JSON parsing (for production, use a proper JSON library)
        if (json.find("\"Type\":\"remote_player\"") != std::string::npos) {
            RemotePlayer player;

            // Extract PlayerId
            size_t playerIdPos = json.find("\"PlayerId\":\"");
            if (playerIdPos != std::string::npos) {
                playerIdPos += 12;  // Length of "PlayerId":""
                size_t endPos = json.find("\"", playerIdPos);
                player.playerId = json.substr(playerIdPos, endPos - playerIdPos);
            }

            // Extract position and health
            player.posX = ExtractFloat(json, "\"posX\":");
            player.posY = ExtractFloat(json, "\"posY\":");
            player.posZ = ExtractFloat(json, "\"posZ\":");
            player.health = ExtractFloat(json, "\"health\":");
            player.isAlive = json.find("\"isAlive\":true") != std::string::npos;
            player.lastUpdate = GetTickCount64();

            // Store remote player
            std::lock_guard<std::mutex> lock(m_remotePlayersMutex);
            m_remotePlayers[player.playerId] = player;
        }
    }

    float ExtractFloat(const std::string& json, const std::string& key) {
        size_t pos = json.find(key);
        if (pos != std::string::npos) {
            pos += key.length();
            size_t endPos = json.find_first_of(",}", pos);
            if (endPos != std::string::npos) {
                try {
                    return std::stof(json.substr(pos, endPos - pos));
                }
                catch (...) {
                    return 0.0f;
                }
            }
        }
        return 0.0f;
    }

    bool m_initialized = false;
    std::unique_ptr<SimpleIPCClient> m_ipcClient;
    std::unordered_map<std::string, RemotePlayer> m_remotePlayers;
    std::mutex m_remotePlayersMutex;
    float m_timeSinceLastUpdate = 0.0f;
};

//=============================================================================
// Plugin Update Thread
//=============================================================================

static std::thread g_updateThread;
static bool g_running = false;

void PluginUpdateLoop() {
    auto& plugin = ReKenshiPlugin::GetInstance();

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        plugin.Update(deltaTime);

        // Sleep for 16ms (~60 FPS)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

//=============================================================================
// DLL Entry Point
//=============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);

        // Initialize plugin
        auto& plugin = ReKenshiPlugin::GetInstance();
        if (!plugin.Initialize()) {
            MessageBoxA(nullptr,
                "Failed to initialize Re_Kenshi Multiplayer Plugin!\n\n"
                "Check ReKenshi.log for details.",
                "Re_Kenshi Error",
                MB_OK | MB_ICONERROR);
            return FALSE;
        }

        // Start update thread
        g_running = true;
        g_updateThread = std::thread(PluginUpdateLoop);

        MessageBoxA(nullptr,
            "Re_Kenshi Multiplayer Plugin loaded successfully!\n\n"
            "Make sure to start:\n"
            "1. ReKenshiServer (on server machine)\n"
            "2. ReKenshiClientService (on this machine)\n\n"
            "Your game state will be synchronized automatically!",
            "Re_Kenshi",
            MB_OK | MB_ICONINFORMATION);
        break;
    }

    case DLL_PROCESS_DETACH: {
        g_running = false;
        if (g_updateThread.joinable()) {
            g_updateThread.join();
        }

        auto& plugin = ReKenshiPlugin::GetInstance();
        plugin.Shutdown();
        break;
    }

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}

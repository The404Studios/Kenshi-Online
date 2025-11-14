#include "../include/NetworkProtocol.h"
#include "../include/NetworkClient.h"
#include "../include/EntityBridge.h"
#include "../include/PatternCoordinator.h"
#include "../include/KServerModIntegration.h"
#include "../include/Logger.h"
#include "../include/Configuration.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace KenshiOnline::Network;
using namespace KenshiOnline::Bridge;
using namespace ReKenshi::Patterns;
using namespace ReKenshi::KServerMod;
using namespace ReKenshi::Logging;
using namespace ReKenshi::Config;

//=============================================================================
// Kenshi Online Plugin
//=============================================================================

class KenshiOnlinePlugin {
public:
    static KenshiOnlinePlugin& GetInstance() {
        static KenshiOnlinePlugin instance;
        return instance;
    }

    bool Initialize() {
        if (m_initialized) return true;

        PrintBanner();

        // Initialize logger
        auto& logger = Logger::GetInstance();
        logger.SetLogLevel(LogLevel::Info);
        logger.SetOutputTargets(LogOutput::DebugString | LogOutput::File);
        logger.SetLogFile("KenshiOnline.log");
        logger.EnableTimestamps(true);

        LOG_INFO("Initializing Kenshi Online Plugin...");

        // Load configuration
        auto& config = Configuration::GetInstance();
        if (!config.LoadConfig("kenshi_online_config.json")) {
            LOG_WARN("Failed to load config, using defaults");
        }

        // Initialize pattern coordinator
        LOG_INFO("Initializing pattern coordinator...");
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

        // Initialize KServerMod integration
        LOG_INFO("Initializing KServerMod integration...");
        auto& kserverMod = KServerModManager::GetInstance();
        if (!kserverMod.Initialize()) {
            LOG_WARN("KServerMod integration failed (non-critical)");
        }

        // Initialize entity bridge
        LOG_INFO("Initializing entity bridge...");
        auto& bridge = EntityBridge::GetInstance();
        if (!bridge.Initialize()) {
            LOG_ERROR("Failed to initialize entity bridge!");
            return false;
        }

        // Set local player info (from config or defaults)
        std::string playerId = config.GetString("player_id", "player_" + std::to_string(GetCurrentProcessId()));
        std::string playerName = config.GetString("player_name", "Player");
        bridge.SetLocalPlayerInfo(playerId, playerName);

        // Initialize network client
        LOG_INFO("Initializing network client...");
        auto& netClient = NetworkClient::GetInstance();

        // Register message callbacks
        RegisterNetworkCallbacks();

        // Connect to client service
        if (!netClient.Connect("KenshiOnline_IPC")) {
            LOG_ERROR("Failed to connect to client service!");
            LOG_ERROR("Make sure KenshiOnlineClientService is running");
            return false;
        }

        // Send connect message
        auto connectMsg = ProtocolHandler::CreateConnectMessage(playerId, playerName);
        netClient.Send(connectMsg);

        m_initialized = true;
        m_running = true;

        // Start update loop
        m_updateThread = std::thread(&KenshiOnlinePlugin::UpdateLoop, this);

        LOG_INFO("Kenshi Online Plugin initialized successfully!");
        return true;
    }

    void Shutdown() {
        if (!m_initialized) return;

        LOG_INFO("Shutting down Kenshi Online Plugin...");

        m_running = false;

        if (m_updateThread.joinable()) {
            m_updateThread.join();
        }

        auto& netClient = NetworkClient::GetInstance();
        netClient.Disconnect();

        m_initialized = false;
        LOG_INFO("Kenshi Online Plugin shut down");
    }

    bool IsInitialized() const {
        return m_initialized;
    }

    bool IsRunning() const {
        return m_running;
    }

private:
    KenshiOnlinePlugin() = default;
    ~KenshiOnlinePlugin() {
        Shutdown();
    }
    KenshiOnlinePlugin(const KenshiOnlinePlugin&) = delete;
    KenshiOnlinePlugin& operator=(const KenshiOnlinePlugin&) = delete;

    void PrintBanner() {
        LOG_INFO("╔════════════════════════════════════════════════════════╗");
        LOG_INFO("║          Kenshi Online Plugin v2.0                     ║");
        LOG_INFO("║          Complete Multiplayer Integration              ║");
        LOG_INFO("╚════════════════════════════════════════════════════════╝");
    }

    void RegisterNetworkCallbacks() {
        auto& netClient = NetworkClient::GetInstance();

        // Response messages
        netClient.RegisterCallback("response", [this](const NetworkMessage& msg) {
            OnResponseReceived(msg);
        });

        // Entity updates
        netClient.RegisterCallback("entity_update", [this](const NetworkMessage& msg) {
            OnEntityUpdate(msg);
        });

        // Entity create/destroy
        netClient.RegisterCallback("entity_create", [this](const NetworkMessage& msg) {
            OnEntityCreate(msg);
        });

        netClient.RegisterCallback("entity_destroy", [this](const NetworkMessage& msg) {
            OnEntityDestroy(msg);
        });

        // Combat events
        netClient.RegisterCallback("combat_event", [this](const NetworkMessage& msg) {
            OnCombatEvent(msg);
        });

        // World state
        netClient.RegisterCallback("world_state", [this](const NetworkMessage& msg) {
            OnWorldState(msg);
        });
    }

    // Message handlers
    void OnResponseReceived(const NetworkMessage& msg) {
        bool success = msg.data.value("success", false);
        std::string message = msg.data.value("message", "");

        if (success) {
            LOG_INFO_F("Server response: %s", message.c_str());

            // Store session ID if provided
            if (msg.data.contains("sessionId")) {
                m_sessionId = msg.data["sessionId"].get<std::string>();
                LOG_INFO_F("Session ID: %s", m_sessionId.c_str());
            }
        }
        else {
            LOG_ERROR_F("Server error: %s", message.c_str());
        }
    }

    void OnEntityUpdate(const NetworkMessage& msg) {
        try {
            PlayerEntity player;
            player.Deserialize(msg.data);

            // Apply to entity bridge
            auto& bridge = EntityBridge::GetInstance();
            bridge.ApplyRemotePlayerEntity(player);
        }
        catch (...) {
            LOG_ERROR("Failed to process entity update");
        }
    }

    void OnEntityCreate(const NetworkMessage& msg) {
        try {
            std::string entityType = msg.data.value("type", "");

            if (entityType == "Player") {
                PlayerEntity player;
                player.Deserialize(msg.data);

                auto& bridge = EntityBridge::GetInstance();
                bridge.ApplyRemotePlayerEntity(player);

                LOG_INFO_F("Remote player joined: %s", player.playerName.c_str());
            }
        }
        catch (...) {
            LOG_ERROR("Failed to process entity create");
        }
    }

    void OnEntityDestroy(const NetworkMessage& msg) {
        try {
            std::string entityId = msg.data.value("id", "");

            if (!entityId.empty()) {
                auto& bridge = EntityBridge::GetInstance();
                bridge.RemoveRemotePlayer(entityId);

                LOG_INFO_F("Remote player left: %s", entityId.c_str());
            }
        }
        catch (...) {
            LOG_ERROR("Failed to process entity destroy");
        }
    }

    void OnCombatEvent(const NetworkMessage& msg) {
        try {
            std::string eventType = msg.data.value("type", "");
            LOG_INFO_F("Combat event: %s", eventType.c_str());

            // TODO: Process combat events (play animations, sounds, etc.)
        }
        catch (...) {
            LOG_ERROR("Failed to process combat event");
        }
    }

    void OnWorldState(const NetworkMessage& msg) {
        try {
            float gameTime = msg.data.value("gameTime", 12.0f);
            int gameDay = msg.data.value("gameDay", 1);
            std::string weather = msg.data.value("weather", "Clear");

            LOG_INFO_F("World state: Day %d, Time %.1f, Weather: %s",
                       gameDay, gameTime, weather.c_str());

            // TODO: Apply world state to game (time, weather, etc.)
        }
        catch (...) {
            LOG_ERROR("Failed to process world state");
        }
    }

    // Update loop (runs at 10 Hz)
    void UpdateLoop() {
        const float updateRate = 10.0f; // 10 Hz
        const auto updateInterval = std::chrono::milliseconds(static_cast<int>(1000.0f / updateRate));

        auto lastHeartbeat = std::chrono::steady_clock::now();
        const auto heartbeatInterval = std::chrono::seconds(30);

        while (m_running) {
            auto frameStart = std::chrono::steady_clock::now();

            try {
                // Update pattern coordinator
                auto& coordinator = PatternCoordinator::GetInstance();
                coordinator.Update(1.0f / updateRate);

                // Read local player state
                auto& bridge = EntityBridge::GetInstance();
                PlayerEntity localPlayer;

                if (bridge.ReadLocalPlayerEntity(localPlayer)) {
                    // Send entity update
                    auto& netClient = NetworkClient::GetInstance();
                    if (netClient.IsConnected()) {
                        auto msg = ProtocolHandler::CreateEntityUpdateMessage(localPlayer);
                        msg.sessionId = m_sessionId;
                        msg.playerId = bridge.GetLocalPlayerId();
                        netClient.Send(msg);
                    }
                }

                // Send heartbeat periodically
                auto now = std::chrono::steady_clock::now();
                if (now - lastHeartbeat >= heartbeatInterval) {
                    auto& netClient = NetworkClient::GetInstance();
                    if (netClient.IsConnected()) {
                        auto heartbeatMsg = ProtocolHandler::CreateHeartbeatMessage(0);
                        heartbeatMsg.sessionId = m_sessionId;
                        heartbeatMsg.playerId = bridge.GetLocalPlayerId();
                        netClient.Send(heartbeatMsg);
                    }
                    lastHeartbeat = now;
                }
            }
            catch (...) {
                LOG_ERROR("Update loop error");
            }

            // Sleep to maintain update rate
            auto frameEnd = std::chrono::steady_clock::now();
            auto frameDuration = frameEnd - frameStart;
            if (frameDuration < updateInterval) {
                std::this_thread::sleep_for(updateInterval - frameDuration);
            }
        }
    }

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::thread m_updateThread;
    std::string m_sessionId;
};

//=============================================================================
// DLL Entry Point
//=============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        // Initialize plugin in separate thread to avoid blocking
        std::thread([hModule]() {
            // Wait a bit for game to initialize
            std::this_thread::sleep_for(std::chrono::seconds(2));

            auto& plugin = KenshiOnlinePlugin::GetInstance();
            if (!plugin.Initialize()) {
                // Failed to initialize
                MessageBoxA(nullptr,
                           "Failed to initialize Kenshi Online Plugin!\nCheck KenshiOnline.log for details.",
                           "Kenshi Online Error",
                           MB_OK | MB_ICONERROR);
            }
        }).detach();
        break;

    case DLL_PROCESS_DETACH:
        auto& plugin = KenshiOnlinePlugin::GetInstance();
        plugin.Shutdown();
        break;
    }

    return TRUE;
}

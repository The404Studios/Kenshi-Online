#pragma once

#include "GameEventManager.h"
#include "KenshiStructures.h"
#include "MessageProtocol.h"
#include <memory>
#include <unordered_map>
#include <queue>

namespace ReKenshi {

// Forward declarations
namespace IPC {
    class IPCClient;
}

namespace Multiplayer {

/**
 * Sync flags - what data to synchronize
 */
enum class SyncFlags : uint32_t {
    None = 0,
    Position = 1 << 0,
    Health = 1 << 1,
    Rotation = 1 << 2,
    Animation = 1 << 3,
    Equipment = 1 << 4,
    Inventory = 1 << 5,
    Stats = 1 << 6,
    All = 0xFFFFFFFF
};

inline SyncFlags operator|(SyncFlags a, SyncFlags b) {
    return static_cast<SyncFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(SyncFlags a, SyncFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

/**
 * Network player representation
 */
struct NetworkPlayer {
    std::string playerId;
    std::string playerName;
    uintptr_t characterAddress;  // Local character representing this network player
    Kenshi::CharacterData lastSyncedData;
    uint64_t lastUpdateTime;
    bool isLocal;

    NetworkPlayer() : characterAddress(0), lastUpdateTime(0), isLocal(false) {}
};

/**
 * Multiplayer Synchronization Manager
 * Handles synchronizing game state between clients via IPC
 */
class MultiplayerSyncManager {
public:
    MultiplayerSyncManager();
    ~MultiplayerSyncManager();

    // Initialization
    void Initialize(
        IPC::IPCClient* ipcClient,
        Events::GameEventManager* eventManager,
        uintptr_t playerPtr
    );
    void Shutdown();

    // Update loop
    void Update(float deltaTime);

    // Player management
    void SetLocalPlayer(const std::string& playerId, const std::string& playerName);
    void AddNetworkPlayer(const std::string& playerId, const std::string& playerName);
    void RemoveNetworkPlayer(const std::string& playerId);

    // Synchronization control
    void SetSyncFlags(SyncFlags flags) { m_syncFlags = flags; }
    SyncFlags GetSyncFlags() const { return m_syncFlags; }

    void SetSyncRate(float rateHz) { m_syncInterval = 1.0f / rateHz; }
    float GetSyncRate() const { return 1.0f / m_syncInterval; }

    // State
    bool IsInitialized() const { return m_initialized; }
    bool IsConnected() const { return m_connected; }
    size_t GetPlayerCount() const { return m_networkPlayers.size(); }

    // Statistics
    struct SyncStats {
        uint64_t packetsSent;
        uint64_t packetsReceived;
        uint64_t bytesSent;
        uint64_t bytesReceived;
        float averageLatency;
        uint64_t updatesSent;
        uint64_t updatesReceived;
    };

    const SyncStats& GetStats() const { return m_stats; }
    void ResetStats();

private:
    // Event handlers
    void OnCharacterDamaged(const Events::GameEvent& evt);
    void OnCharacterMoved(const Events::GameEvent& evt);
    void OnCharacterDied(const Events::GameEvent& evt);
    void OnPlayerConnected(const Events::GameEvent& evt);
    void OnPlayerDisconnected(const Events::GameEvent& evt);

    // IPC handlers
    void OnIPCMessage(const IPC::Message& msg);
    void HandlePlayerUpdate(const IPC::Message& msg);
    void HandleWorldUpdate(const IPC::Message& msg);
    void HandleCombatEvent(const IPC::Message& msg);

    // Sync operations
    void SyncLocalPlayer();
    void SyncNetworkPlayers();
    void SendPlayerUpdate(const Kenshi::CharacterData& data);
    void ApplyPlayerUpdate(const std::string& playerId, const Kenshi::CharacterData& data);

    // Helper methods
    bool ShouldSyncData(const Kenshi::CharacterData& current, const Kenshi::CharacterData& last);
    float CalculateDistance(const Kenshi::Vector3& a, const Kenshi::Vector3& b);

    bool m_initialized;
    bool m_connected;

    // Dependencies
    IPC::IPCClient* m_ipcClient;
    Events::GameEventManager* m_eventManager;

    // Local player
    uintptr_t m_localPlayerPtr;
    NetworkPlayer m_localPlayer;

    // Network players
    std::unordered_map<std::string, NetworkPlayer> m_networkPlayers;

    // Sync configuration
    SyncFlags m_syncFlags;
    float m_syncInterval;
    float m_syncAccumulator;

    // Statistics
    SyncStats m_stats;

    // Throttling
    static constexpr float MIN_SYNC_INTERVAL = 0.05f;  // 20 Hz max
    static constexpr float MAX_SYNC_INTERVAL = 1.0f;   // 1 Hz min
    static constexpr float POSITION_THRESHOLD = 0.5f;  // Sync position if moved > 0.5 units
    static constexpr float HEALTH_THRESHOLD = 1.0f;    // Sync health if changed > 1 point
};

} // namespace Multiplayer
} // namespace ReKenshi

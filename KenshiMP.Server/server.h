#pragma once
#include "kmp/config.h"
#include "kmp/types.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include "upnp.h"
#include <enet/enet.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <atomic>

namespace kmp {

struct ConnectedPlayer {
    PlayerID    id;
    std::string name;
    ENetPeer*   peer;
    Vec3        position;
    ZoneCoord   zone;
    uint32_t    ping;
    float       lastUpdate;
    std::vector<EntityID> ownedEntities;
};

struct ServerEntity {
    // Identity
    EntityID    id = 0;
    EntityType  type = EntityType::NPC;
    PlayerID    owner = 0;

    // State & authority (spec §2.2, §2.3)
    EntityState   state     = EntityState::Active;
    AuthorityType authority = AuthorityType::Host;
    uint16_t      dirtyFlags = Dirty_None;

    // Transform
    Vec3        position;
    Quat        rotation;
    ZoneCoord   zone;

    // Game state
    uint32_t    templateId = 0;
    uint32_t    factionId = 0;
    std::string templateName;   // GameData template name for spawning (e.g. "Greenlander")
    float       health[7] = {100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f};
    CombatInfo  combat;
    uint8_t     animState = 0;
    uint8_t     moveSpeed = 0;  // 0-255 mapped to 0.0-15.0 m/s
    uint16_t    flags = 0;
    bool        alive = true;
    float       buildProgress = 0.f; // 0.0-1.0 for buildings
    uint32_t    equipment[14] = {};  // EquipSlot::Count = 14
};

class GameServer {
public:
    bool Start(const ServerConfig& config);
    void Stop();
    void Update(float deltaTime);

    // Admin commands
    void KickPlayer(PlayerID id, const std::string& reason);
    void BroadcastSystemMessage(const std::string& message);
    void SaveWorld();
    void LoadWorld();
    void PrintStatus();
    void PrintPlayers();

private:
    // Network
    void HandleConnect(ENetPeer* peer);
    void HandleDisconnect(ENetPeer* peer);
    void HandlePacket(ENetPeer* peer, const uint8_t* data, size_t size, int channel);

    // Message handlers
    void HandleHandshake(ENetPeer* peer, PacketReader& reader);
    void HandlePositionUpdate(ConnectedPlayer& player, PacketReader& reader);
    void HandleMoveCommand(ConnectedPlayer& player, PacketReader& reader);
    void HandleAttackIntent(ConnectedPlayer& player, PacketReader& reader);
    void HandleChatMessage(ConnectedPlayer& player, PacketReader& reader);
    void HandleBuildRequest(ConnectedPlayer& player, PacketReader& reader);
    void HandleEntitySpawnReq(ConnectedPlayer& player, PacketReader& reader);
    void HandleEntityDespawnReq(ConnectedPlayer& player, PacketReader& reader);
    void HandleEquipmentUpdate(ConnectedPlayer& player, PacketReader& reader);
    void HandleZoneRequest(ConnectedPlayer& player, PacketReader& reader);
    void HandleItemPickup(ConnectedPlayer& player, PacketReader& reader);
    void HandleItemDrop(ConnectedPlayer& player, PacketReader& reader);
    void HandleTradeRequest(ConnectedPlayer& player, PacketReader& reader);
    void HandleSquadCreate(ConnectedPlayer& player, PacketReader& reader);
    void HandleSquadAddMember(ConnectedPlayer& player, PacketReader& reader);
    void HandleFactionRelation(ConnectedPlayer& player, PacketReader& reader);
    void HandleBuildDismantle(ConnectedPlayer& player, PacketReader& reader);
    void HandleBuildRepair(ConnectedPlayer& player, PacketReader& reader);
    void HandleCombatStance(ConnectedPlayer& player, PacketReader& reader);
    void HandleCombatKO(ConnectedPlayer& player, PacketReader& reader);
    void HandleCombatDeath(ConnectedPlayer& player, PacketReader& reader);
    void HandleItemTransfer(ConnectedPlayer& player, PacketReader& reader);
    void HandleDoorInteract(ConnectedPlayer& player, PacketReader& reader);
    void HandleAdminCommand(ConnectedPlayer& player, PacketReader& reader);

    // Broadcasting
    void Broadcast(const uint8_t* data, size_t len, int channel, uint32_t flags);
    void BroadcastExcept(PlayerID exclude, const uint8_t* data, size_t len, int channel, uint32_t flags);
    void SendTo(PlayerID id, const uint8_t* data, size_t len, int channel, uint32_t flags);

    // Game state
    void BroadcastPositions();
    void BroadcastTimeSync();
    void SendWorldSnapshot(ConnectedPlayer& player);

    // Helpers
    ConnectedPlayer* GetPlayer(ENetPeer* peer);
    ConnectedPlayer* GetPlayer(PlayerID id);
    PlayerID NextPlayerId();

    // Query handler (responds without handshake for server browser)
    void HandleServerQuery(ENetPeer* peer, PacketReader& reader);

    ENetHost* m_host = nullptr;
    ServerConfig m_config;
    std::unordered_map<PlayerID, ConnectedPlayer> m_players;
    std::unordered_map<EntityID, ServerEntity> m_entities;
    std::recursive_mutex m_mutex;

    PlayerID m_nextPlayerId = 1;
    EntityID m_nextEntityId = 1;
    uint32_t m_nextSquadId = 0x80000000; // Separate ID space for squads (avoids entity ID collisions)
    PlayerID m_hostPlayerId = 0;  // First connected player = host
    uint32_t m_serverTick = 0;
    float    m_timeOfDay = 0.5f;
    int      m_weatherState = 0;
    float    m_timeSinceTimeSync = 0.f;
    float    m_uptime = 0.f;

    // UPnP auto port mapping (runs synchronously before ENet host is created)
    UPnPMapper m_upnp;

    // Auto-save
    float    m_timeSinceAutoSave = 0.f;
    float    m_autoSaveInterval = 60.f; // seconds

    // Master server registration
    ENetHost* m_masterHost = nullptr;  // Separate ENet host for master connection
    ENetPeer* m_masterPeer = nullptr;
    bool      m_masterConnected = false;
    float     m_timeSinceMasterHeartbeat = 0.f;
    float     m_masterHeartbeatInterval = 30.f; // seconds
    float     m_masterReconnectTimer = 0.f;
    float     m_masterReconnectDelay = 5.f;    // seconds, doubles on each failure (max 60s)

    void ConnectToMaster();
    void SendMasterRegister();
    void SendMasterHeartbeat();
    void SendMasterDeregister();
    void UpdateMasterConnection(float deltaTime);
};

// World persistence (defined in world_persistence.cpp)
bool SaveWorldToFile(const std::string& path,
                     const std::unordered_map<EntityID, ServerEntity>& entities,
                     float timeOfDay, int weatherState);
bool LoadWorldFromFile(const std::string& path,
                       std::unordered_map<EntityID, ServerEntity>& entities,
                       float& timeOfDay, int& weatherState,
                       EntityID& nextEntityId);

} // namespace kmp

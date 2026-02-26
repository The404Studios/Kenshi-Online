#pragma once
#include "kmp/config.h"
#include "kmp/types.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include <enet/enet.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>

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
    EntityID    id;
    EntityType  type;
    PlayerID    owner;
    Vec3        position;
    Quat        rotation;
    ZoneCoord   zone;
    uint32_t    templateId;
    uint32_t    factionId;
    float       health[7]; // Per body part
    uint8_t     animState;
    bool        alive;
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

    ENetHost* m_host = nullptr;
    ServerConfig m_config;
    std::unordered_map<PlayerID, ConnectedPlayer> m_players;
    std::unordered_map<EntityID, ServerEntity> m_entities;
    std::mutex m_mutex;

    PlayerID m_nextPlayerId = 1;
    EntityID m_nextEntityId = 1;
    uint32_t m_serverTick = 0;
    float    m_timeOfDay = 0.5f;
    int      m_weatherState = 0;
    float    m_timeSinceTimeSync = 0.f;
    float    m_uptime = 0.f;
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

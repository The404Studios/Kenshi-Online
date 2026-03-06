#pragma once
#include "kmp/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace kmp {

// Forward declarations
class EntityRegistry;
class SpawnManager;

// Tracks a remote player's state and their characters in the world.
struct RemotePlayerState {
    PlayerID    playerId = 0;
    std::string playerName;
    Vec3        lastKnownPosition;
    bool        hasSpawnedCharacter = false;  // True if at least one character is visible
    uintptr_t   factionPtr = 0;              // Faction pointer from their first spawned character
    std::vector<EntityID> entities;           // All entity IDs owned by this player
};

// PlayerController manages the multiplayer character lifecycle:
// - Local player: tracks squad, sends entity data, handles spawn/despawn
// - Remote players: renames spawned characters, fixes factions, tracks state
class PlayerController {
public:
    // ── Local Player ──

    // Called after handshake succeeds. Scans game world for local characters
    // and registers them with the entity registry.
    void InitializeLocalPlayer(PlayerID localId, const std::string& playerName);

    // Get the local player's ID
    PlayerID GetLocalPlayerId() const { return m_localPlayerId; }
    const std::string& GetLocalPlayerName() const { return m_localPlayerName; }

    // Get entity IDs for the local player's squad characters
    std::vector<EntityID> GetLocalSquadEntities() const;

    // Get the local player's primary character (first entity, or selected)
    void* GetPrimaryCharacter() const;

    // Get the local player's faction pointer (captured from first character)
    uintptr_t GetLocalFactionPtr() const { return m_localFactionPtr; }

    // Set the local player's faction pointer (called from entity_hooks as faction bootstrap).
    // Also fixes up any previously-spawned remote characters that still have their NPC faction.
    void SetLocalFactionPtr(uintptr_t factionPtr);

    // ── Remote Players ──

    // Register a remote player (called on PlayerJoined)
    void RegisterRemotePlayer(PlayerID id, const std::string& name);

    // Remove a remote player and all their entities (called on PlayerLeft)
    void RemoveRemotePlayer(PlayerID id);

    // Called when a remote player's character is physically spawned in the game world.
    // Renames the character to the player's name and fixes faction.
    // Returns true if the character was successfully set up.
    bool OnRemoteCharacterSpawned(EntityID entityId, void* gameObject, PlayerID owner);

    // Get remote player state
    const RemotePlayerState* GetRemotePlayer(PlayerID id) const;

    // Get all remote player states
    std::vector<RemotePlayerState> GetAllRemotePlayers() const;

    // ── Sync ──

    // Gather position/rotation data for all local entities that need syncing.
    // Returns the number of entities that had position changes.
    int GatherLocalEntityUpdates(float deltaTime);

    // Apply a position update to a remote entity (from network).
    // Feeds the interpolation system.
    void ApplyRemotePositionUpdate(EntityID entityId, const Vec3& pos,
                                    const Quat& rot, uint8_t moveSpeed, uint8_t animState);

    // ── Loading Flow ──

    // Called when the game world finishes loading.
    // Captures the local player's faction from their first character.
    void OnGameWorldLoaded();

    // Called when receiving a world snapshot from the server.
    // Prepares for remote entity spawning.
    void OnWorldSnapshotReceived(int entityCount);

    // Reset all state (on disconnect)
    void Reset();

private:
    mutable std::mutex m_mutex;

    // Local player
    PlayerID    m_localPlayerId = 0;
    std::string m_localPlayerName;
    uintptr_t   m_localFactionPtr = 0;  // Captured from first local character
    bool        m_initialized = false;

    // Remote players
    std::unordered_map<PlayerID, RemotePlayerState> m_remotePlayers;
};

} // namespace kmp

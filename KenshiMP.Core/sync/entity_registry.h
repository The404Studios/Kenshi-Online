#pragma once
#include "kmp/types.h"
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <optional>

namespace kmp {

struct EntityInfo {
    // Identity
    EntityID      netId = INVALID_ENTITY;
    void*         gameObject = nullptr;
    EntityType    type = EntityType::NPC;
    PlayerID      ownerPlayerId = 0; // 0 = server-owned

    // State & authority (spec §2.2, §2.3)
    EntityState   state     = EntityState::Inactive;
    AuthorityType authority = AuthorityType::None;
    uint16_t      dirtyFlags = Dirty_None;

    // Transform
    ZoneCoord     zone;
    Vec3          lastPosition;
    Vec3          velocity;
    Quat          lastRotation;

    // Game state
    float         health = 100.f;
    LimbHealth    limbs;
    CombatInfo    combat;
    uint8_t       moveSpeed = 0;
    uint8_t       animState = 0;

    // Replication
    uint64_t      lastUpdateTick = 0;
    bool          isRemote = false; // True = controlled by another player/server
    uint32_t      lastEquipment[14] = {}; // Per-slot for diff detection
};

class EntityRegistry {
public:
    // Register a local game object (optionally with owner)
    EntityID Register(void* gameObject, EntityType type, PlayerID owner = 0);

    // Register a remote entity (spawned by network)
    EntityID RegisterRemote(EntityID netId, EntityType type, PlayerID owner, const Vec3& pos);

    // Find game object by network ID
    void* GetGameObject(EntityID netId) const;

    // Find network ID by game object pointer
    EntityID GetNetId(void* gameObject) const;

    // Get entity info (raw pointer — caller must not hold across lock boundaries)
    const EntityInfo* GetInfo(EntityID netId) const;

    // Get entity info as a copy (thread-safe — no dangling pointer risk)
    std::optional<EntityInfo> GetInfoCopy(EntityID netId) const;

    // Associate a real game object with a remote entity after spawning
    void SetGameObject(EntityID netId, void* gameObject);

    // Update position tracking
    void UpdatePosition(EntityID netId, const Vec3& pos);
    void UpdateRotation(EntityID netId, const Quat& rot);

    // Update entity owner (for ownership transfer)
    void UpdateOwner(EntityID netId, PlayerID newOwner);

    // Update equipment tracking for a single slot
    void UpdateEquipment(EntityID netId, int slot, uint32_t itemTemplateId);

    // Update dirty flags (thread-safe bitwise OR / AND-NOT)
    void SetDirtyFlags(EntityID netId, uint16_t flags);
    void ClearDirtyFlags(EntityID netId, uint16_t mask);

    // Remap a local entity ID to a new server-assigned ID.
    // Preserves the game object, owner, and all state.
    bool RemapEntityId(EntityID oldId, EntityID newId);

    // Find a local (non-remote) entity near a given position owned by a specific player.
    // Returns INVALID_ENTITY if none found.
    EntityID FindLocalEntityNear(const Vec3& pos, PlayerID owner, float maxDist = 5.0f) const;

    // Remove entity
    void Unregister(EntityID netId);

    // Remove all entities in a zone
    void RemoveEntitiesInZone(const ZoneCoord& zone);

    // Get all entities owned by a player
    std::vector<EntityID> GetPlayerEntities(PlayerID playerId) const;

    // Get all entities in a zone
    std::vector<EntityID> GetEntitiesInZone(const ZoneCoord& zone) const;

    // Get all remote entities (for interpolation)
    std::vector<EntityID> GetRemoteEntities() const;

    // Stats
    size_t GetEntityCount() const;
    size_t GetRemoteCount() const;
    size_t GetSpawnedRemoteCount() const; // Remote entities with linked game objects

    // Clear all remote entities (on disconnect)
    size_t ClearRemoteEntities();

    // Clear all
    void Clear();

private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<EntityID, EntityInfo> m_entities;
    std::unordered_map<void*, EntityID>      m_ptrToId;
    EntityID m_nextId = 0x10000000; // Start high to avoid collisions with server-assigned IDs
};

} // namespace kmp

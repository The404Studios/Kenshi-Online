#pragma once
#include "sync_orchestrator.h"
#include "entity_resolver.h"
#include "zone_engine.h"
#include "player_engine.h"
#include "entity_registry.h"
#include "interpolation.h"
#include "../game/spawn_manager.h"
#include "kmp/types.h"
#include <string>
#include <vector>

namespace kmp {

// SyncFacilitator — simplified facade for common cross-cutting operations.
//
// Instead of hooks, commands, and packet handlers needing to know about
// EntityResolver, ZoneEngine, PlayerEngine, and their APIs individually,
// they call the Facilitator for high-level operations.
//
// Example:
//   auto& fac = SyncFacilitator::Get();
//   fac.OnEntityPositionChanged(entityId, newPos);
//   fac.IsEntityRelevant(entityId);
//   fac.GetNearbyPlayerEntities(pos, radius);
//
// The Facilitator is a thin layer — it holds a reference to the SyncOrchestrator
// and delegates to the appropriate engine. No logic duplication.

class SyncFacilitator {
public:
    static SyncFacilitator& Get();

    // Must be called once after SyncOrchestrator is constructed
    void Bind(SyncOrchestrator* orchestrator, EntityRegistry* registry,
              Interpolation* interpolation, SpawnManager* spawnManager);
    void Unbind();
    bool IsBound() const { return m_orch != nullptr; }

    // ════════════════════════════════════════════════════════════════
    // Entity Operations (combines EntityResolver + Registry + Zone)
    // ════════════════════════════════════════════════════════════════

    // Is this entity relevant to the local player (in interest range)?
    bool IsEntityRelevant(EntityID id) const;

    // Is this entity alive (active + has game object)?
    bool IsEntityAlive(EntityID id) const;

    // Is this entity owned by the local player?
    bool IsOwnedByLocal(EntityID id) const;

    // Get all entities near a world position, optionally filtered by owner
    std::vector<EntityID> GetEntitiesNear(const Vec3& pos, float radius,
                                           PlayerID ownerFilter = INVALID_PLAYER) const;

    // Get all entities owned by a player that are in the interest range
    std::vector<EntityID> GetRelevantPlayerEntities(PlayerID playerId) const;

    // Mark an entity's fields as dirty for replication
    void MarkEntityDirty(EntityID id, uint16_t dirtyFlags);

    // Consume all entities dirty for specific flags (returns IDs, clears flags)
    std::vector<EntityID> ConsumeDirtyEntities(uint16_t mask);

    // ════════════════════════════════════════════════════════════════
    // Player Operations (combines PlayerEngine + ZoneEngine)
    // ════════════════════════════════════════════════════════════════

    // Get a player's display name (works for local + remote)
    std::string GetPlayerName(PlayerID id) const;

    // Is a remote player in the same zone as the local player?
    bool IsPlayerNearby(PlayerID id) const;

    // Get all players currently in the local player's zone neighborhood
    std::vector<PlayerID> GetNearbyPlayers() const;

    // Get player state (Connecting, Loading, InGame, AFK, Disconnected)
    PlayerState GetPlayerState(PlayerID id) const;

    // Find a player by partial name match (case-insensitive)
    PlayerID FindPlayer(const std::string& partialName) const;

    // ════════════════════════════════════════════════════════════════
    // Zone Operations (combines ZoneEngine)
    // ════════════════════════════════════════════════════════════════

    // Get the local player's current zone
    ZoneCoord GetLocalZone() const;

    // Get all entities in the local player's interest range (3x3 zones)
    std::vector<EntityID> GetInterestEntities() const;

    // Get zone population stats for the local area
    struct ZoneStats {
        ZoneCoord localZone;
        size_t localZonePopulation;
        size_t interestPopulation;   // Total across 3x3
        int populatedZoneCount;
    };
    ZoneStats GetLocalZoneStats() const;

    // ════════════════════════════════════════════════════════════════
    // Spawn Operations (combines SpawnManager + EntityResolver)
    // ════════════════════════════════════════════════════════════════

    // Get pending spawn count
    size_t GetPendingSpawnCount() const;

    // Is the spawn system ready?
    bool IsSpawnReady() const;

    // ════════════════════════════════════════════════════════════════
    // Event Notifications (hooks call these to update engines)
    // ════════════════════════════════════════════════════════════════

    // Called when an entity's position changes (from hooks or background read)
    void OnEntityPositionChanged(EntityID id, const Vec3& newPos);

    // Called when a remote player sends any packet (activity tracking)
    void OnPlayerActivity(PlayerID id);

    // Called when a player's position is known (from position updates)
    void OnPlayerPositionKnown(PlayerID id, const Vec3& pos);

private:
    SyncFacilitator() = default;

    SyncOrchestrator* m_orch = nullptr;
    EntityRegistry*   m_registry = nullptr;
    Interpolation*    m_interpolation = nullptr;
    SpawnManager*     m_spawnManager = nullptr;
};

} // namespace kmp

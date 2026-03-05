#pragma once
#include "entity_registry.h"
#include "kmp/types.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>

namespace kmp {

// Filter predicate for compound queries (AND combination of set fields)
struct EntityFilter {
    PlayerID    owner       = INVALID_PLAYER; // 0 = any owner
    ZoneCoord   zone;
    bool        useZone     = false;
    EntityType  type        = EntityType::NPC;
    bool        useType     = false;
    EntityState state       = EntityState::Active;
    bool        useState    = false;
    bool        localOnly   = false;   // Only non-remote entities
    bool        remoteOnly  = false;   // Only remote entities
    uint16_t    dirtyMask   = Dirty_None; // 0 = any dirty state
};

// Result of an interest query: which entities matter to a specific player
struct InterestSet {
    PlayerID                playerId = INVALID_PLAYER;
    ZoneCoord               playerZone;
    std::vector<EntityID>   entities;       // Entities in 3x3 zone grid
    std::vector<EntityID>   enteringView;   // Entities that just entered view
    std::vector<EntityID>   leavingView;    // Entities that just left view
};

class EntityResolver {
public:
    explicit EntityResolver(EntityRegistry& registry);

    // ---- Compound Queries ----

    // Query entities matching all set filter fields (AND combination)
    std::vector<EntityID> Query(const EntityFilter& filter) const;

    // Count entities matching filter (avoids allocation)
    size_t Count(const EntityFilter& filter) const;

    // ---- Spatial Queries ----

    // Entities within radius of a world position
    std::vector<EntityID> InRadius(const Vec3& center, float radius) const;

    // Entities in a zone and all adjacent zones (3x3 grid)
    std::vector<EntityID> InZoneNeighborhood(const ZoneCoord& center) const;

    // ---- Interest Management ----

    // Compute which entities a player should receive updates for.
    // Compares against previous InterestSet to compute enter/leave deltas.
    InterestSet ComputeInterest(PlayerID playerId, const ZoneCoord& playerZone);

    // Get the current interest set for a player (from last ComputeInterest call)
    const InterestSet* GetInterest(PlayerID playerId) const;

    // Clear cached interest state for a player (on disconnect)
    void ClearInterest(PlayerID playerId);

    // ---- Dirty Flag Management ----

    // Mark an entity as dirty for specific fields
    void MarkDirty(EntityID id, uint16_t flags);

    // Get all entities dirty for at least one of the given flags, then clear those flags
    std::vector<EntityID> ConsumeDirty(uint16_t mask);

    // Check if entity has any dirty flags set
    bool IsDirty(EntityID id, uint16_t mask) const;

    // ---- Lifecycle Validation ----

    bool CanSync(EntityID id) const;    // Active or Spawning
    bool CanDespawn(EntityID id) const; // Active only
    bool IsAlive(EntityID id) const;    // Active + has game object

    // ---- Ownership (replaces OwnershipManager singleton) ----

    bool IsLocallyOwned(EntityID id, PlayerID localPlayer) const;
    bool IsServerOwned(EntityID id) const;
    void TransferToServer(PlayerID fromPlayer);

    // ---- Direct passthrough ----
    EntityRegistry& Registry() { return m_registry; }
    const EntityRegistry& Registry() const { return m_registry; }

private:
    bool MatchesFilter(const EntityInfo& info, const EntityFilter& filter) const;

    EntityRegistry& m_registry;

    // Per-player interest caches (small: max 16 players)
    std::unordered_map<PlayerID, InterestSet> m_interestCache;
    mutable std::mutex m_interestMutex;
};

} // namespace kmp

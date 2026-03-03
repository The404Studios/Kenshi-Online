#pragma once
#include "kmp/types.h"
#include <vector>
#include <unordered_map>

namespace kmp {

struct ServerEntity; // Forward declaration from server.h

// Entity management utilities for the dedicated server.
// Provides ownership queries, spatial lookups, and validation.
class EntityManager {
public:
    // ── Ownership queries ──

    // Get all entities owned by a specific player.
    static std::vector<EntityID> GetEntitiesByOwner(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        PlayerID owner);

    // Count entities owned by a specific player.
    static size_t CountByOwner(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        PlayerID owner);

    // Check if a player owns a specific entity.
    static bool IsOwnedBy(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        EntityID entityId, PlayerID playerId);

    // ── Spatial queries ──

    // Get all entities within a zone (exact match).
    static std::vector<EntityID> GetEntitiesInZone(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        ZoneCoord zone);

    // Get all entities within adjacent zones (3x3 grid).
    static std::vector<EntityID> GetEntitiesNearZone(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        ZoneCoord center);

    // Get entities within a radius of a world position.
    static std::vector<EntityID> GetEntitiesInRadius(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        Vec3 center, float radius);

    // Find the nearest entity to a position (optionally filter by type).
    static EntityID FindNearest(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        Vec3 position, EntityType filterType = EntityType::NPC,
        bool filterByType = false);

    // ── Validation ──

    // Check if adding another entity for a player would exceed the per-player limit.
    static bool WouldExceedLimit(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        PlayerID owner, size_t maxPerPlayer = 64);

    // Get entity type distribution (count per type).
    static std::unordered_map<EntityType, size_t> GetTypeDistribution(
        const std::unordered_map<EntityID, ServerEntity>& entities);
};

} // namespace kmp

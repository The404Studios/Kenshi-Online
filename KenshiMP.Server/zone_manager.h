#pragma once
#include "kmp/types.h"
#include <vector>
#include <unordered_map>

namespace kmp {

struct ServerEntity;   // Forward from server.h
struct ConnectedPlayer; // Forward from server.h

// Zone-based interest management for the dedicated server.
// Tracks zone populations and provides efficient spatial queries
// for broadcasting position updates only to relevant players.
class ZoneManager {
public:
    // ── Zone population tracking ──

    // Rebuild zone population index from entity map.
    void RebuildIndex(const std::unordered_map<EntityID, ServerEntity>& entities);

    // Get the number of entities in a specific zone.
    size_t GetZonePopulation(ZoneCoord zone) const;

    // Get all populated zones.
    std::vector<ZoneCoord> GetPopulatedZones() const;

    // ── Interest management ──

    // Get entity IDs that are relevant to a player (in adjacent zones).
    // Excludes the player's own entities.
    static std::vector<EntityID> GetRelevantEntities(
        const std::unordered_map<EntityID, ServerEntity>& entities,
        const ConnectedPlayer& player);

    // Check if a player should receive updates about an entity.
    static bool ShouldReceiveUpdates(
        const ConnectedPlayer& player,
        const ServerEntity& entity);

    // ── Zone transitions ──

    // Check if a player has changed zones. Returns true if zone changed.
    static bool HasChangedZone(ZoneCoord oldZone, ZoneCoord newZone);

    // Get the zone for a world position.
    static ZoneCoord GetZoneForPosition(Vec3 position);

    // Get all zones within render distance of a position.
    static std::vector<ZoneCoord> GetAdjacentZones(ZoneCoord center);

private:
    // Zone -> entity count index (rebuilt periodically)
    std::unordered_map<uint64_t, size_t> m_zonePopulation;

    static uint64_t ZoneKey(ZoneCoord zone) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(zone.x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(zone.y));
    }
};

} // namespace kmp

#include "zone_manager.h"
#include "server.h"

namespace kmp {

// ── Zone population tracking ──

void ZoneManager::RebuildIndex(
    const std::unordered_map<EntityID, ServerEntity>& entities) {

    m_zonePopulation.clear();
    for (auto& [id, entity] : entities) {
        m_zonePopulation[ZoneKey(entity.zone)]++;
    }
}

size_t ZoneManager::GetZonePopulation(ZoneCoord zone) const {
    auto it = m_zonePopulation.find(ZoneKey(zone));
    return it != m_zonePopulation.end() ? it->second : 0;
}

std::vector<ZoneCoord> ZoneManager::GetPopulatedZones() const {
    std::vector<ZoneCoord> zones;
    for (auto& [key, count] : m_zonePopulation) {
        if (count > 0) {
            int32_t x = static_cast<int32_t>(key >> 32);
            int32_t y = static_cast<int32_t>(key & 0xFFFFFFFF);
            zones.push_back(ZoneCoord(x, y));
        }
    }
    return zones;
}

// ── Interest management ──

std::vector<EntityID> ZoneManager::GetRelevantEntities(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    const ConnectedPlayer& player) {

    std::vector<EntityID> result;
    for (auto& [id, entity] : entities) {
        if (entity.owner == player.id) continue; // Skip own entities
        if (player.zone.IsAdjacent(entity.zone)) {
            result.push_back(id);
        }
    }
    return result;
}

bool ZoneManager::ShouldReceiveUpdates(
    const ConnectedPlayer& player,
    const ServerEntity& entity) {

    if (entity.owner == player.id) return false;
    return player.zone.IsAdjacent(entity.zone);
}

// ── Zone transitions ──

bool ZoneManager::HasChangedZone(ZoneCoord oldZone, ZoneCoord newZone) {
    return oldZone.x != newZone.x || oldZone.y != newZone.y;
}

ZoneCoord ZoneManager::GetZoneForPosition(Vec3 position) {
    return ZoneCoord::FromWorldPos(position);
}

std::vector<ZoneCoord> ZoneManager::GetAdjacentZones(ZoneCoord center) {
    std::vector<ZoneCoord> zones;
    zones.reserve(9);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            zones.push_back(ZoneCoord(center.x + dx, center.y + dy));
        }
    }
    return zones;
}

} // namespace kmp

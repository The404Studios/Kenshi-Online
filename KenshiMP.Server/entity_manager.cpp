#include "entity_manager.h"
#include "server.h"
#include <cmath>
#include <limits>

namespace kmp {

// ── Ownership queries ──

std::vector<EntityID> EntityManager::GetEntitiesByOwner(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    PlayerID owner) {

    std::vector<EntityID> result;
    for (auto& [id, entity] : entities) {
        if (entity.owner == owner) {
            result.push_back(id);
        }
    }
    return result;
}

size_t EntityManager::CountByOwner(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    PlayerID owner) {

    size_t count = 0;
    for (auto& [id, entity] : entities) {
        if (entity.owner == owner) count++;
    }
    return count;
}

bool EntityManager::IsOwnedBy(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    EntityID entityId, PlayerID playerId) {

    auto it = entities.find(entityId);
    return it != entities.end() && it->second.owner == playerId;
}

// ── Spatial queries ──

std::vector<EntityID> EntityManager::GetEntitiesInZone(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    ZoneCoord zone) {

    std::vector<EntityID> result;
    for (auto& [id, entity] : entities) {
        if (entity.zone.x == zone.x && entity.zone.y == zone.y) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityID> EntityManager::GetEntitiesNearZone(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    ZoneCoord center) {

    std::vector<EntityID> result;
    for (auto& [id, entity] : entities) {
        if (center.IsAdjacent(entity.zone)) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityID> EntityManager::GetEntitiesInRadius(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    Vec3 center, float radius) {

    float radiusSq = radius * radius;
    std::vector<EntityID> result;
    for (auto& [id, entity] : entities) {
        float dx = entity.position.x - center.x;
        float dy = entity.position.y - center.y;
        float dz = entity.position.z - center.z;
        if (dx * dx + dy * dy + dz * dz <= radiusSq) {
            result.push_back(id);
        }
    }
    return result;
}

EntityID EntityManager::FindNearest(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    Vec3 position, EntityType filterType, bool filterByType) {

    EntityID nearest = 0;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (auto& [id, entity] : entities) {
        if (filterByType && entity.type != filterType) continue;

        float dx = entity.position.x - position.x;
        float dy = entity.position.y - position.y;
        float dz = entity.position.z - position.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = id;
        }
    }
    return nearest;
}

// ── Validation ──

bool EntityManager::WouldExceedLimit(
    const std::unordered_map<EntityID, ServerEntity>& entities,
    PlayerID owner, size_t maxPerPlayer) {

    return CountByOwner(entities, owner) >= maxPerPlayer;
}

std::unordered_map<EntityType, size_t> EntityManager::GetTypeDistribution(
    const std::unordered_map<EntityID, ServerEntity>& entities) {

    std::unordered_map<EntityType, size_t> dist;
    for (auto& [id, entity] : entities) {
        dist[entity.type]++;
    }
    return dist;
}

} // namespace kmp

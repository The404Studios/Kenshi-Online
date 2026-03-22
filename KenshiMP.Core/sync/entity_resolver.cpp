#include "entity_resolver.h"
#include "kmp/constants.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace kmp {

EntityResolver::EntityResolver(EntityRegistry& registry)
    : m_registry(registry) {}

// ---- Internal Filter Matching ----

bool EntityResolver::MatchesFilter(const EntityInfo& info, const EntityFilter& filter) const {
    if (filter.owner != INVALID_PLAYER && info.ownerPlayerId != filter.owner)
        return false;
    if (filter.useZone && !(info.zone == filter.zone))
        return false;
    if (filter.useType && info.type != filter.type)
        return false;
    if (filter.useState && info.state != filter.state)
        return false;
    if (filter.localOnly && info.isRemote)
        return false;
    if (filter.remoteOnly && !info.isRemote)
        return false;
    if (filter.dirtyMask != Dirty_None && (info.dirtyFlags & filter.dirtyMask) == 0)
        return false;
    return true;
}

// ---- Compound Queries ----

std::vector<EntityID> EntityResolver::Query(const EntityFilter& filter) const {
    std::vector<EntityID> result;

    // If filtering by zone, use the registry's zone query as a starting point
    if (filter.useZone) {
        auto zoneEntities = m_registry.GetEntitiesInZone(filter.zone);
        for (EntityID id : zoneEntities) {
            auto infoCopy = m_registry.GetInfoCopy(id);
            if (infoCopy && MatchesFilter(*infoCopy, filter)) {
                result.push_back(id);
            }
        }
        return result;
    }

    // If filtering by owner, use the registry's player query
    if (filter.owner != INVALID_PLAYER) {
        auto playerEntities = m_registry.GetPlayerEntities(filter.owner);
        for (EntityID id : playerEntities) {
            auto infoCopy = m_registry.GetInfoCopy(id);
            if (infoCopy && MatchesFilter(*infoCopy, filter)) {
                result.push_back(id);
            }
        }
        return result;
    }

    // If filtering remote only, use the registry's remote query
    if (filter.remoteOnly) {
        auto remoteEntities = m_registry.GetRemoteEntities();
        for (EntityID id : remoteEntities) {
            auto infoCopy = m_registry.GetInfoCopy(id);
            if (infoCopy && MatchesFilter(*infoCopy, filter)) {
                result.push_back(id);
            }
        }
        return result;
    }

    // General case: scan all entities
    // Use GetPlayerEntities with a broad approach — iterate through what we have.
    // EntityRegistry doesn't expose a full iteration API, so we rely on zone/player queries.
    // For the general case, get all entities via GetEntityCount and known queries.
    // Since we can't iterate all entities directly, use a combination approach:
    // Get local entities (owner = any local) + remote entities
    auto remotes = m_registry.GetRemoteEntities();
    for (EntityID id : remotes) {
        auto infoCopy = m_registry.GetInfoCopy(id);
        if (infoCopy && MatchesFilter(*infoCopy, filter)) {
            result.push_back(id);
        }
    }

    // Also check non-remote entities that weren't in the remote list
    // Since EntityRegistry doesn't expose a full iterator, we rely on the fact that
    // local entities are tracked by player ID. For general queries without an owner filter,
    // we'd need to know all player IDs. This is a known limitation.
    // For now, the filter-by-owner and filter-by-zone paths cover the main use cases.
    // The localOnly path below uses GetPlayerEntities if a localPlayerId is known.
    if (filter.localOnly) {
        // Caller should use the owner filter for best results
        spdlog::debug("EntityResolver::Query: localOnly without owner filter — results may be incomplete");
    }

    return result;
}

size_t EntityResolver::Count(const EntityFilter& filter) const {
    return Query(filter).size();
}

// ---- Spatial Queries ----

std::vector<EntityID> EntityResolver::InRadius(const Vec3& center, float radius) const {
    std::vector<EntityID> result;
    float radiusSq = radius * radius;

    // Get entities in the center zone and adjacent zones
    ZoneCoord centerZone = ZoneCoord::FromWorldPos(center, KMP_ZONE_SIZE);
    auto candidates = InZoneNeighborhood(centerZone);

    for (EntityID id : candidates) {
        auto infoCopy = m_registry.GetInfoCopy(id);
        if (!infoCopy) continue;
        float dx = infoCopy->lastPosition.x - center.x;
        float dy = infoCopy->lastPosition.y - center.y;
        float dz = infoCopy->lastPosition.z - center.z;
        if (dx * dx + dy * dy + dz * dz <= radiusSq) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityID> EntityResolver::InZoneNeighborhood(const ZoneCoord& center) const {
    std::vector<EntityID> result;
    for (int dx = -KMP_INTEREST_RADIUS; dx <= KMP_INTEREST_RADIUS; dx++) {
        for (int dy = -KMP_INTEREST_RADIUS; dy <= KMP_INTEREST_RADIUS; dy++) {
            ZoneCoord z{center.x + dx, center.y + dy};
            auto zoneEntities = m_registry.GetEntitiesInZone(z);
            result.insert(result.end(), zoneEntities.begin(), zoneEntities.end());
        }
    }
    return result;
}

// ---- Interest Management ----

InterestSet EntityResolver::ComputeInterest(PlayerID playerId, const ZoneCoord& playerZone) {
    InterestSet newSet;
    newSet.playerId = playerId;
    newSet.playerZone = playerZone;

    // Gather all entities in the 3x3 interest grid
    newSet.entities = InZoneNeighborhood(playerZone);

    // Compute enter/leave deltas against cached set
    std::lock_guard<std::mutex> lock(m_interestMutex);

    auto it = m_interestCache.find(playerId);
    if (it != m_interestCache.end()) {
        const auto& oldSet = it->second;

        // Build set of old entity IDs for fast lookup
        std::unordered_set<EntityID> oldIds(oldSet.entities.begin(), oldSet.entities.end());
        std::unordered_set<EntityID> newIds(newSet.entities.begin(), newSet.entities.end());

        // Entering: in new but not old
        for (EntityID id : newSet.entities) {
            if (oldIds.find(id) == oldIds.end()) {
                newSet.enteringView.push_back(id);
            }
        }

        // Leaving: in old but not new
        for (EntityID id : oldSet.entities) {
            if (newIds.find(id) == newIds.end()) {
                newSet.leavingView.push_back(id);
            }
        }
    } else {
        // First computation — everything is entering
        newSet.enteringView = newSet.entities;
    }

    // Cache the new set
    m_interestCache[playerId] = newSet;
    return newSet;
}

const InterestSet* EntityResolver::GetInterest(PlayerID playerId) const {
    std::lock_guard<std::mutex> lock(m_interestMutex);
    auto it = m_interestCache.find(playerId);
    if (it != m_interestCache.end()) {
        return &it->second;
    }
    return nullptr;
}

void EntityResolver::ClearInterest(PlayerID playerId) {
    std::lock_guard<std::mutex> lock(m_interestMutex);
    m_interestCache.erase(playerId);
}

// ---- Dirty Flag Management ----

void EntityResolver::MarkDirty(EntityID id, uint16_t flags) {
    m_registry.SetDirtyFlags(id, flags);
}

std::vector<EntityID> EntityResolver::ConsumeDirty(uint16_t mask) {
    std::vector<EntityID> result;

    // Scan remote entities for dirty flags matching mask
    auto remotes = m_registry.GetRemoteEntities();
    for (EntityID id : remotes) {
        auto infoCopy = m_registry.GetInfoCopy(id);
        if (infoCopy && (infoCopy->dirtyFlags & mask) != 0) {
            result.push_back(id);
            m_registry.ClearDirtyFlags(id, mask);
        }
    }
    return result;
}

bool EntityResolver::IsDirty(EntityID id, uint16_t mask) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return (infoCopy->dirtyFlags & mask) != 0;
}

// ---- Lifecycle Validation ----

bool EntityResolver::CanSync(EntityID id) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return infoCopy->state == EntityState::Active || infoCopy->state == EntityState::Spawning;
}

bool EntityResolver::CanDespawn(EntityID id) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return infoCopy->state == EntityState::Active;
}

bool EntityResolver::IsAlive(EntityID id) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return infoCopy->state == EntityState::Active && infoCopy->gameObject != nullptr;
}

// ---- Ownership ----

bool EntityResolver::IsLocallyOwned(EntityID id, PlayerID localPlayer) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return infoCopy->ownerPlayerId == localPlayer;
}

bool EntityResolver::IsServerOwned(EntityID id) const {
    auto infoCopy = m_registry.GetInfoCopy(id);
    if (!infoCopy) return false;
    return infoCopy->ownerPlayerId == 0;
}

void EntityResolver::TransferToServer(PlayerID fromPlayer) {
    auto entities = m_registry.GetPlayerEntities(fromPlayer);
    for (EntityID id : entities) {
        m_registry.UpdateOwner(id, 0);
    }
    if (!entities.empty()) {
        spdlog::info("EntityResolver: Transferred {} entities from player {} to server",
                     entities.size(), fromPlayer);
    }
}

} // namespace kmp

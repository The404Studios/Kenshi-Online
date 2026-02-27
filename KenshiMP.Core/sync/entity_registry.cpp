#include "entity_registry.h"
#include <spdlog/spdlog.h>

namespace kmp {

EntityID EntityRegistry::Register(void* gameObject, EntityType type, PlayerID owner) {
    std::unique_lock lock(m_mutex);

    // Check if already registered
    auto it = m_ptrToId.find(gameObject);
    if (it != m_ptrToId.end()) return it->second;

    EntityID id = m_nextId++;
    EntityInfo info;
    info.netId = id;
    info.gameObject = gameObject;
    info.type = type;
    info.ownerPlayerId = owner;
    info.isRemote = false;

    m_entities[id] = info;
    m_ptrToId[gameObject] = id;

    return id;
}

EntityID EntityRegistry::RegisterRemote(EntityID netId, EntityType type,
                                        PlayerID owner, const Vec3& pos) {
    std::unique_lock lock(m_mutex);

    EntityInfo info;
    info.netId = netId;
    info.gameObject = nullptr; // Will be set when local entity is created
    info.type = type;
    info.ownerPlayerId = owner;
    info.lastPosition = pos;
    info.isRemote = true;
    info.zone = ZoneCoord::FromWorldPos(pos);

    m_entities[netId] = info;

    // Ensure our local ID counter stays ahead of server IDs
    if (netId >= m_nextId) m_nextId = netId + 1;

    return netId;
}

void* EntityRegistry::GetGameObject(EntityID netId) const {
    std::shared_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    return it != m_entities.end() ? it->second.gameObject : nullptr;
}

EntityID EntityRegistry::GetNetId(void* gameObject) const {
    std::shared_lock lock(m_mutex);
    auto it = m_ptrToId.find(gameObject);
    return it != m_ptrToId.end() ? it->second : INVALID_ENTITY;
}

const EntityInfo* EntityRegistry::GetInfo(EntityID netId) const {
    std::shared_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    return it != m_entities.end() ? &it->second : nullptr;
}

void EntityRegistry::SetGameObject(EntityID netId, void* gameObject) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        // Remove old pointer mapping if any
        if (it->second.gameObject) {
            m_ptrToId.erase(it->second.gameObject);
        }
        it->second.gameObject = gameObject;
        if (gameObject) {
            m_ptrToId[gameObject] = netId;
        }
    }
}

void EntityRegistry::UpdatePosition(EntityID netId, const Vec3& pos) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second.lastPosition = pos;
        it->second.zone = ZoneCoord::FromWorldPos(pos);
    }
}

void EntityRegistry::UpdateRotation(EntityID netId, const Quat& rot) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second.lastRotation = rot;
    }
}

void EntityRegistry::Unregister(EntityID netId) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        if (it->second.gameObject) {
            m_ptrToId.erase(it->second.gameObject);
        }
        m_entities.erase(it);
    }
}

void EntityRegistry::RemoveEntitiesInZone(const ZoneCoord& zone) {
    std::unique_lock lock(m_mutex);
    std::vector<EntityID> toRemove;
    for (auto& [id, info] : m_entities) {
        if (info.zone == zone && info.isRemote) {
            toRemove.push_back(id);
        }
    }
    for (EntityID id : toRemove) {
        auto it = m_entities.find(id);
        if (it != m_entities.end()) {
            if (it->second.gameObject) m_ptrToId.erase(it->second.gameObject);
            m_entities.erase(it);
        }
    }
}

std::vector<EntityID> EntityRegistry::GetPlayerEntities(PlayerID playerId) const {
    std::shared_lock lock(m_mutex);
    std::vector<EntityID> result;
    for (auto& [id, info] : m_entities) {
        if (info.ownerPlayerId == playerId) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityID> EntityRegistry::GetEntitiesInZone(const ZoneCoord& zone) const {
    std::shared_lock lock(m_mutex);
    std::vector<EntityID> result;
    for (auto& [id, info] : m_entities) {
        if (info.zone == zone) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<EntityID> EntityRegistry::GetRemoteEntities() const {
    std::shared_lock lock(m_mutex);
    std::vector<EntityID> result;
    for (auto& [id, info] : m_entities) {
        if (info.isRemote) result.push_back(id);
    }
    return result;
}

size_t EntityRegistry::GetEntityCount() const {
    std::shared_lock lock(m_mutex);
    return m_entities.size();
}

size_t EntityRegistry::GetRemoteCount() const {
    std::shared_lock lock(m_mutex);
    size_t count = 0;
    for (auto& [_, info] : m_entities) {
        if (info.isRemote) count++;
    }
    return count;
}

void EntityRegistry::Clear() {
    std::unique_lock lock(m_mutex);
    m_entities.clear();
    m_ptrToId.clear();
}

} // namespace kmp

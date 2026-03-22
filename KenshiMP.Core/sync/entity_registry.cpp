#include "entity_registry.h"
#include <spdlog/spdlog.h>

namespace kmp {

EntityID EntityRegistry::Register(void* gameObject, EntityType type, PlayerID owner) {
    // Reject SSO string data, stack addresses, and other non-heap pointers.
    // Threshold 0x10000 (64KB) matches all spawn/hijack paths in the codebase.
    if (gameObject) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(gameObject);
        if (addr < 0x10000 || addr > 0x00007FFFFFFFFFFF || (addr & 0x3) != 0) {
            spdlog::error("EntityRegistry: Register REJECTED invalid pointer 0x{:X}", addr);
            return INVALID_ENTITY;
        }
    }

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
    info.state = EntityState::Active;
    info.authority = AuthorityType::Local;

    m_entities[id] = info;
    m_ptrToId[gameObject] = id;

    return id;
}

EntityID EntityRegistry::RegisterRemote(EntityID netId, EntityType type,
                                        PlayerID owner, const Vec3& pos) {
    std::unique_lock lock(m_mutex);

    // Check if already registered (duplicate spawn message)
    auto existing = m_entities.find(netId);
    if (existing != m_entities.end()) {
        spdlog::warn("EntityRegistry: RegisterRemote called for already-registered entity {} "
                      "(state={}, hasObj={})", netId,
                      static_cast<int>(existing->second.state),
                      existing->second.gameObject != nullptr);
        return netId;
    }

    EntityInfo info;
    info.netId = netId;
    info.gameObject = nullptr; // Will be set when local entity is created
    info.type = type;
    info.ownerPlayerId = owner;
    info.lastPosition = pos;
    info.isRemote = true;
    info.state = EntityState::Spawning;
    info.authority = AuthorityType::Remote;
    info.zone = ZoneCoord::FromWorldPos(pos);

    m_entities[netId] = info;

    // Ensure our local ID counter stays ahead of server IDs
    if (netId >= m_nextId) m_nextId = netId + 1;

    spdlog::info("EntityRegistry: RegisterRemote entity {} (owner={}, pos=({:.0f},{:.0f},{:.0f}), state=Spawning)",
                 netId, owner, pos.x, pos.y, pos.z);

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

std::optional<EntityInfo> EntityRegistry::GetInfoCopy(EntityID netId) const {
    std::shared_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) return it->second;
    return std::nullopt;
}

void EntityRegistry::SetGameObject(EntityID netId, void* gameObject) {
    std::unique_lock lock(m_mutex);

    // Reject SSO string data, stack addresses, and other non-heap pointers.
    // Threshold 0x10000 (64KB) matches all spawn/hijack paths in the codebase.
    // SSO strings have values like 0x656E6F ("one") which are well below 0x10000.
    if (gameObject) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(gameObject);
        if (addr < 0x10000 || addr > 0x00007FFFFFFFFFFF || (addr & 0x3) != 0) {
            spdlog::error("EntityRegistry: SetGameObject({}) REJECTED invalid pointer 0x{:X} "
                          "(SSO string or stack address)", netId, addr);
            gameObject = nullptr;
        }
    }

    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        // Remove old pointer mapping if any
        if (it->second.gameObject) {
            m_ptrToId.erase(it->second.gameObject);
        }
        it->second.gameObject = gameObject;
        if (gameObject) {
            m_ptrToId[gameObject] = netId;
            // Transition: Spawning → Active once game object is linked
            if (it->second.state == EntityState::Spawning) {
                it->second.state = EntityState::Active;
                spdlog::info("EntityRegistry: Entity {} transitioned Spawning -> Active "
                             "(gameObject=0x{:X}, owner={})",
                             netId, reinterpret_cast<uintptr_t>(gameObject),
                             it->second.ownerPlayerId);
            }
        }
    } else {
        spdlog::warn("EntityRegistry: SetGameObject called for unknown entity {} "
                      "(gameObject=0x{:X})", netId,
                      reinterpret_cast<uintptr_t>(gameObject));
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

void EntityRegistry::UpdateOwner(EntityID netId, PlayerID newOwner) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second.ownerPlayerId = newOwner;
    }
}

void EntityRegistry::UpdateEquipment(EntityID netId, int slot, uint32_t itemTemplateId) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end() && slot >= 0 && slot < 14) {
        it->second.lastEquipment[slot] = itemTemplateId;
    }
}

void EntityRegistry::SetDirtyFlags(EntityID netId, uint16_t flags) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second.dirtyFlags |= flags;
    }
}

void EntityRegistry::ClearDirtyFlags(EntityID netId, uint16_t mask) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(netId);
    if (it != m_entities.end()) {
        it->second.dirtyFlags &= ~mask;
    }
}

bool EntityRegistry::RemapEntityId(EntityID oldId, EntityID newId) {
    std::unique_lock lock(m_mutex);
    auto it = m_entities.find(oldId);
    if (it == m_entities.end()) return false;
    if (m_entities.count(newId) > 0) return false; // newId already taken

    EntityInfo info = it->second;
    info.netId = newId;

    // Update ptr→id mapping
    if (info.gameObject) {
        m_ptrToId[info.gameObject] = newId;
    }

    m_entities.erase(it);
    m_entities[newId] = info;

    // Keep our local ID counter ahead of server IDs
    if (newId >= m_nextId) m_nextId = newId + 1;

    return true;
}

EntityID EntityRegistry::FindLocalEntityNear(const Vec3& pos, PlayerID owner, float maxDist) const {
    std::shared_lock lock(m_mutex);
    EntityID bestId = INVALID_ENTITY;
    float bestDist = maxDist;
    for (auto& [id, info] : m_entities) {
        if (info.isRemote) continue;
        if (info.ownerPlayerId != owner) continue;
        float d = info.lastPosition.DistanceTo(pos);
        if (d < bestDist) {
            bestDist = d;
            bestId = id;
        }
    }
    return bestId;
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

size_t EntityRegistry::GetSpawnedRemoteCount() const {
    std::shared_lock lock(m_mutex);
    size_t count = 0;
    for (auto& [_, info] : m_entities) {
        if (info.isRemote && info.gameObject != nullptr) count++;
    }
    return count;
}

size_t EntityRegistry::ClearRemoteEntities() {
    std::unique_lock lock(m_mutex);
    std::vector<EntityID> toRemove;
    for (auto& [id, info] : m_entities) {
        if (info.isRemote) toRemove.push_back(id);
    }
    for (EntityID id : toRemove) {
        auto it = m_entities.find(id);
        if (it != m_entities.end()) {
            if (it->second.gameObject) m_ptrToId.erase(it->second.gameObject);
            m_entities.erase(it);
        }
    }
    return toRemove.size();
}

void EntityRegistry::Clear() {
    std::unique_lock lock(m_mutex);
    m_entities.clear();
    m_ptrToId.clear();
}

} // namespace kmp

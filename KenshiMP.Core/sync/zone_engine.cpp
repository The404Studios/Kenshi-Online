#include "zone_engine.h"
#include "kmp/protocol.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace kmp {

const std::vector<EntityID> ZoneEngine::s_emptyList;

ZoneEngine::ZoneEngine(EntityRegistry& registry)
    : m_registry(registry) {}

// ---- Local Player Zone Tracking ----

bool ZoneEngine::UpdateLocalPlayerZone(const Vec3& position) {
    ZoneCoord newZone = ZoneCoord::FromWorldPos(position, KMP_ZONE_SIZE);
    if (newZone == m_localZone) return false;

    m_prevLocalZone = m_localZone;
    m_localZone = newZone;

    spdlog::debug("ZoneEngine: Player moved to zone ({}, {})", newZone.x, newZone.y);

    // Fire transition callback
    if (m_transitionCb) {
        ZoneTransition leave;
        leave.type = ZoneTransition::Type::Leave;
        leave.zone = m_prevLocalZone;
        leave.entityId = INVALID_ENTITY;
        leave.playerId = INVALID_PLAYER; // local player
        m_transitionCb(leave);

        ZoneTransition enter;
        enter.type = ZoneTransition::Type::Enter;
        enter.zone = m_localZone;
        enter.entityId = INVALID_ENTITY;
        enter.playerId = INVALID_PLAYER;
        m_transitionCb(enter);
    }

    // Notify server of zone change
    NotifyZoneChange();

    return true;
}

std::vector<ZoneCoord> ZoneEngine::GetInterestZones() const {
    std::vector<ZoneCoord> zones;
    zones.reserve(9);
    for (int dx = -KMP_INTEREST_RADIUS; dx <= KMP_INTEREST_RADIUS; dx++) {
        for (int dy = -KMP_INTEREST_RADIUS; dy <= KMP_INTEREST_RADIUS; dy++) {
            zones.emplace_back(m_localZone.x + dx, m_localZone.y + dy);
        }
    }
    return zones;
}

// ---- Cached Zone Indices ----

void ZoneEngine::RebuildZoneIndex() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_zoneEntities.clear();

    // Gather entities from all interest zones
    // We scan the interest zones and any zones that have entities
    auto interestZones = GetInterestZones();
    for (const auto& zone : interestZones) {
        auto entities = m_registry.GetEntitiesInZone(zone);
        if (!entities.empty()) {
            m_zoneEntities[ZoneKey(zone)] = std::move(entities);
        }
    }
}

const std::vector<EntityID>& ZoneEngine::GetEntitiesInZone(const ZoneCoord& zone) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_zoneEntities.find(ZoneKey(zone));
    if (it != m_zoneEntities.end()) {
        return it->second;
    }
    return s_emptyList;
}

std::vector<ZoneCoord> ZoneEngine::GetPopulatedZones() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ZoneCoord> result;
    for (const auto& [key, entities] : m_zoneEntities) {
        if (!entities.empty()) {
            int32_t x = static_cast<int32_t>(key >> 32);
            int32_t y = static_cast<int32_t>(key & 0xFFFFFFFF);
            result.emplace_back(x, y);
        }
    }
    return result;
}

size_t ZoneEngine::GetZonePopulation(const ZoneCoord& zone) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_zoneEntities.find(ZoneKey(zone));
    if (it != m_zoneEntities.end()) {
        return it->second.size();
    }
    return 0;
}

// ---- Player-Zone Binding ----

void ZoneEngine::UpdatePlayerZone(PlayerID playerId, const ZoneCoord& zone) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove from old zone's player list
    auto oldIt = m_playerZones.find(playerId);
    if (oldIt != m_playerZones.end()) {
        ZoneCoord oldZone = oldIt->second;
        if (oldZone == zone) return; // No change

        uint64_t oldKey = ZoneKey(oldZone);
        auto& oldPlayers = m_zonePlayers[oldKey];
        oldPlayers.erase(
            std::remove(oldPlayers.begin(), oldPlayers.end(), playerId),
            oldPlayers.end());
        if (oldPlayers.empty()) {
            m_zonePlayers.erase(oldKey);
        }
    }

    // Add to new zone
    m_playerZones[playerId] = zone;
    m_zonePlayers[ZoneKey(zone)].push_back(playerId);
}

std::vector<PlayerID> ZoneEngine::GetPlayersInZone(const ZoneCoord& zone) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_zonePlayers.find(ZoneKey(zone));
    if (it != m_zonePlayers.end()) {
        return it->second;
    }
    return {};
}

ZoneCoord ZoneEngine::GetPlayerZone(PlayerID playerId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_playerZones.find(playerId);
    if (it != m_playerZones.end()) {
        return it->second;
    }
    return ZoneCoord{0, 0};
}

void ZoneEngine::RemovePlayer(PlayerID playerId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_playerZones.find(playerId);
    if (it != m_playerZones.end()) {
        uint64_t key = ZoneKey(it->second);
        auto& players = m_zonePlayers[key];
        players.erase(
            std::remove(players.begin(), players.end(), playerId),
            players.end());
        if (players.empty()) {
            m_zonePlayers.erase(key);
        }
        m_playerZones.erase(it);
    }
}

// ---- Interest Queries ----

bool ZoneEngine::IsInRange(const ZoneCoord& entityZone) const {
    return m_localZone.IsAdjacent(entityZone);
}

bool ZoneEngine::ShouldSync(EntityID entityId) const {
    auto infoCopy = m_registry.GetInfoCopy(entityId);
    if (!infoCopy) return false;
    return m_localZone.IsAdjacent(infoCopy->zone);
}

// ---- Networking ----

void ZoneEngine::NotifyZoneChange() {
    if (!m_sendFn) return;

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_ZoneRequest);
    writer.WriteI32(m_localZone.x);
    writer.WriteI32(m_localZone.y);
    m_sendFn(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, true);

    spdlog::info("ZoneEngine: Sent zone request ({}, {}) to server", m_localZone.x, m_localZone.y);
}

// ---- Reset ----

void ZoneEngine::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localZone = ZoneCoord{0, 0};
    m_prevLocalZone = ZoneCoord{0, 0};
    m_zoneEntities.clear();
    m_playerZones.clear();
    m_zonePlayers.clear();
}

} // namespace kmp

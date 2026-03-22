#include "sync_facilitator.h"
#include <spdlog/spdlog.h>

namespace kmp {

SyncFacilitator& SyncFacilitator::Get() {
    static SyncFacilitator instance;
    return instance;
}

void SyncFacilitator::Bind(SyncOrchestrator* orchestrator, EntityRegistry* registry,
                            Interpolation* interpolation, SpawnManager* spawnManager) {
    m_orch = orchestrator;
    m_registry = registry;
    m_interpolation = interpolation;
    m_spawnManager = spawnManager;
    spdlog::info("SyncFacilitator: Bound to orchestrator");
}

void SyncFacilitator::Unbind() {
    m_orch = nullptr;
    m_registry = nullptr;
    m_interpolation = nullptr;
    m_spawnManager = nullptr;
    spdlog::info("SyncFacilitator: Unbound");
}

// ════════════════════════════════════════════════════════════════
// Entity Operations
// ════════════════════════════════════════════════════════════════

bool SyncFacilitator::IsEntityRelevant(EntityID id) const {
    if (!m_orch) return false;
    return m_orch->GetZoneEngine().ShouldSync(id);
}

bool SyncFacilitator::IsEntityAlive(EntityID id) const {
    if (!m_orch) return false;
    return m_orch->GetResolver().IsAlive(id);
}

bool SyncFacilitator::IsOwnedByLocal(EntityID id) const {
    if (!m_orch || !m_registry) return false;
    return m_orch->GetResolver().IsLocallyOwned(id,
        m_orch->GetPlayerEngine().GetLocalPlayerId());
}

std::vector<EntityID> SyncFacilitator::GetEntitiesNear(const Vec3& pos, float radius,
                                                         PlayerID ownerFilter) const {
    if (!m_orch) return {};

    auto entities = m_orch->GetResolver().InRadius(pos, radius);
    if (ownerFilter == INVALID_PLAYER) return entities;

    // Filter by owner
    std::vector<EntityID> filtered;
    for (EntityID id : entities) {
        auto infoCopy = m_registry->GetInfoCopy(id);
        if (infoCopy && infoCopy->ownerPlayerId == ownerFilter) {
            filtered.push_back(id);
        }
    }
    return filtered;
}

std::vector<EntityID> SyncFacilitator::GetRelevantPlayerEntities(PlayerID playerId) const {
    if (!m_orch || !m_registry) return {};

    auto entities = m_registry->GetPlayerEntities(playerId);
    std::vector<EntityID> relevant;
    for (EntityID id : entities) {
        if (m_orch->GetZoneEngine().ShouldSync(id)) {
            relevant.push_back(id);
        }
    }
    return relevant;
}

void SyncFacilitator::MarkEntityDirty(EntityID id, uint16_t dirtyFlags) {
    if (!m_orch) return;
    m_orch->GetResolver().MarkDirty(id, dirtyFlags);
}

std::vector<EntityID> SyncFacilitator::ConsumeDirtyEntities(uint16_t mask) {
    if (!m_orch) return {};
    return m_orch->GetResolver().ConsumeDirty(mask);
}

// ════════════════════════════════════════════════════════════════
// Player Operations
// ════════════════════════════════════════════════════════════════

std::string SyncFacilitator::GetPlayerName(PlayerID id) const {
    if (!m_orch) return "";

    auto& pe = m_orch->GetPlayerEngine();

    // Check if it's the local player
    if (id == pe.GetLocalPlayerId()) {
        return pe.GetLocalPlayerName();
    }

    // Check remote players
    auto* session = pe.GetSession(id);
    if (session) return session->name;
    return "Player_" + std::to_string(id);
}

bool SyncFacilitator::IsPlayerNearby(PlayerID id) const {
    if (!m_orch) return false;
    ZoneCoord playerZone = m_orch->GetZoneEngine().GetPlayerZone(id);
    ZoneCoord localZone = m_orch->GetZoneEngine().GetLocalZone();
    return localZone.IsAdjacent(playerZone);
}

std::vector<PlayerID> SyncFacilitator::GetNearbyPlayers() const {
    if (!m_orch) return {};

    std::vector<PlayerID> nearby;
    ZoneCoord localZone = m_orch->GetZoneEngine().GetLocalZone();
    auto zones = m_orch->GetZoneEngine().GetInterestZones();

    for (const auto& zone : zones) {
        auto players = m_orch->GetZoneEngine().GetPlayersInZone(zone);
        nearby.insert(nearby.end(), players.begin(), players.end());
    }

    // Remove duplicates (shouldn't happen, but defensive)
    std::sort(nearby.begin(), nearby.end());
    nearby.erase(std::unique(nearby.begin(), nearby.end()), nearby.end());
    return nearby;
}

PlayerState SyncFacilitator::GetPlayerState(PlayerID id) const {
    if (!m_orch) return PlayerState::Disconnected;
    return m_orch->GetPlayerEngine().GetState(id);
}

PlayerID SyncFacilitator::FindPlayer(const std::string& partialName) const {
    if (!m_orch) return INVALID_PLAYER;
    return m_orch->GetPlayerEngine().FindByName(partialName);
}

// ════════════════════════════════════════════════════════════════
// Zone Operations
// ════════════════════════════════════════════════════════════════

ZoneCoord SyncFacilitator::GetLocalZone() const {
    if (!m_orch) return ZoneCoord{0, 0};
    return m_orch->GetZoneEngine().GetLocalZone();
}

std::vector<EntityID> SyncFacilitator::GetInterestEntities() const {
    if (!m_orch) return {};

    ZoneCoord localZone = m_orch->GetZoneEngine().GetLocalZone();
    return m_orch->GetResolver().InZoneNeighborhood(localZone);
}

SyncFacilitator::ZoneStats SyncFacilitator::GetLocalZoneStats() const {
    ZoneStats stats = {};
    if (!m_orch) return stats;

    stats.localZone = m_orch->GetZoneEngine().GetLocalZone();
    stats.localZonePopulation = m_orch->GetZoneEngine().GetZonePopulation(stats.localZone);

    auto zones = m_orch->GetZoneEngine().GetInterestZones();
    for (const auto& z : zones) {
        size_t pop = m_orch->GetZoneEngine().GetZonePopulation(z);
        stats.interestPopulation += pop;
        if (pop > 0) stats.populatedZoneCount++;
    }
    return stats;
}

// ════════════════════════════════════════════════════════════════
// Spawn Operations
// ════════════════════════════════════════════════════════════════

size_t SyncFacilitator::GetPendingSpawnCount() const {
    if (!m_spawnManager) return 0;
    return m_spawnManager->GetPendingSpawnCount();
}

bool SyncFacilitator::IsSpawnReady() const {
    if (!m_spawnManager) return false;
    return m_spawnManager->IsReady();
}

// ════════════════════════════════════════════════════════════════
// Event Notifications
// ════════════════════════════════════════════════════════════════

void SyncFacilitator::OnEntityPositionChanged(EntityID id, const Vec3& newPos) {
    if (!m_registry || !m_orch) return;
    m_registry->UpdatePosition(id, newPos);
    m_orch->GetResolver().MarkDirty(id, Dirty_Position);
}

void SyncFacilitator::OnPlayerActivity(PlayerID id) {
    if (!m_orch) return;
    m_orch->GetPlayerEngine().RecordActivity(id);
}

void SyncFacilitator::OnPlayerPositionKnown(PlayerID id, const Vec3& pos) {
    if (!m_orch) return;
    ZoneCoord zone = ZoneCoord::FromWorldPos(pos, KMP_ZONE_SIZE);
    m_orch->GetPlayerEngine().UpdatePlayerPosition(id, pos, zone);
    m_orch->GetZoneEngine().UpdatePlayerZone(id, zone);
}

} // namespace kmp

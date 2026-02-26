#include "entity_registry.h"
#include "kmp/types.h"
#include "kmp/constants.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace kmp {

// Zone-based interest management.
// The world is a grid of zones. Each client only receives updates for
// entities in zones adjacent to the client's current zone (3x3 grid).
// This dramatically reduces bandwidth for distant entities.

class ZoneInterestManager {
public:
    static ZoneInterestManager& Get() {
        static ZoneInterestManager instance;
        return instance;
    }

    // Update the local player's zone based on camera/character position
    void UpdateLocalZone(const Vec3& position) {
        ZoneCoord newZone = ZoneCoord::FromWorldPos(position, KMP_ZONE_SIZE);
        if (newZone != m_localZone) {
            spdlog::debug("ZoneInterest: Player moved to zone ({}, {})", newZone.x, newZone.y);
            m_localZone = newZone;
            RebuildInterestSet();
        }
    }

    // Check if an entity is within our interest range
    bool IsInRange(const ZoneCoord& entityZone) const {
        return m_localZone.IsAdjacent(entityZone);
    }

    // Check if a specific entity should be synced to us
    bool ShouldSync(EntityID entityId, const EntityRegistry& registry) const {
        auto* info = registry.GetInfo(entityId);
        if (!info) return false;
        return m_localZone.IsAdjacent(info->zone);
    }

    // Get the set of zones we're interested in (3x3 around player)
    std::vector<ZoneCoord> GetInterestZones() const {
        std::vector<ZoneCoord> zones;
        for (int dx = -KMP_INTEREST_RADIUS; dx <= KMP_INTEREST_RADIUS; dx++) {
            for (int dy = -KMP_INTEREST_RADIUS; dy <= KMP_INTEREST_RADIUS; dy++) {
                zones.emplace_back(m_localZone.x + dx, m_localZone.y + dy);
            }
        }
        return zones;
    }

    ZoneCoord GetLocalZone() const { return m_localZone; }

private:
    ZoneInterestManager() = default;

    void RebuildInterestSet() {
        // This is called when the player changes zones.
        // In the future, we could notify the server to update our subscription.
    }

    ZoneCoord m_localZone;
};

} // namespace kmp

#include "entity_registry.h"
#include "kmp/types.h"
#include "kmp/constants.h"
#include "kmp/protocol.h"
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <functional>

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

    // Set the callback used to send packets (avoids circular dependency with Core)
    using SendFn = std::function<void(const uint8_t* data, size_t size, int channel, bool reliable)>;
    void SetSendCallback(SendFn fn) { m_sendFn = std::move(fn); }

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
        auto infoCopy = registry.GetInfoCopy(entityId);
        if (!infoCopy) return false;
        return m_localZone.IsAdjacent(infoCopy->zone);
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
        // Notify the server that we've changed zones so it can send us
        // entities in our new interest area.
        if (!m_sendFn) return;

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_ZoneRequest);
        writer.WriteI32(m_localZone.x);
        writer.WriteI32(m_localZone.y);
        m_sendFn(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, true);

        spdlog::info("ZoneInterest: Sent zone request ({}, {}) to server", m_localZone.x, m_localZone.y);
    }

    ZoneCoord m_localZone;
    SendFn m_sendFn;
};

} // namespace kmp

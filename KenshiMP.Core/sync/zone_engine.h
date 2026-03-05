#pragma once
#include "entity_registry.h"
#include "kmp/types.h"
#include "kmp/constants.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace kmp {

// Zone transition event
struct ZoneTransition {
    enum class Type { Enter, Leave };
    Type        type;
    ZoneCoord   zone;
    EntityID    entityId;   // INVALID_ENTITY if this is a player transition
    PlayerID    playerId;   // INVALID_PLAYER if this is an entity transition
};

class ZoneEngine {
public:
    explicit ZoneEngine(EntityRegistry& registry);

    // ---- Local Player Zone Tracking ----

    // Called each tick with the local player's world position.
    // Returns true if the player's zone changed.
    bool UpdateLocalPlayerZone(const Vec3& position);

    ZoneCoord GetLocalZone() const { return m_localZone; }

    // Get the 3x3 interest zones around the local player
    std::vector<ZoneCoord> GetInterestZones() const;

    // ---- Cached Zone Indices (rebuilt periodically) ----

    // Rebuild zone caches from the entity registry.
    void RebuildZoneIndex();

    // Get entity IDs in a specific zone (O(1) lookup after rebuild)
    const std::vector<EntityID>& GetEntitiesInZone(const ZoneCoord& zone) const;

    // Get all populated zones
    std::vector<ZoneCoord> GetPopulatedZones() const;

    // Get zone population count
    size_t GetZonePopulation(const ZoneCoord& zone) const;

    // ---- Player-Zone Binding ----

    // Update a remote player's zone (from their position updates)
    void UpdatePlayerZone(PlayerID playerId, const ZoneCoord& zone);

    // Get all players in a specific zone
    std::vector<PlayerID> GetPlayersInZone(const ZoneCoord& zone) const;

    // Get which zone a player is in
    ZoneCoord GetPlayerZone(PlayerID playerId) const;

    // Remove player from zone tracking (on disconnect)
    void RemovePlayer(PlayerID playerId);

    // ---- Interest Queries ----

    // Is this zone in the local player's interest range?
    bool IsInRange(const ZoneCoord& entityZone) const;

    // Should this entity be synced (convenience: checks zone adjacency)
    bool ShouldSync(EntityID entityId) const;

    // ---- Events + Networking ----

    using TransitionCallback = std::function<void(const ZoneTransition&)>;
    void SetTransitionCallback(TransitionCallback cb) { m_transitionCb = std::move(cb); }

    using SendFn = std::function<void(const uint8_t* data, size_t size, int channel, bool reliable)>;
    void SetSendCallback(SendFn fn) { m_sendFn = std::move(fn); }

    // ---- Reset ----
    void Reset();

private:
    void NotifyZoneChange();

    static uint64_t ZoneKey(const ZoneCoord& z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(z.x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(z.y));
    }

    EntityRegistry& m_registry;
    ZoneCoord m_localZone;
    ZoneCoord m_prevLocalZone;

    // Zone -> entity list cache (rebuilt by RebuildZoneIndex)
    std::unordered_map<uint64_t, std::vector<EntityID>> m_zoneEntities;
    static const std::vector<EntityID> s_emptyList;

    // Player -> zone mapping
    std::unordered_map<PlayerID, ZoneCoord> m_playerZones;
    // Zone -> player set (inverse index)
    std::unordered_map<uint64_t, std::vector<PlayerID>> m_zonePlayers;

    TransitionCallback m_transitionCb;
    SendFn m_sendFn;
    mutable std::mutex m_mutex;
};

} // namespace kmp

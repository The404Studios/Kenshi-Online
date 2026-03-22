#include "entity_registry.h"
#include "kmp/types.h"
#include <spdlog/spdlog.h>

namespace kmp {

// Ownership management: determines who controls each entity.
//
// Ownership rules:
// - Player characters: owned by the player who controls them
// - NPCs: owned by the server (host)
// - Buildings: owned by the player who placed them (or server for world buildings)
// - Items on ground: owned by server until picked up
//
// When a player disconnects, their entities transfer to the server.

class OwnershipManager {
public:
    static OwnershipManager& Get() {
        static OwnershipManager instance;
        return instance;
    }

    // Check if the local player owns this entity
    bool IsLocallyOwned(EntityID entityId) const {
        auto infoCopy = m_registry->GetInfoCopy(entityId);
        if (!infoCopy) return false;
        return infoCopy->ownerPlayerId == m_localPlayerId;
    }

    // Check if the server (host) owns this entity
    bool IsServerOwned(EntityID entityId) const {
        auto infoCopy = m_registry->GetInfoCopy(entityId);
        if (!infoCopy) return false;
        return infoCopy->ownerPlayerId == 0;
    }

    // Transfer ownership of all entities from one player to server (owner=0)
    void TransferToServer(PlayerID playerId) {
        auto entities = m_registry->GetPlayerEntities(playerId);
        for (EntityID id : entities) {
            m_registry->UpdateOwner(id, 0);
            spdlog::info("Ownership: Transferred entity {} from player {} to server", id, playerId);
        }
        if (!entities.empty()) {
            spdlog::info("Ownership: Transferred {} entities from player {} to server",
                        entities.size(), playerId);
        }
    }

    void SetRegistry(EntityRegistry* registry) { m_registry = registry; }
    void SetLocalPlayerId(PlayerID id) { m_localPlayerId = id; }

private:
    OwnershipManager() = default;
    EntityRegistry* m_registry = nullptr;
    PlayerID m_localPlayerId = 0;
};

} // namespace kmp

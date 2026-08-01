#include "authority_validator.h"
#include <spdlog/spdlog.h>

namespace kmp {

SnapshotDecision AuthorityValidator::ValidateInboundSnapshot(
    const CharacterPosition& pos,
    uint32_t sourcePlayerId,
    uint32_t myPlayerId,
    EntityRegistry& registry
) {
    // 1. Check if entity exists in registry
    auto infoOpt = registry.GetInfo(pos.entityId);
    if (!infoOpt.has_value()) {
        // Entity not spawned yet → queue for interpolator to handle later
        return SnapshotDecision::QueuePendingSpawn;
    }
    const auto& info = infoOpt.value();

    // 2. Check generation match (Phase 6: ENABLED)
    // Reject packets from old entity generations (prevents ghost control bugs)
    if (info.generation != pos.generation) {
        spdlog::debug("[AuthorityValidator] Stale generation: entity {} has gen {} but packet has gen {}",
                     pos.entityId, info.generation, pos.generation);
        return SnapshotDecision::RejectStaleGeneration;
    }

    // 3. Check if entity is inactive
    if (info.state == EntityState::Inactive) {
        // Entity is not active → reject update
        return SnapshotDecision::RejectDestroyed;
    }

    // 4. Check ownership
    uint32_t ownerPlayerId = registry.GetOwnerPlayerId(pos.entityId);

    // 4a. My own entity coming back from server → reconcile prediction
    if (ownerPlayerId == myPlayerId) {
        return SnapshotDecision::ReconcileLocal;
    }

    // 4b. Source doesn't own this entity → authority violation
    if (ownerPlayerId != sourcePlayerId) {
        spdlog::warn(
            "[AuthorityValidator] Authority violation: entity {} owned by player {} "
            "but update came from player {}",
            pos.entityId, ownerPlayerId, sourcePlayerId
        );
        return SnapshotDecision::RejectAuthorityViolation;
    }

    // 4c. Remote or server-owned entity with valid owner → apply
    if (registry.IsRemote(pos.entityId)) {
        return SnapshotDecision::ApplyRemote;
    }

    // 5. Default: unknown reason → reject
    spdlog::warn(
        "[AuthorityValidator] Unknown validation failure for entity {} from player {}",
        pos.entityId, sourcePlayerId
    );
    return SnapshotDecision::RejectUnknown;
}

bool AuthorityValidator::ShouldSnapToServer(const Vec3& localPos, const Vec3& serverPos, float threshold) {
    return localPos.DistanceTo(serverPos) > threshold;
}

} // namespace kmp

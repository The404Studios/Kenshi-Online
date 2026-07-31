#pragma once
#include "kmp/types.h"
#include "kmp/messages.h"
#include "entity_registry.h"

namespace kmp {

enum class SnapshotDecision {
    ApplyRemote,             // Remote entity, valid owner → interpolate
    ReconcileLocal,          // My own entity from server → reconcile prediction
    QueuePendingSpawn,       // Entity not spawned yet → queue for later
    RejectAuthorityViolation, // Owner mismatch → reject
    RejectEcho,              // Echo of my own data → skip
    RejectStaleGeneration,   // Old generation → reject
    RejectDestroyed,         // Entity destroyed → reject
    RejectUnknown            // Unknown reason → reject
};

class AuthorityValidator {
public:
    // Validate an inbound position update
    static SnapshotDecision ValidateInboundSnapshot(
        const CharacterPosition& pos,
        uint32_t sourcePlayerId,
        uint32_t myPlayerId,
        EntityRegistry& registry
    );

    // Prediction reconciliation decision (Phase 7):
    // Returns true when local position has diverged from the server echo by
    // more than `threshold` units — caller should snap to server authority.
    // Small deltas are tolerated (local player input is authoritative).
    static bool ShouldSnapToServer(const Vec3& localPos, const Vec3& serverPos, float threshold);
};

} // namespace kmp

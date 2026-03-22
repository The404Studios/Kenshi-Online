#include "interpolation.h"
#include <algorithm>
#include <cmath>

namespace kmp {

void Interpolation::AddSnapshot(EntityID entityId, float timestamp,
                                const Vec3& pos, const Quat& rot,
                                uint8_t moveSpeed, uint8_t animState) {
    // Reject NaN/Inf positions — a single bad value corrupts the ring buffer
    // for multiple frames of interpolation and extrapolation.
    if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z) ||
        std::isinf(pos.x) || std::isinf(pos.y) || std::isinf(pos.z)) {
        return;
    }
    // Reject NaN/Inf rotation quaternion components as well
    if (std::isnan(rot.w) || std::isnan(rot.x) || std::isnan(rot.y) || std::isnan(rot.z) ||
        std::isinf(rot.w) || std::isinf(rot.x) || std::isinf(rot.y) || std::isinf(rot.z)) {
        return;
    }

    std::lock_guard lock(m_mutex);
    auto& state = m_entities[entityId];

    // Update jitter estimator with actual packet arrival time
    state.jitter.OnPacketArrived(timestamp);

    // Compute velocity from previous snapshot
    Vec3 velocity(0.f, 0.f, 0.f);
    if (state.count > 0) {
        const Snapshot* prev = state.Get(0); // Most recent
        if (prev) {
            float dt = timestamp - prev->timestamp;
            if (dt > 0.001f && dt < 2.0f) {
                velocity = (pos - prev->position) * (1.f / dt);
            }

            // Check for snap correction: large position discontinuity
            Vec3 expectedPos = prev->position + prev->velocity * dt;
            Vec3 error = pos - expectedPos;
            float errorDist = error.Length();

            if (errorDist > KMP_SNAP_THRESHOLD_MAX) {
                // Teleport — reset snap correction, let position jump
                state.snap.active = false;
            } else if (errorDist > KMP_SNAP_THRESHOLD_MIN) {
                state.snap.StartCorrection(error);
            }
        }
    }

    Snapshot snap;
    snap.timestamp = timestamp;
    snap.tick = 0;
    snap.position = pos;
    snap.velocity = velocity;
    snap.rotation = rot;
    snap.moveSpeed = moveSpeed;
    snap.animState = animState;
    state.PushSnapshot(snap);
}

bool Interpolation::InterpolateEntity(const EntityInterpState& state, float renderTime,
                                       Vec3& outPos, Quat& outRot,
                                       uint8_t* outMoveSpeed, uint8_t* outAnimState) const {
    if (state.count == 0) return false;

    // Use adaptive delay from jitter estimator
    float interpTime = renderTime - state.jitter.GetDelay();

    // Find bracket snapshots (before/after interpTime)
    // Ring buffer is ordered by insertion time; scan all valid entries
    const Snapshot* before = nullptr;
    const Snapshot* after  = nullptr;

    for (int i = 0; i < state.count; i++) {
        const Snapshot* s = state.Get(i);
        if (!s) continue;

        if (s->timestamp <= interpTime) {
            if (!before || s->timestamp > before->timestamp) {
                before = s;
            }
        }
        if (s->timestamp >= interpTime) {
            if (!after || s->timestamp < after->timestamp) {
                after = s;
            }
        }
    }

    // Case 1: No data at all
    if (!before && !after) return false;

    // Case 2: Only future snapshots — use earliest
    if (!before) {
        outPos = after->position;
        outRot = after->rotation;
        if (outMoveSpeed) *outMoveSpeed = after->moveSpeed;
        if (outAnimState) *outAnimState = after->animState;
        return true;
    }

    // Case 3: Past all snapshots — extrapolate (spec §2.5)
    if (!after || after == before) {
        float overshoot = interpTime - before->timestamp;

        if (overshoot > 0.f && overshoot <= KMP_EXTRAP_MAX_SEC && before->velocity.LengthSq() > 0.001f) {
            // Dead reckoning: position + velocity * dt, capped at 250ms
            outPos = before->position + before->velocity * overshoot;
        } else {
            outPos = before->position;
        }
        outRot = before->rotation;
        if (outMoveSpeed) *outMoveSpeed = before->moveSpeed;
        if (outAnimState) *outAnimState = before->animState;
        return true;
    }

    // Case 4: Normal interpolation between bracket snapshots
    float span = after->timestamp - before->timestamp;
    float t = (span > 0.001f) ? (interpTime - before->timestamp) / span : 0.f;
    t = std::clamp(t, 0.f, 1.f);

    // Hermite/cubic would be ideal but linear lerp is good enough for 20Hz
    outPos = before->position + (after->position - before->position) * t;
    outRot = Quat::Slerp(before->rotation, after->rotation, t);

    // Discrete states: use nearest snapshot's values
    if (outMoveSpeed) *outMoveSpeed = (t > 0.5f) ? after->moveSpeed : before->moveSpeed;
    if (outAnimState) *outAnimState = (t > 0.5f) ? after->animState : before->animState;

    return true;
}

bool Interpolation::GetInterpolated(EntityID entityId, float renderTime,
                                    Vec3& outPos, Quat& outRot) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entities.find(entityId);
    if (it == m_entities.end()) return false;

    if (!InterpolateEntity(it->second, renderTime, outPos, outRot, nullptr, nullptr))
        return false;

    // Apply snap correction with real-time decay (mutable state)
    if (it->second.snap.active) {
        Vec3 correction = it->second.snap.Apply(m_deltaTime);
        outPos = outPos - correction;
    }

    return true;
}

bool Interpolation::GetInterpolated(EntityID entityId, float renderTime,
                                    Vec3& outPos, Quat& outRot,
                                    uint8_t& outMoveSpeed, uint8_t& outAnimState) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entities.find(entityId);
    if (it == m_entities.end()) return false;

    if (!InterpolateEntity(it->second, renderTime, outPos, outRot, &outMoveSpeed, &outAnimState))
        return false;

    // Apply snap correction with real-time decay (mutable state)
    if (it->second.snap.active) {
        Vec3 correction = it->second.snap.Apply(m_deltaTime);
        outPos = outPos - correction;
    }

    return true;
}

void Interpolation::RemoveEntity(EntityID entityId) {
    std::lock_guard lock(m_mutex);
    m_entities.erase(entityId);
}

void Interpolation::Clear() {
    std::lock_guard lock(m_mutex);
    m_entities.clear();
    m_currentTime = 0.f;
}

void Interpolation::Update(float deltaTime) {
    std::lock_guard lock(m_mutex);
    m_currentTime += deltaTime;
    m_deltaTime = deltaTime;
    // Note: snap correction timers are ticked inside SnapCorrection::Apply()
    // which is called from GetInterpolated(). No need to tick here — doing
    // both would double-decrement the timer.
}

} // namespace kmp

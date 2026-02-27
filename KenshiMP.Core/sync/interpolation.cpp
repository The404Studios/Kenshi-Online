#include "interpolation.h"
#include <algorithm>

namespace kmp {

void Interpolation::AddSnapshot(EntityID entityId, float timestamp,
                                const Vec3& pos, const Quat& rot,
                                uint8_t moveSpeed, uint8_t animState) {
    std::lock_guard lock(m_mutex);
    auto& deque = m_snapshots[entityId];

    Snapshot snap;
    snap.timestamp = timestamp;
    snap.position = pos;
    snap.rotation = rot;
    snap.moveSpeed = moveSpeed;
    snap.animState = animState;
    deque.push_back(snap);

    // Keep limited buffer
    while (deque.size() > KMP_MAX_SNAPSHOTS) {
        deque.pop_front();
    }
}

// Internal interpolation logic shared by both overloads
static bool InterpolateSnapshots(const std::deque<Interpolation::Snapshot>& snaps,
                                  float interpTime,
                                  Vec3& outPos, Quat& outRot,
                                  uint8_t* outMoveSpeed, uint8_t* outAnimState) {
    const Interpolation::Snapshot* before = nullptr;
    const Interpolation::Snapshot* after = nullptr;

    for (size_t i = 0; i < snaps.size(); i++) {
        if (snaps[i].timestamp <= interpTime) {
            before = &snaps[i];
        }
        if (snaps[i].timestamp >= interpTime && !after) {
            after = &snaps[i];
        }
    }

    if (!before && !after) return false;

    if (!before) {
        outPos = after->position;
        outRot = after->rotation;
        if (outMoveSpeed) *outMoveSpeed = after->moveSpeed;
        if (outAnimState) *outAnimState = after->animState;
        return true;
    }

    if (!after || before == after) {
        outPos = before->position;
        outRot = before->rotation;
        if (outMoveSpeed) *outMoveSpeed = before->moveSpeed;
        if (outAnimState) *outAnimState = before->animState;
        return true;
    }

    // Interpolate between before and after
    float span = after->timestamp - before->timestamp;
    float t = (span > 0.001f) ? (interpTime - before->timestamp) / span : 0.f;
    t = std::clamp(t, 0.f, 1.f);

    // Linear interpolation for position
    outPos = before->position + (after->position - before->position) * t;

    // Spherical interpolation for rotation
    outRot = Quat::Slerp(before->rotation, after->rotation, t);

    // Use the most recent snapshot's movement data (don't interpolate discrete states)
    if (outMoveSpeed) *outMoveSpeed = (t > 0.5f) ? after->moveSpeed : before->moveSpeed;
    if (outAnimState) *outAnimState = (t > 0.5f) ? after->animState : before->animState;

    return true;
}

bool Interpolation::GetInterpolated(EntityID entityId, float renderTime,
                                    Vec3& outPos, Quat& outRot) const {
    std::lock_guard lock(m_mutex);

    auto it = m_snapshots.find(entityId);
    if (it == m_snapshots.end() || it->second.empty()) return false;

    float interpTime = renderTime - KMP_INTERP_DELAY_SEC;
    return InterpolateSnapshots(it->second, interpTime, outPos, outRot, nullptr, nullptr);
}

bool Interpolation::GetInterpolated(EntityID entityId, float renderTime,
                                    Vec3& outPos, Quat& outRot,
                                    uint8_t& outMoveSpeed, uint8_t& outAnimState) const {
    std::lock_guard lock(m_mutex);

    auto it = m_snapshots.find(entityId);
    if (it == m_snapshots.end() || it->second.empty()) return false;

    float interpTime = renderTime - KMP_INTERP_DELAY_SEC;
    return InterpolateSnapshots(it->second, interpTime, outPos, outRot,
                                &outMoveSpeed, &outAnimState);
}

void Interpolation::RemoveEntity(EntityID entityId) {
    std::lock_guard lock(m_mutex);
    m_snapshots.erase(entityId);
}

void Interpolation::Clear() {
    std::lock_guard lock(m_mutex);
    m_snapshots.clear();
    m_currentTime = 0.f;
}

void Interpolation::Update(float deltaTime) {
    m_currentTime += deltaTime;
}

} // namespace kmp

#include "interpolation.h"
#include <algorithm>

namespace kmp {

void Interpolation::AddSnapshot(EntityID entityId, float timestamp,
                                const Vec3& pos, const Quat& rot) {
    std::lock_guard lock(m_mutex);
    auto& deque = m_snapshots[entityId];

    Snapshot snap{timestamp, pos, rot};
    deque.push_back(snap);

    // Keep limited buffer
    while (deque.size() > KMP_MAX_SNAPSHOTS) {
        deque.pop_front();
    }
}

bool Interpolation::GetInterpolated(EntityID entityId, float renderTime,
                                    Vec3& outPos, Quat& outRot) const {
    std::lock_guard lock(m_mutex);

    auto it = m_snapshots.find(entityId);
    if (it == m_snapshots.end() || it->second.empty()) return false;

    const auto& snaps = it->second;

    // Interpolation with delay buffer
    float interpTime = renderTime - KMP_INTERP_DELAY_SEC;

    // Find the two snapshots surrounding interpTime
    const Snapshot* before = nullptr;
    const Snapshot* after = nullptr;

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
        // Only future snapshots - use earliest
        outPos = after->position;
        outRot = after->rotation;
        return true;
    }

    if (!after || before == after) {
        // Only past snapshots - extrapolate or use latest
        outPos = before->position;
        outRot = before->rotation;
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

    return true;
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

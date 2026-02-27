#pragma once
#include "kmp/types.h"
#include "kmp/constants.h"
#include <unordered_map>
#include <deque>
#include <mutex>

namespace kmp {

class Interpolation {
public:
    void AddSnapshot(EntityID entityId, float timestamp, const Vec3& pos, const Quat& rot,
                     uint8_t moveSpeed = 0, uint8_t animState = 0);

    // Get interpolated position/rotation for rendering
    bool GetInterpolated(EntityID entityId, float renderTime,
                         Vec3& outPos, Quat& outRot) const;

    // Get interpolated position/rotation with movement data
    bool GetInterpolated(EntityID entityId, float renderTime,
                         Vec3& outPos, Quat& outRot,
                         uint8_t& outMoveSpeed, uint8_t& outAnimState) const;

    void RemoveEntity(EntityID entityId);
    void Clear();
    void Update(float deltaTime);

    struct Snapshot {
        float   timestamp;
        Vec3    position;
        Quat    rotation;
        uint8_t moveSpeed = 0;
        uint8_t animState = 0;
    };

private:

    mutable std::mutex m_mutex;
    std::unordered_map<EntityID, std::deque<Snapshot>> m_snapshots;
    float m_currentTime = 0.f;
};

} // namespace kmp

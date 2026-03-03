#pragma once
#include "kmp/types.h"
#include "kmp/constants.h"
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace kmp {

// Per-entity jitter estimator — EMA of inter-packet timing variance (spec §2.5)
struct JitterEstimator {
    float lastArrivalTime = 0.f;
    float jitterEma       = 0.02f; // Start at 20ms
    float adaptiveDelay   = KMP_INTERP_DELAY_SEC;
    bool  initialized     = false;

    void OnPacketArrived(float arrivalTime) {
        if (!initialized) {
            lastArrivalTime = arrivalTime;
            initialized = true;
            return;
        }
        float interval = arrivalTime - lastArrivalTime;
        float expectedInterval = KMP_TICK_INTERVAL_SEC; // 50ms at 20Hz
        float jitter = std::abs(interval - expectedInterval);
        jitterEma = KMP_JITTER_EMA_ALPHA * jitter + (1.f - KMP_JITTER_EMA_ALPHA) * jitterEma;
        lastArrivalTime = arrivalTime;

        // Map jitter to delay: 20ms jitter→50ms delay, 80ms jitter→200ms delay
        float t = (jitterEma - 0.02f) / (0.08f - 0.02f);
        t = std::clamp(t, 0.f, 1.f);
        adaptiveDelay = KMP_INTERP_DELAY_MIN + t * (KMP_INTERP_DELAY_MAX - KMP_INTERP_DELAY_MIN);
    }

    float GetDelay() const { return adaptiveDelay; }
};

// Per-entity snap correction state (spec §2.5)
struct SnapCorrection {
    bool  active      = false;
    Vec3  errorOffset;          // Remaining error to blend out
    float blendTimer  = 0.f;    // Time remaining in blend
    float blendTotal  = 0.f;    // Total blend duration

    void StartCorrection(const Vec3& error) {
        float dist = error.Length();
        if (dist > KMP_SNAP_THRESHOLD_MAX) {
            // Teleport — no blend needed, caller handles instant move
            active = false;
            return;
        }
        if (dist > KMP_SNAP_THRESHOLD_MIN) {
            errorOffset = error;
            blendTimer = KMP_SNAP_CORRECT_SEC;
            blendTotal = KMP_SNAP_CORRECT_SEC;
            active = true;
        }
        // Below threshold: no correction needed, normal interpolation handles it
    }

    Vec3 Apply(float dt) {
        if (!active) return Vec3(0.f, 0.f, 0.f);
        blendTimer -= dt;
        if (blendTimer <= 0.f) {
            active = false;
            return Vec3(0.f, 0.f, 0.f);
        }
        // Proportion of error remaining
        float ratio = blendTimer / blendTotal;
        return errorOffset * ratio;
    }
};

class Interpolation {
public:
    struct Snapshot {
        float      timestamp  = 0.f;
        TickNumber tick       = 0;
        Vec3       position;
        Vec3       velocity;   // Computed from consecutive snapshots
        Quat       rotation;
        uint8_t    moveSpeed  = 0;
        uint8_t    animState  = 0;
    };

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

private:
    // Per-entity state
    struct EntityInterpState {
        Snapshot       buffer[KMP_MAX_SNAPSHOTS]; // Ring buffer (spec: 8 entries)
        int            head     = 0;    // Next write index
        int            count    = 0;    // Number of valid entries
        JitterEstimator jitter;
        SnapCorrection  snap;

        void PushSnapshot(const Snapshot& s) {
            buffer[head] = s;
            head = (head + 1) % KMP_MAX_SNAPSHOTS;
            if (count < KMP_MAX_SNAPSHOTS) count++;
        }

        // Get snapshot by age (0=newest, count-1=oldest)
        const Snapshot* Get(int age) const {
            if (age < 0 || age >= count) return nullptr;
            int idx = (head - 1 - age + KMP_MAX_SNAPSHOTS * 2) % KMP_MAX_SNAPSHOTS;
            return &buffer[idx];
        }
    };

    bool InterpolateEntity(const EntityInterpState& state, float renderTime,
                           Vec3& outPos, Quat& outRot,
                           uint8_t* outMoveSpeed, uint8_t* outAnimState) const;

    mutable std::mutex m_mutex;
    mutable std::unordered_map<EntityID, EntityInterpState> m_entities;
    float m_currentTime = 0.f;
    float m_deltaTime   = 0.f;
};

} // namespace kmp

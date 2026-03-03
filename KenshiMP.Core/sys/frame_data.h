#pragma once
#include "kmp/types.h"
#include <cstdint>
#include <vector>

namespace kmp {

// Cached position data read from local entities by background worker
struct CachedEntityPos {
    EntityID netId    = INVALID_ENTITY;
    Vec3     position;
    Quat     rotation;
    float    speed    = 0.f;
    uint8_t  animState = 0;
    bool     dirty    = false; // Moved beyond KMP_POS_CHANGE_THRESHOLD
};

// Interpolated result for a remote entity, computed by background worker
struct CachedRemoteResult {
    EntityID netId     = INVALID_ENTITY;
    Vec3     position;
    Quat     rotation;
    uint8_t  moveSpeed = 0;
    uint8_t  animState = 0;
    bool     valid     = false;
};

// Per-frame double-buffered data container
struct FrameData {
    std::vector<CachedEntityPos>    localEntities;
    std::vector<CachedRemoteResult> remoteResults;
    std::vector<uint8_t>            packetBytes; // Pre-built position update packet

    bool ready = false;

    void Clear() {
        localEntities.clear();
        remoteResults.clear();
        packetBytes.clear();
        ready = false;
    }
};

} // namespace kmp

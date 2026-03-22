#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <chrono>

namespace kmp {

// Session-relative timestamp in seconds (float).
// Uses a static baseline so the value stays small and float-precise.
// raw time_since_epoch() as float loses sub-second precision after ~9h of uptime.
inline float SessionTime() {
    static auto s_baseline = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - s_baseline);
    return static_cast<float>(elapsed.count()) / 1000000.f;
}

using EntityID = uint32_t;
using PlayerID = uint32_t;
using TickNumber = uint32_t;

constexpr EntityID INVALID_ENTITY = 0;
constexpr PlayerID INVALID_PLAYER = 0;

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    float DistanceTo(const Vec3& o) const { return (*this - o).Length(); }
};

struct Quat {
    float w = 1.f, x = 0.f, y = 0.f, z = 0.f;

    Quat() = default;
    Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    // Smallest-three compression: drop largest component, pack 3 into 32 bits
    uint32_t Compress() const {
        float abs_vals[4] = {std::abs(w), std::abs(x), std::abs(y), std::abs(z)};
        int largest = 0;
        for (int i = 1; i < 4; i++) {
            if (abs_vals[i] > abs_vals[largest]) largest = i;
        }

        float comps[4] = {w, x, y, z};
        float sign = comps[largest] < 0.f ? -1.f : 1.f;

        // Pack: 2 bits for index, 10 bits each for 3 remaining components
        // Range [-0.7071, 0.7071] mapped to [0, 1023]
        uint32_t packed = static_cast<uint32_t>(largest) << 30;
        int slot = 0;
        for (int i = 0; i < 4; i++) {
            if (i == largest) continue;
            float val = comps[i] * sign; // Ensure dropped component is positive
            int quantized = static_cast<int>((val + 0.7071068f) / 1.4142136f * 1023.f + 0.5f);
            if (quantized < 0) quantized = 0;
            if (quantized > 1023) quantized = 1023;
            packed |= static_cast<uint32_t>(quantized) << (slot * 10);
            slot++;
        }
        return packed;
    }

    static Quat Decompress(uint32_t packed) {
        int largest = (packed >> 30) & 0x3;
        float comps[4];
        int slot = 0;
        float sumSq = 0.f;
        for (int i = 0; i < 4; i++) {
            if (i == largest) continue;
            int quantized = (packed >> (slot * 10)) & 0x3FF;
            comps[i] = (static_cast<float>(quantized) / 1023.f) * 1.4142136f - 0.7071068f;
            sumSq += comps[i] * comps[i];
            slot++;
        }
        comps[largest] = std::sqrt(std::max(0.f, 1.f - sumSq));
        return {comps[0], comps[1], comps[2], comps[3]};
    }

    static Quat Slerp(const Quat& a, const Quat& b, float t) {
        float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        Quat b2 = b;
        if (dot < 0.f) {
            dot = -dot;
            b2 = {-b.w, -b.x, -b.y, -b.z};
        }
        // Clamp to prevent NaN from acos(>1.0) due to floating-point drift
        if (dot > 1.f) dot = 1.f;
        if (dot >= 0.9995f) {
            // Nlerp fallback — normalize to stay on unit sphere
            Quat r = {
                a.w + t * (b2.w - a.w),
                a.x + t * (b2.x - a.x),
                a.y + t * (b2.y - a.y),
                a.z + t * (b2.z - a.z)
            };
            float len = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
            if (len > 1e-8f) {
                float inv = 1.f / len;
                r.w *= inv; r.x *= inv; r.y *= inv; r.z *= inv;
            }
            return r;
        }
        float theta = std::acos(dot);
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;
        return {
            wa * a.w + wb * b2.w,
            wa * a.x + wb * b2.x,
            wa * a.y + wb * b2.y,
            wa * a.z + wb * b2.z
        };
    }
};

struct ZoneCoord {
    int32_t x = 0, y = 0;

    ZoneCoord() = default;
    ZoneCoord(int32_t x_, int32_t y_) : x(x_), y(y_) {}

    bool operator==(const ZoneCoord& o) const { return x == o.x && y == o.y; }
    bool operator!=(const ZoneCoord& o) const { return !(*this == o); }

    bool IsAdjacent(const ZoneCoord& o) const {
        return std::abs(x - o.x) <= 1 && std::abs(y - o.y) <= 1;
    }

    // Convert world position to zone coordinate
    static ZoneCoord FromWorldPos(const Vec3& pos, float zoneSize = 750.f) {
        // Guard against NaN/Inf — UB to cast to int32_t
        if (!std::isfinite(pos.x) || !std::isfinite(pos.z)) return {0, 0};
        return {
            static_cast<int32_t>(std::floor(pos.x / zoneSize)),
            static_cast<int32_t>(std::floor(pos.z / zoneSize))
        };
    }
};

struct ZoneCoordHash {
    size_t operator()(const ZoneCoord& z) const {
        return std::hash<int64_t>()(static_cast<int64_t>(z.x) << 32 | static_cast<uint32_t>(z.y));
    }
};

// Entity lifecycle state machine (spec §2.2)
enum class EntityState : uint8_t {
    Inactive    = 0, // Not in use
    Spawning    = 1, // Registered, waiting for game object creation
    Active      = 2, // Fully synced and visible
    Despawning  = 3, // Removal in progress
    Frozen      = 4, // Suspended during zone authority handoff
};

// Authority model (spec §2.3)
enum class AuthorityType : uint8_t {
    None         = 0, // No owner — server-managed
    Local        = 1, // This client owns & simulates
    Remote       = 2, // Another client owns, we interpolate
    Host         = 3, // Server/host authoritative
    Transferring = 4, // Authority handoff in progress
};

// Dirty flags for selective replication (spec §2.4)
enum DirtyFlags : uint16_t {
    Dirty_None        = 0x000,
    Dirty_Position    = 0x001,
    Dirty_Rotation    = 0x002,
    Dirty_Animation   = 0x004,
    Dirty_Health      = 0x008,
    Dirty_Stats       = 0x010,
    Dirty_Inventory   = 0x020,
    Dirty_CombatState = 0x040,
    Dirty_LimbDamage  = 0x080,
    Dirty_SquadInfo   = 0x100,
    Dirty_FactionRel  = 0x200,
    Dirty_Equipment   = 0x400,
    Dirty_AIState     = 0x800,
    Dirty_All         = 0xFFF,
};

enum class EntityType : uint8_t {
    PlayerCharacter = 0,
    NPC             = 1,
    Animal          = 2,
    Building        = 3,
    WorldBuilding   = 4,
    Item            = 5,
    Turret          = 6,
};

enum class BodyPart : uint8_t {
    Head      = 0,
    Chest     = 1,
    Stomach   = 2,
    LeftArm   = 3,
    RightArm  = 4,
    LeftLeg   = 5,
    RightLeg  = 6,
    Count     = 7,
};

enum class EquipSlot : uint8_t {
    Weapon    = 0,
    Back      = 1,
    Hair      = 2,
    Hat       = 3,
    Eyes      = 4,
    Body      = 5,
    Legs      = 6,
    Shirt     = 7,
    Boots     = 8,
    Gloves    = 9,
    Neck      = 10,
    Backpack  = 11,
    Beard     = 12,
    Belt      = 13,
    Count     = 14,
};

// Per-limb health data for replication
struct LimbHealth {
    float hp[static_cast<int>(BodyPart::Count)] = {100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f};
};

// Combat state for replication
struct CombatInfo {
    bool     inCombat = false;
    EntityID targetId = INVALID_ENTITY;
    uint8_t  stance   = 0; // 0=passive, 1=melee, 2=ranged, 3=block
};

struct PlayerInfo {
    PlayerID    id = INVALID_PLAYER;
    std::string name;
    uint32_t    ping = 0;
    bool        isHost = false;
};

} // namespace kmp

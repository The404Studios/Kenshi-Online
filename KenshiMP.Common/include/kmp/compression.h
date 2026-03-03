#pragma once
#include "types.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace kmp {

// Half-float (float16) for compact position deltas
inline uint16_t FloatToHalf(float value) {
    uint32_t f;
    std::memcpy(&f, &value, 4);
    uint32_t sign = (f >> 16) & 0x8000;
    int32_t exponent = ((f >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = f & 0x7FFFFF;

    if (exponent <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00); // Inf
    }
    return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
}

inline float HalfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;

    if (exponent == 0) {
        if (mantissa == 0) {
            uint32_t result = sign;
            float f;
            std::memcpy(&f, &result, 4);
            return f;
        }
        // Denormalized
        exponent = 1;
        while (!(mantissa & 0x400)) {
            mantissa <<= 1;
            exponent--;
        }
        mantissa &= 0x3FF;
    } else if (exponent == 31) {
        uint32_t result = sign | 0x7F800000 | (mantissa << 13);
        float f;
        std::memcpy(&f, &result, 4);
        return f;
    }

    uint32_t result = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    float f;
    std::memcpy(&f, &result, 4);
    return f;
}

// Delta position encoding: store offset from last known position as 3x float16
struct DeltaPosition {
    uint16_t dx, dy, dz;

    static DeltaPosition Encode(const Vec3& current, const Vec3& previous) {
        return {
            FloatToHalf(current.x - previous.x),
            FloatToHalf(current.y - previous.y),
            FloatToHalf(current.z - previous.z)
        };
    }

    Vec3 Decode(const Vec3& previous) const {
        return {
            previous.x + HalfToFloat(dx),
            previous.y + HalfToFloat(dy),
            previous.z + HalfToFloat(dz)
        };
    }
};

// Velocity packing: 3x int8 scaled to [-15, 15] m/s
struct PackedVelocity {
    int8_t vx, vy, vz;

    static PackedVelocity Encode(const Vec3& vel) {
        return {
            static_cast<int8_t>(std::clamp(vel.x / 15.f * 127.f, -127.f, 127.f)),
            static_cast<int8_t>(std::clamp(vel.y / 15.f * 127.f, -127.f, 127.f)),
            static_cast<int8_t>(std::clamp(vel.z / 15.f * 127.f, -127.f, 127.f))
        };
    }

    Vec3 Decode() const {
        return {
            static_cast<float>(vx) / 127.f * 15.f,
            static_cast<float>(vy) / 127.f * 15.f,
            static_cast<float>(vz) / 127.f * 15.f
        };
    }
};

} // namespace kmp

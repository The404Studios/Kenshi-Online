#include "game_types.h"
#include "kmp/memory.h"
#include "kmp/constants.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

class WorldAccessor {
public:
    static WorldAccessor& Get() {
        static WorldAccessor instance;
        return instance;
    }

    // ── Read Methods ──

    float GetTimeOfDay() const {
        auto& offsets = GetOffsets();
        if (offsets.world.timeOfDay < 0 || m_worldPtr == 0) return 0.5f;

        float tod = 0.5f;
        Memory::Read(m_worldPtr + offsets.world.timeOfDay, tod);
        return tod;
    }

    float GetGameSpeed() const {
        auto& offsets = GetOffsets();
        if (offsets.world.gameSpeed < 0 || m_worldPtr == 0) return 1.0f;

        float speed = 1.0f;
        Memory::Read(m_worldPtr + offsets.world.gameSpeed, speed);
        return speed;
    }

    int GetWeatherState() const {
        auto& offsets = GetOffsets();
        if (offsets.world.weatherState < 0 || m_worldPtr == 0) return 0;

        int weather = 0;
        Memory::Read(m_worldPtr + offsets.world.weatherState, weather);
        return weather;
    }

    // ── Write Methods (for applying server state) ──

    bool SetTimeOfDay(float timeOfDay) {
        auto& offsets = GetOffsets();
        if (offsets.world.timeOfDay < 0 || m_worldPtr == 0) return false;

        return Memory::Write(m_worldPtr + offsets.world.timeOfDay, timeOfDay);
    }

    bool SetGameSpeed(float speed) {
        auto& offsets = GetOffsets();
        if (offsets.world.gameSpeed < 0 || m_worldPtr == 0) return false;

        return Memory::Write(m_worldPtr + offsets.world.gameSpeed, speed);
    }

    bool SetWeatherState(int weather) {
        auto& offsets = GetOffsets();
        if (offsets.world.weatherState < 0 || m_worldPtr == 0) return false;

        return Memory::Write(m_worldPtr + offsets.world.weatherState, weather);
    }

    // ── Utility ──

    ZoneCoord GetZone(const Vec3& worldPos) const {
        return ZoneCoord::FromWorldPos(worldPos, KMP_ZONE_SIZE);
    }

    // Set the world singleton pointer (found by scanner at init)
    void SetWorldPointer(uintptr_t ptr) {
        m_worldPtr = ptr;
        spdlog::info("WorldAccessor: World pointer set to 0x{:X}", ptr);
    }

    uintptr_t GetWorldPointer() const { return m_worldPtr; }

    // Try to discover the world pointer from known offsets
    bool DiscoverWorldPointer() {
        uintptr_t base = Memory::GetModuleBase();

        // Try the player base chain — the GameWorld singleton is often
        // accessible from the player base or nearby globals.
        // Known pattern: base+01AC8A90 is the player base pointer,
        // and the world singleton is typically at a nearby .data address.
        //
        // For now, we set a null world pointer and rely on the hook-based approach
        // where the time update hook gives us the timeManager pointer directly.
        return m_worldPtr != 0;
    }

private:
    WorldAccessor() = default;
    uintptr_t m_worldPtr = 0;
};

// ── GameWorldAccessor method implementations ──
// These are the public API declared in game_types.h.
// They delegate to the internal WorldAccessor singleton when possible,
// or read from the provided address directly.

float GameWorldAccessor::GetTimeOfDay() const {
    auto& offsets = GetOffsets();
    if (offsets.world.timeOfDay < 0 || m_addr == 0) return 0.5f;

    // Dereference the singleton pointer first
    uintptr_t worldPtr = 0;
    Memory::Read(m_addr, worldPtr);
    if (worldPtr == 0 || worldPtr < 0x10000) return 0.5f;

    float tod = 0.5f;
    Memory::Read(worldPtr + offsets.world.timeOfDay, tod);
    return tod;
}

float GameWorldAccessor::GetGameSpeed() const {
    auto& offsets = GetOffsets();
    if (offsets.world.gameSpeed < 0 || m_addr == 0) return 1.0f;

    uintptr_t worldPtr = 0;
    Memory::Read(m_addr, worldPtr);
    if (worldPtr == 0 || worldPtr < 0x10000) return 1.0f;

    float speed = 1.0f;
    Memory::Read(worldPtr + offsets.world.gameSpeed, speed);
    return speed;
}

int GameWorldAccessor::GetWeatherState() const {
    auto& offsets = GetOffsets();
    if (offsets.world.weatherState < 0 || m_addr == 0) return 0;

    uintptr_t worldPtr = 0;
    Memory::Read(m_addr, worldPtr);
    if (worldPtr == 0 || worldPtr < 0x10000) return 0;

    int weather = 0;
    Memory::Read(worldPtr + offsets.world.weatherState, weather);
    return weather;
}

bool GameWorldAccessor::WriteTimeOfDay(float time) {
    auto& offsets = GetOffsets();
    if (offsets.world.timeOfDay < 0 || m_addr == 0) return false;

    uintptr_t worldPtr = 0;
    Memory::Read(m_addr, worldPtr);
    if (worldPtr == 0 || worldPtr < 0x10000) return false;

    return Memory::Write(worldPtr + offsets.world.timeOfDay, time);
}

bool GameWorldAccessor::WriteGameSpeed(float speed) {
    auto& offsets = GetOffsets();
    if (offsets.world.gameSpeed < 0 || m_addr == 0) return false;

    uintptr_t worldPtr = 0;
    Memory::Read(m_addr, worldPtr);
    if (worldPtr == 0 || worldPtr < 0x10000) return false;

    return Memory::Write(worldPtr + offsets.world.gameSpeed, speed);
}

} // namespace kmp::game

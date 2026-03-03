#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// ── BuildingAccessor method implementations ──
// Buildings in Kenshi use the same MSVC std::string layout as characters.

std::string BuildingAccessor::GetName() const {
    auto& offsets = GetOffsets().building;
    if (offsets.name < 0) return "Unknown Building";

    uintptr_t strAddr = m_ptr + offsets.name;
    uint64_t size = 0, capacity = 0;
    Memory::Read(strAddr + 0x10, size);
    Memory::Read(strAddr + 0x18, capacity);
    if (size == 0 || size > 256) return "Unknown Building";

    char buffer[257] = {};
    if (capacity > 15) {
        uintptr_t dataPtr = 0;
        Memory::Read(strAddr, dataPtr);
        if (dataPtr == 0) return "Unknown Building";
        for (size_t i = 0; i < size && i < 256; i++) {
            Memory::Read(dataPtr + i, buffer[i]);
        }
    } else {
        for (size_t i = 0; i < size && i < 256; i++) {
            Memory::Read(strAddr + i, buffer[i]);
        }
    }
    return std::string(buffer, size);
}

Vec3 BuildingAccessor::GetPosition() const {
    Vec3 pos;
    auto& offsets = GetOffsets().building;
    if (offsets.position >= 0) {
        Memory::ReadVec3(m_ptr + offsets.position, pos.x, pos.y, pos.z);
    }
    return pos;
}

float BuildingAccessor::GetHealth() const {
    auto& offsets = GetOffsets().building;
    if (offsets.health < 0) return 0.f;

    float hp = 0.f;
    Memory::Read(m_ptr + offsets.health, hp);
    return hp;
}

float BuildingAccessor::GetMaxHealth() const {
    auto& offsets = GetOffsets().building;
    if (offsets.maxHealth < 0) return 0.f;

    float maxHp = 0.f;
    Memory::Read(m_ptr + offsets.maxHealth, maxHp);
    return maxHp;
}

bool BuildingAccessor::IsDestroyed() const {
    auto& offsets = GetOffsets().building;
    if (offsets.isDestroyed < 0) {
        // Fallback: check if health <= 0
        float hp = GetHealth();
        float maxHp = GetMaxHealth();
        return maxHp > 0.f && hp <= 0.f;
    }

    bool destroyed = false;
    Memory::Read(m_ptr + offsets.isDestroyed, destroyed);
    return destroyed;
}

float BuildingAccessor::GetBuildProgress() const {
    auto& offsets = GetOffsets().building;
    if (offsets.buildProgress < 0) return 1.0f; // Assume complete if unknown

    float progress = 0.f;
    Memory::Read(m_ptr + offsets.buildProgress, progress);
    return progress;
}

bool BuildingAccessor::IsConstructed() const {
    auto& offsets = GetOffsets().building;
    if (offsets.isConstructed < 0) {
        // Fallback: check if build progress >= 1.0
        return GetBuildProgress() >= 1.0f;
    }

    bool constructed = false;
    Memory::Read(m_ptr + offsets.isConstructed, constructed);
    return constructed;
}

uintptr_t BuildingAccessor::GetOwnerFaction() const {
    auto& offsets = GetOffsets().building;
    if (offsets.ownerFaction < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offsets.ownerFaction, ptr);
    return (ptr > 0x10000 && ptr < 0x00007FFFFFFFFFFF) ? ptr : 0;
}

uintptr_t BuildingAccessor::GetInventoryPtr() const {
    auto& offsets = GetOffsets().building;
    if (offsets.inventory < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offsets.inventory, ptr);
    return (ptr > 0x10000 && ptr < 0x00007FFFFFFFFFFF) ? ptr : 0;
}

} // namespace kmp::game

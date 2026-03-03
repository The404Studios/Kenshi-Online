#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// ── FactionAccessor method implementations ──

std::string FactionAccessor::GetName() const {
    auto& offsets = GetOffsets().faction;
    if (offsets.name < 0) return "Unknown Faction";

    uintptr_t strAddr = m_ptr + offsets.name;
    uint64_t size = 0, capacity = 0;
    Memory::Read(strAddr + 0x10, size);
    Memory::Read(strAddr + 0x18, capacity);
    if (size == 0 || size > 256) return "Unknown Faction";

    char buffer[257] = {};
    if (capacity > 15) {
        uintptr_t dataPtr = 0;
        Memory::Read(strAddr, dataPtr);
        if (dataPtr == 0) return "Unknown Faction";
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

int FactionAccessor::GetMemberCount() const {
    auto& offsets = GetOffsets().faction;
    if (offsets.memberCount < 0) return 0;

    int count = 0;
    Memory::Read(m_ptr + offsets.memberCount, count);
    return (count >= 0 && count < 10000) ? count : 0;
}

bool FactionAccessor::IsPlayerFaction() const {
    auto& offsets = GetOffsets().faction;
    if (offsets.isPlayerFaction < 0) return false;

    bool isPlayer = false;
    Memory::Read(m_ptr + offsets.isPlayerFaction, isPlayer);
    return isPlayer;
}

int FactionAccessor::GetMoney() const {
    auto& offsets = GetOffsets().faction;
    if (offsets.money < 0) return 0;

    int money = 0;
    Memory::Read(m_ptr + offsets.money, money);
    return money;
}

} // namespace kmp::game

#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// ── Runtime Squad Offset Probing ──
// Validates squad struct offsets by checking if the member list and count
// are consistent: memberList should be a valid pointer (or 0), and
// memberCount should be a small positive integer matching the list length.

static bool s_squadProbed = false;

void ProbeSquadOffsets(uintptr_t squadPtr) {
    if (s_squadProbed || squadPtr == 0) return;
    s_squadProbed = true;

    auto& sq = GetOffsets().squad;

    // Verify memberCount: read at the current offset and check if it's a reasonable int (0-256)
    int memberCount = -1;
    if (sq.memberCount >= 0) {
        Memory::Read(squadPtr + sq.memberCount, memberCount);
    }

    // Verify memberList: should be a valid pointer (or null for empty squads)
    uintptr_t memberList = 0;
    if (sq.memberList >= 0) {
        Memory::Read(squadPtr + sq.memberList, memberList);
    }

    bool listValid = (memberList == 0) ||
                     (memberList > 0x10000 && memberList < 0x00007FFFFFFFFFFF);
    bool countValid = (memberCount >= 0 && memberCount < 256);

    if (listValid && countValid) {
        // Cross-validate: if count > 0 and list is valid, first member should be a valid pointer
        bool crossValid = true;
        if (memberCount > 0 && memberList != 0) {
            uintptr_t firstMember = 0;
            Memory::Read(memberList, firstMember);
            crossValid = (firstMember > 0x10000 && firstMember < 0x00007FFFFFFFFFFF);
        }

        if (crossValid) {
            spdlog::info("ProbeSquadOffsets: Verified memberList=0x{:X} memberCount=0x{:X} "
                         "(count={}, list=0x{:X})",
                         sq.memberList, sq.memberCount, memberCount, memberList);
            return; // Offsets are good
        }
    }

    // Offsets seem wrong — scan for the member list/count
    spdlog::debug("ProbeSquadOffsets: Default offsets failed (count={}, list=0x{:X}), scanning...",
                  memberCount, memberList);

    // Scan from 0x20 to 0x100 for a valid pointer + count pattern
    // Pattern: [pointer to array] [int count] at consecutive offsets
    for (int probe = 0x20; probe <= 0x100; probe += 8) {
        uintptr_t candidateList = 0;
        if (!Memory::Read(squadPtr + probe, candidateList)) continue;
        if (candidateList == 0) continue;
        if (candidateList < 0x10000 || candidateList > 0x00007FFFFFFFFFFF) continue;

        // Check if the next 4/8 bytes are a small integer (member count)
        int candidateCount = -1;
        Memory::Read(squadPtr + probe + 8, candidateCount);
        if (candidateCount < 0 || candidateCount > 256) continue;

        // Validate first member in the array
        if (candidateCount > 0) {
            uintptr_t firstMember = 0;
            Memory::Read(candidateList, firstMember);
            if (firstMember < 0x10000 || firstMember > 0x00007FFFFFFFFFFF) continue;
        }

        // Found a valid pattern
        sq.memberList = probe;
        sq.memberCount = probe + 8;
        spdlog::info("ProbeSquadOffsets: Discovered memberList=0x{:X} memberCount=0x{:X} "
                     "(count={}, list=0x{:X})",
                     probe, probe + 8, candidateCount, candidateList);
        return;
    }

    spdlog::debug("ProbeSquadOffsets: Could not verify squad member offsets");
}

// ── SquadAccessor method implementations ──
// Kenshi organizes characters into squads (called "platoons" internally).

std::string SquadAccessor::GetName() const {
    auto& offsets = GetOffsets();
    if (offsets.squad.name < 0) return "Unknown Squad";

    // MSVC std::string reading (same layout as CharacterAccessor::GetName)
    uintptr_t strAddr = m_ptr + offsets.squad.name;
    uint64_t size = 0, capacity = 0;
    Memory::Read(strAddr + 0x10, size);
    Memory::Read(strAddr + 0x18, capacity);
    if (size == 0 || size > 256) return "Unknown Squad";

    char buffer[257] = {};
    if (capacity > 15) {
        uintptr_t dataPtr = 0;
        Memory::Read(strAddr, dataPtr);
        if (dataPtr == 0) return "Unknown Squad";
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

int SquadAccessor::GetMemberCount() const {
    auto& offsets = GetOffsets();
    if (offsets.squad.memberCount < 0) return 0;

    int count = 0;
    Memory::Read(m_ptr + offsets.squad.memberCount, count);
    return (count >= 0 && count < 256) ? count : 0;
}

uintptr_t SquadAccessor::GetMember(int index) const {
    auto& offsets = GetOffsets();
    if (offsets.squad.memberList < 0) return 0;
    if (index < 0 || index >= GetMemberCount()) return 0;

    uintptr_t listPtr = 0;
    Memory::Read(m_ptr + offsets.squad.memberList, listPtr);
    if (listPtr == 0) return 0;

    uintptr_t charPtr = 0;
    Memory::Read(listPtr + index * sizeof(uintptr_t), charPtr);
    return charPtr;
}

uintptr_t SquadAccessor::GetFactionPtr() const {
    auto& offsets = GetOffsets();
    if (offsets.squad.factionId < 0) return 0;

    uintptr_t factionPtr = 0;
    Memory::Read(m_ptr + offsets.squad.factionId, factionPtr);
    return factionPtr;
}

bool SquadAccessor::IsPlayerSquad() const {
    auto& offsets = GetOffsets();
    if (offsets.squad.isPlayerSquad < 0) return false;

    bool isPlayer = false;
    Memory::Read(m_ptr + offsets.squad.isPlayerSquad, isPlayer);
    return isPlayer;
}

} // namespace kmp::game

#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// Squad/platoon wrapper implementation.
// Kenshi organizes characters into squads (called "platoons" internally).
// Each player controls one or more squads.

class SquadAccessor {
public:
    explicit SquadAccessor(uintptr_t ptr) : m_ptr(ptr) {}

    bool IsValid() const { return m_ptr != 0; }

    std::string GetName() const {
        auto& offsets = GetOffsets();
        if (offsets.squad.name < 0) return "Unknown Squad";

        // MSVC std::string reading
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

    int GetMemberCount() const {
        auto& offsets = GetOffsets();
        if (offsets.squad.memberCount < 0) return 0;

        int count = 0;
        Memory::Read(m_ptr + offsets.squad.memberCount, count);
        return (count >= 0 && count < 256) ? count : 0;
    }

    CharacterAccessor GetMember(int index) const {
        auto& offsets = GetOffsets();
        if (offsets.squad.memberList < 0) return CharacterAccessor(nullptr);
        if (index < 0 || index >= GetMemberCount()) return CharacterAccessor(nullptr);

        uintptr_t listPtr = 0;
        Memory::Read(m_ptr + offsets.squad.memberList, listPtr);
        if (listPtr == 0) return CharacterAccessor(nullptr);

        uintptr_t charPtr = 0;
        Memory::Read(listPtr + index * sizeof(uintptr_t), charPtr);
        return CharacterAccessor(reinterpret_cast<void*>(charPtr));
    }

    bool IsPlayerSquad() const {
        auto& offsets = GetOffsets();
        if (offsets.squad.isPlayerSquad < 0) return false;

        bool isPlayer = false;
        Memory::Read(m_ptr + offsets.squad.isPlayerSquad, isPlayer);
        return isPlayer;
    }

    uint32_t GetFactionId() const {
        auto& offsets = GetOffsets();
        if (offsets.squad.factionId < 0) return 0;

        uint32_t factionId = 0;
        Memory::Read(m_ptr + offsets.squad.factionId, factionId);
        return factionId;
    }

private:
    uintptr_t m_ptr;
};

} // namespace kmp::game

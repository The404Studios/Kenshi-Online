#pragma once
#include "kmp/types.h"
#include <string>
#include <atomic>

namespace kmp {

class LobbyManager {
public:
    void OnFactionAssigned(const std::string& factionString, int playerSlot);
    bool ApplyFactionPatch();
    bool HasFaction() const { return m_hasAssignment; }
    int GetPlayerSlot() const { return m_playerSlot; }
    const std::string& GetFactionString() const { return m_factionString; }

private:
    std::string m_factionString;
    int m_playerSlot = -1;
    bool m_hasAssignment = false;
    uintptr_t FindFactionStringAddress();
};

} // namespace kmp

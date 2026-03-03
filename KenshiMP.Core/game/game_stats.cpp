#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// ── StatsAccessor method implementations ──
// Stats are stored inline in the character struct at character+statsOffset.
// Each stat is a float representing the current skill level (0-100).

static float ReadStat(uintptr_t statsPtr, int offset) {
    if (offset < 0 || statsPtr == 0) return 0.f;
    float val = 0.f;
    Memory::Read(statsPtr + offset, val);
    // Sanity check: stats in Kenshi are typically 0-100+
    return (val >= 0.f && val < 1000.f) ? val : 0.f;
}

float StatsAccessor::GetMeleeAttack() const {
    return ReadStat(m_ptr, GetOffsets().stats.meleeAttack);
}

float StatsAccessor::GetMeleeDefence() const {
    return ReadStat(m_ptr, GetOffsets().stats.meleeDefence);
}

float StatsAccessor::GetDodge() const {
    return ReadStat(m_ptr, GetOffsets().stats.dodge);
}

float StatsAccessor::GetMartialArts() const {
    return ReadStat(m_ptr, GetOffsets().stats.martialArts);
}

float StatsAccessor::GetStrength() const {
    return ReadStat(m_ptr, GetOffsets().stats.strength);
}

float StatsAccessor::GetToughness() const {
    return ReadStat(m_ptr, GetOffsets().stats.toughness);
}

float StatsAccessor::GetDexterity() const {
    return ReadStat(m_ptr, GetOffsets().stats.dexterity);
}

float StatsAccessor::GetAthletics() const {
    return ReadStat(m_ptr, GetOffsets().stats.athletics);
}

float StatsAccessor::GetStealth() const {
    return ReadStat(m_ptr, GetOffsets().stats.stealth);
}

float StatsAccessor::GetCrossbows() const {
    return ReadStat(m_ptr, GetOffsets().stats.crossbows);
}

float StatsAccessor::GetMedic() const {
    return ReadStat(m_ptr, GetOffsets().stats.medic);
}

} // namespace kmp::game

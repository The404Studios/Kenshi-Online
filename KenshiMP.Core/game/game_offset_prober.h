#pragma once
#include <cstdint>

namespace kmp::game {

// ═══════════════════════════════════════════════════════════════════════════
//  RUNTIME OFFSET PROBER
// ═══════════════════════════════════════════════════════════════════════════
// Discovers unknown CharacterOffsets at runtime by probing live game objects.
// Runs once after the first valid character is available (post-game-load).
// Results are cached to offset_cache.json and reloaded on next launch if
// the game executable hasn't changed.

// Run the full probe suite on a character pointer. Safe to call repeatedly;
// internally tracks whether probing has already completed.
// Returns true if at least one NEW offset was discovered this call.
// |charPtr| must be a valid KCharacter* with a non-zero position.
// |npcCharPtr| optional NPC character for differential probing (isPlayerControlled).
//              Pass 0 if no NPC is available yet.
bool RunOffsetProber(uintptr_t charPtr, uintptr_t npcCharPtr = 0);

// Try to load previously cached offsets from offset_cache.json.
// Returns true if cache was valid and offsets were restored.
// Call early in startup (before RunOffsetProber).
bool LoadOffsetCache();

// Save currently discovered offsets to offset_cache.json.
// Called automatically by RunOffsetProber after successful discovery.
void SaveOffsetCache();

// Reset all prober state (call on disconnect/reconnect/second game load).
void ResetOffsetProber();

// Returns true if the prober has completed (all feasible offsets discovered or attempted).
bool IsProberComplete();

} // namespace kmp::game

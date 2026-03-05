#pragma once
#include <cstdint>
#include <vector>

namespace kmp::entity_hooks {

// ── Character data cached during game loading ──
// Saved before connection so SendExistingEntitiesToServer has a fallback
// when CharacterIterator fails (PlayerBase/GameWorld not resolved on Steam).
struct CachedCharacter {
    void*     gameObj;
    uintptr_t factionPtr;
    uint32_t  factionId;
    float     x, y, z;
};

bool Install();
void Uninstall();

// Re-enable the CharacterCreate hook after it was suspended during loading.
// Call this when connecting to a server so new character creates are captured.
void ResumeForNetwork();

// Set/clear the direct spawn bypass flag.
// When true, Hook_CharacterCreate skips all spawn/registration logic
// and just passes through to the original function. Used by SpawnManager
// when calling the factory from GameFrameUpdate to avoid recursive spawn logic.
void SetDirectSpawnBypass(bool bypass);

// Check if an in-place replay spawn succeeded recently.
// Used by game_tick_hooks to avoid competing with in-place replay.
bool HasRecentInPlaceSpawn(int withinSeconds = 30);

// Get total number of successful in-place spawns
int GetInPlaceSpawnCount();

// Diagnostic getters (for PipelineOrchestrator snapshot collection)
int  GetTotalCreates();
int  GetTotalDestroys();
bool IsInBurst();
bool IsLoadingComplete();

// ── Loading-phase character cache (fallback for CharacterIterator) ──
// Returns the faction pointer captured from the first character during loading.
// This is available BEFORE connection and works even when PlayerBase fails.
uintptr_t GetCapturedFaction();

// Returns all characters cached during game loading.
// Used by SendExistingEntitiesToServer when CharacterIterator returns 0.
const std::vector<CachedCharacter>& GetLoadingCharacters();

} // namespace kmp::entity_hooks

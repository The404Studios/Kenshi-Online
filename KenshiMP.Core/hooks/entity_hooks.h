#pragma once
#include <cstdint>

namespace kmp::entity_hooks {

bool Install();
void Uninstall();

// Enable the CharacterCreate hook for multiplayer.
// Call this when connecting to a server so new character creates are captured.
void ResumeForNetwork();

// Disable the CharacterCreate hook on disconnect.
// Prevents MovRaxRsp heap corruption from zone-load bursts while not connected.
void SuspendForDisconnect();

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

// Decrement the per-player spawn cap counter when a remote entity dies or despawns.
// Must be called from any code path that removes a remote entity (despawn handler,
// character destroy hook, heartbeat cleanup, etc.) to prevent the cap from saturating.
void DecrementSpawnCount(uint32_t owner);

// Diagnostic getters (for PipelineOrchestrator snapshot collection)
int  GetTotalCreates();
int  GetTotalDestroys();

// ── Loading detection via create events ──
// Returns milliseconds since the last CharacterCreate hook fired (any mode).
// Used by PollForGameLoad to detect when the loading burst has finished
// without needing CharacterIterator (which corrupts the heap during loading).
int64_t GetTimeSinceLastCreate();

// Returns how many characters were created during the current loading burst.
// Resets to 0 when ResetLoadingCreateCount() is called.
int GetLoadingCreateCount();

// Reset the loading create counter (called when transitioning to Loading phase).
void ResetLoadingCreateCount();

// Enable ultra-lightweight passthrough mode for loading.
// When true, Hook_CharacterCreate only updates timestamp/counter and calls original.
// No game memory reads, no faction voting, no entity registration.
void SetLoadingPassthrough(bool enabled);

// Get pointers to mod template characters captured during loading.
// Returns count of captured pointers (up to 16). Fills outPtrs array.
int GetCapturedModTemplates(void** outPtrs, int maxCount);

// Call the factory function DIRECTLY via the raw MinHook trampoline,
// completely bypassing the hook and MovRaxRsp wrapper.
// The raw trampoline starts with `mov rax, rsp` and sets up its own
// frame correctly — no stack swap, no heap corruption.
// Returns the created character or nullptr.
void* CallFactoryDirect(void* factory, void* requestStruct);

// Call RootObjectFactory::create — the high-level dispatcher that builds a request
// struct internally from a GameData* pointer. Bypasses stale-pointer struct clone issue.
// Takes (factory, GameData*) and returns created character, or nullptr.
void* CallFactoryCreate(void* factory, void* gameData);

// Call RootObjectFactory::createRandomChar — creates a random NPC character.
// Takes just factory pointer. Last-resort fallback when templates fail.
void* CallFactoryCreateRandom(void* factory);

// Get the fallback faction pointer (last valid faction seen from any character creation).
// Used by SEH_FixUpFaction_Core when primary character faction isn't available.
// Validates the pointer before returning; returns 0 if stale.
uintptr_t GetFallbackFaction();

// Get the player faction elected during savegame loading via multi-source voting.
// Scans the first N characters and picks the most common faction, with bonus
// weight for characters whose name matches the config playerName and for
// factions with the isPlayerFaction flag set. Returns 0 if not yet elected.
// Validates the pointer before returning; returns 0 if stale.
uintptr_t GetEarlyPlayerFaction();

// Re-validate stored faction pointers. If all are stale (e.g., after a save/load),
// re-scans the local squad to discover a fresh valid faction. Prioritizes
// characters whose name matches config playerName and factions with isPlayerFaction.
// Returns true if a valid faction is available after the call.
bool RevalidateFaction();

// Get the detected GameData pointer offset within the factory request struct.
// Returns -1 if not yet detected.
int GetGameDataOffsetInStruct();

// Get the detected position offset within the factory request struct.
// Returns -1 if not yet detected.
int GetPositionOffsetInStruct();

} // namespace kmp::entity_hooks

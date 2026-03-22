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

// Diagnostic getters (for PipelineOrchestrator snapshot collection)
int  GetTotalCreates();
int  GetTotalDestroys();

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
uintptr_t GetFallbackFaction();

// Get the player faction captured during savegame loading.
// The first character created during load is the player's squad leader — its faction
// is reliably the player's faction. Returns 0 if not captured yet.
uintptr_t GetEarlyPlayerFaction();

// Get the detected GameData pointer offset within the factory request struct.
// Returns -1 if not yet detected.
int GetGameDataOffsetInStruct();

// Get the detected position offset within the factory request struct.
// Returns -1 if not yet detected.
int GetPositionOffsetInStruct();

} // namespace kmp::entity_hooks

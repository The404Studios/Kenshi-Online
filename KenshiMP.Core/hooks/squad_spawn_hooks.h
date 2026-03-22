#pragma once
#include "kmp/types.h"
#include <queue>
#include <mutex>
#include <atomic>

namespace kmp::squad_spawn_hooks {

// Install hooks for squad spawn bypass
bool Install();
void Uninstall();

// Queue a character spawn request. When the game next evaluates squad spawning,
// the hook will force-spawn this character through the natural pipeline.
void QueueSquadSpawn(void* gameData, const Vec3& position);

// Get number of pending squad spawns
int GetPendingCount();

// Get total successful squad bypass spawns
int GetSuccessCount();

} // namespace kmp::squad_spawn_hooks

#pragma once
#include <cstdint>

namespace kmp::squad_hooks {

bool Install();
void Uninstall();

// Suppress network sends during loading
void SetLoading(bool loading);

// Called by packet handler when the server assigns a net ID to a newly created squad.
// Maps the most recently created squad pointer to the server-assigned ID.
void OnSquadNetIdAssigned(uint32_t squadNetId);

// ── Squad Injection ──
// Adds a character to the local player's squad using the engine's own SquadAddMember function.
// This makes the character selectable, orderable, and visible in the squad panel.
// Returns true if injection succeeded, false if squad not found or function unavailable.
bool AddCharacterToLocalSquad(void* character);

} // namespace kmp::squad_hooks

#pragma once
#include <cstdint>

namespace kmp::faction_hooks {

bool Install();
void Uninstall();

// Suppress network sends during loading
void SetLoading(bool loading);

// Suppress network sends during server-sourced faction changes
// (prevents feedback loop when applying incoming S2C_FactionRelation)
void SetServerSourced(bool sourced);

// Get the original FactionRelation function pointer (for calling from packet_handler)
using FactionRelationFn = void(__fastcall*)(void* factionA, void* factionB, float relation);
FactionRelationFn GetOriginal();

} // namespace kmp::faction_hooks

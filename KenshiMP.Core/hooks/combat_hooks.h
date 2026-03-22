#pragma once
#include <cstdint>

namespace kmp::combat_hooks {

bool Install();
void Uninstall();

// Process deferred combat events from the safe game-tick context.
// Called from Core::OnGameTick — NOT from inside a hook.
void ProcessDeferredEvents();

} // namespace kmp::combat_hooks

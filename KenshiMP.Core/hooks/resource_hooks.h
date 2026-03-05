#pragma once

namespace kmp::resource_hooks {

// Attempt to discover and hook Ogre3D resource managers.
// Returns true if at least one hook was installed.
// Returns false if discovery failed (graceful degradation — LoadingOrchestrator
// works without resource hooks, falling back to burst-detection timing).
bool Install();

void Uninstall();

// Whether Ogre resource hooks are active
bool IsActive();

} // namespace kmp::resource_hooks

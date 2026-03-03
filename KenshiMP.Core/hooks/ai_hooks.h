#pragma once
#include <cstdint>

namespace kmp::ai_hooks {

bool Install();
void Uninstall();

// Remote-controlled character tracking.
// Characters marked as remote get their AI decisions overridden (not suppressed).
void MarkRemoteControlled(void* character);
void UnmarkRemoteControlled(void* character);
bool IsRemoteControlled(void* character);

} // namespace kmp::ai_hooks

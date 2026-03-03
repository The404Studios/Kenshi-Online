#pragma once

namespace kmp::render_hooks {

bool Install();
void Uninstall();

// Post a custom message to trigger spawn queue processing from WndProc context.
// The message pump runs BETWEEN frames (not during DX11 Present), so the factory works.
void PostSpawnTrigger();

} // namespace kmp::render_hooks

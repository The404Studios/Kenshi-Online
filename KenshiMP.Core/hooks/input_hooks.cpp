#include "input_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace kmp::input_hooks {

// Input hooks are handled through the WndProc hook in render_hooks.cpp.
// This module handles additional keybind processing.

// Keybinds:
// Tab        - Toggle player list
// Enter      - Toggle chat
// F1         - Toggle connection UI
// Escape     - Close any open overlay panel
// Tilde (~)  - Toggle debug overlay

static bool s_installed = false;

bool Install() {
    // Input is primarily handled via the WndProc hook in render_hooks.
    // Additional OIS hooks can be added here if needed.
    s_installed = true;
    spdlog::info("input_hooks: Installed (using WndProc-based input)");
    return true;
}

void Uninstall() {
    s_installed = false;
}

} // namespace kmp::input_hooks

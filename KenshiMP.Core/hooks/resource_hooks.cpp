#include "resource_hooks.h"
#include "../core.h"
#include "../game/loading_orchestrator.h"
#include "../game/asset_facilitator.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace kmp::resource_hooks {

// ── State ──
static bool s_active = false;

// ═══════════════════════════════════════════════════════════════════════════
// OGRE RESOURCE MANAGER VTable HOOKING
// ═══════════════════════════════════════════════════════════════════════════
//
// Kenshi uses Ogre3D 1.x. Resource loading goes through:
//   - Ogre::MeshManager::load(name, group, ...)
//   - Ogre::TextureManager::load(name, group, ...)
//   - Ogre::MaterialManager::load(name, group, ...)
//
// These are virtual methods on singleton manager objects. We discover the
// singletons by scanning for known Ogre strings in .rdata, finding their
// xrefs in .text, and walking back to the singleton access pattern.
//
// If discovery fails, the LoadingOrchestrator gracefully degrades to
// burst-detection timing (the existing behavior before this system).
// ═══════════════════════════════════════════════════════════════════════════

// Ogre::ResourceManager::load is typically at VTable index 5-8.
// We detect the correct index by looking for the pattern of:
//   - First param (RCX) = this (ResourceManager*)
//   - Second param (RDX) = const String& name
//   - Returns ResourcePtr

// Placeholder for future VTable discovery.
// The actual implementation requires:
//   1. Finding OgreMain.dll or the static Ogre code in kenshi_x64.exe
//   2. Locating MeshManager::getSingletonPtr() or the singleton global
//   3. Reading the VTable pointer from the singleton
//   4. Installing VTable hooks at the correct indices
//
// This is non-trivial RE work that should be done with the game running
// (runtime VTable dump + method call tracing). For now, we provide the
// framework and gracefully degrade.

static bool TryDiscoverOgreManagers() {
    // Step 1: Check if OgreMain.dll is loaded (some Kenshi versions link dynamically)
    HMODULE hOgre = GetModuleHandleA("OgreMain.dll");
    if (hOgre) {
        spdlog::info("resource_hooks: Found OgreMain.dll at 0x{:X}",
                     reinterpret_cast<uintptr_t>(hOgre));
        // TODO: Use GetProcAddress for Ogre::MeshManager::getSingletonPtr()
        // and hook via VTable from there.
        return false; // Not yet implemented
    }

    // Step 2: Ogre is statically linked — scan for singleton patterns in kenshi_x64.exe
    HMODULE hGame = GetModuleHandleA("kenshi_x64.exe");
    if (!hGame) {
        hGame = GetModuleHandleA(nullptr);
    }
    uintptr_t gameBase = reinterpret_cast<uintptr_t>(hGame);

    // Scan .rdata for "MeshManager" string
    // Then find xrefs to it in .text to locate the singleton accessor
    // This requires the PatternScanner infrastructure with PE section enumeration.
    //
    // For now, log and return false — the infrastructure is ready but the
    // Ogre VTable indices need to be determined through runtime analysis.
    spdlog::info("resource_hooks: Game base at 0x{:X} — Ogre VTable discovery not yet implemented",
                 gameBase);

    return false;
}

bool Install() {
    spdlog::info("resource_hooks: Attempting Ogre resource manager discovery...");

    bool discovered = TryDiscoverOgreManagers();
    if (discovered) {
        s_active = true;
        spdlog::info("resource_hooks: Ogre resource hooks installed successfully");
    } else {
        s_active = false;
        spdlog::info("resource_hooks: Ogre discovery failed — graceful degradation "
                     "(LoadingOrchestrator will use burst-detection timing)");
    }

    return s_active;
}

void Uninstall() {
    if (s_active) {
        // Remove VTable hooks if any were installed
        auto& hookMgr = HookManager::Get();
        if (hookMgr.IsInstalled("OgreMeshLoad")) {
            hookMgr.Remove("OgreMeshLoad");
        }
        if (hookMgr.IsInstalled("OgreTextureLoad")) {
            hookMgr.Remove("OgreTextureLoad");
        }
        s_active = false;
        spdlog::info("resource_hooks: Uninstalled");
    }
}

bool IsActive() {
    return s_active;
}

} // namespace kmp::resource_hooks

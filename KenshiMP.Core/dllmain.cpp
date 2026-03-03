#include "core.h"
#include <Windows.h>

// ── Ogre Plugin Interface ──
// Kenshi loads Ogre plugins via Plugins_x64.cfg.
// By adding "Plugin=KenshiMP.Core" to that file, Ogre calls these exports.

static kmp::Core* g_core = nullptr;

extern "C" {

__declspec(dllexport) void dllStartPlugin() {
    g_core = &kmp::Core::Get();
    g_core->Initialize();
}

__declspec(dllexport) void dllStopPlugin() {
    if (g_core) {
        g_core->Shutdown();
        g_core = nullptr;
    }
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // Ogre should call dllStopPlugin, but just in case
        if (g_core) {
            g_core->Shutdown();
            g_core = nullptr;
        }
        break;
    }
    return TRUE;
}

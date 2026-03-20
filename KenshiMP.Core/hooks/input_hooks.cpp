#include "input_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace kmp::input_hooks {

using InjectKeyPressFn   = bool(*)(void*, uint32_t, uint32_t);
using InjectKeyReleaseFn = bool(*)(void*, uint32_t);
using KenshiHotkeyFn     = void(*)(void* self);

static InjectKeyPressFn   s_origInjectKeyPress   = nullptr;
static InjectKeyReleaseFn s_origInjectKeyRelease  = nullptr;
static KenshiHotkeyFn     s_origKenshiHotkey      = nullptr;

static bool s_myguiHooksInstalled  = false;
static bool s_kenshiHookInstalled  = false;

static void* ResolveCodeTarget(void* p) {
    auto* cur = static_cast<uint8_t*>(p);
    if (!cur) return nullptr;
    for (int i = 0; i < 8; ++i) {
        if (cur[0] == 0xE9) { int32_t r = *reinterpret_cast<int32_t*>(cur+1); cur += 5+r; continue; }
        if (cur[0] == 0xEB) { int8_t  r = *reinterpret_cast<int8_t*> (cur+1); cur += 2+r; continue; }
        if (cur[0] == 0xFF && cur[1] == 0x25) {
            int32_t d = *reinterpret_cast<int32_t*>(cur+2);
            cur = *reinterpret_cast<uint8_t**>(cur+6+d);
            continue;
        }
        break;
    }
    return cur;
}

static bool IsModalUiActive() {
    return Core::Get().GetNativeHud().IsChatInputActive();
}

// ── THE REAL FIX ──────────────────────────────────────────────────────────
// Found via: bp GetKeyboardState -> press M -> call stack showed caller at
// kenshi_x64.exe+0x22B3C3, function starts at kenshi_x64.exe+0x22B370
// This is Kenshi's hotkey processing function. When chat is active we skip
// it entirely so no menus can open.
static void Hook_KenshiHotkey(void* self) {
    static int s_logCount = 0;
    if (s_logCount < 5) {
        spdlog::info("input_hooks: KenshiHotkey called modal={}", IsModalUiActive());
        ++s_logCount;
    }

    if (IsModalUiActive()) {
        return; // skip entire hotkey function
    }

    if (s_origKenshiHotkey) s_origKenshiHotkey(self);
}

// ── MyGUI hooks (keep these for text injection side) ──────────────────────
static bool Hook_InjectKeyPress(void* inputMgr, uint32_t keyCode, uint32_t text) {
    if (IsModalUiActive()) return true;
    return s_origInjectKeyPress ? s_origInjectKeyPress(inputMgr, keyCode, text) : false;
}

static bool Hook_InjectKeyRelease(void* inputMgr, uint32_t keyCode) {
    if (IsModalUiActive()) return true;
    return s_origInjectKeyRelease ? s_origInjectKeyRelease(inputMgr, keyCode) : false;
}

// ── Install ───────────────────────────────────────────────────────────────

bool Install() {
    auto& hookMgr = HookManager::Get();

    // ── MyGUI hooks ───────────────────────────────────────────────────────
    if (!s_myguiHooksInstalled) {
        HMODULE mygui = GetModuleHandleA("MyGUIEngine_x64.dll");
        if (!mygui) { spdlog::error("input_hooks: MyGUI not loaded"); return false; }

        auto pPress   = GetProcAddress(mygui, "?injectKeyPress@InputManager@MyGUI@@QEAA_NUKeyCode@2@I@Z");
        auto pRelease = GetProcAddress(mygui, "?injectKeyRelease@InputManager@MyGUI@@QEAA_NUKeyCode@2@@Z");
        if (!pPress || !pRelease) { spdlog::error("input_hooks: MyGUI exports not found"); return false; }

        auto rPress   = ResolveCodeTarget(reinterpret_cast<void*>(pPress));
        auto rRelease = ResolveCodeTarget(reinterpret_cast<void*>(pRelease));

        if (!hookMgr.InstallAt("MyGUI_InjectKeyPress",
                               reinterpret_cast<uintptr_t>(rPress),
                               &Hook_InjectKeyPress, &s_origInjectKeyPress))
            return false;
        if (!hookMgr.InstallAt("MyGUI_InjectKeyRelease",
                               reinterpret_cast<uintptr_t>(rRelease),
                               &Hook_InjectKeyRelease, &s_origInjectKeyRelease))
            return false;

        s_myguiHooksInstalled = true;
        spdlog::info("input_hooks: MyGUI keyboard hooks installed");
    }

    // ── Direct Kenshi hotkey function hook ────────────────────────────────
    // Confirmed address: kenshi_x64.exe + 0x22B370
    // Found via GetKeyboardState breakpoint -> call stack
    if (!s_kenshiHookInstalled) {
        HMODULE exe = GetModuleHandleA(nullptr);
        if (!exe) {
            spdlog::error("input_hooks: Failed to get exe module");
        } else {
            auto pHotkey = reinterpret_cast<void*>(
                reinterpret_cast<uint8_t*>(exe) + 0x82B370);

            spdlog::info("input_hooks: KenshiHotkey target={}", pHotkey);

            // Verify bytes — should be: 40 57 48 83 EC 60 (push rdi / sub rsp,60)
            // from the x64dbg screenshot of 0x7FF6E51BB370
            auto* bytes = reinterpret_cast<uint8_t*>(pHotkey);
            spdlog::info("input_hooks: KenshiHotkey bytes: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                         bytes[0], bytes[1], bytes[2], bytes[3],
                         bytes[4], bytes[5], bytes[6], bytes[7]);

            if (hookMgr.InstallAt("KenshiHotkey",
                                  reinterpret_cast<uintptr_t>(pHotkey),
                                  &Hook_KenshiHotkey,
                                  &s_origKenshiHotkey)) {
                s_kenshiHookInstalled = true;
                spdlog::info("input_hooks: KenshiHotkey hooked at exe+0x22B370");
            } else {
                spdlog::error("input_hooks: Failed to hook KenshiHotkey");
            }
        }
    }

    OutputDebugStringA("KMP: input_hooks Install() complete\n");
    return s_myguiHooksInstalled;
}

// ── Uninstall ─────────────────────────────────────────────────────────────

void Uninstall() {
    auto& hookMgr = HookManager::Get();

    if (s_kenshiHookInstalled) {
        hookMgr.Remove("KenshiHotkey");
        s_kenshiHookInstalled = false;
    }
    if (s_myguiHooksInstalled) {
        hookMgr.Remove("MyGUI_InjectKeyPress");
        hookMgr.Remove("MyGUI_InjectKeyRelease");
        s_myguiHooksInstalled = false;
    }

    s_origInjectKeyPress   = nullptr;
    s_origInjectKeyRelease = nullptr;
    s_origKenshiHotkey     = nullptr;

    spdlog::info("input_hooks: Uninstalled");
}

} // namespace kmp::input_hooks

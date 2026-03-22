#include "kmp/hook_manager.h"
#include "kmp/mov_rax_rsp_fix.h"
#include <spdlog/spdlog.h>
#include <MinHook.h>
#include <Windows.h>
#include <cstring>

namespace kmp {

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

bool HookManager::Initialize() {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return true;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_Initialize failed: {}", MH_StatusToString(status));
        return false;
    }

    m_initialized = true;
    spdlog::info("HookManager: Initialized successfully");
    return true;
}

void HookManager::Shutdown() {
    std::lock_guard lock(m_mutex);
    if (!m_initialized) return;

    // IMPORTANT: Only DISABLE hooks — do NOT call MH_RemoveHook or MH_Uninitialize.
    // MH_Uninitialize frees trampoline memory, but Kenshi's atexit handlers may still
    // call functions through stale pointers that reference those trampolines.
    // MH_DisableHook restores original function bytes so direct calls work normally.
    // The trampoline memory is freed automatically when the process exits.
    MH_DisableHook(MH_ALL_HOOKS);

    // Restore VTable hooks manually (these aren't managed by MH_DisableHook)
    // and free allocated executable stubs
    for (auto& [name, entry] : m_hooks) {
        if (entry.isVtable && entry.vtableAddr) {
            DWORD oldProtect;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          PAGE_EXECUTE_READWRITE, &oldProtect);
            entry.vtableAddr[entry.vtableIndex] = entry.original;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          oldProtect, &oldProtect);
        }
        // Free relay thunks (deprecated, but handle any leftovers)
        if (entry.relayThunk) {
            VirtualFree(entry.relayThunk, 0, MEM_RELEASE);
            spdlog::debug("HookManager: Freed relay thunk for '{}'", name);
        }
        // Free custom caller stubs (deprecated)
        if (entry.customCaller) {
            VirtualFree(entry.customCaller, 0, MEM_RELEASE);
            spdlog::debug("HookManager: Freed custom caller for '{}'", name);
        }
        // Free MovRaxRsp fix stubs
        if (entry.movRaxRspHook.allocBase) {
            FreeMovRaxRspHook(entry.movRaxRspHook);
            spdlog::debug("HookManager: Freed MovRaxRsp fix for '{}'", name);
        }
    }

    m_hooks.clear();
    m_initialized = false;
    spdlog::info("HookManager: Shutdown complete (hooks disabled, trampolines preserved)");
}

// ═══════════════════════════════════════════════════════════════════════════
// RELAY THUNK BUILDER (DEPRECATED — superseded by BuildCustomCaller)
// ═══════════════════════════════════════════════════════════════════════════
//
// The relay thunk approach was REMOVED because `add rax, 8` shifts ALL
// [rax+XX] register saves by one slot, causing stack corruption.
//
// The raw MinHook trampoline was also found to be UNSAFE for CharacterCreate:
// the function does `lea rbp, [rax-0x158]` then uses [rbp+XX] for ALL locals.
// When RAX captures the trampoline's RSP instead of the real caller's RSP,
// RBP points into trampoline memory, causing corruption after ~100 calls.
//
// The fix is BuildCustomCaller (below), which bypasses MinHook's trampoline
// entirely: it does `mov rax, rsp` (capturing the REAL caller's RSP), then
// jumps directly into the original function at offset +3 (past the original's
// own `mov rax, rsp`). This gives the function the correct RAX/RBP values.
//
void* HookManager::BuildRelayThunk(const std::string& name, void* trampoline) {
    (void)name;
    (void)trampoline;
    return nullptr; // Deprecated — use BuildCustomCaller instead
}

// ═══════════════════════════════════════════════════════════════════════════
// CUSTOM CALLER BUILDER (for `mov rax, rsp` functions)
// ═══════════════════════════════════════════════════════════════════════════
//
// Creates a minimal executable stub that bypasses MinHook's trampoline:
//
//   mov rax, rsp           ; 48 8B C4  — capture real caller's RSP
//   jmp qword [rip+0]      ; FF 25 00 00 00 00  — indirect jump
//   dq original_addr + 3   ; 8 bytes: target past original's mov rax,rsp
//
// Total: 17 bytes. The stub is called like a normal function pointer.
// The `call` into the stub pushes a return address (RSP-8), then the stub
// captures that RSP in RAX — identical to what the original function sees
// when called directly. Execution then continues in the original function
// body at offset +3, which begins with `push rbp; push rsi; ...` and
// `lea rbp, [rax-0x158]` — RAX is correct, so RBP is correct.
//
void* HookManager::BuildCustomCaller(const std::string& name, uintptr_t originalAddr) {
    // Allocate executable page
    void* mem = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) {
        spdlog::error("HookManager: VirtualAlloc failed for custom caller '{}': error {}",
                      name, GetLastError());
        return nullptr;
    }

    auto* code = static_cast<uint8_t*>(mem);
    int off = 0;

    // mov rax, rsp  (48 8B C4)
    code[off++] = 0x48;
    code[off++] = 0x8B;
    code[off++] = 0xC4;

    // jmp qword ptr [rip+0]  (FF 25 00 00 00 00)
    code[off++] = 0xFF;
    code[off++] = 0x25;
    code[off++] = 0x00;
    code[off++] = 0x00;
    code[off++] = 0x00;
    code[off++] = 0x00;

    // 8-byte target address: original function + 3 (skip its mov rax, rsp)
    uintptr_t target = originalAddr + 3;
    memcpy(&code[off], &target, 8);
    off += 8;

    // Lock down to execute-read only
    DWORD oldProtect;
    VirtualProtect(mem, 64, PAGE_EXECUTE_READ, &oldProtect);

    spdlog::info("HookManager: Built custom caller for '{}' at 0x{:X} -> 0x{:X} (original+3)",
                 name, reinterpret_cast<uintptr_t>(mem), target);
    return mem;
}

bool HookManager::InstallRaw(const std::string& name, void* target, void* detour, void** original) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        spdlog::error("HookManager: Not initialized when installing '{}'", name);
        return false;
    }

    if (m_hooks.count(name)) {
        spdlog::warn("HookManager: Hook '{}' already installed", name);
        return false;
    }

    // ═══ .PDATA VALIDATION ═══
    // Verify the target is a real function entry point, not a mid-function address.
    // A flawed pattern scanner can give us mid-function addresses (e.g., IsPrologue
    // matching `mov [rsp+0x38], rbx` as a prologue when the stack offset is too deep
    // for a real function entry). Hooking mid-function corrupts the containing function.
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(target);

        // Check 1: .pdata — is the target a RUNTIME_FUNCTION entry point?
        DWORD64 imageBase = 0;
        auto* rtFunc = RtlLookupFunctionEntry(
            reinterpret_cast<DWORD64>(target), &imageBase, nullptr);
        if (rtFunc) {
            uintptr_t funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
            if (funcStart != addr) {
                spdlog::error("HookManager: REFUSING hook '{}' — target 0x{:X} is MID-FUNCTION "
                              "(real function at 0x{:X}, offset +0x{:X}). "
                              "The pattern scanner likely resolved the wrong address.",
                              name, addr, funcStart, addr - funcStart);
                return false;
            }
        }

        // Check 2: Alignment — MSVC often 16-byte aligns function entry points,
        // but NOT always (small functions, COMDAT folding, vtable entries).
        // VTable-discovered functions are definitively valid even if not 16-byte aligned.
        // Log a warning but proceed — MinHook handles the actual hookability.
        if ((addr & 0xF) != 0) {
            spdlog::warn("HookManager: Hook '{}' target 0x{:X} is NOT 16-byte aligned "
                         "(low nibble = 0x{:X}) — may be vtable-discovered function. Proceeding anyway.",
                         name, addr, addr & 0xF);
        }
    }

    // ═══ PROLOGUE ANALYSIS ═══
    auto* bytes = reinterpret_cast<const uint8_t*>(target);
    bool hasMovRaxRsp = (bytes[0] == 0x48 && bytes[1] == 0x8B && bytes[2] == 0xC4);

    spdlog::info("HookManager: '{}' prologue at 0x{:X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}{}",
                 name, reinterpret_cast<uintptr_t>(target),
                 bytes[0], bytes[1], bytes[2], bytes[3],
                 bytes[4], bytes[5], bytes[6], bytes[7],
                 hasMovRaxRsp ? " [mov rax,rsp detected — MovRaxRsp fix will be applied]" : "");

    // ═══ MOV RAX, RSP — NAKED DETOUR + TRAMPOLINE WRAPPER FIX ═══
    //
    // For functions starting with `mov rax, rsp` (48 8B C4), the function
    // derives RBP from RAX and aliases push-saved register slots with
    // [rbp+XX] frame offsets.  Any extra data on the stack shifts pushes
    // by 8 bytes, breaking the aliasing and corrupting all locals.
    //
    // Fix: Two runtime-generated ASM stubs per hook.
    //
    //   NAKED DETOUR (MinHook JMPs here instead of C++ hook):
    //     Captures game's RSP + return address, creates 4KB+8 stack gap,
    //     CALLs the C++ hook, restores game's return address, RETs to game.
    //
    //   TRAMPOLINE WRAPPER (C++ hook calls this as "original"):
    //     Swaps RSP to captured game RSP, patches [RSP] with return_point,
    //     sets RAX = RSP, JMPs to trampoline+3.  Original function runs
    //     with ZERO extra bytes on stack — perfect alignment.
    //
    // This replaces the old "custom caller" approach which captured RSP at
    // the wrong call depth (inside the C++ hook's SEH wrapper), corrupting
    // every [rbp+XX] access in the original function.

    if (hasMovRaxRsp) {
        // Phase 1: Pre-allocate fix page (naked detour address is at known offset)
        void* fixPage = AllocMovRaxRspPage();
        if (!fixPage) {
            spdlog::error("HookManager: AllocMovRaxRspPage failed for '{}': error {}",
                         name, GetLastError());
            return false;
        }
        void* nakedDetourAddr = static_cast<uint8_t*>(fixPage) + MOVRAXRSP_NAKED_OFFSET;

        spdlog::info("HookManager: '{}' allocated MovRaxRsp fix page at 0x{:X}, "
                     "naked detour at 0x{:X}",
                     name, reinterpret_cast<uintptr_t>(fixPage),
                     reinterpret_cast<uintptr_t>(nakedDetourAddr));

        // Phase 2: Create MinHook hook — naked detour is the detour target
        // MinHook will JMP to nakedDetourAddr, which captures RSP and CALLs the real C++ hook.
        void* rawTrampoline = nullptr;
        MH_STATUS status2 = MH_CreateHook(target, nakedDetourAddr, &rawTrampoline);
        if (status2 != MH_OK) {
            spdlog::error("HookManager: MH_CreateHook failed for '{}': {}",
                         name, MH_StatusToString(status2));
            VirtualFree(fixPage, 0, MEM_RELEASE);
            return false;
        }

        // Verify trampoline starts with 48 8B C4
        auto* tp = static_cast<const uint8_t*>(rawTrampoline);
        bool trampolineOk = (tp[0] == 0x48 && tp[1] == 0x8B && tp[2] == 0xC4);

        spdlog::info("HookManager: '{}' trampoline at 0x{:X}: {:02X} {:02X} {:02X} {}",
                     name, reinterpret_cast<uintptr_t>(rawTrampoline),
                     tp[0], tp[1], tp[2],
                     trampolineOk ? "[OK]" : "[UNEXPECTED — not 48 8B C4]");

        if (!trampolineOk) {
            spdlog::error("HookManager: '{}' trampoline doesn't start with 48 8B C4 "
                         "— cannot apply MovRaxRsp fix", name);
            MH_RemoveHook(target);
            VirtualFree(fixPage, 0, MEM_RELEASE);
            return false;
        }

        // Phase 3: Build naked detour + trampoline wrapper into the pre-allocated page
        MovRaxRspHook fix = BuildMovRaxRspHookAt(fixPage, name, detour, rawTrampoline, 3);
        fix.rawTrampoline = rawTrampoline;  // Preserve for reentrant bypass
        if (!fix.trampolineWrapper) {
            spdlog::error("HookManager: BuildMovRaxRspHookAt failed for '{}'", name);
            MH_RemoveHook(target);
            VirtualFree(fixPage, 0, MEM_RELEASE);
            return false;
        }

        // Phase 4: Set *original to trampoline wrapper
        // When the C++ hook calls *original, it enters the wrapper which swaps
        // to the game's stack, sets RAX correctly, and calls the real original.
        if (original) *original = fix.trampolineWrapper;

        // Phase 5: Enable hook
        MH_STATUS enableStatus = MH_EnableHook(target);
        if (enableStatus != MH_OK) {
            spdlog::error("HookManager: MH_EnableHook failed for '{}': {}",
                         name, MH_StatusToString(enableStatus));
            MH_RemoveHook(target);
            FreeMovRaxRspHook(fix);
            return false;
        }

        // Phase 6: Start with bypass=1 (passthrough) — hook is installed but disabled.
        // Caller must explicitly Enable() to activate the hook. This prevents
        // the hook from intercepting calls during loading/startup.
        if (fix.bypassFlag) {
            *fix.bypassFlag = 1;
        }

        // Phase 7: Record
        HookEntry entry;
        entry.name = name;
        entry.target = target;
        entry.detour = detour;
        entry.original = original ? *original : rawTrampoline;
        entry.enabled = false;  // Starts disabled (bypass=1)
        entry.hasMovRaxRsp = true;
        entry.movRaxRspHook = fix;
        entry.customCaller = nullptr;  // deprecated
        memcpy(entry.prologueBytes, bytes, 8);
        m_hooks[name] = std::move(entry);

        spdlog::info("HookManager: Installed hook '{}' at 0x{:X} "
                     "(MovRaxRsp fix — naked detour + trampoline wrapper, starts BYPASSED)",
                     name, reinterpret_cast<uintptr_t>(target));
        return true;
    }

    // ═══ STANDARD HOOK (no mov rax, rsp) ═══
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_CreateHook failed for '{}': {}", name, MH_StatusToString(status));
        return false;
    }

    // ═══ ENABLE HOOK ═══
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_EnableHook failed for '{}': {}", name, MH_StatusToString(status));
        MH_RemoveHook(target);
        return false;
    }

    // ═══ RECORD ENTRY ═══
    HookEntry entry;
    entry.name = name;
    entry.target = target;
    entry.detour = detour;
    entry.original = original ? *original : nullptr;
    entry.enabled = true;
    entry.hasMovRaxRsp = false;
    entry.relayThunk = nullptr;
    entry.customCaller = nullptr;
    memcpy(entry.prologueBytes, bytes, 8);
    m_hooks[name] = std::move(entry);

    spdlog::info("HookManager: Installed hook '{}' at 0x{:X}",
                 name, reinterpret_cast<uintptr_t>(target));
    return true;
}

bool HookManager::Remove(const std::string& name) {
    std::lock_guard lock(m_mutex);

    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return false;

    auto& entry = it->second;

    if (entry.isVtable) {
        // Restore vtable entry
        DWORD oldProtect;
        VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                      PAGE_EXECUTE_READWRITE, &oldProtect);
        entry.vtableAddr[entry.vtableIndex] = entry.original;
        VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                      oldProtect, &oldProtect);
    } else {
        MH_DisableHook(entry.target);
        MH_RemoveHook(entry.target);
    }

    // Free allocated stubs
    if (entry.relayThunk) {
        VirtualFree(entry.relayThunk, 0, MEM_RELEASE);
    }
    if (entry.customCaller) {
        VirtualFree(entry.customCaller, 0, MEM_RELEASE);
    }
    if (entry.movRaxRspHook.allocBase) {
        FreeMovRaxRspHook(entry.movRaxRspHook);
    }

    spdlog::info("HookManager: Removed hook '{}'", name);
    m_hooks.erase(it);
    return true;
}

void HookManager::RemoveAll() {
    // NOTE: Caller must already hold m_mutex (called from Shutdown).
    std::vector<std::string> names;
    for (auto& [name, _] : m_hooks) names.push_back(name);

    for (auto& name : names) {
        auto it = m_hooks.find(name);
        if (it == m_hooks.end()) continue;

        auto& entry = it->second;
        if (entry.isVtable) {
            DWORD oldProtect;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          PAGE_EXECUTE_READWRITE, &oldProtect);
            entry.vtableAddr[entry.vtableIndex] = entry.original;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          oldProtect, &oldProtect);
        } else {
            MH_DisableHook(entry.target);
            MH_RemoveHook(entry.target);
        }

        if (entry.relayThunk) {
            VirtualFree(entry.relayThunk, 0, MEM_RELEASE);
        }
        if (entry.customCaller) {
            VirtualFree(entry.customCaller, 0, MEM_RELEASE);
        }
        if (entry.movRaxRspHook.allocBase) {
            FreeMovRaxRspHook(entry.movRaxRspHook);
        }

        spdlog::info("HookManager: Removed hook '{}'", name);
        m_hooks.erase(it);
    }
}

bool HookManager::Enable(const std::string& name) {
    static int s_enableCount = 0;
    int callNum = ++s_enableCount;
    if (callNum <= 20 || callNum % 1000 == 0) {
        char buf[128];
        sprintf_s(buf, "KMP: HookManager::Enable('%s') #%d\n", name.c_str(), callNum);
        OutputDebugStringA(buf);
    }

    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) {
        OutputDebugStringA("KMP: HookManager::Enable — hook not found!\n");
        return false;
    }

    // For MovRaxRsp hooks: use the software bypass flag instead of MH_EnableHook.
    // MH_EnableHook re-patches function bytes + suspends all threads — unsafe after
    // a Disable/Enable cycle for MovRaxRsp hooks (corrupts the detour chain).
    if (it->second.hasMovRaxRsp && it->second.movRaxRspHook.bypassFlag) {
        *it->second.movRaxRspHook.bypassFlag = 0;  // Clear bypass → hook active
        it->second.enabled = true;
        spdlog::info("HookManager::Enable('{}') — bypass flag cleared (hook active)", name);
        return true;
    }

    if (!it->second.isVtable) {
        MH_STATUS status = MH_EnableHook(it->second.target);
        if (status != MH_OK) {
            char buf[128];
            sprintf_s(buf, "KMP: HookManager::Enable — MH_EnableHook FAILED: %s\n",
                      MH_StatusToString(status));
            OutputDebugStringA(buf);
            return false;
        }
    }
    it->second.enabled = true;
    return true;
}

bool HookManager::Disable(const std::string& name) {
    spdlog::info("HookManager::Disable('{}') called", name);

    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) {
        spdlog::error("HookManager::Disable('{}') — hook NOT FOUND in map (size={})",
                      name, m_hooks.size());
        return false;
    }

    spdlog::info("HookManager::Disable('{}') — found, isVtable={}, enabled={}, target=0x{:X}",
                 name, it->second.isVtable, it->second.enabled,
                 reinterpret_cast<uintptr_t>(it->second.target));

    // For MovRaxRsp hooks: use the software bypass flag instead of MH_DisableHook.
    // MH_DisableHook restores original bytes + suspends all threads — after re-enable
    // via MH_EnableHook, the detour chain can be corrupted for MovRaxRsp hooks.
    // The bypass flag makes the naked detour JMP straight to the raw trampoline
    // (pure passthrough, zero hook overhead, no thread suspension).
    if (it->second.hasMovRaxRsp && it->second.movRaxRspHook.bypassFlag) {
        *it->second.movRaxRspHook.bypassFlag = 1;  // Set bypass → passthrough
        it->second.enabled = false;
        spdlog::info("HookManager::Disable('{}') — bypass flag set (passthrough)", name);
        return true;
    }

    if (!it->second.isVtable) {
        MH_STATUS status = MH_DisableHook(it->second.target);
        if (status != MH_OK) {
            spdlog::error("HookManager::Disable('{}') — MH_DisableHook FAILED: {}",
                          name, MH_StatusToString(status));
            return false;
        }
        spdlog::info("HookManager::Disable('{}') — MH_DisableHook OK", name);
    }
    it->second.enabled = false;
    return true;
}

void* HookManager::GetTarget(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return nullptr;
    return it->second.target;
}

void* HookManager::GetCustomCaller(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return nullptr;
    return it->second.customCaller;
}

void* HookManager::GetRawTrampoline(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return nullptr;
    if (!it->second.hasMovRaxRsp) return nullptr;
    return it->second.movRaxRspHook.rawTrampoline;
}

bool HookManager::IsInstalled(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    return m_hooks.count(name) > 0;
}

size_t HookManager::GetHookCount() const {
    std::lock_guard lock(m_mutex);
    return m_hooks.size();
}

// ── Diagnostics ──

std::vector<HookDiag> HookManager::GetDiagnostics() const {
    std::lock_guard lock(m_mutex);
    std::vector<HookDiag> diags;
    diags.reserve(m_hooks.size());

    for (auto& [name, entry] : m_hooks) {
        HookDiag d;
        d.name = entry.name;
        d.targetAddr = reinterpret_cast<uintptr_t>(entry.target);
        memcpy(d.prologue, entry.prologueBytes, 8);
        d.installed = true;
        d.enabled = entry.enabled;
        d.isVtable = entry.isVtable;
        d.hasRelayThunk = (entry.relayThunk != nullptr);
        d.hasCustomCaller = (entry.customCaller != nullptr || entry.movRaxRspHook.trampolineWrapper != nullptr);
        d.hasMovRaxRspFix = (entry.movRaxRspHook.allocBase != nullptr);
        d.callCount = entry.callCount;
        d.crashCount = entry.crashCount;
        diags.push_back(std::move(d));
    }
    return diags;
}

void HookManager::IncrementCallCount(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it != m_hooks.end()) {
        it->second.callCount++;
    }
}

void HookManager::IncrementCrashCount(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it != m_hooks.end()) {
        it->second.crashCount++;
    }
}

bool HookManager::InstallVTableHook(const std::string& name, void** vtable, int index,
                                    void* detour, void** original) {
    std::lock_guard lock(m_mutex);

    if (m_hooks.count(name)) {
        spdlog::warn("HookManager: VTable hook '{}' already installed", name);
        return false;
    }

    // Save original
    *original = vtable[index];

    // Overwrite vtable entry
    DWORD oldProtect;
    if (!VirtualProtect(vtable + index, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        spdlog::error("HookManager: VirtualProtect failed for vtable hook '{}'", name);
        return false;
    }

    vtable[index] = detour;
    VirtualProtect(vtable + index, sizeof(void*), oldProtect, &oldProtect);

    HookEntry entry;
    entry.name = name;
    entry.target = vtable[index];
    entry.detour = detour;
    entry.original = *original;
    entry.enabled = true;
    entry.isVtable = true;
    entry.vtableAddr = vtable;
    entry.vtableIndex = index;
    m_hooks[name] = std::move(entry);

    spdlog::info("HookManager: Installed vtable hook '{}' at index {}", name, index);
    return true;
}

} // namespace kmp

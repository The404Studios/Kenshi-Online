#include "kmp/hook_manager.h"
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
    // and free relay thunks
    for (auto& [name, entry] : m_hooks) {
        if (entry.isVtable && entry.vtableAddr) {
            DWORD oldProtect;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          PAGE_EXECUTE_READWRITE, &oldProtect);
            entry.vtableAddr[entry.vtableIndex] = entry.original;
            VirtualProtect(entry.vtableAddr + entry.vtableIndex, sizeof(void*),
                          oldProtect, &oldProtect);
        }
        // Free relay thunks (VirtualAlloc'd executable memory)
        if (entry.relayThunk) {
            VirtualFree(entry.relayThunk, 0, MEM_RELEASE);
            spdlog::debug("HookManager: Freed relay thunk for '{}'", name);
        }
    }

    m_hooks.clear();
    m_initialized = false;
    spdlog::info("HookManager: Shutdown complete (hooks disabled, trampolines preserved)");
}

// ═══════════════════════════════════════════════════════════════════════════
// RELAY THUNK BUILDER (DISABLED)
// ═══════════════════════════════════════════════════════════════════════════
//
// The relay thunk approach was REMOVED because `add rax, 8` shifts ALL
// [rax+XX] register saves by one slot. The last save at [rax+20h] overflows
// past the 32-byte shadow space into the caller's stack frame, causing
// delayed stack corruption (CharacterCreate survived 400+ calls then crashed).
//
// The raw MinHook trampoline works correctly for `mov rax, rsp` functions:
// the trampoline copies the instruction verbatim, so RAX captures the
// trampoline's RSP. All saves via [rax+8/10/18/20] and their corresponding
// restores use the SAME [rax+XX] addresses — internally consistent.
// The "wrong" RAX value doesn't matter because saves and loads are paired.
//
// For functions where the raw trampoline truly fails (none found so far),
// use HookBypass (disable-call-reenable) as a proven alternative.
//
void* HookManager::BuildRelayThunk(const std::string& name, void* trampoline) {
    (void)name;
    (void)trampoline;
    return nullptr; // Relay thunks disabled — raw trampoline is safe
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

    // ═══ PROLOGUE ANALYSIS ═══
    auto* bytes = reinterpret_cast<const uint8_t*>(target);
    bool hasMovRaxRsp = (bytes[0] == 0x48 && bytes[1] == 0x8B && bytes[2] == 0xC4);

    spdlog::info("HookManager: '{}' prologue at 0x{:X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}{}",
                 name, reinterpret_cast<uintptr_t>(target),
                 bytes[0], bytes[1], bytes[2], bytes[3],
                 bytes[4], bytes[5], bytes[6], bytes[7],
                 hasMovRaxRsp ? " [mov rax,rsp detected — raw trampoline OK]" : "");

    // ═══ CREATE HOOK ═══
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_CreateHook failed for '{}': {}", name, MH_StatusToString(status));
        return false;
    }

    // NOTE: Relay thunks (add rax, 8) were tried and REMOVED. The raw MinHook
    // trampoline works correctly for mov-rax-rsp functions because the trampoline
    // copies the instruction verbatim — saves via [rax+XX] and restores via [rax+XX]
    // use the same addresses (internally consistent). The relay thunk's add rax,8
    // shifted ALL [rax+XX] accesses by one slot, causing [rax+20] to overflow past
    // the 32-byte shadow space and corrupt the caller's stack frame.

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
    entry.hasMovRaxRsp = hasMovRaxRsp;
    entry.relayThunk = nullptr;
    memcpy(entry.prologueBytes, bytes, 8);
    m_hooks[name] = std::move(entry);

    spdlog::info("HookManager: Installed hook '{}' at 0x{:X}{}",
                 name, reinterpret_cast<uintptr_t>(target),
                 hasMovRaxRsp ? " (mov rax,rsp — raw trampoline)" : "");
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

    // Free relay thunk
    if (entry.relayThunk) {
        VirtualFree(entry.relayThunk, 0, MEM_RELEASE);
        spdlog::debug("HookManager: Freed relay thunk for '{}'", name);
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
    static int s_disableCount = 0;
    int callNum = ++s_disableCount;
    if (callNum <= 20 || callNum % 1000 == 0) {
        char buf[128];
        sprintf_s(buf, "KMP: HookManager::Disable('%s') #%d\n", name.c_str(), callNum);
        OutputDebugStringA(buf);
    }

    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) {
        OutputDebugStringA("KMP: HookManager::Disable — hook not found!\n");
        return false;
    }

    if (!it->second.isVtable) {
        MH_STATUS status = MH_DisableHook(it->second.target);
        if (status != MH_OK) {
            char buf[128];
            sprintf_s(buf, "KMP: HookManager::Disable — MH_DisableHook FAILED: %s\n",
                      MH_StatusToString(status));
            OutputDebugStringA(buf);
            return false;
        }
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

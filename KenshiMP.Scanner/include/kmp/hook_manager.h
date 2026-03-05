#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "kmp/mov_rax_rsp_fix.h"

namespace kmp {

// Diagnostics snapshot for a single hook (returned by GetDiagnostics)
struct HookDiag {
    std::string name;
    uintptr_t   targetAddr    = 0;
    uint8_t     prologue[8]   = {};      // First 8 bytes of original function
    bool        installed     = false;
    bool        enabled       = false;
    bool        isVtable      = false;
    bool        hasRelayThunk = false;    // DEPRECATED (always false)
    bool        hasCustomCaller = false;  // True if MovRaxRsp fix is active
    bool        hasMovRaxRspFix = false;  // True if naked detour + wrapper are active
    int         callCount     = 0;
    int         crashCount    = 0;
};

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    // Install an inline hook using MinHook.
    // If the target starts with `mov rax, rsp` (48 8B C4), the MovRaxRsp fix
    // is automatically applied: a naked detour captures RSP at hook entry, and
    // *original receives a trampoline wrapper that restores RAX before entering
    // the original function body. This fixes the RBP-derived-from-RAX corruption.
    template<typename T>
    bool Install(const std::string& name, T* target, T* detour, T** original) {
        return InstallRaw(name,
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(detour),
            reinterpret_cast<void**>(original));
    }

    // Install a hook by address
    template<typename T>
    bool InstallAt(const std::string& name, uintptr_t address, T* detour, T** original) {
        return InstallRaw(name,
            reinterpret_cast<void*>(address),
            reinterpret_cast<void*>(detour),
            reinterpret_cast<void**>(original));
    }

    // Remove a specific hook by name
    bool Remove(const std::string& name);

    // Remove all hooks
    void RemoveAll();

    // Enable/disable a hook without removing it
    bool Enable(const std::string& name);
    bool Disable(const std::string& name);

    // Get the original target address (NOT the trampoline) for direct calling
    void* GetTarget(const std::string& name) const;

    // Get the custom caller stub for a hook (or nullptr if none).
    // For MovRaxRsp hooks, this returns the trampoline wrapper.
    void* GetCustomCaller(const std::string& name) const;

    // Get MinHook's raw trampoline for a MovRaxRsp hook.
    // This starts with `mov rax, rsp` and does NOT use the global data slots.
    // Safe for REENTRANT calls where the wrapper would corrupt the outer call's state.
    // Returns nullptr for non-MovRaxRsp hooks or if the hook doesn't exist.
    void* GetRawTrampoline(const std::string& name) const;

    // Check if a hook is installed
    bool IsInstalled(const std::string& name) const;

    // Get hook count
    size_t GetHookCount() const;

    // Install a vtable hook (swap vtable entry)
    bool InstallVTableHook(const std::string& name, void** vtable, int index,
                           void* detour, void** original);

    // ── Diagnostics ──

    // Get diagnostics for all hooks (thread-safe snapshot)
    std::vector<HookDiag> GetDiagnostics() const;

    // Increment call/crash counters (called from hook detours)
    void IncrementCallCount(const std::string& name);
    void IncrementCrashCount(const std::string& name);

private:
    HookManager() = default;
    ~HookManager() = default;

    bool InstallRaw(const std::string& name, void* target, void* detour, void** original);

    // Build a relay thunk for functions starting with `mov rax, rsp` (48 8B C4).
    // DEPRECATED: Relay thunks are disabled. Use MovRaxRsp fix instead.
    void* BuildRelayThunk(const std::string& name, void* trampoline);

    // Build a custom caller stub for functions starting with `mov rax, rsp`.
    // DEPRECATED: Superseded by MovRaxRsp fix (BuildMovRaxRspHook).
    void* BuildCustomCaller(const std::string& name, uintptr_t originalAddr);

    struct HookEntry {
        std::string name;
        void*       target   = nullptr;
        void*       detour   = nullptr;
        void*       original = nullptr;     // Raw trampoline from MinHook
        bool        enabled  = false;
        bool        isVtable = false;
        void**      vtableAddr = nullptr;
        int         vtableIndex = -1;

        // ── MovRaxRsp fix ──
        MovRaxRspHook movRaxRspHook = {}; // Naked detour + trampoline wrapper (if applicable)

        // ── Diagnostics ──
        uint8_t     prologueBytes[8] = {};  // First 8 bytes of original function
        void*       relayThunk = nullptr;   // VirtualAlloc'd relay thunk (DEPRECATED, always nullptr)
        void*       customCaller = nullptr; // DEPRECATED: use movRaxRspHook.trampolineWrapper
        bool        hasMovRaxRsp = false;   // True if target starts with 48 8B C4
        int         callCount  = 0;         // Protected by m_mutex
        int         crashCount = 0;         // Protected by m_mutex
    };

    std::unordered_map<std::string, HookEntry> m_hooks;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

// RAII guard: disables a hook on construction, re-enables on destruction.
// DEPRECATED: With the MovRaxRsp fix, this is no longer needed. The trampoline
// wrapper handles RAX correction automatically. Kept for backward compatibility.
class HookBypass {
public:
    HookBypass(const std::string& name) : m_name(name) {
        HookManager::Get().Disable(name);
    }
    ~HookBypass() {
        HookManager::Get().Enable(m_name);
    }
    HookBypass(const HookBypass&) = delete;
    HookBypass& operator=(const HookBypass&) = delete;
private:
    std::string m_name;
};

} // namespace kmp

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace kmp {

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    // Install an inline hook using MinHook
    // Returns true on success. `original` receives the trampoline pointer.
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

    // Check if a hook is installed
    bool IsInstalled(const std::string& name) const;

    // Get hook count
    size_t GetHookCount() const;

    // Install a vtable hook (swap vtable entry)
    bool InstallVTableHook(const std::string& name, void** vtable, int index,
                           void* detour, void** original);

private:
    HookManager() = default;
    ~HookManager() = default;

    bool InstallRaw(const std::string& name, void* target, void* detour, void** original);

    struct HookEntry {
        std::string name;
        void*       target   = nullptr;
        void*       detour   = nullptr;
        void*       original = nullptr;
        bool        enabled  = false;
        bool        isVtable = false;
        void**      vtableAddr = nullptr;
        int         vtableIndex = -1;
    };

    std::unordered_map<std::string, HookEntry> m_hooks;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace kmp

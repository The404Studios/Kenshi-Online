#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <MinHook.h>
#include <Windows.h>

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

    RemoveAll();
    MH_Uninitialize();
    m_initialized = false;
    spdlog::info("HookManager: Shutdown complete");
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

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_CreateHook failed for '{}': {}", name, MH_StatusToString(status));
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK) {
        spdlog::error("HookManager: MH_EnableHook failed for '{}': {}", name, MH_StatusToString(status));
        MH_RemoveHook(target);
        return false;
    }

    HookEntry entry;
    entry.name = name;
    entry.target = target;
    entry.detour = detour;
    entry.original = original ? *original : nullptr;
    entry.enabled = true;
    m_hooks[name] = entry;

    spdlog::info("HookManager: Installed hook '{}' at 0x{:X}", name, reinterpret_cast<uintptr_t>(target));
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

    spdlog::info("HookManager: Removed hook '{}'", name);
    m_hooks.erase(it);
    return true;
}

void HookManager::RemoveAll() {
    // NOTE: Caller must already hold m_mutex (called from Shutdown).
    // We do the removal inline to avoid re-locking the non-recursive mutex.
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

        spdlog::info("HookManager: Removed hook '{}'", name);
        m_hooks.erase(it);
    }
}

bool HookManager::Enable(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return false;

    if (!it->second.isVtable) {
        MH_STATUS status = MH_EnableHook(it->second.target);
        if (status != MH_OK) return false;
    }
    it->second.enabled = true;
    return true;
}

bool HookManager::Disable(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_hooks.find(name);
    if (it == m_hooks.end()) return false;

    if (!it->second.isVtable) {
        MH_STATUS status = MH_DisableHook(it->second.target);
        if (status != MH_OK) return false;
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
    m_hooks[name] = entry;

    spdlog::info("HookManager: Installed vtable hook '{}' at index {}", name, index);
    return true;
}

} // namespace kmp

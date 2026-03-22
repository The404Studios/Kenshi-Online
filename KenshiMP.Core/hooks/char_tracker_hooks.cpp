#include "char_tracker_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "kmp/hook_manager.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <unordered_map>
#include <mutex>

namespace kmp::char_tracker_hooks {

static std::mutex s_trackerMutex;
static std::unordered_map<void*, TrackedChar> s_trackedChars;
static std::function<void(const TrackedChar&)> s_onNewChar;
static void* s_localPlayerAnimClass = nullptr;

// ── Deferred discovery ring buffer ──
// The inline hook fires 300+ times/sec. We MUST avoid heap allocation, mutex,
// spdlog, and CharacterAccessor in the hook body. Instead, record raw pointers
// into a lock-free ring buffer. ProcessDeferredDiscovery() (called from
// OnGameTick) does the expensive name lookup and map insertion.
struct PendingCharUpdate {
    void* animClassPtr;
    uintptr_t charPtr;
};
static constexpr int PENDING_RING_SIZE = 128;
static PendingCharUpdate s_pendingRing[PENDING_RING_SIZE];
static std::atomic<int> s_pendingWrite{0};
static std::atomic<int> s_pendingRead{0};

static void OnCharUpdate(void* animClassHuman) {
    if (!animClassHuman) return;
    uintptr_t animPtr = reinterpret_cast<uintptr_t>(animClassHuman);
    if (animPtr < 0x10000 || animPtr > 0x00007FFFFFFFFFFF) return;

    uintptr_t charPtr = 0;
    if (!Memory::Read(animPtr + 0x2D8, charPtr) || charPtr == 0) return;
    if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) return;

    void* charKey = reinterpret_cast<void*>(charPtr);

    // Fast path: already tracked — just update timestamp (atomic, no mutex)
    // Use a simple spinlock-free check: try_lock fails = skip this update (no stall)
    if (s_trackerMutex.try_lock()) {
        auto it = s_trackedChars.find(charKey);
        if (it != s_trackedChars.end()) {
            it->second.animClassPtr = animClassHuman;
            it->second.lastSeenTick = GetTickCount64();
            s_trackerMutex.unlock();
            return;
        }
        s_trackerMutex.unlock();
    }

    // Slow path: new character — push to ring buffer for deferred processing.
    // NO heap allocation, NO mutex hold, NO spdlog in this path.
    int writeIdx = s_pendingWrite.load(std::memory_order_relaxed);
    int nextIdx = (writeIdx + 1) % PENDING_RING_SIZE;
    if (nextIdx != s_pendingRead.load(std::memory_order_acquire)) {
        s_pendingRing[writeIdx] = {animClassHuman, charPtr};
        s_pendingWrite.store(nextIdx, std::memory_order_release);
    }
    // If ring is full, drop this update (will be caught on next animation tick)
}

// ── Inline Hook Implementation ──
static uint8_t s_originalBytes[14] = {};
static void* s_trampolineAlloc = nullptr;

static void SEH_OnCharUpdate(void* animClassHuman) {
    __try {
        OnCharUpdate(animClassHuman);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static bool BuildInlineHook(uintptr_t hookAddr) {
    __try {
        memcpy(s_originalBytes, reinterpret_cast<void*>(hookAddr), 14);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("char_tracker: Failed to read original bytes at 0x{:X}", hookAddr);
        return false;
    }

    size_t trampolineSize = 256;
    s_trampolineAlloc = VirtualAlloc(nullptr, trampolineSize,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!s_trampolineAlloc) {
        spdlog::error("char_tracker: VirtualAlloc failed for trampoline");
        return false;
    }

    uint8_t* code = static_cast<uint8_t*>(s_trampolineAlloc);
    int off = 0;

    // Save all registers
    uint8_t saveRegs[] = {
        0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57,
        0x41,0x50, 0x41,0x51, 0x41,0x52, 0x41,0x53,
        0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57
    };
    memcpy(code + off, saveRegs, sizeof(saveRegs));
    off += sizeof(saveRegs);

    // sub rsp, 0x28 (shadow space)
    code[off++] = 0x48; code[off++] = 0x83; code[off++] = 0xEC; code[off++] = 0x28;

    // mov rcx, rbx (AnimationClassHuman* is in RBX)
    code[off++] = 0x48; code[off++] = 0x89; code[off++] = 0xD9;

    // call [rip+2]; jmp over ptr
    code[off++] = 0xFF; code[off++] = 0x15; code[off++] = 0x02;
    code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00;
    code[off++] = 0xEB; code[off++] = 0x08;
    uintptr_t funcAddr = reinterpret_cast<uintptr_t>(&SEH_OnCharUpdate);
    memcpy(code + off, &funcAddr, 8);
    off += 8;

    // add rsp, 0x28
    code[off++] = 0x48; code[off++] = 0x83; code[off++] = 0xC4; code[off++] = 0x28;

    // Restore all registers
    uint8_t restoreRegs[] = {
        0x41,0x5F, 0x41,0x5E, 0x41,0x5D, 0x41,0x5C,
        0x41,0x5B, 0x41,0x5A, 0x41,0x59, 0x41,0x58,
        0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58
    };
    memcpy(code + off, restoreRegs, sizeof(restoreRegs));
    off += sizeof(restoreRegs);

    // Execute original 14 bytes
    memcpy(code + off, s_originalBytes, 14);
    off += 14;

    // jmp back to hookAddr + 14
    code[off++] = 0xFF; code[off++] = 0x25;
    code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00; code[off++] = 0x00;
    uintptr_t returnAddr = hookAddr + 14;
    memcpy(code + off, &returnAddr, 8);
    off += 8;

    // Patch original code to jump to trampoline
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        spdlog::error("char_tracker: VirtualProtect failed for hook site");
        VirtualFree(s_trampolineAlloc, 0, MEM_RELEASE);
        s_trampolineAlloc = nullptr;
        return false;
    }

    uint8_t* hookSite = reinterpret_cast<uint8_t*>(hookAddr);
    hookSite[0] = 0xFF; hookSite[1] = 0x25;
    hookSite[2] = 0x00; hookSite[3] = 0x00; hookSite[4] = 0x00; hookSite[5] = 0x00;
    uintptr_t trampolineAddr = reinterpret_cast<uintptr_t>(s_trampolineAlloc);
    memcpy(hookSite + 6, &trampolineAddr, 8);

    VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, oldProtect, &oldProtect);

    spdlog::info("char_tracker: Inline hook installed at 0x{:X} -> trampoline 0x{:X}",
                 hookAddr, trampolineAddr);
    return true;
}

const TrackedChar* FindByName(const std::string& name) {
    std::lock_guard lock(s_trackerMutex);
    for (auto& [key, tc] : s_trackedChars) {
        if (tc.name == name) return &tc;
    }
    return nullptr;
}

const TrackedChar* FindByPtr(void* characterPtr) {
    std::lock_guard lock(s_trackerMutex);
    auto it = s_trackedChars.find(characterPtr);
    return (it != s_trackedChars.end()) ? &it->second : nullptr;
}

void* GetLocalPlayerAnimClass() { return s_localPlayerAnimClass; }

void* GetRemotePlayerAnimClass(const std::string& name) {
    auto* tc = FindByName(name);
    return tc ? tc->animClassPtr : nullptr;
}

void SetOnNewCharacter(std::function<void(const TrackedChar&)> callback) {
    std::lock_guard lock(s_trackerMutex);
    s_onNewChar = callback;
}

int GetTrackedCount() {
    std::lock_guard lock(s_trackerMutex);
    return static_cast<int>(s_trackedChars.size());
}

void DumpTrackedChars() {
    std::lock_guard lock(s_trackerMutex);
    uint64_t now = GetTickCount64();
    spdlog::info("char_tracker: {} tracked characters:", s_trackedChars.size());
    for (auto& [key, tc] : s_trackedChars) {
        float ageSec = (now - tc.lastSeenTick) / 1000.f;
        spdlog::info("  '{}' at 0x{:X} (animClass=0x{:X}), pos=({:.0f},{:.0f},{:.0f}), age={:.1f}s",
                     tc.name, reinterpret_cast<uintptr_t>(tc.characterPtr),
                     reinterpret_cast<uintptr_t>(tc.animClassPtr),
                     tc.position.x, tc.position.y, tc.position.z, ageSec);
    }
}

void ProcessDeferredDiscovery() {
    int processed = 0;
    while (processed < 8) { // Cap per tick to avoid stalls
        int readIdx = s_pendingRead.load(std::memory_order_relaxed);
        if (readIdx == s_pendingWrite.load(std::memory_order_acquire)) break; // Empty
        PendingCharUpdate pending = s_pendingRing[readIdx];
        s_pendingRead.store((readIdx + 1) % PENDING_RING_SIZE, std::memory_order_release);
        processed++;

        void* charKey = reinterpret_cast<void*>(pending.charPtr);

        // Check if already tracked (could have been added between push and now)
        {
            std::lock_guard lock(s_trackerMutex);
            if (s_trackedChars.count(charKey) > 0) {
                s_trackedChars[charKey].animClassPtr = pending.animClassPtr;
                s_trackedChars[charKey].lastSeenTick = GetTickCount64();
                continue;
            }
        }

        // Now do the expensive work: read name, build TrackedChar, insert
        game::CharacterAccessor accessor(charKey);
        std::string name = accessor.GetName();
        if (name.empty()) continue;

        TrackedChar tc;
        tc.animClassPtr = pending.animClassPtr;
        tc.characterPtr = charKey;
        tc.name = name;
        tc.position = accessor.GetPosition();
        tc.lastSeenTick = GetTickCount64();

        {
            std::lock_guard lock(s_trackerMutex);
            s_trackedChars[charKey] = tc;
        }

        spdlog::info("char_tracker: NEW character '{}' at 0x{:X} (animClass=0x{:X})",
                     name, pending.charPtr, reinterpret_cast<uintptr_t>(pending.animClassPtr));

        if (s_onNewChar) {
            s_onNewChar(tc);
        }
    }
}

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();

    if (!funcs.CharAnimUpdate) {
        spdlog::warn("char_tracker: CharAnimUpdate address not resolved — hook not installed");
        return false;
    }

    uintptr_t hookAddr = reinterpret_cast<uintptr_t>(funcs.CharAnimUpdate);
    spdlog::info("char_tracker: Installing inline hook at 0x{:X}", hookAddr);

    return BuildInlineHook(hookAddr);
}

void Uninstall() {
    if (s_trampolineAlloc) {
        auto& funcs = Core::Get().GetGameFunctions();
        if (funcs.CharAnimUpdate) {
            uintptr_t hookAddr = reinterpret_cast<uintptr_t>(funcs.CharAnimUpdate);
            DWORD oldProtect;
            if (VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                memcpy(reinterpret_cast<void*>(hookAddr), s_originalBytes, 14);
                VirtualProtect(reinterpret_cast<void*>(hookAddr), 14, oldProtect, &oldProtect);
            }
        }
        VirtualFree(s_trampolineAlloc, 0, MEM_RELEASE);
        s_trampolineAlloc = nullptr;
    }
}

} // namespace kmp::char_tracker_hooks

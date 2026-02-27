#include "entity_hooks.h"
#include "save_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace kmp::entity_hooks {

// ── Function Types ──
// CharacterSpawn prologue: mov rax,rsp + 7 pushes + 544 stack
// Confirmed 2-param: RCX=factory(this), RDX=GameData*
// CANNOT use MinHook trampoline (mov rax,rsp captures wrong RSP).
// Use disable-call-reenable pattern instead.
using CharacterCreateFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn = void(__fastcall*)(void* character);

// Store the ORIGINAL function addresses (NOT trampolines)
static uintptr_t s_createTargetAddr = 0;
static uintptr_t s_destroyTargetAddr = 0;

// Trampoline pointers (set by MinHook but NOT used for calling CharacterCreate)
static CharacterCreateFn  s_origCreate  = nullptr;
static CharacterDestroyFn s_origDestroy = nullptr;

// Whether CharacterDestroy hook is actually installed (may be skipped if wrong function found)
static bool s_destroyHookInstalled = false;

// ── Diagnostic Counters ──
static std::atomic<int> s_totalCreates{0};
static std::atomic<int> s_totalDestroys{0};

// ── Loading Burst Detection ──
static std::atomic<int>  s_burstCreateCount{0};
static std::atomic<bool> s_inBurst{false};
static auto s_lastCreateTime = std::chrono::steady_clock::now();
static constexpr int    BURST_THRESHOLD = 5;
static constexpr int    BURST_WINDOW_MS = 500;

static bool IsInLoadingBurst() {
    return s_inBurst.load();
}

static void TrackCreationRate() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastCreateTime);

    if (elapsed.count() < BURST_WINDOW_MS) {
        int count = s_burstCreateCount.fetch_add(1) + 1;
        if (count >= BURST_THRESHOLD && !s_inBurst.load()) {
            s_inBurst.store(true);
            spdlog::info("entity_hooks: BURST DETECTED ({} creates in {}ms) — deferring network ops",
                         count, elapsed.count());
        }
    } else {
        s_burstCreateCount.store(1);
        s_lastCreateTime = now;

        if (s_inBurst.load()) {
            s_inBurst.store(false);
            spdlog::info("entity_hooks: BURST ENDED — total creates={}, destroys={}",
                         s_totalCreates.load(), s_totalDestroys.load());
        }
    }
}

// ── Hook suspension state ──
// After capturing the factory pointer, we permanently disable the CharacterCreate
// hook during loading to avoid the massive overhead of MH_DisableHook/MH_EnableHook
// (which freeze ALL threads on every call). The hook is re-enabled when connecting.
static std::atomic<bool> s_hookSuspended{false};

// ── SEH wrapper for calling the original CharacterCreate ──
// MSVC forbids __try in functions with C++ objects needing unwinding.
static void* SEH_CallOriginalCreate(uintptr_t targetAddr, void* factory, void* templateData) {
    __try {
        auto origFn = reinterpret_cast<CharacterCreateFn>(targetAddr);
        return origFn(factory, templateData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ── Hooks ──

static void* __fastcall Hook_CharacterCreate(void* factory, void* templateData) {
    int createNum = s_totalCreates.fetch_add(1) + 1;

    // Log first 10 and every 100th
    if (createNum <= 10 || createNum % 100 == 0) {
        spdlog::debug("entity_hooks: CharacterCreate #{} factory=0x{:X} template=0x{:X}",
                      createNum,
                      reinterpret_cast<uintptr_t>(factory),
                      reinterpret_cast<uintptr_t>(templateData));
    }

    // Call the ORIGINAL function using disable-call-reenable pattern.
    // MinHook's trampoline crashes because the function starts with `mov rax, rsp`
    // which captures the wrong stack pointer when executed in trampoline context.
    void* character = nullptr;
    {
        HookBypass bypass("CharacterCreate");
        character = SEH_CallOriginalCreate(s_createTargetAddr, factory, templateData);
    }

    if (!character) {
        return nullptr;
    }

    // Feed SpawnManager (always, for template/factory capture)
    auto& core = Core::Get();
    core.GetSpawnManager().OnGameCharacterCreated(factory, templateData, character);

    // Track creation rate
    TrackCreationRate();

    // Once we've captured the factory and we're not connected to a server,
    // permanently disable this hook to eliminate the per-call overhead of
    // MH_DisableHook/MH_EnableHook (each freezes ALL threads).
    // The hook will be re-enabled via ResumeForNetwork() when connecting.
    if (!core.IsConnected() && core.GetSpawnManager().IsReady() &&
        !s_hookSuspended.load(std::memory_order_relaxed)) {
        s_hookSuspended.store(true, std::memory_order_relaxed);
        HookManager::Get().Disable("CharacterCreate");
        spdlog::info("entity_hooks: Hook SUSPENDED during load (factory captured after {} creates)", createNum);

        // Notify Core that the game world is loaded
        core.OnGameLoaded();

        return character;
    }

    // During burst loading or when not connected, skip network ops
    if (!core.IsConnected() || IsInLoadingBurst()) {
        return character;
    }

    // Read character data
    game::CharacterAccessor accessor(character);
    Vec3 pos = accessor.GetPosition();

    // Skip uninitialized characters (position 0,0,0)
    if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) {
        return character;
    }

    // Register entity with network
    EntityID netId = core.GetEntityRegistry().Register(character, EntityType::NPC);

    // Report spawn to server
    {
        Quat rot = accessor.GetRotation();
        uint32_t compQuat = rot.Compress();
        core.GetEntityRegistry().UpdatePosition(netId, pos);

        uintptr_t factionPtr = accessor.GetFactionPtr();
        uint32_t factionId = 0;
        if (factionPtr != 0) {
            Memory::Read(factionPtr + 0x08, factionId);
        }

        uint32_t templateId = 0;
        std::string templateName;
        if (templateData) {
            Memory::Read(reinterpret_cast<uintptr_t>(templateData) + 0x08, templateId);
            templateName = SpawnManager::ReadKenshiString(
                reinterpret_cast<uintptr_t>(templateData) + 0x28);
        }

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
        writer.WriteU32(netId);
        writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
        writer.WriteU32(core.GetLocalPlayerId());
        writer.WriteU32(templateId);
        writer.WriteF32(pos.x);
        writer.WriteF32(pos.y);
        writer.WriteF32(pos.z);
        writer.WriteU32(compQuat);
        writer.WriteU32(factionId);
        uint16_t nameLen = static_cast<uint16_t>(
            std::min<size_t>(templateName.size(), 255));
        writer.WriteU16(nameLen);
        if (nameLen > 0) {
            writer.WriteRaw(templateName.data(), nameLen);
        }

        core.GetClient().SendReliable(writer.Data(), writer.Size());
    }

    return character;
}

static void __fastcall Hook_CharacterDestroy(void* character) {
    int destroyNum = s_totalDestroys.fetch_add(1) + 1;

    // Validate pointer before doing anything
    uintptr_t charAddr = reinterpret_cast<uintptr_t>(character);
    if (charAddr < 0x10000 || charAddr > 0x00007FFFFFFFFFFF) {
        s_origDestroy(character);
        return;
    }

    auto& core = Core::Get();
    if (core.IsConnected() && !IsInLoadingBurst()) {
        EntityID netId = core.GetEntityRegistry().GetNetId(character);
        if (netId != INVALID_ENTITY) {
            auto* info = core.GetEntityRegistry().GetInfo(netId);
            bool isOurs = info && info->ownerPlayerId == core.GetLocalPlayerId();

            if (isOurs) {
                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_EntityDespawnReq);
                writer.WriteU32(netId);
                writer.WriteU8(0);
                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }

            core.GetEntityRegistry().Unregister(netId);
        }
    }

    // CharacterDestroy prologue: 48 89 5C 24 08 (saves RBX to shadow space)
    // This does NOT start with mov rax,rsp — standard trampoline works.
    s_origDestroy(character);
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    bool success = true;

    if (funcs.CharacterSpawn) {
        s_createTargetAddr = reinterpret_cast<uintptr_t>(funcs.CharacterSpawn);
        if (!hookMgr.InstallAt("CharacterCreate",
                               s_createTargetAddr,
                               &Hook_CharacterCreate, &s_origCreate)) {
            spdlog::error("entity_hooks: Failed to hook CharacterCreate");
            success = false;
        } else {
            // Set the REAL function address for SpawnManager (not the trampoline)
            core.GetSpawnManager().SetOrigProcess(
                reinterpret_cast<FactoryProcessFn>(s_createTargetAddr));
            spdlog::info("entity_hooks: CharacterCreate hooked (disable-call-reenable mode)");
        }
    }

    // ── CharacterDestroy: DISABLED ──
    // The string fallback resolved "NodeList::destroyNodesByBuilding" which is NOT the
    // character destructor. Crash dump confirmed: access violation at CharacterDestroy+0x16
    // trying to read [null+0xD8] — the trampoline passes garbage in RDX because our
    // hook typedef only has 1 param but the real function uses 2.
    // TODO: Find the real character destructor via a different string anchor or pattern.
    if (funcs.CharacterDestroy) {
        spdlog::warn("entity_hooks: CharacterDestroy at 0x{:X} is 'NodeList::destroyNodesByBuilding' "
                     "(wrong function) — SKIPPING hook to prevent crash",
                     reinterpret_cast<uintptr_t>(funcs.CharacterDestroy));
        s_destroyHookInstalled = false;
    }

    spdlog::info("entity_hooks: Installed (create={}, destroy={})",
                 funcs.CharacterSpawn != nullptr, s_destroyHookInstalled);
    return success;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterCreate");
    if (s_destroyHookInstalled) {
        HookManager::Get().Remove("CharacterDestroy");
    }
}

void ResumeForNetwork() {
    if (s_hookSuspended.load(std::memory_order_relaxed)) {
        HookManager::Get().Enable("CharacterCreate");
        s_hookSuspended.store(false, std::memory_order_relaxed);
        spdlog::info("entity_hooks: Hook RESUMED for network sync (total creates so far: {})",
                     s_totalCreates.load());
    }
}

} // namespace kmp::entity_hooks

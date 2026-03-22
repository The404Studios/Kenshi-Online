#include "entity_hooks.h"
#include "save_hooks.h"
#include "ai_hooks.h"
#include "squad_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "../game/asset_facilitator.h"
#include "../sync/pipeline_state.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include "kmp/string_convert.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <mutex>

// Declared in core.cpp — updated here so VEH crash handler shows which create# crashed
namespace kmp { extern volatile int g_lastCharacterCreateNum; }

namespace kmp::entity_hooks {

// ── Function Types ──
using CharacterCreateFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn = void(__fastcall*)(void* character);

// Store the ORIGINAL function addresses (NOT trampolines)
static uintptr_t s_createTargetAddr = 0;
static uintptr_t s_destroyTargetAddr = 0;

// s_origCreate: Points to the TRAMPOLINE WRAPPER built by MovRaxRsp fix.
static CharacterCreateFn  s_origCreate  = nullptr;
static CharacterDestroyFn s_origDestroy = nullptr;

// s_rawTrampoline: MinHook's raw trampoline (starts with `mov rax, rsp`).
// Used for REENTRANT calls to avoid corrupting the MovRaxRsp wrapper's global
// data slots (captured_rsp, stub_rsp, saved_game_ret).
static CharacterCreateFn  s_rawCreateTrampoline = nullptr;

// s_directCallStub: Custom caller stub for CallFactoryDirect.
// Does `mov rax, rsp; jmp rawTrampoline+3` — gives the original function the
// correct RAX (caller's RSP after CALL pushed return address) then enters the
// original body past its own `mov rax, rsp`. Without this, calling the raw
// trampoline from C++ gives wrong RAX → wrong RBP → all [rbp+XX] accesses
// read/write wrong memory → sign-extended faction pointers → crash.
static CharacterCreateFn  s_directCallStub = nullptr;
static void*              s_directCallStubAlloc = nullptr;

// Whether CharacterDestroy hook is actually installed
static bool s_destroyHookInstalled = false;

// ── Diagnostic Counters ──
static std::atomic<int> s_totalCreates{0};
static std::atomic<int> s_totalDestroys{0};

// ── Position offset in request struct ──
static int s_positionOffsetInStruct = -1;
static int s_positionDetectAttempts = 0;

// ── GameData pointer offset in request struct ──
static int s_gameDataOffsetInStruct = -1;
static int s_gameDataDetectAttempts = 0;

// ── Direct spawn bypass ──
static std::atomic<bool> s_directSpawnBypass{false};

// ── Higher-level factory functions (resolved from known RVAs) ──
// RootObjectFactory::create (0x583400) — dispatches to process() but builds request struct internally.
// Takes (factory, GameData*), not a raw request struct. This bypasses the stale-pointer problem.
static CharacterCreateFn s_factoryCreate = nullptr;
// RootObjectFactory::createRandomChar (0x5836E0) — creates a random NPC character.
static CharacterCreateFn s_factoryCreateRandomChar = nullptr;

// ── In-place replay tracking ──
static std::atomic<int> s_inPlaceSpawnCount{0};
static auto s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();

// ── Connected-mode counters (file scope for reset from ResumeForNetwork) ──
// These are accessed from game thread (Hook_CharacterCreate) and network thread
// (ResumeForNetwork), so use atomics for the counters.
static std::atomic<int> s_connectedCreateNum{0};
static auto s_connectedBurstStart = std::chrono::steady_clock::now();
static std::atomic<int> s_connectedBurstCount{0};
static std::atomic<int> s_earlyProbeCount{0};

// ── Per-player spawn cap ──
// Accessed from game thread (Hook_CharacterCreate) and network thread (ResumeForNetwork).
static constexpr int MAX_SPAWNS_PER_PLAYER = 4;
static std::mutex s_spawnsPerPlayerMutex;
static std::unordered_map<PlayerID, int> s_spawnsPerPlayer;

// ── Request struct handling ──
static constexpr size_t REQUEST_STRUCT_SIZE = 1024;
static uint8_t s_preCallStruct[REQUEST_STRUCT_SIZE] = {};
static bool s_havePreCallData = false;
static void* s_savedFactory = nullptr;

// ── Loading capture gate ──
// After capturing pre-call data + factory + faction from the first CharacterCreate,
// the hook disables itself so all remaining loading creates (100+) go directly
// to the original function — zero MovRaxRsp overhead, zero corruption risk.
static bool s_loadingCapturesDone = false;

// ── SEH-safe memcpy for request struct capture ──
static bool SEH_MemcpySafe(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── Player faction pointer (captured during savegame loading) ──
// The FIRST character created during savegame load is the player's squad leader.
// Kenshi loads player characters before NPCs/buildings, so char #1's faction
// is reliably the player faction. Locked after first capture — never overwritten.
static std::atomic<uintptr_t> s_earlyPlayerFaction{0};
static std::atomic<bool>      s_earlyFactionLocked{false};

// SEH-protected faction read from a character pointer.
// Returns the faction pointer at char+0x10, or 0 on failure.
static uintptr_t SEH_ReadFaction(void* character) {
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);
        if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) return 0;
        uintptr_t factionPtr = 0;
        Memory::Read(charPtr + 0x10, factionPtr);
        if (factionPtr > 0x10000 && factionPtr < 0x00007FFFFFFFFFFF && (factionPtr & 0x7) == 0) {
            return factionPtr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// ── Fallback faction pointer ──
// Updated whenever we see a character with a valid faction during ANY creation.
// Used as last-resort when local faction hasn't been discovered yet.
static std::atomic<uintptr_t> s_fallbackFaction{0};

static void UpdateFallbackFaction(uintptr_t factionPtr) {
    if (factionPtr > 0x10000 && factionPtr < 0x00007FFFFFFFFFFF && (factionPtr & 0x7) == 0) {
        s_fallbackFaction.store(factionPtr, std::memory_order_relaxed);
    }
}

// ── SEH-safe character memory reads ──
struct SEH_CharData {
    Vec3 position;
    Quat rotation;
    uintptr_t factionPtr;
    uint32_t factionId;
    bool valid;
};

static SEH_CharData SEH_ReadCharacterData(void* character) {
    SEH_CharData result{};
    result.valid = false;
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);
        if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) return result;

        Memory::Read(charPtr + 0x48, result.position.x);
        Memory::Read(charPtr + 0x4C, result.position.y);
        Memory::Read(charPtr + 0x50, result.position.z);

        Memory::Read(charPtr + 0x58, result.rotation.w);
        Memory::Read(charPtr + 0x5C, result.rotation.x);
        Memory::Read(charPtr + 0x60, result.rotation.y);
        Memory::Read(charPtr + 0x64, result.rotation.z);

        Memory::Read(charPtr + 0x10, result.factionPtr);
        if (result.factionPtr > 0x10000 && result.factionPtr < 0x00007FFFFFFFFFFF && (result.factionPtr & 0x7) == 0) {
            Memory::Read(result.factionPtr + game::GetOffsets().faction.id, result.factionId);
            UpdateFallbackFaction(result.factionPtr);
        }

        result.valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_readCrash = 0;
        if (++s_readCrash <= 5) {
            OutputDebugStringA("KMP: SEH_ReadCharacterData CRASHED — character memory invalid\n");
        }
    }
    return result;
}

static bool SEH_FeedSpawnManager(void* factory, void* templateData, void* character) {
    __try {
        Core::Get().GetSpawnManager().OnGameCharacterCreated(factory, templateData, character);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_feedCrash = 0;
        if (++s_feedCrash <= 5) {
            OutputDebugStringA("KMP: SEH_FeedSpawnManager CRASHED — template scan hit bad memory\n");
        }
        return false;
    }
}

// ── SEH-protected NPC hijack ──
// Instead of creating a new character, TAKE OVER an existing NPC.
// The NPC already has a valid faction, model, animations — zero crash risk.
// We just: register it, teleport it, rename it, disable AI.
static bool SEH_HijackNPC(void* npcChar, EntityID netId, PlayerID owner, Vec3 spawnPos) {
    uintptr_t charAddr = reinterpret_cast<uintptr_t>(npcChar);
    if (charAddr < 0x10000 || charAddr >= 0x00007FFFFFFFFFFF || (charAddr & 0x7) != 0) {
        spdlog::warn("SEH_HijackNPC: Invalid char 0x{:X} for entity {} — skipped", charAddr, netId);
        return false;
    }

    __try {
        // Vtable check: first qword must point into module (.rdata)
        uintptr_t vtable = 0;
        if (!Memory::Read(charAddr, vtable)) return false;
        uintptr_t modBase = Memory::GetModuleBase();
        if (vtable < modBase || vtable >= modBase + 0x4000000) {
            spdlog::warn("SEH_HijackNPC: Bad vtable 0x{:X} for entity {} — not a game object", vtable, netId);
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spdlog::warn("SEH_HijackNPC: Vtable read crashed for entity {}", netId);
        return false;
    }

    // Link to entity registry
    Core::Get().GetEntityRegistry().SetGameObject(netId, npcChar);
    Core::Get().GetEntityRegistry().UpdatePosition(netId, spawnPos);

    __try {
        game::CharacterAccessor accessor(npcChar);

        // Teleport to remote player's position
        if (spawnPos.x != 0.f || spawnPos.y != 0.f || spawnPos.z != 0.f) {
            accessor.WritePosition(spawnPos);
        }

        // Rename to remote player's name + disable AI
        Core::Get().GetPlayerController().OnRemoteCharacterSpawned(netId, npcChar, owner);
        ai_hooks::MarkRemoteControlled(npcChar);

        // DO NOT touch the faction — the NPC's original faction is already valid.
        // This is the whole point of hijacking: zero faction issues.

        game::ScheduleDeferredAnimClassProbe(charAddr);

        spdlog::info("SEH_HijackNPC: HIJACKED NPC 0x{:X} as entity {} for player {} at ({:.1f},{:.1f},{:.1f})",
                     charAddr, netId, owner, spawnPos.x, spawnPos.y, spawnPos.z);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_hijackCrash = 0;
        if (++s_hijackCrash <= 10) {
            char buf[256];
            sprintf_s(buf, "KMP: SEH_HijackNPC CRASHED for entity %u (AV caught)\n", netId);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

void SetDirectSpawnBypass(bool bypass) {
    s_directSpawnBypass.store(bypass, std::memory_order_release);
}

// Direct call to original CharacterCreate.
// No __try wrapper: the raw trampoline starts with mov rax,rsp and the original
// function derives its frame from RAX. Extra stack frames shift RSP and corrupt
// the frame. If it crashes, VEH logs it and the game handler deals with it.
#pragma optimize("", off)
static void* CallOriginalCreate(void* factory, void* templateData) {
    // CRITICAL: Prefer the MovRaxRsp wrapper (s_origCreate) over the raw trampoline.
    // When called from within Hook_CharacterCreate (which was invoked via the naked
    // detour), the wrapper correctly restores the captured game RSP before calling
    // the original function. The raw trampoline's `mov rax, rsp` would get the WRONG
    // RSP (our C++ stack instead of the game caller's stack), corrupting the frame.
    // This fixes CallFactoryCreate: create() → process() → hook → wrapper → correct RSP.
    CharacterCreateFn fn = s_origCreate ? s_origCreate : s_rawCreateTrampoline;
    return fn(factory, templateData);
}
#pragma optimize("", on)

// ── SEH wrapper for in-place spawn replay via trampoline ──
// CRITICAL: Must restore reqStruct even on crash to prevent stack corruption
static uint8_t s_replayBackup[REQUEST_STRUCT_SIZE];
static void* SEH_ReplayFactory(CharacterCreateFn trampoline, void* factory, void* reqStruct,
                                const uint8_t* preCallData, size_t structSize) {
    // Save original BEFORE __try (so we can always restore)
    memcpy(s_replayBackup, reqStruct, structSize);
    memcpy(reqStruct, preCallData, structSize);
    __try {
        void* result = trampoline(factory, reqStruct);
        // Restore original on success
        memcpy(reqStruct, s_replayBackup, structSize);
        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // ALWAYS restore original on crash — prevents stack corruption
        memcpy(reqStruct, s_replayBackup, structSize);
        static int s_replayCrash = 0;
        if (++s_replayCrash <= 5) {
            OutputDebugStringA("KMP: SEH_ReplayFactory CRASHED — reqStruct restored\n");
        }
        return nullptr;
    }
}

// ── SEH-safe loading data capture (pure C, no C++ objects) ──
static void SEH_CaptureTemplateData(void* templateData) {
    __try {
        if (templateData && !s_havePreCallData) {
            SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
            s_havePreCallData = true;
            Core::Get().GetSpawnManager().SetPreCallData(
                s_preCallStruct, REQUEST_STRUCT_SIZE,
                reinterpret_cast<uintptr_t>(templateData));
            Core::Get().GetSpawnManager().SetSavedRequestStruct(
                s_preCallStruct, REQUEST_STRUCT_SIZE);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silent failure — factory capture is best-effort
    }
}

// ── SEH-safe entity data reading (pure C, no C++ objects) ──
struct SEH_EntityInfo {
    SEH_CharData charData;
    EntityID netId;
    PlayerID owner;
    uint32_t templateId;
    char templateNameBuf[256];
    int templateNameLen;
    bool registered;
};

static SEH_EntityInfo SEH_ReadAndRegisterEntity(void* character, void* templateData) {
    SEH_EntityInfo info{};
    info.registered = false;
    __try {
        auto& coreRef = Core::Get();
        info.charData = SEH_ReadCharacterData(character);
        if (!info.charData.valid) return info;
        if (info.charData.position.x == 0.f && info.charData.position.y == 0.f && info.charData.position.z == 0.f) return info;

        // ── Faction matching: only register entities that belong to the LOCAL player ──
        // Priority chain: PlayerController → early loading capture → fallback
        uintptr_t playerFaction = coreRef.GetPlayerController().GetLocalFactionPtr();

        if (playerFaction == 0) {
            // Use faction captured during savegame loading (first character = player's squad leader)
            playerFaction = s_earlyPlayerFaction.load(std::memory_order_relaxed);
        }
        if (playerFaction == 0) {
            // Last resort: any valid faction seen from recent creates
            playerFaction = s_fallbackFaction.load(std::memory_order_relaxed);
        }

        // If we found a faction from loading/fallback but PlayerController doesn't have it, set it
        if (playerFaction != 0 && coreRef.GetPlayerController().GetLocalFactionPtr() == 0) {
            const_cast<PlayerController&>(coreRef.GetPlayerController())
                .SetLocalFactionPtr(playerFaction);
            spdlog::info("SEH_ReadAndRegisterEntity: Set local faction 0x{:X} from early/fallback capture", playerFaction);
        }

        // Must have a player faction AND entity must match it — prevents registering random NPCs/buildings
        if (playerFaction == 0 || info.charData.factionPtr != playerFaction) return info;

        info.owner = coreRef.GetLocalPlayerId();
        info.netId = coreRef.GetEntityRegistry().Register(
            character, EntityType::NPC, info.owner);
        coreRef.GetEntityRegistry().UpdatePosition(info.netId, info.charData.position);

        info.templateId = 0;
        info.templateNameLen = 0;
        memset(info.templateNameBuf, 0, sizeof(info.templateNameBuf));
        if (templateData) {
            Memory::Read(reinterpret_cast<uintptr_t>(templateData) + 0x08, info.templateId);
            // Read Kenshi string manually (SSO-aware) to avoid std::string in SEH
            uintptr_t strAddr = reinterpret_cast<uintptr_t>(templateData) + 0x28;
            uint64_t strSize = 0, strCap = 0;
            Memory::Read(strAddr + 0x10, strSize);
            Memory::Read(strAddr + 0x18, strCap);
            if (strSize > 0 && strSize < 255) {
                uintptr_t dataPtr = strAddr; // SSO: data inline
                if (strCap > 15) Memory::Read(strAddr, dataPtr); // heap
                SEH_MemcpySafe(info.templateNameBuf, reinterpret_cast<void*>(dataPtr), (size_t)strSize);
                // Convert from system ANSI codepage to UTF-8 (handles CJK on Chinese/Japanese/Korean Windows)
                info.templateNameLen = kmp::AnsiToUtf8InPlace(
                    info.templateNameBuf, (int)strSize, (int)sizeof(info.templateNameBuf));
            }
        }

        info.registered = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_readCrash = 0;
        if (++s_readCrash <= 5) {
            OutputDebugStringA("KMP: SEH_ReadAndRegisterEntity CRASHED (SEH caught)\n");
        }
    }
    return info;
}

// ── Connected post-processing: reads entity data (SEH), then builds & sends packet ──
// No __try here — PacketWriter has a destructor (C2712). The dangerous part
// (game memory reads) is in SEH_ReadAndRegisterEntity above.
static void SEH_ConnectedPostProcess(void* character, void* templateData, int connectedCreateNum) {
    SEH_EntityInfo info = SEH_ReadAndRegisterEntity(character, templateData);
    if (!info.registered) return;

    if (info.charData.factionPtr != 0 && connectedCreateNum <= 3) {
        char dbgBuf[128];
        sprintf_s(dbgBuf, "KMP: FACTION BOOTSTRAP — faction 0x%llX\n",
                  (unsigned long long)info.charData.factionPtr);
        OutputDebugStringA(dbgBuf);
    }

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
    writer.WriteU32(info.netId);
    writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
    writer.WriteU32(info.owner);
    writer.WriteU32(info.templateId);
    writer.WriteF32(info.charData.position.x);
    writer.WriteF32(info.charData.position.y);
    writer.WriteF32(info.charData.position.z);
    writer.WriteU32(info.charData.rotation.Compress());
    writer.WriteU32(info.charData.factionId);
    writer.WriteU16(static_cast<uint16_t>(info.templateNameLen));
    if (info.templateNameLen > 0)
        writer.WriteRaw(info.templateNameBuf, info.templateNameLen);

    Core::Get().GetClient().SendReliable(writer.Data(), writer.Size());
}

// ═══════════════════════════════════════════════════════════════════════════
//  Hook_CharacterCreate
//
//  Two modes:
//  (A) LOADING MODE (not connected): First 2 calls capture factory + template
//      data for SpawnManager. After call 2, sets the software bypass flag so
//      the remaining 100+ loading creates go directly to the raw trampoline
//      (zero overhead, no MovRaxRsp stack swap).
//  (B) CONNECTED MODE: Full hook body — entity registration, spawn replay,
//      faction bootstrap, packet sending.
// ═══════════════════════════════════════════════════════════════════════════

static void* __fastcall Hook_CharacterCreate(void* factory, void* templateData) {
    // ── Re-entrancy guard ──
    static thread_local int s_hookDepth = 0;
    if (s_hookDepth > 0) {
        if (s_rawCreateTrampoline)
            return s_rawCreateTrampoline(factory, templateData);
        return CallOriginalCreate(factory, templateData);
    }
    s_hookDepth++;

    // Direct spawn bypass (SpawnManager calling factory — skip all logic)
    if (s_directSpawnBypass.load(std::memory_order_acquire)) {
        void* r = CallOriginalCreate(factory, templateData);
        s_hookDepth--;
        return r;
    }

    int createNum = s_totalCreates.fetch_add(1) + 1;
    g_lastCharacterCreateNum = createNum;

    // One-time factory pointer save
    if (!s_savedFactory) s_savedFactory = factory;

    auto& coreRef = Core::Get();

    // ── NOT CONNECTED: capture factory+pre-call data, then passthrough ──
    // Even when not connected, capture factory pointer and pre-call data so
    // the spawn system is ready when the player connects. This prevents the
    // "walk near a town" requirement — data is captured during loading.
    // Uses s_origCreate (MovRaxRsp wrapper) which correctly restores RSP for
    // the original function. Safe here because loading creates are sequential
    // (not reentrant), so the wrapper's global slots don't conflict.
    if (!coreRef.IsConnected()) {
        // Capture pre-call data + factory + faction from the FIRST call only.
        // Then DISABLE the hook so all subsequent loading creates go through
        // the ORIGINAL function with zero hook overhead — no MovRaxRsp wrapper,
        // no stack manipulation, nothing that could corrupt game state.
        if (!s_loadingCapturesDone) {
            if (templateData && !s_havePreCallData) {
                if (SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE)) {
                    s_havePreCallData = true;
                    coreRef.GetSpawnManager().SetPreCallData(
                        s_preCallStruct, REQUEST_STRUCT_SIZE,
                        reinterpret_cast<uintptr_t>(templateData));
                    coreRef.GetSpawnManager().SetSavedRequestStruct(
                        s_preCallStruct, REQUEST_STRUCT_SIZE);
                }
            }
            if (factory && !coreRef.GetSpawnManager().IsReady()) {
                coreRef.GetSpawnManager().SetFactory(factory);
            }

            void* r = CallOriginalCreate(factory, templateData);

            // Capture faction from this first character (player's squad leader)
            if (r) {
                uintptr_t fac = SEH_ReadFaction(r);
                if (fac != 0) {
                    s_earlyPlayerFaction.store(fac, std::memory_order_relaxed);
                    s_earlyFactionLocked.store(true, std::memory_order_relaxed);
                    UpdateFallbackFaction(fac);
                }

                // CRITICAL: Feed SpawnManager the first character so it captures
                // the GameData backpointer and discovers GameDataManager. Without
                // this, the heap scan has no m_managerPointer and fails on Steam.
                SEH_FeedSpawnManager(factory, templateData, r);
            }

            // Got everything we need — DISABLE the hook for the rest of loading.
            // This prevents 100+ calls through MovRaxRsp wrapper during savegame load.
            if (s_havePreCallData && s_savedFactory) {
                s_loadingCapturesDone = true;
                HookManager::Get().Disable("CharacterCreate");
                OutputDebugStringA("KMP: Hook DISABLED after loading capture — safe passthrough for remaining loads\n");
            }

            s_hookDepth--;
            return r;
        }

        // If we reach here, captures are done but hook is somehow still active.
        // Use CallOriginalCreate which prefers the MovRaxRsp wrapper (correct RSP).
        void* r = CallOriginalCreate(factory, templateData);
        s_hookDepth--;
        return r;
    }

    // Connected create counter (minimal logging to avoid heap pressure in MovRaxRsp context)
    int connNum = s_connectedCreateNum.fetch_add(1) + 1;
    if (connNum <= 5) {
        char dbgBuf[128];
        sprintf_s(dbgBuf, "KMP: Connected CharacterCreate #%d\n", connNum);
        OutputDebugStringA(dbgBuf);
    }

    // Rapid-fire detection: if >5 creates in 100ms, go lightweight (zone load burst)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_connectedBurstStart);
    if (elapsed.count() < 100) {
        s_connectedBurstCount++;
    } else {
        s_connectedBurstStart = now;
        s_connectedBurstCount = 1;
    }

    // During zone-load burst (>5 creates in 100ms): lightweight passthrough
    // Still capture faction if we haven't locked one yet (backup path).
    if (s_connectedBurstCount > 5) {
        if (s_connectedBurstCount <= 10 || s_connectedBurstCount % 50 == 0) {
            spdlog::debug("entity_hooks: BURST passthrough #{}", s_connectedBurstCount.load());
        }
        void* character = CallOriginalCreate(factory, templateData);

        // Lightweight faction capture — only if we still don't have a player faction
        if (character && !s_earlyFactionLocked.load(std::memory_order_relaxed)) {
            uintptr_t fac = SEH_ReadFaction(character);
            if (fac != 0) {
                UpdateFallbackFaction(fac);
                // Don't lock here — burst characters may be NPCs, not player chars.
                // Loading path locks it; this is just a backup for fallback.
            }
        }

        s_hookDepth--;
        return character;
    }

    // Pre-call struct capture (needed for in-place spawn replay)
    uint8_t localPreCall[REQUEST_STRUCT_SIZE];
    bool haveLocalPreCall = false;
    if (templateData) {
        memset(localPreCall, 0, REQUEST_STRUCT_SIZE);
        if (SEH_MemcpySafe(localPreCall, templateData, REQUEST_STRUCT_SIZE)) {
            haveLocalPreCall = true;
        }
        // Always refresh pre-call data from connected NPC spawns — the struct
        // captured during loading has stale external pointers (faction, squad, etc.)
        // that crash CallFactoryDirect. Fresh NPC structs have live heap pointers.
        {
            SEH_MemcpySafe(s_preCallStruct, templateData, REQUEST_STRUCT_SIZE);
            s_havePreCallData = true;
            coreRef.GetSpawnManager().SetPreCallData(
                s_preCallStruct, REQUEST_STRUCT_SIZE,
                reinterpret_cast<uintptr_t>(templateData));
            coreRef.GetSpawnManager().SetSavedRequestStruct(
                s_preCallStruct, REQUEST_STRUCT_SIZE);
        }
    }

    // ═══ CALL ORIGINAL FUNCTION ═══
    void* character = CallOriginalCreate(factory, templateData);
    bool wasHijacked = false;

    if (character && haveLocalPreCall) {
        // Position offset detection
        if (s_positionOffsetInStruct == -1 && s_positionDetectAttempts < 10) {
            s_positionDetectAttempts++;
            SEH_CharData charData = SEH_ReadCharacterData(character);
            float cx = charData.position.x, cy = charData.position.y, cz = charData.position.z;
            if (cx != 0.f || cy != 0.f || cz != 0.f) {
                for (int off = 0; off < (int)REQUEST_STRUCT_SIZE - 12; off += 4) {
                    float sx, sy, sz;
                    memcpy(&sx, &localPreCall[off], 4);
                    memcpy(&sy, &localPreCall[off + 4], 4);
                    memcpy(&sz, &localPreCall[off + 8], 4);
                    if (fabsf(sx - cx) < 2.0f && fabsf(sy - cy) < 2.0f && fabsf(sz - cz) < 2.0f) {
                        s_positionOffsetInStruct = off;
                        spdlog::info("entity_hooks: POSITION OFFSET at struct+0x{:X}", off);
                        break;
                    }
                }
            }
        }

        // GameData pointer offset detection
        // Finds where in the request struct the GameData* pointer lives, so
        // SpawnManager can swap it with mod templates for remote spawns.
        if (s_gameDataOffsetInStruct == -1 && s_gameDataDetectAttempts < 10) {
            s_gameDataDetectAttempts++;
            game::CharacterAccessor accessor(character);
            uintptr_t charGameData = accessor.GetGameDataPtr();
            if (charGameData != 0 && charGameData > 0x10000 && charGameData < 0x00007FFFFFFFFFFF) {
                for (int off = 0; off + 8 <= (int)REQUEST_STRUCT_SIZE; off += 8) {
                    uintptr_t val = 0;
                    memcpy(&val, &localPreCall[off], 8);
                    if (val == charGameData) {
                        s_gameDataOffsetInStruct = off;
                        coreRef.GetSpawnManager().SetGameDataOffset(off);
                        spdlog::info("entity_hooks: GAMEDATA OFFSET at struct+0x{:X} = 0x{:X}",
                                     off, charGameData);
                        break;
                    }
                }
            }
            // Also propagate position offset to SpawnManager
            if (s_positionOffsetInStruct >= 0) {
                coreRef.GetSpawnManager().SetPositionOffset(s_positionOffsetInStruct);
            }
        }

        // ═══ NPC HIJACK ═══
        // Instead of calling the factory again (replay), take over the NPC that
        // the game just created. The NPC already has a valid faction, model, and
        // animations — zero crash risk from faction pointer issues.
        // We just: register it, teleport it to the remote player's position,
        // rename it, and disable its AI.
        if (character && coreRef.IsGameLoaded()) {
            auto& spawnMgr = coreRef.GetSpawnManager();
            SpawnRequest spawnReq;
            if (spawnMgr.PopNextSpawn(spawnReq)) {
                bool canSpawnForPlayer = false;
                {
                    std::lock_guard lock(s_spawnsPerPlayerMutex);
                    canSpawnForPlayer = s_spawnsPerPlayer[spawnReq.owner] < MAX_SPAWNS_PER_PLAYER;
                }
                if (canSpawnForPlayer) {
                    spdlog::info("entity_hooks: NPC HIJACK for entity {} owner={} — using just-created NPC",
                                 spawnReq.netId, spawnReq.owner);

                    if (SEH_HijackNPC(character, spawnReq.netId, spawnReq.owner, spawnReq.position)) {
                        s_inPlaceSpawnCount.fetch_add(1);
                        s_lastInPlaceSpawnTime = std::chrono::steady_clock::now();
                        wasHijacked = true;
                        {
                            std::lock_guard lock(s_spawnsPerPlayerMutex);
                            s_spawnsPerPlayer[spawnReq.owner]++;
                        }
                    } else {
                        spawnReq.retryCount++;
                        if (spawnReq.retryCount < MAX_SPAWN_RETRIES)
                            spawnMgr.RequeueSpawn(spawnReq);
                    }
                } else {
                    spawnMgr.RequeueSpawn(spawnReq);
                }
            }
        }
    }

    if (!character) {
        s_hookDepth--;
        return nullptr;
    }

    // AnimClass probe (first 5 only)
    {
        if (s_earlyProbeCount < 5) {
            game::ScheduleDeferredAnimClassProbe(reinterpret_cast<uintptr_t>(character));
            s_earlyProbeCount++;
        }
    }

    // Skip local entity registration for hijacked NPCs — they are already
    // registered as REMOTE via SEH_HijackNPC. Feeding them to SpawnManager
    // and SEH_ConnectedPostProcess would re-register as LOCAL and send a
    // spurious C2S_EntitySpawnReq to the server.
    if (!wasHijacked) {
        // Feed SpawnManager
        SEH_FeedSpawnManager(factory, templateData, character);

        // Register player faction characters in EntityRegistry (SEH-protected, no C++ objects)
        if (coreRef.IsGameLoaded()) {
            SEH_ConnectedPostProcess(character, templateData, s_connectedCreateNum);
        }
    }

    s_hookDepth--;
    return character;
}

static void __fastcall Hook_CharacterDestroy(void* character) {
    int destroyNum = s_totalDestroys.fetch_add(1) + 1;

    uintptr_t charAddr = reinterpret_cast<uintptr_t>(character);
    if (charAddr < 0x10000 || charAddr > 0x00007FFFFFFFFFFF) {
        s_origDestroy(character);
        return;
    }

    auto& core = Core::Get();
    if (core.IsConnected()) {
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

    s_origDestroy(character);
}

// ── Direct Call Stub Builder ──

// Build a small executable stub: `mov rax, rsp; jmp [rip+0]; dq target`
// This gives the original function the correct RAX value (our caller's RSP)
// and jumps to rawTrampoline+3 (skipping the trampoline's own `mov rax, rsp`).
// Total: 17 bytes. Allocated as PAGE_EXECUTE_READWRITE, locked to PAGE_EXECUTE_READ.
static void* BuildDirectCallStub(void* rawTrampoline) {
    void* mem = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;

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

    // 8-byte target: rawTrampoline + 3 (skip its mov rax, rsp)
    uintptr_t target = reinterpret_cast<uintptr_t>(rawTrampoline) + 3;
    memcpy(&code[off], &target, 8);

    DWORD oldProtect;
    VirtualProtect(mem, 64, PAGE_EXECUTE_READ, &oldProtect);

    spdlog::info("entity_hooks: Built direct call stub at 0x{:X} -> trampoline+3 = 0x{:X}",
                 reinterpret_cast<uintptr_t>(mem), target);
    return mem;
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    bool success = true;

    // ── Resolve higher-level factory functions from known RVAs ──
    // These are NOT hooked — called directly via function pointer. No MinHook
    // trampoline involved, so `mov rax, rsp` prologue works correctly when
    // called from C++ (CPU pushes return address, then function reads RSP).
    //
    // Validation: .pdata check confirms address is a real function start,
    // prologue byte check confirms it looks like executable code.
    // If validation fails, the function pointer is left null and spawn
    // falls back to createRandomChar or NPC hijack — never blocks startup.
    {
        uintptr_t modBase = Memory::GetModuleBase();

        auto validateFactoryFunc = [](uintptr_t addr, const char* name) -> bool {
            // .pdata validation — confirms this is a real function entry point
            DWORD64 imageBase = 0;
            auto* rtFunc = RtlLookupFunctionEntry(
                static_cast<DWORD64>(addr), &imageBase, nullptr);
            if (rtFunc) {
                uintptr_t funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
                if (funcStart != addr) {
                    spdlog::error("entity_hooks: {} at 0x{:X} is MID-FUNCTION "
                                  "(real start 0x{:X}, offset +0x{:X}) — SKIPPING",
                                  name, addr, funcStart, addr - funcStart);
                    return false;
                }
            }
            // Prologue byte check — verify it looks like a function start
            __try {
                auto* p = reinterpret_cast<const uint8_t*>(addr);
                bool validPrologue =
                    (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0xC4) || // mov rax, rsp
                    (p[0] == 0x48 && p[1] == 0x89)                  || // mov [rsp+xx], reg
                    (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC)  || // sub rsp, imm8
                    (p[0] == 0x48 && p[1] == 0x81 && p[2] == 0xEC)  || // sub rsp, imm32
                    (p[0] == 0x40 && p[1] >= 0x53 && p[1] <= 0x57)  || // push r64
                    (p[0] == 0x55);                                     // push rbp
                spdlog::info("entity_hooks: {} at 0x{:X} prologue: {:02X} {:02X} {:02X} {}",
                             name, addr, p[0], p[1], p[2],
                             validPrologue ? "[OK]" : "[UNUSUAL]");
                if (!validPrologue) {
                    spdlog::warn("entity_hooks: {} has unexpected prologue — may crash on call", name);
                }
                return true; // Don't block on unusual prologue — SEH catches crashes
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::error("entity_hooks: {} at 0x{:X} — cannot read memory", name, addr);
                return false;
            }
        };

        // RootObjectFactory::create — the dispatcher called by 11 game systems.
        uintptr_t createAddr = modBase + 0x583400;
        if (validateFactoryFunc(createAddr, "FactoryCreate")) {
            s_factoryCreate = reinterpret_cast<CharacterCreateFn>(createAddr);
            spdlog::info("entity_hooks: FactoryCreate VALIDATED at 0x{:X}", createAddr);
        } else {
            spdlog::warn("entity_hooks: FactoryCreate at 0x{:X} FAILED validation — "
                         "mod template spawn disabled, will use createRandomChar or NPC hijack",
                         createAddr);
        }

        // RootObjectFactory::createRandomChar — creates random NPC character.
        uintptr_t createRandomAddr = modBase + 0x5836E0;
        if (validateFactoryFunc(createRandomAddr, "CreateRandomChar")) {
            s_factoryCreateRandomChar = reinterpret_cast<CharacterCreateFn>(createRandomAddr);
            spdlog::info("entity_hooks: CreateRandomChar VALIDATED at 0x{:X}", createRandomAddr);
        } else {
            spdlog::warn("entity_hooks: CreateRandomChar at 0x{:X} FAILED validation — "
                         "random char fallback disabled, will rely on NPC hijack only",
                         createRandomAddr);
        }

        // Store in GameFunctions for other subsystems
        core.GetGameFunctions().FactoryCreate = reinterpret_cast<void*>(
            s_factoryCreate ? createAddr : 0);
        core.GetGameFunctions().CreateRandomChar = reinterpret_cast<void*>(
            s_factoryCreateRandomChar ? createRandomAddr : 0);
    }

    if (funcs.CharacterSpawn) {
        s_createTargetAddr = reinterpret_cast<uintptr_t>(funcs.CharacterSpawn);

        char buf[128];
        sprintf_s(buf, "KMP: entity_hooks — hooking CharacterCreate at 0x%llX\n",
                  (unsigned long long)s_createTargetAddr);
        OutputDebugStringA(buf);

        if (!hookMgr.InstallAt("CharacterCreate",
                               s_createTargetAddr,
                               &Hook_CharacterCreate, &s_origCreate)) {
            spdlog::error("entity_hooks: Failed to hook CharacterCreate");
            OutputDebugStringA("KMP: entity_hooks — InstallAt FAILED\n");
            success = false;
        } else {
            // Get raw trampoline for reentrant calls
            void* rawTramp = hookMgr.GetRawTrampoline("CharacterCreate");
            if (rawTramp) {
                s_rawCreateTrampoline = reinterpret_cast<CharacterCreateFn>(rawTramp);
                spdlog::info("entity_hooks: Raw trampoline at 0x{:X}", reinterpret_cast<uintptr_t>(rawTramp));

                // Build direct call stub for CallFactoryDirect.
                // The stub does `mov rax, rsp; jmp rawTrampoline+3` — correct RAX
                // for functions that derive RBP from RAX (mov rax,rsp prologue).
                s_directCallStubAlloc = BuildDirectCallStub(rawTramp);
                if (s_directCallStubAlloc) {
                    s_directCallStub = reinterpret_cast<CharacterCreateFn>(s_directCallStubAlloc);
                    spdlog::info("entity_hooks: Direct call stub READY at 0x{:X}",
                                 reinterpret_cast<uintptr_t>(s_directCallStubAlloc));
                }
            }

            core.GetSpawnManager().SetOrigProcess(
                reinterpret_cast<FactoryProcessFn>(s_createTargetAddr));

            // Hook installed DISABLED during loading. The 130+ character creates
            // during game load fire through the MovRaxRsp wrapper whose global
            // RSP slots get corrupted, causing DEP violations (0x3FFD).
            // The hook is enabled in OnGameLoaded() after the loading burst
            // completes, so factory data is captured from the first runtime
            // NPC spawn (safe — single calls don't corrupt the wrapper).
            HookManager::Get().Disable("CharacterCreate");
            spdlog::info("entity_hooks: CharacterCreate installed DISABLED (enabled after loading)");
            OutputDebugStringA("KMP: entity_hooks — CharacterCreate installed DISABLED (safe during load)\n");
        }
    }

    // CharacterDestroy hook NOT installed
    s_destroyHookInstalled = false;

    spdlog::info("entity_hooks: Installed (create={}, destroy={})",
                 funcs.CharacterSpawn != nullptr, s_destroyHookInstalled);
    return success;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterCreate");
    if (s_destroyHookInstalled) {
        HookManager::Get().Remove("CharacterDestroy");
    }
    // Free direct call stub
    if (s_directCallStubAlloc) {
        VirtualFree(s_directCallStubAlloc, 0, MEM_RELEASE);
        s_directCallStubAlloc = nullptr;
        s_directCallStub = nullptr;
    }
}

void ResumeForNetwork() {
    // Reset per-player spawn caps for new connection
    {
        std::lock_guard lock(s_spawnsPerPlayerMutex);
        s_spawnsPerPlayer.clear();
    }

    // Reset connected-mode statics that don't reset naturally on reconnect
    s_connectedCreateNum.store(0);
    s_connectedBurstCount.store(0);
    s_inPlaceSpawnCount.store(0);
    s_earlyProbeCount.store(0);

    // Reset loading capture state so fresh factory/template/faction data is
    // captured on the next savegame load or reconnect.
    s_loadingCapturesDone = false;
    s_havePreCallData = false;
    s_savedFactory = nullptr;

    // Propagate early faction to PlayerController if we captured one during loading
    // (must read BEFORE resetting the atomic)
    uintptr_t earlyFac = s_earlyPlayerFaction.load(std::memory_order_relaxed);
    if (earlyFac != 0 && Core::Get().GetPlayerController().GetLocalFactionPtr() == 0) {
        const_cast<PlayerController&>(Core::Get().GetPlayerController())
            .SetLocalFactionPtr(earlyFac);
        spdlog::info("entity_hooks: ResumeForNetwork — set PlayerController faction to 0x{:X} (from loading capture)",
                     earlyFac);
    }

    // Reset faction capture state after propagation so next load captures fresh data
    s_earlyFactionLocked.store(false);
    s_earlyPlayerFaction.store(0);

    // Enable CharacterCreate hook for multiplayer.
    // At this point loading is complete — only single/few runtime spawns will
    // trigger the hook (new zone NPCs, remote player injection). MovRaxRsp
    // handles single calls fine; it's only the 130+ loading burst that crashes.
    if (HookManager::Get().Enable("CharacterCreate")) {
        spdlog::info("entity_hooks: ResumeForNetwork — CharacterCreate hook ENABLED");
    } else {
        spdlog::warn("entity_hooks: ResumeForNetwork — CharacterCreate Enable() returned false");
    }

    spdlog::info("entity_hooks: ResumeForNetwork — hook active for runtime spawns "
                 "(earlyFaction=0x{:X}, fallback=0x{:X})", earlyFac,
                 s_fallbackFaction.load(std::memory_order_relaxed));
}

void SuspendForDisconnect() {
    // Disable CharacterCreate hook when disconnecting from multiplayer.
    // Zone-load bursts (90+ creates) go through MovRaxRsp and corrupt the heap
    // when the hook body is active. With the hook disabled, zone loads are safe.
    if (HookManager::Get().Disable("CharacterCreate")) {
        spdlog::info("entity_hooks: SuspendForDisconnect — CharacterCreate hook DISABLED");
    } else {
        spdlog::warn("entity_hooks: SuspendForDisconnect — Disable() returned false");
    }
}

bool HasRecentInPlaceSpawn(int withinSeconds) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastInPlaceSpawnTime);
    return elapsed.count() < withinSeconds;
}

int GetInPlaceSpawnCount() {
    return s_inPlaceSpawnCount.load();
}

int GetTotalCreates() {
    return s_totalCreates.load();
}

int GetTotalDestroys() {
    return s_totalDestroys.load();
}

void* CallFactoryDirect(void* factory, void* requestStruct) {
    // Use the direct call stub which sets RAX = caller's RSP before entering
    // the original function body. Without this, the raw trampoline's
    // `mov rax, rsp` captures the wrong RSP, causing all [rbp+XX] accesses
    // to read/write wrong memory (sign-extended faction pointers, stack corruption).
    CharacterCreateFn callFn = s_directCallStub ? s_directCallStub : s_rawCreateTrampoline;
    if (!callFn) {
        spdlog::warn("entity_hooks: CallFactoryDirect — no trampoline available");
        return nullptr;
    }

    s_directSpawnBypass.store(true, std::memory_order_release);
    void* result = nullptr;
    __try {
        result = callFn(factory, requestStruct);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            spdlog::error("entity_hooks: CallFactoryDirect CRASHED (SEH caught, attempt {})", s_crashCount);
        }
    }
    s_directSpawnBypass.store(false, std::memory_order_release);

    return result;
}

void* CallFactoryCreate(void* factory, void* gameData) {
    // Call RootObjectFactory::create — the high-level dispatcher that builds
    // a proper request struct from a GameData* and calls process() internally.
    // This bypasses the stale-pointer problem entirely because create()
    // constructs FRESH internal pointers (faction, squad, AI, etc.).
    //
    // NOT hooked by MinHook, so no trampoline/stub issues. The CPU naturally
    // sets RAX = RSP after CALL pushes the return address, which is what the
    // mov rax, rsp prologue expects.
    if (!s_factoryCreate) {
        spdlog::warn("entity_hooks: CallFactoryCreate — function not resolved");
        return nullptr;
    }

    s_directSpawnBypass.store(true, std::memory_order_release);
    void* result = nullptr;
    __try {
        result = s_factoryCreate(factory, gameData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            spdlog::error("entity_hooks: CallFactoryCreate CRASHED (SEH caught, attempt {})", s_crashCount);
        }
    }
    s_directSpawnBypass.store(false, std::memory_order_release);
    return result;
}

void* CallFactoryCreateRandom(void* factory) {
    // Call RootObjectFactory::createRandomChar — creates a random NPC.
    // Takes just the factory pointer (RCX=factory, RDX=0).
    // Useful as last-resort when mod templates fail.
    if (!s_factoryCreateRandomChar) {
        spdlog::warn("entity_hooks: CallFactoryCreateRandom — function not resolved");
        return nullptr;
    }

    s_directSpawnBypass.store(true, std::memory_order_release);
    void* result = nullptr;
    __try {
        result = s_factoryCreateRandomChar(factory, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            spdlog::error("entity_hooks: CallFactoryCreateRandom CRASHED (SEH caught, attempt {})", s_crashCount);
        }
    }
    s_directSpawnBypass.store(false, std::memory_order_release);
    return result;
}

uintptr_t GetFallbackFaction() {
    return s_fallbackFaction.load(std::memory_order_relaxed);
}

uintptr_t GetEarlyPlayerFaction() {
    return s_earlyPlayerFaction.load(std::memory_order_relaxed);
}

int GetGameDataOffsetInStruct() {
    return s_gameDataOffsetInStruct;
}

int GetPositionOffsetInStruct() {
    return s_positionOffsetInStruct;
}

} // namespace kmp::entity_hooks

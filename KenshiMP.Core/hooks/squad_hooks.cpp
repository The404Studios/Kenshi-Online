#include "squad_hooks.h"
#include "kmp/hook_manager.h"
#include "kmp/patterns.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "../core.h"
#include "../game/game_types.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <deque>

namespace kmp::squad_hooks {

// ── Function typedefs ──
using SquadCreateFn    = void*(__fastcall*)(void* squadManager, void* templateData);
using SquadAddMemberFn = void(__fastcall*)(void* squad, void* character);

// ── State ──
static SquadCreateFn    s_origSquadCreate    = nullptr;
static SquadAddMemberFn s_origSquadAddMember = nullptr;
static int s_createCount = 0;
static int s_addMemberCount = 0;
static bool s_loading = false;

// Squad pointer → server-assigned net ID mapping.
// Populated when the server responds with S2C_SquadCreated.
static std::unordered_map<void*, uint32_t> s_squadPtrToNetId;
static std::deque<void*> s_pendingSquadPtrs; // Queued squad pointers awaiting server ID

// ── SEH wrappers ──

// SEH wrapper must NOT use C++ objects (MSVC C2712).
// Crash tracking is done via static counter + OutputDebugString.
static void* SEH_SquadCreate(void* squadManager, void* templateData) {
    __try {
        return s_origSquadCreate(squadManager, templateData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH_SquadCreate CRASHED #%d\n", s_crashCount);
            OutputDebugStringA(buf);
        }
        return nullptr;
    }
}

static bool SEH_SquadAddMember(void* squad, void* character) {
    __try {
        s_origSquadAddMember(squad, character);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── Hooks ──

static void* __fastcall Hook_SquadCreate(void* squadManager, void* templateData) {
    s_createCount++;

    void* result = SEH_SquadCreate(squadManager, templateData);
    if (!result) {
        spdlog::error("squad_hooks: SquadCreate crashed or returned null");
        return nullptr;
    }

    if (s_loading) return result;

    auto& core = Core::Get();
    if (!core.IsConnected()) return result;

    spdlog::info("squad_hooks: SquadCreate #{} (mgr=0x{:X}, template=0x{:X}) -> 0x{:X}",
                  s_createCount, (uintptr_t)squadManager, (uintptr_t)templateData,
                  (uintptr_t)result);

    // Queue this squad pointer so OnSquadNetIdAssigned() can map it
    // when the server responds with S2C_SquadCreated.
    s_pendingSquadPtrs.push_back(result);

    auto& registry = core.GetEntityRegistry();
    auto localEntities = registry.GetPlayerEntities(core.GetLocalPlayerId());
    EntityID creatorId = localEntities.empty() ? 0 : localEntities[0];

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_SquadCreate);
    writer.WriteU32(creatorId);
    writer.WriteString("Squad");
    core.GetClient().SendReliable(writer.Data(), writer.Size());

    return result;
}

static void __fastcall Hook_SquadAddMember(void* squad, void* character) {
    s_addMemberCount++;

    if (!SEH_SquadAddMember(squad, character)) {
        spdlog::error("squad_hooks: SquadAddMember crashed");
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    spdlog::info("squad_hooks: SquadAddMember #{} (squad=0x{:X}, char=0x{:X})",
                  s_addMemberCount, (uintptr_t)squad, (uintptr_t)character);

    auto& registry = core.GetEntityRegistry();
    EntityID memberNetId = registry.GetNetId(character);
    if (memberNetId == INVALID_ENTITY) return;

    // Look up the squad's server-assigned net ID from our mapping
    uint32_t squadNetId = 0;
    auto it = s_squadPtrToNetId.find(squad);
    if (it != s_squadPtrToNetId.end()) {
        squadNetId = it->second;
    } else {
        spdlog::warn("squad_hooks: SquadAddMember #{} - squad 0x{:X} has no net ID mapping",
                     s_addMemberCount, (uintptr_t)squad);
    }

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_SquadAddMember);
    MsgSquadMemberUpdate msg{};
    msg.squadNetId = squadNetId;
    msg.memberEntityId = memberNetId;
    msg.action = 0; // added
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());
}

// ── Install / Uninstall ──

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hooks = HookManager::Get();
    int installed = 0;

    // SquadCreate hook DISABLED — starts with `mov rax, rsp` (48 8B C4).
    // The raw trampoline appeared safe in theory but caused silent crashes
    // during zone loading when 100+ NPC squads are created rapidly.
    // SquadCreate sync is not needed for host — only SquadAddMember matters
    // (for AddCharacterToLocalSquad injection).
    if (funcs.SquadCreate) {
        spdlog::info("squad_hooks: SquadCreate SKIPPED (mov rax,rsp — crash risk during zone loads)");
    }

    // SquadAddMember hook DISABLED — fires 30-40+ times during zone loading
    // when NPC squads are assembled. Each call through the trampoline does entity
    // lookups + packet writes, causing cumulative corruption → crash ~10s later.
    // The raw function pointer is kept for AddCharacterToLocalSquad (direct call).
    if (funcs.SquadAddMember) {
        spdlog::info("squad_hooks: SquadAddMember at 0x{:X} — NOT hooked (zone-load crash risk). "
                     "Raw ptr kept for squad injection.",
                     reinterpret_cast<uintptr_t>(funcs.SquadAddMember));
    }

    spdlog::info("squad_hooks: {}/2 hooks installed", installed);
    return installed > 0;
}

void Uninstall() {
    auto& hooks = HookManager::Get();
    if (s_origSquadCreate)    hooks.Remove("SquadCreate");
    if (s_origSquadAddMember) hooks.Remove("SquadAddMember");
    s_origSquadCreate = nullptr;
    s_origSquadAddMember = nullptr;
    s_squadPtrToNetId.clear();
    s_pendingSquadPtrs.clear();
}

void OnSquadNetIdAssigned(uint32_t squadNetId) {
    if (s_pendingSquadPtrs.empty()) {
        spdlog::warn("squad_hooks: OnSquadNetIdAssigned({}) but no pending squad pointers", squadNetId);
        return;
    }
    void* squadPtr = s_pendingSquadPtrs.front();
    s_pendingSquadPtrs.pop_front();
    s_squadPtrToNetId[squadPtr] = squadNetId;
    spdlog::info("squad_hooks: Mapped squad 0x{:X} -> netId={}", (uintptr_t)squadPtr, squadNetId);
}

void SetLoading(bool loading) {
    s_loading = loading;
}

// ── Squad Injection (Engine Exploit) ──
// Adds a remote character to the local player's squad by calling the engine's
// own SquadAddMember function directly. This exploits the game's squad system
// to make the character appear in the squad panel, be selectable via click,
// and respond to group orders — as if it was naturally recruited.

static bool SEH_InjectIntoSquad(SquadAddMemberFn addFn, void* squad, void* character) {
    __try {
        addFn(squad, character);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spdlog::error("squad_hooks: SEH_InjectIntoSquad crashed (squad=0x{:X}, char=0x{:X})",
                       (uintptr_t)squad, (uintptr_t)character);
        return false;
    }
}

// SEH-protected pointer read for following chains
static uintptr_t SEH_ReadPtr(uintptr_t addr) {
    __try {
        return *reinterpret_cast<uintptr_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Try to resolve the activePlatoon from the primary character.
// The addMember function expects activePlatoon as 'this', not platoon.
// Research data (GOG): CharacterHuman+0x658 = activePlatoon*
//                       platoon+0x1D8 = activePlatoon*
// Steam offsets differ — we identify the activePlatoon by matching its vtable
// against the known vtable address from RTTI discovery.
static uintptr_t ResolveActivePlatoon(void* primaryChar) {
    uintptr_t charAddr = reinterpret_cast<uintptr_t>(primaryChar);
    uintptr_t moduleBase = Core::Get().GetScanner().GetBase();
    size_t moduleSize = Core::Get().GetScanner().GetSize();

    spdlog::info("squad_hooks: ResolveActivePlatoon: char=0x{:X}", charAddr);

    // Get the known ActivePlatoon vtable from RTTI discovery.
    // The orchestrator found SquadAddMember at vtable slot 2 (offset +0x10).
    // We can find the vtable base by scanning .rdata for a pointer table
    // that contains SquadAddMember at offset +0x10.
    auto& funcs = Core::Get().GetGameFunctions();
    uintptr_t knownVTable = 0;
    if (funcs.SquadAddMember) {
        uintptr_t addMemberAddr = reinterpret_cast<uintptr_t>(funcs.SquadAddMember);
        // The vtable is in .rdata (inside the module). Scan for a qword in .rdata
        // that equals addMemberAddr — the vtable base is 0x10 bytes before it.
        for (uintptr_t scan = moduleBase; scan < moduleBase + moduleSize - 0x20; scan += 8) {
            uintptr_t val = SEH_ReadPtr(scan);
            if (val == addMemberAddr) {
                // This is vtable slot 2 (offset +0x10), so vtable base = scan - 0x10
                knownVTable = scan - 0x10;
                spdlog::info("squad_hooks: Found ActivePlatoon vtable at 0x{:X} (slot2 at 0x{:X} = 0x{:X})",
                             knownVTable, scan, addMemberAddr);
                break;
            }
        }
    }

    // Validate candidate: check if object's vtable matches known ActivePlatoon vtable
    // Minimum 16MB: filters out SSO string data like "one" (0x656E6F) that passes
    // the old 0x10000 check and crashes SEH_ReadPtr + fills VEH log with AV noise.
    auto validateAP = [moduleBase, moduleSize, knownVTable](uintptr_t candidate, const char* source) -> bool {
        if (candidate < 0x1000000 || candidate > 0x00007FFFFFFFFFFF) return false;
        uintptr_t vtable = SEH_ReadPtr(candidate);
        if (vtable == 0) return false;

        // Primary check: exact vtable match against RTTI-discovered vtable
        if (knownVTable != 0 && vtable == knownVTable) {
            spdlog::info("squad_hooks: activePlatoon MATCHED via {} — 0x{:X} vtable=0x{:X} (RTTI match!)",
                         source, candidate, vtable);
            return true;
        }
        // Fallback: vtable in game module (works for GOG and some Steam configs)
        if (vtable >= moduleBase && vtable < moduleBase + moduleSize) {
            spdlog::info("squad_hooks: activePlatoon candidate via {} — 0x{:X} vtable=0x{:X} (in module)",
                         source, candidate, vtable);
            return true;
        }
        return false;
    };

    // Scan character struct from 0x600 to 0x780 (covers GOG 0x658 + Steam variants)
    for (int off = 0x600; off <= 0x780; off += 8) {
        uintptr_t ap = SEH_ReadPtr(charAddr + off);
        if (validateAP(ap, ([off]{ char b[16]; sprintf_s(b, "char+0x%X", off); return std::string(b); })().c_str())) {
            if (off != 0x658) {
                spdlog::info("squad_hooks: DISCOVERED activePlatoon at char+0x{:X} (differs from GOG 0x658)", off);
            }
            return ap;
        }
    }

    // Try via GetSquadPtr → platoon chain
    game::CharacterAccessor accessor(primaryChar);
    uintptr_t squadPtr = accessor.GetSquadPtr();
    if (squadPtr != 0) {
        spdlog::info("squad_hooks: GetSquadPtr=0x{:X}, trying platoon chains", squadPtr);
        for (int off = 0x1B0; off <= 0x220; off += 8) {
            uintptr_t ap = SEH_ReadPtr(squadPtr + off);
            if (validateAP(ap, ([off]{ char b[16]; sprintf_s(b, "squad+0x%X", off); return std::string(b); })().c_str())) {
                return ap;
            }
        }
        if (validateAP(squadPtr, "squadPtr direct")) return squadPtr;
    }

    // Diagnostic dump
    spdlog::warn("squad_hooks: activePlatoon NOT FOUND (knownVTable=0x{:X}) — dumping char+0x600..0x780:",
                 knownVTable);
    for (int off = 0x600; off <= 0x780; off += 8) {
        uintptr_t val = SEH_ReadPtr(charAddr + off);
        if (val > 0x1000000 && val < 0x00007FFFFFFFFFFF) {
            uintptr_t vtable = SEH_ReadPtr(val);
            spdlog::warn("  char+0x{:03X} = 0x{:X} vtable=0x{:X} {}{}",
                         off, val, vtable,
                         (vtable >= moduleBase && vtable < moduleBase + moduleSize) ? "IN_MODULE " : "",
                         (knownVTable != 0 && vtable == knownVTable) ? "** RTTI MATCH **" : "");
        }
    }

    return 0;
}

bool AddCharacterToLocalSquad(void* character) {
    if (!character) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — null character");
        return false;
    }

    // Resolve the SquadAddMember function — prefer the hook trampoline (bypasses
    // our hook to avoid recursive C2S_SquadAddMember sends), fall back to the
    // raw game function pointer from the scanner/vtable discovery.
    SquadAddMemberFn addFn = s_origSquadAddMember;
    if (!addFn) {
        auto& funcs = Core::Get().GetGameFunctions();
        addFn = reinterpret_cast<SquadAddMemberFn>(funcs.SquadAddMember);
    }
    if (!addFn) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — no SquadAddMember function "
                      "(hook not installed, scanner didn't find it, vtable discovery pending)");
        return false;
    }

    auto& core = Core::Get();

    // Get the local player's primary character to find their squad
    void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
    if (!primaryChar) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — no primary character found");
        return false;
    }

    // Resolve the activePlatoon — the addMember function operates on activePlatoon, not platoon.
    // CT research: activePlatoon vtable[2] = addMember(this=activePlatoon, character)
    uintptr_t activePlatoonPtr = ResolveActivePlatoon(primaryChar);
    if (activePlatoonPtr == 0) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — could not resolve activePlatoon");
        return false;
    }

    // Inject the remote character into the local player's squad
    void* squad = reinterpret_cast<void*>(activePlatoonPtr);
    bool ok = SEH_InjectIntoSquad(addFn, squad, character);

    if (ok) {
        spdlog::info("squad_hooks: SQUAD INJECTION SUCCESS — char 0x{:X} added to activePlatoon 0x{:X} "
                     "(fn=0x{:X}, via {})",
                     (uintptr_t)character, activePlatoonPtr, (uintptr_t)addFn,
                     (addFn == s_origSquadAddMember) ? "hook trampoline" : "raw game function");
    } else {
        spdlog::error("squad_hooks: SQUAD INJECTION FAILED — char 0x{:X}, activePlatoon 0x{:X}",
                       (uintptr_t)character, activePlatoonPtr);
    }

    return ok;
}

} // namespace kmp::squad_hooks

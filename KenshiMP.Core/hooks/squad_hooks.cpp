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

    if (funcs.SquadAddMember) {
        if (hooks.InstallAt("SquadAddMember", reinterpret_cast<uintptr_t>(funcs.SquadAddMember),
                            &Hook_SquadAddMember, &s_origSquadAddMember)) {
            installed++;
            spdlog::info("squad_hooks: SquadAddMember hook installed");
        }
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

bool AddCharacterToLocalSquad(void* character) {
    if (!character) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — null character");
        return false;
    }

    // Resolve the SquadAddMember function — prefer the hook trampoline (bypasses
    // our hook to avoid recursive C2S_SquadAddMember sends), fall back to the
    // raw game function pointer from the scanner.
    SquadAddMemberFn addFn = s_origSquadAddMember;
    if (!addFn) {
        auto& funcs = Core::Get().GetGameFunctions();
        addFn = reinterpret_cast<SquadAddMemberFn>(funcs.SquadAddMember);
    }
    if (!addFn) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — no SquadAddMember function "
                      "(hook not installed and scanner didn't find it)");
        return false;
    }

    auto& core = Core::Get();

    // Get the local player's primary character to find their squad
    void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
    if (!primaryChar) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — no primary character found");
        return false;
    }

    // Read the squad pointer from the primary character
    game::CharacterAccessor accessor(primaryChar);
    uintptr_t squadPtr = accessor.GetSquadPtr();
    if (squadPtr == 0) {
        spdlog::warn("squad_hooks: AddCharacterToLocalSquad — primary character has no squad");
        return false;
    }

    // Inject the remote character into the local player's squad
    void* squad = reinterpret_cast<void*>(squadPtr);
    bool ok = SEH_InjectIntoSquad(addFn, squad, character);

    if (ok) {
        spdlog::info("squad_hooks: SQUAD INJECTION SUCCESS — char 0x{:X} added to squad 0x{:X} "
                     "(fn=0x{:X}, via {})",
                     (uintptr_t)character, squadPtr, (uintptr_t)addFn,
                     (addFn == s_origSquadAddMember) ? "hook trampoline" : "raw game function");
    } else {
        spdlog::error("squad_hooks: SQUAD INJECTION FAILED — char 0x{:X}, squad 0x{:X}",
                       (uintptr_t)character, squadPtr);
    }

    return ok;
}

} // namespace kmp::squad_hooks

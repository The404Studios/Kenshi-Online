#include "faction_hooks.h"
#include "kmp/hook_manager.h"
#include "kmp/patterns.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::faction_hooks {

// ── Function typedefs ──
using FactionRelationFn = void(__fastcall*)(void* factionA, void* factionB, float relation);

// ── State ──
static FactionRelationFn s_origFactionRelation = nullptr;
static int s_relationChangeCount = 0;
static bool s_loading = false;
static bool s_serverSourced = false;

// ── SEH wrapper ──

static bool SEH_FactionRelation(void* factionA, void* factionB, float relation) {
    __try {
        s_origFactionRelation(factionA, factionB, relation);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── Hooks ──

static void __fastcall Hook_FactionRelation(void* factionA, void* factionB, float relation) {
    s_relationChangeCount++;

    if (!SEH_FactionRelation(factionA, factionB, relation)) {
        spdlog::error("faction_hooks: FactionRelation crashed");
        return;
    }

    if (s_loading || s_serverSourced) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    spdlog::info("faction_hooks: FactionRelation #{} (A=0x{:X}, B=0x{:X}, rel={:.2f})",
                  s_relationChangeCount, (uintptr_t)factionA, (uintptr_t)factionB, relation);

    uint32_t factionIdA = 0, factionIdB = 0;
    if (factionA) Memory::Read(reinterpret_cast<uintptr_t>(factionA) + 0x08, factionIdA);
    if (factionB) Memory::Read(reinterpret_cast<uintptr_t>(factionB) + 0x08, factionIdB);

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_FactionRelation);
    MsgFactionRelation msg{};
    msg.factionIdA = factionIdA;
    msg.factionIdB = factionIdB;
    msg.relation = relation;
    msg.causerEntityId = 0;
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());
}

// ── Install / Uninstall ──

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hooks = HookManager::Get();

    if (funcs.FactionRelation) {
        if (hooks.InstallAt("FactionRelation", reinterpret_cast<uintptr_t>(funcs.FactionRelation),
                            &Hook_FactionRelation, &s_origFactionRelation)) {
            spdlog::info("faction_hooks: FactionRelation hook installed");
            return true;
        }
    }

    spdlog::warn("faction_hooks: No hooks installed (FactionRelation pattern not resolved)");
    return false;
}

void Uninstall() {
    auto& hooks = HookManager::Get();
    if (s_origFactionRelation) hooks.Remove("FactionRelation");
    s_origFactionRelation = nullptr;
}

void SetLoading(bool loading) {
    s_loading = loading;
}

void SetServerSourced(bool sourced) {
    s_serverSourced = sourced;
}

FactionRelationFn GetOriginal() {
    return s_origFactionRelation;
}

} // namespace kmp::faction_hooks

#include "movement_hooks.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/hook_manager.h"
#include "kmp/protocol.h"
#include "kmp/constants.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace kmp::movement_hooks {

// ── Function Types ──
using SetPositionFn = void(__fastcall*)(void* character, float x, float y, float z);
using MoveToFn = void(__fastcall*)(void* character, float x, float y, float z, int moveType);

static SetPositionFn s_origSetPosition = nullptr;
static MoveToFn      s_origMoveTo      = nullptr;

// Position send throttle
static auto s_lastPositionSend = std::chrono::steady_clock::now();

// ── Hooks ──

static void __fastcall Hook_SetPosition(void* character, float x, float y, float z) {
    // Call original first
    s_origSetPosition(character, x, y, z);

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    // Check if this is a local player character
    auto& registry = core.GetEntityRegistry();
    EntityID netId = registry.GetNetId(character);
    if (netId == INVALID_ENTITY) return;

    auto* info = registry.GetInfo(netId);
    if (!info || info->ownerPlayerId != core.GetLocalPlayerId()) return;

    // Throttle position updates to tick rate
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPositionSend);
    if (elapsed.count() < KMP_TICK_INTERVAL_MS) return;

    // Check if position changed enough to send
    Vec3 newPos(x, y, z);
    if (newPos.DistanceTo(info->lastPosition) < KMP_POS_CHANGE_THRESHOLD) return;

    s_lastPositionSend = now;

    // Read rotation and movement data from the character object
    game::CharacterAccessor accessor(character);
    Quat rotation = accessor.GetRotation();
    uint32_t compQuat = rotation.Compress();

    // Compute moveSpeed from position delta (reliable fallback when offset = -1)
    float dist = newPos.DistanceTo(info->lastPosition);
    float elapsedSec = elapsed.count() / 1000.f;
    float computedSpeed = (elapsedSec > 0.001f) ? dist / elapsedSec : 0.f;

    // Try memory read first; fall back to computed speed
    float moveSpeed = accessor.GetMoveSpeed();
    if (moveSpeed <= 0.f && computedSpeed > 0.f) {
        moveSpeed = computedSpeed;
    }

    // Derive animation state from speed when offset is unavailable
    uint8_t animState = accessor.GetAnimState();
    if (animState == 0 && moveSpeed > 0.5f) {
        animState = (moveSpeed > 5.0f) ? 2 : 1; // 1=walking, 2=running
    }

    // Map move speed (0..15 m/s) to uint8 (0..255)
    uint8_t moveSpeedU8 = static_cast<uint8_t>(
        std::min(255.f, moveSpeed / 15.f * 255.f));

    // Determine flags
    uint16_t flags = 0;
    if (moveSpeed > 3.0f) flags |= 0x01; // running

    // Send position update
    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_PositionUpdate);
    writer.WriteU8(1); // one character
    writer.WriteU32(netId);
    writer.WriteF32(x);
    writer.WriteF32(y);
    writer.WriteF32(z);
    writer.WriteU32(compQuat);
    writer.WriteU8(animState);
    writer.WriteU8(moveSpeedU8);
    writer.WriteU16(flags);

    core.GetClient().SendUnreliable(writer.Data(), writer.Size());

    // Update local tracking
    registry.UpdatePosition(netId, newPos);
    registry.UpdateRotation(netId, rotation);
}

static void __fastcall Hook_MoveTo(void* character, float x, float y, float z, int moveType) {
    // Call original
    s_origMoveTo(character, x, y, z, moveType);

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    EntityID netId = core.GetEntityRegistry().GetNetId(character);
    if (netId == INVALID_ENTITY) return;

    auto* info = core.GetEntityRegistry().GetInfo(netId);
    if (!info || info->ownerPlayerId != core.GetLocalPlayerId()) return;

    // Send move command
    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_MoveCommand);
    writer.WriteU32(netId);
    writer.WriteF32(x);
    writer.WriteF32(y);
    writer.WriteF32(z);
    writer.WriteU8(static_cast<uint8_t>(moveType));

    core.GetClient().SendReliable(writer.Data(), writer.Size());
}

// ── Install/Uninstall ──

bool Install() {
    auto& core = Core::Get();
    auto& hookMgr = HookManager::Get();
    auto& funcs = core.GetGameFunctions();

    if (funcs.CharacterSetPosition) {
        hookMgr.InstallAt("CharacterSetPosition",
                          reinterpret_cast<uintptr_t>(funcs.CharacterSetPosition),
                          &Hook_SetPosition, &s_origSetPosition);
    }

    if (funcs.CharacterMoveTo) {
        hookMgr.InstallAt("CharacterMoveTo",
                          reinterpret_cast<uintptr_t>(funcs.CharacterMoveTo),
                          &Hook_MoveTo, &s_origMoveTo);
    }

    spdlog::info("movement_hooks: Installed (setPos={}, moveTo={})",
                 funcs.CharacterSetPosition != nullptr, funcs.CharacterMoveTo != nullptr);
    return true;
}

void Uninstall() {
    HookManager::Get().Remove("CharacterSetPosition");
    HookManager::Get().Remove("CharacterMoveTo");
}

} // namespace kmp::movement_hooks

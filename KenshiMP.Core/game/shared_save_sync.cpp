#include "shared_save_sync.h"
#include "game_types.h"
#include "../core.h"
#include "../hooks/char_tracker_hooks.h"
#include "../hooks/ai_hooks.h"
#include "kmp/protocol.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <mutex>
#include <chrono>
#include <Windows.h>

namespace kmp::shared_save_sync {

// ═══════════════════════════════════════════════════════════════════════════
//  SHARED-SAVE SYNC
//
//  Both players load the SAME save with kenshi-online.mod.
//  The mod defines "Player 1" and "Player 2" factions + characters.
//  Server assigns each player a faction string:
//    "10-kenshi-online" → you control "Player 1", other player is "Player 2"
//    "12-kenshi-online" → you control "Player 2", other player is "Player 1"
//  Characters already exist in the save — no factory spawning needed.
//  We just find them by name and sync positions.
// ═══════════════════════════════════════════════════════════════════════════

// ── State ──
static std::string s_ownCharName;
static std::string s_otherCharName;

static void* s_ownAnimClass = nullptr;
static void* s_otherAnimClass = nullptr;
static void* s_ownCharPtr = nullptr;
static void* s_otherCharPtr = nullptr;

static bool s_initialized = false;
static bool s_ownFound = false;
static bool s_otherFound = false;

// Position sending throttle
static auto s_lastPosSend = std::chrono::steady_clock::time_point{};
static constexpr int POS_SEND_INTERVAL_MS = 50; // 20 Hz position updates

// Discovery retry
static auto s_lastDiscoveryLog = std::chrono::steady_clock::time_point{};
static int s_discoveryAttempts = 0;

// Remote position — mutex-protected because OnRemotePositionReceived is called
// from the network thread while Update reads from the game thread.
static std::mutex s_remoteMutex;
static Vec3 s_remotePosition{0, 0, 0};
static bool s_hasRemotePosition = false;
static std::atomic<float> s_remoteGameSpeed{-1.f};

// ── Faction string → character name mapping ──
static std::string FactionToOwnName(const std::string& faction) {
    if (faction == "10-kenshi-online") return "Player 1";
    if (faction == "12-kenshi-online") return "Player 2";
    return "";
}

static std::string FactionToOtherName(const std::string& faction) {
    if (faction == "10-kenshi-online") return "Player 2";
    if (faction == "12-kenshi-online") return "Player 1";
    return "";
}

void Init() {
    auto& lobby = Core::Get().GetLobbyManager();
    if (!lobby.HasFaction()) {
        spdlog::warn("shared_save_sync: Init — no faction yet (will retry in Update)");
        return;
    }

    std::string faction = lobby.GetFactionString();
    s_ownCharName = FactionToOwnName(faction);
    s_otherCharName = FactionToOtherName(faction);

    if (s_ownCharName.empty() || s_otherCharName.empty()) {
        spdlog::error("shared_save_sync: Unknown faction '{}' — cannot determine character names", faction);
        return;
    }

    s_initialized = true;
    s_ownFound = false;
    s_otherFound = false;
    s_ownAnimClass = nullptr;
    s_otherAnimClass = nullptr;
    s_ownCharPtr = nullptr;
    s_otherCharPtr = nullptr;
    {
        std::lock_guard lock(s_remoteMutex);
        s_hasRemotePosition = false;
        s_remotePosition = {0, 0, 0};
    }
    s_remoteGameSpeed.store(-1.f);
    s_discoveryAttempts = 0;

    spdlog::info("shared_save_sync: Initialized — own='{}' other='{}'",
                 s_ownCharName, s_otherCharName);
    Core::Get().GetNativeHud().AddSystemMessage(
        "Shared save sync: you are " + s_ownCharName + ", looking for " + s_otherCharName + "...");
}

void Reset() {
    s_initialized = false;
    s_ownFound = false;
    s_otherFound = false;
    s_ownAnimClass = nullptr;
    s_otherAnimClass = nullptr;
    s_ownCharPtr = nullptr;
    s_otherCharPtr = nullptr;
    s_ownCharName.clear();
    s_otherCharName.clear();
    {
        std::lock_guard lock(s_remoteMutex);
        s_hasRemotePosition = false;
    }
    s_remoteGameSpeed.store(-1.f);
    spdlog::info("shared_save_sync: Reset");
}

// ── SEH-protected position read from AnimClass chain ──
static bool SEH_ReadAnimClassPosition(void* animClass, Vec3& out) {
    __try {
        uintptr_t animPtr = reinterpret_cast<uintptr_t>(animClass);
        if (animPtr < 0x10000 || animPtr > 0x00007FFFFFFFFFFF) return false;

        uintptr_t charMovement = 0;
        if (!Memory::Read(animPtr + 0xC0, charMovement) || charMovement == 0) return false;
        if (charMovement < 0x10000 || charMovement > 0x00007FFFFFFFFFFF) return false;

        uintptr_t posStruct = 0;
        if (!Memory::Read(charMovement + 0x320, posStruct) || posStruct == 0) return false;
        if (posStruct < 0x10000 || posStruct > 0x00007FFFFFFFFFFF) return false;

        Memory::Read(posStruct + 0x20, out.x);
        Memory::Read(posStruct + 0x24, out.y);
        Memory::Read(posStruct + 0x28, out.z);

        return (out.x != 0.f || out.y != 0.f || out.z != 0.f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── SEH-protected position write to AnimClass chain ──
static bool SEH_WriteAnimClassPosition(void* animClass, const Vec3& pos) {
    __try {
        uintptr_t animPtr = reinterpret_cast<uintptr_t>(animClass);
        if (animPtr < 0x10000 || animPtr > 0x00007FFFFFFFFFFF) return false;

        uintptr_t charMovement = 0;
        if (!Memory::Read(animPtr + 0xC0, charMovement) || charMovement == 0) return false;
        if (charMovement < 0x10000 || charMovement > 0x00007FFFFFFFFFFF) return false;

        uintptr_t posStruct = 0;
        if (!Memory::Read(charMovement + 0x320, posStruct) || posStruct == 0) return false;
        if (posStruct < 0x10000 || posStruct > 0x00007FFFFFFFFFFF) return false;

        Memory::Write(posStruct + 0x20, pos.x);
        Memory::Write(posStruct + 0x24, pos.y);
        Memory::Write(posStruct + 0x28, pos.z);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SEH_WriteCachedPosition(void* charPtr, const Vec3& pos) {
    if (!charPtr) return;
    __try {
        uintptr_t charAddr = reinterpret_cast<uintptr_t>(charPtr);
        Memory::Write(charAddr + 0x48, pos.x);
        Memory::Write(charAddr + 0x4C, pos.y);
        Memory::Write(charAddr + 0x50, pos.z);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void Update(float deltaTime) {
    auto& core = Core::Get();
    if (!core.IsConnected() || !core.IsGameLoaded()) return;

    // ── LAZY INIT: faction assignment arrives AFTER SetConnected(true) ──
    // Init() is called from SetConnected but faction isn't assigned yet.
    // Retry here every tick until the faction arrives.
    if (!s_initialized) {
        auto& lobby = core.GetLobbyManager();
        if (lobby.HasFaction()) {
            Init(); // Now the faction is available
        }
        if (!s_initialized) return;
    }

    // ── STEP 1: Discover characters by name ──
    if (!s_ownFound || !s_otherFound) {
        s_discoveryAttempts++;

        // Re-validate cached pointers on every discovery tick (handles zone changes)
        if (!s_ownFound) {
            auto* tc = char_tracker_hooks::FindByName(s_ownCharName);
            if (tc && tc->animClassPtr) {
                s_ownAnimClass = tc->animClassPtr;
                s_ownCharPtr = tc->characterPtr;
                s_ownFound = true;
                spdlog::info("shared_save_sync: Found OWN character '{}' animClass=0x{:X}",
                             s_ownCharName, reinterpret_cast<uintptr_t>(s_ownAnimClass));
                core.GetNativeHud().AddSystemMessage("Found your character: " + s_ownCharName);
            }
        }

        if (!s_otherFound) {
            auto* tc = char_tracker_hooks::FindByName(s_otherCharName);
            if (tc && tc->animClassPtr) {
                s_otherAnimClass = tc->animClassPtr;
                s_otherCharPtr = tc->characterPtr;
                s_otherFound = true;

                if (s_otherCharPtr) {
                    ai_hooks::MarkRemoteControlled(s_otherCharPtr);
                }

                spdlog::info("shared_save_sync: Found OTHER character '{}' animClass=0x{:X}",
                             s_otherCharName, reinterpret_cast<uintptr_t>(s_otherAnimClass));
                core.GetNativeHud().AddSystemMessage("Found remote player: " + s_otherCharName);
            }
        }

        // Periodic status log
        auto now = std::chrono::steady_clock::now();
        auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(now - s_lastDiscoveryLog);
        if (sinceLog.count() >= 5) {
            s_lastDiscoveryLog = now;
            if (!s_ownFound || !s_otherFound) {
                core.GetNativeHud().AddSystemMessage(
                    "Looking for characters... (tracked: " +
                    std::to_string(char_tracker_hooks::GetTrackedCount()) + ")");
            }
        }

        if (!s_ownFound || !s_otherFound) return;

        core.GetNativeHud().AddSystemMessage("Both players found! Position sync active.");
        spdlog::info("shared_save_sync: BOTH CHARACTERS FOUND — sync active");
    } else {
        // Re-validate AnimClass pointers periodically (handles zone-load recreation)
        static int s_revalidateCounter = 0;
        if (++s_revalidateCounter % 300 == 0) { // Every ~5 seconds at 60fps
            auto* tc = char_tracker_hooks::FindByName(s_ownCharName);
            if (tc && tc->animClassPtr != s_ownAnimClass) {
                s_ownAnimClass = tc->animClassPtr;
                s_ownCharPtr = tc->characterPtr;
                spdlog::debug("shared_save_sync: Own animClass updated to 0x{:X}",
                              reinterpret_cast<uintptr_t>(s_ownAnimClass));
            }
            auto* tc2 = char_tracker_hooks::FindByName(s_otherCharName);
            if (tc2 && tc2->animClassPtr != s_otherAnimClass) {
                s_otherAnimClass = tc2->animClassPtr;
                s_otherCharPtr = tc2->characterPtr;
                if (s_otherCharPtr) ai_hooks::MarkRemoteControlled(s_otherCharPtr);
                spdlog::debug("shared_save_sync: Other animClass updated to 0x{:X}",
                              reinterpret_cast<uintptr_t>(s_otherAnimClass));
            }
        }
    }

    // ── STEP 2: Read own position and send to server ──
    // Uses the EXISTING C2S_PositionUpdate format that the server already handles.
    // The server stores position on the ConnectedPlayer and broadcasts via
    // S2C_PositionUpdate to other clients.
    auto now = std::chrono::steady_clock::now();
    auto sinceSend = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPosSend);
    if (sinceSend.count() >= POS_SEND_INTERVAL_MS && s_ownAnimClass) {
        s_lastPosSend = now;

        Vec3 myPos;
        if (SEH_ReadAnimClassPosition(s_ownAnimClass, myPos)) {
            // Use the existing position update format — the server reads:
            // U32(sourcePlayer) [handled by server from peer], U8(count), then
            // CharacterPosition structs. We need to match this EXACTLY.
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_PositionUpdate);
            // The server reads sourcePlayer as U32 first, but the canonical client
            // code (core.cpp PollLocalPositions) writes U8(count) first, then
            // CharacterPosition structs. Let me match the canonical format.
            writer.WriteU8(1); // count = 1 (FIX: was U32, must be U8)

            // CharacterPosition struct — must match the server's ReadRaw size
            CharacterPosition cp{};
            cp.entityId = 0; // Shared-save mode uses entityId 0 as "player avatar"
            cp.posX = myPos.x;
            cp.posY = myPos.y;
            cp.posZ = myPos.z;
            cp.compressedQuat = 0;
            cp.animStateId = 0;
            cp.moveSpeed = 0;
            cp.flags = 0;
            writer.WriteRaw(&cp, sizeof(cp));

            core.GetClient().SendUnreliable(writer.Data(), writer.Size());
        }
    }

    // ── STEP 3: Write received position to other player's character ──
    {
        Vec3 remotePos;
        bool hasRemote;
        {
            std::lock_guard lock(s_remoteMutex);
            remotePos = s_remotePosition;
            hasRemote = s_hasRemotePosition;
        }
        if (hasRemote && s_otherAnimClass) {
            SEH_WriteAnimClassPosition(s_otherAnimClass, remotePos);
            SEH_WriteCachedPosition(s_otherCharPtr, remotePos);
        }
    }

    // ── STEP 4: Game speed sync ──
    float speed = s_remoteGameSpeed.load();
    if (speed >= 0.f) {
        uintptr_t gwSingleton = core.GetGameFunctions().GameWorldSingleton;
        if (gwSingleton != 0) {
            game::GameWorldAccessor gw(gwSingleton);
            if (gw.IsValid()) {
                float currentSpeed = gw.GetGameSpeed();
                if (std::abs(currentSpeed - speed) > 0.01f) {
                    gw.WriteGameSpeed(speed);
                }
            }
        }
        s_remoteGameSpeed.store(-1.f);
    }
}

void OnRemotePositionReceived(const Vec3& pos) {
    std::lock_guard lock(s_remoteMutex);
    s_remotePosition = pos;
    s_hasRemotePosition = true;
}

void OnRemoteGameSpeedReceived(float speed) {
    s_remoteGameSpeed.store(speed);
}

bool IsOwnCharacterFound() { return s_ownFound; }
bool IsOtherCharacterFound() { return s_otherFound; }
const std::string& GetOwnCharacterName() { return s_ownCharName; }
const std::string& GetOtherCharacterName() { return s_otherCharName; }

} // namespace kmp::shared_save_sync

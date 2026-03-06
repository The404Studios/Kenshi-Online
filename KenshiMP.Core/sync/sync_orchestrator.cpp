#include "sync_orchestrator.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/asset_facilitator.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/ai_hooks.h"
#include "../hooks/squad_hooks.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <Windows.h>

namespace kmp {

// ════════════════════════════════════════════════════════════════════════════
// SEH-protected wrappers (must be free functions — __try forbids C++ unwind)
// ════════════════════════════════════════════════════════════════════════════

static bool SEH_WritePositionRotation(void* gameObj, Vec3 pos, Quat rot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        accessor.WritePosition(pos);

        auto& offsets = game::GetOffsets().character;
        if (offsets.rotation >= 0) {
            uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
            Memory::Write(charPtr + offsets.rotation, rot);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SyncOrch SEH_WritePositionRotation CRASHED for 0x%p\n", gameObj);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

static bool SEH_ReadPosition(void* gameObj, Vec3& outPos, Quat& outRot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        outPos = accessor.GetPosition();
        outRot = accessor.GetRotation();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct BGReadResult {
    Vec3 pos;
    Quat rot;
    float speed;
    uint8_t animState;
    bool valid;
};

static BGReadResult SEH_ReadCharacterBG(void* gameObj) {
    BGReadResult r = {};
    __try {
        game::CharacterAccessor character(gameObj);
        if (!character.IsValid()) return r;
        r.pos = character.GetPosition();
        r.rot = character.GetRotation();
        r.speed = character.GetMoveSpeed();
        r.animState = character.GetAnimState();
        r.valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SyncOrch SEH_ReadCharacterBG CRASHED for 0x%p\n", gameObj);
            OutputDebugStringA(buf);
        }
        r.valid = false;
    }
    return r;
}

// Fix stale faction pointer on a spawned character by copying the LIVE
// faction from the host's primary character.  Must be called BEFORE the
// game engine's next character-update tick to prevent a use-after-free
// crash at game+0x927E94 (reads faction+0x250 on every character).
static bool SEH_FixUpFaction(void* spawnedChar) {
    __try {
        void* primaryChar = Core::Get().GetPlayerController().GetPrimaryCharacter();
        if (!primaryChar) return false;

        uintptr_t primaryPtr = reinterpret_cast<uintptr_t>(primaryChar);
        uintptr_t spawnedPtr = reinterpret_cast<uintptr_t>(spawnedChar);

        // Read LIVE faction from the host's primary character (always in a loaded zone)
        uintptr_t faction = 0;
        Memory::Read(primaryPtr + 0x10, faction);
        if (faction == 0 || faction < 0x10000 || faction > 0x00007FFFFFFFFFFF)
            return false;

        // Validate: the faction object should be readable (not freed)
        uintptr_t vtable = 0;
        Memory::Read(faction, vtable);
        if (vtable == 0) return false;

        // Write to spawned character
        Memory::Write(spawnedPtr + 0x10, faction);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_FallbackPostSpawnSetup(void* character, EntityID netId,
                                        PlayerID owner, Vec3 pos) {
    __try {
        game::CharacterAccessor accessor(character);
        if (pos.x != 0.f || pos.y != 0.f || pos.z != 0.f) {
            accessor.WritePosition(pos);
        }
        Core::Get().GetPlayerController().OnRemoteCharacterSpawned(
            netId, character, owner);
        ai_hooks::MarkRemoteControlled(character);
        squad_hooks::AddCharacterToLocalSquad(character);
        game::WritePlayerControlled(
            reinterpret_cast<uintptr_t>(character), true);
        game::ScheduleDeferredAnimClassProbe(
            reinterpret_cast<uintptr_t>(character));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 10) {
            char buf[256];
            sprintf_s(buf, "KMP: SyncOrch SEH_FallbackPostSpawnSetup CRASHED entity %u char=0x%p\n",
                      netId, character);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Construction / Lifecycle
// ════════════════════════════════════════════════════════════════════════════

SyncOrchestrator::SyncOrchestrator(EntityRegistry& registry,
                                   PlayerController& playerCtrl,
                                   Interpolation& interp,
                                   SpawnManager& spawnMgr,
                                   NetworkClient& client,
                                   TaskOrchestrator& taskOrch)
    : m_registry(registry)
    , m_playerController(playerCtrl)
    , m_interpolation(interp)
    , m_spawnManager(spawnMgr)
    , m_client(client)
    , m_taskOrchestrator(taskOrch)
    , m_resolver(registry)
    , m_zoneEngine(registry)
    , m_playerEngine(playerCtrl)
{
    spdlog::info("SyncOrchestrator: Constructed");
}

void SyncOrchestrator::Initialize(PlayerID localId, const std::string& playerName) {
    m_localPlayerId = localId;
    m_active = true;
    m_tickCount = 0;

    m_playerEngine.OnHandshakeAck(localId, playerName);

    // Wire ZoneEngine's send callback to the network client
    m_zoneEngine.SetSendCallback([this](const uint8_t* data, size_t size, int channel, bool reliable) {
        if (reliable) {
            m_client.SendReliable(data, size);
        } else {
            m_client.SendUnreliable(data, size);
        }
    });

    auto now = std::chrono::steady_clock::now();
    m_lastZoneRebuild = now;
    m_lastPollTime = now;
    m_lastSpawnLog = now;
    m_lastDiagLog = now;

    spdlog::info("SyncOrchestrator: Initialized for player {} '{}'", localId, playerName);
}

void SyncOrchestrator::Shutdown() {
    m_active = false;
    spdlog::info("SyncOrchestrator: Shutdown");
}

void SyncOrchestrator::Reset() {
    m_active = false;
    m_pipelineStarted = false;
    m_tickCount = 0;
    m_localPlayerId = INVALID_PLAYER;
    m_writeBuffer = 0;
    m_readBuffer = 1;

    m_frameData[0].Clear();
    m_frameData[1].Clear();

    m_resolver.ClearInterest(m_localPlayerId);
    m_zoneEngine.Reset();
    m_playerEngine.Reset();

    // Reset spawn state
    m_hasPendingTimer = false;
    m_directSpawnAttempts = 0;
    m_shownWaitingMsg = false;
    m_shownTimeoutMsg = false;
    m_heapScanned = false;

    spdlog::info("SyncOrchestrator: Reset");
}

// ════════════════════════════════════════════════════════════════════════════
// Main Tick
// ════════════════════════════════════════════════════════════════════════════

bool SyncOrchestrator::Tick(float deltaTime) {
    if (!m_active || m_localPlayerId == INVALID_PLAYER) return false;

    // Debug: log every tick for first 50, then every 100th
    if (m_tickCount <= 50 || m_tickCount % 100 == 0) {
        spdlog::debug("SyncOrch::Tick #{} dt={:.4f} entities={} active={}",
                      m_tickCount, deltaTime,
                      m_registry.GetPlayerEntities(m_localPlayerId).size(),
                      m_active);
    }

    // Stage 1: Update zone state
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage1 UpdateZones");
    StageUpdateZones();

    // Stage 2: Wait for previous frame's background work + swap buffers
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage2 SwapBuffers");
    StageSwapBuffers();

    // Stage 3: Apply interpolated positions to remote game objects
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage3 ApplyRemotePositions");
    StageApplyRemotePositions();

    // Stage 4: Poll local entity positions and send updates
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage4 PollAndSendPositions");
    StagePollAndSendPositions();

    // Stage 5: Process spawn queue
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage5 ProcessSpawns");
    StageProcessSpawns();

    // Stage 6: Kick background work for this frame
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage6 KickBackgroundWork");
    StageKickBackgroundWork();

    // Stage 7: Update player states + diagnostics
    if (m_tickCount <= 5) spdlog::debug("SyncOrch: Stage7 UpdatePlayers");
    StageUpdatePlayers(deltaTime);

    m_tickCount++;
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Pipeline Stages
// ════════════════════════════════════════════════════════════════════════════

void SyncOrchestrator::StageUpdateZones() {
    // Get local player position from primary character
    void* primaryChar = m_playerController.GetPrimaryCharacter();
    if (!primaryChar) return;

    Vec3 pos;
    Quat rot;
    if (!SEH_ReadPosition(primaryChar, pos, rot)) return;
    if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) return;

    // Update local player zone
    bool zoneChanged = m_zoneEngine.UpdateLocalPlayerZone(pos);
    if (zoneChanged) {
        m_playerEngine.SetLocalZone(m_zoneEngine.GetLocalZone());
    }

    // Rebuild zone index periodically
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastZoneRebuild);
    if (elapsed.count() >= ZONE_REBUILD_INTERVAL_MS || zoneChanged) {
        m_zoneEngine.RebuildZoneIndex();
        m_lastZoneRebuild = now;
    }
}

void SyncOrchestrator::StageSwapBuffers() {
    if (m_pipelineStarted) {
        m_taskOrchestrator.WaitForFrameWork();
        std::swap(m_readBuffer, m_writeBuffer);
    }
}

void SyncOrchestrator::StageApplyRemotePositions() {
    // Update interpolation timers
    // Note: deltaTime is managed by Interpolation::Update which is called from Core
    // We just apply the results from the read buffer.

    if (!m_pipelineStarted) return;

    auto& readFrame = m_frameData[m_readBuffer];
    if (!readFrame.ready) return;

    int applied = 0;

    for (auto& result : readFrame.remoteResults) {
        if (!result.valid) continue;

        void* gameObj = m_registry.GetGameObject(result.netId);
        if (!gameObj) continue;

        if (SEH_WritePositionRotation(gameObj, result.position, result.rotation)) {
            m_registry.UpdatePosition(result.netId, result.position);
            m_registry.UpdateRotation(result.netId, result.rotation);
            applied++;
        } else {
            // Unlink bad game object
            m_registry.SetGameObject(result.netId, nullptr);
        }
    }

    if (applied > 0 && (m_tickCount <= 5 || m_tickCount % 100 == 0)) {
        spdlog::info("SyncOrch::ApplyRemote: applied {} this frame (tick={})",
                     applied, m_tickCount);
    }
}

void SyncOrchestrator::StagePollAndSendPositions() {
    if (m_localPlayerId == INVALID_PLAYER) return;

    // Throttle to tick rate
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPollTime);
    if (elapsed.count() < KMP_TICK_INTERVAL_MS) return;
    m_lastPollTime = now;

    auto localEntities = m_registry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) return;

    for (EntityID netId : localEntities) {
        auto* info = m_registry.GetInfo(netId);
        if (!info) continue;

        void* gameObj = m_registry.GetGameObject(netId);
        if (!gameObj) continue;

        Vec3 pos;
        Quat rotation;
        if (!SEH_ReadPosition(gameObj, pos, rotation)) {
            m_registry.SetGameObject(netId, nullptr);
            continue;
        }

        if (pos.DistanceTo(info->lastPosition) < KMP_POS_CHANGE_THRESHOLD) continue;

        float elapsedSec = elapsed.count() / 1000.f;
        float dist = pos.DistanceTo(info->lastPosition);
        float moveSpeed = (elapsedSec > 0.001f) ? dist / elapsedSec : 0.f;

        uint32_t compQuat = rotation.Compress();
        uint8_t animState = 0;
        if (moveSpeed > 5.0f) animState = 2;
        else if (moveSpeed > 0.5f) animState = 1;

        uint8_t moveSpeedU8 = static_cast<uint8_t>(
            std::min(255.f, moveSpeed / 15.f * 255.f));
        uint16_t flags = (moveSpeed > 3.0f) ? 0x01 : 0x00;

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(1);
        writer.WriteU32(netId);
        writer.WriteF32(pos.x);
        writer.WriteF32(pos.y);
        writer.WriteF32(pos.z);
        writer.WriteU32(compQuat);
        writer.WriteU8(animState);
        writer.WriteU8(moveSpeedU8);
        writer.WriteU16(flags);

        m_client.SendUnreliable(writer.Data(), writer.Size());

        m_registry.UpdatePosition(netId, pos);
        m_registry.UpdateRotation(netId, rotation);
    }

    // Also send cached packets from background read buffer
    if (m_pipelineStarted) {
        auto& readFrame = m_frameData[m_readBuffer];
        if (readFrame.ready && !readFrame.packetBytes.empty()) {
            m_client.SendUnreliable(readFrame.packetBytes.data(), readFrame.packetBytes.size());
        }
    }
}

void SyncOrchestrator::StageProcessSpawns() {
    auto& nativeHud = Core::Get().GetNativeHud();

    // One-shot heap scan
    if (!m_heapScanned && m_spawnManager.IsReady()) {
        if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() < 10) {
            spdlog::info("SyncOrch: Triggering GameData heap scan...");
            m_spawnManager.ScanGameDataHeap();
            m_heapScanned = true;
            spdlog::info("SyncOrch: Heap scan complete, {} templates", m_spawnManager.GetTemplateCount());
        }
    }

    size_t pending = m_spawnManager.GetPendingSpawnCount();

    // Timer tracking
    if (pending > 0 && !m_hasPendingTimer) {
        m_firstPendingTime = std::chrono::steady_clock::now();
        m_hasPendingTimer = true;
        m_shownWaitingMsg = false;
        m_shownTimeoutMsg = false;
        spdlog::info("SyncOrch: {} spawn(s) queued", pending);
        nativeHud.LogStep("GAME", std::to_string(pending) + " spawn(s) queued");
        nativeHud.AddSystemMessage("Waiting for game to create an NPC for remote player...");
    } else if (pending == 0) {
        m_hasPendingTimer = false;
        m_directSpawnAttempts = 0;
        m_shownWaitingMsg = false;
        m_shownTimeoutMsg = false;
    }

    // Status updates while waiting
    if (pending > 0 && m_hasPendingTimer) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_firstPendingTime);

        if (pendingDuration.count() >= 10 && !m_shownWaitingMsg) {
            m_shownWaitingMsg = true;
            nativeHud.AddSystemMessage("Still waiting for NPC creation... Walk near a town or camp.");
        }
        if (pendingDuration.count() >= 30 && !m_shownTimeoutMsg) {
            m_shownTimeoutMsg = true;
            spdlog::warn("SyncOrch: Spawn queue waiting 30s+ — hook may not be firing");
            nativeHud.AddSystemMessage("Spawn timeout! Try walking near NPCs or entering a town.");
        }
    }

    // Periodic log
    auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_lastSpawnLog);
    if (sinceLog.count() >= 5 && pending > 0) {
        m_lastSpawnLog = std::chrono::steady_clock::now();
        spdlog::info("SyncOrch: {} pending spawns (inPlace={}, charTemplates={}, ready={}, preCall={})",
                     pending, entity_hooks::GetInPlaceSpawnCount(),
                     m_spawnManager.GetCharacterTemplateCount(),
                     m_spawnManager.IsReady(), m_spawnManager.HasPreCallData());
    }

    // SpawnCharacterDirect DISABLED — it copies a stale pre-call struct from
    // loading time whose faction pointer (char+0x10) becomes a use-after-free
    // when the source NPC's zone unloads.  The game crashes at game+0x927E94
    // reading faction+0x250 on every character update tick.
    //
    // Remote characters are spawned ONLY via in-place replay in entity_hooks:
    // when the host walks near NPCs, Hook_CharacterCreate fires and piggybacks
    // on the natural NPC creation to replay the spawn with valid game state.
    // The "walk near a town" messages guide the player.
    if (pending > 0 && m_hasPendingTimer) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_firstPendingTime);
        if (pendingDuration.count() >= 10 && pendingDuration.count() % 10 == 0) {
            static int64_t s_lastLogSec = 0;
            if (pendingDuration.count() != s_lastLogSec) {
                s_lastLogSec = pendingDuration.count();
                spdlog::info("SyncOrch: {} spawns pending for {}s — waiting for in-place replay "
                             "(walk near NPCs to trigger)", pending, pendingDuration.count());
            }
        }
    }
}

void SyncOrchestrator::StageKickBackgroundWork() {
    m_frameData[m_writeBuffer].Clear();

    m_taskOrchestrator.PostFrameWork([this] { BackgroundReadEntities(); });
    m_taskOrchestrator.PostFrameWork([this] { BackgroundInterpolate(); });

    m_pipelineStarted = true;
}

void SyncOrchestrator::StageUpdatePlayers(float deltaTime) {
    // AFK check every 10 seconds
    if (m_tickCount % 200 == 0) {
        auto afkPlayers = m_playerEngine.CheckAFK();
        for (PlayerID id : afkPlayers) {
            auto* session = m_playerEngine.GetSession(id);
            if (session) {
                spdlog::info("SyncOrch: Player {} '{}' is now AFK", id, session->name);
            }
        }
    }

    // Diagnostics every 5 seconds
    m_diagTickCount++;
    auto diagNow = std::chrono::steady_clock::now();
    auto diagElapsed = std::chrono::duration_cast<std::chrono::seconds>(diagNow - m_lastDiagLog);
    if (diagElapsed.count() >= 5) {
        spdlog::info("SyncOrch::Tick: {} ticks in {}s, entities={}, remote={}, zone=({},{})",
                     m_diagTickCount, diagElapsed.count(),
                     m_registry.GetEntityCount(), m_registry.GetRemoteCount(),
                     m_zoneEngine.GetLocalZone().x, m_zoneEngine.GetLocalZone().y);
        m_diagTickCount = 0;
        m_lastDiagLog = diagNow;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Background Workers
// ════════════════════════════════════════════════════════════════════════════

void SyncOrchestrator::BackgroundReadEntities() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto localEntities = m_registry.GetPlayerEntities(m_localPlayerId);

    struct PendingPos {
        CharacterPosition cp;
        EntityID netId;
        Vec3 pos;
        Quat rot;
    };
    std::vector<PendingPos> pendingPositions;

    for (EntityID netId : localEntities) {
        void* gameObj = m_registry.GetGameObject(netId);
        if (!gameObj) continue;

        auto* info = m_registry.GetInfo(netId);
        if (!info || info->isRemote) continue;
        Vec3 lastPos = info->lastPosition;

        BGReadResult rd = SEH_ReadCharacterBG(gameObj);
        if (!rd.valid) continue;

        Vec3 pos = rd.pos;
        Quat rot = rd.rot;

        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

        float dist = pos.DistanceTo(lastPos);
        if (dist < KMP_POS_CHANGE_THRESHOLD) continue;

        float computedSpeed = dist / 0.016f;
        float speed = rd.speed;
        if (speed <= 0.f && computedSpeed > 0.f) {
            speed = computedSpeed;
        }

        uint8_t animState = rd.animState;
        if (animState == 0 && speed > 0.5f) {
            animState = (speed > 5.0f) ? 2 : 1;
        }

        CachedEntityPos cached;
        cached.netId = netId;
        cached.position = pos;
        cached.rotation = rot;
        cached.speed = speed;
        cached.animState = animState;
        cached.dirty = true;
        writeFrame.localEntities.push_back(cached);

        PendingPos pp;
        pp.cp.entityId = netId;
        pp.cp.posX = pos.x;
        pp.cp.posY = pos.y;
        pp.cp.posZ = pos.z;
        pp.cp.compressedQuat = rot.Compress();
        pp.cp.animStateId = animState;
        pp.cp.moveSpeed = static_cast<uint8_t>(std::min(255.f, speed / 15.f * 255.f));
        pp.cp.flags = (speed > 3.0f) ? 0x01 : 0x00;
        pp.netId = netId;
        pp.pos = pos;
        pp.rot = rot;
        pendingPositions.push_back(pp);
    }

    if (!pendingPositions.empty()) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(static_cast<uint8_t>(pendingPositions.size()));
        for (auto& pp : pendingPositions) {
            writer.WriteRaw(&pp.cp, sizeof(pp.cp));
            m_registry.UpdatePosition(pp.netId, pp.pos);
            m_registry.UpdateRotation(pp.netId, pp.rot);
        }
        writeFrame.packetBytes = std::move(writer.Buffer());
    }

    writeFrame.ready = true;
}

void SyncOrchestrator::BackgroundInterpolate() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto remoteEntities = m_registry.GetRemoteEntities();
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

    for (EntityID remoteId : remoteEntities) {
        CachedRemoteResult result;
        result.netId = remoteId;

        uint8_t moveSpeed = 0;
        uint8_t animState = 0;
        if (m_interpolation.GetInterpolated(remoteId, now,
                                             result.position, result.rotation,
                                             moveSpeed, animState)) {
            result.moveSpeed = moveSpeed;
            result.animState = animState;
            result.valid = true;
        }

        writeFrame.remoteResults.push_back(result);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Priority Management
// ════════════════════════════════════════════════════════════════════════════

SyncPriority SyncOrchestrator::ComputePriority(EntityID entityId) const {
    auto* info = m_registry.GetInfo(entityId);
    if (!info) return SyncPriority::None;

    ZoneCoord localZone = m_zoneEngine.GetLocalZone();

    // Same zone as local player
    if (info->zone == localZone) return SyncPriority::Critical;

    // Adjacent zone (3x3 grid)
    if (localZone.IsAdjacent(info->zone)) return SyncPriority::Normal;

    // Everything else
    return SyncPriority::None;
}

bool SyncOrchestrator::ShouldSyncThisTick(EntityID entityId) const {
    SyncPriority prio = ComputePriority(entityId);
    switch (prio) {
    case SyncPriority::Critical: return true;                     // Every tick (20Hz)
    case SyncPriority::Normal:   return true;                     // Every tick (20Hz)
    case SyncPriority::Low:      return (m_tickCount % 2) == 0;  // Every other tick (10Hz)
    case SyncPriority::None:     return false;
    }
    return false;
}

} // namespace kmp

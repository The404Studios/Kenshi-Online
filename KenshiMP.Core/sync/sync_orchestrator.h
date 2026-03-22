#pragma once
#include "entity_resolver.h"
#include "zone_engine.h"
#include "player_engine.h"
#include "interpolation.h"
#include "../sys/task_orchestrator.h"
#include "../sys/frame_data.h"
#include "../game/spawn_manager.h"
#include "../net/client.h"
#include "kmp/types.h"
#include "kmp/constants.h"
#include <chrono>
#include <atomic>
#include <memory>

namespace kmp {

// Forward declarations
class EntityRegistry;
class PlayerController;

// Priority bucket for entity sync
enum class SyncPriority : uint8_t {
    Critical = 0,  // Same zone as any player (highest frequency)
    Normal   = 1,  // Adjacent zone (standard 20Hz)
    Low      = 2,  // Two zones away (10Hz)
    None     = 3,  // Out of interest range (no sync)
};

class SyncOrchestrator {
public:
    SyncOrchestrator(EntityRegistry& registry,
                     PlayerController& playerCtrl,
                     Interpolation& interp,
                     SpawnManager& spawnMgr,
                     NetworkClient& client,
                     TaskOrchestrator& taskOrch);

    // ---- Initialization ----
    void Initialize(PlayerID localId, const std::string& playerName);
    void Shutdown();

    // ---- Per-Frame Tick (called from Core::OnGameTick) ----
    // Returns true if sync work was performed.
    bool Tick(float deltaTime);

    // ---- Accessors ----
    EntityResolver& GetResolver()       { return m_resolver; }
    ZoneEngine&     GetZoneEngine()     { return m_zoneEngine; }
    PlayerEngine&   GetPlayerEngine()   { return m_playerEngine; }

    const EntityResolver& GetResolver() const       { return m_resolver; }
    const ZoneEngine&     GetZoneEngine() const     { return m_zoneEngine; }
    const PlayerEngine&   GetPlayerEngine() const   { return m_playerEngine; }

    // ---- Priority Management ----
    SyncPriority ComputePriority(EntityID entityId) const;
    bool ShouldSyncThisTick(EntityID entityId) const;

    // ---- State ----
    bool IsActive() const { return m_active; }
    uint64_t GetTickCount() const { return m_tickCount; }

    // ---- Double Buffer Access (for Core compatibility) ----
    FrameData& GetWriteBuffer() { return m_frameData[m_writeBuffer]; }
    FrameData& GetReadBuffer()  { return m_frameData[m_readBuffer]; }

    // ---- Reset (on disconnect) ----
    void Reset();

private:
    // ---- Pipeline Stages ----
    void StageUpdateZones();
    void StageSwapBuffers();
    void StageApplyRemotePositions();
    void StagePollAndSendPositions();
    void StageProcessSpawns();
    void StageKickBackgroundWork();
    void StageUpdatePlayers(float deltaTime);

    // ---- Background Workers ----
    void BackgroundReadEntities();
    void BackgroundInterpolate();

    // ---- Subsystem references ----
    EntityRegistry&   m_registry;
    PlayerController& m_playerController;
    Interpolation&    m_interpolation;
    SpawnManager&     m_spawnManager;
    NetworkClient&    m_client;
    TaskOrchestrator& m_taskOrchestrator;

    // ---- Owned engines ----
    EntityResolver    m_resolver;
    ZoneEngine        m_zoneEngine;
    PlayerEngine      m_playerEngine;

    // ---- Double-buffered frame data ----
    FrameData         m_frameData[2];
    int               m_writeBuffer = 0;
    int               m_readBuffer  = 1;
    bool              m_pipelineStarted = false;

    // ---- Sync state ----
    uint64_t          m_tickCount = 0;
    bool              m_active = false;
    PlayerID          m_localPlayerId = INVALID_PLAYER;

    // ---- Timing ----
    std::chrono::steady_clock::time_point m_lastZoneRebuild;
    static constexpr int ZONE_REBUILD_INTERVAL_MS = 500;

    // ---- Spawn queue state ----
    std::chrono::steady_clock::time_point m_firstPendingTime;
    bool              m_hasPendingTimer = false;
    int               m_directSpawnAttempts = 0;
    bool              m_shownWaitingMsg = false;
    bool              m_shownTimeoutMsg = false;
    std::chrono::steady_clock::time_point m_lastDirectAttempt;
    bool              m_heapScanned = false;
    int               m_heapScanAttempts = 0;
    std::chrono::steady_clock::time_point m_lastHeapScan;
    std::chrono::steady_clock::time_point m_lastSpawnLog;

    // ---- Poll throttle ----
    std::chrono::steady_clock::time_point m_lastPollTime;

    // ---- Diagnostics ----
    int               m_diagTickCount = 0;
    std::chrono::steady_clock::time_point m_lastDiagLog;
};

} // namespace kmp

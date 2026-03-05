#pragma once
#include "kmp/scanner.h"
#include "kmp/patterns.h"
#include "kmp/hook_manager.h"
#include "kmp/orchestrator.h"
#include "kmp/config.h"
#include "net/client.h"
#include "sync/entity_registry.h"
#include "sync/interpolation.h"
#include "game/spawn_manager.h"
#include "game/player_controller.h"
#include "game/loading_orchestrator.h"
#include "ui/overlay.h"
#include "ui/native_hud.h"
#include "sync/sync_orchestrator.h"
#include "sync/sync_facilitator.h"
#include "sync/pipeline_orchestrator.h"
#include "sys/task_orchestrator.h"
#include "sys/frame_data.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

namespace kmp {

class Core {
public:
    static Core& Get();

    bool Initialize();
    void Shutdown();

    // Accessors
    PatternScanner&      GetScanner()        { return m_scanner; }
    GameFunctions&       GetGameFunctions()   { return m_gameFuncs; }
    PatternOrchestrator& GetPatternOrchestrator() { return m_patternOrchestrator; }
    NetworkClient&    GetClient()          { return m_client; }
    EntityRegistry&   GetEntityRegistry()  { return m_entityRegistry; }
    Interpolation&    GetInterpolation()   { return m_interpolation; }
    SpawnManager&        GetSpawnManager()       { return m_spawnManager; }
    PlayerController&    GetPlayerController()  { return m_playerController; }
    LoadingOrchestrator& GetLoadingOrch()       { return m_loadingOrch; }
    Overlay&          GetOverlay()         { return m_overlay; }
    NativeHud&        GetNativeHud()       { return m_nativeHud; }
    ClientConfig&     GetConfig()          { return m_config; }

    bool IsConnected() const { return m_connected; }
    bool IsHost() const { return m_isHost; }
    bool IsSteamVersion() const { return m_isSteamVersion; }
    bool IsGameLoaded() const { return m_gameLoaded; }
    bool IsLoading() const { return m_isLoading; }
    void SetLoading(bool loading) { m_isLoading = loading; }
    PlayerID GetLocalPlayerId() const { return m_localPlayerId.load(); }

    void SetConnected(bool connected) {
        m_connected = connected;
        if (!connected) {
            // Clean up remote entities and interpolation
            size_t removed = m_entityRegistry.ClearRemoteEntities();
            m_interpolation.Clear();
            m_playerController.Reset();
            if (removed > 0) {
                spdlog::info("Core: Cleared {} remote entities on disconnect", removed);
            }

            // Reset state for potential reconnect
            m_initialEntityScanDone = false;
            m_spawnTeleportDone = false;
            m_hasHostSpawnPoint = false;
            m_isHost = false;
            m_hostTpTimerStarted = false;
            m_pipelineStarted = false;
            m_frameData[0].Clear();
            m_frameData[1].Clear();

            // Reset sync orchestrator state
            if (m_syncOrchestrator) {
                m_syncOrchestrator->Reset();
            }

            // Reset pipeline debugger state
            m_pipelineOrch.Shutdown();

            // Reset overlay auto-connect state so user can reconnect
            m_overlay.ResetForReconnect();
        }
    }
    void SetLocalPlayerId(PlayerID id) { m_localPlayerId.store(id); }
    void SetIsHost(bool host) { m_isHost = host; }

    // Called once when the game world has loaded (factory captured by entity_hooks)
    void OnGameLoaded();

    // Called from game thread hooks
    void OnGameTick(float deltaTime);

    // Called after handshake: scan existing local characters and send them to server
    void SendExistingEntitiesToServer();

    // Called by entity_hooks after faction bootstrap to trigger a re-scan of existing characters
    void RequestEntityRescan() { m_needsEntityRescan.store(true); }

    // Host spawn point: joiner teleports here
    void SetHostSpawnPoint(const Vec3& pos) { m_hostSpawnPoint = pos; m_hasHostSpawnPoint = true; }
    bool HasHostSpawnPoint() const { return m_hasHostSpawnPoint; }
    Vec3 GetHostSpawnPoint() const { return m_hostSpawnPoint; }

    // Teleport local player's squad to the nearest remote player character.
    // Returns true if teleport succeeded. Can be called from chat command "/tp".
    bool TeleportToNearestRemotePlayer();

    // Set by time_hooks when TimeUpdate hook is active.
    // When true, render_hooks skips its fallback OnGameTick call.
    void SetTimeHookActive(bool active) { m_timeHookActive = active; }
    bool IsTimeHookActive() const { return m_timeHookActive; }

    // Task orchestrator access
    TaskOrchestrator& GetOrchestrator() { return m_orchestrator; }

    // Crash breadcrumb: last completed pipeline step (for SEH crash diagnostics)
    int GetLastCompletedStep() const { return m_lastCompletedStep.load(std::memory_order_relaxed); }
    void SetLastCompletedStep(int step) { m_lastCompletedStep.store(step, std::memory_order_relaxed); }

    // Sync orchestrator (new engine pipeline)
    SyncOrchestrator* GetSyncOrchestrator() { return m_syncOrchestrator.get(); }

    // Pipeline debugger (network-replicated pipeline state)
    PipelineOrchestrator& GetPipelineOrch() { return m_pipelineOrch; }

private:
    // Staged pipeline methods (called from OnGameTick)
    void ApplyRemotePositions();
    void PollLocalPositions();
    void SendCachedPackets();
    void HandleSpawnQueue();
    void HandleHostTeleport();
    void KickBackgroundWork();
    void UpdateDiagnostics(float deltaTime);

    // Background worker methods (run on orchestrator threads)
    void BackgroundReadEntities();
    void BackgroundInterpolate();

    Core() = default;
    ~Core() = default;

    bool InitScanner();
    bool InitHooks();
    bool InitNetwork();
    bool InitUI();

    // Network thread
    void NetworkThreadFunc();

    PatternScanner       m_scanner;
    GameFunctions        m_gameFuncs;
    PatternOrchestrator  m_patternOrchestrator;
    NetworkClient   m_client;
    EntityRegistry  m_entityRegistry;
    Interpolation   m_interpolation;
    SpawnManager       m_spawnManager;
    PlayerController   m_playerController;
    LoadingOrchestrator m_loadingOrch;
    Overlay           m_overlay;
    NativeHud         m_nativeHud;
    ClientConfig    m_config;

    std::atomic<int>  m_lastCompletedStep{-1}; // Crash breadcrumb for SEH diagnostics
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_gameLoaded{false};
    std::atomic<bool> m_isLoading{false};  // Set true by CharacterCreate burst, false by OnGameLoaded()
    bool              m_isHost = false;
    bool              m_isSteamVersion = false;
    bool              m_timeHookActive = false;
    std::atomic<PlayerID> m_localPlayerId{0};

    // Host spawn point: where joiners teleport to
    Vec3              m_hostSpawnPoint;
    bool              m_hasHostSpawnPoint = false;
    bool              m_spawnTeleportDone = false;
    bool              m_initialEntityScanDone = false;
    std::atomic<bool> m_needsEntityRescan{false}; // Set by entity_hooks after faction bootstrap

    // Host teleport timer (member instead of static so it resets on reconnect)
    std::chrono::steady_clock::time_point m_hostTpTimer;
    bool              m_hostTpTimerStarted = false;

    std::thread m_networkThread;

    // Background task orchestrator + double-buffered frame data
    TaskOrchestrator  m_orchestrator;
    FrameData         m_frameData[2];
    int               m_writeBuffer = 0; // Workers fill this
    int               m_readBuffer  = 1; // Game thread reads this
    bool              m_pipelineStarted = false; // True after first KickBackgroundWork

    // Sync orchestrator (owns EntityResolver, ZoneEngine, PlayerEngine)
    std::unique_ptr<SyncOrchestrator> m_syncOrchestrator;
    bool              m_useSyncOrchestrator = false; // Feature flag for incremental rollout

    // Pipeline debugger (network-replicated pipeline state)
    PipelineOrchestrator m_pipelineOrch;
};

} // namespace kmp

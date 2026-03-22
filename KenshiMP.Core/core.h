#pragma once
#include "kmp/scanner.h"
#include "kmp/patterns.h"
#include "kmp/hook_manager.h"
#include "kmp/orchestrator.h"
#include "kmp/config.h"
#include "net/client.h"
#include "sync/entity_registry.h"
#include "sync/interpolation.h"
#include "game/game_types.h"
#include "game/spawn_manager.h"
#include "game/player_controller.h"
#include "game/loading_orchestrator.h"
#include "game/lobby_manager.h"
#include "game/shared_save_sync.h"
#include "ui/overlay.h"
#include "ui/native_hud.h"
#include "sync/sync_orchestrator.h"
#include "sync/sync_facilitator.h"
#include "sync/pipeline_orchestrator.h"
#include "sys/task_orchestrator.h"
#include "sys/frame_data.h"
#include "hooks/entity_hooks.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

namespace kmp {

// Client lifecycle phases — deterministic state machine.
// Transitions happen in specific, well-defined places.
enum class ClientPhase : uint8_t {
    Startup,           // DLL loaded, pattern scan + hook install. Present not yet firing.
    MainMenu,          // Splash done, user on main menu. Present fires at high fps.
    Loading,           // User clicked New Game/Continue/Load. Game is blocking on load.
                       // Detected by a >2s gap between Present calls.
    GameReady,         // World loaded, characters exist. OnGameLoaded has fired.
                       // Auto-connect can proceed. CharacterCreate hook still disabled.
    Connecting,        // ConnectAsync called, waiting for handshake.
    Connected,         // Handshake done, CharacterCreate hook enabled, entities syncing.
};

inline const char* ClientPhaseToString(ClientPhase p) {
    switch (p) {
        case ClientPhase::Startup:    return "Startup";
        case ClientPhase::MainMenu:   return "MainMenu";
        case ClientPhase::Loading:    return "Loading";
        case ClientPhase::GameReady:  return "GameReady";
        case ClientPhase::Connecting: return "Connecting";
        case ClientPhase::Connected:  return "Connected";
        default:                      return "Unknown";
    }
}

// Defined in core.cpp — resets keepalive timer on reconnect
void ResetKeepaliveTimer();

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
    LobbyManager&        GetLobbyManager()      { return m_lobbyManager; }
    Overlay&          GetOverlay()         { return m_overlay; }
    NativeHud&        GetNativeHud()       { return m_nativeHud; }
    ClientConfig&     GetConfig()          { return m_config; }

    bool IsConnected() const { return m_connected; }
    bool IsHost() const { return m_isHost; }
    bool IsSteamVersion() const { return m_isSteamVersion; }
    bool IsGameLoaded() const { return m_gameLoaded; }
    bool IsLoading() const { return m_clientPhase == ClientPhase::Loading; }
    void SetLoading(bool) {} // No-op — phase transitions handled by state machine

    // ── Client phase state machine ──
    ClientPhase GetClientPhase() const { return m_clientPhase.load(std::memory_order_acquire); }
    void TransitionTo(ClientPhase newPhase);

    // Called by HookPresent when a long frame gap (>2s) is detected,
    // indicating the game was blocking on a load screen.
    void OnLoadingGapDetected();
    PlayerID GetLocalPlayerId() const { return m_localPlayerId.load(); }

    void SetConnected(bool connected) {
        m_connected = connected;
        if (connected) {
            // Phase transition handled by PacketHandler (TransitionTo(Connected))
            ResetKeepaliveTimer(); // Reset so first keepalive fires 5s after connect, not immediately

            // Enable combat hooks for multiplayer sync (they start bypassed due to MovRaxRsp)
            HookManager::Get().Enable("CharacterDeath");
            HookManager::Get().Enable("CharacterKO");
            spdlog::info("Core: Combat hooks (CharacterDeath, CharacterKO) ENABLED for multiplayer");

            // shared_save_sync::Init() is called lazily from Update() after
            // faction assignment arrives (which comes AFTER handshake ack).
        }
        if (!connected) {
            // Drop back to GameReady so user can reconnect
            if (m_gameLoaded) {
                TransitionTo(ClientPhase::GameReady);
            }

            // Disable CharacterCreate hook — zone-load bursts while disconnected
            // would go through MovRaxRsp and corrupt the heap
            entity_hooks::SuspendForDisconnect();

            // Disable combat hooks — no need to sync while disconnected
            HookManager::Get().Disable("CharacterDeath");
            HookManager::Get().Disable("CharacterKO");

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

            // Reset shared-save sync
            shared_save_sync::Reset();

            // Clear stale spawn queue from previous session
            m_spawnManager.ClearSpawnQueue();

            // Signal HandleSpawnQueue to reset its static timers on next call
            m_needSpawnQueueReset = true;

            // Reset game_character probe statics so animClassOffset can be
            // re-discovered on next connection (s_totalAttempts, s_animClassProbed, etc.)
            game::ResetProbeState();

            // Close chat input — prevents keyboard being consumed by invisible chat
            m_nativeHud.CloseChatInput();

            // Reset overlay auto-connect state so user can reconnect
            m_overlay.ResetForReconnect();
        }
    }
    void SetLocalPlayerId(PlayerID id) { m_localPlayerId.store(id); }
    void SetIsHost(bool host) { m_isHost = host; }

    // Called once when the game world has loaded
    void OnGameLoaded();

    // Called from render thread to detect game load completion.
    // Retries global discovery, checks CharacterIterator, triggers OnGameLoaded if ready.
    void PollForGameLoad();

    // Called from game thread hooks
    void OnGameTick(float deltaTime);

    // Called after handshake: scan existing local characters and send them to server
    void SendExistingEntitiesToServer();

    // Called by entity_hooks after faction bootstrap to trigger a re-scan of existing characters
    void RequestEntityRescan() { m_needsEntityRescan.store(true); }

    // Host spawn point: joiner teleports here
    // Set from network thread, read from game thread — use mutex to prevent torn reads
    void SetHostSpawnPoint(const Vec3& pos) {
        std::lock_guard lock(m_hostSpawnPointMutex);
        m_hostSpawnPoint = pos;
        m_hasHostSpawnPoint.store(true, std::memory_order_release);
    }
    bool HasHostSpawnPoint() const { return m_hasHostSpawnPoint.load(std::memory_order_acquire); }
    Vec3 GetHostSpawnPoint() const {
        std::lock_guard lock(m_hostSpawnPointMutex);
        return m_hostSpawnPoint;
    }

    // Teleport local player's squad to the nearest remote player character.
    // Returns true if teleport succeeded. Can be called from chat command "/tp".
    bool TeleportToNearestRemotePlayer();

    // Force-spawn: bypass all gates and immediately spawn pending remote players.
    // Used by /forcespawn chat command as a playtest escape hatch.
    void ForceSpawnRemotePlayers();

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
    void ApplyRemotePositionsDirect(); // Direct interpolation → game character (no double-buffer)
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
    LobbyManager       m_lobbyManager;
    Overlay           m_overlay;
    NativeHud         m_nativeHud;
    ClientConfig    m_config;

    std::atomic<int>  m_lastCompletedStep{-1}; // Crash breadcrumb for SEH diagnostics
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_gameLoaded{false};
    std::atomic<ClientPhase> m_clientPhase{ClientPhase::Startup};
    bool              m_isHost = false;
    bool              m_isSteamVersion = false;
    std::atomic<bool> m_timeHookActive{false};
    std::atomic<PlayerID> m_localPlayerId{0};

    // Host spawn point: where joiners teleport to (network thread writes, game thread reads)
    mutable std::mutex m_hostSpawnPointMutex;
    Vec3              m_hostSpawnPoint;
    std::atomic<bool> m_hasHostSpawnPoint{false};
    bool              m_spawnTeleportDone = false;
    bool              m_initialEntityScanDone = false;
    std::atomic<bool> m_needsEntityRescan{false}; // Set by entity_hooks after faction bootstrap
    bool              m_needSpawnQueueReset = false; // HandleSpawnQueue statics reset on reconnect
    bool              m_needPollReset = false;       // PollForGameLoad statics reset on second load
    std::atomic<bool> m_forceSpawnBypass{false};     // /forcespawn command bypass

    // Host teleport timer (member instead of static so it resets on reconnect)
    std::chrono::steady_clock::time_point m_hostTpTimer;
    bool              m_hostTpTimerStarted = false;

    std::thread m_networkThread;

    // Background task orchestrator + double-buffered frame data
    TaskOrchestrator  m_orchestrator;
    FrameData         m_frameData[2];
    std::atomic<int>  m_writeBuffer{0}; // Workers fill this
    std::atomic<int>  m_readBuffer{1};  // Game thread reads this
    bool              m_pipelineStarted = false; // True after first KickBackgroundWork

    // Sync orchestrator (owns EntityResolver, ZoneEngine, PlayerEngine)
    std::unique_ptr<SyncOrchestrator> m_syncOrchestrator;
    bool              m_useSyncOrchestrator = false; // Feature flag for incremental rollout

    // Pipeline debugger (network-replicated pipeline state)
    PipelineOrchestrator m_pipelineOrch;
};

} // namespace kmp

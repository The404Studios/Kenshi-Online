#pragma once
#include "pipeline_state.h"
#include "kmp/types.h"
#include "kmp/protocol.h"
#include <mutex>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>

namespace kmp {

// Forward declarations
class EntityRegistry;
class SpawnManager;
class LoadingOrchestrator;
class NetworkClient;
class NativeHud;

// PipelineOrchestrator — network-replicated pipeline debugger.
// Collects local pipeline state, exchanges snapshots with peers,
// detects anomalies, and renders a debug HUD.
class PipelineOrchestrator {
public:
    PipelineOrchestrator() = default;

    // ── Lifecycle ──
    void Initialize(PlayerID localId, EntityRegistry& registry, SpawnManager& spawnMgr,
                    LoadingOrchestrator& loadingOrch, NetworkClient& client, NativeHud& hud);
    void Shutdown();

    // ── Per-frame tick (called from Core::OnGameTick, Step 11) ──
    void Tick(float deltaTime);
    void TickInner(); // Called from SEH wrapper in Tick()

    // ── Event Recording (thread-safe, called from any thread) ──
    void RecordEvent(PipelineEventType type, uint8_t severity,
                     EntityID entityId, uint32_t auxData,
                     const std::string& detail);

    // ── Network Handlers (called from PacketHandler on network thread) ──
    void OnRemoteSnapshot(PlayerID sender, const uint8_t* data, size_t size);
    void OnRemoteEvent(PlayerID sender, const uint8_t* data, size_t size);

    // ── HUD Toggle ──
    void ToggleHud();
    bool IsHudVisible() const { return m_hudVisible.load(std::memory_order_relaxed); }

    // ── Queries ──
    PipelineSnapshot GetLocalSnapshot() const;
    std::unordered_map<PlayerID, PipelineSnapshot> GetRemoteSnapshots() const;
    std::vector<PipelineEvent> GetRecentEvents(int maxCount = 50) const;
    std::vector<PipelineAnomaly> GetActiveAnomalies() const;
    std::vector<PipelineEvent> GetEntityHistory(EntityID entityId, int maxCount = 20) const;

    // ── Text dumps (for /pipeline command) ──
    std::string FormatStatusDump() const;
    std::string FormatEntityTrack(EntityID entityId) const;

    // ── Configuration ──
    static constexpr int SNAPSHOT_INTERVAL_MS       = 1000;
    static constexpr int EVENT_BROADCAST_BATCH      = 8;
    static constexpr int MAX_LOCAL_EVENTS           = 200;
    static constexpr int MAX_ANOMALIES              = 32;
    static constexpr int ANOMALY_CHECK_INTERVAL_MS  = 2000;
    static constexpr int CANSPAWN_STUCK_THRESHOLD_MS= 30000;
    static constexpr int HOOK_SILENT_THRESHOLD_MS   = 30000;
    static constexpr int PHASE_STUCK_THRESHOLD_MS   = 60000;
    static constexpr int SNAPSHOT_STALE_THRESHOLD_MS= 5000;

private:
    PipelineSnapshot CollectSnapshot() const;
    void BroadcastSnapshot(const PipelineSnapshot& snap);
    void BroadcastPendingEvents();
    void RunAnomalyDetection();
    void RaiseAnomaly(AnomalyType type, PlayerID source, const std::string& desc);
    void ResolveAnomaly(AnomalyType type, PlayerID source);
    void UpdateHud();

    uint32_t GetTimestampMs() const;

    // Subsystem references
    EntityRegistry*      m_registry    = nullptr;
    SpawnManager*        m_spawnMgr    = nullptr;
    LoadingOrchestrator* m_loadingOrch = nullptr;
    NetworkClient*       m_client      = nullptr;
    NativeHud*           m_hud         = nullptr;
    PlayerID             m_localPlayerId = 0;

    // Timing
    std::chrono::steady_clock::time_point m_connectionStart;
    std::chrono::steady_clock::time_point m_lastSnapshotTime;
    std::chrono::steady_clock::time_point m_lastAnomalyCheck;
    std::chrono::steady_clock::time_point m_gameLoadedTime;
    bool m_gameLoadedTimeSet = false;

    // Local snapshot cache
    mutable std::mutex m_snapshotMutex;
    PipelineSnapshot   m_lastLocalSnapshot{};

    // Remote snapshots
    mutable std::mutex m_remoteMutex;
    std::unordered_map<PlayerID, PipelineSnapshot> m_remoteSnapshots;
    std::unordered_map<PlayerID, std::chrono::steady_clock::time_point> m_remoteSnapshotTimes;

    // Event ring buffers
    mutable std::mutex m_eventMutex;
    std::deque<PipelineEvent> m_events;             // All events (local + remote)
    std::deque<PipelineEvent> m_pendingBroadcast;   // Queued for network send

    // Anomaly state
    mutable std::mutex m_anomalyMutex;
    std::vector<PipelineAnomaly> m_anomalies;

    // HUD state
    std::atomic<bool> m_hudVisible{false};
    bool m_initialized = false;
};

} // namespace kmp

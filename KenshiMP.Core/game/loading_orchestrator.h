#pragma once
#include "kmp/types.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace kmp {

// Phases of the game's loading lifecycle.
// The orchestrator tracks these to gate spawns appropriately.
enum class LoadingPhase : uint8_t {
    Idle,            // No loading in progress — safe to spawn
    InitialLoad,     // Game first loading (save file, initial NPC burst)
    ZoneTransition,  // Player moved to new zone (navmesh + NPC loading)
    SpawnLoad,       // Resources loading for a specific character spawn
};

// Tracking state for a single resource being loaded
struct ResourceState {
    std::string name;
    std::string type;   // "mesh", "texture", "material"
    bool loaded  = false;
    bool failed  = false;
    std::chrono::steady_clock::time_point requestTime;
    std::chrono::steady_clock::time_point completedTime;
};

// LoadingOrchestrator — tracks the game's loading state machine and resource
// loading events to gate spawn timing. Integrates with entity_hooks burst
// detection and (optionally) Ogre resource hooks for fine-grained control.
//
// Thread safety: all public methods are thread-safe (mutex + atomics).
class LoadingOrchestrator {
public:
    // ── Phase queries ──
    LoadingPhase GetPhase() const { return m_phase.load(std::memory_order_acquire); }
    bool IsLoading() const { return GetPhase() != LoadingPhase::Idle; }
    bool IsIdle() const { return GetPhase() == LoadingPhase::Idle; }

    // ── Spawn gating ──
    // Returns true only when: phase is Idle AND no pending resources AND
    // cooldown has elapsed since the last resource load completed.
    bool IsSafeToSpawn() const;

    // Request preloading of resources for a template (anticipatory, before spawn)
    void RequestPreload(const std::string& templateName);

    // ── Resource tracking (called from Ogre resource hooks) ──
    void OnResourceRequested(const std::string& name, const std::string& type);
    void OnResourceLoaded(const std::string& name, const std::string& type);
    void OnResourceFailed(const std::string& name, const std::string& type);

    // ── Burst detection integration (called from entity_hooks) ──
    void OnBurstDetected(int createCount);
    void OnBurstEnded(int totalCreates);

    // ── Lifecycle events ──
    void OnGameLoaded();
    void OnZoneLoadStart();
    void OnZoneLoadEnd();

    // ── Per-frame tick (called from SyncOrchestrator) ──
    void Tick();

    // ── Diagnostics ──
    size_t GetPendingResourceCount() const;
    size_t GetLoadedResourceCount() const;
    int    GetBurstCount() const { return m_burstCount.load(std::memory_order_relaxed); }
    bool   IsGameLoaded() const { return m_gameLoaded.load(std::memory_order_relaxed); }
    bool   IsInBurst() const { return m_inBurst.load(std::memory_order_relaxed); }

    // Returns human-readable reason why IsSafeToSpawn() returns false, or "OK" if safe.
    std::string GetSpawnBlockReason() const;

    // ── Spawn gate callback ──
    using SpawnGateCallback = std::function<void(bool safe)>;
    void SetSpawnGateCallback(SpawnGateCallback cb);

    // ── Configuration ──
    static constexpr int SPAWN_COOLDOWN_MS = 2000;  // Wait after last resource load
    static constexpr int ZONE_SETTLE_MS    = 1500;  // Wait after zone load completes
    static constexpr int BURST_TIMEOUT_MS  = 30000; // Auto-clear stuck burst after 30s

private:
    void TransitionTo(LoadingPhase newPhase);

    std::atomic<LoadingPhase> m_phase{LoadingPhase::InitialLoad};

    // Resource tracking
    mutable std::mutex m_resourceMutex;
    std::unordered_map<std::string, ResourceState> m_resources;
    std::unordered_set<std::string> m_pendingResources;
    size_t m_totalLoaded = 0;

    // Burst tracking
    std::atomic<int>  m_burstCount{0};
    std::atomic<bool> m_gameLoaded{false};
    std::atomic<bool> m_inBurst{false};
    std::chrono::steady_clock::time_point m_burstStartTime{}; // For timeout auto-clear

    // Timing
    std::chrono::steady_clock::time_point m_lastLoadCompleteTime;
    std::chrono::steady_clock::time_point m_zoneLoadEndTime;
    bool m_hasResourceHooks = false;  // True if Ogre hooks are installed

    // Callback
    SpawnGateCallback m_spawnGateCallback;
    mutable std::mutex m_callbackMutex;
};

} // namespace kmp

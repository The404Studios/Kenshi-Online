#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Engine Pipeline — Deterministic frame processing for multiplayer
// ═══════════════════════════════════════════════════════════════════════════
// Formalizes the game loop into a sequence of typed, validated stages.
// Each stage has:
//   - Pre/post conditions (asserted)
//   - Timing (profiled)
//   - Dependencies (ordered)
//   - Enable/disable (conditional)
//   - Error handling (conditionalized)
//
// The pipeline replaces the ad-hoc OnGameTick() method chain with a
// structured, debuggable, and extensible frame processor.
//
// Pipeline stages (in execution order):
//   0. FrameBegin      — Snapshot frame data, detect loading gaps
//   1. NetworkReceive   — Pump ENet, dispatch incoming packets
//   2. ZoneUpdate       — Recompute zone grid and interest sets
//   3. BufferSwap       — Double-buffer swap for frame data
//   4. SpawnProcess     — Process pending spawn queue
//   5. RemoteApply      — Apply interpolated positions to remote entities
//   6. LocalPoll        — Read local entity positions, build update packets
//   7. CombatResolve    — Process combat events (server-authoritative)
//   8. InventorySync    — Sync inventory changes
//   9. WorldState       — Time progression, weather updates
//  10. NetworkSend      — Flush outgoing packets
//  11. UIUpdate         — Update HUD, chat, overlays
//  12. Diagnostics      — Log performance stats, health checks
//  13. FrameEnd         — Cleanup, increment tick counter

#include "kmp/assert.h"
#include "kmp/diagnostics.h"
#include "kmp/types.h"
#include <vector>
#include <array>
#include <functional>
#include <string>
#include <spdlog/spdlog.h>

namespace kmp::engine {

// ── Stage Index Constants ──
namespace Stage {
    constexpr int FrameBegin     = 0;
    constexpr int NetworkReceive = 1;
    constexpr int ZoneUpdate     = 2;
    constexpr int BufferSwap     = 3;
    constexpr int SpawnProcess   = 4;
    constexpr int RemoteApply    = 5;
    constexpr int LocalPoll      = 6;
    constexpr int CombatResolve  = 7;
    constexpr int InventorySync  = 8;
    constexpr int WorldState     = 9;
    constexpr int NetworkSend    = 10;
    constexpr int UIUpdate       = 11;
    constexpr int Diagnostics    = 12;
    constexpr int FrameEnd       = 13;
    constexpr int COUNT          = 14;
}

// ── Pipeline Stage Definition ──
struct PipelineStage {
    // Identity
    int         index = -1;
    const char* name  = nullptr;

    // Execution
    std::function<void(float)> execute;           // Main work (deltaTime)
    std::function<bool()>      precondition;      // Returns true if stage should run
    std::function<void()>      postcondition;     // Validates output (debug only)

    // Configuration
    bool enabled      = true;    // Can be disabled at runtime
    bool requiresConnection = false;  // Only runs when connected to server
    bool requiresGameLoaded = false;  // Only runs when game world is loaded
    float warnThresholdMs = 10.0f;    // ScopeTimer warning threshold

    // Diagnostics (populated by pipeline)
    float lastExecutionMs = 0.f;
    int   executionCount  = 0;
    int   skipCount       = 0;     // Times skipped due to precondition
    int   errorCount      = 0;     // Times execute threw
};

// ── Engine Pipeline ──
class EnginePipeline {
public:
    EnginePipeline() {
        // Initialize all stages
        for (int i = 0; i < Stage::COUNT; i++) {
            m_stages[i].index = i;
        }

        // Set up profiler
        m_profiler.DefineStage(Stage::FrameBegin,     "FrameBegin");
        m_profiler.DefineStage(Stage::NetworkReceive,  "NetworkReceive");
        m_profiler.DefineStage(Stage::ZoneUpdate,      "ZoneUpdate");
        m_profiler.DefineStage(Stage::BufferSwap,      "BufferSwap");
        m_profiler.DefineStage(Stage::SpawnProcess,    "SpawnProcess");
        m_profiler.DefineStage(Stage::RemoteApply,     "RemoteApply");
        m_profiler.DefineStage(Stage::LocalPoll,       "LocalPoll");
        m_profiler.DefineStage(Stage::CombatResolve,   "CombatResolve");
        m_profiler.DefineStage(Stage::InventorySync,   "InventorySync");
        m_profiler.DefineStage(Stage::WorldState,      "WorldState");
        m_profiler.DefineStage(Stage::NetworkSend,     "NetworkSend");
        m_profiler.DefineStage(Stage::UIUpdate,        "UIUpdate");
        m_profiler.DefineStage(Stage::Diagnostics,     "Diagnostics");
        m_profiler.DefineStage(Stage::FrameEnd,        "FrameEnd");
    }

    // ── Stage Registration ──

    // Register a stage's execution function
    void RegisterStage(int index, const char* name, std::function<void(float)> executeFn) {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        auto& stage = m_stages[index];
        stage.name = name;
        stage.execute = std::move(executeFn);
    }

    // Set precondition for a stage (returns true to run, false to skip)
    void SetPrecondition(int index, std::function<bool()> fn) {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        m_stages[index].precondition = std::move(fn);
    }

    // Set postcondition for a stage (called after execution in debug builds)
    void SetPostcondition(int index, std::function<void()> fn) {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        m_stages[index].postcondition = std::move(fn);
    }

    // Configure stage properties
    void ConfigureStage(int index, bool requiresConnection, bool requiresGameLoaded,
                        float warnThresholdMs = 10.0f) {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        auto& stage = m_stages[index];
        stage.requiresConnection = requiresConnection;
        stage.requiresGameLoaded = requiresGameLoaded;
        stage.warnThresholdMs = warnThresholdMs;
    }

    // Enable/disable a stage at runtime
    void SetStageEnabled(int index, bool enabled) {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        m_stages[index].enabled = enabled;
    }

    // ── Pipeline Execution ──

    // Execute all stages in order. Called once per game tick (~20Hz).
    void Tick(float deltaTime, bool isConnected, bool isGameLoaded) {
        KMP_ASSERT_MSG(deltaTime >= 0.f, "Negative deltaTime");
        KMP_ASSERT_FINITE(deltaTime);

        m_health.OnTick();
        m_tickCount++;

        for (int i = 0; i < Stage::COUNT; i++) {
            ExecuteStage(i, deltaTime, isConnected, isGameLoaded);
        }

        // Periodic diagnostics logging
        if (m_tickCount % 2000 == 0) {
            m_profiler.LogSummary();
        }
    }

    // ── Accessors ──

    const PipelineStage& GetStage(int index) const {
        KMP_PRECONDITION(index >= 0 && index < Stage::COUNT);
        return m_stages[index];
    }

    diag::PipelineProfiler& GetProfiler() { return m_profiler; }
    const diag::PipelineProfiler& GetProfiler() const { return m_profiler; }

    diag::HealthMonitor& GetHealth() { return m_health; }
    const diag::HealthMonitor& GetHealth() const { return m_health; }

    uint64_t GetTickCount() const { return m_tickCount; }

    // Get the last stage that was executing (for crash breadcrumbs)
    int GetCurrentStage() const { return m_currentStage; }

    // Reset all state (on disconnect/reconnect)
    void Reset() {
        m_tickCount = 0;
        m_currentStage = -1;
        m_profiler.Reset();
        for (auto& stage : m_stages) {
            stage.executionCount = 0;
            stage.skipCount = 0;
            stage.errorCount = 0;
            stage.lastExecutionMs = 0.f;
        }
    }

private:
    void ExecuteStage(int index, float deltaTime, bool isConnected, bool isGameLoaded) {
        auto& stage = m_stages[index];

        // Skip if not registered
        if (!stage.execute) return;

        // Skip if disabled
        if (!stage.enabled) {
            stage.skipCount++;
            return;
        }

        // Skip if connection/game-load requirements not met
        if (stage.requiresConnection && !isConnected) {
            stage.skipCount++;
            return;
        }
        if (stage.requiresGameLoaded && !isGameLoaded) {
            stage.skipCount++;
            return;
        }

        // Check precondition
        if (stage.precondition && !stage.precondition()) {
            stage.skipCount++;
            return;
        }

        // Execute with timing and crash breadcrumb
        m_currentStage = index;
        m_profiler.BeginStage(index);

        __try {
            stage.execute(deltaTime);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            stage.errorCount++;
            spdlog::error("Pipeline: SEH exception in stage {} ({})",
                          index, stage.name ? stage.name : "?");
        }

        m_profiler.EndStage(index);
        stage.lastExecutionMs = m_profiler.GetStage(index).lastMs;
        stage.executionCount++;

        // Warn on slow stages
        if (stage.lastExecutionMs > stage.warnThresholdMs) {
            spdlog::warn("Pipeline: Stage {} ({}) took {:.2f}ms (threshold: {:.1f}ms)",
                         index, stage.name, stage.lastExecutionMs, stage.warnThresholdMs);
        }

        // Debug postcondition check
#if KMP_DEBUG
        if (stage.postcondition) {
            stage.postcondition();
        }
#endif

        m_currentStage = -1;
    }

    std::array<PipelineStage, Stage::COUNT> m_stages{};
    diag::PipelineProfiler  m_profiler;
    diag::HealthMonitor     m_health;
    uint64_t                m_tickCount = 0;
    int                     m_currentStage = -1;
};

} // namespace kmp::engine

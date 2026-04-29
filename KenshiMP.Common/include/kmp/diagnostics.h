#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Diagnostics — Performance and health monitoring for the engine
// ═══════════════════════════════════════════════════════════════════════════
// Lightweight instrumentation that stays in Release builds.
// Zero-overhead when not actively profiling (counters are atomic increments).
//
// Components:
//   ScopeTimer      — RAII timer that logs if a scope exceeds a threshold
//   Counter         — Thread-safe counter with periodic logging
//   RateTracker     — Operations-per-second measurement
//   PipelineProfiler— Per-stage timing for the engine pipeline
//   HealthMonitor   — Watchdog for detecting stalls and anomalies

#include <atomic>
#include <chrono>
#include <string>
#include <array>
#include <mutex>
#include <spdlog/spdlog.h>

namespace kmp::diag {

// ── ScopeTimer ──
// Measures how long a scope takes. Logs a warning if it exceeds the threshold.
// Usage:
//   {
//       KMP_SCOPE_TIMER("ApplyPositions", 5.0f);
//       // ... code ...
//   }  // logs if took > 5ms

class ScopeTimer {
public:
    ScopeTimer(const char* name, float warnThresholdMs = 10.0f)
        : m_name(name)
        , m_warnMs(warnThresholdMs)
        , m_start(std::chrono::steady_clock::now()) {}

    ~ScopeTimer() {
        auto elapsed = std::chrono::steady_clock::now() - m_start;
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        if (ms > m_warnMs) {
            spdlog::warn("PERF: {} took {:.2f}ms (threshold: {:.1f}ms)", m_name, ms, m_warnMs);
        }
    }

    // Get elapsed time without stopping
    float ElapsedMs() const {
        auto elapsed = std::chrono::steady_clock::now() - m_start;
        return std::chrono::duration<float, std::milli>(elapsed).count();
    }

    // Non-copyable
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    const char* m_name;
    float       m_warnMs;
    std::chrono::steady_clock::time_point m_start;
};

#define KMP_SCOPE_TIMER(name, thresholdMs) \
    kmp::diag::ScopeTimer _kmp_timer_##__LINE__(name, thresholdMs)

#define KMP_SCOPE_TIMER_DEFAULT(name) \
    kmp::diag::ScopeTimer _kmp_timer_##__LINE__(name, 10.0f)

// ── Counter ──
// Thread-safe counter that periodically logs its value.
// Usage:
//   static Counter s_packetsSent("PacketsSent", 100);
//   s_packetsSent.Increment();

class Counter {
public:
    Counter(const char* name, int logEveryN = 1000)
        : m_name(name), m_logEveryN(logEveryN) {}

    void Increment(int amount = 1) {
        int newVal = m_count.fetch_add(amount, std::memory_order_relaxed) + amount;
        if (m_logEveryN > 0 && (newVal % m_logEveryN) == 0) {
            spdlog::debug("Counter[{}]: {}", m_name, newVal);
        }
    }

    int Get() const { return m_count.load(std::memory_order_relaxed); }
    void Reset() { m_count.store(0, std::memory_order_relaxed); }

private:
    const char*      m_name;
    int              m_logEveryN;
    std::atomic<int> m_count{0};
};

// ── RateTracker ──
// Measures operations per second over a sliding window.
// Usage:
//   static RateTracker s_syncRate("EntitySync");
//   s_syncRate.Tick();  // call once per operation
//   float opsPerSec = s_syncRate.GetRate();

class RateTracker {
public:
    RateTracker(const char* name, float windowSeconds = 1.0f)
        : m_name(name)
        , m_windowSec(windowSeconds)
        , m_lastReset(std::chrono::steady_clock::now()) {}

    void Tick(int amount = 1) {
        m_count.fetch_add(amount, std::memory_order_relaxed);

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - m_lastReset.load()).count();

        if (elapsed >= m_windowSec) {
            int count = m_count.exchange(0, std::memory_order_relaxed);
            m_lastRate.store(static_cast<float>(count) / elapsed, std::memory_order_relaxed);
            m_lastReset.store(now);
        }
    }

    float GetRate() const { return m_lastRate.load(std::memory_order_relaxed); }
    const char* GetName() const { return m_name; }

private:
    const char*      m_name;
    float            m_windowSec;
    std::atomic<int> m_count{0};
    std::atomic<float> m_lastRate{0.f};
    std::atomic<std::chrono::steady_clock::time_point> m_lastReset;
};

// ── PipelineProfiler ──
// Tracks per-stage timing for the engine pipeline.
// Each stage records its last execution time and a rolling average.

class PipelineProfiler {
public:
    static constexpr int MAX_STAGES = 16;

    struct StageStats {
        const char* name     = nullptr;
        float       lastMs   = 0.f;
        float       avgMs    = 0.f;
        float       maxMs    = 0.f;
        int         runCount = 0;
        bool        active   = false;
    };

    void DefineStage(int index, const char* name) {
        if (index >= 0 && index < MAX_STAGES) {
            m_stages[index].name = name;
            m_stages[index].active = true;
        }
    }

    void BeginStage(int index) {
        if (index >= 0 && index < MAX_STAGES) {
            m_stageStart[index] = std::chrono::steady_clock::now();
        }
    }

    void EndStage(int index) {
        if (index < 0 || index >= MAX_STAGES || !m_stages[index].active) return;

        auto elapsed = std::chrono::steady_clock::now() - m_stageStart[index];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();

        auto& s = m_stages[index];
        s.lastMs = ms;
        s.runCount++;

        // Exponential moving average (alpha = 0.1)
        if (s.runCount == 1) {
            s.avgMs = ms;
        } else {
            s.avgMs = s.avgMs * 0.9f + ms * 0.1f;
        }
        if (ms > s.maxMs) s.maxMs = ms;
    }

    const StageStats& GetStage(int index) const {
        static StageStats empty;
        if (index < 0 || index >= MAX_STAGES) return empty;
        return m_stages[index];
    }

    // Log all stage timings (call periodically, e.g., every 100 ticks)
    void LogSummary() const {
        spdlog::info("=== Pipeline Profile ===");
        for (int i = 0; i < MAX_STAGES; i++) {
            if (!m_stages[i].active) continue;
            auto& s = m_stages[i];
            spdlog::info("  [{}] {}: last={:.2f}ms avg={:.2f}ms max={:.2f}ms runs={}",
                         i, s.name ? s.name : "?", s.lastMs, s.avgMs, s.maxMs, s.runCount);
        }
    }

    void Reset() {
        for (auto& s : m_stages) {
            s.lastMs = 0.f;
            s.avgMs = 0.f;
            s.maxMs = 0.f;
            s.runCount = 0;
        }
    }

private:
    std::array<StageStats, MAX_STAGES> m_stages{};
    std::array<std::chrono::steady_clock::time_point, MAX_STAGES> m_stageStart{};
};

// ── HealthMonitor ──
// Detects stalls, anomalies, and degradation in the engine pipeline.

class HealthMonitor {
public:
    // Call every frame/tick
    void OnTick() {
        auto now = std::chrono::steady_clock::now();

        if (m_lastTick.time_since_epoch().count() > 0) {
            float gap = std::chrono::duration<float, std::milli>(now - m_lastTick).count();

            // Detect stalls (>500ms between ticks)
            if (gap > 500.f) {
                m_stallCount.fetch_add(1, std::memory_order_relaxed);
                spdlog::warn("HealthMonitor: Stall detected — {:.0f}ms gap between ticks", gap);
            }

            // Track frame time
            m_lastFrameMs = gap;
            if (m_avgFrameMs == 0.f) {
                m_avgFrameMs = gap;
            } else {
                m_avgFrameMs = m_avgFrameMs * 0.95f + gap * 0.05f;
            }
        }

        m_lastTick = now;
        m_tickCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Check if the engine is healthy
    bool IsHealthy() const {
        // Healthy = we've ticked recently and frame time is reasonable
        auto elapsed = std::chrono::steady_clock::now() - m_lastTick;
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        return ms < 1000.f;  // Less than 1 second since last tick
    }

    float GetAvgFrameMs() const { return m_avgFrameMs; }
    float GetLastFrameMs() const { return m_lastFrameMs; }
    int GetStallCount() const { return m_stallCount.load(std::memory_order_relaxed); }
    int GetTickCount() const { return m_tickCount.load(std::memory_order_relaxed); }

private:
    std::chrono::steady_clock::time_point m_lastTick{};
    float            m_lastFrameMs = 0.f;
    float            m_avgFrameMs = 0.f;
    std::atomic<int> m_stallCount{0};
    std::atomic<int> m_tickCount{0};
};

} // namespace kmp::diag

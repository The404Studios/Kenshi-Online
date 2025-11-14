#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace ReKenshi {
namespace Performance {

/**
 * High-resolution timer for performance profiling
 */
class PerformanceTimer {
public:
    PerformanceTimer() { Start(); }

    void Start() {
        m_start = std::chrono::high_resolution_clock::now();
    }

    void Stop() {
        m_end = std::chrono::high_resolution_clock::now();
    }

    // Get elapsed time in various units
    double GetElapsedMilliseconds() const {
        auto duration = m_end - m_start;
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    double GetElapsedMicroseconds() const {
        auto duration = m_end - m_start;
        return std::chrono::duration<double, std::micro>(duration).count();
    }

    double GetElapsedSeconds() const {
        auto duration = m_end - m_start;
        return std::chrono::duration<double>(duration).count();
    }

private:
    std::chrono::high_resolution_clock::time_point m_start;
    std::chrono::high_resolution_clock::time_point m_end;
};

/**
 * RAII-style profiling scope
 */
class ProfileScope {
public:
    ProfileScope(const char* name);
    ~ProfileScope();

private:
    const char* m_name;
    PerformanceTimer m_timer;
};

/**
 * Performance statistics for a single metric
 */
struct PerformanceStats {
    uint64_t callCount = 0;
    double totalTime = 0.0;      // milliseconds
    double minTime = 1e9;        // milliseconds
    double maxTime = 0.0;        // milliseconds
    double lastTime = 0.0;       // milliseconds

    double GetAverageTime() const {
        return callCount > 0 ? totalTime / callCount : 0.0;
    }

    void Reset() {
        callCount = 0;
        totalTime = 0.0;
        minTime = 1e9;
        maxTime = 0.0;
        lastTime = 0.0;
    }
};

/**
 * Performance profiler for tracking plugin overhead
 */
class PerformanceProfiler {
public:
    static PerformanceProfiler& GetInstance();

    // Start/stop timing
    void BeginProfile(const std::string& name);
    void EndProfile(const std::string& name);

    // Get statistics
    const PerformanceStats* GetStats(const std::string& name) const;
    const std::unordered_map<std::string, PerformanceStats>& GetAllStats() const { return m_stats; }

    // Reset
    void ResetStats(const std::string& name);
    void ResetAllStats();

    // Reporting
    std::string GenerateReport() const;
    void PrintReport() const;

    // Configuration
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    PerformanceProfiler() : m_enabled(true) {}
    ~PerformanceProfiler() = default;
    PerformanceProfiler(const PerformanceProfiler&) = delete;
    PerformanceProfiler& operator=(const PerformanceProfiler&) = delete;

    bool m_enabled;
    std::unordered_map<std::string, PerformanceStats> m_stats;
    std::unordered_map<std::string, PerformanceTimer> m_activeTimers;
};

// Helper macros for easy profiling
#define PROFILE_SCOPE(name) ReKenshi::Performance::ProfileScope _profileScope_##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

/**
 * Frame time tracker
 */
class FrameTimeTracker {
public:
    FrameTimeTracker(size_t historySize = 120);  // 2 seconds at 60 FPS

    void BeginFrame();
    void EndFrame();

    // Statistics
    double GetCurrentFPS() const { return m_currentFPS; }
    double GetAverageFPS() const;
    double GetMinFPS() const;
    double GetMaxFPS() const;
    double GetAverageFrameTime() const;  // milliseconds
    double GetLastFrameTime() const { return m_lastFrameTime; }

    // History
    const std::vector<double>& GetFrameTimeHistory() const { return m_frameTimeHistory; }

    void Reset();

private:
    PerformanceTimer m_frameTimer;
    std::vector<double> m_frameTimeHistory;
    size_t m_historySize;
    size_t m_historyIndex;
    double m_lastFrameTime;
    double m_currentFPS;
};

/**
 * Memory usage tracker
 */
class MemoryTracker {
public:
    struct MemoryStats {
        size_t workingSetSize;        // Current memory usage (bytes)
        size_t peakWorkingSetSize;    // Peak memory usage (bytes)
        size_t privateUsage;          // Private bytes (bytes)
        size_t virtualSize;           // Virtual memory size (bytes)
    };

    static MemoryStats GetCurrentMemoryUsage();
    static std::string FormatBytes(size_t bytes);
};

} // namespace Performance
} // namespace ReKenshi

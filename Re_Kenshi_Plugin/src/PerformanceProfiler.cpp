#include "PerformanceProfiler.h"
#include <windows.h>
#include <psapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace ReKenshi {
namespace Performance {

//=============================================================================
// ProfileScope Implementation
//=============================================================================

ProfileScope::ProfileScope(const char* name) : m_name(name) {
    m_timer.Start();
    PerformanceProfiler::GetInstance().BeginProfile(m_name);
}

ProfileScope::~ProfileScope() {
    m_timer.Stop();
    PerformanceProfiler::GetInstance().EndProfile(m_name);
}

//=============================================================================
// PerformanceProfiler Implementation
//=============================================================================

PerformanceProfiler& PerformanceProfiler::GetInstance() {
    static PerformanceProfiler instance;
    return instance;
}

void PerformanceProfiler::BeginProfile(const std::string& name) {
    if (!m_enabled) return;

    m_activeTimers[name].Start();
}

void PerformanceProfiler::EndProfile(const std::string& name) {
    if (!m_enabled) return;

    auto it = m_activeTimers.find(name);
    if (it == m_activeTimers.end()) return;

    it->second.Stop();
    double elapsedMs = it->second.GetElapsedMilliseconds();

    // Update statistics
    PerformanceStats& stats = m_stats[name];
    stats.callCount++;
    stats.totalTime += elapsedMs;
    stats.lastTime = elapsedMs;
    stats.minTime = std::min(stats.minTime, elapsedMs);
    stats.maxTime = std::max(stats.maxTime, elapsedMs);

    m_activeTimers.erase(it);
}

const PerformanceStats* PerformanceProfiler::GetStats(const std::string& name) const {
    auto it = m_stats.find(name);
    return (it != m_stats.end()) ? &it->second : nullptr;
}

void PerformanceProfiler::ResetStats(const std::string& name) {
    auto it = m_stats.find(name);
    if (it != m_stats.end()) {
        it->second.Reset();
    }
}

void PerformanceProfiler::ResetAllStats() {
    for (auto& pair : m_stats) {
        pair.second.Reset();
    }
}

std::string PerformanceProfiler::GenerateReport() const {
    std::ostringstream report;

    report << "\n========== Performance Report ==========\n";
    report << std::left << std::setw(30) << "Metric"
           << std::right << std::setw(10) << "Calls"
           << std::setw(12) << "Total (ms)"
           << std::setw(12) << "Avg (ms)"
           << std::setw(12) << "Min (ms)"
           << std::setw(12) << "Max (ms)"
           << std::setw(12) << "Last (ms)"
           << "\n";
    report << std::string(100, '-') << "\n";

    // Sort by total time (descending)
    std::vector<std::pair<std::string, PerformanceStats>> sorted(m_stats.begin(), m_stats.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second.totalTime > b.second.totalTime; });

    for (const auto& pair : sorted) {
        const std::string& name = pair.first;
        const PerformanceStats& stats = pair.second;

        report << std::left << std::setw(30) << name
               << std::right << std::setw(10) << stats.callCount
               << std::setw(12) << std::fixed << std::setprecision(3) << stats.totalTime
               << std::setw(12) << std::fixed << std::setprecision(3) << stats.GetAverageTime()
               << std::setw(12) << std::fixed << std::setprecision(3) << stats.minTime
               << std::setw(12) << std::fixed << std::setprecision(3) << stats.maxTime
               << std::setw(12) << std::fixed << std::setprecision(3) << stats.lastTime
               << "\n";
    }

    report << std::string(100, '=') << "\n\n";

    return report.str();
}

void PerformanceProfiler::PrintReport() const {
    std::string report = GenerateReport();
    OutputDebugStringA(report.c_str());
}

//=============================================================================
// FrameTimeTracker Implementation
//=============================================================================

FrameTimeTracker::FrameTimeTracker(size_t historySize)
    : m_historySize(historySize)
    , m_historyIndex(0)
    , m_lastFrameTime(0.0)
    , m_currentFPS(0.0)
{
    m_frameTimeHistory.resize(historySize, 0.0);
}

void FrameTimeTracker::BeginFrame() {
    m_frameTimer.Start();
}

void FrameTimeTracker::EndFrame() {
    m_frameTimer.Stop();
    m_lastFrameTime = m_frameTimer.GetElapsedMilliseconds();

    // Update history (circular buffer)
    m_frameTimeHistory[m_historyIndex] = m_lastFrameTime;
    m_historyIndex = (m_historyIndex + 1) % m_historySize;

    // Calculate FPS
    if (m_lastFrameTime > 0.0) {
        m_currentFPS = 1000.0 / m_lastFrameTime;
    }
}

double FrameTimeTracker::GetAverageFPS() const {
    double avgFrameTime = GetAverageFrameTime();
    return (avgFrameTime > 0.0) ? (1000.0 / avgFrameTime) : 0.0;
}

double FrameTimeTracker::GetMinFPS() const {
    double maxFrameTime = GetMaxFPS();  // Max frame time = min FPS
    return (maxFrameTime > 0.0) ? (1000.0 / maxFrameTime) : 0.0;
}

double FrameTimeTracker::GetMaxFPS() const {
    double minFrameTime = *std::min_element(m_frameTimeHistory.begin(), m_frameTimeHistory.end());
    return (minFrameTime > 0.0) ? (1000.0 / minFrameTime) : 0.0;
}

double FrameTimeTracker::GetAverageFrameTime() const {
    double sum = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0);
    return sum / m_frameTimeHistory.size();
}

void FrameTimeTracker::Reset() {
    std::fill(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0);
    m_historyIndex = 0;
    m_lastFrameTime = 0.0;
    m_currentFPS = 0.0;
}

//=============================================================================
// MemoryTracker Implementation
//=============================================================================

MemoryTracker::MemoryStats MemoryTracker::GetCurrentMemoryUsage() {
    MemoryStats stats = {};

    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        stats.workingSetSize = pmc.WorkingSetSize;
        stats.peakWorkingSetSize = pmc.PeakWorkingSetSize;
        stats.privateUsage = pmc.PrivateUsage;
    }

    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        stats.virtualSize = memStatus.ullTotalVirtual - memStatus.ullAvailVirtual;
    }

    return stats;
}

std::string MemoryTracker::FormatBytes(size_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

} // namespace Performance
} // namespace ReKenshi

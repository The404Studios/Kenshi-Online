#pragma once

#include "PatternResolver.h"
#include "PatternInterpreter.h"
#include "PatternDatabase.h"
#include "KenshiStructures.h"
#include "KenshiAdvancedStructures.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>

namespace ReKenshi {
namespace Patterns {

/**
 * Pattern update callback
 */
using PatternUpdateCallback = std::function<void(const std::string& patternName, const InterpretedData& data)>;

/**
 * Cached pattern data
 */
struct CachedPatternData {
    std::string patternName;
    InterpretedData data;
    uint64_t lastUpdate;  // timestamp ms
    uint64_t updateInterval;  // ms
    bool needsUpdate;
};

/**
 * Pattern Coordinator
 * Orchestrates pattern resolution, interpretation, and automatic updates
 * This is the main system that does all the heavy lifting
 */
class PatternCoordinator {
public:
    static PatternCoordinator& GetInstance();

    // Lifecycle
    bool Initialize(const std::string& moduleName = "kenshi_x64.exe");
    void Shutdown();
    void Update(float deltaTime);

    bool IsInitialized() const { return m_initialized; }
    bool IsRunning() const { return m_running; }

    // Automatic pattern management
    bool EnableAutoUpdate(bool enable);
    bool SetUpdateRate(float rateHz);  // How often to update cached data
    bool SetUpdateInterval(const std::string& patternName, uint64_t intervalMs);

    // Pattern operations (high-level API)
    bool ResolveAllPatterns();
    bool ResolvePattern(const std::string& patternName);
    bool IsPatternReady(const std::string& patternName);

    // Data access (automatically cached and updated)
    bool GetCharacterData(const std::string& patternName, Kenshi::CharacterData& outData);
    bool GetWorldState(Kenshi::WorldStateData& outData);
    bool GetCurrentDay(int& outDay);
    bool GetCurrentTime(float& outTime);

    // Get any interpreted data
    bool GetInterpretedData(const std::string& patternName, InterpretedData& outData);

    // List operations
    std::vector<Kenshi::CharacterData> GetAllCharacters();
    std::vector<Kenshi::BuildingData> GetAllBuildings();
    std::vector<Kenshi::NPCData> GetAllNPCs();

    // Subscribe to pattern updates
    void SubscribeToPattern(const std::string& patternName, PatternUpdateCallback callback);
    void UnsubscribeFromPattern(const std::string& patternName);

    // Register custom patterns
    bool RegisterCustomPattern(const std::string& category, const PatternEntry& pattern);

    // Diagnostics
    void PrintFullDiagnostics();
    void PrintPatternStatus(const std::string& patternName);
    void PrintCategoryStatus(const std::string& category);

    // Get comprehensive statistics
    struct ComprehensiveStats {
        PatternResolver::Statistics resolver;
        PatternInterpreter::Statistics interpreter;
        uint32_t cachedPatterns;
        uint32_t activeSubscriptions;
        double totalUpdateTime;  // ms
        uint64_t updateCycles;
    };

    ComprehensiveStats GetComprehensiveStatistics() const;

    // Access underlying systems (if needed)
    PatternResolver& GetResolver() { return *m_resolver; }
    PatternInterpreter& GetInterpreter() { return *m_interpreter; }
    PatternDatabase& GetDatabase() { return PatternDatabase::GetInstance(); }

private:
    PatternCoordinator() = default;
    ~PatternCoordinator() = default;
    PatternCoordinator(const PatternCoordinator&) = delete;
    PatternCoordinator& operator=(const PatternCoordinator&) = delete;

    // Initialization helpers
    bool InitializeResolver();
    bool InitializeInterpreter();
    bool InitializeCache();

    // Update logic
    void UpdateCachedData();
    void UpdatePattern(const std::string& patternName);
    bool ShouldUpdatePattern(const CachedPatternData& cached);

    // Callback handling
    void NotifySubscribers(const std::string& patternName, const InterpretedData& data);

    // Critical pattern initialization
    bool InitializeCriticalPatterns();

    // State
    bool m_initialized = false;
    bool m_running = false;
    std::string m_moduleName;
    float m_updateAccumulator = 0.0f;
    float m_updateInterval = 0.1f;  // 10 Hz default

    // Core systems
    std::unique_ptr<PatternResolver> m_resolver;
    std::unique_ptr<PatternInterpreter> m_interpreter;

    // Cache
    std::unordered_map<std::string, CachedPatternData> m_cache;
    mutable std::mutex m_cacheMutex;

    // Subscriptions
    std::unordered_map<std::string, std::vector<PatternUpdateCallback>> m_subscriptions;
    mutable std::mutex m_subscriptionMutex;

    // Statistics
    mutable std::mutex m_statsMutex;
    ComprehensiveStats m_stats;
    uint64_t m_lastUpdateTime = 0;
};

/**
 * RAII helper for automatic coordinator initialization
 */
class CoordinatorGuard {
public:
    explicit CoordinatorGuard(const std::string& moduleName = "kenshi_x64.exe") {
        m_initialized = PatternCoordinator::GetInstance().Initialize(moduleName);
    }

    ~CoordinatorGuard() {
        if (m_initialized) {
            PatternCoordinator::GetInstance().Shutdown();
        }
    }

    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace Patterns
} // namespace ReKenshi

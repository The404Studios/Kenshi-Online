#include "PatternCoordinator.h"
#include "Logger.h"
#include "Utilities.h"

namespace ReKenshi {
namespace Patterns {

PatternCoordinator& PatternCoordinator::GetInstance() {
    static PatternCoordinator instance;
    return instance;
}

bool PatternCoordinator::Initialize(const std::string& moduleName) {
    LOG_FUNCTION();

    if (m_initialized) {
        LOG_WARNING("PatternCoordinator already initialized");
        return true;
    }

    LOG_INFO("========================================");
    LOG_INFO("Initializing PatternCoordinator");
    LOG_INFO("========================================");

    m_moduleName = moduleName;

    // Step 1: Initialize Resolver
    if (!InitializeResolver()) {
        LOG_ERROR("Failed to initialize resolver");
        return false;
    }

    // Step 2: Initialize Interpreter
    if (!InitializeInterpreter()) {
        LOG_ERROR("Failed to initialize interpreter");
        return false;
    }

    // Step 3: Initialize Cache
    if (!InitializeCache()) {
        LOG_ERROR("Failed to initialize cache");
        return false;
    }

    // Step 4: Resolve critical patterns
    if (!InitializeCriticalPatterns()) {
        LOG_ERROR("Failed to resolve critical patterns");
        return false;
    }

    m_initialized = true;
    m_running = true;

    LOG_INFO("========================================");
    LOG_INFO("PatternCoordinator initialized");
    LOG_INFO("========================================");

    PrintFullDiagnostics();

    return true;
}

void PatternCoordinator::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down PatternCoordinator");

    m_running = false;

    // Clear cache
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache.clear();
    }

    // Clear subscriptions
    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        m_subscriptions.clear();
    }

    m_resolver.reset();
    m_interpreter.reset();

    m_initialized = false;

    LOG_INFO("PatternCoordinator shut down");
}

void PatternCoordinator::Update(float deltaTime) {
    if (!m_initialized || !m_running) {
        return;
    }

    m_updateAccumulator += deltaTime;

    // Update at configured rate
    if (m_updateAccumulator >= m_updateInterval) {
        m_updateAccumulator = 0.0f;

        uint64_t startTime = Utils::TimeUtils::GetCurrentTimestampMs();

        UpdateCachedData();

        double updateTime = Utils::TimeUtils::GetElapsedMs(startTime);

        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalUpdateTime += updateTime;
        m_stats.updateCycles++;
    }
}

bool PatternCoordinator::EnableAutoUpdate(bool enable) {
    m_running = enable;
    LOG_INFO_F("Auto-update %s", enable ? "enabled" : "disabled");
    return true;
}

bool PatternCoordinator::SetUpdateRate(float rateHz) {
    if (rateHz <= 0.0f || rateHz > 120.0f) {
        LOG_ERROR_F("Invalid update rate: %.1f Hz (must be 0.1-120 Hz)", rateHz);
        return false;
    }

    m_updateInterval = 1.0f / rateHz;
    LOG_INFO_F("Update rate set to %.1f Hz (%.1f ms)", rateHz, m_updateInterval * 1000.0f);
    return true;
}

bool PatternCoordinator::SetUpdateInterval(const std::string& patternName, uint64_t intervalMs) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_cache.find(patternName);
    if (it != m_cache.end()) {
        it->second.updateInterval = intervalMs;
        LOG_DEBUG_F("Update interval for '%s' set to %llu ms", patternName.c_str(), intervalMs);
        return true;
    }

    LOG_WARNING_F("Pattern not in cache: %s", patternName.c_str());
    return false;
}

bool PatternCoordinator::ResolveAllPatterns() {
    if (!m_resolver) {
        LOG_ERROR("Resolver not initialized");
        return false;
    }

    return m_resolver->ResolveAllPatterns(m_moduleName);
}

bool PatternCoordinator::ResolvePattern(const std::string& patternName) {
    if (!m_resolver) {
        LOG_ERROR("Resolver not initialized");
        return false;
    }

    return m_resolver->ResolvePattern(patternName, m_moduleName);
}

bool PatternCoordinator::IsPatternReady(const std::string& patternName) {
    if (!m_resolver) {
        return false;
    }

    return m_resolver->IsResolved(patternName);
}

bool PatternCoordinator::GetCharacterData(const std::string& patternName, Kenshi::CharacterData& outData) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_cache.find(patternName);
    if (it != m_cache.end() && it->second.data.success) {
        try {
            outData = std::any_cast<Kenshi::CharacterData>(it->second.data.data);
            return true;
        } catch (const std::bad_any_cast&) {
            LOG_WARNING_F("Type mismatch for pattern: %s", patternName.c_str());
        }
    }

    // Not in cache or failed, try to read directly
    if (m_interpreter) {
        return m_interpreter->ReadCharacterData(patternName, outData);
    }

    return false;
}

bool PatternCoordinator::GetWorldState(Kenshi::WorldStateData& outData) {
    return GetInterpretedData(PatternNames::WORLD_STATE, [&](const InterpretedData& data) {
        try {
            outData = std::any_cast<Kenshi::WorldStateData>(data.data);
            return true;
        } catch (const std::bad_any_cast&) {
            return false;
        }
    });
}

bool PatternCoordinator::GetCurrentDay(int& outDay) {
    Kenshi::WorldStateData worldState;
    if (GetWorldState(worldState)) {
        outDay = worldState.currentDay;
        return true;
    }
    return false;
}

bool PatternCoordinator::GetCurrentTime(float& outTime) {
    Kenshi::WorldStateData worldState;
    if (GetWorldState(worldState)) {
        outTime = worldState.currentTime;
        return true;
    }
    return false;
}

bool PatternCoordinator::GetInterpretedData(const std::string& patternName, InterpretedData& outData) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    auto it = m_cache.find(patternName);
    if (it != m_cache.end()) {
        outData = it->second.data;
        return it->second.data.success;
    }

    // Not in cache, interpret now
    if (m_interpreter) {
        outData = m_interpreter->InterpretPattern(patternName);
        return outData.success;
    }

    return false;
}

std::vector<Kenshi::CharacterData> PatternCoordinator::GetAllCharacters() {
    std::vector<Kenshi::CharacterData> characters;

    if (m_interpreter) {
        characters = m_interpreter->ReadCharacterList(PatternNames::CHARACTER_LIST, 100);
    }

    return characters;
}

std::vector<Kenshi::BuildingData> PatternCoordinator::GetAllBuildings() {
    std::vector<Kenshi::BuildingData> buildings;

    // TODO: Implement building list traversal
    LOG_TRACE("GetAllBuildings called");

    return buildings;
}

std::vector<Kenshi::NPCData> PatternCoordinator::GetAllNPCs() {
    std::vector<Kenshi::NPCData> npcs;

    // TODO: Implement NPC list traversal
    LOG_TRACE("GetAllNPCs called");

    return npcs;
}

void PatternCoordinator::SubscribeToPattern(const std::string& patternName, PatternUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(m_subscriptionMutex);

    m_subscriptions[patternName].push_back(callback);

    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    m_stats.activeSubscriptions++;

    LOG_DEBUG_F("Subscribed to pattern: %s", patternName.c_str());
}

void PatternCoordinator::UnsubscribeFromPattern(const std::string& patternName) {
    std::lock_guard<std::mutex> lock(m_subscriptionMutex);

    auto it = m_subscriptions.find(patternName);
    if (it != m_subscriptions.end()) {
        size_t count = it->second.size();
        m_subscriptions.erase(it);

        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.activeSubscriptions -= static_cast<uint32_t>(count);

        LOG_DEBUG_F("Unsubscribed from pattern: %s", patternName.c_str());
    }
}

bool PatternCoordinator::RegisterCustomPattern(const std::string& category, const PatternEntry& pattern) {
    PatternDatabase::GetInstance().AddPattern(category, pattern);
    LOG_INFO_F("Registered custom pattern: %s in category: %s", pattern.name.c_str(), category.c_str());
    return true;
}

void PatternCoordinator::PrintFullDiagnostics() {
    LOG_INFO("========================================");
    LOG_INFO("PatternCoordinator Full Diagnostics");
    LOG_INFO("========================================");

    LOG_INFO_F("Initialized: %s", m_initialized ? "Yes" : "No");
    LOG_INFO_F("Running: %s", m_running ? "Yes" : "No");
    LOG_INFO_F("Module: %s", m_moduleName.c_str());
    LOG_INFO_F("Update Rate: %.1f Hz", 1.0f / m_updateInterval);
    LOG_INFO("");

    // Resolver statistics
    if (m_resolver) {
        const auto& resolverStats = m_resolver->GetStatistics();
        LOG_INFO("Resolver Statistics:");
        LOG_INFO_F("  Attempted: %u", resolverStats.totalAttempted);
        LOG_INFO_F("  Resolved: %u", resolverStats.totalResolved);
        LOG_INFO_F("  Failed: %u", resolverStats.totalFailed);
        LOG_INFO_F("  Avg Time: %.2f ms", resolverStats.averageResolutionTime);
        LOG_INFO("");
    }

    // Interpreter statistics
    if (m_interpreter) {
        const auto& interpStats = m_interpreter->GetStatistics();
        LOG_INFO("Interpreter Statistics:");
        LOG_INFO_F("  Interpretations: %u", interpStats.interpretationsAttempted);
        LOG_INFO_F("  Succeeded: %u", interpStats.interpretationsSucceeded);
        LOG_INFO_F("  Failed: %u", interpStats.interpretationsFailed);
        LOG_INFO_F("  Structures Read: %u", interpStats.structuresRead);
        LOG_INFO("");
    }

    // Coordinator statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        LOG_INFO("Coordinator Statistics:");
        LOG_INFO_F("  Cached Patterns: %u", m_stats.cachedPatterns);
        LOG_INFO_F("  Active Subscriptions: %u", m_stats.activeSubscriptions);
        LOG_INFO_F("  Update Cycles: %llu", m_stats.updateCycles);
        LOG_INFO_F("  Total Update Time: %.2f ms", m_stats.totalUpdateTime);
        if (m_stats.updateCycles > 0) {
            double avgUpdateTime = m_stats.totalUpdateTime / m_stats.updateCycles;
            LOG_INFO_F("  Avg Update Time: %.2f ms", avgUpdateTime);
        }
    }

    LOG_INFO("========================================");
}

void PatternCoordinator::PrintPatternStatus(const std::string& patternName) {
    LOG_INFO_F("========== Pattern Status: %s ==========", patternName.c_str());

    if (m_resolver) {
        const ResolvedPattern* resolved = m_resolver->GetResolvedPattern(patternName);
        if (resolved) {
            LOG_INFO_F("Resolved: %s", resolved->found ? "Yes" : "No");
            if (resolved->found) {
                LOG_INFO_F("Address: 0x%llX", resolved->address);
                LOG_INFO_F("Resolution Time: %.2f ms", resolved->resolutionTime);
            } else {
                LOG_INFO_F("Error: %s", resolved->error.c_str());
            }
        } else {
            LOG_INFO("Not resolved");
        }
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_cache.find(patternName);
    if (it != m_cache.end()) {
        LOG_INFO("Cached: Yes");
        LOG_INFO_F("Data Type: %s", it->second.data.dataType.c_str());
        LOG_INFO_F("Last Update: %llu ms ago",
                   Utils::TimeUtils::GetCurrentTimestampMs() - it->second.lastUpdate);
    } else {
        LOG_INFO("Cached: No");
    }

    LOG_INFO("========================================");
}

void PatternCoordinator::PrintCategoryStatus(const std::string& category) {
    LOG_INFO_F("========== Category Status: %s ==========", category.c_str());

    if (m_resolver) {
        auto resolved = m_resolver->GetResolvedPatternsInCategory(category);
        LOG_INFO_F("Resolved Patterns: %zu", resolved.size());

        for (const auto& pattern : resolved) {
            LOG_INFO_F("  - %s: 0x%llX", pattern.name.c_str(), pattern.address);
        }
    }

    LOG_INFO("========================================");
}

PatternCoordinator::ComprehensiveStats PatternCoordinator::GetComprehensiveStatistics() const {
    ComprehensiveStats stats;

    if (m_resolver) {
        stats.resolver = m_resolver->GetStatistics();
    }

    if (m_interpreter) {
        stats.interpreter = m_interpreter->GetStatistics();
    }

    std::lock_guard<std::mutex> lock(m_statsMutex);
    stats.cachedPatterns = m_stats.cachedPatterns;
    stats.activeSubscriptions = m_stats.activeSubscriptions;
    stats.totalUpdateTime = m_stats.totalUpdateTime;
    stats.updateCycles = m_stats.updateCycles;

    return stats;
}

// Private helper methods

bool PatternCoordinator::InitializeResolver() {
    LOG_INFO("Initializing PatternResolver...");

    m_resolver = std::make_unique<PatternResolver>();
    m_resolver->SetRetryCount(3);
    m_resolver->SetRetryDelay(100);
    m_resolver->EnableCaching(true);

    LOG_INFO("PatternResolver initialized");
    return true;
}

bool PatternCoordinator::InitializeInterpreter() {
    LOG_INFO("Initializing PatternInterpreter...");

    m_interpreter = std::make_unique<PatternInterpreter>();
    m_interpreter->Initialize(m_resolver.get());

    LOG_INFO("PatternInterpreter initialized");
    return true;
}

bool PatternCoordinator::InitializeCache() {
    LOG_INFO("Initializing pattern cache...");

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();

    LOG_INFO("Pattern cache initialized");
    return true;
}

bool PatternCoordinator::InitializeCriticalPatterns() {
    LOG_INFO("Resolving critical patterns...");

    std::vector<std::string> criticalPatterns = {
        PatternNames::GAME_WORLD,
        PatternNames::PLAYER_CHARACTER,
        PatternNames::CHARACTER_LIST,
        PatternNames::WORLD_STATE
    };

    int resolved = 0;
    for (const auto& pattern : criticalPatterns) {
        if (ResolvePattern(pattern)) {
            resolved++;

            // Add to cache
            CachedPatternData cached;
            cached.patternName = pattern;
            cached.lastUpdate = 0;
            cached.updateInterval = 100;  // 100ms default
            cached.needsUpdate = true;

            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_cache[pattern] = cached;

            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            m_stats.cachedPatterns++;
        }
    }

    LOG_INFO_F("Resolved %d/%zu critical patterns", resolved, criticalPatterns.size());

    return resolved >= 2;  // At least 2 critical patterns must be found
}

void PatternCoordinator::UpdateCachedData() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);

    for (auto& [name, cached] : m_cache) {
        if (ShouldUpdatePattern(cached)) {
            UpdatePattern(name);
        }
    }
}

void PatternCoordinator::UpdatePattern(const std::string& patternName) {
    auto it = m_cache.find(patternName);
    if (it == m_cache.end()) {
        return;
    }

    InterpretedData data = m_interpreter->InterpretPattern(patternName);

    it->second.data = data;
    it->second.lastUpdate = Utils::TimeUtils::GetCurrentTimestampMs();
    it->second.needsUpdate = false;

    // Notify subscribers
    if (data.success) {
        NotifySubscribers(patternName, data);
    }
}

bool PatternCoordinator::ShouldUpdatePattern(const CachedPatternData& cached) {
    if (cached.needsUpdate) {
        return true;
    }

    uint64_t now = Utils::TimeUtils::GetCurrentTimestampMs();
    uint64_t elapsed = now - cached.lastUpdate;

    return elapsed >= cached.updateInterval;
}

void PatternCoordinator::NotifySubscribers(const std::string& patternName, const InterpretedData& data) {
    std::lock_guard<std::mutex> lock(m_subscriptionMutex);

    auto it = m_subscriptions.find(patternName);
    if (it != m_subscriptions.end()) {
        for (const auto& callback : it->second) {
            try {
                callback(patternName, data);
            } catch (const std::exception& e) {
                LOG_ERROR_F("Subscription callback error for '%s': %s",
                            patternName.c_str(), e.what());
            }
        }
    }
}

} // namespace Patterns
} // namespace ReKenshi

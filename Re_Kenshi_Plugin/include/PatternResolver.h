#pragma once

#include "PatternDatabase.h"
#include "MemoryScanner.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

namespace ReKenshi {
namespace Patterns {

/**
 * Pattern resolution result
 */
struct ResolvedPattern {
    std::string name;
    uintptr_t address;
    bool found;
    bool resolved;
    double resolutionTime;  // ms
    std::string error;
};

/**
 * Pattern Resolver
 * Automatically finds and resolves all patterns from the database
 */
class PatternResolver {
public:
    static PatternResolver& GetInstance();

    // Resolution operations
    bool ResolveAllPatterns(const std::string& moduleName = "kenshi_x64.exe");
    bool ResolvePattern(const std::string& patternName, const std::string& moduleName = "kenshi_x64.exe");
    bool ResolveCategory(const std::string& category, const std::string& moduleName = "kenshi_x64.exe");

    // Query resolved patterns
    bool IsResolved(const std::string& patternName) const;
    uintptr_t GetResolvedAddress(const std::string& patternName) const;
    const ResolvedPattern* GetResolvedPattern(const std::string& patternName) const;

    // Get all resolved patterns
    std::vector<ResolvedPattern> GetAllResolvedPatterns() const;
    std::vector<ResolvedPattern> GetResolvedPatternsInCategory(const std::string& category) const;

    // Statistics
    struct Statistics {
        uint32_t totalAttempted = 0;
        uint32_t totalResolved = 0;
        uint32_t totalFailed = 0;
        double totalResolutionTime = 0.0;  // ms
        double averageResolutionTime = 0.0;  // ms
    };

    const Statistics& GetStatistics() const { return m_stats; }
    void ResetStatistics();

    // Advanced options
    void SetRetryCount(int retries) { m_retryCount = retries; }
    void SetRetryDelay(int delayMs) { m_retryDelayMs = delayMs; }
    void EnableCaching(bool enable) { m_enableCaching = enable; }
    void ClearCache();

private:
    PatternResolver() = default;
    ~PatternResolver() = default;
    PatternResolver(const PatternResolver&) = delete;
    PatternResolver& operator=(const PatternResolver&) = delete;

    // Core resolution logic
    ResolvedPattern ResolvePatternInternal(const PatternEntry& pattern, const std::string& moduleName);
    uintptr_t ScanForPattern(const std::string& patternString, const std::string& moduleName);
    uintptr_t ApplyPatternOffset(uintptr_t address, int offset);
    uintptr_t ResolveRIPRelative(uintptr_t address);

    // Thread-safe storage
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ResolvedPattern> m_resolvedPatterns;

    // Configuration
    int m_retryCount = 3;
    int m_retryDelayMs = 100;
    bool m_enableCaching = true;

    // Statistics
    Statistics m_stats;
};

} // namespace Patterns
} // namespace ReKenshi

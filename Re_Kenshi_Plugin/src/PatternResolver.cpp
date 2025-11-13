#include "PatternResolver.h"
#include "Logger.h"
#include "Utilities.h"
#include <chrono>

namespace ReKenshi {
namespace Patterns {

PatternResolver& PatternResolver::GetInstance() {
    static PatternResolver instance;
    return instance;
}

bool PatternResolver::ResolveAllPatterns(const std::string& moduleName) {
    LOG_FUNCTION();
    LOG_INFO("Resolving all patterns...");

    auto& db = PatternDatabase::GetInstance();
    auto categories = db.GetCategories();

    int totalResolved = 0;
    int totalAttempted = 0;

    for (const auto& category : categories) {
        auto patterns = db.GetPatternsByCategory(category);

        for (const auto* pattern : patterns) {
            totalAttempted++;

            if (ResolvePattern(pattern->name, moduleName)) {
                totalResolved++;
            }
        }
    }

    LOG_INFO_F("Resolved %d/%d patterns", totalResolved, totalAttempted);

    return totalResolved > 0;
}

bool PatternResolver::ResolvePattern(const std::string& patternName, const std::string& moduleName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check cache first
    if (m_enableCaching) {
        auto it = m_resolvedPatterns.find(patternName);
        if (it != m_resolvedPatterns.end() && it->second.found) {
            LOG_TRACE_F("Pattern '%s' already resolved (cached)", patternName.c_str());
            return true;
        }
    }

    auto& db = PatternDatabase::GetInstance();
    const PatternEntry* pattern = db.GetPattern(patternName);

    if (!pattern) {
        LOG_ERROR_F("Pattern not found in database: %s", patternName.c_str());
        return false;
    }

    LOG_DEBUG_F("Resolving pattern: %s", patternName.c_str());

    ResolvedPattern resolved = ResolvePatternInternal(*pattern, moduleName);

    m_resolvedPatterns[patternName] = resolved;
    m_stats.totalAttempted++;

    if (resolved.found && resolved.resolved) {
        m_stats.totalResolved++;
        m_stats.totalResolutionTime += resolved.resolutionTime;
        m_stats.averageResolutionTime = m_stats.totalResolutionTime / m_stats.totalResolved;

        LOG_INFO_F("Pattern resolved: %s at 0x%llX (%.2f ms)",
                   patternName.c_str(),
                   resolved.address,
                   resolved.resolutionTime);
        return true;
    } else {
        m_stats.totalFailed++;
        LOG_WARNING_F("Failed to resolve pattern: %s - %s",
                      patternName.c_str(),
                      resolved.error.c_str());
        return false;
    }
}

bool PatternResolver::ResolveCategory(const std::string& category, const std::string& moduleName) {
    LOG_INFO_F("Resolving category: %s", category.c_str());

    auto& db = PatternDatabase::GetInstance();
    auto patterns = db.GetPatternsByCategory(category);

    if (patterns.empty()) {
        LOG_WARNING_F("Category not found or empty: %s", category.c_str());
        return false;
    }

    int resolved = 0;
    for (const auto* pattern : patterns) {
        if (ResolvePattern(pattern->name, moduleName)) {
            resolved++;
        }
    }

    LOG_INFO_F("Resolved %d/%zu patterns in category '%s'",
               resolved, patterns.size(), category.c_str());

    return resolved > 0;
}

bool PatternResolver::IsResolved(const std::string& patternName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resolvedPatterns.find(patternName);
    return (it != m_resolvedPatterns.end() && it->second.found && it->second.resolved);
}

uintptr_t PatternResolver::GetResolvedAddress(const std::string& patternName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resolvedPatterns.find(patternName);
    if (it != m_resolvedPatterns.end() && it->second.found) {
        return it->second.address;
    }

    return 0;
}

const ResolvedPattern* PatternResolver::GetResolvedPattern(const std::string& patternName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_resolvedPatterns.find(patternName);
    if (it != m_resolvedPatterns.end()) {
        return &it->second;
    }

    return nullptr;
}

std::vector<ResolvedPattern> PatternResolver::GetAllResolvedPatterns() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ResolvedPattern> result;
    for (const auto& [name, pattern] : m_resolvedPatterns) {
        if (pattern.found) {
            result.push_back(pattern);
        }
    }

    return result;
}

std::vector<ResolvedPattern> PatternResolver::GetResolvedPatternsInCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& db = PatternDatabase::GetInstance();
    auto categoryPatterns = db.GetPatternsByCategory(category);

    std::vector<ResolvedPattern> result;

    for (const auto* pattern : categoryPatterns) {
        auto it = m_resolvedPatterns.find(pattern->name);
        if (it != m_resolvedPatterns.end() && it->second.found) {
            result.push_back(it->second);
        }
    }

    return result;
}

void PatternResolver::ResetStatistics() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats = Statistics();
}

void PatternResolver::ClearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_resolvedPatterns.clear();
    LOG_INFO("Pattern cache cleared");
}

ResolvedPattern PatternResolver::ResolvePatternInternal(const PatternEntry& pattern, const std::string& moduleName) {
    ResolvedPattern result;
    result.name = pattern.name;
    result.found = false;
    result.resolved = false;

    auto startTime = Utils::TimeUtils::GetCurrentTimestampMs();

    // Attempt to scan for pattern with retries
    uintptr_t address = 0;

    for (int attempt = 0; attempt < m_retryCount; attempt++) {
        address = ScanForPattern(pattern.pattern, moduleName);

        if (address) {
            break;
        }

        if (attempt < m_retryCount - 1) {
            LOG_TRACE_F("Pattern scan attempt %d/%d failed, retrying...",
                        attempt + 1, m_retryCount);
            Utils::TimeUtils::SleepMs(m_retryDelayMs);
        }
    }

    if (!address) {
        result.error = "Pattern not found in module";
        result.resolutionTime = Utils::TimeUtils::GetElapsedMs(startTime);
        return result;
    }

    result.found = true;

    // Apply offset
    address = ApplyPatternOffset(address, pattern.offset);

    // Resolve RIP-relative if needed
    if (pattern.isRIPRelative) {
        uintptr_t resolvedAddr = ResolveRIPRelative(address);

        if (!resolvedAddr) {
            result.error = "Failed to resolve RIP-relative address";
            result.resolutionTime = Utils::TimeUtils::GetElapsedMs(startTime);
            return result;
        }

        address = resolvedAddr;
    }

    result.address = address;
    result.resolved = true;
    result.resolutionTime = Utils::TimeUtils::GetElapsedMs(startTime);

    return result;
}

uintptr_t PatternResolver::ScanForPattern(const std::string& patternString, const std::string& moduleName) {
    Memory::MemoryScanner::Pattern pattern;
    pattern.pattern = patternString;
    pattern.mask = "";  // Auto-generated from pattern

    return Memory::MemoryScanner::FindPattern(moduleName.c_str(), pattern);
}

uintptr_t PatternResolver::ApplyPatternOffset(uintptr_t address, int offset) {
    if (offset == 0) {
        return address;
    }

    LOG_TRACE_F("Applying offset: %d", offset);
    return address + offset;
}

uintptr_t PatternResolver::ResolveRIPRelative(uintptr_t address) {
    LOG_TRACE("Resolving RIP-relative address");
    return Memory::MemoryScanner::ResolveRelativeAddress(address);
}

} // namespace Patterns
} // namespace ReKenshi

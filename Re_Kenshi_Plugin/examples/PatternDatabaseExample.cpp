/**
 * Pattern Database Example for Re_Kenshi Plugin
 *
 * This file demonstrates the pattern database system.
 */

#include "../include/PatternDatabase.h"
#include "../include/MemoryScanner.h"
#include "../include/Logger.h"
#include <iostream>

using namespace ReKenshi::Patterns;
using namespace ReKenshi::Memory;
using namespace ReKenshi::Logging;

//=============================================================================
// Example 1: Basic Pattern Retrieval
//=============================================================================

void Example_BasicRetrieval() {
    std::cout << "=== Basic Pattern Retrieval ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    // Get a specific pattern
    const PatternEntry* pattern = db.GetPattern(PatternNames::PLAYER_CHARACTER);

    if (pattern) {
        std::cout << "Pattern found:\n";
        std::cout << "  Name: " << pattern->name << "\n";
        std::cout << "  Pattern: " << pattern->pattern << "\n";
        std::cout << "  Offset: " << pattern->offset << "\n";
        std::cout << "  RIP-relative: " << (pattern->isRIPRelative ? "Yes" : "No") << "\n";
        std::cout << "  Description: " << pattern->description << "\n";
        std::cout << "  Version: " << pattern->version << "\n";
    } else {
        std::cout << "Pattern not found!\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 2: Category-based Retrieval
//=============================================================================

void Example_CategoryRetrieval() {
    std::cout << "=== Category-based Retrieval ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    // Get all patterns in a category
    auto characterPatterns = db.GetPatternsByCategory(PatternCategories::CHARACTERS);

    std::cout << "Character patterns (" << characterPatterns.size() << " found):\n";
    for (const auto* pattern : characterPatterns) {
        std::cout << "  - " << pattern->name << ": " << pattern->description << "\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 3: List All Categories
//=============================================================================

void Example_ListCategories() {
    std::cout << "=== All Categories ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    auto categories = db.GetCategories();
    std::cout << "Available categories (" << categories.size() << " total):\n";

    for (const auto& category : categories) {
        auto patterns = db.GetPatternsByCategory(category);
        std::cout << "  - " << category << " (" << patterns.size() << " patterns)\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 4: Pattern Scanning with Database
//=============================================================================

void Example_PatternScanning() {
    std::cout << "=== Pattern Scanning ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    // Get pattern from database
    const PatternEntry* pattern = db.GetPattern(PatternNames::GAME_WORLD);

    if (!pattern) {
        std::cout << "Pattern not found in database!\n\n";
        return;
    }

    std::cout << "Scanning for: " << pattern->name << "\n";
    std::cout << "Description: " << pattern->description << "\n";
    std::cout << "Pattern: " << pattern->pattern << "\n\n";

    // Convert pattern string to byte array
    // In real usage, MemoryScanner::FindPattern would handle this
    std::cout << "Scan would be performed with MemoryScanner::FindPattern()\n";
    std::cout << "Result address would then be:\n";
    std::cout << "  - Adjusted by offset: " << pattern->offset << "\n";

    if (pattern->isRIPRelative) {
        std::cout << "  - Resolved as RIP-relative address\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 5: Custom Pattern Addition
//=============================================================================

void Example_CustomPatterns() {
    std::cout << "=== Custom Pattern Addition ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    // Add a custom pattern
    PatternEntry customPattern;
    customPattern.name = "CustomGameState";
    customPattern.pattern = "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC";
    customPattern.offset = 0;
    customPattern.isRIPRelative = false;
    customPattern.description = "Custom game state manager";
    customPattern.version = "1.0.0-custom";

    db.AddPattern("Custom", customPattern);

    std::cout << "Added custom pattern to 'Custom' category\n";

    // Retrieve it
    const PatternEntry* retrieved = db.GetPattern("CustomGameState");
    if (retrieved) {
        std::cout << "Successfully retrieved custom pattern:\n";
        std::cout << "  Name: " << retrieved->name << "\n";
        std::cout << "  Description: " << retrieved->description << "\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 6: World Pattern Showcase
//=============================================================================

void Example_WorldPatterns() {
    std::cout << "=== World Patterns ===\n\n";

    auto& db = PatternDatabase::GetInstance();
    auto patterns = db.GetPatternsByCategory(PatternCategories::WORLD);

    std::cout << "World-related patterns:\n\n";

    for (const auto* pattern : patterns) {
        std::cout << "Pattern: " << pattern->name << "\n";
        std::cout << "  " << pattern->description << "\n";
        std::cout << "  Signature: " << pattern->pattern << "\n";
        std::cout << "\n";
    }
}

//=============================================================================
// Example 7: Combat Pattern Showcase
//=============================================================================

void Example_CombatPatterns() {
    std::cout << "=== Combat Patterns ===\n\n";

    auto& db = PatternDatabase::GetInstance();
    auto patterns = db.GetPatternsByCategory(PatternCategories::COMBAT);

    std::cout << "Combat-related patterns:\n\n";

    for (const auto* pattern : patterns) {
        std::cout << "Pattern: " << pattern->name << "\n";
        std::cout << "  " << pattern->description << "\n";
        std::cout << "  Signature: " << pattern->pattern << "\n";
        std::cout << "\n";
    }
}

//=============================================================================
// Example 8: Pattern Existence Check
//=============================================================================

void Example_PatternCheck() {
    std::cout << "=== Pattern Existence Check ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    std::vector<std::string> patternsToCheck = {
        PatternNames::GAME_WORLD,
        PatternNames::PLAYER_CHARACTER,
        PatternNames::COMBAT_MANAGER,
        "NonExistentPattern"
    };

    std::cout << "Checking pattern existence:\n";
    for (const auto& name : patternsToCheck) {
        bool exists = db.HasPattern(name);
        std::cout << "  " << name << ": " << (exists ? "EXISTS" : "NOT FOUND") << "\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Example 9: Complete Pattern Database Overview
//=============================================================================

void Example_DatabaseOverview() {
    std::cout << "=== Complete Database Overview ===\n\n";

    auto& db = PatternDatabase::GetInstance();
    auto categories = db.GetCategories();

    int totalPatterns = 0;

    for (const auto& category : categories) {
        auto patterns = db.GetPatternsByCategory(category);
        totalPatterns += static_cast<int>(patterns.size());

        std::cout << category << " (" << patterns.size() << " patterns)\n";
        std::cout << std::string(40, '-') << "\n";

        for (const auto* pattern : patterns) {
            std::cout << "  " << pattern->name << "\n";
            std::cout << "    " << pattern->description << "\n";
        }

        std::cout << "\n";
    }

    std::cout << "Total patterns in database: " << totalPatterns << "\n\n";
}

//=============================================================================
// Example 10: Practical Usage - Game Structure Scanner
//=============================================================================

class GameStructureScanner {
public:
    void ScanAllCriticalStructures() {
        LOG_INFO("Scanning for critical game structures...");

        auto& db = PatternDatabase::GetInstance();

        // Critical patterns to scan
        std::vector<std::string> criticalPatterns = {
            PatternNames::GAME_WORLD,
            PatternNames::PLAYER_CHARACTER,
            PatternNames::CHARACTER_LIST,
            PatternNames::WORLD_STATE
        };

        int found = 0;
        int total = static_cast<int>(criticalPatterns.size());

        for (const auto& patternName : criticalPatterns) {
            const PatternEntry* pattern = db.GetPattern(patternName);

            if (!pattern) {
                LOG_ERROR_F("Pattern not in database: %s", patternName.c_str());
                continue;
            }

            LOG_INFO_F("Scanning for: %s", pattern->name.c_str());

            // In real usage, would call MemoryScanner::FindPattern here
            // uintptr_t address = MemoryScanner::FindPattern("kenshi_x64.exe", pattern->pattern);

            // For demonstration, simulate finding it
            bool simulated_found = true;

            if (simulated_found) {
                LOG_INFO_F("  Found: %s", pattern->name.c_str());

                if (pattern->isRIPRelative) {
                    LOG_DEBUG("  Resolving RIP-relative address...");
                }

                found++;
            } else {
                LOG_WARNING_F("  Not found: %s", pattern->name.c_str());
            }
        }

        LOG_INFO_F("Scan complete: %d/%d structures found", found, total);
    }

private:
    std::unordered_map<std::string, uintptr_t> m_structureAddresses;
};

void Example_PracticalUsage() {
    std::cout << "=== Practical Usage Example ===\n\n";

    // Initialize logger for this example
    auto& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::Info);
    logger.SetOutputTargets(LogOutput::Console);

    GameStructureScanner scanner;
    scanner.ScanAllCriticalStructures();

    std::cout << "\n";
}

//=============================================================================
// Example 11: Pattern Version Tracking
//=============================================================================

void Example_VersionTracking() {
    std::cout << "=== Pattern Version Tracking ===\n\n";

    auto& db = PatternDatabase::GetInstance();

    std::cout << "Pattern versions (useful for multi-version support):\n\n";

    auto characterPatterns = db.GetPatternsByCategory(PatternCategories::CHARACTERS);

    for (const auto* pattern : characterPatterns) {
        std::cout << pattern->name << " - Version: " << pattern->version << "\n";
    }

    std::cout << "\n";
    std::cout << "In practice, you could:\n";
    std::cout << "  - Have multiple patterns for different game versions\n";
    std::cout << "  - Try patterns in order of version compatibility\n";
    std::cout << "  - Automatically select correct pattern set\n\n";
}

//=============================================================================
// Example 12: Building a Pattern Scanner Helper
//=============================================================================

class PatternScanHelper {
public:
    static bool ScanAndResolve(const std::string& patternName, uintptr_t& outAddress) {
        auto& db = PatternDatabase::GetInstance();

        const PatternEntry* pattern = db.GetPattern(patternName);
        if (!pattern) {
            LOG_ERROR_F("Pattern not found in database: %s", patternName.c_str());
            return false;
        }

        LOG_DEBUG_F("Scanning for pattern: %s", pattern->name.c_str());

        // In real usage:
        // uintptr_t address = MemoryScanner::FindPattern("kenshi_x64.exe", pattern->pattern);

        // For demonstration:
        uintptr_t address = 0x140000000;  // Simulated address

        if (!address) {
            LOG_WARNING_F("Pattern not found: %s", pattern->name.c_str());
            return false;
        }

        // Apply offset
        address += pattern->offset;

        // Resolve RIP-relative if needed
        if (pattern->isRIPRelative) {
            LOG_DEBUG("Resolving RIP-relative address...");
            // address = MemoryScanner::ResolveRelativeAddress(address);
        }

        outAddress = address;
        LOG_INFO_F("Pattern found: %s at 0x%llX", pattern->name.c_str(), address);

        return true;
    }
};

void Example_ScanHelper() {
    std::cout << "=== Pattern Scan Helper ===\n\n";

    auto& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::Debug);
    logger.SetOutputTargets(LogOutput::Console);

    uintptr_t address = 0;

    if (PatternScanHelper::ScanAndResolve(PatternNames::PLAYER_CHARACTER, address)) {
        std::cout << "Successfully scanned and resolved pattern\n";
        std::cout << "Address: 0x" << std::hex << address << std::dec << "\n";
    }

    std::cout << "\n";
}

//=============================================================================
// Main Example Runner
//=============================================================================

int main() {
    std::cout << "========== Re_Kenshi Pattern Database Examples ==========\n\n";

    Example_BasicRetrieval();
    Example_CategoryRetrieval();
    Example_ListCategories();
    Example_PatternScanning();
    Example_CustomPatterns();
    Example_WorldPatterns();
    Example_CombatPatterns();
    Example_PatternCheck();
    Example_DatabaseOverview();
    Example_PracticalUsage();
    Example_VersionTracking();
    Example_ScanHelper();

    std::cout << "========================================\n";

    return 0;
}

/**
 * Pattern Coordinator Example for Re_Kenshi Plugin
 *
 * This file demonstrates the three-tier intelligent pattern management system:
 * - PatternResolver: Finds and resolves patterns automatically
 * - PatternInterpreter: Interprets resolved patterns and reads game structures
 * - PatternCoordinator: Orchestrates everything and does all the heavy lifting
 *
 * This is the EASIEST and RECOMMENDED way to use the Re_Kenshi pattern system!
 */

#include "../include/PatternCoordinator.h"
#include "../include/Logger.h"
#include "../include/Utilities.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace ReKenshi;
using namespace ReKenshi::Patterns;
using namespace ReKenshi::Logging;
using namespace ReKenshi::Utils;

//=============================================================================
// Example 1: Basic Pattern Coordinator Usage
//=============================================================================

void Example_BasicUsage() {
    std::cout << "=== Basic Pattern Coordinator Usage ===\n\n";

    // The PatternCoordinator is a singleton that does EVERYTHING for you!
    auto& coordinator = PatternCoordinator::GetInstance();

    // Initialize - this will:
    // 1. Initialize PatternResolver
    // 2. Initialize PatternInterpreter
    // 3. Scan and resolve all patterns
    // 4. Set up caching system
    // 5. Configure auto-updates
    LOG_INFO("Initializing Pattern Coordinator...");

    if (!coordinator.Initialize()) {
        LOG_ERROR("Failed to initialize Pattern Coordinator!");
        return;
    }

    LOG_INFO("Pattern Coordinator initialized successfully!");
    LOG_INFO_F("Resolved %d/%d patterns",
               coordinator.GetResolvedPatternCount(),
               coordinator.GetTotalPatternCount());

    std::cout << "\n";
}

//=============================================================================
// Example 2: Reading Game Data (Easy Mode!)
//=============================================================================

void Example_ReadingGameData() {
    std::cout << "=== Reading Game Data ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    // Reading player character data is super easy!
    Kenshi::CharacterData playerData;
    if (coordinator.GetCharacterData(PatternNames::PLAYER_CHARACTER, playerData)) {
        LOG_INFO("Player Character Data:");
        LOG_INFO_F("  Name: %s", playerData.name);
        LOG_INFO_F("  Health: %.1f / %.1f (%.1f%%)",
                   playerData.health,
                   playerData.maxHealth,
                   (playerData.health / playerData.maxHealth) * 100.0f);
        LOG_INFO_F("  Position: (%.1f, %.1f, %.1f)",
                   playerData.position.x,
                   playerData.position.y,
                   playerData.position.z);
        LOG_INFO_F("  Alive: %s", playerData.isAlive ? "Yes" : "No");
    } else {
        LOG_WARNING("Failed to read player character data");
    }

    std::cout << "\n";

    // Reading world state is just as easy!
    Kenshi::WorldStateData worldState;
    if (coordinator.GetWorldState(worldState)) {
        int hours = static_cast<int>(worldState.currentTime);
        int minutes = static_cast<int>((worldState.currentTime - hours) * 60);

        LOG_INFO("World State Data:");
        LOG_INFO_F("  Current Day: %d", worldState.currentDay);
        LOG_INFO_F("  Current Time: %02d:%02d", hours, minutes);
    } else {
        LOG_WARNING("Failed to read world state");
    }

    std::cout << "\n";
}

//=============================================================================
// Example 3: Pattern Subscriptions (Get notified of changes!)
//=============================================================================

void Example_PatternSubscriptions() {
    std::cout << "=== Pattern Subscriptions ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    LOG_INFO("Setting up pattern update subscriptions...");

    // Subscribe to player character updates
    coordinator.SubscribeToPattern(PatternNames::PLAYER_CHARACTER,
        [](const std::string& patternName, const InterpretedData& data) {
            LOG_INFO_F("[SUBSCRIPTION] Player character updated!");
            LOG_DEBUG_F("  Pattern: %s", patternName.c_str());
            LOG_DEBUG_F("  Data type: %s", data.dataType.c_str());
        });

    // Subscribe to world state updates
    coordinator.SubscribeToPattern(PatternNames::WORLD_STATE,
        [](const std::string& patternName, const InterpretedData& data) {
            LOG_INFO_F("[SUBSCRIPTION] World state updated!");
        });

    LOG_INFO("Subscriptions configured!");
    LOG_INFO("The coordinator will automatically notify you when patterns are updated");

    std::cout << "\n";
}

//=============================================================================
// Example 4: Auto-Update System
//=============================================================================

void Example_AutoUpdate() {
    std::cout << "=== Auto-Update System ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    // Enable auto-updates (enabled by default)
    coordinator.EnableAutoUpdate(true);

    // Configure update rate
    coordinator.SetUpdateRate(10.0f);  // 10 Hz (update every 100ms)

    LOG_INFO("Auto-update enabled at 10 Hz");
    LOG_INFO("Monitoring for 5 seconds...");

    // Simulate game loop
    for (int i = 0; i < 50; i++) {  // 5 seconds at 100ms intervals
        float deltaTime = 0.1f;  // 100ms

        // Just call Update() and the coordinator does everything!
        coordinator.Update(deltaTime);

        TimeUtils::SleepMs(100);
    }

    LOG_INFO("Auto-update monitoring complete");

    std::cout << "\n";
}

//=============================================================================
// Example 5: Cache Management
//=============================================================================

void Example_CacheManagement() {
    std::cout << "=== Cache Management ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    // Configure cache TTL (time-to-live)
    coordinator.SetCacheTTL(5000);  // 5 seconds
    LOG_INFO("Cache TTL set to 5 seconds");

    // The coordinator automatically caches interpreted data
    // You can access it without re-reading from memory each time
    LOG_INFO("First access (cache miss - reads from memory):");
    Kenshi::CharacterData playerData1;
    if (coordinator.GetCharacterData(PatternNames::PLAYER_CHARACTER, playerData1)) {
        LOG_INFO_F("  Player health: %.1f", playerData1.health);
    }

    LOG_INFO("Second access (cache hit - instant!):");
    Kenshi::CharacterData playerData2;
    if (coordinator.GetCharacterData(PatternNames::PLAYER_CHARACTER, playerData2)) {
        LOG_INFO_F("  Player health: %.1f", playerData2.health);
    }

    // Clear cache manually if needed
    coordinator.ClearCache();
    LOG_INFO("Cache cleared");

    std::cout << "\n";
}

//=============================================================================
// Example 6: Direct Address Access
//=============================================================================

void Example_DirectAccess() {
    std::cout << "=== Direct Address Access ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    // Sometimes you need the raw resolved address
    LOG_INFO("Getting resolved addresses:");

    if (coordinator.IsPatternResolved(PatternNames::GAME_WORLD)) {
        uintptr_t gameWorldAddr = coordinator.GetResolvedAddress(PatternNames::GAME_WORLD);
        LOG_INFO_F("  Game World: 0x%llX", gameWorldAddr);
    }

    if (coordinator.IsPatternResolved(PatternNames::PLAYER_CHARACTER)) {
        uintptr_t playerAddr = coordinator.GetResolvedAddress(PatternNames::PLAYER_CHARACTER);
        LOG_INFO_F("  Player Character: 0x%llX", playerAddr);
    }

    if (coordinator.IsPatternResolved(PatternNames::CHARACTER_LIST)) {
        uintptr_t charListAddr = coordinator.GetResolvedAddress(PatternNames::CHARACTER_LIST);
        LOG_INFO_F("  Character List: 0x%llX", charListAddr);
    }

    std::cout << "\n";
}

//=============================================================================
// Example 7: Error Handling and Diagnostics
//=============================================================================

void Example_ErrorHandling() {
    std::cout << "=== Error Handling and Diagnostics ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    // Check if a pattern was resolved
    if (!coordinator.IsPatternResolved("NonExistentPattern")) {
        LOG_WARNING("Pattern 'NonExistentPattern' was not resolved (expected)");
    }

    // Try to access invalid pattern
    Kenshi::CharacterData dummyData;
    if (!coordinator.GetCharacterData("InvalidPattern", dummyData)) {
        LOG_WARNING("Failed to get data for 'InvalidPattern' (expected)");
    }

    // Print comprehensive diagnostics
    LOG_INFO("Printing full diagnostics:");
    coordinator.PrintFullDiagnostics();

    std::cout << "\n";
}

//=============================================================================
// Example 8: Performance Monitoring
//=============================================================================

void Example_Performance() {
    std::cout << "=== Performance Monitoring ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    LOG_INFO("Running performance test (1000 reads)...");

    uint64_t startTime = TimeUtils::GetCurrentTimestampMs();

    // Perform 1000 reads (will be mostly cache hits)
    for (int i = 0; i < 1000; i++) {
        Kenshi::CharacterData playerData;
        coordinator.GetCharacterData(PatternNames::PLAYER_CHARACTER, playerData);
    }

    double elapsedMs = TimeUtils::GetElapsedMs(startTime);

    LOG_INFO_F("1000 reads completed in %.2f ms", elapsedMs);
    LOG_INFO_F("Average per read: %.4f ms", elapsedMs / 1000.0);
    LOG_INFO_F("Reads per second: %.0f", 1000.0 / (elapsedMs / 1000.0));

    std::cout << "\n";
}

//=============================================================================
// Example 9: Practical Game Loop Integration
//=============================================================================

class GameIntegrationExample {
public:
    void Run() {
        std::cout << "=== Practical Game Loop Integration ===\n\n";

        LOG_INFO("Initializing game systems...");

        auto& coordinator = PatternCoordinator::GetInstance();

        // Initialize coordinator
        if (!coordinator.Initialize()) {
            LOG_ERROR("Failed to initialize coordinator");
            return;
        }

        // Enable auto-update
        coordinator.EnableAutoUpdate(true);
        coordinator.SetUpdateRate(10.0f);  // 10 Hz

        // Set up subscriptions
        SetupSubscriptions();

        LOG_INFO("Running game loop for 10 seconds...");

        // Simulate game loop
        float totalTime = 0.0f;
        const float targetFrameTime = 0.016f;  // ~60 FPS

        while (totalTime < 10.0f) {
            // Update coordinator (handles all pattern updates automatically)
            coordinator.Update(targetFrameTime);

            // Your game logic here
            ProcessGameLogic();

            // Simulate frame timing
            TimeUtils::SleepMs(16);
            totalTime += targetFrameTime;
        }

        LOG_INFO("Game loop complete");

        std::cout << "\n";
    }

private:
    void SetupSubscriptions() {
        auto& coordinator = PatternCoordinator::GetInstance();

        // React to player health changes
        coordinator.SubscribeToPattern(PatternNames::PLAYER_CHARACTER,
            [this](const std::string& patternName, const InterpretedData& data) {
                m_playerUpdateCount++;
                if (m_playerUpdateCount % 10 == 0) {
                    LOG_DEBUG_F("Player update count: %d", m_playerUpdateCount);
                }
            });

        // React to world state changes
        coordinator.SubscribeToPattern(PatternNames::WORLD_STATE,
            [this](const std::string& patternName, const InterpretedData& data) {
                m_worldUpdateCount++;
                if (m_worldUpdateCount % 10 == 0) {
                    LOG_DEBUG_F("World update count: %d", m_worldUpdateCount);
                }
            });
    }

    void ProcessGameLogic() {
        // Your game logic here
        // The coordinator automatically keeps data fresh!
    }

    int m_playerUpdateCount = 0;
    int m_worldUpdateCount = 0;
};

void Example_GameIntegration() {
    GameIntegrationExample example;
    example.Run();
}

//=============================================================================
// Example 10: Configuration Options
//=============================================================================

void Example_Configuration() {
    std::cout << "=== Configuration Options ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    LOG_INFO("Available configuration options:");

    // Update rate
    LOG_INFO("  SetUpdateRate(float hz) - Set auto-update frequency");
    coordinator.SetUpdateRate(20.0f);  // 20 Hz
    LOG_INFO("    Current: 20 Hz");

    // Cache TTL
    LOG_INFO("  SetCacheTTL(uint64_t ms) - Set cache time-to-live");
    coordinator.SetCacheTTL(10000);  // 10 seconds
    LOG_INFO("    Current: 10000 ms");

    // Enable/disable auto-update
    LOG_INFO("  EnableAutoUpdate(bool enable) - Enable/disable auto-updates");
    coordinator.EnableAutoUpdate(true);
    LOG_INFO("    Current: Enabled");

    // Clear cache
    LOG_INFO("  ClearCache() - Clear all cached data");

    std::cout << "\n";
}

//=============================================================================
// Example 11: Advanced Pattern Access
//=============================================================================

void Example_AdvancedAccess() {
    std::cout << "=== Advanced Pattern Access ===\n\n";

    auto& coordinator = PatternCoordinator::GetInstance();

    LOG_INFO("Accessing all resolved patterns:");

    // Get all available patterns
    auto& db = PatternDatabase::GetInstance();
    auto categories = db.GetCategories();

    int totalResolved = 0;
    int totalPatterns = 0;

    for (const auto& category : categories) {
        auto patterns = db.GetPatternsByCategory(category);

        for (const auto* pattern : patterns) {
            totalPatterns++;

            if (coordinator.IsPatternResolved(pattern->name)) {
                totalResolved++;
                uintptr_t addr = coordinator.GetResolvedAddress(pattern->name);
                LOG_DEBUG_F("  [✓] %s: 0x%llX", pattern->name.c_str(), addr);
            } else {
                LOG_DEBUG_F("  [✗] %s: Not resolved", pattern->name.c_str());
            }
        }
    }

    LOG_INFO_F("Resolved %d/%d patterns (%.1f%%)",
               totalResolved,
               totalPatterns,
               (totalResolved * 100.0f) / totalPatterns);

    std::cout << "\n";
}

//=============================================================================
// Main Example Runner
//=============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Re_Kenshi Pattern Coordinator Examples             ║\n";
    std::cout << "║                                                        ║\n";
    std::cout << "║  The EASIEST way to use Re_Kenshi pattern system!     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    // Initialize logger
    auto& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::Info);
    logger.SetOutputTargets(LogOutput::Console);
    logger.EnableTimestamps(true);

    // Run all examples
    Example_BasicUsage();
    Example_ReadingGameData();
    Example_PatternSubscriptions();
    Example_AutoUpdate();
    Example_CacheManagement();
    Example_DirectAccess();
    Example_ErrorHandling();
    Example_Performance();
    Example_GameIntegration();
    Example_Configuration();
    Example_AdvancedAccess();

    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║              All Examples Complete!                    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";

    return 0;
}

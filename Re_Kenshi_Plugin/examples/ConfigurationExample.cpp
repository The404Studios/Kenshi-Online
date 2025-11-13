/**
 * Configuration System Example for Re_Kenshi Plugin
 *
 * This file demonstrates how to use the configuration system.
 */

#include "../include/Configuration.h"
#include <iostream>

using namespace ReKenshi::Config;

//=============================================================================
// Example 1: Basic Configuration Loading
//=============================================================================

void Example_BasicLoading() {
    auto& config = Configuration::GetInstance();

    // Load from default file (re_kenshi_config.json)
    if (config.LoadFromFile()) {
        std::cout << "Configuration loaded successfully!" << std::endl;
    } else {
        std::cout << "Failed to load configuration, using defaults" << std::endl;
    }

    // Access configuration values
    std::cout << "IPC Pipe Name: " << config.IPC().pipeName << std::endl;
    std::cout << "Sync Rate: " << config.Multiplayer().syncRate << " Hz" << std::endl;
    std::cout << "Performance Profiling: "
              << (config.Performance().enabled ? "Enabled" : "Disabled") << std::endl;
}

//=============================================================================
// Example 2: Modifying Configuration
//=============================================================================

void Example_ModifyingConfig() {
    auto& config = Configuration::GetInstance();

    // Modify IPC settings
    config.IPC().pipeName = "MyCustomPipe";
    config.IPC().connectTimeout = 10000;  // 10 seconds

    // Modify multiplayer settings
    config.Multiplayer().syncRate = 20.0f;  // 20 Hz
    config.Multiplayer().positionThreshold = 0.25f;  // More sensitive

    // Enable performance profiling
    config.Performance().enabled = true;
    config.Performance().reportInterval = 30;  // Every 30 seconds

    // Save modified configuration
    if (config.SaveToFile("my_custom_config.json")) {
        std::cout << "Configuration saved successfully!" << std::endl;
    }
}

//=============================================================================
// Example 3: Configuration Validation
//=============================================================================

void Example_Validation() {
    auto& config = Configuration::GetInstance();

    // Modify with invalid values
    config.Multiplayer().syncRate = -5.0f;  // Invalid!
    config.Multiplayer().maxPlayers = 1000;  // Too many!

    if (!config.Validate()) {
        std::cout << "Configuration validation failed!" << std::endl;
        std::cout << "Errors: " << config.GetValidationErrors() << std::endl;

        // Reset to defaults
        config.LoadDefaults();
    } else {
        std::cout << "Configuration is valid!" << std::endl;
    }
}

//=============================================================================
// Example 4: Using Configuration Guards
//=============================================================================

void Example_ConfigurationGuard() {
    // ConfigurationGuard automatically loads on construction
    // and saves on destruction
    {
        ConfigurationGuard guard("game_session_config.json");

        // Use configuration normally
        auto& config = Configuration::GetInstance();
        config.Debug().logLevel = DebugConfig::LogLevel::Debug;

        std::cout << "Inside scope - config will auto-save on exit" << std::endl;

    } // Config automatically saved here

    std::cout << "Config has been saved automatically!" << std::endl;
}

//=============================================================================
// Example 5: Debug Configuration
//=============================================================================

void Example_DebugConfig() {
    auto& config = Configuration::GetInstance();

    // Set debug level
    config.Debug().logLevel = DebugConfig::LogLevel::Trace;
    config.Debug().outputToDebugString = true;
    config.Debug().outputToFile = true;
    config.Debug().logFilePath = "debug_session.log";

    // Enable diagnostics
    config.Debug().printDiagnostics = true;
    config.Debug().diagnosticInterval = 5;  // Every 5 seconds

    std::cout << "Debug configuration set!" << std::endl;
}

//=============================================================================
// Example 6: Multiplayer Configuration Presets
//=============================================================================

void ApplyLowLatencyPreset(Configuration& config) {
    config.Multiplayer().syncRate = 30.0f;  // 30 Hz - higher frequency
    config.Multiplayer().positionThreshold = 0.1f;  // Very sensitive
    config.Multiplayer().healthThreshold = 0.5f;
    config.Multiplayer().syncPosition = true;
    config.Multiplayer().syncRotation = true;
    config.Multiplayer().syncHealth = true;

    config.Events().pollRate = 30.0f;  // Match sync rate

    std::cout << "Applied LOW LATENCY preset" << std::endl;
}

void ApplyBalancedPreset(Configuration& config) {
    config.Multiplayer().syncRate = 15.0f;  // 15 Hz - balanced
    config.Multiplayer().positionThreshold = 0.5f;
    config.Multiplayer().healthThreshold = 1.0f;
    config.Multiplayer().syncPosition = true;
    config.Multiplayer().syncRotation = true;
    config.Multiplayer().syncHealth = true;

    config.Events().pollRate = 15.0f;

    std::cout << "Applied BALANCED preset" << std::endl;
}

void ApplyLowBandwidthPreset(Configuration& config) {
    config.Multiplayer().syncRate = 5.0f;  // 5 Hz - lower frequency
    config.Multiplayer().positionThreshold = 1.0f;  // Less sensitive
    config.Multiplayer().healthThreshold = 2.0f;
    config.Multiplayer().syncPosition = true;
    config.Multiplayer().syncRotation = false;  // Disable rotation sync
    config.Multiplayer().syncHealth = true;
    config.Multiplayer().syncInventory = false;
    config.Multiplayer().syncStats = false;

    config.Events().pollRate = 5.0f;

    std::cout << "Applied LOW BANDWIDTH preset" << std::endl;
}

void Example_Presets() {
    auto& config = Configuration::GetInstance();

    // Apply preset based on user preference
    std::string preset = "balanced";  // Could come from user input

    if (preset == "low_latency") {
        ApplyLowLatencyPreset(config);
    } else if (preset == "balanced") {
        ApplyBalancedPreset(config);
    } else if (preset == "low_bandwidth") {
        ApplyLowBandwidthPreset(config);
    }

    config.SaveToFile();
}

//=============================================================================
// Example 7: Runtime Configuration Adjustment
//=============================================================================

class AdaptiveConfigManager {
public:
    void AdjustBasedOnPerformance(float currentFPS, float targetFPS) {
        auto& config = Configuration::GetInstance();

        if (currentFPS < targetFPS * 0.8f) {
            // Performance issues - reduce load
            std::cout << "Low FPS detected, reducing sync rate" << std::endl;

            float newRate = config.Multiplayer().syncRate * 0.8f;
            if (newRate < 5.0f) newRate = 5.0f;  // Minimum 5 Hz

            config.Multiplayer().syncRate = newRate;
            config.Events().pollRate = newRate;
        } else if (currentFPS > targetFPS * 1.2f) {
            // Good performance - can increase quality
            std::cout << "High FPS detected, increasing sync rate" << std::endl;

            float newRate = config.Multiplayer().syncRate * 1.1f;
            if (newRate > 30.0f) newRate = 30.0f;  // Maximum 30 Hz

            config.Multiplayer().syncRate = newRate;
            config.Events().pollRate = newRate;
        }
    }

    void AdjustBasedOnPlayerCount(int playerCount) {
        auto& config = Configuration::GetInstance();

        if (playerCount > 32) {
            // Many players - reduce sync rate
            std::cout << "High player count, optimizing bandwidth" << std::endl;
            ApplyLowBandwidthPreset(config);
        } else if (playerCount < 8) {
            // Few players - can use higher quality
            std::cout << "Low player count, using high quality sync" << std::endl;
            ApplyLowLatencyPreset(config);
        }
    }
};

void Example_AdaptiveConfiguration() {
    AdaptiveConfigManager manager;

    // Simulate performance monitoring
    float currentFPS = 45.0f;
    float targetFPS = 60.0f;
    manager.AdjustBasedOnPerformance(currentFPS, targetFPS);

    // Simulate player count changes
    int playerCount = 48;
    manager.AdjustBasedOnPlayerCount(playerCount);
}

//=============================================================================
// Example 8: Configuration for Different Scenarios
//=============================================================================

void ConfigureForSinglePlayer(Configuration& config) {
    // Disable multiplayer features
    config.Multiplayer().syncRate = 0.0f;  // Will be clamped to minimum
    config.Events().trackCharacterEvents = true;
    config.Events().trackWorldEvents = true;
    config.Events().trackCombatEvents = true;

    // Enable performance monitoring for single player
    config.Performance().enabled = true;

    std::cout << "Configured for SINGLE PLAYER" << std::endl;
}

void ConfigureForLANMultiplayer(Configuration& config) {
    // LAN has low latency, use high quality settings
    ApplyLowLatencyPreset(config);

    config.IPC().connectTimeout = 2000;  // Shorter timeout for LAN
    config.IPC().reconnectInterval = 3000;

    std::cout << "Configured for LAN MULTIPLAYER" << std::endl;
}

void ConfigureForInternetMultiplayer(Configuration& config) {
    // Internet has higher latency, use balanced settings
    ApplyBalancedPreset(config);

    config.IPC().connectTimeout = 10000;  // Longer timeout for internet
    config.IPC().reconnectInterval = 10000;

    std::cout << "Configured for INTERNET MULTIPLAYER" << std::endl;
}

void Example_ScenarioConfigs() {
    auto& config = Configuration::GetInstance();

    // Choose scenario
    std::string scenario = "internet";

    if (scenario == "single_player") {
        ConfigureForSinglePlayer(config);
    } else if (scenario == "lan") {
        ConfigureForLANMultiplayer(config);
    } else if (scenario == "internet") {
        ConfigureForInternetMultiplayer(config);
    }

    config.SaveToFile();
}

//=============================================================================
// Main Example Runner
//=============================================================================

void RunAllExamples() {
    std::cout << "========== Re_Kenshi Configuration Examples ==========\n\n";

    std::cout << "Example 1: Basic Loading\n";
    Example_BasicLoading();
    std::cout << "\n";

    std::cout << "Example 2: Modifying Configuration\n";
    Example_ModifyingConfig();
    std::cout << "\n";

    std::cout << "Example 3: Validation\n";
    Example_Validation();
    std::cout << "\n";

    std::cout << "Example 4: Configuration Guards\n";
    Example_ConfigurationGuard();
    std::cout << "\n";

    std::cout << "Example 5: Debug Configuration\n";
    Example_DebugConfig();
    std::cout << "\n";

    std::cout << "Example 6: Presets\n";
    Example_Presets();
    std::cout << "\n";

    std::cout << "Example 7: Adaptive Configuration\n";
    Example_AdaptiveConfiguration();
    std::cout << "\n";

    std::cout << "Example 8: Scenario Configs\n";
    Example_ScenarioConfigs();
    std::cout << "\n";

    std::cout << "========================================\n";
}

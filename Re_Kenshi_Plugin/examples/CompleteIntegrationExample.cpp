/**
 * Complete Integration Example for Re_Kenshi Plugin
 *
 * This file demonstrates how all systems work together in a real application.
 * This is the closest example to actual plugin usage.
 */

#include "../include/Re_Kenshi_Plugin.h"
#include "../include/GameStateManager.h"
#include "../include/Logger.h"
#include "../include/Configuration.h"
#include "../include/PerformanceProfiler.h"
#include "../include/GameEventManager.h"
#include "../include/MultiplayerSyncManager.h"
#include "../include/Utilities.h"
#include "../include/PatternDatabase.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace ReKenshi;
using namespace ReKenshi::Logging;
using namespace ReKenshi::Config;
using namespace ReKenshi::Performance;
using namespace ReKenshi::Events;
using namespace ReKenshi::Multiplayer;
using namespace ReKenshi::Utils;

//=============================================================================
// Example 1: Complete Plugin Initialization
//=============================================================================

class PluginInitializer {
public:
    bool Initialize() {
        LOG_FUNCTION();

        LOG_INFO("========================================");
        LOG_INFO("Re_Kenshi Plugin - Initialization");
        LOG_INFO("========================================");

        // Step 1: Configure Logging
        if (!InitializeLogging()) {
            return false;
        }

        // Step 2: Load Configuration
        if (!LoadConfiguration()) {
            return false;
        }

        // Step 3: Initialize Performance Profiling
        InitializeProfiling();

        // Step 4: Initialize Game State Manager
        if (!InitializeGameState()) {
            return false;
        }

        // Step 5: Initialize Event System
        if (!InitializeEventSystem()) {
            return false;
        }

        // Step 6: Initialize Multiplayer
        if (!InitializeMultiplayer()) {
            return false;
        }

        LOG_INFO("========================================");
        LOG_INFO("Plugin initialization complete!");
        LOG_INFO("========================================");

        return true;
    }

private:
    bool InitializeLogging() {
        PROFILE_SCOPE("InitializeLogging");

        LOG_INFO("Initializing logging system...");

        auto& logger = Logger::GetInstance();
        auto& config = Configuration::GetInstance();

        // Configure from config
        logger.SetLogLevel(static_cast<LogLevel>(config.Debug().logLevel));

        LogOutput output = LogOutput::None;
        if (config.Debug().outputToDebugString) {
            output = output | LogOutput::DebugString;
        }
        if (config.Debug().outputToFile) {
            output = output | LogOutput::File;
        }
        logger.SetOutputTargets(output);

        if (config.Debug().outputToFile) {
            logger.SetLogFile(config.Debug().logFilePath);
        }

        logger.EnableTimestamps(true);
        logger.EnableThreadIds(false);

        LOG_INFO("Logging system initialized");
        return true;
    }

    bool LoadConfiguration() {
        PROFILE_SCOPE("LoadConfiguration");

        LOG_INFO("Loading configuration...");

        auto& config = Configuration::GetInstance();

        if (config.LoadFromFile()) {
            LOG_INFO("Configuration loaded from file");
        } else {
            LOG_WARNING("Using default configuration");
        }

        if (!config.Validate()) {
            LOG_ERROR_F("Configuration validation failed: %s",
                        config.GetValidationErrors().c_str());
            return false;
        }

        LOG_INFO("Configuration validated successfully");
        return true;
    }

    void InitializeProfiling() {
        PROFILE_SCOPE("InitializeProfiling");

        LOG_INFO("Initializing performance profiling...");

        auto& config = Configuration::GetInstance();
        auto& profiler = PerformanceProfiler::GetInstance();

        profiler.SetEnabled(config.Performance().enabled);

        if (config.Performance().enabled) {
            LOG_INFO("Performance profiling enabled");
        } else {
            LOG_DEBUG("Performance profiling disabled");
        }
    }

    bool InitializeGameState() {
        PROFILE_SCOPE("InitializeGameState");

        LOG_INFO("Initializing game state manager...");

        auto& gsm = GameStateManager::GetInstance();

        if (!gsm.Initialize()) {
            LOG_ERROR("Failed to initialize game state manager");
            return false;
        }

        // Print diagnostics
        gsm.PrintDiagnostics();

        LOG_INFO("Game state manager initialized");
        return true;
    }

    bool InitializeEventSystem() {
        PROFILE_SCOPE("InitializeEventSystem");

        LOG_INFO("Initializing event system...");

        auto& config = Configuration::GetInstance();
        auto& gsm = GameStateManager::GetInstance();

        // Create event manager
        m_eventManager = std::make_unique<GameEventManager>();

        uintptr_t worldPtr = gsm.GetGameWorldPtr();
        uintptr_t characterListPtr = gsm.GetCharacterListPtr();

        m_eventManager->Initialize(worldPtr, characterListPtr);
        m_eventManager->SetPollRate(config.Events().pollRate);

        // Subscribe to events
        SetupEventHandlers();

        LOG_INFO("Event system initialized");
        return true;
    }

    void SetupEventHandlers() {
        LOG_DEBUG("Setting up event handlers...");

        // Character events
        m_eventManager->Subscribe(GameEventType::CharacterDamaged,
            [](const GameEvent& evt) {
                const auto& damageEvt = static_cast<const CharacterDamageEvent&>(evt);
                LOG_INFO_F("[EVENT] %s took %.1f damage (%.1f HP remaining)",
                           damageEvt.characterName.c_str(),
                           damageEvt.damageAmount,
                           damageEvt.healthAfter);
            });

        m_eventManager->Subscribe(GameEventType::CharacterMoved,
            [](const GameEvent& evt) {
                const auto& moveEvt = static_cast<const CharacterMovementEvent&>(evt);
                LOG_TRACE_F("[EVENT] %s moved %.1f units",
                            moveEvt.characterName.c_str(),
                            moveEvt.distance);
            });

        m_eventManager->Subscribe(GameEventType::CharacterDied,
            [](const GameEvent& evt) {
                const auto& deathEvt = static_cast<const CharacterEvent&>(evt);
                LOG_WARNING_F("[EVENT] %s has died!", deathEvt.characterName.c_str());
            });

        m_eventManager->Subscribe(GameEventType::DayChanged,
            [](const GameEvent& evt) {
                const auto& dayEvt = static_cast<const DayChangeEvent&>(evt);
                LOG_INFO_F("[EVENT] Day changed: %d -> %d", dayEvt.oldDay, dayEvt.newDay);
            });

        LOG_DEBUG("Event handlers configured");
    }

    bool InitializeMultiplayer() {
        PROFILE_SCOPE("InitializeMultiplayer");

        LOG_INFO("Initializing multiplayer system...");

        auto& config = Configuration::GetInstance();
        auto& gsm = GameStateManager::GetInstance();

        // Create multiplayer sync manager
        m_syncManager = std::make_unique<MultiplayerSyncManager>();

        // Note: In real usage, would pass actual IPC client
        // m_syncManager->Initialize(ipcClient, m_eventManager.get(), gsm.GetPlayerCharacterPtr());

        m_syncManager->SetSyncRate(config.Multiplayer().syncRate);

        SyncFlags flags = SyncFlags::None;
        if (config.Multiplayer().syncPosition) flags = flags | SyncFlags::Position;
        if (config.Multiplayer().syncRotation) flags = flags | SyncFlags::Rotation;
        if (config.Multiplayer().syncHealth) flags = flags | SyncFlags::Health;

        m_syncManager->SetSyncFlags(flags);

        LOG_INFO("Multiplayer system initialized");
        return true;
    }

    std::unique_ptr<GameEventManager> m_eventManager;
    std::unique_ptr<MultiplayerSyncManager> m_syncManager;
};

//=============================================================================
// Example 2: Main Game Loop Integration
//=============================================================================

class GameLoopSimulator {
public:
    void Run(int frameCount) {
        LOG_INFO_F("Starting game loop simulation (%d frames)", frameCount);

        FrameTimeTracker frameTracker;
        auto& gsm = GameStateManager::GetInstance();

        for (int frame = 0; frame < frameCount; frame++) {
            frameTracker.BeginFrame();
            {
                PROFILE_SCOPE("GameLoop");

                float deltaTime = 0.016f;  // ~60 FPS

                // Update game state manager
                gsm.Update(deltaTime);

                // Simulate game events occasionally
                if (frame % 100 == 0) {
                    SimulateGameEvent(frame);
                }

                // Print status every 60 frames
                if (frame % 60 == 0 && frame > 0) {
                    PrintFrameStatus(frame, frameTracker);
                }
            }
            frameTracker.EndFrame();

            // Simulate frame timing
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        LOG_INFO("Game loop simulation complete");
        PrintFinalStatistics(frameTracker);
    }

private:
    void SimulateGameEvent(int frame) {
        LOG_DEBUG_F("Simulating game event at frame %d", frame);
        // In real usage, events would be detected by GameEventManager
    }

    void PrintFrameStatus(int frame, const FrameTimeTracker& tracker) {
        LOG_INFO("========================================");
        LOG_INFO_F("Frame %d Status", frame);
        LOG_INFO("========================================");
        LOG_INFO_F("FPS: %.1f", tracker.GetCurrentFPS());
        LOG_INFO_F("Avg Frame Time: %.2f ms", tracker.GetAverageFrameTime());

        // Memory info
        auto memStats = MemoryTracker::GetCurrentMemoryUsage();
        LOG_INFO_F("Memory: %s", MemoryTracker::FormatBytes(memStats.workingSetSize).c_str());

        // Game state info
        auto& gsm = GameStateManager::GetInstance();
        const auto& stats = gsm.GetStatistics();
        LOG_INFO_F("Memory Reads: %u (Failed: %u)", stats.memoryReads, stats.failedReads);
        LOG_INFO("========================================");
    }

    void PrintFinalStatistics(const FrameTimeTracker& tracker) {
        LOG_INFO("");
        LOG_INFO("========================================");
        LOG_INFO("Final Statistics");
        LOG_INFO("========================================");
        LOG_INFO_F("Average FPS: %.1f", tracker.GetAverageFPS());
        LOG_INFO_F("Min FPS: %.1f", tracker.GetMinFPS());
        LOG_INFO_F("Max FPS: %.1f", tracker.GetMaxFPS());
        LOG_INFO_F("Total Frames: %llu", tracker.GetTotalFrames());

        // Performance profiler report
        auto& profiler = PerformanceProfiler::GetInstance();
        if (profiler.IsEnabled()) {
            LOG_INFO("");
            LOG_INFO("Performance Profile:");
            profiler.PrintReport();
        }

        LOG_INFO("========================================");
    }
};

//=============================================================================
// Example 3: Player State Monitoring
//=============================================================================

class PlayerStateMonitor {
public:
    void MonitorPlayer(int duration) {
        LOG_INFO_F("Monitoring player state for %d seconds...", duration);

        auto& gsm = GameStateManager::GetInstance();
        int elapsed = 0;

        while (elapsed < duration) {
            Kenshi::CharacterData playerData;
            if (gsm.ReadPlayerCharacter(playerData)) {
                PrintPlayerStatus(playerData);
            } else {
                LOG_WARNING("Failed to read player data");
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
            elapsed++;
        }

        LOG_INFO("Player monitoring complete");
    }

private:
    void PrintPlayerStatus(const Kenshi::CharacterData& data) {
        // Health status
        float healthPercent = (data.health / data.maxHealth) * 100.0f;
        std::string healthStatus = GetHealthStatus(healthPercent);

        LOG_INFO_F("[PLAYER] %s - Health: %.1f%% (%s)",
                   data.name,
                   healthPercent,
                   healthStatus.c_str());

        // Position
        LOG_DEBUG_F("[PLAYER] Position: (%.1f, %.1f, %.1f)",
                    data.position.x,
                    data.position.y,
                    data.position.z);

        // Status flags
        if (!data.isAlive) {
            LOG_ERROR("[PLAYER] Status: DEAD");
        } else if (data.isUnconscious) {
            LOG_WARNING("[PLAYER] Status: UNCONSCIOUS");
        } else {
            LOG_TRACE("[PLAYER] Status: Active");
        }
    }

    std::string GetHealthStatus(float healthPercent) {
        if (healthPercent >= 75.0f) return "Healthy";
        if (healthPercent >= 50.0f) return "Injured";
        if (healthPercent >= 25.0f) return "Badly Injured";
        return "Critical";
    }
};

//=============================================================================
// Example 4: World State Analysis
//=============================================================================

class WorldStateAnalyzer {
public:
    void AnalyzeWorld() {
        LOG_INFO("Analyzing world state...");

        auto& gsm = GameStateManager::GetInstance();

        Kenshi::WorldStateData worldState;
        if (!gsm.ReadWorldState(worldState)) {
            LOG_ERROR("Failed to read world state");
            return;
        }

        LOG_INFO("========================================");
        LOG_INFO("World State Analysis");
        LOG_INFO("========================================");

        // Time information
        int day = worldState.currentDay;
        float time = worldState.currentTime;
        int hours = static_cast<int>(time);
        int minutes = static_cast<int>((time - hours) * 60);

        LOG_INFO_F("Current Day: %d", day);
        LOG_INFO_F("Current Time: %02d:%02d", hours, minutes);
        LOG_INFO_F("Time of Day: %s", GetTimeOfDay(time).c_str());

        // Weather would be here if available
        LOG_DEBUG("Weather: [Not implemented in current structures]");

        LOG_INFO("========================================");
    }

private:
    std::string GetTimeOfDay(float time) {
        int hour = static_cast<int>(time) % 24;

        if (hour >= 6 && hour < 12) return "Morning";
        if (hour >= 12 && hour < 18) return "Afternoon";
        if (hour >= 18 && hour < 21) return "Evening";
        return "Night";
    }
};

//=============================================================================
// Example 5: Multiplayer Session Simulation
//=============================================================================

class MultiplayerSessionSimulator {
public:
    void SimulateSession() {
        LOG_INFO("========================================");
        LOG_INFO("Multiplayer Session Simulation");
        LOG_INFO("========================================");

        // Connect to server
        ConnectToServer();

        // Add other players
        AddPlayers();

        // Simulate gameplay
        SimulateGameplay(10);  // 10 seconds

        // Disconnect
        Disconnect();

        LOG_INFO("Multiplayer session complete");
    }

private:
    void ConnectToServer() {
        LOG_INFO("Connecting to server...");
        TimeUtils::SleepMs(500);  // Simulate connection time
        LOG_INFO("Connected to server: game.example.com:7777");
    }

    void AddPlayers() {
        LOG_INFO("Adding network players...");

        std::vector<std::string> playerNames = {
            "Warrior_John",
            "Trader_Sarah",
            "Scout_Mike"
        };

        for (const auto& name : playerNames) {
            LOG_INFO_F("Player joined: %s", name.c_str());
            TimeUtils::SleepMs(100);
        }
    }

    void SimulateGameplay(int duration) {
        LOG_INFO_F("Simulating gameplay for %d seconds...", duration);

        for (int i = 0; i < duration; i++) {
            // Simulate sync operations
            if (i % 2 == 0) {
                LOG_DEBUG_F("[SYNC] Sending player update (tick %d)", i);
            }

            if (RandomUtils::RandomBool()) {
                LOG_TRACE("[SYNC] Received player update from network");
            }

            TimeUtils::SleepMs(1000);
        }
    }

    void Disconnect() {
        LOG_INFO("Disconnecting from server...");
        TimeUtils::SleepMs(200);
        LOG_INFO("Disconnected");
    }
};

//=============================================================================
// Example 6: Comprehensive Diagnostics
//=============================================================================

class DiagnosticsRunner {
public:
    void RunDiagnostics() {
        LOG_INFO("========================================");
        LOG_INFO("Running Comprehensive Diagnostics");
        LOG_INFO("========================================");

        PrintSystemInfo();
        PrintGameStateInfo();
        PrintPerformanceInfo();
        PrintConfigurationInfo();

        LOG_INFO("========================================");
        LOG_INFO("Diagnostics Complete");
        LOG_INFO("========================================");
    }

private:
    void PrintSystemInfo() {
        LOG_INFO("");
        LOG_INFO("=== System Information ===");

        LOG_INFO_F("Process ID: %u", SystemUtils::GetProcessId());
        LOG_INFO_F("Thread ID: %u", SystemUtils::GetThreadId());
        LOG_INFO_F("Computer: %s", SystemUtils::GetComputerName().c_str());
        LOG_INFO_F("Username: %s", SystemUtils::GetUsername().c_str());

        size_t totalMem = SystemUtils::GetTotalPhysicalMemory();
        size_t availMem = SystemUtils::GetAvailablePhysicalMemory();

        LOG_INFO_F("Total Memory: %s", StringUtils::FormatBytes(totalMem).c_str());
        LOG_INFO_F("Available Memory: %s", StringUtils::FormatBytes(availMem).c_str());

        std::string exePath = SystemUtils::GetExecutablePath();
        LOG_INFO_F("Executable: %s", FileUtils::GetFilename(exePath).c_str());
    }

    void PrintGameStateInfo() {
        LOG_INFO("");
        LOG_INFO("=== Game State Information ===");

        auto& gsm = GameStateManager::GetInstance();
        gsm.PrintDiagnostics();
    }

    void PrintPerformanceInfo() {
        LOG_INFO("");
        LOG_INFO("=== Performance Information ===");

        auto& profiler = PerformanceProfiler::GetInstance();

        if (profiler.IsEnabled()) {
            profiler.PrintReport();
        } else {
            LOG_INFO("Performance profiling is disabled");
        }
    }

    void PrintConfigurationInfo() {
        LOG_INFO("");
        LOG_INFO("=== Configuration Information ===");

        auto& config = Configuration::GetInstance();

        LOG_INFO_F("IPC Pipe: %s", config.IPC().pipeName.c_str());
        LOG_INFO_F("Sync Rate: %.1f Hz", config.Multiplayer().syncRate);
        LOG_INFO_F("Event Poll Rate: %.1f Hz", config.Events().pollRate);
        LOG_INFO_F("Performance Profiling: %s",
                   config.Performance().enabled ? "Enabled" : "Disabled");
    }
};

//=============================================================================
// Main Integration Example
//=============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Re_Kenshi Plugin - Complete Integration Example    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    // Initialize random for simulations
    RandomUtils::Initialize();

    // Step 1: Initialize Plugin
    PluginInitializer initializer;
    if (!initializer.Initialize()) {
        LOG_CRITICAL("Plugin initialization failed!");
        return 1;
    }

    std::cout << "\n";
    TimeUtils::SleepMs(1000);

    // Step 2: Run Diagnostics
    DiagnosticsRunner diagnostics;
    diagnostics.RunDiagnostics();

    std::cout << "\n";
    TimeUtils::SleepMs(1000);

    // Step 3: Monitor Player State
    PlayerStateMonitor playerMonitor;
    playerMonitor.MonitorPlayer(3);  // 3 seconds

    std::cout << "\n";
    TimeUtils::SleepMs(1000);

    // Step 4: Analyze World
    WorldStateAnalyzer worldAnalyzer;
    worldAnalyzer.AnalyzeWorld();

    std::cout << "\n";
    TimeUtils::SleepMs(1000);

    // Step 5: Simulate Multiplayer Session
    MultiplayerSessionSimulator mpSimulator;
    mpSimulator.SimulateSession();

    std::cout << "\n";
    TimeUtils::SleepMs(1000);

    // Step 6: Simulate Game Loop
    GameLoopSimulator gameLoop;
    gameLoop.Run(180);  // 180 frames (~3 seconds)

    std::cout << "\n";

    // Step 7: Final Diagnostics
    diagnostics.RunDiagnostics();

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║            Integration Example Complete!               ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";

    return 0;
}

/**
 * Basic Usage Example for Re_Kenshi Plugin
 *
 * This file demonstrates the most common use cases for the plugin.
 * Copy and adapt this code to your own projects.
 */

#include "../include/Re_Kenshi_Plugin.h"
#include "../include/GameEventManager.h"
#include "../include/MultiplayerSyncManager.h"
#include "../include/PerformanceProfiler.h"
#include <iostream>

using namespace ReKenshi;

//=============================================================================
// Example 1: Basic Plugin Initialization
//=============================================================================

void Example_BasicInitialization() {
    // Get plugin singleton
    auto& plugin = Plugin::GetInstance();

    // Initialize (this happens automatically in DllMain, but shown for reference)
    if (plugin.Initialize()) {
        std::cout << "Plugin initialized successfully!" << std::endl;
    }

    // Access components
    auto* eventManager = plugin.GetEventManager();
    auto* syncManager = plugin.GetSyncManager();
    auto* ipcClient = plugin.GetIPCClient();

    std::cout << "Event Manager: " << (eventManager ? "Active" : "Inactive") << std::endl;
    std::cout << "Sync Manager: " << (syncManager ? "Active" : "Inactive") << std::endl;
    std::cout << "IPC Client: " << (ipcClient && ipcClient->IsConnected() ? "Connected" : "Disconnected") << std::endl;
}

//=============================================================================
// Example 2: Subscribing to Game Events
//=============================================================================

void Example_EventSubscription() {
    auto& plugin = Plugin::GetInstance();
    auto* eventManager = plugin.GetEventManager();

    if (!eventManager) {
        std::cerr << "Event manager not available!" << std::endl;
        return;
    }

    // Subscribe to character damage events
    eventManager->Subscribe(
        Events::GameEventType::CharacterDamaged,
        [](const Events::GameEvent& evt) {
            const auto& damageEvt = static_cast<const Events::CharacterDamageEvent&>(evt);

            std::cout << "=== Character Damaged ===" << std::endl;
            std::cout << "Name: " << damageEvt.characterName << std::endl;
            std::cout << "Damage: " << damageEvt.damageAmount << std::endl;
            std::cout << "Health: " << damageEvt.healthAfter << "/"
                      << damageEvt.characterData.maxHealth << std::endl;

            if (damageEvt.attackerAddress) {
                std::cout << "Attacker: " << damageEvt.attackerName << std::endl;
            }
        }
    );

    // Subscribe to character movement
    eventManager->Subscribe(
        Events::GameEventType::CharacterMoved,
        [](const Events::GameEvent& evt) {
            const auto& moveEvt = static_cast<const Events::CharacterMovementEvent&>(evt);

            std::cout << "Character " << moveEvt.characterName
                      << " moved " << moveEvt.distance << " units" << std::endl;
        }
    );

    // Subscribe to death events
    eventManager->Subscribe(
        Events::GameEventType::CharacterDied,
        [](const Events::GameEvent& evt) {
            const auto& deathEvt = static_cast<const Events::CharacterEvent&>(evt);

            std::cout << "RIP: " << deathEvt.characterName << " has died!" << std::endl;
        }
    );

    // Subscribe to world events
    eventManager->Subscribe(
        Events::GameEventType::DayChanged,
        [](const Events::GameEvent& evt) {
            const auto& dayEvt = static_cast<const Events::DayChangeEvent&>(evt);

            std::cout << "Day changed: " << dayEvt.oldDay
                      << " -> " << dayEvt.newDay << std::endl;
        }
    );

    std::cout << "Event subscriptions set up successfully!" << std::endl;
}

//=============================================================================
// Example 3: Configuring Multiplayer Sync
//=============================================================================

void Example_MultiplayerSync() {
    auto& plugin = Plugin::GetInstance();
    auto* syncManager = plugin.GetSyncManager();

    if (!syncManager) {
        std::cerr << "Sync manager not available!" << std::endl;
        return;
    }

    // Set local player
    syncManager->SetLocalPlayer("player_12345", "MyUsername");

    // Configure sync rate (updates per second)
    syncManager->SetSyncRate(20.0f);  // 20 Hz = 50ms per update

    // Configure what data to sync
    using SyncFlags = Multiplayer::SyncFlags;
    syncManager->SetSyncFlags(
        SyncFlags::Position |
        SyncFlags::Health |
        SyncFlags::Rotation
    );

    // Add other players as they connect
    syncManager->AddNetworkPlayer("player_67890", "OtherPlayer");
    syncManager->AddNetworkPlayer("player_11111", "AnotherPlayer");

    std::cout << "Multiplayer sync configured!" << std::endl;
    std::cout << "Connected players: " << syncManager->GetPlayerCount() << std::endl;
}

//=============================================================================
// Example 4: Performance Profiling
//=============================================================================

void Example_PerformanceProfiling() {
    using namespace Performance;

    // Method 1: Manual profiling
    {
        PerformanceProfiler::GetInstance().BeginProfile("MyFunction");

        // Your code here
        for (int i = 0; i < 1000; i++) {
            // Simulate work
        }

        PerformanceProfiler::GetInstance().EndProfile("MyFunction");
    }

    // Method 2: Automatic profiling with RAII
    {
        PROFILE_SCOPE("AutomaticProfiling");

        // Your code here
        // Automatically profiled when scope exits
    }

    // Method 3: Function profiling
    auto myFunction = []() {
        PROFILE_FUNCTION();  // Profiles entire function

        // Function code here
    };
    myFunction();

    // Get statistics
    const auto* stats = PerformanceProfiler::GetInstance().GetStats("MyFunction");
    if (stats) {
        std::cout << "MyFunction statistics:" << std::endl;
        std::cout << "  Calls: " << stats->callCount << std::endl;
        std::cout << "  Average: " << stats->GetAverageTime() << " ms" << std::endl;
        std::cout << "  Min: " << stats->minTime << " ms" << std::endl;
        std::cout << "  Max: " << stats->maxTime << " ms" << std::endl;
    }

    // Print full report
    PerformanceProfiler::GetInstance().PrintReport();
}

//=============================================================================
// Example 5: Reading Game Data
//=============================================================================

void Example_ReadGameData() {
    auto& plugin = Plugin::GetInstance();

    // Get player character address (you would get this from pattern scanning)
    uintptr_t playerPtr = 0; // TODO: Get from plugin.GetPlayerPtr() or similar

    if (!playerPtr) {
        std::cerr << "Player pointer not available!" << std::endl;
        return;
    }

    // Read character data
    Kenshi::CharacterData character;
    if (Kenshi::GameDataReader::ReadCharacter(playerPtr, character)) {
        std::cout << "=== Player Data ===" << std::endl;
        std::cout << "Name: " << character.name << std::endl;
        std::cout << "Health: " << character.health << "/" << character.maxHealth << std::endl;
        std::cout << "Position: (" << character.position.x << ", "
                  << character.position.y << ", " << character.position.z << ")" << std::endl;
        std::cout << "Faction ID: " << character.factionId << std::endl;
        std::cout << "Squad ID: " << character.squadId << std::endl;
        std::cout << "Alive: " << (character.isAlive ? "Yes" : "No") << std::endl;
        std::cout << "Unconscious: " << (character.isUnconscious ? "Yes" : "No") << std::endl;
    }
}

//=============================================================================
// Example 6: Custom Event Handler Class
//=============================================================================

class MyGameSystem {
public:
    void Initialize(Events::GameEventManager* eventMgr, Multiplayer::MultiplayerSyncManager* syncMgr) {
        m_eventManager = eventMgr;
        m_syncManager = syncMgr;

        // Subscribe to events we care about
        m_eventManager->Subscribe(
            Events::GameEventType::CharacterDamaged,
            [this](const Events::GameEvent& evt) { OnCharacterDamaged(evt); }
        );

        m_eventManager->Subscribe(
            Events::GameEventType::CharacterDied,
            [this](const Events::GameEvent& evt) { OnCharacterDied(evt); }
        );
    }

    void Update(float deltaTime) {
        // Your update logic here
        m_updateTimer += deltaTime;

        if (m_updateTimer > 1.0f) {
            // Do something every second
            PrintStatistics();
            m_updateTimer = 0.0f;
        }
    }

    void Shutdown() {
        // Cleanup
        m_eventManager = nullptr;
        m_syncManager = nullptr;
    }

private:
    void OnCharacterDamaged(const Events::GameEvent& evt) {
        const auto& damageEvt = static_cast<const Events::CharacterDamageEvent&>(evt);

        m_totalDamageDealt += damageEvt.damageAmount;
        m_damageEventCount++;

        // Custom handling here
    }

    void OnCharacterDied(const Events::GameEvent& evt) {
        m_deathCount++;

        // Custom handling here
    }

    void PrintStatistics() {
        std::cout << "=== Game System Statistics ===" << std::endl;
        std::cout << "Total damage: " << m_totalDamageDealt << std::endl;
        std::cout << "Damage events: " << m_damageEventCount << std::endl;
        std::cout << "Deaths: " << m_deathCount << std::endl;
    }

    Events::GameEventManager* m_eventManager = nullptr;
    Multiplayer::MultiplayerSyncManager* m_syncManager = nullptr;

    float m_updateTimer = 0.0f;
    float m_totalDamageDealt = 0.0f;
    int m_damageEventCount = 0;
    int m_deathCount = 0;
};

//=============================================================================
// Example 7: Complete Integration Example
//=============================================================================

void Example_CompleteIntegration() {
    // 1. Get plugin and verify initialization
    auto& plugin = Plugin::GetInstance();
    if (!plugin.IsInitialized()) {
        std::cerr << "Plugin not initialized!" << std::endl;
        return;
    }

    // 2. Get all managers
    auto* eventManager = plugin.GetEventManager();
    auto* syncManager = plugin.GetSyncManager();
    auto* ipcClient = plugin.GetIPCClient();

    // 3. Configure multiplayer
    syncManager->SetLocalPlayer("my_player_id", "MyUsername");
    syncManager->SetSyncRate(15.0f);  // 15 Hz
    syncManager->SetSyncFlags(Multiplayer::SyncFlags::All);

    // 4. Set up event handlers
    MyGameSystem gameSystem;
    gameSystem.Initialize(eventManager, syncManager);

    // 5. Monitor performance
    Performance::FrameTimeTracker frameTracker;
    Performance::PerformanceProfiler::GetInstance().SetEnabled(true);

    // 6. Main game loop (simplified)
    float deltaTime = 0.016f;  // ~60 FPS
    for (int frame = 0; frame < 1000; frame++) {
        frameTracker.BeginFrame();
        {
            PROFILE_SCOPE("GameLoop");

            // Plugin update
            plugin.Update(deltaTime);

            // Your game system update
            gameSystem.Update(deltaTime);

            // Every 60 frames, print statistics
            if (frame % 60 == 0) {
                std::cout << "\n=== Frame " << frame << " ===" << std::endl;
                std::cout << "FPS: " << frameTracker.GetCurrentFPS() << std::endl;

                const auto& syncStats = syncManager->GetStats();
                std::cout << "Packets sent: " << syncStats.packetsSent << std::endl;
                std::cout << "Packets received: " << syncStats.packetsReceived << std::endl;

                auto memStats = Performance::MemoryTracker::GetCurrentMemoryUsage();
                std::cout << "Memory: " << Performance::MemoryTracker::FormatBytes(memStats.workingSetSize) << std::endl;
            }
        }
        frameTracker.EndFrame();
    }

    // 7. Cleanup
    gameSystem.Shutdown();

    // 8. Print final report
    std::cout << "\n=== Final Performance Report ===" << std::endl;
    Performance::PerformanceProfiler::GetInstance().PrintReport();
}

//=============================================================================
// Main Example Runner
//=============================================================================

void RunAllExamples() {
    std::cout << "========== Re_Kenshi Examples ==========\n\n";

    std::cout << "Example 1: Basic Initialization\n";
    Example_BasicInitialization();
    std::cout << "\n";

    std::cout << "Example 2: Event Subscription\n";
    Example_EventSubscription();
    std::cout << "\n";

    std::cout << "Example 3: Multiplayer Sync\n";
    Example_MultiplayerSync();
    std::cout << "\n";

    std::cout << "Example 4: Performance Profiling\n";
    Example_PerformanceProfiling();
    std::cout << "\n";

    std::cout << "Example 5: Reading Game Data\n";
    Example_ReadGameData();
    std::cout << "\n";

    std::cout << "Example 6 & 7: Complete Integration\n";
    Example_CompleteIntegration();
    std::cout << "\n";

    std::cout << "========================================\n";
}

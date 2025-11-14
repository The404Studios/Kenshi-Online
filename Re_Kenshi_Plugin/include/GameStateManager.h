#pragma once

#include "KenshiStructures.h"
#include "KenshiAdvancedStructures.h"
#include "PatternDatabase.h"
#include "MemoryScanner.h"
#include "Logger.h"
#include <unordered_map>
#include <string>
#include <vector>

namespace ReKenshi {

/**
 * Centralized game state manager
 * Integrates pattern scanning, memory reading, and state tracking
 */
class GameStateManager {
public:
    static GameStateManager& GetInstance();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    void Update(float deltaTime);

    // State queries
    bool IsInitialized() const { return m_initialized; }
    bool HasGameWorld() const { return m_gameWorldPtr != 0; }
    bool HasPlayer() const { return m_playerCharacterPtr != 0; }

    // Game structure access
    uintptr_t GetGameWorldPtr() const { return m_gameWorldPtr; }
    uintptr_t GetPlayerCharacterPtr() const { return m_playerCharacterPtr; }
    uintptr_t GetCharacterListPtr() const { return m_characterListPtr; }
    uintptr_t GetWorldStatePtr() const { return m_worldStatePtr; }

    // Character operations
    bool ReadPlayerCharacter(Kenshi::CharacterData& outData);
    std::vector<Kenshi::CharacterData> GetNearbyCharacters(float radius);

    // World operations
    bool ReadWorldState(Kenshi::WorldStateData& outData);
    int GetCurrentDay();
    float GetCurrentTime();

    // Building operations
    std::vector<Kenshi::BuildingData> GetNearbyBuildings(const Kenshi::Vector3& position, float radius);

    // Statistics
    struct Statistics {
        uint32_t scannedPatterns = 0;
        uint32_t foundPatterns = 0;
        uint32_t memoryReads = 0;
        uint32_t failedReads = 0;
        double initializationTime = 0.0;
    };

    const Statistics& GetStatistics() const { return m_stats; }
    void PrintDiagnostics();

private:
    GameStateManager() = default;
    ~GameStateManager() = default;
    GameStateManager(const GameStateManager&) = delete;
    GameStateManager& operator=(const GameStateManager&) = delete;

    // Initialization helpers
    bool ScanCriticalStructures();
    bool ScanAndResolve(const std::string& patternName, uintptr_t& outAddress);

    // State
    bool m_initialized = false;
    float m_updateAccumulator = 0.0f;

    // Game structure pointers
    uintptr_t m_gameWorldPtr = 0;
    uintptr_t m_playerCharacterPtr = 0;
    uintptr_t m_characterListPtr = 0;
    uintptr_t m_worldStatePtr = 0;
    uintptr_t m_squadListPtr = 0;
    uintptr_t m_factionListPtr = 0;
    uintptr_t m_buildingListPtr = 0;

    // Cache
    Kenshi::CharacterData m_cachedPlayerData;
    Kenshi::WorldStateData m_cachedWorldState;
    uint64_t m_lastPlayerUpdate = 0;
    uint64_t m_lastWorldUpdate = 0;

    // Statistics
    Statistics m_stats;
};

} // namespace ReKenshi

#include "GameStateManager.h"
#include "Utilities.h"
#include <chrono>

namespace ReKenshi {

GameStateManager& GameStateManager::GetInstance() {
    static GameStateManager instance;
    return instance;
}

bool GameStateManager::Initialize() {
    if (m_initialized) {
        LOG_WARNING("GameStateManager already initialized");
        return true;
    }

    LOG_INFO("Initializing GameStateManager...");

    auto startTime = Utils::TimeUtils::GetCurrentTimestampMs();

    // Scan for critical game structures
    if (!ScanCriticalStructures()) {
        LOG_ERROR("Failed to scan critical structures");
        return false;
    }

    m_stats.initializationTime = Utils::TimeUtils::GetElapsedMs(startTime);

    m_initialized = true;

    LOG_INFO_F("GameStateManager initialized in %.2f ms", m_stats.initializationTime);
    LOG_INFO_F("Found %d/%d patterns", m_stats.foundPatterns, m_stats.scannedPatterns);

    return true;
}

void GameStateManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down GameStateManager");

    // Clear pointers
    m_gameWorldPtr = 0;
    m_playerCharacterPtr = 0;
    m_characterListPtr = 0;
    m_worldStatePtr = 0;
    m_squadListPtr = 0;
    m_factionListPtr = 0;
    m_buildingListPtr = 0;

    m_initialized = false;
}

void GameStateManager::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    m_updateAccumulator += deltaTime;

    // Update cached data periodically (every 100ms)
    if (m_updateAccumulator >= 0.1f) {
        m_updateAccumulator = 0.0f;

        // Update player data cache
        if (m_playerCharacterPtr) {
            ReadPlayerCharacter(m_cachedPlayerData);
            m_lastPlayerUpdate = Utils::TimeUtils::GetCurrentTimestampMs();
        }

        // Update world state cache
        if (m_worldStatePtr) {
            ReadWorldState(m_cachedWorldState);
            m_lastWorldUpdate = Utils::TimeUtils::GetCurrentTimestampMs();
        }
    }
}

bool GameStateManager::ScanCriticalStructures() {
    LOG_INFO("Scanning for critical game structures...");

    using namespace Patterns;

    // List of critical patterns to scan
    struct ScanTarget {
        std::string patternName;
        uintptr_t* targetPtr;
        bool required;
    };

    std::vector<ScanTarget> targets = {
        { PatternNames::GAME_WORLD, &m_gameWorldPtr, true },
        { PatternNames::PLAYER_CHARACTER, &m_playerCharacterPtr, true },
        { PatternNames::CHARACTER_LIST, &m_characterListPtr, true },
        { PatternNames::WORLD_STATE, &m_worldStatePtr, false },
        { PatternNames::SQUAD_LIST, &m_squadListPtr, false },
        { PatternNames::FACTION_LIST, &m_factionListPtr, false },
        { PatternNames::BUILDING_LIST, &m_buildingListPtr, false }
    };

    int requiredFound = 0;
    int requiredTotal = 0;

    for (const auto& target : targets) {
        m_stats.scannedPatterns++;

        if (target.required) {
            requiredTotal++;
        }

        LOG_DEBUG_F("Scanning for: %s", target.patternName.c_str());

        if (ScanAndResolve(target.patternName, *target.targetPtr)) {
            m_stats.foundPatterns++;

            if (target.required) {
                requiredFound++;
            }

            LOG_INFO_F("  Found: %s at 0x%llX", target.patternName.c_str(), *target.targetPtr);
        } else {
            if (target.required) {
                LOG_ERROR_F("  REQUIRED pattern not found: %s", target.patternName.c_str());
            } else {
                LOG_WARNING_F("  Optional pattern not found: %s", target.patternName.c_str());
            }
        }
    }

    // Check if all required patterns were found
    if (requiredFound < requiredTotal) {
        LOG_ERROR_F("Missing required patterns: %d/%d found", requiredFound, requiredTotal);
        return false;
    }

    LOG_INFO("All required structures found");
    return true;
}

bool GameStateManager::ScanAndResolve(const std::string& patternName, uintptr_t& outAddress) {
    auto& db = Patterns::PatternDatabase::GetInstance();

    // Get pattern from database
    const Patterns::PatternEntry* pattern = db.GetPattern(patternName);
    if (!pattern) {
        LOG_ERROR_F("Pattern not in database: %s", patternName.c_str());
        return false;
    }

    // Scan for pattern
    Memory::MemoryScanner::Pattern scanPattern;
    scanPattern.pattern = pattern->pattern;
    scanPattern.mask = "";  // MemoryScanner will generate from pattern

    uintptr_t address = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", scanPattern);

    if (!address) {
        LOG_DEBUG_F("Pattern not found: %s", pattern->pattern.c_str());
        return false;
    }

    // Apply offset
    address += pattern->offset;

    // Resolve RIP-relative if needed
    if (pattern->isRIPRelative) {
        LOG_TRACE("Resolving RIP-relative address...");
        address = Memory::MemoryScanner::ResolveRelativeAddress(address);

        if (!address) {
            LOG_ERROR("Failed to resolve RIP-relative address");
            return false;
        }
    }

    outAddress = address;
    return true;
}

bool GameStateManager::ReadPlayerCharacter(Kenshi::CharacterData& outData) {
    if (!m_playerCharacterPtr) {
        return false;
    }

    m_stats.memoryReads++;

    bool success = Kenshi::GameDataReader::ReadCharacter(m_playerCharacterPtr, outData);

    if (!success) {
        m_stats.failedReads++;
        LOG_WARNING("Failed to read player character data");
    }

    return success;
}

std::vector<Kenshi::CharacterData> GameStateManager::GetNearbyCharacters(float radius) {
    std::vector<Kenshi::CharacterData> characters;

    if (!m_characterListPtr || !m_playerCharacterPtr) {
        return characters;
    }

    // Get player position for distance check
    Kenshi::CharacterData playerData;
    if (!ReadPlayerCharacter(playerData)) {
        return characters;
    }

    // TODO: Implement character list traversal
    // This requires understanding the structure of the character list
    // For now, return empty vector

    LOG_TRACE_F("GetNearbyCharacters called (radius: %.1f)", radius);

    return characters;
}

bool GameStateManager::ReadWorldState(Kenshi::WorldStateData& outData) {
    if (!m_worldStatePtr) {
        return false;
    }

    m_stats.memoryReads++;

    bool success = Kenshi::GameDataReader::ReadWorldState(m_worldStatePtr, outData);

    if (!success) {
        m_stats.failedReads++;
        LOG_WARNING("Failed to read world state data");
    }

    return success;
}

int GameStateManager::GetCurrentDay() {
    Kenshi::WorldStateData worldState;
    if (ReadWorldState(worldState)) {
        return worldState.currentDay;
    }
    return -1;
}

float GameStateManager::GetCurrentTime() {
    Kenshi::WorldStateData worldState;
    if (ReadWorldState(worldState)) {
        return worldState.currentTime;
    }
    return -1.0f;
}

std::vector<Kenshi::BuildingData> GameStateManager::GetNearbyBuildings(
    const Kenshi::Vector3& position,
    float radius)
{
    std::vector<Kenshi::BuildingData> buildings;

    if (!m_buildingListPtr) {
        LOG_WARNING("Building list pointer not available");
        return buildings;
    }

    // TODO: Implement building list traversal and proximity filtering
    LOG_TRACE_F("GetNearbyBuildings called (radius: %.1f)", radius);

    return buildings;
}

void GameStateManager::PrintDiagnostics() {
    LOG_INFO("=== GameStateManager Diagnostics ===");
    LOG_INFO_F("Initialized: %s", m_initialized ? "Yes" : "No");
    LOG_INFO("");

    LOG_INFO("Game Structure Pointers:");
    LOG_INFO_F("  Game World:       0x%llX", m_gameWorldPtr);
    LOG_INFO_F("  Player Character: 0x%llX", m_playerCharacterPtr);
    LOG_INFO_F("  Character List:   0x%llX", m_characterListPtr);
    LOG_INFO_F("  World State:      0x%llX", m_worldStatePtr);
    LOG_INFO_F("  Squad List:       0x%llX", m_squadListPtr);
    LOG_INFO_F("  Faction List:     0x%llX", m_factionListPtr);
    LOG_INFO_F("  Building List:    0x%llX", m_buildingListPtr);
    LOG_INFO("");

    LOG_INFO("Statistics:");
    LOG_INFO_F("  Patterns Scanned: %u", m_stats.scannedPatterns);
    LOG_INFO_F("  Patterns Found:   %u", m_stats.foundPatterns);
    LOG_INFO_F("  Memory Reads:     %u", m_stats.memoryReads);
    LOG_INFO_F("  Failed Reads:     %u", m_stats.failedReads);
    LOG_INFO_F("  Init Time:        %.2f ms", m_stats.initializationTime);
    LOG_INFO("");

    LOG_INFO("Cache Status:");
    LOG_INFO_F("  Last Player Update: %llu ms ago",
               Utils::TimeUtils::GetCurrentTimestampMs() - m_lastPlayerUpdate);
    LOG_INFO_F("  Last World Update:  %llu ms ago",
               Utils::TimeUtils::GetCurrentTimestampMs() - m_lastWorldUpdate);

    if (m_playerCharacterPtr) {
        Kenshi::CharacterData playerData;
        if (ReadPlayerCharacter(playerData)) {
            LOG_INFO("");
            LOG_INFO("Current Player Data:");
            LOG_INFO_F("  Name: %s", playerData.name);
            LOG_INFO_F("  Health: %.1f / %.1f", playerData.health, playerData.maxHealth);
            LOG_INFO_F("  Position: (%.1f, %.1f, %.1f)",
                       playerData.position.x, playerData.position.y, playerData.position.z);
            LOG_INFO_F("  Alive: %s", playerData.isAlive ? "Yes" : "No");
        }
    }

    LOG_INFO("=====================================");
}

} // namespace ReKenshi

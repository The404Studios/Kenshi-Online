#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ReKenshi {
namespace KServerMod {

/**
 * KServerMod Integration
 *
 * This module integrates functionality from the KServerMod project
 * (https://github.com/codiren/KServerMod)
 *
 * Adds support for:
 * - Item spawning
 * - Character spawning
 * - Squad spawning
 * - Game speed synchronization
 * - Direct memory offsets (GOG 1.0.68)
 */

//=============================================================================
// Direct Memory Offsets (GOG 1.0.68 - x64)
// These are proven working offsets from KServerMod
//=============================================================================

namespace Offsets {
    // Base address will be added at runtime
    constexpr uintptr_t FACTION_STRING = 0x16C2F68;
    constexpr uintptr_t GAME_WORLD = 0x2133040;
    constexpr uintptr_t SET_PAUSED = 0x7876A0;

    // Hook points
    constexpr uintptr_t CHAR_UPDATE_HOOK = 0x65F6C7;
    constexpr uintptr_t BUILDING_UPDATE_HOOK = 0x9FAA57;

    // Item spawning
    constexpr uintptr_t ITEM_SPAWNING_HAND = 0x1E395F8;
    constexpr uintptr_t ITEM_SPAWNING_MAGIC = 0x21334E0;
    constexpr uintptr_t SPAWN_ITEM_FUNC = 0x2E41F;
    constexpr uintptr_t GET_SECTION_FROM_INV_BY_NAME = 0x4FE3F;

    // Game data managers
    constexpr uintptr_t GAME_DATA_MANAGER_MAIN = 0x2133060;
    constexpr uintptr_t GAME_DATA_MANAGER_FOLIAGE = 0x21331E0;
    constexpr uintptr_t GAME_DATA_MANAGER_SQUADS = 0x2133360;

    // Squad spawning
    constexpr uintptr_t SPAWN_SQUAD_BYPASS = 0x4FF47C;
    constexpr uintptr_t SPAWN_SQUAD_FUNC_CALL = 0x4FFA88;
    constexpr uintptr_t SQUAD_SPAWNING_HAND = 0x21334E0;
}

//=============================================================================
// Item Spawning
//=============================================================================

struct ItemSpawnInfo {
    std::string itemName;
    int quantity;
    float posX;
    float posY;
    float posZ;
};

class ItemSpawner {
public:
    static ItemSpawner& GetInstance();

    // Initialize with game base address
    bool Initialize(uintptr_t gameBase);

    // Spawn an item at position
    bool SpawnItem(const ItemSpawnInfo& info);

    // Spawn item in character inventory
    bool SpawnItemInInventory(const std::string& characterName, const std::string& itemName, int quantity);

    // Get item database pointer
    uintptr_t GetItemDatabasePointer() const;

private:
    ItemSpawner() = default;

    uintptr_t m_gameBase = 0;
    uintptr_t m_itemSpawningHand = 0;
    uintptr_t m_itemSpawningMagic = 0;
    uintptr_t m_spawnItemFunc = 0;
    bool m_initialized = false;
};

//=============================================================================
// Character/Squad Spawning
//=============================================================================

struct SquadSpawnInfo {
    std::string squadName;
    std::string factionName;
    float posX;
    float posY;
    float posZ;
    int memberCount;
};

class SquadSpawner {
public:
    static SquadSpawner& GetInstance();

    // Initialize with game base address
    bool Initialize(uintptr_t gameBase);

    // Spawn a squad at position
    bool SpawnSquad(const SquadSpawnInfo& info);

    // Spawn single character
    bool SpawnCharacter(const std::string& characterName, const std::string& factionName, float x, float y, float z);

    // Get squad database pointer
    uintptr_t GetSquadDatabasePointer() const;

private:
    SquadSpawner() = default;

    uintptr_t m_gameBase = 0;
    uintptr_t m_gameDataManagerSquads = 0;
    uintptr_t m_spawnSquadBypass = 0;
    uintptr_t m_squadSpawningHand = 0;
    bool m_initialized = false;
};

//=============================================================================
// Game Speed Control
//=============================================================================

class GameSpeedController {
public:
    static GameSpeedController& GetInstance();

    // Initialize with game base address
    bool Initialize(uintptr_t gameBase);

    // Set game speed multiplier (0.0 = paused, 1.0 = normal, 2.0 = double speed, etc.)
    bool SetGameSpeed(float speedMultiplier);

    // Pause/unpause the game
    bool SetPaused(bool paused);

    // Get current game speed
    float GetGameSpeed() const;

    // Check if game is paused
    bool IsPaused() const;

private:
    GameSpeedController() = default;

    uintptr_t m_gameBase = 0;
    uintptr_t m_setPausedFunc = 0;
    bool m_initialized = false;
    float m_currentSpeed = 1.0f;
    bool m_isPaused = false;
};

//=============================================================================
// Faction Manager
//=============================================================================

class FactionManager {
public:
    static FactionManager& GetInstance();

    // Initialize with game base address
    bool Initialize(uintptr_t gameBase);

    // Get player faction name
    std::string GetPlayerFaction() const;

    // Set player faction
    bool SetPlayerFaction(const std::string& factionName);

    // Get faction list
    std::vector<std::string> GetAvailableFactions() const;

private:
    FactionManager() = default;

    uintptr_t m_gameBase = 0;
    uintptr_t m_factionString = 0;
    bool m_initialized = false;
};

//=============================================================================
// KServerMod Manager (Main Integration Point)
//=============================================================================

class KServerModManager {
public:
    static KServerModManager& GetInstance();

    // Initialize all KServerMod systems
    bool Initialize();

    // Check if running on GOG 1.0.68 (offsets only work on this version)
    bool IsCompatibleVersion() const;

    // Get game base address
    uintptr_t GetGameBaseAddress() const;

    // Access to subsystems
    ItemSpawner& GetItemSpawner() { return ItemSpawner::GetInstance(); }
    SquadSpawner& GetSquadSpawner() { return SquadSpawner::GetInstance(); }
    GameSpeedController& GetGameSpeedController() { return GameSpeedController::GetInstance(); }
    FactionManager& GetFactionManager() { return FactionManager::GetInstance(); }

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

private:
    KServerModManager() = default;

    uintptr_t m_gameBase = 0;
    bool m_initialized = false;
};

} // namespace KServerMod
} // namespace ReKenshi

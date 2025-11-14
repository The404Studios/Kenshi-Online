#include "../include/KServerModIntegration.h"
#include "../include/Logger.h"
#include "../include/MemoryScanner.h"
#include <windows.h>
#include <psapi.h>

using namespace ReKenshi::Logging;
using namespace ReKenshi::Memory;

namespace ReKenshi {
namespace KServerMod {

//=============================================================================
// Helper Functions
//=============================================================================

static uintptr_t GetModuleBaseAddress(const char* moduleName) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) {
        return 0;
    }

    MODULEINFO modInfo;
    if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        return reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    }

    return 0;
}

//=============================================================================
// KServerModManager
//=============================================================================

KServerModManager& KServerModManager::GetInstance() {
    static KServerModManager instance;
    return instance;
}

bool KServerModManager::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("╔════════════════════════════════════════════════════════╗");
    LOG_INFO("║        KServerMod Integration v1.0                     ║");
    LOG_INFO("╚════════════════════════════════════════════════════════╝");

    // Get game base address
    m_gameBase = GetModuleBaseAddress("kenshi_x64.exe");
    if (!m_gameBase) {
        LOG_ERROR("Failed to get kenshi_x64.exe base address");
        return false;
    }

    LOG_INFO_F("Game base address: 0x%llX", m_gameBase);

    // Check version compatibility
    if (!IsCompatibleVersion()) {
        LOG_WARNING("Game version may not be GOG 1.0.68 - KServerMod offsets may not work!");
        LOG_WARNING("Pattern scanning will be used as fallback.");
    }

    // Initialize subsystems
    bool success = true;

    if (!ItemSpawner::GetInstance().Initialize(m_gameBase)) {
        LOG_WARNING("Item spawner initialization failed");
        success = false;
    }

    if (!SquadSpawner::GetInstance().Initialize(m_gameBase)) {
        LOG_WARNING("Squad spawner initialization failed");
        success = false;
    }

    if (!GameSpeedController::GetInstance().Initialize(m_gameBase)) {
        LOG_WARNING("Game speed controller initialization failed");
        success = false;
    }

    if (!FactionManager::GetInstance().Initialize(m_gameBase)) {
        LOG_WARNING("Faction manager initialization failed");
        success = false;
    }

    m_initialized = success;

    if (m_initialized) {
        LOG_INFO("✓ KServerMod integration initialized successfully");
    } else {
        LOG_WARNING("KServerMod integration partially initialized - some features may not work");
    }

    return m_initialized;
}

bool KServerModManager::IsCompatibleVersion() const {
    // Check if Game World offset contains valid pointer
    uintptr_t gameWorldAddr = m_gameBase + Offsets::GAME_WORLD;
    uintptr_t gameWorldPtr = 0;

    if (MemoryScanner::ReadMemory(gameWorldAddr, gameWorldPtr)) {
        // Valid pointer should be in process memory range
        if (gameWorldPtr > 0x10000 && gameWorldPtr < 0x7FFFFFFFFFFF) {
            return true;
        }
    }

    return false;
}

uintptr_t KServerModManager::GetGameBaseAddress() const {
    return m_gameBase;
}

//=============================================================================
// ItemSpawner
//=============================================================================

ItemSpawner& ItemSpawner::GetInstance() {
    static ItemSpawner instance;
    return instance;
}

bool ItemSpawner::Initialize(uintptr_t gameBase) {
    if (m_initialized) {
        return true;
    }

    m_gameBase = gameBase;
    m_itemSpawningHand = gameBase + Offsets::ITEM_SPAWNING_HAND;
    m_itemSpawningMagic = gameBase + Offsets::ITEM_SPAWNING_MAGIC;
    m_spawnItemFunc = gameBase + Offsets::SPAWN_ITEM_FUNC;

    LOG_INFO("ItemSpawner initialized");
    LOG_DEBUG_F("  Item spawning hand: 0x%llX", m_itemSpawningHand);
    LOG_DEBUG_F("  Item spawning magic: 0x%llX", m_itemSpawningMagic);
    LOG_DEBUG_F("  Spawn item function: 0x%llX", m_spawnItemFunc);

    m_initialized = true;
    return true;
}

bool ItemSpawner::SpawnItem(const ItemSpawnInfo& info) {
    if (!m_initialized) {
        LOG_ERROR("ItemSpawner not initialized");
        return false;
    }

    LOG_INFO_F("Spawning item: %s (qty: %d) at (%.1f, %.1f, %.1f)",
               info.itemName.c_str(),
               info.quantity,
               info.posX, info.posY, info.posZ);

    // This is a simplified implementation
    // Full implementation would require:
    // 1. Looking up item ID from item name in item database
    // 2. Calling the spawn function with proper calling convention
    // 3. Setting position in world

    // TODO: Implement actual spawning logic
    // For now, just log that we would spawn it

    LOG_WARNING("Item spawning not yet fully implemented - requires assembly hooks");
    return false;
}

bool ItemSpawner::SpawnItemInInventory(const std::string& characterName, const std::string& itemName, int quantity) {
    if (!m_initialized) {
        LOG_ERROR("ItemSpawner not initialized");
        return false;
    }

    LOG_INFO_F("Spawning %d x %s in inventory of %s",
               quantity,
               itemName.c_str(),
               characterName.c_str());

    // TODO: Implement inventory spawning
    LOG_WARNING("Inventory item spawning not yet fully implemented");
    return false;
}

uintptr_t ItemSpawner::GetItemDatabasePointer() const {
    return m_gameBase + Offsets::GAME_DATA_MANAGER_MAIN;
}

//=============================================================================
// SquadSpawner
//=============================================================================

SquadSpawner& SquadSpawner::GetInstance() {
    static SquadSpawner instance;
    return instance;
}

bool SquadSpawner::Initialize(uintptr_t gameBase) {
    if (m_initialized) {
        return true;
    }

    m_gameBase = gameBase;
    m_gameDataManagerSquads = gameBase + Offsets::GAME_DATA_MANAGER_SQUADS;
    m_spawnSquadBypass = gameBase + Offsets::SPAWN_SQUAD_BYPASS;
    m_squadSpawningHand = gameBase + Offsets::SQUAD_SPAWNING_HAND;

    LOG_INFO("SquadSpawner initialized");
    LOG_DEBUG_F("  Squad manager: 0x%llX", m_gameDataManagerSquads);
    LOG_DEBUG_F("  Spawn bypass: 0x%llX", m_spawnSquadBypass);

    m_initialized = true;
    return true;
}

bool SquadSpawner::SpawnSquad(const SquadSpawnInfo& info) {
    if (!m_initialized) {
        LOG_ERROR("SquadSpawner not initialized");
        return false;
    }

    LOG_INFO_F("Spawning squad: %s (faction: %s, members: %d) at (%.1f, %.1f, %.1f)",
               info.squadName.c_str(),
               info.factionName.c_str(),
               info.memberCount,
               info.posX, info.posY, info.posZ);

    // TODO: Implement squad spawning
    LOG_WARNING("Squad spawning not yet fully implemented - requires assembly hooks");
    return false;
}

bool SquadSpawner::SpawnCharacter(const std::string& characterName, const std::string& factionName, float x, float y, float z) {
    if (!m_initialized) {
        LOG_ERROR("SquadSpawner not initialized");
        return false;
    }

    LOG_INFO_F("Spawning character: %s (faction: %s) at (%.1f, %.1f, %.1f)",
               characterName.c_str(),
               factionName.c_str(),
               x, y, z);

    // TODO: Implement character spawning
    LOG_WARNING("Character spawning not yet fully implemented - requires assembly hooks");
    return false;
}

uintptr_t SquadSpawner::GetSquadDatabasePointer() const {
    return m_gameDataManagerSquads;
}

//=============================================================================
// GameSpeedController
//=============================================================================

GameSpeedController& GameSpeedController::GetInstance() {
    static GameSpeedController instance;
    return instance;
}

bool GameSpeedController::Initialize(uintptr_t gameBase) {
    if (m_initialized) {
        return true;
    }

    m_gameBase = gameBase;
    m_setPausedFunc = gameBase + Offsets::SET_PAUSED;

    LOG_INFO("GameSpeedController initialized");
    LOG_DEBUG_F("  Set paused function: 0x%llX", m_setPausedFunc);

    m_initialized = true;
    return true;
}

bool GameSpeedController::SetGameSpeed(float speedMultiplier) {
    if (!m_initialized) {
        LOG_ERROR("GameSpeedController not initialized");
        return false;
    }

    LOG_INFO_F("Setting game speed to %.2fx", speedMultiplier);

    // TODO: Find and modify game speed variable
    // This requires finding the game speed memory location

    m_currentSpeed = speedMultiplier;
    LOG_WARNING("Game speed control not yet fully implemented");
    return false;
}

bool GameSpeedController::SetPaused(bool paused) {
    if (!m_initialized) {
        LOG_ERROR("GameSpeedController not initialized");
        return false;
    }

    LOG_INFO_F("Setting paused state: %s", paused ? "true" : "false");

    // TODO: Call the SetPaused function
    // This requires proper calling convention and parameters

    m_isPaused = paused;
    LOG_WARNING("Pause control not yet fully implemented");
    return false;
}

float GameSpeedController::GetGameSpeed() const {
    return m_currentSpeed;
}

bool GameSpeedController::IsPaused() const {
    return m_isPaused;
}

//=============================================================================
// FactionManager
//=============================================================================

FactionManager& FactionManager::GetInstance() {
    static FactionManager instance;
    return instance;
}

bool FactionManager::Initialize(uintptr_t gameBase) {
    if (m_initialized) {
        return true;
    }

    m_gameBase = gameBase;
    m_factionString = gameBase + Offsets::FACTION_STRING;

    LOG_INFO("FactionManager initialized");
    LOG_DEBUG_F("  Faction string: 0x%llX", m_factionString);

    m_initialized = true;
    return true;
}

std::string FactionManager::GetPlayerFaction() const {
    if (!m_initialized) {
        return "";
    }

    // TODO: Read faction string from memory
    char buffer[256] = {0};
    if (MemoryScanner::ReadMemory(m_factionString, buffer, sizeof(buffer) - 1)) {
        return std::string(buffer);
    }

    return "";
}

bool FactionManager::SetPlayerFaction(const std::string& factionName) {
    if (!m_initialized) {
        LOG_ERROR("FactionManager not initialized");
        return false;
    }

    LOG_INFO_F("Setting player faction to: %s", factionName.c_str());

    // TODO: Write faction string to memory
    LOG_WARNING("Faction changing not yet fully implemented");
    return false;
}

std::vector<std::string> FactionManager::GetAvailableFactions() const {
    // TODO: Read faction list from game data manager
    return {
        "The Holy Nation",
        "The Shek Kingdom",
        "The United Cities",
        "Tech Hunters",
        "Traders Guild",
        "Anti-Slavers",
        "Flotsam Ninjas"
    };
}

} // namespace KServerMod
} // namespace ReKenshi

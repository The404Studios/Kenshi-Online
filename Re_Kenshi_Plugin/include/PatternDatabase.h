#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace ReKenshi {
namespace Patterns {

/**
 * Pattern entry with metadata
 */
struct PatternEntry {
    std::string name;           // Descriptive name
    std::string pattern;        // Pattern string with wildcards
    int offset;                 // Offset from found address
    bool isRIPRelative;         // Whether to resolve RIP-relative address
    std::string description;    // What this pattern finds
    std::string version;        // Game version this pattern is for
};

/**
 * Pattern database for common Kenshi structures
 */
class PatternDatabase {
public:
    static PatternDatabase& GetInstance();

    // Get pattern by name
    const PatternEntry* GetPattern(const std::string& name) const;

    // Get all patterns for a category
    std::vector<const PatternEntry*> GetPatternsByCategory(const std::string& category) const;

    // Check if pattern exists
    bool HasPattern(const std::string& name) const;

    // Add custom pattern
    void AddPattern(const std::string& category, const PatternEntry& pattern);

    // Get all categories
    std::vector<std::string> GetCategories() const;

private:
    PatternDatabase();
    ~PatternDatabase() = default;
    PatternDatabase(const PatternDatabase&) = delete;
    PatternDatabase& operator=(const PatternDatabase&) = delete;

    void InitializeDefaultPatterns();

    std::unordered_map<std::string, std::vector<PatternEntry>> m_patterns;
};

/**
 * Common pattern names
 */
namespace PatternNames {
    // World and game state
    constexpr const char* GAME_WORLD = "GameWorld";
    constexpr const char* WORLD_STATE = "WorldState";
    constexpr const char* GAME_TIME = "GameTime";
    constexpr const char* DAY_COUNTER = "DayCounter";

    // Character and entity
    constexpr const char* CHARACTER_LIST = "CharacterList";
    constexpr const char* PLAYER_CHARACTER = "PlayerCharacter";
    constexpr const char* LOCAL_PLAYER = "LocalPlayer";
    constexpr const char* CHARACTER_MANAGER = "CharacterManager";

    // Squad and faction
    constexpr const char* SQUAD_LIST = "SquadList";
    constexpr const char* FACTION_LIST = "FactionList";
    constexpr const char* PLAYER_FACTION = "PlayerFaction";

    // Combat and stats
    constexpr const char* COMBAT_MANAGER = "CombatManager";
    constexpr const char* DAMAGE_CALCULATOR = "DamageCalculator";
    constexpr const char* STAT_CALCULATOR = "StatCalculator";

    // Inventory and items
    constexpr const char* INVENTORY_MANAGER = "InventoryManager";
    constexpr const char* ITEM_DATABASE = "ItemDatabase";
    constexpr const char* EQUIPMENT_SLOTS = "EquipmentSlots";

    // World objects
    constexpr const char* BUILDING_LIST = "BuildingList";
    constexpr const char* NPC_LIST = "NPCList";
    constexpr const char* ANIMAL_LIST = "AnimalList";
    constexpr const char* WORLD_OBJECTS = "WorldObjects";

    // UI and camera
    constexpr const char* UI_MANAGER = "UIManager";
    constexpr const char* CAMERA_CONTROLLER = "CameraController";
    constexpr const char* SELECTION_MANAGER = "SelectionManager";

    // Weather and environment
    constexpr const char* WEATHER_SYSTEM = "WeatherSystem";
    constexpr const char* ENVIRONMENT_MANAGER = "EnvironmentManager";

    // Quests and dialogue
    constexpr const char* QUEST_MANAGER = "QuestManager";
    constexpr const char* DIALOGUE_SYSTEM = "DialogueSystem";

    // Shop and economy
    constexpr const char* SHOP_MANAGER = "ShopManager";
    constexpr const char* TRADE_MANAGER = "TradeManager";

    // AI and pathfinding
    constexpr const char* AI_MANAGER = "AIManager";
    constexpr const char* PATHFINDING = "Pathfinding";

    // Rendering (OGRE)
    constexpr const char* OGRE_ROOT = "OgreRoot";
    constexpr const char* SCENE_MANAGER = "SceneManager";
    constexpr const char* RENDER_WINDOW = "RenderWindow";
}

/**
 * Pattern categories
 */
namespace PatternCategories {
    constexpr const char* WORLD = "World";
    constexpr const char* CHARACTERS = "Characters";
    constexpr const char* FACTIONS = "Factions";
    constexpr const char* COMBAT = "Combat";
    constexpr const char* INVENTORY = "Inventory";
    constexpr const char* WORLD_OBJECTS = "WorldObjects";
    constexpr const char* UI = "UI";
    constexpr const char* WEATHER = "Weather";
    constexpr const char* QUESTS = "Quests";
    constexpr const char* SHOPS = "Shops";
    constexpr const char* AI = "AI";
    constexpr const char* RENDERING = "Rendering";
}

} // namespace Patterns
} // namespace ReKenshi

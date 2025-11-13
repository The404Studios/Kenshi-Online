#include "PatternDatabase.h"

namespace ReKenshi {
namespace Patterns {

PatternDatabase& PatternDatabase::GetInstance() {
    static PatternDatabase instance;
    return instance;
}

PatternDatabase::PatternDatabase() {
    InitializeDefaultPatterns();
}

const PatternEntry* PatternDatabase::GetPattern(const std::string& name) const {
    for (const auto& [category, patterns] : m_patterns) {
        for (const auto& pattern : patterns) {
            if (pattern.name == name) {
                return &pattern;
            }
        }
    }
    return nullptr;
}

std::vector<const PatternEntry*> PatternDatabase::GetPatternsByCategory(const std::string& category) const {
    std::vector<const PatternEntry*> result;

    auto it = m_patterns.find(category);
    if (it != m_patterns.end()) {
        for (const auto& pattern : it->second) {
            result.push_back(&pattern);
        }
    }

    return result;
}

bool PatternDatabase::HasPattern(const std::string& name) const {
    return GetPattern(name) != nullptr;
}

void PatternDatabase::AddPattern(const std::string& category, const PatternEntry& pattern) {
    m_patterns[category].push_back(pattern);
}

std::vector<std::string> PatternDatabase::GetCategories() const {
    std::vector<std::string> categories;
    for (const auto& [category, _] : m_patterns) {
        categories.push_back(category);
    }
    return categories;
}

void PatternDatabase::InitializeDefaultPatterns() {
    //=========================================================================
    // World Patterns
    //=========================================================================

    m_patterns[PatternCategories::WORLD] = {
        {
            PatternNames::GAME_WORLD,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01",
            3,
            true,
            "Main game world instance pointer",
            "1.0.0"
        },
        {
            PatternNames::WORLD_STATE,
            "48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9",
            3,
            true,
            "World state manager",
            "1.0.0"
        },
        {
            PatternNames::GAME_TIME,
            "F3 0F 10 05 ?? ?? ?? ?? F3 0F 59 C1",
            4,
            true,
            "Current game time (hours)",
            "1.0.0"
        },
        {
            PatternNames::DAY_COUNTER,
            "8B 05 ?? ?? ?? ?? 89 44 24 ?? 48 8D 4C 24",
            2,
            true,
            "Current day counter",
            "1.0.0"
        }
    };

    //=========================================================================
    // Character Patterns
    //=========================================================================

    m_patterns[PatternCategories::CHARACTERS] = {
        {
            PatternNames::CHARACTER_LIST,
            "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 85 C0",
            3,
            true,
            "List of all characters in world",
            "1.0.0"
        },
        {
            PatternNames::PLAYER_CHARACTER,
            "48 8B 15 ?? ?? ?? ?? 48 85 D2 74 ?? 48 8B 42",
            3,
            true,
            "Local player character",
            "1.0.0"
        },
        {
            PatternNames::LOCAL_PLAYER,
            "48 89 1D ?? ?? ?? ?? 48 8B 03 48 8B CB FF 90",
            3,
            true,
            "Local player controller",
            "1.0.0"
        },
        {
            PatternNames::CHARACTER_MANAGER,
            "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B D8 48 85 C0",
            3,
            true,
            "Character manager singleton",
            "1.0.0"
        }
    };

    //=========================================================================
    // Faction Patterns
    //=========================================================================

    m_patterns[PatternCategories::FACTIONS] = {
        {
            PatternNames::SQUAD_LIST,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 48 8B 03",
            3,
            true,
            "List of all squads",
            "1.0.0"
        },
        {
            PatternNames::FACTION_LIST,
            "48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? E8",
            3,
            true,
            "List of all factions",
            "1.0.0"
        },
        {
            PatternNames::PLAYER_FACTION,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 8B 81",
            3,
            true,
            "Player's faction",
            "1.0.0"
        }
    };

    //=========================================================================
    // Combat Patterns
    //=========================================================================

    m_patterns[PatternCategories::COMBAT] = {
        {
            PatternNames::COMBAT_MANAGER,
            "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 48 8B",
            3,
            true,
            "Combat manager singleton",
            "1.0.0"
        },
        {
            PatternNames::DAMAGE_CALCULATOR,
            "E8 ?? ?? ?? ?? F3 0F 10 44 24 ?? F3 0F 5C C6",
            1,
            true,
            "Function that calculates damage",
            "1.0.0"
        },
        {
            PatternNames::STAT_CALCULATOR,
            "E8 ?? ?? ?? ?? F3 0F 10 00 F3 0F 11 45",
            1,
            true,
            "Function that calculates character stats",
            "1.0.0"
        }
    };

    //=========================================================================
    // Inventory Patterns
    //=========================================================================

    m_patterns[PatternCategories::INVENTORY] = {
        {
            PatternNames::INVENTORY_MANAGER,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 48 8B 01",
            3,
            true,
            "Inventory manager singleton",
            "1.0.0"
        },
        {
            PatternNames::ITEM_DATABASE,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 0F 84 ?? ?? ?? ??",
            3,
            true,
            "Item database/definition list",
            "1.0.0"
        },
        {
            PatternNames::EQUIPMENT_SLOTS,
            "48 8B 80 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 88",
            2,
            false,
            "Character equipment slots offset",
            "1.0.0"
        }
    };

    //=========================================================================
    // World Objects Patterns
    //=========================================================================

    m_patterns[PatternCategories::WORLD_OBJECTS] = {
        {
            PatternNames::BUILDING_LIST,
            "48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 8B 01",
            3,
            true,
            "List of all buildings in world",
            "1.0.0"
        },
        {
            PatternNames::NPC_LIST,
            "48 8B 0D ?? ?? ?? ?? 45 33 C0 E8 ?? ?? ?? ??",
            3,
            true,
            "List of NPCs",
            "1.0.0"
        },
        {
            PatternNames::ANIMAL_LIST,
            "48 8B 15 ?? ?? ?? ?? 48 85 D2 0F 84 ?? ?? ?? ??",
            3,
            true,
            "List of animals/creatures",
            "1.0.0"
        },
        {
            PatternNames::WORLD_OBJECTS,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 0F 84 ?? ?? ?? ?? 48 8B 03",
            3,
            true,
            "World objects list (items on ground, containers)",
            "1.0.0"
        }
    };

    //=========================================================================
    // UI Patterns
    //=========================================================================

    m_patterns[PatternCategories::UI] = {
        {
            PatternNames::UI_MANAGER,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? E8 ?? ?? ?? ?? 84 C0",
            3,
            true,
            "UI manager singleton",
            "1.0.0"
        },
        {
            PatternNames::CAMERA_CONTROLLER,
            "48 8B 05 ?? ?? ?? ?? F3 0F 10 88 ?? ?? ?? ??",
            3,
            true,
            "Camera controller",
            "1.0.0"
        },
        {
            PatternNames::SELECTION_MANAGER,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 48 8B 0B",
            3,
            true,
            "Selection/interaction manager",
            "1.0.0"
        }
    };

    //=========================================================================
    // Weather Patterns
    //=========================================================================

    m_patterns[PatternCategories::WEATHER] = {
        {
            PatternNames::WEATHER_SYSTEM,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 8B 81 ?? ?? ?? ??",
            3,
            true,
            "Weather system manager",
            "1.0.0"
        },
        {
            PatternNames::ENVIRONMENT_MANAGER,
            "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? F3 0F 10 80",
            3,
            true,
            "Environment manager (time of day, fog, etc.)",
            "1.0.0"
        }
    };

    //=========================================================================
    // Quest Patterns
    //=========================================================================

    m_patterns[PatternCategories::QUESTS] = {
        {
            PatternNames::QUEST_MANAGER,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 48 8B 01",
            3,
            true,
            "Quest manager singleton",
            "1.0.0"
        },
        {
            PatternNames::DIALOGUE_SYSTEM,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 0F 84 ?? ?? ?? ??",
            3,
            true,
            "Dialogue system",
            "1.0.0"
        }
    };

    //=========================================================================
    // Shop Patterns
    //=========================================================================

    m_patterns[PatternCategories::SHOPS] = {
        {
            PatternNames::SHOP_MANAGER,
            "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B F8 48 85 C0",
            3,
            true,
            "Shop/vendor manager",
            "1.0.0"
        },
        {
            PatternNames::TRADE_MANAGER,
            "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 88 ?? ?? ?? ??",
            3,
            true,
            "Trade manager (buying/selling)",
            "1.0.0"
        }
    };

    //=========================================================================
    // AI Patterns
    //=========================================================================

    m_patterns[PatternCategories::AI] = {
        {
            PatternNames::AI_MANAGER,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 90 ?? ?? ?? ??",
            3,
            true,
            "AI manager singleton",
            "1.0.0"
        },
        {
            PatternNames::PATHFINDING,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? E8 ?? ?? ?? ??",
            3,
            true,
            "Pathfinding system",
            "1.0.0"
        }
    };

    //=========================================================================
    // Rendering Patterns (OGRE)
    //=========================================================================

    m_patterns[PatternCategories::RENDERING] = {
        {
            PatternNames::OGRE_ROOT,
            "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 8B D8",
            3,
            true,
            "OGRE Root singleton",
            "1.0.0"
        },
        {
            PatternNames::SCENE_MANAGER,
            "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9",
            3,
            true,
            "OGRE Scene Manager",
            "1.0.0"
        },
        {
            PatternNames::RENDER_WINDOW,
            "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 48 8B 03 48 8B CB",
            3,
            true,
            "OGRE Render Window",
            "1.0.0"
        }
    };
}

} // namespace Patterns
} // namespace ReKenshi

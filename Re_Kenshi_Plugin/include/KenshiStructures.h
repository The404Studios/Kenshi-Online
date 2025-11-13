#pragma once

#include <cstdint>
#include <string>

namespace ReKenshi {
namespace Kenshi {

/**
 * Reverse-engineered Kenshi game structures
 * These are approximate and may need adjustment based on game version
 */

// Forward declarations
class Character;
class WorldState;
class GameController;

/**
 * 3D Vector structure
 */
struct Vector3 {
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

/**
 * Quaternion for rotation
 */
struct Quaternion {
    float x, y, z, w;

    Quaternion() : x(0), y(0), z(0), w(1) {}
    Quaternion(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

/**
 * Character data structure
 * Offset: Base address varies by game version
 */
struct CharacterData {
    char pad_0000[0x10];           // 0x0000
    char name[64];                 // 0x0010 - Character name
    char pad_0050[0x20];           // 0x0050
    Vector3 position;              // 0x0070 - World position
    Quaternion rotation;           // 0x007C - Rotation
    char pad_008C[0x14];           // 0x008C
    float health;                  // 0x00A0 - Current health
    float maxHealth;               // 0x00A4 - Max health
    char pad_00A8[0x18];           // 0x00A8
    int32_t factionId;             // 0x00C0 - Faction ID
    char pad_00C4[0x3C];           // 0x00C4
    bool isAlive;                  // 0x0100 - Alive status
    bool isUnconscious;            // 0x0101 - Unconscious
    char pad_0102[0x2E];           // 0x0102
    int32_t squadId;               // 0x0130 - Squad ID
    char pad_0134[0xCC];           // 0x0134
};

static_assert(sizeof(CharacterData) >= 0x200, "CharacterData structure too small");

/**
 * World State structure
 */
struct WorldStateData {
    char pad_0000[0x20];           // 0x0000
    int32_t dayNumber;             // 0x0020 - Current day
    float timeOfDay;               // 0x0024 - Time (0.0 - 1.0)
    char pad_0028[0x18];           // 0x0028
    Vector3 playerPosition;        // 0x0040 - Last player position
    char pad_004C[0x34];           // 0x004C
    bool isPaused;                 // 0x0080 - Game paused
    char pad_0081[0x7F];           // 0x0081
};

/**
 * Squad structure
 */
struct SquadData {
    char pad_0000[0x10];           // 0x0000
    char name[64];                 // 0x0010 - Squad name
    char pad_0050[0x20];           // 0x0050
    int32_t leaderCharacterId;     // 0x0070 - Leader ID
    int32_t memberCount;           // 0x0074 - Number of members
    char pad_0078[0x88];           // 0x0078
};

/**
 * Faction structure
 */
struct FactionData {
    char pad_0000[0x10];           // 0x0000
    char name[64];                 // 0x0010 - Faction name
    char pad_0050[0x30];           // 0x0050
    int32_t reputation;            // 0x0080 - Reputation (-100 to 100)
    char pad_0084[0x7C];           // 0x0084
};

/**
 * Item structure
 */
struct ItemData {
    char pad_0000[0x10];           // 0x0000
    int32_t itemId;                // 0x0010 - Item ID
    char pad_0014[0x0C];           // 0x0014
    int32_t quantity;              // 0x0020 - Stack size
    float condition;               // 0x0024 - Condition (0.0 - 1.0)
    char pad_0028[0x38];           // 0x0028
};

/**
 * Inventory structure
 */
struct InventoryData {
    char pad_0000[0x10];           // 0x0000
    ItemData* items;               // 0x0010 - Item array pointer
    int32_t itemCount;             // 0x0018 - Number of items
    int32_t capacity;              // 0x001C - Max items
    char pad_0020[0x40];           // 0x0020
};

/**
 * Game offsets (discovered through reverse engineering)
 * These need to be updated for each Kenshi version
 */
namespace Offsets {
    // Base pointers (for Kenshi v1.0.60)
    constexpr uintptr_t GAME_WORLD_BASE = 0x0;          // Found via pattern scan
    constexpr uintptr_t CHARACTER_LIST_BASE = 0x0;      // Found via pattern scan
    constexpr uintptr_t PLAYER_CONTROLLER_BASE = 0x0;   // Found via pattern scan

    // Character offsets
    namespace Character {
        constexpr uintptr_t NAME = 0x10;
        constexpr uintptr_t POSITION = 0x70;
        constexpr uintptr_t ROTATION = 0x7C;
        constexpr uintptr_t HEALTH = 0xA0;
        constexpr uintptr_t MAX_HEALTH = 0xA4;
        constexpr uintptr_t FACTION_ID = 0xC0;
        constexpr uintptr_t IS_ALIVE = 0x100;
        constexpr uintptr_t SQUAD_ID = 0x130;
    }

    // World State offsets
    namespace WorldState {
        constexpr uintptr_t DAY_NUMBER = 0x20;
        constexpr uintptr_t TIME_OF_DAY = 0x24;
        constexpr uintptr_t PLAYER_POSITION = 0x40;
        constexpr uintptr_t IS_PAUSED = 0x80;
    }

    // Function addresses (found via pattern scanning)
    namespace Functions {
        extern uintptr_t SpawnCharacter;
        extern uintptr_t UpdateWorld;
        extern uintptr_t GetCharacterByName;
        extern uintptr_t DamageCharacter;
    }
}

/**
 * Helper class for reading game data safely
 */
class GameDataReader {
public:
    static bool ReadCharacter(uintptr_t address, CharacterData& outData);
    static bool ReadWorldState(uintptr_t address, WorldStateData& outData);
    static bool ReadSquad(uintptr_t address, SquadData& outData);
    static bool ReadFaction(uintptr_t address, FactionData& outData);
    static bool ReadInventory(uintptr_t address, InventoryData& outData);

    // Write functions (use with caution!)
    static bool WriteCharacterPosition(uintptr_t address, const Vector3& position);
    static bool WriteCharacterHealth(uintptr_t address, float health);
};

} // namespace Kenshi
} // namespace ReKenshi

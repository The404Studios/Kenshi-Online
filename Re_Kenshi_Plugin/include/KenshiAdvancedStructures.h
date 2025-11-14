#pragma once

#include "KenshiStructures.h"
#include <string>
#include <vector>

namespace ReKenshi {
namespace Kenshi {

/**
 * Building/Structure data
 */
struct BuildingData {
    char pad_0000[0x10];           // 0x0000
    char name[64];                 // 0x0010 - Building name
    char pad_0050[0x20];           // 0x0050
    Vector3 position;              // 0x0070 - World position
    char pad_007C[0x24];           // 0x007C
    int32_t buildingType;          // 0x00A0 - Type ID
    int32_t ownerId;               // 0x00A4 - Owner faction/character ID
    float healthPercentage;        // 0x00A8 - 0.0 to 1.0
    bool isConstructed;            // 0x00AC - Fully built
    bool isBeingRepaired;          // 0x00AD - Under repair
    char pad_00AE[0x52];           // 0x00AE
};

/**
 * NPC (Non-player character) specific data
 */
struct NPCData : public CharacterData {
    char pad_0200[0x10];           // 0x0200
    int32_t aiState;               // 0x0210 - Current AI state
    uintptr_t targetAddress;       // 0x0214 - Current target
    Vector3 destinationPos;        // 0x021C - Movement destination
    float aggroRadius;             // 0x0228 - Aggression range
    char pad_022C[0x14];           // 0x022C
    bool isHostile;                // 0x0240 - Hostile to player
    bool isTrader;                 // 0x0241 - Can trade
    bool isRecruitab le;            // 0x0242 - Can be recruited
    char pad_0243[0x3D];           // 0x0243
};

/**
 * World object (items on ground, containers, etc.)
 */
struct WorldObjectData {
    char pad_0000[0x10];           // 0x0000
    int32_t objectType;            // 0x0010 - Type ID
    Vector3 position;              // 0x0014 - World position
    Quaternion rotation;           // 0x0020 - Rotation
    char pad_0030[0x10];           // 0x0030
    uintptr_t inventoryAddress;    // 0x0040 - Inventory pointer (if container)
    bool isLootable;               // 0x0048 - Can be looted
    bool isLocked;                 // 0x0049 - Locked container
    char pad_004A[0x36];           // 0x004A
};

/**
 * Quest data structure
 */
struct QuestData {
    char pad_0000[0x10];           // 0x0000
    char questName[128];           // 0x0010 - Quest name
    char pad_0090[0x10];           // 0x0090
    int32_t questId;               // 0x00A0 - Unique quest ID
    int32_t questState;            // 0x00A4 - Current state/progress
    bool isActive;                 // 0x00A8 - Currently active
    bool isCompleted;              // 0x00A9 - Completed
    char pad_00AA[0x56];           // 0x00AA
};

/**
 * Animal/Creature specific data
 */
struct AnimalData : public CharacterData {
    char pad_0200[0x10];           // 0x0200
    int32_t animalType;            // 0x0210 - Species type
    int32_t packId;                // 0x0214 - Pack/herd ID
    float hungerLevel;             // 0x0218 - 0.0 to 1.0
    bool isTamed;                  // 0x021C - Domesticated
    bool isAggressive;             // 0x021D - Will attack on sight
    char pad_021E[0x42];           // 0x021E
};

/**
 * Shop/Vendor data
 */
struct ShopData {
    char pad_0000[0x10];           // 0x0000
    char shopName[64];             // 0x0010 - Shop name
    char pad_0050[0x20];           // 0x0050
    uintptr_t vendorAddress;       // 0x0070 - Vendor character
    uintptr_t inventoryAddress;    // 0x0078 - Shop inventory
    int32_t shopType;              // 0x0080 - Type of shop
    float buyPriceMultiplier;      // 0x0084 - Price modifier for buying
    float sellPriceMultiplier;     // 0x0088 - Price modifier for selling
    int32_t currency;              // 0x008C - Available money
    char pad_0090[0x70];           // 0x0090
};

/**
 * Weather system data
 */
struct WeatherData {
    char pad_0000[0x10];           // 0x0000
    int32_t weatherType;           // 0x0010 - Current weather
    float intensity;               // 0x0014 - 0.0 to 1.0
    float windSpeed;               // 0x0018 - Wind strength
    Vector3 windDirection;         // 0x001C - Wind direction
    bool isStorm;                  // 0x0028 - Storm active
    char pad_0029[0x37];           // 0x0029
};

/**
 * Advanced game data reader
 */
class AdvancedGameDataReader {
public:
    // Building operations
    static bool ReadBuilding(uintptr_t address, BuildingData& outData);
    static std::vector<BuildingData> GetAllBuildings(uintptr_t buildingListPtr);

    // NPC operations
    static bool ReadNPC(uintptr_t address, NPCData& outData);
    static std::vector<NPCData> GetNearbyNPCs(const Vector3& position, float radius);

    // World object operations
    static bool ReadWorldObject(uintptr_t address, WorldObjectData& outData);
    static std::vector<WorldObjectData> GetNearbyObjects(const Vector3& position, float radius);

    // Quest operations
    static bool ReadQuest(uintptr_t address, QuestData& outData);
    static std::vector<QuestData> GetActiveQuests(uintptr_t questListPtr);

    // Animal operations
    static bool ReadAnimal(uintptr_t address, AnimalData& outData);

    // Shop operations
    static bool ReadShop(uintptr_t address, ShopData& outData);

    // Weather operations
    static bool ReadWeather(uintptr_t address, WeatherData& outData);

    // Helper functions
    static float CalculateDistance(const Vector3& a, const Vector3& b);
    static bool IsPositionInRange(const Vector3& pos, const Vector3& center, float radius);
};

/**
 * Game constants and enums
 */
namespace GameConstants {
    // Weather types
    enum class WeatherType : int32_t {
        Clear = 0,
        Cloudy = 1,
        Rain = 2,
        Sandstorm = 3,
        AcidRain = 4,
        Fog = 5,
    };

    // Building types
    enum class BuildingType : int32_t {
        House = 0,
        Shop = 1,
        Outpost = 2,
        Farm = 3,
        Mine = 4,
        WallSection = 5,
        Gate = 6,
        Turret = 7,
    };

    // NPC AI states
    enum class AIState : int32_t {
        Idle = 0,
        Wandering = 1,
        Following = 2,
        Combat = 3,
        Fleeing = 4,
        Working = 5,
        Trading = 6,
        Sleeping = 7,
    };

    // Animal types
    enum class AnimalType : int32_t {
        Bonedog = 0,
        Garru = 1,
        Goat = 2,
        Bull = 3,
        Leviathan = 4,
        Crab = 5,
        Spider = 6,
    };
}

} // namespace Kenshi
} // namespace ReKenshi

#include "KenshiAdvancedStructures.h"
#include "MemoryScanner.h"
#include <cmath>

namespace ReKenshi {
namespace Kenshi {

//=============================================================================
// AdvancedGameDataReader Implementation
//=============================================================================

bool AdvancedGameDataReader::ReadBuilding(uintptr_t address, BuildingData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

std::vector<BuildingData> AdvancedGameDataReader::GetAllBuildings(uintptr_t buildingListPtr) {
    std::vector<BuildingData> buildings;

    // TODO: Implement building list traversal
    // This requires knowing the structure of the building list
    // For now, return empty vector

    return buildings;
}

bool AdvancedGameDataReader::ReadNPC(uintptr_t address, NPCData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

std::vector<NPCData> AdvancedGameDataReader::GetNearbyNPCs(const Vector3& position, float radius) {
    std::vector<NPCData> npcs;

    // TODO: Implement NPC proximity search
    // This requires iterating through the character list and filtering by distance

    return npcs;
}

bool AdvancedGameDataReader::ReadWorldObject(uintptr_t address, WorldObjectData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

std::vector<WorldObjectData> AdvancedGameDataReader::GetNearbyObjects(const Vector3& position, float radius) {
    std::vector<WorldObjectData> objects;

    // TODO: Implement object proximity search

    return objects;
}

bool AdvancedGameDataReader::ReadQuest(uintptr_t address, QuestData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

std::vector<QuestData> AdvancedGameDataReader::GetActiveQuests(uintptr_t questListPtr) {
    std::vector<QuestData> quests;

    // TODO: Implement quest list traversal

    return quests;
}

bool AdvancedGameDataReader::ReadAnimal(uintptr_t address, AnimalData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool AdvancedGameDataReader::ReadShop(uintptr_t address, ShopData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool AdvancedGameDataReader::ReadWeather(uintptr_t address, WeatherData& outData) {
    if (!address) {
        return false;
    }
    return Memory::MemoryScanner::ReadMemory(address, outData);
}

float AdvancedGameDataReader::CalculateDistance(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

bool AdvancedGameDataReader::IsPositionInRange(const Vector3& pos, const Vector3& center, float radius) {
    return CalculateDistance(pos, center) <= radius;
}

} // namespace Kenshi
} // namespace ReKenshi

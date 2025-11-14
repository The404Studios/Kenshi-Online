#include "KenshiStructures.h"
#include "MemoryScanner.h"
#include <cstring>

namespace ReKenshi {
namespace Kenshi {

// Initialize function addresses
namespace Offsets {
namespace Functions {
    uintptr_t SpawnCharacter = 0;
    uintptr_t UpdateWorld = 0;
    uintptr_t GetCharacterByName = 0;
    uintptr_t DamageCharacter = 0;
}
}

bool GameDataReader::ReadCharacter(uintptr_t address, CharacterData& outData) {
    if (!address) {
        return false;
    }

    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool GameDataReader::ReadWorldState(uintptr_t address, WorldStateData& outData) {
    if (!address) {
        return false;
    }

    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool GameDataReader::ReadSquad(uintptr_t address, SquadData& outData) {
    if (!address) {
        return false;
    }

    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool GameDataReader::ReadFaction(uintptr_t address, FactionData& outData) {
    if (!address) {
        return false;
    }

    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool GameDataReader::ReadInventory(uintptr_t address, InventoryData& outData) {
    if (!address) {
        return false;
    }

    return Memory::MemoryScanner::ReadMemory(address, outData);
}

bool GameDataReader::WriteCharacterPosition(uintptr_t address, const Vector3& position) {
    if (!address) {
        return false;
    }

    uintptr_t positionAddress = address + Offsets::Character::POSITION;
    return Memory::MemoryScanner::WriteMemory(positionAddress, position);
}

bool GameDataReader::WriteCharacterHealth(uintptr_t address, float health) {
    if (!address) {
        return false;
    }

    uintptr_t healthAddress = address + Offsets::Character::HEALTH;
    return Memory::MemoryScanner::WriteMemory(healthAddress, health);
}

} // namespace Kenshi
} // namespace ReKenshi

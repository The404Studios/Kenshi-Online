/*
 * Pattern Scanner for Kenshi
 * Scans executable memory for byte patterns to find offsets dynamically
 * This allows the mod to work across different game versions
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace Kenshi
{
    //=========================================================================
    // PATTERN DEFINITIONS
    //=========================================================================

    // Pattern signature with mask
    // '?' = wildcard (any byte)
    // 'x' = exact match
    struct PatternSignature
    {
        const char* name;
        const char* pattern;
        const char* mask;
        int32_t offset;          // Offset from pattern start to target
        bool isRelative;         // If true, read relative offset at location
        int32_t relativeBase;    // Base for relative calculation (usually pattern location + instruction size)
    };

    //=========================================================================
    // KNOWN SIGNATURES FOR KENSHI
    //=========================================================================

    // These signatures are based on Kenshi v1.0.x x64
    // Pattern format: hex bytes, '?' for wildcard
    namespace Signatures
    {
        // GameWorld singleton access
        // mov rax, [rip+offset] ; where offset points to g_GameWorld
        constexpr PatternSignature GameWorld = {
            "GameWorld",
            "\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8B\x40\x08",
            "xxx????xxxx?xxxx",
            3,      // Offset to the RIP-relative value
            true,   // Is relative address
            7       // Base = pattern location + 7 (instruction size)
        };

        // Player squad list access
        // lea rcx, [rip+offset] ; player squad array
        constexpr PatternSignature PlayerSquadList = {
            "PlayerSquadList",
            "\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xD8",
            "xxx????x????xxx",
            3,
            true,
            7
        };

        // All characters list
        constexpr PatternSignature AllCharactersList = {
            "AllCharactersList",
            "\x48\x8B\x05\x00\x00\x00\x00\x48\x8B\x04\xC8\x48\x85\xC0",
            "xxx????xxxxxxx",
            3,
            true,
            7
        };

        // Faction manager
        constexpr PatternSignature FactionManager = {
            "FactionManager",
            "\x48\x8B\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xC8\x48\x8B\x10",
            "xxx????x????xxxxxx",
            3,
            true,
            7
        };

        // Weather system
        constexpr PatternSignature WeatherSystem = {
            "WeatherSystem",
            "\x48\x8B\x05\x00\x00\x00\x00\xF3\x0F\x10\x40\x00\xF3\x0F\x58\x05",
            "xxx????xxxx?xxxx",
            3,
            true,
            7
        };

        // Game time getter
        constexpr PatternSignature GameTime = {
            "GameTime",
            "\xF3\x0F\x10\x05\x00\x00\x00\x00\xF3\x0F\x5A\xC0\x48\x8D\x4C\x24",
            "xxxx????xxxxxxxx",
            4,
            true,
            8
        };

        // SpawnCharacter function
        constexpr PatternSignature SpawnCharacter = {
            "SpawnCharacter",
            "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x40\x48\x8B\xF2\x48\x8B\xF9",
            "xxxx?xxxx?xxxxxxxxxxx",
            0,
            false,
            0
        };

        // Character update function (called every tick)
        constexpr PatternSignature CharacterUpdate = {
            "CharacterUpdate",
            "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x48\x89\x74\x24\x00\x57\x41\x56\x41\x57\x48\x83\xEC\x20\x48\x8B\xE9",
            "xxxx?xxxx?xxxx?xxxxxxxxxxxx",
            0,
            false,
            0
        };

        // Character position in struct (used to verify offset)
        // Reads position: movss xmm0, [rcx+70h]
        constexpr PatternSignature CharacterPositionOffset = {
            "CharacterPositionOffset",
            "\xF3\x0F\x10\x41\x00\xF3\x0F\x10\x49\x00\xF3\x0F\x10\x51\x00",
            "xxxx?xxxx?xxxx?",
            4,
            false,
            0
        };

        // Input handler
        constexpr PatternSignature InputHandler = {
            "InputHandler",
            "\x48\x8B\x0D\x00\x00\x00\x00\x48\x8B\x01\xFF\x50\x00\x84\xC0",
            "xxx????xxxxx?xx",
            3,
            true,
            7
        };

        // Camera controller
        constexpr PatternSignature CameraController = {
            "CameraController",
            "\x48\x8B\x05\x00\x00\x00\x00\x48\x8B\x48\x08\xF3\x0F\x10",
            "xxx????xxxxxxx",
            3,
            true,
            7
        };

        // Building manager
        constexpr PatternSignature BuildingManager = {
            "BuildingManager",
            "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x48\x8B\x01\xFF\x90",
            "xxx????xxxx?xxxxx",
            3,
            true,
            7
        };

        // Combat system
        constexpr PatternSignature CombatSystem = {
            "CombatSystem",
            "\x48\x89\x5C\x24\x00\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\x00\x48\x81\xEC\x00\x00\x00\x00\x4C\x8B\xF2",
            "xxxx?xxxxxxxxxxxxxxx?xxx????xxx",
            0,
            false,
            0
        };

        // AI state machine update
        constexpr PatternSignature AIUpdate = {
            "AIUpdate",
            "\x40\x53\x48\x83\xEC\x20\x48\x8B\x41\x00\x48\x8B\xD9\x48\x85\xC0\x74\x00\x48\x8B\x08",
            "xxxxxxxxx?xxxxxxx?xxx",
            0,
            false,
            0
        };

        // Pathfinding request
        constexpr PatternSignature PathfindRequest = {
            "PathfindRequest",
            "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x48\x89\x74\x24\x00\x48\x89\x7C\x24\x00\x41\x56\x48\x83\xEC\x30",
            "xxxx?xxxx?xxxx?xxxx?xxxxxx",
            0,
            false,
            0
        };

        // Inventory add item
        constexpr PatternSignature InventoryAddItem = {
            "InventoryAddItem",
            "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x20\x49\x8B\xF8\x48\x8B\xF2\x48\x8B\xD9",
            "xxxx?xxxx?xxxxxxxxxxxxxx",
            0,
            false,
            0
        };

        // Inventory remove item
        constexpr PatternSignature InventoryRemoveItem = {
            "InventoryRemoveItem",
            "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x30\x8B\xEA",
            "xxxx?xxxx?xxxx?xxxxxxx",
            0,
            false,
            0
        };

        // SetFactionRelation function
        constexpr PatternSignature SetFactionRelation = {
            "SetFactionRelation",
            "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x20\x8B\xFA\x48\x8B\xD9\x85\xD2\x78",
            "xxxx?xxxxxxxxxxxxx",
            0,
            false,
            0
        };

        // Squad management
        constexpr PatternSignature AddToSquad = {
            "AddToSquad",
            "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x20\x48\x8B\xFA\x48\x8B\xF1\x48\x85\xD2",
            "xxxx?xxxx?xxxxxxxxxxxxxx",
            0,
            false,
            0
        };

        // Character state setter
        constexpr PatternSignature SetCharacterState = {
            "SetCharacterState",
            "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x20\x8B\xFA\x48\x8B\xD9\x39\x91",
            "xxxx?xxxxxxxxxxxx",
            0,
            false,
            0
        };

        // Issue movement command
        constexpr PatternSignature IssueMovementCommand = {
            "IssueMovementCommand",
            "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x48\x89\x7C\x24\x00\x55\x41\x54\x41\x55\x41\x56\x41\x57",
            "xxxx?xxxx?xxxx?xxxxxxxxx",
            0,
            false,
            0
        };
    }

    //=========================================================================
    // PATTERN SCANNER CLASS
    //=========================================================================

    class PatternScanner
    {
    public:
        static PatternScanner& Get()
        {
            static PatternScanner instance;
            return instance;
        }

        // Initialize scanner with module base
        bool Initialize(HMODULE module = nullptr);

        // Scan for a single pattern
        uintptr_t FindPattern(const char* pattern, const char* mask);

        // Scan using a signature definition
        uintptr_t FindSignature(const PatternSignature& sig);

        // Scan all known signatures
        bool ScanAllSignatures();

        // Get a found address by name
        uintptr_t GetAddress(const std::string& name) const;

        // Check if all critical addresses were found
        bool AreAllCriticalFound() const;

        // Get module base address
        uintptr_t GetModuleBase() const { return m_moduleBase; }
        uintptr_t GetModuleSize() const { return m_moduleSize; }

        // Export found addresses for debugging
        void DumpAddresses(const std::string& filename) const;

        // Validate found addresses
        bool ValidateAddresses() const;

    private:
        PatternScanner() = default;
        ~PatternScanner() = default;
        PatternScanner(const PatternScanner&) = delete;
        PatternScanner& operator=(const PatternScanner&) = delete;

        // Internal scan implementation
        uintptr_t InternalScan(const uint8_t* pattern, const char* mask, size_t size);

        // Calculate relative address
        uintptr_t CalculateRelativeAddress(uintptr_t instructionAddr, int32_t offset, int32_t instructionSize);

        uintptr_t m_moduleBase = 0;
        uintptr_t m_moduleSize = 0;

        std::unordered_map<std::string, uintptr_t> m_foundAddresses;
    };

    //=========================================================================
    // OFFSET MANAGER
    //=========================================================================

    // Cached offsets after scanning
    struct GameOffsets
    {
        // Global pointers
        uintptr_t gameWorld = 0;
        uintptr_t playerSquadList = 0;
        uintptr_t playerSquadCount = 0;
        uintptr_t allCharactersList = 0;
        uintptr_t allCharactersCount = 0;
        uintptr_t factionManager = 0;
        uintptr_t factionList = 0;
        uintptr_t factionCount = 0;
        uintptr_t weatherSystem = 0;
        uintptr_t gameTime = 0;
        uintptr_t gameDay = 0;
        uintptr_t inputHandler = 0;
        uintptr_t cameraController = 0;
        uintptr_t buildingManager = 0;
        uintptr_t buildingList = 0;
        uintptr_t buildingCount = 0;

        // Functions
        uintptr_t fnSpawnCharacter = 0;
        uintptr_t fnDespawnCharacter = 0;
        uintptr_t fnCharacterUpdate = 0;
        uintptr_t fnAIUpdate = 0;
        uintptr_t fnPathfindRequest = 0;
        uintptr_t fnInventoryAdd = 0;
        uintptr_t fnInventoryRemove = 0;
        uintptr_t fnSetFactionRelation = 0;
        uintptr_t fnAddToSquad = 0;
        uintptr_t fnRemoveFromSquad = 0;
        uintptr_t fnSetCharacterState = 0;
        uintptr_t fnIssueCommand = 0;
        uintptr_t fnCombatAttack = 0;

        // Structure offsets (within Character struct)
        int32_t charPosition = 0x70;
        int32_t charRotation = 0x7C;
        int32_t charHealth = 0xC0;
        int32_t charMaxHealth = 0xC4;
        int32_t charBlood = 0xC8;
        int32_t charHunger = 0xD0;
        int32_t charInventory = 0xF0;
        int32_t charEquipment = 0xF8;
        int32_t charAI = 0x110;
        int32_t charState = 0x118;
        int32_t charFaction = 0x158;
        int32_t charSquad = 0x168;
        int32_t charAnimState = 0x140;
        int32_t charBody = 0xB8;

        // Validation
        bool isValid = false;
    };

    // Global offset access
    class OffsetManager
    {
    public:
        static OffsetManager& Get()
        {
            static OffsetManager instance;
            return instance;
        }

        // Initialize and scan for all offsets
        bool Initialize();

        // Get the current offsets
        const GameOffsets& GetOffsets() const { return m_offsets; }
        GameOffsets& GetOffsets() { return m_offsets; }

        // Check if offsets are valid
        bool IsValid() const { return m_offsets.isValid; }

        // Reload offsets (for game updates)
        bool Reload();

        // Save/Load offset cache
        bool SaveCache(const std::string& filename);
        bool LoadCache(const std::string& filename);

    private:
        OffsetManager() = default;

        GameOffsets m_offsets;
    };

} // namespace Kenshi

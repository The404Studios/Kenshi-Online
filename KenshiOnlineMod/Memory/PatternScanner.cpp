/*
 * Pattern Scanner Implementation
 * Scans executable memory for byte patterns to find offsets dynamically
 */

#include "PatternScanner.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <Psapi.h>

namespace Kenshi
{
    //=========================================================================
    // PATTERN SCANNER IMPLEMENTATION
    //=========================================================================

    bool PatternScanner::Initialize(HMODULE module)
    {
        // Get module handle if not provided
        if (module == nullptr)
        {
            module = GetModuleHandle(NULL);
        }

        if (module == nullptr)
        {
            return false;
        }

        // Get module info
        MODULEINFO moduleInfo;
        if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo)))
        {
            return false;
        }

        m_moduleBase = reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll);
        m_moduleSize = moduleInfo.SizeOfImage;

        return m_moduleBase != 0 && m_moduleSize != 0;
    }

    uintptr_t PatternScanner::FindPattern(const char* pattern, const char* mask)
    {
        if (m_moduleBase == 0 || m_moduleSize == 0)
        {
            return 0;
        }

        size_t patternLength = strlen(mask);
        if (patternLength == 0)
        {
            return 0;
        }

        return InternalScan(
            reinterpret_cast<const uint8_t*>(pattern),
            mask,
            patternLength
        );
    }

    uintptr_t PatternScanner::FindSignature(const PatternSignature& sig)
    {
        uintptr_t patternAddr = FindPattern(sig.pattern, sig.mask);

        if (patternAddr == 0)
        {
            return 0;
        }

        uintptr_t result = patternAddr;

        // Apply offset
        if (sig.offset != 0)
        {
            result += sig.offset;
        }

        // Handle relative addressing
        if (sig.isRelative)
        {
            // Read the relative offset value
            int32_t relativeOffset = *reinterpret_cast<int32_t*>(result);

            // Calculate absolute address: instruction address + instruction size + relative offset
            result = patternAddr + sig.relativeBase + relativeOffset;
        }

        // Store the found address
        m_foundAddresses[sig.name] = result;

        return result;
    }

    bool PatternScanner::ScanAllSignatures()
    {
        if (m_moduleBase == 0)
        {
            return false;
        }

        int foundCount = 0;
        int totalCount = 0;

        // Scan all known signatures
        auto scanSignature = [&](const PatternSignature& sig) {
            totalCount++;
            uintptr_t addr = FindSignature(sig);
            if (addr != 0)
            {
                foundCount++;
                return true;
            }
            return false;
        };

        // Core game state
        scanSignature(Signatures::GameWorld);
        scanSignature(Signatures::PlayerSquadList);
        scanSignature(Signatures::AllCharactersList);
        scanSignature(Signatures::FactionManager);
        scanSignature(Signatures::WeatherSystem);
        scanSignature(Signatures::GameTime);

        // Input and camera
        scanSignature(Signatures::InputHandler);
        scanSignature(Signatures::CameraController);

        // Managers
        scanSignature(Signatures::BuildingManager);

        // Functions
        scanSignature(Signatures::SpawnCharacter);
        scanSignature(Signatures::CharacterUpdate);
        scanSignature(Signatures::AIUpdate);
        scanSignature(Signatures::PathfindRequest);
        scanSignature(Signatures::InventoryAddItem);
        scanSignature(Signatures::InventoryRemoveItem);
        scanSignature(Signatures::SetFactionRelation);
        scanSignature(Signatures::AddToSquad);
        scanSignature(Signatures::SetCharacterState);
        scanSignature(Signatures::IssueMovementCommand);

        // Combat
        scanSignature(Signatures::CombatSystem);

        // Structure offset verification
        scanSignature(Signatures::CharacterPositionOffset);

        return foundCount > 0;
    }

    uintptr_t PatternScanner::GetAddress(const std::string& name) const
    {
        auto it = m_foundAddresses.find(name);
        if (it != m_foundAddresses.end())
        {
            return it->second;
        }
        return 0;
    }

    bool PatternScanner::AreAllCriticalFound() const
    {
        // Critical addresses needed for basic functionality
        static const char* criticalNames[] = {
            "GameWorld",
            "PlayerSquadList",
            "CharacterUpdate"
        };

        for (const char* name : criticalNames)
        {
            if (GetAddress(name) == 0)
            {
                return false;
            }
        }

        return true;
    }

    void PatternScanner::DumpAddresses(const std::string& filename) const
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            return;
        }

        file << "// Kenshi Memory Addresses - Auto-generated by PatternScanner\n";
        file << "// Module Base: 0x" << std::hex << m_moduleBase << "\n";
        file << "// Module Size: 0x" << m_moduleSize << "\n\n";

        for (const auto& pair : m_foundAddresses)
        {
            uintptr_t relativeAddr = pair.second - m_moduleBase;
            file << pair.first << " = 0x" << std::hex << pair.second;
            file << " (RVA: 0x" << relativeAddr << ")\n";
        }

        file.close();
    }

    bool PatternScanner::ValidateAddresses() const
    {
        // Validate by checking if addresses point to valid memory
        for (const auto& pair : m_foundAddresses)
        {
            __try
            {
                volatile uint8_t test = *reinterpret_cast<uint8_t*>(pair.second);
                (void)test;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        return true;
    }

    uintptr_t PatternScanner::InternalScan(const uint8_t* pattern, const char* mask, size_t size)
    {
        const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(m_moduleBase);
        const uint8_t* scanEnd = scanStart + m_moduleSize - size;

        for (const uint8_t* current = scanStart; current < scanEnd; ++current)
        {
            bool found = true;

            for (size_t i = 0; i < size; ++i)
            {
                if (mask[i] == 'x' && current[i] != pattern[i])
                {
                    found = false;
                    break;
                }
            }

            if (found)
            {
                return reinterpret_cast<uintptr_t>(current);
            }
        }

        return 0;
    }

    uintptr_t PatternScanner::CalculateRelativeAddress(uintptr_t instructionAddr, int32_t offset, int32_t instructionSize)
    {
        // For x64: absolute address = instruction address + instruction size + relative offset
        return instructionAddr + instructionSize + offset;
    }

    //=========================================================================
    // OFFSET MANAGER IMPLEMENTATION
    //=========================================================================

    bool OffsetManager::Initialize()
    {
        PatternScanner& scanner = PatternScanner::Get();

        // Initialize the scanner
        if (!scanner.Initialize())
        {
            return false;
        }

        // Try to load from cache first
        if (LoadCache("kenshi_offsets.cache"))
        {
            // Validate cached offsets
            if (scanner.ValidateAddresses())
            {
                m_offsets.isValid = true;
                return true;
            }
        }

        // Scan for all signatures
        if (!scanner.ScanAllSignatures())
        {
            return false;
        }

        // Populate offsets structure
        m_offsets.gameWorld = scanner.GetAddress("GameWorld");
        m_offsets.playerSquadList = scanner.GetAddress("PlayerSquadList");
        m_offsets.allCharactersList = scanner.GetAddress("AllCharactersList");
        m_offsets.factionManager = scanner.GetAddress("FactionManager");
        m_offsets.weatherSystem = scanner.GetAddress("WeatherSystem");
        m_offsets.gameTime = scanner.GetAddress("GameTime");
        m_offsets.inputHandler = scanner.GetAddress("InputHandler");
        m_offsets.cameraController = scanner.GetAddress("CameraController");
        m_offsets.buildingManager = scanner.GetAddress("BuildingManager");

        // Functions
        m_offsets.fnSpawnCharacter = scanner.GetAddress("SpawnCharacter");
        m_offsets.fnCharacterUpdate = scanner.GetAddress("CharacterUpdate");
        m_offsets.fnAIUpdate = scanner.GetAddress("AIUpdate");
        m_offsets.fnPathfindRequest = scanner.GetAddress("PathfindRequest");
        m_offsets.fnInventoryAdd = scanner.GetAddress("InventoryAddItem");
        m_offsets.fnInventoryRemove = scanner.GetAddress("InventoryRemoveItem");
        m_offsets.fnSetFactionRelation = scanner.GetAddress("SetFactionRelation");
        m_offsets.fnAddToSquad = scanner.GetAddress("AddToSquad");
        m_offsets.fnSetCharacterState = scanner.GetAddress("SetCharacterState");
        m_offsets.fnIssueCommand = scanner.GetAddress("IssueMovementCommand");
        m_offsets.fnCombatAttack = scanner.GetAddress("CombatSystem");

        // Verify structure offsets from pattern
        uintptr_t posOffsetPattern = scanner.GetAddress("CharacterPositionOffset");
        if (posOffsetPattern != 0)
        {
            // The pattern contains the offset byte - extract it
            uint8_t extractedOffset = *reinterpret_cast<uint8_t*>(posOffsetPattern);
            if (extractedOffset != 0)
            {
                m_offsets.charPosition = extractedOffset;
            }
        }

        // Check if critical addresses were found
        if (!scanner.AreAllCriticalFound())
        {
            // Fall back to hardcoded offsets for known versions
            FallbackToHardcodedOffsets();
        }

        m_offsets.isValid = true;

        // Save cache for future use
        SaveCache("kenshi_offsets.cache");

        // Dump addresses for debugging
        scanner.DumpAddresses("kenshi_addresses.txt");

        return true;
    }

    bool OffsetManager::Reload()
    {
        m_offsets = GameOffsets();
        return Initialize();
    }

    bool OffsetManager::SaveCache(const std::string& filename)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        // Write version marker
        uint32_t version = 1;
        file.write(reinterpret_cast<char*>(&version), sizeof(version));

        // Write module base for validation
        uintptr_t moduleBase = PatternScanner::Get().GetModuleBase();
        file.write(reinterpret_cast<char*>(&moduleBase), sizeof(moduleBase));

        // Write offsets structure
        file.write(reinterpret_cast<char*>(&m_offsets), sizeof(m_offsets));

        file.close();
        return true;
    }

    bool OffsetManager::LoadCache(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        // Read version
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1)
        {
            return false;
        }

        // Read and validate module base
        uintptr_t cachedBase;
        file.read(reinterpret_cast<char*>(&cachedBase), sizeof(cachedBase));

        uintptr_t currentBase = PatternScanner::Get().GetModuleBase();
        if (cachedBase != currentBase)
        {
            // Module relocated, cache invalid
            return false;
        }

        // Read offsets
        file.read(reinterpret_cast<char*>(&m_offsets), sizeof(m_offsets));

        file.close();
        return m_offsets.isValid;
    }

    // Fallback hardcoded offsets for known Kenshi versions
    void FallbackToHardcodedOffsets()
    {
        GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        uintptr_t base = PatternScanner::Get().GetModuleBase();

        // Kenshi v1.0.x (64-bit) offsets
        // These are relative to module base

        if (offsets.gameWorld == 0)
            offsets.gameWorld = base + 0x24D8F40;

        if (offsets.playerSquadList == 0)
            offsets.playerSquadList = base + 0x24C5A20;

        if (offsets.playerSquadCount == 0)
            offsets.playerSquadCount = base + 0x24C5A28;

        if (offsets.allCharactersList == 0)
            offsets.allCharactersList = base + 0x24C5B00;

        if (offsets.allCharactersCount == 0)
            offsets.allCharactersCount = base + 0x24C5B08;

        if (offsets.factionList == 0)
            offsets.factionList = base + 0x24D2100;

        if (offsets.factionCount == 0)
            offsets.factionCount = base + 0x24D2108;

        if (offsets.weatherSystem == 0)
            offsets.weatherSystem = base + 0x24E7000;

        if (offsets.gameTime == 0)
            offsets.gameTime = base + 0x24D8F50;

        if (offsets.gameDay == 0)
            offsets.gameDay = base + 0x24D8F58;

        if (offsets.inputHandler == 0)
            offsets.inputHandler = base + 0x24F2D80;

        if (offsets.cameraController == 0)
            offsets.cameraController = base + 0x24E7C20;

        if (offsets.buildingList == 0)
            offsets.buildingList = base + 0x24E1000;

        if (offsets.buildingCount == 0)
            offsets.buildingCount = base + 0x24E1008;

        // Function offsets
        if (offsets.fnSpawnCharacter == 0)
            offsets.fnSpawnCharacter = base + 0x8B3C80;

        if (offsets.fnSetCharacterState == 0)
            offsets.fnSetCharacterState = base + 0x8C1000;

        if (offsets.fnIssueCommand == 0)
            offsets.fnIssueCommand = base + 0x8D5000;

        if (offsets.fnInventoryAdd == 0)
            offsets.fnInventoryAdd = base + 0x9C2100;

        if (offsets.fnInventoryRemove == 0)
            offsets.fnInventoryRemove = base + 0x9C2200;

        if (offsets.fnSetFactionRelation == 0)
            offsets.fnSetFactionRelation = base + 0x7A2500;

        if (offsets.fnAddToSquad == 0)
            offsets.fnAddToSquad = base + 0x8B4500;
    }

} // namespace Kenshi

#pragma once

#include "KenshiStructures.h"
#include "KenshiAdvancedStructures.h"
#include "PatternResolver.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <any>

namespace ReKenshi {
namespace Patterns {

/**
 * Interpreted data result
 */
struct InterpretedData {
    std::string patternName;
    std::string dataType;
    std::any data;
    bool success;
    std::string error;
};

/**
 * Data reader function type
 */
using DataReaderFunc = std::function<InterpretedData(uintptr_t address)>;

/**
 * Pattern Interpreter
 * Interprets resolved patterns and reads/writes game structures
 */
class PatternInterpreter {
public:
    static PatternInterpreter& GetInstance();

    // Initialize with resolver
    void Initialize(PatternResolver* resolver);

    // Interpret patterns
    InterpretedData InterpretPattern(const std::string& patternName);
    std::vector<InterpretedData> InterpretCategory(const std::string& category);
    std::vector<InterpretedData> InterpretAll();

    // Read game structures
    bool ReadCharacterData(const std::string& patternName, Kenshi::CharacterData& outData);
    bool ReadWorldState(const std::string& patternName, Kenshi::WorldStateData& outData);
    bool ReadSquadData(const std::string& patternName, Kenshi::SquadData& outData);
    bool ReadBuildingData(const std::string& patternName, Kenshi::BuildingData& outData);
    bool ReadNPCData(const std::string& patternName, Kenshi::NPCData& outData);

    // Read primitive types
    template<typename T>
    bool ReadValue(const std::string& patternName, T& outValue) {
        if (!m_resolver) {
            return false;
        }

        uintptr_t address = m_resolver->GetResolvedAddress(patternName);
        if (!address) {
            return false;
        }

        return Memory::MemoryScanner::ReadMemory(address, outValue);
    }

    // List operations (for arrays/lists in game)
    std::vector<uintptr_t> ReadPointerList(const std::string& patternName, size_t maxCount = 100);
    std::vector<Kenshi::CharacterData> ReadCharacterList(const std::string& patternName, size_t maxCount = 100);

    // Register custom data readers
    void RegisterDataReader(const std::string& patternName, DataReaderFunc reader);

    // Auto-detection of data types
    std::string DetectDataType(uintptr_t address);

    // Statistics
    struct Statistics {
        uint32_t interpretationsAttempted = 0;
        uint32_t interpretationsSucceeded = 0;
        uint32_t interpretationsFailed = 0;
        uint32_t structuresRead = 0;
    };

    const Statistics& GetStatistics() const { return m_stats; }

private:
    PatternInterpreter() = default;
    ~PatternInterpreter() = default;
    PatternInterpreter(const PatternInterpreter&) = delete;
    PatternInterpreter& operator=(const PatternInterpreter&) = delete;

    // Helper functions
    InterpretedData InterpretAsCharacter(uintptr_t address);
    InterpretedData InterpretAsWorldState(uintptr_t address);
    InterpretedData InterpretAsSquad(uintptr_t address);
    InterpretedData InterpretAsBuilding(uintptr_t address);

    // Determine interpretation strategy based on pattern name
    DataReaderFunc GetDefaultReader(const std::string& patternName);

    // Dependencies
    PatternResolver* m_resolver = nullptr;

    // Custom readers
    std::unordered_map<std::string, DataReaderFunc> m_customReaders;

    // Statistics
    Statistics m_stats;
};

} // namespace Patterns
} // namespace ReKenshi

#include "PatternInterpreter.h"
#include "Logger.h"
#include "Utilities.h"

namespace ReKenshi {
namespace Patterns {

PatternInterpreter& PatternInterpreter::GetInstance() {
    static PatternInterpreter instance;
    return instance;
}

void PatternInterpreter::Initialize(PatternResolver* resolver) {
    LOG_INFO("Initializing PatternInterpreter");
    m_resolver = resolver;
}

InterpretedData PatternInterpreter::InterpretPattern(const std::string& patternName) {
    m_stats.interpretationsAttempted++;

    if (!m_resolver) {
        InterpretedData result;
        result.patternName = patternName;
        result.success = false;
        result.error = "Resolver not initialized";
        m_stats.interpretationsFailed++;
        return result;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        InterpretedData result;
        result.patternName = patternName;
        result.success = false;
        result.error = "Pattern not resolved";
        m_stats.interpretationsFailed++;
        return result;
    }

    // Check for custom reader first
    auto customIt = m_customReaders.find(patternName);
    if (customIt != m_customReaders.end()) {
        InterpretedData result = customIt->second(address);
        if (result.success) {
            m_stats.interpretationsSucceeded++;
        } else {
            m_stats.interpretationsFailed++;
        }
        return result;
    }

    // Use default reader based on pattern name
    DataReaderFunc defaultReader = GetDefaultReader(patternName);
    if (defaultReader) {
        InterpretedData result = defaultReader(address);
        if (result.success) {
            m_stats.interpretationsSucceeded++;
        } else {
            m_stats.interpretationsFailed++;
        }
        return result;
    }

    // No specific reader found, return generic success
    InterpretedData result;
    result.patternName = patternName;
    result.dataType = "pointer";
    result.data = address;
    result.success = true;
    m_stats.interpretationsSucceeded++;

    return result;
}

std::vector<InterpretedData> PatternInterpreter::InterpretCategory(const std::string& category) {
    std::vector<InterpretedData> results;

    if (!m_resolver) {
        LOG_ERROR("Resolver not initialized");
        return results;
    }

    auto resolvedPatterns = m_resolver->GetResolvedPatternsInCategory(category);

    for (const auto& resolved : resolvedPatterns) {
        results.push_back(InterpretPattern(resolved.name));
    }

    return results;
}

std::vector<InterpretedData> PatternInterpreter::InterpretAll() {
    std::vector<InterpretedData> results;

    if (!m_resolver) {
        LOG_ERROR("Resolver not initialized");
        return results;
    }

    auto resolvedPatterns = m_resolver->GetAllResolvedPatterns();

    for (const auto& resolved : resolvedPatterns) {
        results.push_back(InterpretPattern(resolved.name));
    }

    return results;
}

bool PatternInterpreter::ReadCharacterData(const std::string& patternName, Kenshi::CharacterData& outData) {
    if (!m_resolver) {
        return false;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        return false;
    }

    m_stats.structuresRead++;
    return Kenshi::GameDataReader::ReadCharacter(address, outData);
}

bool PatternInterpreter::ReadWorldState(const std::string& patternName, Kenshi::WorldStateData& outData) {
    if (!m_resolver) {
        return false;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        return false;
    }

    m_stats.structuresRead++;
    return Kenshi::GameDataReader::ReadWorldState(address, outData);
}

bool PatternInterpreter::ReadSquadData(const std::string& patternName, Kenshi::SquadData& outData) {
    if (!m_resolver) {
        return false;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        return false;
    }

    m_stats.structuresRead++;
    return Kenshi::GameDataReader::ReadSquad(address, outData);
}

bool PatternInterpreter::ReadBuildingData(const std::string& patternName, Kenshi::BuildingData& outData) {
    if (!m_resolver) {
        return false;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        return false;
    }

    m_stats.structuresRead++;
    return Kenshi::AdvancedGameDataReader::ReadBuilding(address, outData);
}

bool PatternInterpreter::ReadNPCData(const std::string& patternName, Kenshi::NPCData& outData) {
    if (!m_resolver) {
        return false;
    }

    uintptr_t address = m_resolver->GetResolvedAddress(patternName);
    if (!address) {
        return false;
    }

    m_stats.structuresRead++;
    return Kenshi::AdvancedGameDataReader::ReadNPC(address, outData);
}

std::vector<uintptr_t> PatternInterpreter::ReadPointerList(const std::string& patternName, size_t maxCount) {
    std::vector<uintptr_t> pointers;

    if (!m_resolver) {
        return pointers;
    }

    uintptr_t listAddress = m_resolver->GetResolvedAddress(patternName);
    if (!listAddress) {
        return pointers;
    }

    // TODO: Implement list traversal
    // This requires understanding the list structure
    // For now, return empty vector

    LOG_TRACE_F("ReadPointerList called for: %s", patternName.c_str());

    return pointers;
}

std::vector<Kenshi::CharacterData> PatternInterpreter::ReadCharacterList(const std::string& patternName, size_t maxCount) {
    std::vector<Kenshi::CharacterData> characters;

    auto pointers = ReadPointerList(patternName, maxCount);

    for (uintptr_t ptr : pointers) {
        Kenshi::CharacterData data;
        if (Kenshi::GameDataReader::ReadCharacter(ptr, data)) {
            characters.push_back(data);
        }
    }

    return characters;
}

void PatternInterpreter::RegisterDataReader(const std::string& patternName, DataReaderFunc reader) {
    m_customReaders[patternName] = reader;
    LOG_DEBUG_F("Registered custom data reader for: %s", patternName.c_str());
}

std::string PatternInterpreter::DetectDataType(uintptr_t address) {
    // Simple heuristic-based detection
    // In practice, would need more sophisticated analysis

    if (!Utils::MemoryUtils::IsValidPointer(address)) {
        return "invalid";
    }

    // TODO: Implement more sophisticated type detection
    return "unknown";
}

InterpretedData PatternInterpreter::InterpretAsCharacter(uintptr_t address) {
    InterpretedData result;
    result.dataType = "CharacterData";

    Kenshi::CharacterData data;
    if (Kenshi::GameDataReader::ReadCharacter(address, data)) {
        result.data = data;
        result.success = true;
    } else {
        result.success = false;
        result.error = "Failed to read character data";
    }

    return result;
}

InterpretedData PatternInterpreter::InterpretAsWorldState(uintptr_t address) {
    InterpretedData result;
    result.dataType = "WorldStateData";

    Kenshi::WorldStateData data;
    if (Kenshi::GameDataReader::ReadWorldState(address, data)) {
        result.data = data;
        result.success = true;
    } else {
        result.success = false;
        result.error = "Failed to read world state data";
    }

    return result;
}

InterpretedData PatternInterpreter::InterpretAsSquad(uintptr_t address) {
    InterpretedData result;
    result.dataType = "SquadData";

    Kenshi::SquadData data;
    if (Kenshi::GameDataReader::ReadSquad(address, data)) {
        result.data = data;
        result.success = true;
    } else {
        result.success = false;
        result.error = "Failed to read squad data";
    }

    return result;
}

InterpretedData PatternInterpreter::InterpretAsBuilding(uintptr_t address) {
    InterpretedData result;
    result.dataType = "BuildingData";

    Kenshi::BuildingData data;
    if (Kenshi::AdvancedGameDataReader::ReadBuilding(address, data)) {
        result.data = data;
        result.success = true;
    } else {
        result.success = false;
        result.error = "Failed to read building data";
    }

    return result;
}

DataReaderFunc PatternInterpreter::GetDefaultReader(const std::string& patternName) {
    // Map pattern names to appropriate readers
    using namespace std::placeholders;

    if (patternName.find("Character") != std::string::npos ||
        patternName.find("Player") != std::string::npos) {
        return std::bind(&PatternInterpreter::InterpretAsCharacter, this, _1);
    }

    if (patternName.find("World") != std::string::npos ||
        patternName.find("State") != std::string::npos) {
        return std::bind(&PatternInterpreter::InterpretAsWorldState, this, _1);
    }

    if (patternName.find("Squad") != std::string::npos) {
        return std::bind(&PatternInterpreter::InterpretAsSquad, this, _1);
    }

    if (patternName.find("Building") != std::string::npos) {
        return std::bind(&PatternInterpreter::InterpretAsBuilding, this, _1);
    }

    return nullptr;
}

} // namespace Patterns
} // namespace ReKenshi

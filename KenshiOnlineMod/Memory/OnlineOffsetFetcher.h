/*
 * Online Offset Fetcher for Kenshi
 * Downloads offset database from online sources for dynamic configuration
 * Supports multiple fallback servers and local caching
 */

#pragma once

#include <Windows.h>
#include <winhttp.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>

#pragma comment(lib, "winhttp.lib")

namespace Kenshi
{
    //=========================================================================
    // ONLINE OFFSET DATA STRUCTURES
    //=========================================================================

    struct OnlineGameOffsets
    {
        // Base
        uint64_t baseAddress = 0x140000000;

        // Game Core
        uint64_t worldInstance = 0;
        uint64_t gameState = 0;
        uint64_t gameTime = 0;
        uint64_t gameDay = 0;

        // Characters
        uint64_t playerSquadList = 0;
        uint64_t playerSquadCount = 0;
        uint64_t allCharactersList = 0;
        uint64_t allCharactersCount = 0;
        uint64_t selectedCharacter = 0;

        // Factions
        uint64_t factionList = 0;
        uint64_t factionCount = 0;
        uint64_t playerFaction = 0;
        uint64_t relationMatrix = 0;

        // World
        uint64_t buildingList = 0;
        uint64_t buildingCount = 0;
        uint64_t worldItemsList = 0;
        uint64_t worldItemsCount = 0;
        uint64_t weatherSystem = 0;

        // Engine
        uint64_t physicsWorld = 0;
        uint64_t camera = 0;
        uint64_t cameraTarget = 0;
        uint64_t renderer = 0;

        // Input
        uint64_t inputHandler = 0;
        uint64_t commandQueue = 0;
        uint64_t selectedUnits = 0;
        uint64_t uiState = 0;
    };

    struct OnlineFunctionOffsets
    {
        uint64_t spawnCharacter = 0;
        uint64_t despawnCharacter = 0;
        uint64_t addToSquad = 0;
        uint64_t removeFromSquad = 0;
        uint64_t addItemToInventory = 0;
        uint64_t removeItemFromInventory = 0;
        uint64_t setCharacterState = 0;
        uint64_t issueCommand = 0;
        uint64_t createFaction = 0;
        uint64_t setFactionRelation = 0;
        uint64_t pathfindRequest = 0;
        uint64_t combatAttack = 0;
        uint64_t characterUpdate = 0;
        uint64_t aiUpdate = 0;
    };

    struct OnlineStructureOffsets
    {
        // Character
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

        // Squad
        int32_t squadMembers = 0x20;
        int32_t squadMemberCount = 0x28;
        int32_t squadLeader = 0x30;
        int32_t squadFactionId = 0x38;

        // Faction
        int32_t factionRelations = 0x20;
        int32_t factionMembers = 0x30;
        int32_t factionLeader = 0x40;

        // Item
        int32_t itemName = 0x10;
        int32_t itemCategory = 0x18;
        int32_t itemValue = 0x20;
        int32_t itemWeight = 0x24;
        int32_t itemStackCount = 0x28;
    };

    struct OnlinePatternDef
    {
        std::string name;
        std::string pattern;
        std::string mask;
        int32_t offset = 0;
        bool isRelative = false;
        int32_t relativeBase = 0;
    };

    struct OnlineOffsetDatabase
    {
        int schemaVersion = 1;
        std::string gameVersion;
        bool isUniversal = false;
        std::string lastUpdated;
        std::string checksum;
        std::string author;
        std::string notes;

        OnlineGameOffsets gameOffsets;
        OnlineFunctionOffsets functionOffsets;
        OnlineStructureOffsets structureOffsets;
        std::unordered_map<std::string, OnlinePatternDef> patterns;

        std::vector<std::string> supportedVersions;

        bool isValid = false;
    };

    //=========================================================================
    // ONLINE OFFSET FETCHER CLASS
    //=========================================================================

    class OnlineOffsetFetcher
    {
    public:
        static OnlineOffsetFetcher& Get()
        {
            static OnlineOffsetFetcher instance;
            return instance;
        }

        // Initialize with optional custom server URL
        bool Initialize(const std::string& customServerUrl = "");

        // Fetch offsets from online sources
        bool FetchOffsets(const std::string& gameVersion = "");

        // Get the loaded offset database
        const OnlineOffsetDatabase& GetDatabase() const { return m_database; }

        // Check if offsets are loaded
        bool IsLoaded() const { return m_database.isValid; }

        // Get specific offsets
        const OnlineGameOffsets& GetGameOffsets() const { return m_database.gameOffsets; }
        const OnlineFunctionOffsets& GetFunctionOffsets() const { return m_database.functionOffsets; }
        const OnlineStructureOffsets& GetStructureOffsets() const { return m_database.structureOffsets; }

        // Save/Load local cache
        bool SaveCache(const std::string& filename = "kenshi_offsets_cache.dat");
        bool LoadCache(const std::string& filename = "kenshi_offsets_cache.dat");

        // Clear cache and force re-fetch
        bool RefreshOffsets();

        // Add custom server URL (inserted at beginning of list)
        void AddServer(const std::string& url);

        // Set callback for offset load events
        using LoadCallback = std::function<void(bool success, const std::string& message)>;
        void SetLoadCallback(LoadCallback callback) { m_loadCallback = callback; }

        // Export current offsets as JSON
        std::string ExportAsJson() const;

    private:
        OnlineOffsetFetcher();
        ~OnlineOffsetFetcher() = default;
        OnlineOffsetFetcher(const OnlineOffsetFetcher&) = delete;
        OnlineOffsetFetcher& operator=(const OnlineOffsetFetcher&) = delete;

        // HTTP fetch implementation
        std::optional<std::string> FetchUrl(const std::string& url);

        // JSON parsing
        bool ParseJson(const std::string& json);
        bool ParseGameOffsets(const std::string& json);
        bool ParseFunctionOffsets(const std::string& json);
        bool ParseStructureOffsets(const std::string& json);
        bool ParsePatterns(const std::string& json);

        // Validation
        bool ValidateOffsets();
        std::string CalculateChecksum();

        // Helper functions
        static std::string ExtractJsonValue(const std::string& json, const std::string& key);
        static int64_t ExtractJsonNumber(const std::string& json, const std::string& key);
        static bool ExtractJsonBool(const std::string& json, const std::string& key);
        static std::string ExtractJsonObject(const std::string& json, const std::string& key);
        static std::string ExtractJsonArray(const std::string& json, const std::string& key);

        // Server URLs
        std::vector<std::string> m_serverUrls;

        // Loaded database
        OnlineOffsetDatabase m_database;

        // Callback
        LoadCallback m_loadCallback;

        // State
        bool m_initialized = false;
    };

    //=========================================================================
    // INTEGRATED OFFSET MANAGER (combines online + pattern scan + hardcoded)
    //=========================================================================

    class IntegratedOffsetManager
    {
    public:
        static IntegratedOffsetManager& Get()
        {
            static IntegratedOffsetManager instance;
            return instance;
        }

        // Initialize with priority: Online -> Pattern Scan -> Hardcoded
        bool Initialize(HMODULE gameModule = nullptr);

        // Get offset by category and name
        uint64_t GetOffset(const std::string& category, const std::string& name) const;

        // Get absolute address (moduleBase + offset)
        uint64_t GetAbsolute(const std::string& category, const std::string& name) const;

        // Get function address
        uint64_t GetFunction(const std::string& name) const;

        // Get structure offset
        int32_t GetStructureOffset(const std::string& structure, const std::string& field) const;

        // Check initialization status
        bool IsOnlineLoaded() const { return m_onlineLoaded; }
        bool IsPatternScanned() const { return m_patternScanned; }
        bool IsInitialized() const { return m_initialized; }

        // Get current source
        enum class OffsetSource { Online, PatternScan, Hardcoded };
        OffsetSource GetCurrentSource() const { return m_currentSource; }

        // Force specific source
        void SetSourcePriority(bool preferOnline, bool allowPatternScan);

        // Reload offsets
        bool Reload();

    private:
        IntegratedOffsetManager() = default;

        // Apply online offsets to internal storage
        void ApplyOnlineOffsets();

        // Apply pattern-scanned offsets
        void ApplyPatternOffsets();

        // Apply hardcoded fallback
        void ApplyHardcodedOffsets();

        // Module info
        uintptr_t m_moduleBase = 0;
        uintptr_t m_moduleSize = 0;

        // State
        bool m_initialized = false;
        bool m_onlineLoaded = false;
        bool m_patternScanned = false;
        OffsetSource m_currentSource = OffsetSource::Hardcoded;

        // Preferences
        bool m_preferOnline = true;
        bool m_allowPatternScan = true;

        // Storage
        std::unordered_map<std::string, uint64_t> m_offsets;
        std::unordered_map<std::string, uint64_t> m_functions;
        std::unordered_map<std::string, int32_t> m_structureOffsets;
    };

} // namespace Kenshi

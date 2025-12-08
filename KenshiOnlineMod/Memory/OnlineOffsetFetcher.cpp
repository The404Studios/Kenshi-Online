/*
 * Online Offset Fetcher Implementation
 * Downloads and parses offset database from online sources
 */

#include "OnlineOffsetFetcher.h"
#include "PatternScanner.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>

namespace Kenshi
{
    //=========================================================================
    // ONLINE OFFSET FETCHER IMPLEMENTATION
    //=========================================================================

    OnlineOffsetFetcher::OnlineOffsetFetcher()
    {
        // Default server URLs
        m_serverUrls = {
            "https://raw.githubusercontent.com/The404Studios/Kenshi-Online/main/offsets/kenshi_offsets.json",
            "https://kenshi-online.the404studios.com/api/offsets"
        };
    }

    bool OnlineOffsetFetcher::Initialize(const std::string& customServerUrl)
    {
        if (!customServerUrl.empty())
        {
            AddServer(customServerUrl);
        }

        m_initialized = true;
        return true;
    }

    bool OnlineOffsetFetcher::FetchOffsets(const std::string& gameVersion)
    {
        // Try to load from cache first
        if (LoadCache())
        {
            // Validate cached version matches
            if (m_database.isUniversal || m_database.gameVersion == gameVersion || gameVersion.empty())
            {
                if (m_loadCallback)
                    m_loadCallback(true, "Loaded offsets from cache");
                return true;
            }
        }

        // Try each server
        for (const auto& url : m_serverUrls)
        {
            std::string fetchUrl = url;
            if (!gameVersion.empty() && url.find('?') == std::string::npos)
            {
                fetchUrl += "?version=" + gameVersion;
            }

            auto response = FetchUrl(fetchUrl);
            if (response.has_value())
            {
                if (ParseJson(response.value()))
                {
                    if (ValidateOffsets())
                    {
                        m_database.isValid = true;
                        SaveCache();

                        if (m_loadCallback)
                            m_loadCallback(true, "Loaded offsets from " + url);

                        return true;
                    }
                }
            }
        }

        if (m_loadCallback)
            m_loadCallback(false, "Failed to fetch offsets from all sources");

        return false;
    }

    std::optional<std::string> OnlineOffsetFetcher::FetchUrl(const std::string& url)
    {
        std::string result;

        // Parse URL components
        std::string host, path;
        bool useHttps = false;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;

        if (url.substr(0, 8) == "https://")
        {
            useHttps = true;
            port = INTERNET_DEFAULT_HTTPS_PORT;
            size_t pathStart = url.find('/', 8);
            if (pathStart != std::string::npos)
            {
                host = url.substr(8, pathStart - 8);
                path = url.substr(pathStart);
            }
            else
            {
                host = url.substr(8);
                path = "/";
            }
        }
        else if (url.substr(0, 7) == "http://")
        {
            size_t pathStart = url.find('/', 7);
            if (pathStart != std::string::npos)
            {
                host = url.substr(7, pathStart - 7);
                path = url.substr(pathStart);
            }
            else
            {
                host = url.substr(7);
                path = "/";
            }
        }
        else
        {
            return std::nullopt;
        }

        // Convert to wide strings
        std::wstring wHost(host.begin(), host.end());
        std::wstring wPath(path.begin(), path.end());

        // Create session
        HINTERNET hSession = WinHttpOpen(
            L"KenshiOnline/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

        if (!hSession)
            return std::nullopt;

        // Connect
        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return std::nullopt;
        }

        // Create request
        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            wPath.c_str(),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags
        );

        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return std::nullopt;
        }

        // Send request
        BOOL bResults = WinHttpSendRequest(
            hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        );

        if (bResults)
        {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }

        if (bResults)
        {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;

            do
            {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                    break;

                if (dwSize == 0)
                    break;

                std::vector<char> buffer(dwSize + 1, 0);

                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded))
                    break;

                result.append(buffer.data(), dwDownloaded);

            } while (dwSize > 0);
        }

        // Cleanup
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (result.empty())
            return std::nullopt;

        return result;
    }

    bool OnlineOffsetFetcher::ParseJson(const std::string& json)
    {
        // Extract metadata
        m_database.schemaVersion = static_cast<int>(ExtractJsonNumber(json, "version"));
        m_database.gameVersion = ExtractJsonValue(json, "gameVersion");
        m_database.isUniversal = ExtractJsonBool(json, "isUniversal");
        m_database.lastUpdated = ExtractJsonValue(json, "lastUpdated");
        m_database.checksum = ExtractJsonValue(json, "checksum");
        m_database.author = ExtractJsonValue(json, "author");
        m_database.notes = ExtractJsonValue(json, "notes");

        // Parse offsets
        std::string offsetsJson = ExtractJsonObject(json, "offsets");
        if (!offsetsJson.empty())
        {
            ParseGameOffsets(offsetsJson);
        }

        // Parse function offsets
        std::string functionsJson = ExtractJsonObject(json, "functionOffsets");
        if (!functionsJson.empty())
        {
            ParseFunctionOffsets(functionsJson);
        }

        // Parse structure offsets
        std::string structuresJson = ExtractJsonObject(json, "structureOffsets");
        if (!structuresJson.empty())
        {
            ParseStructureOffsets(structuresJson);
        }

        // Parse patterns
        std::string patternsJson = ExtractJsonObject(json, "patterns");
        if (!patternsJson.empty())
        {
            ParsePatterns(patternsJson);
        }

        return true;
    }

    bool OnlineOffsetFetcher::ParseGameOffsets(const std::string& json)
    {
        auto& o = m_database.gameOffsets;

        o.baseAddress = static_cast<uint64_t>(ExtractJsonNumber(json, "baseAddress"));
        o.worldInstance = static_cast<uint64_t>(ExtractJsonNumber(json, "worldInstance"));
        o.gameState = static_cast<uint64_t>(ExtractJsonNumber(json, "gameState"));
        o.gameTime = static_cast<uint64_t>(ExtractJsonNumber(json, "gameTime"));
        o.gameDay = static_cast<uint64_t>(ExtractJsonNumber(json, "gameDay"));

        o.playerSquadList = static_cast<uint64_t>(ExtractJsonNumber(json, "playerSquadList"));
        o.playerSquadCount = static_cast<uint64_t>(ExtractJsonNumber(json, "playerSquadCount"));
        o.allCharactersList = static_cast<uint64_t>(ExtractJsonNumber(json, "allCharactersList"));
        o.allCharactersCount = static_cast<uint64_t>(ExtractJsonNumber(json, "allCharactersCount"));
        o.selectedCharacter = static_cast<uint64_t>(ExtractJsonNumber(json, "selectedCharacter"));

        o.factionList = static_cast<uint64_t>(ExtractJsonNumber(json, "factionList"));
        o.factionCount = static_cast<uint64_t>(ExtractJsonNumber(json, "factionCount"));
        o.playerFaction = static_cast<uint64_t>(ExtractJsonNumber(json, "playerFaction"));
        o.relationMatrix = static_cast<uint64_t>(ExtractJsonNumber(json, "relationMatrix"));

        o.buildingList = static_cast<uint64_t>(ExtractJsonNumber(json, "buildingList"));
        o.buildingCount = static_cast<uint64_t>(ExtractJsonNumber(json, "buildingCount"));
        o.worldItemsList = static_cast<uint64_t>(ExtractJsonNumber(json, "worldItemsList"));
        o.worldItemsCount = static_cast<uint64_t>(ExtractJsonNumber(json, "worldItemsCount"));
        o.weatherSystem = static_cast<uint64_t>(ExtractJsonNumber(json, "weatherSystem"));

        o.physicsWorld = static_cast<uint64_t>(ExtractJsonNumber(json, "physicsWorld"));
        o.camera = static_cast<uint64_t>(ExtractJsonNumber(json, "camera"));
        o.cameraTarget = static_cast<uint64_t>(ExtractJsonNumber(json, "cameraTarget"));
        o.renderer = static_cast<uint64_t>(ExtractJsonNumber(json, "renderer"));

        o.inputHandler = static_cast<uint64_t>(ExtractJsonNumber(json, "inputHandler"));
        o.commandQueue = static_cast<uint64_t>(ExtractJsonNumber(json, "commandQueue"));
        o.selectedUnits = static_cast<uint64_t>(ExtractJsonNumber(json, "selectedUnits"));
        o.uiState = static_cast<uint64_t>(ExtractJsonNumber(json, "uiState"));

        return true;
    }

    bool OnlineOffsetFetcher::ParseFunctionOffsets(const std::string& json)
    {
        auto& f = m_database.functionOffsets;

        f.spawnCharacter = static_cast<uint64_t>(ExtractJsonNumber(json, "spawnCharacter"));
        f.despawnCharacter = static_cast<uint64_t>(ExtractJsonNumber(json, "despawnCharacter"));
        f.addToSquad = static_cast<uint64_t>(ExtractJsonNumber(json, "addToSquad"));
        f.removeFromSquad = static_cast<uint64_t>(ExtractJsonNumber(json, "removeFromSquad"));
        f.addItemToInventory = static_cast<uint64_t>(ExtractJsonNumber(json, "addItemToInventory"));
        f.removeItemFromInventory = static_cast<uint64_t>(ExtractJsonNumber(json, "removeItemFromInventory"));
        f.setCharacterState = static_cast<uint64_t>(ExtractJsonNumber(json, "setCharacterState"));
        f.issueCommand = static_cast<uint64_t>(ExtractJsonNumber(json, "issueCommand"));
        f.createFaction = static_cast<uint64_t>(ExtractJsonNumber(json, "createFaction"));
        f.setFactionRelation = static_cast<uint64_t>(ExtractJsonNumber(json, "setFactionRelation"));
        f.pathfindRequest = static_cast<uint64_t>(ExtractJsonNumber(json, "pathfindRequest"));
        f.combatAttack = static_cast<uint64_t>(ExtractJsonNumber(json, "combatAttack"));
        f.characterUpdate = static_cast<uint64_t>(ExtractJsonNumber(json, "characterUpdate"));
        f.aiUpdate = static_cast<uint64_t>(ExtractJsonNumber(json, "aiUpdate"));

        return true;
    }

    bool OnlineOffsetFetcher::ParseStructureOffsets(const std::string& json)
    {
        auto& s = m_database.structureOffsets;

        // Parse character offsets
        std::string charJson = ExtractJsonObject(json, "character");
        if (!charJson.empty())
        {
            s.charPosition = static_cast<int32_t>(ExtractJsonNumber(charJson, "position"));
            s.charRotation = static_cast<int32_t>(ExtractJsonNumber(charJson, "rotation"));
            s.charHealth = static_cast<int32_t>(ExtractJsonNumber(charJson, "health"));
            s.charMaxHealth = static_cast<int32_t>(ExtractJsonNumber(charJson, "maxHealth"));
            s.charBlood = static_cast<int32_t>(ExtractJsonNumber(charJson, "blood"));
            s.charHunger = static_cast<int32_t>(ExtractJsonNumber(charJson, "hunger"));
            s.charInventory = static_cast<int32_t>(ExtractJsonNumber(charJson, "inventory"));
            s.charEquipment = static_cast<int32_t>(ExtractJsonNumber(charJson, "equipment"));
            s.charAI = static_cast<int32_t>(ExtractJsonNumber(charJson, "ai"));
            s.charState = static_cast<int32_t>(ExtractJsonNumber(charJson, "state"));
            s.charFaction = static_cast<int32_t>(ExtractJsonNumber(charJson, "faction"));
            s.charSquad = static_cast<int32_t>(ExtractJsonNumber(charJson, "squad"));
            s.charAnimState = static_cast<int32_t>(ExtractJsonNumber(charJson, "animState"));
            s.charBody = static_cast<int32_t>(ExtractJsonNumber(charJson, "body"));
        }

        // Parse squad offsets
        std::string squadJson = ExtractJsonObject(json, "squad");
        if (!squadJson.empty())
        {
            s.squadMembers = static_cast<int32_t>(ExtractJsonNumber(squadJson, "members"));
            s.squadMemberCount = static_cast<int32_t>(ExtractJsonNumber(squadJson, "memberCount"));
            s.squadLeader = static_cast<int32_t>(ExtractJsonNumber(squadJson, "leader"));
            s.squadFactionId = static_cast<int32_t>(ExtractJsonNumber(squadJson, "factionId"));
        }

        // Parse faction offsets
        std::string factionJson = ExtractJsonObject(json, "faction");
        if (!factionJson.empty())
        {
            s.factionRelations = static_cast<int32_t>(ExtractJsonNumber(factionJson, "relations"));
            s.factionMembers = static_cast<int32_t>(ExtractJsonNumber(factionJson, "members"));
            s.factionLeader = static_cast<int32_t>(ExtractJsonNumber(factionJson, "leader"));
        }

        // Parse item offsets
        std::string itemJson = ExtractJsonObject(json, "item");
        if (!itemJson.empty())
        {
            s.itemName = static_cast<int32_t>(ExtractJsonNumber(itemJson, "name"));
            s.itemCategory = static_cast<int32_t>(ExtractJsonNumber(itemJson, "category"));
            s.itemValue = static_cast<int32_t>(ExtractJsonNumber(itemJson, "value"));
            s.itemWeight = static_cast<int32_t>(ExtractJsonNumber(itemJson, "weight"));
            s.itemStackCount = static_cast<int32_t>(ExtractJsonNumber(itemJson, "stackCount"));
        }

        return true;
    }

    bool OnlineOffsetFetcher::ParsePatterns(const std::string& json)
    {
        // Pattern names to look for
        std::vector<std::string> patternNames = {
            "gameWorld", "playerSquadList", "allCharactersList", "factionManager",
            "weatherSystem", "inputHandler", "cameraController", "spawnCharacter",
            "characterUpdate", "combatSystem"
        };

        for (const auto& name : patternNames)
        {
            std::string patternJson = ExtractJsonObject(json, name);
            if (!patternJson.empty())
            {
                OnlinePatternDef def;
                def.name = ExtractJsonValue(patternJson, "name");
                def.pattern = ExtractJsonValue(patternJson, "pattern");
                def.mask = ExtractJsonValue(patternJson, "mask");
                def.offset = static_cast<int32_t>(ExtractJsonNumber(patternJson, "offset"));
                def.isRelative = ExtractJsonBool(patternJson, "isRelative");
                def.relativeBase = static_cast<int32_t>(ExtractJsonNumber(patternJson, "relativeBase"));

                m_database.patterns[name] = def;
            }
        }

        return true;
    }

    bool OnlineOffsetFetcher::ValidateOffsets()
    {
        const auto& o = m_database.gameOffsets;

        // Check that we have at least some critical offsets
        if (o.worldInstance == 0 && o.playerSquadList == 0 && o.allCharactersList == 0)
        {
            return false;
        }

        // TODO: Add checksum validation if checksum is provided
        // if (!m_database.checksum.empty()) { ... }

        return true;
    }

    bool OnlineOffsetFetcher::SaveCache(const std::string& filename)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open())
            return false;

        // Write magic + version
        uint32_t magic = 0x4B4F4646; // "KOFF"
        uint32_t version = 1;
        file.write(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<char*>(&version), sizeof(version));

        // Write timestamp
        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        file.write(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));

        // Write game version string length + data
        uint32_t versionLen = static_cast<uint32_t>(m_database.gameVersion.length());
        file.write(reinterpret_cast<char*>(&versionLen), sizeof(versionLen));
        file.write(m_database.gameVersion.c_str(), versionLen);

        // Write offsets
        file.write(reinterpret_cast<const char*>(&m_database.gameOffsets), sizeof(OnlineGameOffsets));
        file.write(reinterpret_cast<const char*>(&m_database.functionOffsets), sizeof(OnlineFunctionOffsets));
        file.write(reinterpret_cast<const char*>(&m_database.structureOffsets), sizeof(OnlineStructureOffsets));

        file.close();
        return true;
    }

    bool OnlineOffsetFetcher::LoadCache(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return false;

        // Read and verify magic
        uint32_t magic, version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != 0x4B4F4646 || version != 1)
            return false;

        // Read timestamp and check if cache is too old (7 days)
        uint64_t timestamp;
        file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));

        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t nowTimestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

        if (nowTimestamp - timestamp > 7 * 24 * 60 * 60)
        {
            // Cache is too old
            return false;
        }

        // Read game version
        uint32_t versionLen;
        file.read(reinterpret_cast<char*>(&versionLen), sizeof(versionLen));
        if (versionLen > 64)
            return false;

        std::vector<char> versionBuf(versionLen + 1, 0);
        file.read(versionBuf.data(), versionLen);
        m_database.gameVersion = versionBuf.data();

        // Read offsets
        file.read(reinterpret_cast<char*>(&m_database.gameOffsets), sizeof(OnlineGameOffsets));
        file.read(reinterpret_cast<char*>(&m_database.functionOffsets), sizeof(OnlineFunctionOffsets));
        file.read(reinterpret_cast<char*>(&m_database.structureOffsets), sizeof(OnlineStructureOffsets));

        m_database.isValid = true;
        file.close();
        return true;
    }

    bool OnlineOffsetFetcher::RefreshOffsets()
    {
        // Delete cache file
        std::remove("kenshi_offsets_cache.dat");

        m_database = OnlineOffsetDatabase();
        return FetchOffsets();
    }

    void OnlineOffsetFetcher::AddServer(const std::string& url)
    {
        m_serverUrls.insert(m_serverUrls.begin(), url);
    }

    std::string OnlineOffsetFetcher::ExportAsJson() const
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"version\": " << m_database.schemaVersion << ",\n";
        json << "  \"gameVersion\": \"" << m_database.gameVersion << "\",\n";
        json << "  \"isUniversal\": " << (m_database.isUniversal ? "true" : "false") << ",\n";
        json << "  \"offsets\": {\n";

        const auto& o = m_database.gameOffsets;
        json << "    \"baseAddress\": " << o.baseAddress << ",\n";
        json << "    \"worldInstance\": " << o.worldInstance << ",\n";
        json << "    \"playerSquadList\": " << o.playerSquadList << ",\n";
        json << "    \"allCharactersList\": " << o.allCharactersList << "\n";
        // ... (abbreviated for brevity)

        json << "  }\n";
        json << "}\n";

        return json.str();
    }

    // JSON parsing helpers (simple implementation without external library)
    std::string OnlineOffsetFetcher::ExtractJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos)
            return "";

        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos)
            return "";

        size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos)
            return "";

        if (json[valueStart] == '"')
        {
            // String value
            size_t valueEnd = json.find('"', valueStart + 1);
            if (valueEnd == std::string::npos)
                return "";
            return json.substr(valueStart + 1, valueEnd - valueStart - 1);
        }

        return "";
    }

    int64_t OnlineOffsetFetcher::ExtractJsonNumber(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos)
            return 0;

        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos)
            return 0;

        size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos)
            return 0;

        // Check for hex format
        if (json.substr(valueStart, 2) == "0x" || json.substr(valueStart, 2) == "0X")
        {
            return std::stoll(json.substr(valueStart), nullptr, 16);
        }

        // Decimal
        size_t valueEnd = json.find_first_of(",}\n\r", valueStart);
        std::string numStr = json.substr(valueStart, valueEnd - valueStart);

        // Remove whitespace
        numStr.erase(std::remove_if(numStr.begin(), numStr.end(), ::isspace), numStr.end());

        try
        {
            return std::stoll(numStr);
        }
        catch (...)
        {
            return 0;
        }
    }

    bool OnlineOffsetFetcher::ExtractJsonBool(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos)
            return false;

        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos)
            return false;

        size_t valueStart = json.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos)
            return false;

        return json.substr(valueStart, 4) == "true";
    }

    std::string OnlineOffsetFetcher::ExtractJsonObject(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos)
            return "";

        size_t braceStart = json.find('{', keyPos);
        if (braceStart == std::string::npos)
            return "";

        // Find matching closing brace
        int braceCount = 1;
        size_t braceEnd = braceStart + 1;
        while (braceEnd < json.length() && braceCount > 0)
        {
            if (json[braceEnd] == '{')
                braceCount++;
            else if (json[braceEnd] == '}')
                braceCount--;
            braceEnd++;
        }

        if (braceCount != 0)
            return "";

        return json.substr(braceStart, braceEnd - braceStart);
    }

    std::string OnlineOffsetFetcher::ExtractJsonArray(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = json.find(searchKey);
        if (keyPos == std::string::npos)
            return "";

        size_t bracketStart = json.find('[', keyPos);
        if (bracketStart == std::string::npos)
            return "";

        // Find matching closing bracket
        int bracketCount = 1;
        size_t bracketEnd = bracketStart + 1;
        while (bracketEnd < json.length() && bracketCount > 0)
        {
            if (json[bracketEnd] == '[')
                bracketCount++;
            else if (json[bracketEnd] == ']')
                bracketCount--;
            bracketEnd++;
        }

        if (bracketCount != 0)
            return "";

        return json.substr(bracketStart, bracketEnd - bracketStart);
    }

    //=========================================================================
    // INTEGRATED OFFSET MANAGER IMPLEMENTATION
    //=========================================================================

    bool IntegratedOffsetManager::Initialize(HMODULE gameModule)
    {
        if (m_initialized)
            return true;

        // Get module base
        if (gameModule == nullptr)
            gameModule = GetModuleHandle(NULL);

        MODULEINFO moduleInfo;
        if (GetModuleInformation(GetCurrentProcess(), gameModule, &moduleInfo, sizeof(moduleInfo)))
        {
            m_moduleBase = reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll);
            m_moduleSize = moduleInfo.SizeOfImage;
        }

        // Priority 1: Try online offsets
        if (m_preferOnline)
        {
            auto& fetcher = OnlineOffsetFetcher::Get();
            fetcher.Initialize();

            if (fetcher.FetchOffsets())
            {
                ApplyOnlineOffsets();
                m_onlineLoaded = true;
                m_currentSource = OffsetSource::Online;
                m_initialized = true;
                return true;
            }
        }

        // Priority 2: Try pattern scanning
        if (m_allowPatternScan)
        {
            auto& scanner = PatternScanner::Get();
            if (scanner.Initialize(gameModule) && scanner.ScanAllSignatures())
            {
                ApplyPatternOffsets();
                m_patternScanned = true;
                m_currentSource = OffsetSource::PatternScan;
                m_initialized = true;
                return true;
            }
        }

        // Priority 3: Use hardcoded offsets
        ApplyHardcodedOffsets();
        m_currentSource = OffsetSource::Hardcoded;
        m_initialized = true;
        return true;
    }

    uint64_t IntegratedOffsetManager::GetOffset(const std::string& category, const std::string& name) const
    {
        std::string key = category + "." + name;
        auto it = m_offsets.find(key);
        if (it != m_offsets.end())
            return it->second;
        return 0;
    }

    uint64_t IntegratedOffsetManager::GetAbsolute(const std::string& category, const std::string& name) const
    {
        return m_moduleBase + GetOffset(category, name);
    }

    uint64_t IntegratedOffsetManager::GetFunction(const std::string& name) const
    {
        auto it = m_functions.find(name);
        if (it != m_functions.end())
            return m_moduleBase + it->second;
        return 0;
    }

    int32_t IntegratedOffsetManager::GetStructureOffset(const std::string& structure, const std::string& field) const
    {
        std::string key = structure + "." + field;
        auto it = m_structureOffsets.find(key);
        if (it != m_structureOffsets.end())
            return it->second;
        return 0;
    }

    void IntegratedOffsetManager::SetSourcePriority(bool preferOnline, bool allowPatternScan)
    {
        m_preferOnline = preferOnline;
        m_allowPatternScan = allowPatternScan;
    }

    bool IntegratedOffsetManager::Reload()
    {
        m_initialized = false;
        m_onlineLoaded = false;
        m_patternScanned = false;
        m_offsets.clear();
        m_functions.clear();
        m_structureOffsets.clear();

        return Initialize(nullptr);
    }

    void IntegratedOffsetManager::ApplyOnlineOffsets()
    {
        const auto& db = OnlineOffsetFetcher::Get().GetDatabase();
        const auto& o = db.gameOffsets;
        const auto& f = db.functionOffsets;
        const auto& s = db.structureOffsets;

        // Game offsets
        m_offsets["Game.WorldInstance"] = o.worldInstance;
        m_offsets["Game.GameState"] = o.gameState;
        m_offsets["Game.GameTime"] = o.gameTime;
        m_offsets["Game.GameDay"] = o.gameDay;

        m_offsets["Characters.PlayerSquadList"] = o.playerSquadList;
        m_offsets["Characters.PlayerSquadCount"] = o.playerSquadCount;
        m_offsets["Characters.AllCharactersList"] = o.allCharactersList;
        m_offsets["Characters.AllCharactersCount"] = o.allCharactersCount;
        m_offsets["Characters.SelectedCharacter"] = o.selectedCharacter;

        m_offsets["Factions.FactionList"] = o.factionList;
        m_offsets["Factions.FactionCount"] = o.factionCount;
        m_offsets["Factions.PlayerFaction"] = o.playerFaction;

        m_offsets["World.BuildingList"] = o.buildingList;
        m_offsets["World.BuildingCount"] = o.buildingCount;
        m_offsets["World.WeatherSystem"] = o.weatherSystem;

        m_offsets["Engine.PhysicsWorld"] = o.physicsWorld;
        m_offsets["Engine.Camera"] = o.camera;

        m_offsets["Input.InputHandler"] = o.inputHandler;

        // Function offsets
        m_functions["SpawnCharacter"] = f.spawnCharacter;
        m_functions["DespawnCharacter"] = f.despawnCharacter;
        m_functions["AddToSquad"] = f.addToSquad;
        m_functions["RemoveFromSquad"] = f.removeFromSquad;
        m_functions["AddItemToInventory"] = f.addItemToInventory;
        m_functions["RemoveItemFromInventory"] = f.removeItemFromInventory;
        m_functions["SetCharacterState"] = f.setCharacterState;
        m_functions["IssueCommand"] = f.issueCommand;
        m_functions["SetFactionRelation"] = f.setFactionRelation;
        m_functions["PathfindRequest"] = f.pathfindRequest;
        m_functions["CombatAttack"] = f.combatAttack;

        // Structure offsets
        m_structureOffsets["Character.Position"] = s.charPosition;
        m_structureOffsets["Character.Rotation"] = s.charRotation;
        m_structureOffsets["Character.Health"] = s.charHealth;
        m_structureOffsets["Character.MaxHealth"] = s.charMaxHealth;
        m_structureOffsets["Character.Blood"] = s.charBlood;
        m_structureOffsets["Character.Hunger"] = s.charHunger;
        m_structureOffsets["Character.Inventory"] = s.charInventory;
        m_structureOffsets["Character.Equipment"] = s.charEquipment;
        m_structureOffsets["Character.AI"] = s.charAI;
        m_structureOffsets["Character.State"] = s.charState;
        m_structureOffsets["Character.Faction"] = s.charFaction;
        m_structureOffsets["Character.Squad"] = s.charSquad;
        m_structureOffsets["Character.AnimState"] = s.charAnimState;
        m_structureOffsets["Character.Body"] = s.charBody;
    }

    void IntegratedOffsetManager::ApplyPatternOffsets()
    {
        auto& scanner = PatternScanner::Get();

        // Get pattern-scanned addresses and convert to offsets
        auto gameWorld = scanner.GetAddress("GameWorld");
        if (gameWorld)
            m_offsets["Game.WorldInstance"] = gameWorld - m_moduleBase;

        auto playerSquad = scanner.GetAddress("PlayerSquadList");
        if (playerSquad)
            m_offsets["Characters.PlayerSquadList"] = playerSquad - m_moduleBase;

        auto allChars = scanner.GetAddress("AllCharactersList");
        if (allChars)
            m_offsets["Characters.AllCharactersList"] = allChars - m_moduleBase;

        // ... Add more pattern results

        // For structure offsets, use defaults as patterns don't typically provide these
        ApplyHardcodedOffsets();
    }

    void IntegratedOffsetManager::ApplyHardcodedOffsets()
    {
        // Kenshi v1.0.x 64-bit hardcoded offsets
        m_offsets["Game.WorldInstance"] = 0x24D8F40;
        m_offsets["Game.GameState"] = 0x24D8F48;
        m_offsets["Game.GameTime"] = 0x24D8F50;
        m_offsets["Game.GameDay"] = 0x24D8F58;

        m_offsets["Characters.PlayerSquadList"] = 0x24C5A20;
        m_offsets["Characters.PlayerSquadCount"] = 0x24C5A28;
        m_offsets["Characters.AllCharactersList"] = 0x24C5B00;
        m_offsets["Characters.AllCharactersCount"] = 0x24C5B08;
        m_offsets["Characters.SelectedCharacter"] = 0x24C5A30;

        m_offsets["Factions.FactionList"] = 0x24D2100;
        m_offsets["Factions.FactionCount"] = 0x24D2108;
        m_offsets["Factions.PlayerFaction"] = 0x24D2110;

        m_offsets["World.BuildingList"] = 0x24E1000;
        m_offsets["World.BuildingCount"] = 0x24E1008;
        m_offsets["World.WeatherSystem"] = 0x24E7000;

        m_offsets["Engine.PhysicsWorld"] = 0x24F0000;
        m_offsets["Engine.Camera"] = 0x24E7C20;

        m_offsets["Input.InputHandler"] = 0x24F2D80;

        // Functions
        m_functions["SpawnCharacter"] = 0x8B3C80;
        m_functions["DespawnCharacter"] = 0x8B4120;
        m_functions["AddToSquad"] = 0x8B4500;
        m_functions["RemoveFromSquad"] = 0x8B4600;
        m_functions["AddItemToInventory"] = 0x9C2100;
        m_functions["RemoveItemFromInventory"] = 0x9C2200;
        m_functions["SetCharacterState"] = 0x8C1000;
        m_functions["IssueCommand"] = 0x8D5000;
        m_functions["SetFactionRelation"] = 0x7A2500;
        m_functions["PathfindRequest"] = 0x7B1000;
        m_functions["CombatAttack"] = 0x8E2000;

        // Structure offsets
        m_structureOffsets["Character.Position"] = 0x70;
        m_structureOffsets["Character.Rotation"] = 0x7C;
        m_structureOffsets["Character.Health"] = 0xC0;
        m_structureOffsets["Character.MaxHealth"] = 0xC4;
        m_structureOffsets["Character.Blood"] = 0xC8;
        m_structureOffsets["Character.Hunger"] = 0xD0;
        m_structureOffsets["Character.Inventory"] = 0xF0;
        m_structureOffsets["Character.Equipment"] = 0xF8;
        m_structureOffsets["Character.AI"] = 0x110;
        m_structureOffsets["Character.State"] = 0x118;
        m_structureOffsets["Character.Faction"] = 0x158;
        m_structureOffsets["Character.Squad"] = 0x168;
        m_structureOffsets["Character.AnimState"] = 0x140;
        m_structureOffsets["Character.Body"] = 0xB8;
    }

} // namespace Kenshi

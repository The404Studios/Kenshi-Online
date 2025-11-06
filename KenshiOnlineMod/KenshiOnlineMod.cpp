/*
 * Kenshi Online Mod DLL
 * This DLL injects into Kenshi and acts as a native mod
 * Communicates with the C# server for multiplayer functionality
 */

// Include winsock2 BEFORE Windows.h to avoid redefinition errors
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

// Forward declarations
void InitializeMod();
void ShutdownMod();
void NetworkThread();
bool ConnectToServer(const char* address, int port);
void SendGameState();
void ReceiveCommands();

// Global state
HMODULE g_ModuleHandle = nullptr;
SOCKET g_Socket = INVALID_SOCKET;
bool g_Running = false;
std::mutex g_Mutex;

// Kenshi base address (will be calculated at runtime)
uintptr_t g_KenshiBase = 0;

// Configuration
const char* SERVER_ADDRESS = "127.0.0.1";
const int SERVER_PORT = 5555;

#pragma region Memory Addresses

// Kenshi memory offsets (relative to base)
namespace Kenshi
{
    namespace Offsets
    {
        constexpr uintptr_t GameWorld = 0x24D8F40;
        constexpr uintptr_t PlayerSquadList = 0x24C5A20;
        constexpr uintptr_t PlayerSquadCount = 0x24C5A28;
        constexpr uintptr_t FactionList = 0x24D2100;
        constexpr uintptr_t FactionCount = 0x24D2108;
        constexpr uintptr_t AllSquadsList = 0x24C5B00;
        constexpr uintptr_t AllSquadsCount = 0x24C5B08;
        constexpr uintptr_t BuildingList = 0x24D3200;
        constexpr uintptr_t BuildingCount = 0x24D3208;
    }

    namespace Functions
    {
        constexpr uintptr_t SpawnCharacter = 0x8B3C80;
        constexpr uintptr_t IssueCommand = 0x8D5000;
        constexpr uintptr_t CreateSquad = 0x8E2400;
        constexpr uintptr_t PlaceBuilding = 0x9A1C00;
    }
}

#pragma endregion

#pragma region Game Structures

struct Vector3
{
    float x, y, z;
};

struct Character
{
    void* vtable;
    int characterID;
    char* name;
    void* race;

    Vector3 position;
    Vector3 rotation;

    void* stats;
    void* skills;

    float health;
    float maxHealth;
    float bloodLevel;
    float hunger;
    float hungerRate;

    void* limbs;
    int limbCount;

    void* inventory;
    void* equipment;
    int inventorySize;

    void* aiController;
    void* currentAction;
    int characterState;
    void* target;

    int factionID;
    void* squad;
    void* playerOwner;

    void* animationState;
    void* skeleton;

    uint8_t isUnconscious;
    uint8_t isDead;
    uint8_t isPlayerControlled;
    uint8_t isInCombat;
};

struct Faction
{
    void* vtable;
    int factionID;
    char* name;
    void* leader;

    void* relations;
    int relationCount;

    void* memberList;
    int memberCount;

    int factionType;
    int wealth;
    void* territory;

    uint8_t isPlayerFaction;
    uint8_t isHostileToPlayer;
    uint8_t canRecruit;
    uint8_t isActive;
};

struct FactionRelation
{
    int targetFactionID;
    int relationValue; // -100 to +100
    uint8_t isEnemy;
    uint8_t isAlly;
    uint8_t permanentWar;
    uint8_t permanentPeace;
};

struct Squad
{
    void* vtable;
    int squadID;
    char* name;
    void* leader;

    void* memberList;
    int memberCount;
    int maxMembers;

    void* orders;
    void* formation;

    uint8_t isPlayerSquad;
    uint8_t isActive;
};

struct Building
{
    void* vtable;
    int buildingID;
    char* name;
    int buildingType;

    Vector3 position;
    Vector3 rotation;

    void* owner; // Faction or character
    int ownerFactionID;

    float health;
    float maxHealth;

    void* inventory;
    void* production;

    uint8_t isConstructed;
    uint8_t isDamaged;
    uint8_t isPlayerOwned;
};

#pragma endregion

#pragma region Helper Functions

uintptr_t GetKenshiBase()
{
    return (uintptr_t)GetModuleHandle(NULL);
}

template<typename T>
T Read(uintptr_t address)
{
    return *(T*)address;
}

template<typename T>
void Write(uintptr_t address, T value)
{
    DWORD oldProtect;
    VirtualProtect((void*)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
    *(T*)address = value;
    VirtualProtect((void*)address, sizeof(T), oldProtect, &oldProtect);
}

std::vector<Character*> GetPlayerCharacters()
{
    std::vector<Character*> characters;

    uintptr_t listAddr = g_KenshiBase + Kenshi::Offsets::PlayerSquadList;
    uintptr_t countAddr = g_KenshiBase + Kenshi::Offsets::PlayerSquadCount;

    int count = Read<int>(countAddr);
    Character** list = Read<Character**>(listAddr);

    for (int i = 0; i < count && i < 100; i++)
    {
        if (list[i] != nullptr)
        {
            characters.push_back(list[i]);
        }
    }

    return characters;
}

std::vector<Faction*> GetAllFactions()
{
    std::vector<Faction*> factions;

    uintptr_t listAddr = g_KenshiBase + Kenshi::Offsets::FactionList;
    uintptr_t countAddr = g_KenshiBase + Kenshi::Offsets::FactionCount;

    int count = Read<int>(countAddr);
    Faction** list = Read<Faction**>(listAddr);

    for (int i = 0; i < count && i < 256; i++)
    {
        if (list[i] != nullptr)
        {
            factions.push_back(list[i]);
        }
    }

    return factions;
}

Faction* GetFactionByID(int factionID)
{
    auto factions = GetAllFactions();
    for (auto* faction : factions)
    {
        if (faction && faction->factionID == factionID)
        {
            return faction;
        }
    }
    return nullptr;
}

bool SetFactionRelation(int faction1ID, int faction2ID, int relationValue)
{
    Faction* faction1 = GetFactionByID(faction1ID);
    if (!faction1 || !faction1->relations) return false;

    // Faction relations are stored as an array of FactionRelation structs
    FactionRelation* relations = (FactionRelation*)faction1->relations;

    // Find existing relation or create new one
    for (int i = 0; i < faction1->relationCount; i++)
    {
        if (relations[i].targetFactionID == faction2ID)
        {
            // Update existing relation
            Write<int>((uintptr_t)&relations[i].relationValue, relationValue);

            // Update flags based on relation value
            Write<uint8_t>((uintptr_t)&relations[i].isEnemy, relationValue < -50 ? 1 : 0);
            Write<uint8_t>((uintptr_t)&relations[i].isAlly, relationValue > 50 ? 1 : 0);
            return true;
        }
    }

    return false;
}

void SendFactionData()
{
    if (g_Socket == INVALID_SOCKET) return;

    try
    {
        auto factions = GetAllFactions();

        for (auto* faction : factions)
        {
            if (faction == nullptr || faction->name == nullptr) continue;

            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                "FACTION_DATA|%d|%s|%d|%d|%d|%d\n",
                faction->factionID,
                faction->name,
                faction->factionType,
                faction->wealth,
                faction->isPlayerFaction ? 1 : 0,
                faction->memberCount
            );

            send(g_Socket, buffer, (int)strlen(buffer), 0);
        }
    }
    catch (...)
    {
        // Ignore errors
    }
}

std::vector<Squad*> GetAllSquads()
{
    std::vector<Squad*> squads;

    uintptr_t listAddr = g_KenshiBase + Kenshi::Offsets::AllSquadsList;
    uintptr_t countAddr = g_KenshiBase + Kenshi::Offsets::AllSquadsCount;

    int count = Read<int>(countAddr);
    Squad** list = Read<Squad**>(listAddr);

    for (int i = 0; i < count && i < 512; i++)
    {
        if (list[i] != nullptr && list[i]->isActive)
        {
            squads.push_back(list[i]);
        }
    }

    return squads;
}

void SendSquadData()
{
    if (g_Socket == INVALID_SOCKET) return;

    try
    {
        auto squads = GetAllSquads();

        for (auto* squad : squads)
        {
            if (squad == nullptr || squad->name == nullptr) continue;

            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                "SQUAD_DATA|%d|%s|%d|%d|%d\n",
                squad->squadID,
                squad->name,
                squad->memberCount,
                squad->maxMembers,
                squad->isPlayerSquad ? 1 : 0
            );

            send(g_Socket, buffer, (int)strlen(buffer), 0);
        }
    }
    catch (...)
    {
        // Ignore errors
    }
}

std::vector<Building*> GetAllBuildings()
{
    std::vector<Building*> buildings;

    uintptr_t listAddr = g_KenshiBase + Kenshi::Offsets::BuildingList;
    uintptr_t countAddr = g_KenshiBase + Kenshi::Offsets::BuildingCount;

    int count = Read<int>(countAddr);
    Building** list = Read<Building**>(listAddr);

    for (int i = 0; i < count && i < 2048; i++)
    {
        if (list[i] != nullptr && list[i]->isConstructed)
        {
            buildings.push_back(list[i]);
        }
    }

    return buildings;
}

void SendBuildingData()
{
    if (g_Socket == INVALID_SOCKET) return;

    try
    {
        auto buildings = GetAllBuildings();

        for (auto* building : buildings)
        {
            if (building == nullptr) continue;

            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                "BUILDING_DATA|%d|%s|%d|%.2f|%.2f|%.2f|%d|%.2f|%.2f\n",
                building->buildingID,
                building->name ? building->name : "Unknown",
                building->buildingType,
                building->position.x,
                building->position.y,
                building->position.z,
                building->ownerFactionID,
                building->health,
                building->maxHealth
            );

            send(g_Socket, buffer, (int)strlen(buffer), 0);
        }
    }
    catch (...)
    {
        // Ignore errors
    }
}

#pragma endregion

#pragma region Hooks

// Function pointers for original functions
typedef Character* (*SpawnCharacterFunc)(void* worldPtr, const char* name, Vector3 pos, int factionID);
SpawnCharacterFunc originalSpawnCharacter = nullptr;

// Hooked spawn function
Character* HookedSpawnCharacter(void* worldPtr, const char* name, Vector3 pos, int factionID)
{
    // Call original
    Character* character = originalSpawnCharacter(worldPtr, name, pos, factionID);

    // Notify server of spawn
    if (character != nullptr && g_Socket != INVALID_SOCKET)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "SPAWN|%d|%s|%.2f|%.2f|%.2f|%d\n",
            character->characterID, name, pos.x, pos.y, pos.z, factionID);
        send(g_Socket, buffer, (int)strlen(buffer), 0);
    }

    return character;
}

void InstallHooks()
{
    // Hook spawn function
    uintptr_t spawnAddr = g_KenshiBase + Kenshi::Functions::SpawnCharacter;

    DWORD oldProtect;
    VirtualProtect((void*)spawnAddr, 14, PAGE_EXECUTE_READWRITE, &oldProtect);

    // Save original bytes
    originalSpawnCharacter = (SpawnCharacterFunc)spawnAddr;

    // Write jump to hook
    *(uint8_t*)(spawnAddr) = 0xFF; // JMP [rip+0]
    *(uint8_t*)(spawnAddr + 1) = 0x25;
    *(uint32_t*)(spawnAddr + 2) = 0;
    *(uint64_t*)(spawnAddr + 6) = (uint64_t)&HookedSpawnCharacter;

    VirtualProtect((void*)spawnAddr, 14, oldProtect, &oldProtect);
}

#pragma endregion

#pragma region Network

bool ConnectToServer(const char* address, int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return false;
    }

    g_Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_Socket == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, address, &serverAddr.sin_addr);

    if (connect(g_Socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(g_Socket);
        g_Socket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    // Send handshake
    const char* handshake = "KENSHI_MOD_HELLO\n";
    send(g_Socket, handshake, (int)strlen(handshake), 0);

    return true;
}

void SendGameState()
{
    if (g_Socket == INVALID_SOCKET) return;

    std::lock_guard<std::mutex> lock(g_Mutex);

    try
    {
        auto characters = GetPlayerCharacters();

        for (auto* character : characters)
        {
            if (character == nullptr) continue;

            char buffer[512];
            snprintf(buffer, sizeof(buffer),
                "STATE|%d|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%d|%d\n",
                character->characterID,
                character->position.x,
                character->position.y,
                character->position.z,
                character->health,
                character->maxHealth,
                character->characterState,
                character->factionID,
                character->isInCombat ? 1 : 0
            );

            send(g_Socket, buffer, (int)strlen(buffer), 0);
        }
    }
    catch (...)
    {
        // Ignore errors
    }
}

void ReceiveCommands()
{
    if (g_Socket == INVALID_SOCKET) return;

    char buffer[1024];
    int bytesReceived = recv(g_Socket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived > 0)
    {
        buffer[bytesReceived] = '\0';

        // Parse commands
        char* token = strtok(buffer, "\n");
        while (token != nullptr)
        {
            // Parse command format: COMMAND|param1|param2|...
            char command[64];
            sscanf(token, "%63[^|]", command);

            if (strcmp(command, "MOVE") == 0)
            {
                int charID;
                float x, y, z;
                if (sscanf(token, "MOVE|%d|%f|%f|%f", &charID, &x, &y, &z) == 4)
                {
                    // Issue move command to character
                    auto characters = GetPlayerCharacters();
                    for (auto* character : characters)
                    {
                        if (character->characterID == charID)
                        {
                            Write<Vector3>((uintptr_t)&character->position, { x, y, z });
                            break;
                        }
                    }
                }
            }
            else if (strcmp(command, "SPAWN") == 0)
            {
                char name[128];
                float x, y, z;
                int factionID;
                if (sscanf(token, "SPAWN|%127[^|]|%f|%f|%f|%d", name, &x, &y, &z, &factionID) == 5)
                {
                    // Call spawn function
                    if (originalSpawnCharacter != nullptr)
                    {
                        originalSpawnCharacter(nullptr, name, { x, y, z }, factionID);
                    }
                }
            }
            else if (strcmp(command, "FACTION") == 0)
            {
                int faction1, faction2, relation;
                if (sscanf(token, "FACTION|%d|%d|%d", &faction1, &faction2, &relation) == 3)
                {
                    // Set faction relation
                    if (SetFactionRelation(faction1, faction2, relation))
                    {
                        // Also set the reverse relation
                        SetFactionRelation(faction2, faction1, relation);
                        std::cout << "Set faction relation: " << faction1 << " <-> " << faction2 << " = " << relation << "\n";
                    }
                }
            }
            else if (strcmp(command, "GET_FACTIONS") == 0)
            {
                // Send all faction data to server
                SendFactionData();
            }
            else if (strcmp(command, "GET_SQUADS") == 0)
            {
                // Send all squad data to server
                SendSquadData();
            }
            else if (strcmp(command, "GET_BUILDINGS") == 0)
            {
                // Send all building data to server
                SendBuildingData();
            }
            else if (strcmp(command, "SQUAD_COMMAND") == 0)
            {
                int squadID, commandType;
                float x, y, z;
                if (sscanf(token, "SQUAD_COMMAND|%d|%d|%f|%f|%f", &squadID, &commandType, &x, &y, &z) == 5)
                {
                    // Issue command to squad
                    // commandType: 0=Move, 1=Attack, 2=Follow, 3=Hold
                    std::cout << "Squad command: " << squadID << " type:" << commandType << " pos:(" << x << "," << y << "," << z << ")\n";
                    // TODO: Implement squad command execution
                }
            }
            else if (strcmp(command, "BUILDING_UPDATE") == 0)
            {
                int buildingID;
                float health;
                if (sscanf(token, "BUILDING_UPDATE|%d|%f", &buildingID, &health) == 2)
                {
                    // Update building health
                    auto buildings = GetAllBuildings();
                    for (auto* building : buildings)
                    {
                        if (building && building->buildingID == buildingID)
                        {
                            Write<float>((uintptr_t)&building->health, health);
                            std::cout << "Updated building " << buildingID << " health to " << health << "\n";
                            break;
                        }
                    }
                }
            }

            token = strtok(nullptr, "\n");
        }
    }
}

void NetworkThread()
{
    while (g_Running)
    {
        // Send game state every 50ms (20 Hz)
        SendGameState();

        // Receive commands
        ReceiveCommands();

        Sleep(50);
    }
}

#pragma endregion

#pragma region Initialization

void InitializeMod()
{
    // Create console for debugging
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "=================================\n";
    std::cout << "  Kenshi Online Mod Loaded\n";
    std::cout << "=================================\n";

    // Get Kenshi base address
    g_KenshiBase = GetKenshiBase();
    std::cout << "Kenshi Base: 0x" << std::hex << g_KenshiBase << std::dec << "\n";

    // Install hooks
    std::cout << "Installing hooks...\n";
    InstallHooks();
    std::cout << "Hooks installed!\n";

    // Connect to server
    std::cout << "Connecting to server at " << SERVER_ADDRESS << ":" << SERVER_PORT << "...\n";
    if (ConnectToServer(SERVER_ADDRESS, SERVER_PORT))
    {
        std::cout << "Connected to server!\n";

        // Start network thread
        g_Running = true;
        std::thread networkThread(NetworkThread);
        networkThread.detach();

        std::cout << "Network thread started!\n";
    }
    else
    {
        std::cout << "Failed to connect to server!\n";
    }

    std::cout << "\nKenshi Online Mod initialized!\n";
    std::cout << "You can now play Kenshi in multiplayer mode!\n";
    std::cout << "=================================\n";
}

void ShutdownMod()
{
    g_Running = false;

    if (g_Socket != INVALID_SOCKET)
    {
        closesocket(g_Socket);
        g_Socket = INVALID_SOCKET;
    }

    WSACleanup();

    FreeConsole();
}

#pragma endregion

#pragma region DLL Entry Point

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_ModuleHandle = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitializeMod, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        ShutdownMod();
        break;
    }

    return TRUE;
}

#pragma endregion

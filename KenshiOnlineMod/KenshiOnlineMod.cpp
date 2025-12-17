/*
 * Kenshi Online Mod DLL
 * Main entry point - Initializes overlay and networking
 * Integrates with Kenshi for multiplayer functionality
 */

#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>

#include "Overlay/Overlay.h"
#include "Overlay/OverlayUI.h"
#include "Hooks/D3D11Hook.h"
#include "Hooks/InputHook.h"
#include "Memory/KenshiGameBridge.h"
#include "Memory/PatternScanner.h"

#pragma comment(lib, "ws2_32.lib")

namespace KenshiOnline
{
    // Forward declarations
    void InitializeMod();
    void ShutdownMod();
    void NetworkThread();
    bool ConnectToServer(const char* address, int port);
    void DisconnectFromServer();
    void SendGameState();
    void SendGameStateV2();  // New GameBridge-based state sync
    void ReceiveCommands();
    void ProcessCommand(const std::string& command);
    void SetupUICallbacks();
    void SetupGameBridgeCallbacks();

    // GameBridge initialized flag
    std::atomic<bool> g_GameBridgeReady{ false };

    // Global state
    HMODULE g_ModuleHandle = nullptr;
    SOCKET g_Socket = INVALID_SOCKET;
    std::atomic<bool> g_Running{ false };
    std::atomic<bool> g_Connected{ false };
    std::atomic<bool> g_Authenticated{ false };
    std::mutex g_SocketMutex;
    std::thread g_NetworkThread;

    // Kenshi base address
    uintptr_t g_KenshiBase = 0;

    // User/Session info
    std::string g_Username = "";
    std::string g_Password = "";
    std::string g_SessionToken = "";
    std::string g_UserId = "";

    // Server info
    std::string g_ServerAddress = "127.0.0.1";
    int g_ServerPort = 5555;

    // Current lobby
    std::string g_CurrentLobbyId = "";
    bool g_IsInLobby = false;
    bool g_IsHost = false;

    #pragma region Memory Addresses

    // Kenshi memory offsets (relative to base)
    namespace Offsets
    {
        constexpr uintptr_t GameWorld = 0x24D8F40;
        constexpr uintptr_t PlayerSquadList = 0x24C5A20;
        constexpr uintptr_t PlayerSquadCount = 0x24C5A28;
        constexpr uintptr_t FactionList = 0x24D2100;
        constexpr uintptr_t FactionCount = 0x24D2108;
    }

    namespace Functions
    {
        constexpr uintptr_t SpawnCharacter = 0x8B3C80;
        constexpr uintptr_t IssueCommand = 0x8D5000;
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

    #pragma endregion

    #pragma region Helper Functions

    uintptr_t GetKenshiBase()
    {
        return (uintptr_t)GetModuleHandle(NULL);
    }

    template<typename T>
    T Read(uintptr_t address)
    {
        __try
        {
            return *(T*)address;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return T{};
        }
    }

    template<typename T>
    void Write(uintptr_t address, T value)
    {
        __try
        {
            DWORD oldProtect;
            VirtualProtect((void*)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
            *(T*)address = value;
            VirtualProtect((void*)address, sizeof(T), oldProtect, &oldProtect);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Ignore
        }
    }

    std::vector<Character*> GetPlayerCharacters()
    {
        std::vector<Character*> characters;

        if (g_KenshiBase == 0) return characters;

        uintptr_t listAddr = g_KenshiBase + Offsets::PlayerSquadList;
        uintptr_t countAddr = g_KenshiBase + Offsets::PlayerSquadCount;

        int count = Read<int>(countAddr);
        if (count <= 0 || count > 100) return characters;

        Character** list = Read<Character**>(listAddr);
        if (!list) return characters;

        for (int i = 0; i < count && i < 100; i++)
        {
            __try
            {
                if (list[i] != nullptr)
                {
                    characters.push_back(list[i]);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                break;
            }
        }

        return characters;
    }

    std::vector<std::string> SplitString(const std::string& str, char delimiter)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, delimiter))
        {
            tokens.push_back(token);
        }
        return tokens;
    }

    #pragma endregion

    #pragma region Network

    bool SendToServer(const std::string& message)
    {
        if (!g_Connected || g_Socket == INVALID_SOCKET)
            return false;

        std::lock_guard<std::mutex> lock(g_SocketMutex);
        std::string msg = message + "\n";
        return send(g_Socket, msg.c_str(), (int)msg.length(), 0) > 0;
    }

    bool ConnectToServer(const char* address, int port)
    {
        std::lock_guard<std::mutex> lock(g_SocketMutex);

        if (g_Socket != INVALID_SOCKET)
        {
            closesocket(g_Socket);
            g_Socket = INVALID_SOCKET;
        }

        g_Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_Socket == INVALID_SOCKET)
        {
            std::cout << "[Network] Failed to create socket\n";
            return false;
        }

        // Set non-blocking temporarily for connection timeout
        u_long mode = 1;
        ioctlsocket(g_Socket, FIONBIO, &mode);

        sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, address, &serverAddr.sin_addr);

        connect(g_Socket, (sockaddr*)&serverAddr, sizeof(serverAddr));

        // Wait for connection with timeout
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(g_Socket, &writeSet);

        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int result = select(0, nullptr, &writeSet, nullptr, &timeout);

        if (result <= 0)
        {
            std::cout << "[Network] Connection timeout\n";
            closesocket(g_Socket);
            g_Socket = INVALID_SOCKET;
            return false;
        }

        // Set back to blocking
        mode = 0;
        ioctlsocket(g_Socket, FIONBIO, &mode);

        // Set socket options
        int flag = 1;
        setsockopt(g_Socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

        g_Connected = true;
        g_ServerAddress = address;
        g_ServerPort = port;

        std::cout << "[Network] Connected to " << address << ":" << port << "\n";
        return true;
    }

    void DisconnectFromServer()
    {
        std::lock_guard<std::mutex> lock(g_SocketMutex);

        g_Connected = false;
        g_Authenticated = false;
        g_IsInLobby = false;

        if (g_Socket != INVALID_SOCKET)
        {
            shutdown(g_Socket, SD_BOTH);
            closesocket(g_Socket);
            g_Socket = INVALID_SOCKET;
        }

        std::cout << "[Network] Disconnected from server\n";
    }

    void SendGameState()
    {
        if (!g_Connected || !g_Authenticated || g_Socket == INVALID_SOCKET)
            return;

        try
        {
            auto characters = GetPlayerCharacters();
            std::vector<PlayerInfo> playerList;

            for (auto* character : characters)
            {
                if (character == nullptr) continue;

                char buffer[512];
                snprintf(buffer, sizeof(buffer),
                    "STATE|%d|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%d|%d",
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

                SendToServer(buffer);

                // Update overlay
                PlayerInfo info;
                if (character->name)
                    info.name = character->name;
                else
                    info.name = "Character " + std::to_string(character->characterID);

                info.id = std::to_string(character->characterID);
                info.health = character->health;
                info.maxHealth = character->maxHealth;
                info.x = character->position.x;
                info.y = character->position.y;
                info.z = character->position.z;
                info.isOnline = true;
                info.factionId = character->factionID;

                playerList.push_back(info);
            }

            // Update overlay player list
            Overlay::Get().UpdatePlayerList(playerList);
        }
        catch (...)
        {
            // Ignore errors
        }
    }

    // New GameBridge-based state synchronization
    void SendGameStateV2()
    {
        if (!g_Connected || !g_Authenticated || g_Socket == INVALID_SOCKET)
            return;

        if (!g_GameBridgeReady)
            return;

        try
        {
            auto& bridge = Kenshi::GameBridge::Get();

            // Get all player characters using the GameBridge
            auto playerStates = bridge.GetAllPlayerCharacters();
            std::vector<PlayerInfo> playerList;

            for (const auto& state : playerStates)
            {
                // Send detailed state to server
                char buffer[1024];
                snprintf(buffer, sizeof(buffer),
                    "STATE2|%u|%s|%.2f|%.2f|%.2f|%.4f|%.4f|%.4f|%.4f|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%d|%d|%d|%d|%llu",
                    state.characterId,
                    state.name,
                    state.position.x, state.position.y, state.position.z,
                    state.rotation.x, state.rotation.y, state.rotation.z, state.rotation.w,
                    state.health, state.maxHealth, state.bloodLevel, state.hunger, state.thirst,
                    static_cast<int>(state.state),
                    state.factionId,
                    state.isInCombat ? 1 : 0,
                    state.isUnconscious ? 1 : 0,
                    state.isDead ? 1 : 0,
                    state.syncTick
                );

                SendToServer(buffer);

                // Update overlay
                PlayerInfo info;
                info.name = state.name;
                info.id = std::to_string(state.characterId);
                info.health = state.health;
                info.maxHealth = state.maxHealth;
                info.x = state.position.x;
                info.y = state.position.y;
                info.z = state.position.z;
                info.isOnline = true;
                info.factionId = state.factionId;

                playerList.push_back(info);
            }

            // Send world state periodically (every 60 ticks / ~3 seconds)
            static uint64_t lastWorldStateTick = 0;
            uint64_t currentTick = bridge.GetCurrentTick();

            if (currentTick - lastWorldStateTick >= 60)
            {
                Kenshi::WorldState worldState;
                if (bridge.GetWorldState(worldState))
                {
                    char worldBuffer[512];
                    snprintf(worldBuffer, sizeof(worldBuffer),
                        "WORLD|%.2f|%d|%d|%.2f|%d|%.2f|%.2f|%d|%llu",
                        worldState.gameTime,
                        worldState.gameDay,
                        worldState.gameYear,
                        worldState.timeScale,
                        static_cast<int>(worldState.weather),
                        worldState.weatherIntensity,
                        worldState.temperature,
                        worldState.playerMoney,
                        worldState.syncTick
                    );
                    SendToServer(worldBuffer);
                }
                lastWorldStateTick = currentTick;
            }

            // Update overlay player list
            Overlay::Get().UpdatePlayerList(playerList);

            // Increment sync tick
            bridge.IncrementTick();
        }
        catch (...)
        {
            // Ignore errors
        }
    }

    // Set up callbacks for game events
    void SetupGameBridgeCallbacks()
    {
        if (!g_GameBridgeReady)
            return;

        auto& bridge = Kenshi::GameBridge::Get();

        // Combat event callback - send to server
        bridge.SetCombatCallback([](const Kenshi::CombatEventSync& event) {
            if (!g_Connected || !g_Authenticated)
                return;

            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                "COMBAT|%u|%u|%d|%d|%d|%.2f|%d|%d|%d|%d|%.2f",
                event.attackerId,
                event.defenderId,
                static_cast<int>(event.attackType),
                static_cast<int>(event.damageType),
                static_cast<int>(event.targetLimb),
                event.damage,
                event.wasBlocked ? 1 : 0,
                event.wasDodged ? 1 : 0,
                event.isCritical ? 1 : 0,
                event.causedKnockdown ? 1 : 0,
                event.timestamp
            );
            SendToServer(buffer);
        });

        // Inventory change callback
        bridge.SetInventoryCallback([](const Kenshi::InventoryEvent& event) {
            if (!g_Connected || !g_Authenticated)
                return;

            char buffer[128];
            snprintf(buffer, sizeof(buffer),
                "INVENTORY|%u|%u|%u|%d|%d|%.2f",
                event.characterId,
                event.itemId,
                event.itemTemplateId,
                event.quantityChange,
                event.slotIndex,
                event.timestamp
            );
            SendToServer(buffer);
        });
    }

    void ReceiveCommands()
    {
        if (!g_Connected || g_Socket == INVALID_SOCKET)
            return;

        // Check if data available
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(g_Socket, &readSet);

        timeval timeout = { 0, 0 };  // Non-blocking
        if (select(0, &readSet, nullptr, nullptr, &timeout) <= 0)
            return;

        char buffer[4096];
        int bytesReceived = recv(g_Socket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived <= 0)
        {
            if (bytesReceived == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
            {
                std::cout << "[Network] Server disconnected\n";
                g_Connected = false;
                g_Authenticated = false;
                Overlay::Get().SetConnectionStatus(ConnectionStatus::Disconnected);
                Overlay::Get().AddSystemMessage("Disconnected from server");

                if (auto* ui = Overlay::Get().GetUI())
                {
                    ui->SetLoggedIn(false);
                    ui->SetScreen(UIScreen::Login);
                }
            }
            return;
        }

        buffer[bytesReceived] = '\0';

        // Parse commands
        std::string data(buffer);
        size_t pos = 0;
        while ((pos = data.find('\n')) != std::string::npos)
        {
            std::string line = data.substr(0, pos);
            data.erase(0, pos + 1);

            if (!line.empty())
            {
                ProcessCommand(line);
            }
        }
    }

    void ProcessCommand(const std::string& command)
    {
        auto parts = SplitString(command, '|');
        if (parts.empty()) return;

        std::string cmd = parts[0];

        if (cmd == "PING")
        {
            SendToServer("PONG|" + (parts.size() > 1 ? parts[1] : "0"));
        }
        else if (cmd == "LOGIN_OK")
        {
            // Login successful: LOGIN_OK|userId|sessionToken|username
            g_Authenticated = true;
            if (parts.size() >= 4)
            {
                g_UserId = parts[1];
                g_SessionToken = parts[2];
                g_Username = parts[3];
            }

            Overlay::Get().SetConnectionStatus(ConnectionStatus::Connected);

            if (auto* ui = Overlay::Get().GetUI())
            {
                // Set user profile
                UserProfile profile;
                profile.id = g_UserId;
                profile.username = g_Username;
                profile.level = 1;
                profile.gamesPlayed = 0;
                profile.totalPlayTime = 0;
                ui->SetUserProfile(profile);

                ui->SetLoggedIn(true, g_Username);
            }

            std::cout << "[Auth] Login successful! User: " << g_Username << "\n";
        }
        else if (cmd == "LOGIN_FAIL")
        {
            // Login failed: LOGIN_FAIL|reason
            std::string reason = parts.size() > 1 ? parts[1] : "Invalid credentials";

            if (auto* ui = Overlay::Get().GetUI())
            {
                ui->SetLoginError(reason);
            }

            std::cout << "[Auth] Login failed: " << reason << "\n";
        }
        else if (cmd == "REGISTER_OK")
        {
            // Registration successful
            if (auto* ui = Overlay::Get().GetUI())
            {
                ui->ShowSuccess("Account created! Please sign in.");
                ui->SetScreen(UIScreen::Login);
            }
        }
        else if (cmd == "REGISTER_FAIL")
        {
            std::string reason = parts.size() > 1 ? parts[1] : "Registration failed";
            if (auto* ui = Overlay::Get().GetUI())
            {
                ui->ShowError(reason);
                ui->SetConnecting(false);
            }
        }
        else if (cmd == "SERVER_LIST")
        {
            // Server list: SERVER_LIST|count then SERVER|id|name|address|port|players|maxPlayers|ping|official|password|mode|region|version
            // Handle in subsequent SERVER messages
        }
        else if (cmd == "SERVER")
        {
            // Parse server info
            if (parts.size() >= 12)
            {
                ServerInfo server;
                server.id = parts[1];
                server.name = parts[2];
                server.address = parts[3];
                server.port = std::stoi(parts[4]);
                server.playerCount = std::stoi(parts[5]);
                server.maxPlayers = std::stoi(parts[6]);
                server.ping = std::stoi(parts[7]);
                server.isOfficial = parts[8] == "1";
                server.hasPassword = parts[9] == "1";
                server.gameMode = parts[10];
                server.region = parts[11];
                server.version = parts.size() > 12 ? parts[12] : "1.0";

                // Add to UI (you'd accumulate these and call SetServerList)
            }
        }
        else if (cmd == "SERVERS_END")
        {
            // Server list complete - would trigger UI update
        }
        else if (cmd == "FRIENDS_LIST")
        {
            // Friends list start
        }
        else if (cmd == "FRIEND")
        {
            // Parse friend info: FRIEND|id|username|status|server|lobby|pending|incoming|lastOnline
            if (parts.size() >= 9)
            {
                FriendInfo friendInfo;
                friendInfo.id = parts[1];
                friendInfo.username = parts[2];
                friendInfo.status = static_cast<FriendStatus>(std::stoi(parts[3]));
                friendInfo.currentServer = parts[4];
                friendInfo.currentLobby = parts[5];
                friendInfo.isPendingRequest = parts[6] == "1";
                friendInfo.isIncomingRequest = parts[7] == "1";
                friendInfo.lastOnline = std::stoull(parts[8]);

                // Add to accumulated list
            }
        }
        else if (cmd == "FRIENDS_END")
        {
            // Friends list complete
        }
        else if (cmd == "LOBBY_CREATED")
        {
            // Lobby created: LOBBY_CREATED|lobbyId|name
            if (parts.size() >= 3)
            {
                g_CurrentLobbyId = parts[1];
                g_IsInLobby = true;
                g_IsHost = true;

                LobbyInfo lobby;
                lobby.id = parts[1];
                lobby.name = parts[2];
                lobby.hostName = g_Username;
                lobby.playerCount = 1;
                lobby.maxPlayers = 4;
                lobby.isPrivate = false;
                lobby.state = LobbyState::Waiting;
                lobby.gameMode = "Coop";

                LobbyPlayer localPlayer;
                localPlayer.id = g_UserId;
                localPlayer.username = g_Username;
                localPlayer.isReady = false;
                localPlayer.isHost = true;
                localPlayer.characterLevel = 1;
                lobby.players.push_back(localPlayer);

                if (auto* ui = Overlay::Get().GetUI())
                {
                    ui->SetCurrentLobby(lobby);
                    ui->ShowSuccess("Lobby created!");
                }
            }
        }
        else if (cmd == "LOBBY_JOINED")
        {
            // Joined a lobby: LOBBY_JOINED|lobbyId|name|hostName|playerCount|maxPlayers|gameMode
            if (parts.size() >= 7)
            {
                g_CurrentLobbyId = parts[1];
                g_IsInLobby = true;
                g_IsHost = false;

                LobbyInfo lobby;
                lobby.id = parts[1];
                lobby.name = parts[2];
                lobby.hostName = parts[3];
                lobby.playerCount = std::stoi(parts[4]);
                lobby.maxPlayers = std::stoi(parts[5]);
                lobby.gameMode = parts[6];
                lobby.state = LobbyState::Waiting;

                if (auto* ui = Overlay::Get().GetUI())
                {
                    ui->SetCurrentLobby(lobby);
                    ui->ShowSuccess("Joined lobby!");
                }
            }
        }
        else if (cmd == "LOBBY_PLAYER")
        {
            // Player in lobby: LOBBY_PLAYER|id|username|isReady|isHost|level|character
            // Would add to current lobby's player list
        }
        else if (cmd == "LOBBY_LEFT")
        {
            g_CurrentLobbyId = "";
            g_IsInLobby = false;
            g_IsHost = false;

            if (auto* ui = Overlay::Get().GetUI())
            {
                ui->ClearCurrentLobby();
                ui->SetScreen(UIScreen::MainMenu);
            }
        }
        else if (cmd == "GAME_START")
        {
            // Game starting!
            if (auto* ui = Overlay::Get().GetUI())
            {
                ui->ShowSuccess("Game starting!");
                ui->SetScreen(UIScreen::InGame);
            }
        }
        else if (cmd == "CHAT")
        {
            // Chat message: CHAT|sender|message|type
            if (parts.size() >= 3)
            {
                ChatMessage msg;
                msg.sender = parts[1];
                msg.message = parts[2];
                msg.timestamp = time(nullptr);
                msg.type = parts.size() > 3 ? std::stoi(parts[3]) : 0;

                Overlay::Get().AddChatMessage(msg);
            }
        }
        else if (cmd == "PLAYER_JOIN")
        {
            if (parts.size() > 1)
            {
                Overlay::Get().AddSystemMessage(parts[1] + " joined the server");
            }
        }
        else if (cmd == "PLAYER_LEAVE")
        {
            if (parts.size() > 1)
            {
                Overlay::Get().AddSystemMessage(parts[1] + " left the server");
                Overlay::Get().RemovePlayer(parts[1]);
            }
        }
        else if (cmd == "LATENCY")
        {
            if (parts.size() > 1)
            {
                int ping = std::stoi(parts[1]);
                Overlay::Get().SetPing(ping);
            }
        }
        else if (cmd == "MOVE")
        {
            // Movement command: MOVE|charID|x|y|z
            if (parts.size() >= 5)
            {
                uint32_t charID = static_cast<uint32_t>(std::stoul(parts[1]));
                float x = std::stof(parts[2]);
                float y = std::stof(parts[3]);
                float z = std::stof(parts[4]);

                if (g_GameBridgeReady)
                {
                    Kenshi::Vector3 pos{ x, y, z };
                    Kenshi::GameBridge::Get().SetCharacterPosition(charID, pos);
                }
                else
                {
                    // Legacy fallback
                    auto characters = GetPlayerCharacters();
                    for (auto* character : characters)
                    {
                        if (character->characterID == static_cast<int>(charID))
                        {
                            Write<Vector3>((uintptr_t)&character->position, { x, y, z });
                            break;
                        }
                    }
                }
            }
        }
        else if (cmd == "STATE_UPDATE")
        {
            // Full state update from server: STATE_UPDATE|charID|x|y|z|qx|qy|qz|qw|health|blood|hunger|state|inCombat
            if (parts.size() >= 14 && g_GameBridgeReady)
            {
                Kenshi::PlayerState state;
                state.characterId = static_cast<uint32_t>(std::stoul(parts[1]));
                state.position.x = std::stof(parts[2]);
                state.position.y = std::stof(parts[3]);
                state.position.z = std::stof(parts[4]);
                state.rotation.x = std::stof(parts[5]);
                state.rotation.y = std::stof(parts[6]);
                state.rotation.z = std::stof(parts[7]);
                state.rotation.w = std::stof(parts[8]);
                state.health = std::stof(parts[9]);
                state.bloodLevel = std::stof(parts[10]);
                state.hunger = std::stof(parts[11]);
                state.state = static_cast<Kenshi::AIState>(std::stoi(parts[12]));
                state.isInCombat = parts[13] == "1" ? 1 : 0;

                Kenshi::GameBridge::Get().SetPlayerState(state);
            }
        }
        else if (cmd == "COMBAT_EVENT")
        {
            // Combat event from server: COMBAT_EVENT|attackerId|defenderId|attackType|damageType|limb|damage|blocked|dodged|crit|knockdown
            if (parts.size() >= 11 && g_GameBridgeReady)
            {
                Kenshi::CombatEventSync event;
                event.attackerId = static_cast<uint32_t>(std::stoul(parts[1]));
                event.defenderId = static_cast<uint32_t>(std::stoul(parts[2]));
                event.attackType = static_cast<Kenshi::AttackType>(std::stoi(parts[3]));
                event.damageType = static_cast<Kenshi::DamageType>(std::stoi(parts[4]));
                event.targetLimb = static_cast<Kenshi::LimbType>(std::stoi(parts[5]));
                event.damage = std::stof(parts[6]);
                event.wasBlocked = parts[7] == "1" ? 1 : 0;
                event.wasDodged = parts[8] == "1" ? 1 : 0;
                event.isCritical = parts[9] == "1" ? 1 : 0;
                event.causedKnockdown = parts[10] == "1" ? 1 : 0;

                Kenshi::GameBridge::Get().ApplyCombatEvent(event);
            }
        }
        else if (cmd == "SET_TIME")
        {
            // Server time sync: SET_TIME|gameTime
            if (parts.size() >= 2 && g_GameBridgeReady)
            {
                float gameTime = std::stof(parts[1]);
                Kenshi::GameBridge::Get().SetGameTime(gameTime);
            }
        }
        else if (cmd == "SET_WEATHER")
        {
            // Weather sync: SET_WEATHER|weatherType|intensity
            if (parts.size() >= 3 && g_GameBridgeReady)
            {
                auto weather = static_cast<Kenshi::WeatherType>(std::stoi(parts[1]));
                float intensity = std::stof(parts[2]);
                Kenshi::GameBridge::Get().SetWeather(weather, intensity);
            }
        }
        else if (cmd == "SQUAD_ORDER")
        {
            // Squad order from server: SQUAD_ORDER|squadId|order|targetX|targetY|targetZ
            if (parts.size() >= 6 && g_GameBridgeReady)
            {
                uint32_t squadId = static_cast<uint32_t>(std::stoul(parts[1]));
                auto order = static_cast<Kenshi::SquadOrder>(std::stoi(parts[2]));
                Kenshi::Vector3 target{
                    std::stof(parts[3]),
                    std::stof(parts[4]),
                    std::stof(parts[5])
                };

                Kenshi::GameBridge::Get().IssueSquadOrder(squadId, order, target);
            }
        }
        else if (cmd == "DAMAGE")
        {
            // Apply damage from server: DAMAGE|charId|limbType|damage|damageType
            if (parts.size() >= 5 && g_GameBridgeReady)
            {
                uint32_t charId = static_cast<uint32_t>(std::stoul(parts[1]));
                auto limb = static_cast<Kenshi::LimbType>(std::stoi(parts[2]));
                float damage = std::stof(parts[3]);
                auto damageType = static_cast<Kenshi::DamageType>(std::stoi(parts[4]));

                Kenshi::GameBridge::Get().ApplyDamage(charId, limb, damage, damageType);
            }
        }
        else if (cmd == "ANIMATION")
        {
            // Play animation: ANIMATION|charId|animType|time|speed
            if (parts.size() >= 5 && g_GameBridgeReady)
            {
                uint32_t charId = static_cast<uint32_t>(std::stoul(parts[1]));
                auto anim = static_cast<Kenshi::AnimationType>(std::stoi(parts[2]));
                float time = std::stof(parts[3]);
                float speed = std::stof(parts[4]);

                Kenshi::GameBridge::Get().SyncAnimation(charId, anim, time, speed);
            }
        }
    }

    void NetworkThread()
    {
        std::cout << "[Network] Network thread started\n";

        while (g_Running)
        {
            if (g_Connected)
            {
                if (g_Authenticated)
                {
                    // Use GameBridge for state sync if available
                    if (g_GameBridgeReady)
                    {
                        SendGameStateV2();
                    }
                    else
                    {
                        SendGameState();  // Fallback to legacy method
                    }
                }
                ReceiveCommands();
            }

            Sleep(50);  // 20 Hz update rate
        }

        std::cout << "[Network] Network thread stopped\n";
    }

    #pragma endregion

    #pragma region UI Callbacks

    void OnLoginRequested(const std::string& username, const std::string& password)
    {
        std::cout << "[UI] Login requested for: " << username << "\n";

        g_Username = username;
        g_Password = password;

        // Connect to auth server if not connected
        if (!g_Connected)
        {
            Overlay::Get().SetConnectionStatus(ConnectionStatus::Connecting);

            std::thread([username, password]() {
                if (ConnectToServer(g_ServerAddress.c_str(), g_ServerPort))
                {
                    // Send login request
                    SendToServer("LOGIN|" + username + "|" + password);
                }
                else
                {
                    Overlay::Get().SetConnectionStatus(ConnectionStatus::Error);
                    if (auto* ui = Overlay::Get().GetUI())
                    {
                        ui->SetLoginError("Could not connect to server");
                    }
                }
            }).detach();
        }
        else
        {
            // Already connected, just send login
            SendToServer("LOGIN|" + username + "|" + password);
        }
    }

    void OnRegisterRequested(const std::string& username, const std::string& password, const std::string& email)
    {
        std::cout << "[UI] Register requested for: " << username << "\n";

        if (!g_Connected)
        {
            std::thread([username, password, email]() {
                if (ConnectToServer(g_ServerAddress.c_str(), g_ServerPort))
                {
                    SendToServer("REGISTER|" + username + "|" + password + "|" + email);
                }
                else
                {
                    if (auto* ui = Overlay::Get().GetUI())
                    {
                        ui->ShowError("Could not connect to server");
                        ui->SetConnecting(false);
                    }
                }
            }).detach();
        }
        else
        {
            SendToServer("REGISTER|" + username + "|" + password + "|" + email);
        }
    }

    void OnRefreshServersRequested()
    {
        std::cout << "[UI] Refresh servers requested\n";
        SendToServer("GET_SERVERS");
    }

    void OnJoinServerRequested(const ServerInfo& server, const std::string& password)
    {
        std::cout << "[UI] Join server requested: " << server.name << "\n";

        std::string cmd = "JOIN_SERVER|" + server.id;
        if (!password.empty())
        {
            cmd += "|" + password;
        }
        SendToServer(cmd);
    }

    void OnCreateLobbyRequested(const std::string& name, int maxPlayers, bool isPrivate, const std::string& password)
    {
        std::cout << "[UI] Create lobby requested: " << name << "\n";

        std::string cmd = "CREATE_LOBBY|" + name + "|" + std::to_string(maxPlayers) + "|" +
                          (isPrivate ? "1" : "0");
        if (!password.empty())
        {
            cmd += "|" + password;
        }
        SendToServer(cmd);
    }

    void OnJoinLobbyRequested(const std::string& lobbyId, const std::string& password)
    {
        std::cout << "[UI] Join lobby requested: " << lobbyId << "\n";

        std::string cmd = "JOIN_LOBBY|" + lobbyId;
        if (!password.empty())
        {
            cmd += "|" + password;
        }
        SendToServer(cmd);
    }

    void OnLeaveLobbyRequested()
    {
        std::cout << "[UI] Leave lobby requested\n";
        SendToServer("LEAVE_LOBBY|" + g_CurrentLobbyId);
    }

    void OnReadyUpRequested(bool ready)
    {
        std::cout << "[UI] Ready up: " << (ready ? "true" : "false") << "\n";
        SendToServer("READY|" + std::string(ready ? "1" : "0"));
    }

    void OnStartGameRequested()
    {
        std::cout << "[UI] Start game requested\n";
        if (g_IsHost)
        {
            SendToServer("START_GAME|" + g_CurrentLobbyId);
        }
    }

    void OnAddFriendRequested(const std::string& username)
    {
        std::cout << "[UI] Add friend requested: " << username << "\n";
        SendToServer("ADD_FRIEND|" + username);
    }

    void OnRemoveFriendRequested(const std::string& friendId)
    {
        std::cout << "[UI] Remove friend requested: " << friendId << "\n";
        SendToServer("REMOVE_FRIEND|" + friendId);
    }

    void OnInviteFriendRequested(const std::string& friendId)
    {
        std::cout << "[UI] Invite friend requested: " << friendId << "\n";
        SendToServer("INVITE_FRIEND|" + friendId + "|" + g_CurrentLobbyId);
    }

    void OnAcceptFriendRequested(const std::string& friendId)
    {
        std::cout << "[UI] Accept friend requested: " << friendId << "\n";
        SendToServer("ACCEPT_FRIEND|" + friendId);
    }

    void OnLogoutRequested()
    {
        std::cout << "[UI] Logout requested\n";
        SendToServer("LOGOUT");
        g_Authenticated = false;
        g_SessionToken = "";
        g_UserId = "";
    }

    void OnChatRequested(const std::string& message)
    {
        if (!g_Connected || !g_Authenticated)
            return;

        SendToServer("CHAT|" + g_Username + "|" + message + "|0");

        // Add to local chat
        ChatMessage msg;
        msg.sender = g_Username;
        msg.message = message;
        msg.timestamp = time(nullptr);
        msg.type = 0;

        Overlay::Get().AddChatMessage(msg);
    }

    void OnDisconnectRequested()
    {
        DisconnectFromServer();
        Overlay::Get().SetConnectionStatus(ConnectionStatus::Disconnected);
        Overlay::Get().AddSystemMessage("Disconnected from server");
    }

    void SetupUICallbacks()
    {
        auto* ui = Overlay::Get().GetUI();
        if (!ui)
            return;

        // Authentication callbacks
        ui->SetLoginCallback(OnLoginRequested);
        ui->SetRegisterCallback(OnRegisterRequested);
        ui->SetLogoutCallback(OnLogoutRequested);

        // Server browser callbacks
        ui->SetRefreshServersCallback(OnRefreshServersRequested);
        ui->SetJoinServerCallback(OnJoinServerRequested);

        // Lobby callbacks
        ui->SetCreateLobbyCallback(OnCreateLobbyRequested);
        ui->SetJoinLobbyCallback(OnJoinLobbyRequested);
        ui->SetLeaveLobbyCallback(OnLeaveLobbyRequested);
        ui->SetReadyUpCallback(OnReadyUpRequested);
        ui->SetStartGameCallback(OnStartGameRequested);

        // Friends callbacks
        ui->SetAddFriendCallback(OnAddFriendRequested);
        ui->SetRemoveFriendCallback(OnRemoveFriendRequested);
        ui->SetInviteFriendCallback(OnInviteFriendRequested);
        ui->SetAcceptFriendCallback(OnAcceptFriendRequested);

        // Basic overlay callbacks
        Overlay::Get().SetConnectCallback([](const std::string& address, int port, const std::string& username, const std::string& password) {
            OnLoginRequested(username, password);
        });
        Overlay::Get().SetChatCallback(OnChatRequested);
        Overlay::Get().SetDisconnectCallback(OnDisconnectRequested);
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

        std::cout << "========================================\n";
        std::cout << "   Kenshi Online Mod v2.0\n";
        std::cout << "   With In-Game Overlay\n";
        std::cout << "========================================\n\n";

        // Initialize Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::cout << "[ERROR] Failed to initialize Winsock\n";
            return;
        }

        // Get Kenshi base address
        g_KenshiBase = GetKenshiBase();
        std::cout << "[Init] Kenshi Base: 0x" << std::hex << g_KenshiBase << std::dec << "\n";

        // Wait for game to fully load
        std::cout << "[Init] Waiting for game to initialize...\n";
        Sleep(3000);

        // Initialize GameBridge for memory access
        std::cout << "[Init] Initializing GameBridge...\n";
        if (Kenshi::GameBridge::Get().Initialize())
        {
            g_GameBridgeReady = true;
            std::cout << "[Init] GameBridge initialized successfully!\n";

            // Set up game event callbacks
            SetupGameBridgeCallbacks();

            // Dump found addresses for debugging
            Kenshi::PatternScanner::Get().DumpAddresses("kenshi_addresses.txt");
        }
        else
        {
            std::cout << "[WARNING] GameBridge initialization failed: "
                      << Kenshi::GameBridge::Get().GetLastError() << "\n";
            std::cout << "[WARNING] Falling back to legacy memory access\n";
        }

        // Initialize overlay
        std::cout << "[Init] Initializing overlay system...\n";

        // Initialize overlay (will wait for D3D11)
        if (!Overlay::Get().Initialize())
        {
            std::cout << "[ERROR] Failed to initialize overlay\n";
            std::cout << "[ERROR] The overlay requires DirectX 11\n";
        }
        else
        {
            std::cout << "[Init] Overlay initialized successfully!\n";

            // Set up UI callbacks after overlay is initialized
            SetupUICallbacks();
        }

        // Start network thread
        g_Running = true;
        g_NetworkThread = std::thread(NetworkThread);

        std::cout << "\n========================================\n";
        std::cout << "   Kenshi Online Mod Loaded!\n";
        std::cout << "   Press INSERT to open the overlay\n";
        std::cout << "========================================\n\n";

        // Add initial system message
        Overlay::Get().AddSystemMessage("Kenshi Online Mod loaded!");
        Overlay::Get().AddSystemMessage("Sign in to start playing");
    }

    void ShutdownMod()
    {
        std::cout << "[Shutdown] Shutting down Kenshi Online Mod...\n";

        g_Running = false;

        // Stop network
        DisconnectFromServer();

        // Wait for network thread
        if (g_NetworkThread.joinable())
        {
            g_NetworkThread.join();
        }

        // Shutdown GameBridge
        if (g_GameBridgeReady)
        {
            std::cout << "[Shutdown] Shutting down GameBridge...\n";
            Kenshi::GameBridge::Get().Shutdown();
            g_GameBridgeReady = false;
        }

        // Shutdown overlay
        Overlay::Get().Shutdown();

        // Cleanup Winsock
        WSACleanup();

        // Free console
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
}

// DllMain wrapper for proper linkage
extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return KenshiOnline::DllMain(hModule, ul_reason_for_call, lpReserved);
}

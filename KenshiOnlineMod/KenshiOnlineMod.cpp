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

#include "Overlay/Overlay.h"
#include "Hooks/D3D11Hook.h"
#include "Hooks/InputHook.h"

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
    void ReceiveCommands();
    void ProcessCommand(const std::string& command);

    // Global state
    HMODULE g_ModuleHandle = nullptr;
    SOCKET g_Socket = INVALID_SOCKET;
    std::atomic<bool> g_Running{ false };
    std::atomic<bool> g_Connected{ false };
    std::mutex g_SocketMutex;
    std::thread g_NetworkThread;

    // Kenshi base address
    uintptr_t g_KenshiBase = 0;

    // Configuration
    std::string g_ServerAddress = "127.0.0.1";
    int g_ServerPort = 5555;
    std::string g_Username = "";
    std::string g_Password = "";

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

    #pragma endregion

    #pragma region Network

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

        // Send handshake
        std::string handshake = "KENSHI_ONLINE|HELLO|" + g_Username + "\n";
        send(g_Socket, handshake.c_str(), (int)handshake.length(), 0);

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
        if (!g_Connected || g_Socket == INVALID_SOCKET) return;

        std::lock_guard<std::mutex> lock(g_SocketMutex);

        try
        {
            auto characters = GetPlayerCharacters();
            std::vector<PlayerInfo> playerList;

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

                // Update overlay
                PlayerInfo info;
                if (character->name)
                    info.name = character->name;
                else
                    info.name = "Character " + std::to_string(character->characterID);

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

    void ReceiveCommands()
    {
        if (!g_Connected || g_Socket == INVALID_SOCKET) return;

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
                Overlay::Get().SetConnectionStatus(ConnectionStatus::Disconnected);
                Overlay::Get().AddSystemMessage("Disconnected from server");
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
        // Parse command format: COMMAND|param1|param2|...
        size_t delimPos = command.find('|');
        std::string cmd = (delimPos != std::string::npos) ? command.substr(0, delimPos) : command;
        std::string params = (delimPos != std::string::npos) ? command.substr(delimPos + 1) : "";

        if (cmd == "PING")
        {
            // Respond to ping
            std::lock_guard<std::mutex> lock(g_SocketMutex);
            std::string pong = "PONG|" + params + "\n";
            send(g_Socket, pong.c_str(), (int)pong.length(), 0);
        }
        else if (cmd == "CHAT")
        {
            // Chat message: CHAT|sender|message
            size_t sep = params.find('|');
            if (sep != std::string::npos)
            {
                ChatMessage msg;
                msg.sender = params.substr(0, sep);
                msg.message = params.substr(sep + 1);
                msg.timestamp = time(nullptr);
                msg.type = 0;  // Global

                Overlay::Get().AddChatMessage(msg);
            }
        }
        else if (cmd == "PLAYER_JOIN")
        {
            Overlay::Get().AddSystemMessage(params + " joined the server");
        }
        else if (cmd == "PLAYER_LEAVE")
        {
            Overlay::Get().AddSystemMessage(params + " left the server");
            Overlay::Get().RemovePlayer(params);
        }
        else if (cmd == "PLAYERS")
        {
            // Player count update
            try
            {
                int count = std::stoi(params);
                // Update is handled elsewhere
            }
            catch (...) {}
        }
        else if (cmd == "LATENCY")
        {
            // Ping/latency update
            try
            {
                int ping = std::stoi(params);
                Overlay::Get().SetPing(ping);
            }
            catch (...) {}
        }
        else if (cmd == "MOVE")
        {
            // Movement command: MOVE|charID|x|y|z
            int charID;
            float x, y, z;
            if (sscanf(params.c_str(), "%d|%f|%f|%f", &charID, &x, &y, &z) == 4)
            {
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
    }

    void NetworkThread()
    {
        std::cout << "[Network] Network thread started\n";

        while (g_Running)
        {
            if (g_Connected)
            {
                SendGameState();
                ReceiveCommands();
            }

            Sleep(50);  // 20 Hz update rate
        }

        std::cout << "[Network] Network thread stopped\n";
    }

    #pragma endregion

    #pragma region Overlay Callbacks

    void OnConnectRequested(const std::string& address, int port, const std::string& username, const std::string& password)
    {
        std::cout << "[Overlay] Connect requested to " << address << ":" << port << "\n";

        g_Username = username;
        g_Password = password;

        Overlay::Get().SetConnectionStatus(ConnectionStatus::Connecting);

        // Connect in background thread to not block UI
        std::thread([address, port]() {
            if (ConnectToServer(address.c_str(), port))
            {
                Overlay::Get().SetConnectionStatus(ConnectionStatus::Connected);
                Overlay::Get().SetServerAddress(address);
                Overlay::Get().AddSystemMessage("Connected to server!");
            }
            else
            {
                Overlay::Get().SetConnectionStatus(ConnectionStatus::Error);
                Overlay::Get().ShowError("Failed to connect to server");
            }
        }).detach();
    }

    void OnChatRequested(const std::string& message)
    {
        if (!g_Connected || g_Socket == INVALID_SOCKET) return;

        std::lock_guard<std::mutex> lock(g_SocketMutex);

        std::string chatCmd = "CHAT|" + g_Username + "|" + message + "\n";
        send(g_Socket, chatCmd.c_str(), (int)chatCmd.length(), 0);

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

        // Initialize overlay
        std::cout << "[Init] Initializing overlay system...\n";

        // Set up overlay callbacks
        Overlay::Get().SetConnectCallback(OnConnectRequested);
        Overlay::Get().SetChatCallback(OnChatRequested);
        Overlay::Get().SetDisconnectCallback(OnDisconnectRequested);

        // Initialize overlay (will wait for D3D11)
        if (!Overlay::Get().Initialize())
        {
            std::cout << "[ERROR] Failed to initialize overlay\n";
            std::cout << "[ERROR] The overlay requires DirectX 11\n";
        }
        else
        {
            std::cout << "[Init] Overlay initialized successfully!\n";
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
        Overlay::Get().AddSystemMessage("Press INSERT to open the overlay");
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

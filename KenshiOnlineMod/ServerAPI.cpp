#include "ServerAPI.h"
#include <iostream>
#include <unordered_map>
#include <mutex>

namespace KenshiOnline
{
    static std::mutex g_APILock;

    // Singleton implementation
    ServerAPIManager& ServerAPIManager::GetInstance()
    {
        static ServerAPIManager instance;
        return instance;
    }

    ServerAPIManager::ServerAPIManager()
        : m_OgreController(nullptr)
        , m_AnimController(nullptr)
        , m_NetworkController(nullptr)
        , m_Initialized(false)
        , m_OnPlayerJoin(nullptr)
        , m_OnPlayerLeave(nullptr)
        , m_OnPlayerAction(nullptr)
        , m_OnCombatEvent(nullptr)
        , m_OnChatMessage(nullptr)
    {
    }

    ServerAPIManager::~ServerAPIManager()
    {
        Shutdown();
    }

    bool ServerAPIManager::Initialize()
    {
        if (m_Initialized)
            return true;

        try
        {
            m_OgreController = new OgreController();
            if (!m_OgreController->Initialize())
            {
                std::cout << "Failed to initialize OGRE controller\n";
                delete m_OgreController;
                m_OgreController = nullptr;
                return false;
            }

            m_AnimController = new AnimationController(m_OgreController);
            m_NetworkController = new NetworkController(m_OgreController, m_AnimController);

            m_Initialized = true;
            std::cout << "Server API Manager initialized successfully\n";
            return true;
        }
        catch (...)
        {
            std::cout << "Exception during Server API initialization\n";
            return false;
        }
    }

    void ServerAPIManager::Shutdown()
    {
        if (!m_Initialized)
            return;

        // Clean up player controllers
        for (auto& pair : m_PlayerControllers)
        {
            delete pair.second;
        }
        m_PlayerControllers.clear();
        m_PlayerEntities.clear();

        // Clean up controllers
        if (m_NetworkController)
        {
            m_NetworkController->Shutdown();
            delete m_NetworkController;
            m_NetworkController = nullptr;
        }

        if (m_AnimController)
        {
            delete m_AnimController;
            m_AnimController = nullptr;
        }

        if (m_OgreController)
        {
            m_OgreController->Shutdown();
            delete m_OgreController;
            m_OgreController = nullptr;
        }

        m_Initialized = false;
    }

    void ServerAPIManager::SetPlayerController(const std::string& playerId, PlayerController* controller)
    {
        m_PlayerControllers[playerId] = controller;
    }

    PlayerController* ServerAPIManager::GetPlayerController(const std::string& playerId)
    {
        auto it = m_PlayerControllers.find(playerId);
        if (it != m_PlayerControllers.end())
            return it->second;
        return nullptr;
    }

    void ServerAPIManager::Update(float deltaTime)
    {
        if (!m_Initialized)
            return;

        if (m_OgreController)
            m_OgreController->Update(deltaTime);

        if (m_AnimController)
            m_AnimController->Update(deltaTime);

        if (m_NetworkController)
            m_NetworkController->Update(deltaTime);

        for (auto& pair : m_PlayerControllers)
        {
            if (pair.second)
                pair.second->Update(deltaTime);
        }
    }

    // ===== Exported API Functions =====

    KENSHAI_API bool ServerAPI_Initialize()
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        return ServerAPIManager::GetInstance().Initialize();
    }

    KENSHAI_API void ServerAPI_Shutdown()
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().Shutdown();
    }

    KENSHAI_API bool ServerAPI_IsInitialized()
    {
        return ServerAPIManager::GetInstance().GetOgreController() != nullptr;
    }

    KENSHAI_API bool ServerAPI_SpawnPlayer(const char* playerId, const char* characterName,
                                          float x, float y, float z, int factionID)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        auto* ogreController = manager.GetOgreController();
        auto* animController = manager.GetAnimationController();

        if (!ogreController || !animController)
            return false;

        try
        {
            // Create entity
            std::string entityName = std::string("player_") + playerId;
            OgreEntity* entity = ogreController->CreateEntity(entityName.c_str(), "character.mesh");

            if (!entity)
                return false;

            // Create scene node
            Vector3 position(x, y, z);
            OgreSceneNode* node = ogreController->CreateSceneNode(entityName.c_str(), position);

            if (node)
            {
                entity->sceneNode = node;
                node->attachedObject = entity;
            }

            // Create player controller
            PlayerController* playerController = new PlayerController(ogreController, animController);
            OgreCamera* camera = ogreController->GetCamera("MainCamera");

            if (playerController->Initialize(entity, camera))
            {
                playerController->SetPosition(position);
                manager.SetPlayerController(playerId, playerController);

                std::cout << "Spawned player: " << playerId << " at (" << x << ", " << y << ", " << z << ")\n";

                // Trigger callback
                if (manager.m_OnPlayerJoin)
                    manager.m_OnPlayerJoin(playerId);

                return true;
            }

            delete playerController;
            return false;
        }
        catch (...)
        {
            std::cout << "Exception spawning player: " << playerId << "\n";
            return false;
        }
    }

    KENSHAI_API bool ServerAPI_DespawnPlayer(const char* playerId)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        PlayerController* controller = manager.GetPlayerController(playerId);

        if (!controller)
            return false;

        controller->Shutdown();
        delete controller;

        // Remove from map
        manager.GetPlayerController(playerId); // This will remove it

        // Trigger callback
        if (manager.m_OnPlayerLeave)
            manager.m_OnPlayerLeave(playerId);

        std::cout << "Despawned player: " << playerId << "\n";
        return true;
    }

    KENSHAI_API bool ServerAPI_SetPlayerPosition(const char* playerId, float x, float y, float z)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        PlayerController* controller = manager.GetPlayerController(playerId);

        if (!controller)
            return false;

        controller->SetPosition(Vector3(x, y, z));
        return true;
    }

    KENSHAI_API bool ServerAPI_GetPlayerPosition(const char* playerId, float* x, float* y, float* z)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        PlayerController* controller = manager.GetPlayerController(playerId);

        if (!controller || !x || !y || !z)
            return false;

        Vector3 pos = controller->GetPosition();
        *x = pos.x;
        *y = pos.y;
        *z = pos.z;
        return true;
    }

    KENSHAI_API bool ServerAPI_PlayAnimation(const char* playerId, const char* animName, bool loop)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        auto* animController = manager.GetAnimationController();
        PlayerController* playerController = manager.GetPlayerController(playerId);

        if (!animController || !playerController)
            return false;

        std::string entityName = std::string("player_") + playerId;
        auto* ogreController = manager.GetOgreController();
        OgreEntity* entity = ogreController ? ogreController->GetEntity(entityName.c_str()) : nullptr;

        if (entity)
        {
            animController->PlayAnimation(entity, animName, loop);
            return true;
        }

        return false;
    }

    KENSHAI_API bool ServerAPI_BlendToAnimation(const char* playerId, const char* animName, float blendTime)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        auto* animController = manager.GetAnimationController();

        if (!animController)
            return false;

        std::string entityName = std::string("player_") + playerId;
        auto* ogreController = manager.GetOgreController();
        OgreEntity* entity = ogreController ? ogreController->GetEntity(entityName.c_str()) : nullptr;

        if (entity)
        {
            animController->BlendToAnimation(entity, animName, blendTime);
            return true;
        }

        return false;
    }

    KENSHAI_API bool ServerAPI_SetCombatMode(const char* playerId, bool inCombat)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        auto& manager = ServerAPIManager::GetInstance();
        PlayerController* controller = manager.GetPlayerController(playerId);

        if (!controller)
            return false;

        controller->SetCombatMode(inCombat);
        return true;
    }

    KENSHAI_API bool ServerAPI_BroadcastMessage(const char* message)
    {
        std::lock_guard<std::mutex> lock(g_APILock);

        if (!message)
            return false;

        std::cout << "[BROADCAST] " << message << "\n";
        return true;
    }

    KENSHAI_API void ServerAPI_SetOnPlayerJoinCallback(OnPlayerJoinCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().m_OnPlayerJoin = callback;
    }

    KENSHAI_API void ServerAPI_SetOnPlayerLeaveCallback(OnPlayerLeaveCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().m_OnPlayerLeave = callback;
    }

    KENSHAI_API void ServerAPI_SetOnPlayerActionCallback(OnPlayerActionCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().m_OnPlayerAction = callback;
    }

    KENSHAI_API void ServerAPI_SetOnCombatEventCallback(OnCombatEventCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().m_OnCombatEvent = callback;
    }

    KENSHAI_API void ServerAPI_SetOnChatMessageCallback(OnChatMessageCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().m_OnChatMessage = callback;
    }

    KENSHAI_API void ServerAPI_Update(float deltaTime)
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        ServerAPIManager::GetInstance().Update(deltaTime);
    }

    KENSHAI_API int ServerAPI_GetConnectedPlayerCount()
    {
        std::lock_guard<std::mutex> lock(g_APILock);
        auto& manager = ServerAPIManager::GetInstance();
        auto* networkController = manager.GetNetworkController();

        if (networkController)
        {
            return (int)networkController->GetAllNetworkEntities().size();
        }

        return 0;
    }

    KENSHAI_API float ServerAPI_GetServerTime()
    {
        return (float)GetTickCount() / 1000.0f;
    }

    // Stub implementations for remaining functions
    KENSHAI_API bool ServerAPI_StopAnimation(const char* playerId, const char* animName) { return false; }
    KENSHAI_API bool ServerAPI_SetAnimationSpeed(const char* playerId, const char* animName, float speed) { return false; }
    KENSHAI_API bool ServerAPI_SetCharacterState(const char* playerId, int stateType) { return false; }
    KENSHAI_API int ServerAPI_GetCharacterState(const char* playerId) { return 0; }
    KENSHAI_API bool ServerAPI_IsInCombat(const char* playerId) { return false; }
    KENSHAI_API bool ServerAPI_SetPlayerHealth(const char* playerId, float health) { return false; }
    KENSHAI_API float ServerAPI_GetPlayerHealth(const char* playerId) { return 100.0f; }
    KENSHAI_API bool ServerAPI_SetPlayerFaction(const char* playerId, int factionID) { return false; }
    KENSHAI_API int ServerAPI_GetPlayerFaction(const char* playerId) { return 0; }
    KENSHAI_API bool ServerAPI_SetFactionRelation(int factionID1, int factionID2, int relationValue) { return false; }
    KENSHAI_API int ServerAPI_GetFactionRelation(int factionID1, int factionID2) { return 0; }
    KENSHAI_API int ServerAPI_GetFactionCount() { return 0; }
    KENSHAI_API bool ServerAPI_GetFactionInfo(int index, int* factionID, char* nameBuffer, int bufferSize) { return false; }
    KENSHAI_API bool ServerAPI_CreateSquad(const char* squadName, const char* leaderId) { return false; }
    KENSHAI_API bool ServerAPI_DisbandSquad(int squadID) { return false; }
    KENSHAI_API bool ServerAPI_AddToSquad(int squadID, const char* playerId) { return false; }
    KENSHAI_API bool ServerAPI_RemoveFromSquad(int squadID, const char* playerId) { return false; }
    KENSHAI_API bool ServerAPI_IssueSquadCommand(int squadID, int commandType, float x, float y, float z) { return false; }
    KENSHAI_API int ServerAPI_GetSquadCount() { return 0; }
    KENSHAI_API bool ServerAPI_PlaceBuilding(int buildingType, float x, float y, float z, float rotX, float rotY, float rotZ, int ownerFactionID) { return false; }
    KENSHAI_API bool ServerAPI_RemoveBuilding(int buildingID) { return false; }
    KENSHAI_API bool ServerAPI_SetBuildingHealth(int buildingID, float health) { return false; }
    KENSHAI_API float ServerAPI_GetBuildingHealth(int buildingID) { return 0.0f; }
    KENSHAI_API int ServerAPI_GetBuildingCount() { return 0; }
    KENSHAI_API bool ServerAPI_GetBuildingInfo(int index, int* buildingID, int* type, float* x, float* y, float* z) { return false; }
    KENSHAI_API int ServerAPI_GetPlayersInRadius(float x, float y, float z, float radius, char** playerIds, int maxPlayers) { return 0; }
    KENSHAI_API bool ServerAPI_RaycastWorld(float originX, float originY, float originZ, float dirX, float dirY, float dirZ, float maxDistance, float* hitX, float* hitY, float* hitZ) { return false; }
    KENSHAI_API bool ServerAPI_SetCameraMode(const char* playerId, int mode) { return false; }
    KENSHAI_API bool ServerAPI_SetCameraPosition(const char* playerId, float x, float y, float z) { return false; }
    KENSHAI_API bool ServerAPI_SetCameraTarget(const char* playerId, float x, float y, float z) { return false; }
    KENSHAI_API bool ServerAPI_SendMessageToPlayer(const char* playerId, const char* message) { return false; }
    KENSHAI_API bool ServerAPI_GetConnectedPlayerIds(char** playerIds, int maxPlayers) { return false; }
    KENSHAI_API int ServerAPI_GetFrameRate() { return 60; }
    KENSHAI_API int ServerAPI_GetTotalEntities() { return 0; }
}

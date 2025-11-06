#pragma once

#include "OgreController.h"
#include "AnimationController.h"
#include "PlayerController.h"
#include "NetworkController.h"
#include <Windows.h>
#include <string>

namespace KenshiOnline
{
    // Server API - Embedded API for C# middleware to call into the mod
    // These functions are exported as DLL exports for the C# server to invoke

    #define KENSHAI_API extern "C" __declspec(dllexport)

    // Initialization
    KENSHAI_API bool ServerAPI_Initialize();
    KENSHAI_API void ServerAPI_Shutdown();
    KENSHAI_API bool ServerAPI_IsInitialized();

    // Player management
    KENSHAI_API bool ServerAPI_SpawnPlayer(const char* playerId, const char* characterName,
                                          float x, float y, float z, int factionID);
    KENSHAI_API bool ServerAPI_DespawnPlayer(const char* playerId);
    KENSHAI_API bool ServerAPI_SetPlayerPosition(const char* playerId, float x, float y, float z);
    KENSHAI_API bool ServerAPI_GetPlayerPosition(const char* playerId, float* x, float* y, float* z);
    KENSHAI_API bool ServerAPI_SetPlayerHealth(const char* playerId, float health);
    KENSHAI_API float ServerAPI_GetPlayerHealth(const char* playerId);

    // Animation control
    KENSHAI_API bool ServerAPI_PlayAnimation(const char* playerId, const char* animName, bool loop);
    KENSHAI_API bool ServerAPI_StopAnimation(const char* playerId, const char* animName);
    KENSHAI_API bool ServerAPI_BlendToAnimation(const char* playerId, const char* animName, float blendTime);
    KENSHAI_API bool ServerAPI_SetAnimationSpeed(const char* playerId, const char* animName, float speed);

    // Character state
    KENSHAI_API bool ServerAPI_SetCharacterState(const char* playerId, int stateType);
    KENSHAI_API int ServerAPI_GetCharacterState(const char* playerId);
    KENSHAI_API bool ServerAPI_SetCombatMode(const char* playerId, bool inCombat);
    KENSHAI_API bool ServerAPI_IsInCombat(const char* playerId);

    // Faction management
    KENSHAI_API bool ServerAPI_SetPlayerFaction(const char* playerId, int factionID);
    KENSHAI_API int ServerAPI_GetPlayerFaction(const char* playerId);
    KENSHAI_API bool ServerAPI_SetFactionRelation(int factionID1, int factionID2, int relationValue);
    KENSHAI_API int ServerAPI_GetFactionRelation(int factionID1, int factionID2);
    KENSHAI_API int ServerAPI_GetFactionCount();
    KENSHAI_API bool ServerAPI_GetFactionInfo(int index, int* factionID, char* nameBuffer, int bufferSize);

    // Squad management
    KENSHAI_API bool ServerAPI_CreateSquad(const char* squadName, const char* leaderId);
    KENSHAI_API bool ServerAPI_DisbandSquad(int squadID);
    KENSHAI_API bool ServerAPI_AddToSquad(int squadID, const char* playerId);
    KENSHAI_API bool ServerAPI_RemoveFromSquad(int squadID, const char* playerId);
    KENSHAI_API bool ServerAPI_IssueSquadCommand(int squadID, int commandType, float x, float y, float z);
    KENSHAI_API int ServerAPI_GetSquadCount();

    // Building management
    KENSHAI_API bool ServerAPI_PlaceBuilding(int buildingType, float x, float y, float z,
                                             float rotX, float rotY, float rotZ, int ownerFactionID);
    KENSHAI_API bool ServerAPI_RemoveBuilding(int buildingID);
    KENSHAI_API bool ServerAPI_SetBuildingHealth(int buildingID, float health);
    KENSHAI_API float ServerAPI_GetBuildingHealth(int buildingID);
    KENSHAI_API int ServerAPI_GetBuildingCount();
    KENSHAI_API bool ServerAPI_GetBuildingInfo(int index, int* buildingID, int* type,
                                               float* x, float* y, float* z);

    // World queries
    KENSHAI_API int ServerAPI_GetPlayersInRadius(float x, float y, float z, float radius,
                                                 char** playerIds, int maxPlayers);
    KENSHAI_API bool ServerAPI_RaycastWorld(float originX, float originY, float originZ,
                                           float dirX, float dirY, float dirZ, float maxDistance,
                                           float* hitX, float* hitY, float* hitZ);

    // Camera control
    KENSHAI_API bool ServerAPI_SetCameraMode(const char* playerId, int mode);
    KENSHAI_API bool ServerAPI_SetCameraPosition(const char* playerId, float x, float y, float z);
    KENSHAI_API bool ServerAPI_SetCameraTarget(const char* playerId, float x, float y, float z);

    // Network
    KENSHAI_API bool ServerAPI_BroadcastMessage(const char* message);
    KENSHAI_API bool ServerAPI_SendMessageToPlayer(const char* playerId, const char* message);
    KENSHAI_API int ServerAPI_GetConnectedPlayerCount();
    KENSHAI_API bool ServerAPI_GetConnectedPlayerIds(char** playerIds, int maxPlayers);

    // Statistics
    KENSHAI_API int ServerAPI_GetFrameRate();
    KENSHAI_API float ServerAPI_GetServerTime();
    KENSHAI_API int ServerAPI_GetTotalEntities();

    // Callbacks (set by C# server)
    typedef void (*OnPlayerJoinCallback)(const char* playerId);
    typedef void (*OnPlayerLeaveCallback)(const char* playerId);
    typedef void (*OnPlayerActionCallback)(const char* playerId, int actionType, const char* data);
    typedef void (*OnCombatEventCallback)(const char* attacker, const char* target, int damageType, float damage);
    typedef void (*OnChatMessageCallback)(const char* playerId, const char* message);

    KENSHAI_API void ServerAPI_SetOnPlayerJoinCallback(OnPlayerJoinCallback callback);
    KENSHAI_API void ServerAPI_SetOnPlayerLeaveCallback(OnPlayerLeaveCallback callback);
    KENSHAI_API void ServerAPI_SetOnPlayerActionCallback(OnPlayerActionCallback callback);
    KENSHAI_API void ServerAPI_SetOnCombatEventCallback(OnCombatEventCallback callback);
    KENSHAI_API void ServerAPI_SetOnChatMessageCallback(OnChatMessageCallback callback);

    // Update (called by C# server each frame)
    KENSHAI_API void ServerAPI_Update(float deltaTime);

    // Internal controller manager (not exported)
    class ServerAPIManager
    {
    public:
        static ServerAPIManager& GetInstance();

        bool Initialize();
        void Shutdown();

        OgreController* GetOgreController() { return m_OgreController; }
        AnimationController* GetAnimationController() { return m_AnimController; }
        NetworkController* GetNetworkController() { return m_NetworkController; }

        void SetPlayerController(const std::string& playerId, PlayerController* controller);
        PlayerController* GetPlayerController(const std::string& playerId);

        void Update(float deltaTime);

        // Callbacks
        OnPlayerJoinCallback m_OnPlayerJoin;
        OnPlayerLeaveCallback m_OnPlayerLeave;
        OnPlayerActionCallback m_OnPlayerAction;
        OnCombatEventCallback m_OnCombatEvent;
        OnChatMessageCallback m_OnChatMessage;

    private:
        ServerAPIManager();
        ~ServerAPIManager();

        OgreController* m_OgreController;
        AnimationController* m_AnimController;
        NetworkController* m_NetworkController;

        std::unordered_map<std::string, PlayerController*> m_PlayerControllers;
        std::unordered_map<std::string, OgreEntity*> m_PlayerEntities;

        bool m_Initialized;
    };
}

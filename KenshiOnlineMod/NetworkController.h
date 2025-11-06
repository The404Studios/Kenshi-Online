#pragma once

#include "OgreController.h"
#include "AnimationController.h"
#include "PlayerController.h"
#include <winsock2.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace KenshiOnline
{
    // Network entity - represents a remote player
    struct NetworkEntity
    {
        std::string playerId;
        OgreEntity* entity;
        OgreSceneNode* sceneNode;

        Vector3 position;
        Vector3 targetPosition;
        Quaternion rotation;
        Vector3 velocity;

        std::string currentAnimation;
        bool inCombat;
        bool isBlocking;
        float health;
        int factionID;

        // Interpolation
        float interpolationTime;
        Vector3 lastPosition;

        NetworkEntity()
            : entity(nullptr)
            , sceneNode(nullptr)
            , position(0, 0, 0)
            , targetPosition(0, 0, 0)
            , rotation(1, 0, 0, 0)
            , velocity(0, 0, 0)
            , inCombat(false)
            , isBlocking(false)
            , health(100.0f)
            , factionID(0)
            , interpolationTime(0)
            , lastPosition(0, 0, 0)
        {}
    };

    // Network message types
    enum class NetworkMessageType
    {
        PlayerJoin,
        PlayerLeave,
        PlayerUpdate,
        PlayerAction,
        AnimationUpdate,
        CombatAction,
        FactionUpdate,
        SquadCommand,
        BuildingUpdate,
        ChatMessage,
        Ping,
        Pong
    };

    // Network controller - handles multiplayer synchronization
    class NetworkController
    {
    public:
        NetworkController(OgreController* ogreController, AnimationController* animController);
        ~NetworkController();

        // Initialization
        bool Initialize(SOCKET socket, const std::string& localPlayerId);
        void Shutdown();

        // Connection management
        bool IsConnected() const { return m_Socket != INVALID_SOCKET; }
        void Disconnect();

        // Player management
        void RegisterLocalPlayer(PlayerController* playerController);
        NetworkEntity* GetNetworkEntity(const std::string& playerId);
        std::vector<NetworkEntity*> GetAllNetworkEntities();

        // Sending
        void SendPlayerUpdate(const PlayerController::PlayerSyncData& data);
        void SendAnimationUpdate(const std::string& animName, float time);
        void SendCombatAction(int actionType, const std::string& targetId);
        void SendChatMessage(const std::string& message);
        void SendFactionUpdate(int factionID, int relationFactionID, int relationValue);
        void SendSquadCommand(int squadID, int commandType, Vector3 position);
        void SendBuildingUpdate(int buildingID, float health);

        // Receiving
        void ProcessIncomingMessages();

        // Interpolation settings
        void SetInterpolationTime(float time) { m_InterpolationTime = time; }
        float GetInterpolationTime() const { return m_InterpolationTime; }

        // Statistics
        int GetPing() const { return m_Ping; }
        int GetPacketsSent() const { return m_PacketsSent; }
        int GetPacketsReceived() const { return m_PacketsReceived; }

        // Update
        void Update(float deltaTime);

    private:
        OgreController* m_OgreController;
        AnimationController* m_AnimController;
        PlayerController* m_LocalPlayerController;

        SOCKET m_Socket;
        std::string m_LocalPlayerId;

        // Network entities (remote players)
        std::unordered_map<std::string, NetworkEntity> m_NetworkEntities;
        std::mutex m_EntitiesMutex;

        // Interpolation
        float m_InterpolationTime;

        // Statistics
        int m_Ping;
        int m_PacketsSent;
        int m_PacketsReceived;
        DWORD m_LastPingTime;

        // Message handling
        void HandlePlayerJoin(const char* data);
        void HandlePlayerLeave(const char* data);
        void HandlePlayerUpdate(const char* data);
        void HandlePlayerAction(const char* data);
        void HandleAnimationUpdate(const char* data);
        void HandleCombatAction(const char* data);
        void HandleFactionUpdate(const char* data);
        void HandleSquadCommand(const char* data);
        void HandleBuildingUpdate(const char* data);
        void HandleChatMessage(const char* data);
        void HandlePong(const char* data);

        // Helper functions
        void InterpolateNetworkEntities(float deltaTime);
        void UpdateNetworkEntityAnimations();
        void CreateNetworkEntity(const std::string& playerId, Vector3 position);
        void RemoveNetworkEntity(const std::string& playerId);
        bool SendMessage(NetworkMessageType type, const char* data, int length);
        void SendPing();
    };
}

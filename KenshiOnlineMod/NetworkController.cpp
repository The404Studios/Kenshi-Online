#define _CRT_SECURE_NO_WARNINGS

#include "NetworkController.h"
#include <iostream>
#include <sstream>
#include <cstring>

namespace KenshiOnline
{
    NetworkController::NetworkController(OgreController* ogreController, AnimationController* animController)
        : m_OgreController(ogreController)
        , m_AnimController(animController)
        , m_LocalPlayerController(nullptr)
        , m_Socket(INVALID_SOCKET)
        , m_InterpolationTime(0.1f) // 100ms interpolation
        , m_Ping(0)
        , m_PacketsSent(0)
        , m_PacketsReceived(0)
        , m_LastPingTime(0)
    {
    }

    NetworkController::~NetworkController()
    {
        Shutdown();
    }

    bool NetworkController::Initialize(SOCKET socket, const std::string& localPlayerId)
    {
        m_Socket = socket;
        m_LocalPlayerId = localPlayerId;
        m_LastPingTime = GetTickCount();

        std::cout << "Network controller initialized for player: " << localPlayerId << "\n";
        return true;
    }

    void NetworkController::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        // Clean up network entities
        for (auto& pair : m_NetworkEntities)
        {
            if (pair.second.entity && m_OgreController)
            {
                m_OgreController->DestroyEntity(pair.second.entity->name);
            }
            if (pair.second.sceneNode && m_OgreController)
            {
                m_OgreController->DestroySceneNode(pair.second.sceneNode->name);
            }
        }

        m_NetworkEntities.clear();
        m_Socket = INVALID_SOCKET;
    }

    void NetworkController::Disconnect()
    {
        if (m_Socket != INVALID_SOCKET)
        {
            closesocket(m_Socket);
            m_Socket = INVALID_SOCKET;
        }
    }

    void NetworkController::RegisterLocalPlayer(PlayerController* playerController)
    {
        m_LocalPlayerController = playerController;
        std::cout << "Registered local player controller\n";
    }

    NetworkEntity* NetworkController::GetNetworkEntity(const std::string& playerId)
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        auto it = m_NetworkEntities.find(playerId);
        if (it != m_NetworkEntities.end())
            return &it->second;

        return nullptr;
    }

    std::vector<NetworkEntity*> NetworkController::GetAllNetworkEntities()
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        std::vector<NetworkEntity*> entities;
        for (auto& pair : m_NetworkEntities)
        {
            entities.push_back(&pair.second);
        }

        return entities;
    }

    void NetworkController::SendPlayerUpdate(const PlayerController::PlayerSyncData& data)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "PLAYER_UPDATE|%s|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%d|%d|%s\n",
            m_LocalPlayerId.c_str(),
            data.position.x, data.position.y, data.position.z,
            data.velocity.x, data.velocity.y, data.velocity.z,
            data.rotation.w,
            static_cast<int>(data.movementState),
            data.inCombat ? 1 : 0,
            data.isBlocking ? 1 : 0,
            data.currentAnimation.c_str()
        );

        SendMessage(NetworkMessageType::PlayerUpdate, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendAnimationUpdate(const std::string& animName, float time)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "ANIM_UPDATE|%s|%s|%.3f\n",
            m_LocalPlayerId.c_str(), animName.c_str(), time);

        SendMessage(NetworkMessageType::AnimationUpdate, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendCombatAction(int actionType, const std::string& targetId)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "COMBAT_ACTION|%s|%d|%s\n",
            m_LocalPlayerId.c_str(), actionType, targetId.c_str());

        SendMessage(NetworkMessageType::CombatAction, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendChatMessage(const std::string& message)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "CHAT|%s|%s\n",
            m_LocalPlayerId.c_str(), message.c_str());

        SendMessage(NetworkMessageType::ChatMessage, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendFactionUpdate(int factionID, int relationFactionID, int relationValue)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "FACTION|%d|%d|%d\n",
            factionID, relationFactionID, relationValue);

        SendMessage(NetworkMessageType::FactionUpdate, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendSquadCommand(int squadID, int commandType, Vector3 position)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "SQUAD_COMMAND|%d|%d|%.2f|%.2f|%.2f\n",
            squadID, commandType, position.x, position.y, position.z);

        SendMessage(NetworkMessageType::SquadCommand, buffer, (int)strlen(buffer));
    }

    void NetworkController::SendBuildingUpdate(int buildingID, float health)
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "BUILDING_UPDATE|%d|%.2f\n",
            buildingID, health);

        SendMessage(NetworkMessageType::BuildingUpdate, buffer, (int)strlen(buffer));
    }

    void NetworkController::ProcessIncomingMessages()
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        char buffer[4096];
        int bytesReceived = recv(m_Socket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';
            m_PacketsReceived++;

            // Parse messages (newline delimited)
            char* context = nullptr;
            char* token = strtok(buffer, "\n");

            while (token != nullptr)
            {
                // Parse message type
                char msgType[64];
                sscanf(token, "%63[^|]", msgType);

                if (strcmp(msgType, "PLAYER_JOIN") == 0)
                    HandlePlayerJoin(token);
                else if (strcmp(msgType, "PLAYER_LEAVE") == 0)
                    HandlePlayerLeave(token);
                else if (strcmp(msgType, "PLAYER_UPDATE") == 0)
                    HandlePlayerUpdate(token);
                else if (strcmp(msgType, "PLAYER_ACTION") == 0)
                    HandlePlayerAction(token);
                else if (strcmp(msgType, "ANIM_UPDATE") == 0)
                    HandleAnimationUpdate(token);
                else if (strcmp(msgType, "COMBAT_ACTION") == 0)
                    HandleCombatAction(token);
                else if (strcmp(msgType, "FACTION") == 0)
                    HandleFactionUpdate(token);
                else if (strcmp(msgType, "SQUAD_COMMAND") == 0)
                    HandleSquadCommand(token);
                else if (strcmp(msgType, "BUILDING_UPDATE") == 0)
                    HandleBuildingUpdate(token);
                else if (strcmp(msgType, "CHAT") == 0)
                    HandleChatMessage(token);
                else if (strcmp(msgType, "PONG") == 0)
                    HandlePong(token);

                token = strtok(nullptr, "\n");
            }
        }
    }

    void NetworkController::Update(float deltaTime)
    {
        // Process incoming messages
        ProcessIncomingMessages();

        // Interpolate network entities
        InterpolateNetworkEntities(deltaTime);

        // Update network entity animations
        UpdateNetworkEntityAnimations();

        // Send ping every 5 seconds
        DWORD currentTime = GetTickCount();
        if (currentTime - m_LastPingTime > 5000)
        {
            SendPing();
            m_LastPingTime = currentTime;
        }

        // Send local player update (if we have a local player)
        if (m_LocalPlayerController)
        {
            auto syncData = m_LocalPlayerController->GetSyncData();
            SendPlayerUpdate(syncData);
        }
    }

    // Message handlers

    void NetworkController::HandlePlayerJoin(const char* data)
    {
        char playerId[128];
        float x, y, z;

        if (sscanf(data, "PLAYER_JOIN|%127[^|]|%f|%f|%f", playerId, &x, &y, &z) == 4)
        {
            // Don't create entity for ourselves
            if (playerId == m_LocalPlayerId)
                return;

            std::cout << "Player joined: " << playerId << " at (" << x << ", " << y << ", " << z << ")\n";
            CreateNetworkEntity(playerId, Vector3(x, y, z));
        }
    }

    void NetworkController::HandlePlayerLeave(const char* data)
    {
        char playerId[128];

        if (sscanf(data, "PLAYER_LEAVE|%127s", playerId) == 1)
        {
            std::cout << "Player left: " << playerId << "\n";
            RemoveNetworkEntity(playerId);
        }
    }

    void NetworkController::HandlePlayerUpdate(const char* data)
    {
        char playerId[128];
        float px, py, pz, vx, vy, vz, rotW;
        int movementState, inCombat, isBlocking;
        char animName[64];

        if (sscanf(data, "PLAYER_UPDATE|%127[^|]|%f|%f|%f|%f|%f|%f|%f|%d|%d|%d|%63s",
            playerId, &px, &py, &pz, &vx, &vy, &vz, &rotW,
            &movementState, &inCombat, &isBlocking, animName) == 12)
        {
            // Don't update ourselves
            if (playerId == m_LocalPlayerId)
                return;

            std::lock_guard<std::mutex> lock(m_EntitiesMutex);
            auto it = m_NetworkEntities.find(playerId);

            if (it != m_NetworkEntities.end())
            {
                NetworkEntity& entity = it->second;

                // Store last position for interpolation
                entity.lastPosition = entity.position;

                // Set target position
                entity.targetPosition = Vector3(px, py, pz);
                entity.velocity = Vector3(vx, vy, vz);
                entity.inCombat = (inCombat != 0);
                entity.isBlocking = (isBlocking != 0);
                entity.currentAnimation = animName;

                // Reset interpolation time
                entity.interpolationTime = 0.0f;
            }
        }
    }

    void NetworkController::HandlePlayerAction(const char* data)
    {
        std::cout << "Player action received: " << data << "\n";
    }

    void NetworkController::HandleAnimationUpdate(const char* data)
    {
        char playerId[128];
        char animName[64];
        float time;

        if (sscanf(data, "ANIM_UPDATE|%127[^|]|%63[^|]|%f", playerId, animName, &time) == 3)
        {
            std::lock_guard<std::mutex> lock(m_EntitiesMutex);
            auto it = m_NetworkEntities.find(playerId);

            if (it != m_NetworkEntities.end() && it->second.entity && m_AnimController)
            {
                m_AnimController->PlayAnimation(it->second.entity, animName, true);
            }
        }
    }

    void NetworkController::HandleCombatAction(const char* data)
    {
        std::cout << "Combat action received: " << data << "\n";
    }

    void NetworkController::HandleFactionUpdate(const char* data)
    {
        int faction1, faction2, relation;
        if (sscanf(data, "FACTION|%d|%d|%d", &faction1, &faction2, &relation) == 3)
        {
            std::cout << "Faction relation update: " << faction1 << " <-> " << faction2 << " = " << relation << "\n";
        }
    }

    void NetworkController::HandleSquadCommand(const char* data)
    {
        std::cout << "Squad command received: " << data << "\n";
    }

    void NetworkController::HandleBuildingUpdate(const char* data)
    {
        std::cout << "Building update received: " << data << "\n";
    }

    void NetworkController::HandleChatMessage(const char* data)
    {
        char playerId[128];
        char message[512];

        if (sscanf(data, "CHAT|%127[^|]|%511[^\n]", playerId, message) == 2)
        {
            std::cout << "[CHAT] " << playerId << ": " << message << "\n";
        }
    }

    void NetworkController::HandlePong(const char* data)
    {
        DWORD serverTime;
        if (sscanf(data, "PONG|%lu", &serverTime) == 1)
        {
            DWORD currentTime = GetTickCount();
            m_Ping = (int)(currentTime - serverTime);
        }
    }

    // Helper functions

    void NetworkController::InterpolateNetworkEntities(float deltaTime)
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        for (auto& pair : m_NetworkEntities)
        {
            NetworkEntity& entity = pair.second;

            entity.interpolationTime += deltaTime;
            float t = entity.interpolationTime / m_InterpolationTime;

            if (t > 1.0f)
                t = 1.0f;

            // Linear interpolation
            entity.position.x = entity.lastPosition.x + (entity.targetPosition.x - entity.lastPosition.x) * t;
            entity.position.y = entity.lastPosition.y + (entity.targetPosition.y - entity.lastPosition.y) * t;
            entity.position.z = entity.lastPosition.z + (entity.targetPosition.z - entity.lastPosition.z) * t;

            // Apply to scene node
            if (entity.sceneNode && m_OgreController)
            {
                m_OgreController->Write<Vector3>((uintptr_t)&entity.sceneNode->position, entity.position);
                entity.sceneNode->needsUpdate = 1;
            }
        }
    }

    void NetworkController::UpdateNetworkEntityAnimations()
    {
        if (!m_AnimController)
            return;

        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        for (auto& pair : m_NetworkEntities)
        {
            NetworkEntity& entity = pair.second;

            if (entity.entity && !entity.currentAnimation.empty())
            {
                if (!m_AnimController->IsAnimationPlaying(entity.entity, entity.currentAnimation))
                {
                    m_AnimController->PlayAnimation(entity.entity, entity.currentAnimation, true);
                }
            }
        }
    }

    void NetworkController::CreateNetworkEntity(const std::string& playerId, Vector3 position)
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        NetworkEntity entity;
        entity.playerId = playerId;
        entity.position = position;
        entity.targetPosition = position;
        entity.lastPosition = position;

        // Create OGRE entity and scene node
        if (m_OgreController)
        {
            std::string entityName = "network_player_" + playerId;

            entity.entity = m_OgreController->CreateEntity(entityName.c_str(), "character.mesh");
            entity.sceneNode = m_OgreController->CreateSceneNode(entityName.c_str(), position);

            if (entity.entity && entity.sceneNode)
            {
                entity.entity->sceneNode = entity.sceneNode;
                entity.sceneNode->attachedObject = entity.entity;
            }
        }

        m_NetworkEntities[playerId] = entity;

        std::cout << "Created network entity for: " << playerId << "\n";
    }

    void NetworkController::RemoveNetworkEntity(const std::string& playerId)
    {
        std::lock_guard<std::mutex> lock(m_EntitiesMutex);

        auto it = m_NetworkEntities.find(playerId);
        if (it != m_NetworkEntities.end())
        {
            if (it->second.entity && m_OgreController)
            {
                m_OgreController->DestroyEntity(it->second.entity->name);
            }
            if (it->second.sceneNode && m_OgreController)
            {
                m_OgreController->DestroySceneNode(it->second.sceneNode->name);
            }

            m_NetworkEntities.erase(it);
        }
    }

    bool NetworkController::SendMessage(NetworkMessageType type, const char* data, int length)
    {
        if (m_Socket == INVALID_SOCKET || !data)
            return false;

        int sent = send(m_Socket, data, length, 0);

        if (sent > 0)
        {
            m_PacketsSent++;
            return true;
        }

        return false;
    }

    void NetworkController::SendPing()
    {
        if (m_Socket == INVALID_SOCKET)
            return;

        DWORD currentTime = GetTickCount();
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "PING|%lu\n", currentTime);

        SendMessage(NetworkMessageType::Ping, buffer, (int)strlen(buffer));
    }
}

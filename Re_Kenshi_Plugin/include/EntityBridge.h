#pragma once

#include "NetworkProtocol.h"
#include "PatternCoordinator.h"
#include "Logger.h"
#include <memory>
#include <unordered_map>
#include <mutex>

namespace KenshiOnline {
namespace Bridge {

using namespace Network;
using namespace ReKenshi::Patterns;
using namespace ReKenshi::Logging;

//=============================================================================
// Entity Bridge - Converts game data to network entities
//=============================================================================

class EntityBridge {
public:
    static EntityBridge& GetInstance() {
        static EntityBridge instance;
        return instance;
    }

    // Initialize bridge
    bool Initialize() {
        LOG_INFO("Initializing Entity Bridge...");
        m_initialized = true;
        return true;
    }

    // Read local player data from game memory and convert to PlayerEntity
    bool ReadLocalPlayerEntity(PlayerEntity& outPlayer) {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto& coordinator = PatternCoordinator::GetInstance();

        // Read character data using pattern coordinator
        Kenshi::CharacterData charData;
        if (!coordinator.GetCharacterData("Player", charData)) {
            return false;
        }

        // Convert to PlayerEntity
        outPlayer.playerId = m_localPlayerId;
        outPlayer.playerName = m_localPlayerName;

        // Position
        outPlayer.position.x = charData.position[0];
        outPlayer.position.y = charData.position[1];
        outPlayer.position.z = charData.position[2];

        // Rotation (simplified - would need proper rotation reading)
        outPlayer.rotation.x = 0.0f;
        outPlayer.rotation.y = 0.0f;
        outPlayer.rotation.z = 0.0f;
        outPlayer.rotation.w = 1.0f;

        // Velocity (calculate from position delta)
        if (m_lastPosition.x != 0 || m_lastPosition.y != 0 || m_lastPosition.z != 0) {
            outPlayer.velocity.x = outPlayer.position.x - m_lastPosition.x;
            outPlayer.velocity.y = outPlayer.position.y - m_lastPosition.y;
            outPlayer.velocity.z = outPlayer.position.z - m_lastPosition.z;
        }
        m_lastPosition = outPlayer.position;

        // Health
        outPlayer.health = charData.health;
        outPlayer.maxHealth = charData.maxHealth;

        // Hunger/Blood (would need additional patterns)
        outPlayer.hunger = 100.0f; // TODO: Read from game
        outPlayer.blood = 100.0f;  // TODO: Read from game

        // State
        outPlayer.isAlive = charData.isAlive;
        outPlayer.isUnconscious = charData.isUnconscious;
        outPlayer.isInCombat = charData.isInCombat;

        // TODO: Read more states (sneaking, running, etc.)
        outPlayer.isSneaking = false;
        outPlayer.isRunning = false;

        return true;
    }

    // Apply remote player data to game world (for rendering other players)
    void ApplyRemotePlayerEntity(const PlayerEntity& player) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Store remote player for rendering
        m_remotePlayers[player.id] = player;

        // TODO: Implement actual spawning/updating of remote player models in game
        // This would require:
        // 1. Creating a game character entity
        // 2. Setting position/rotation
        // 3. Playing animations
        // 4. Updating health bars
    }

    // Get remote player
    bool GetRemotePlayer(const std::string& entityId, PlayerEntity& outPlayer) {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_remotePlayers.find(entityId);
        if (it != m_remotePlayers.end()) {
            outPlayer = it->second;
            return true;
        }
        return false;
    }

    // Get all remote players
    std::vector<PlayerEntity> GetAllRemotePlayers() {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<PlayerEntity> players;
        players.reserve(m_remotePlayers.size());

        for (const auto& pair : m_remotePlayers) {
            players.push_back(pair.second);
        }

        return players;
    }

    // Remove remote player
    void RemoveRemotePlayer(const std::string& entityId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_remotePlayers.erase(entityId);

        // TODO: Remove from game world
    }

    // Set local player info
    void SetLocalPlayerInfo(const std::string& playerId, const std::string& playerName) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_localPlayerId = playerId;
        m_localPlayerName = playerName;
        LOG_INFO_F("Local player set: %s (%s)", playerName.c_str(), playerId.c_str());
    }

    // Get local player ID
    std::string GetLocalPlayerId() const {
        return m_localPlayerId;
    }

    // Get local player name
    std::string GetLocalPlayerName() const {
        return m_localPlayerName;
    }

    // Clear all remote players
    void ClearRemotePlayers() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_remotePlayers.clear();
    }

private:
    EntityBridge() = default;
    ~EntityBridge() = default;
    EntityBridge(const EntityBridge&) = delete;
    EntityBridge& operator=(const EntityBridge&) = delete;

    bool m_initialized = false;
    std::mutex m_mutex;

    // Local player info
    std::string m_localPlayerId;
    std::string m_localPlayerName;
    Vector3 m_lastPosition;

    // Remote players
    std::unordered_map<std::string, PlayerEntity> m_remotePlayers;
};

} // namespace Bridge
} // namespace KenshiOnline

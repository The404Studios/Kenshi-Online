#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include "json.hpp"

using json = nlohmann::json;

namespace KenshiOnline {
namespace Network {

//=============================================================================
// Message Types (matching server protocol)
//=============================================================================

enum class MessageType {
    Connect,
    Disconnect,
    Heartbeat,
    EntityUpdate,
    EntityCreate,
    EntityDestroy,
    EntitySnapshot,
    CombatEvent,
    InventoryAction,
    WorldState,
    AdminCommand,
    Response
};

//=============================================================================
// Network Message
//=============================================================================

class NetworkMessage {
public:
    std::string type;
    std::string playerId;
    std::string sessionId;
    json data;
    int64_t timestamp;

    NetworkMessage() {
        timestamp = GetCurrentTimestamp();
    }

    NetworkMessage(const std::string& msgType) : type(msgType) {
        timestamp = GetCurrentTimestamp();
    }

    // Serialize to JSON string
    std::string Serialize() const {
        json j;
        j["Type"] = type;
        j["PlayerId"] = playerId;
        j["SessionId"] = sessionId;
        j["Data"] = data;
        j["Timestamp"] = timestamp;
        return j.dump();
    }

    // Deserialize from JSON string
    static NetworkMessage Deserialize(const std::string& jsonStr) {
        NetworkMessage msg;
        try {
            json j = json::parse(jsonStr);
            msg.type = j.value("Type", "");
            msg.playerId = j.value("PlayerId", "");
            msg.sessionId = j.value("SessionId", "");
            msg.data = j.value("Data", json::object());
            msg.timestamp = j.value("Timestamp", 0LL);
        }
        catch (...) {
            // Failed to parse
        }
        return msg;
    }

private:
    static int64_t GetCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

//=============================================================================
// Entity Data Structures
//=============================================================================

struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    json Serialize() const {
        return json{{"x", x}, {"y", y}, {"z", z}};
    }

    static Vector3 Deserialize(const json& j) {
        Vector3 v;
        v.x = j.value("x", 0.0f);
        v.y = j.value("y", 0.0f);
        v.z = j.value("z", 0.0f);
        return v;
    }
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    json Serialize() const {
        return json{{"x", x}, {"y", y}, {"z", z}, {"w", w}};
    }

    static Quaternion Deserialize(const json& j) {
        Quaternion q;
        q.x = j.value("x", 0.0f);
        q.y = j.value("y", 0.0f);
        q.z = j.value("z", 0.0f);
        q.w = j.value("w", 1.0f);
        return q;
    }
};

//=============================================================================
// Entity Base
//=============================================================================

class Entity {
public:
    std::string id;
    std::string type;
    Vector3 position;
    Quaternion rotation;
    Vector3 velocity;
    int priority = 5;
    float syncRadius = 100.0f;

    virtual json Serialize() const {
        json j;
        j["id"] = id;
        j["type"] = type;
        j["position"] = position.Serialize();
        j["rotation"] = rotation.Serialize();
        j["velocity"] = velocity.Serialize();
        j["priority"] = priority;
        j["syncRadius"] = syncRadius;
        return j;
    }

    virtual void Deserialize(const json& j) {
        id = j.value("id", "");
        type = j.value("type", "");
        if (j.contains("position")) position = Vector3::Deserialize(j["position"]);
        if (j.contains("rotation")) rotation = Quaternion::Deserialize(j["rotation"]);
        if (j.contains("velocity")) velocity = Vector3::Deserialize(j["velocity"]);
        priority = j.value("priority", 5);
        syncRadius = j.value("syncRadius", 100.0f);
    }
};

//=============================================================================
// Player Entity
//=============================================================================

class PlayerEntity : public Entity {
public:
    std::string playerId;
    std::string playerName;

    // Stats
    float health = 100.0f;
    float maxHealth = 100.0f;
    float hunger = 100.0f;
    float blood = 100.0f;

    // State
    bool isAlive = true;
    bool isUnconscious = false;
    bool isInCombat = false;
    bool isSneaking = false;
    bool isRunning = false;

    // Animation
    std::string currentAnimation;
    float animationTime = 0.0f;

    PlayerEntity() {
        type = "Player";
    }

    json Serialize() const override {
        json j = Entity::Serialize();
        j["playerId"] = playerId;
        j["playerName"] = playerName;
        j["health"] = health;
        j["maxHealth"] = maxHealth;
        j["hunger"] = hunger;
        j["blood"] = blood;
        j["isAlive"] = isAlive;
        j["isUnconscious"] = isUnconscious;
        j["isInCombat"] = isInCombat;
        j["isSneaking"] = isSneaking;
        j["isRunning"] = isRunning;
        j["currentAnimation"] = currentAnimation;
        j["animationTime"] = animationTime;
        return j;
    }

    void Deserialize(const json& j) override {
        Entity::Deserialize(j);
        playerId = j.value("playerId", "");
        playerName = j.value("playerName", "");
        health = j.value("health", 100.0f);
        maxHealth = j.value("maxHealth", 100.0f);
        hunger = j.value("hunger", 100.0f);
        blood = j.value("blood", 100.0f);
        isAlive = j.value("isAlive", true);
        isUnconscious = j.value("isUnconscious", false);
        isInCombat = j.value("isInCombat", false);
        isSneaking = j.value("isSneaking", false);
        isRunning = j.value("isRunning", false);
        currentAnimation = j.value("currentAnimation", "");
        animationTime = j.value("animationTime", 0.0f);
    }
};

//=============================================================================
// Protocol Handler
//=============================================================================

class ProtocolHandler {
public:
    using MessageCallback = std::function<void(const NetworkMessage&)>;

    ProtocolHandler() = default;

    // Register callback for message type
    void RegisterCallback(const std::string& messageType, MessageCallback callback) {
        m_callbacks[messageType] = callback;
    }

    // Process incoming message
    void ProcessMessage(const std::string& jsonStr) {
        auto msg = NetworkMessage::Deserialize(jsonStr);

        auto it = m_callbacks.find(msg.type);
        if (it != m_callbacks.end()) {
            it->second(msg);
        }
    }

    // Create connect message
    static NetworkMessage CreateConnectMessage(const std::string& playerId, const std::string& playerName) {
        NetworkMessage msg("connect");
        msg.data["playerId"] = playerId;
        msg.data["playerName"] = playerName;
        return msg;
    }

    // Create heartbeat message
    static NetworkMessage CreateHeartbeatMessage(int ping) {
        NetworkMessage msg("heartbeat");
        msg.data["ping"] = ping;
        return msg;
    }

    // Create entity update message
    static NetworkMessage CreateEntityUpdateMessage(const PlayerEntity& player) {
        NetworkMessage msg("entity_update");
        msg.data["position"] = player.position.Serialize();
        msg.data["velocity"] = player.velocity.Serialize();
        msg.data["rotation"] = player.rotation.Serialize();
        msg.data["health"] = player.health;
        msg.data["isInCombat"] = player.isInCombat;
        msg.data["isSneaking"] = player.isSneaking;
        msg.data["isRunning"] = player.isRunning;
        msg.data["currentAnimation"] = player.currentAnimation;
        return msg;
    }

    // Create combat event message
    static NetworkMessage CreateCombatEventMessage(const std::string& defenderId, float damage, const std::string& animation) {
        NetworkMessage msg("combat_event");
        msg.data["defenderId"] = defenderId;
        msg.data["damage"] = damage;
        msg.data["animation"] = animation;
        return msg;
    }

    // Create inventory action message
    static NetworkMessage CreateInventoryActionMessage(const std::string& action, const std::string& itemId, const std::string& slot = "") {
        NetworkMessage msg("inventory_action");
        msg.data["action"] = action;
        msg.data["itemId"] = itemId;
        if (!slot.empty()) {
            msg.data["slot"] = slot;
        }
        return msg;
    }

private:
    std::unordered_map<std::string, MessageCallback> m_callbacks;
};

} // namespace Network
} // namespace KenshiOnline

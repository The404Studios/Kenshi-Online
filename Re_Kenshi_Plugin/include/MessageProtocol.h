#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace ReKenshi {
namespace IPC {

/**
 * IPC Message Types - must match C# backend
 */
enum class MessageType : uint32_t {
    // Client → Server (UI → Backend)
    AUTHENTICATE_REQUEST = 1,
    SERVER_LIST_REQUEST = 2,
    CONNECT_SERVER_REQUEST = 3,
    DISCONNECT_REQUEST = 4,
    PLAYER_UPDATE = 5,
    CHAT_MESSAGE = 6,

    // Server → Client (Backend → UI)
    AUTH_RESPONSE = 100,
    SERVER_LIST_RESPONSE = 101,
    CONNECTION_STATUS = 102,
    GAME_STATE_UPDATE = 103,
    PLAYER_UPDATE_BROADCAST = 104,
    CHAT_MESSAGE_BROADCAST = 105,
    ERROR_MESSAGE = 199,
};

/**
 * Connection status codes
 */
enum class ConnectionStatus : uint32_t {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    AUTHENTICATED = 3,
    ERROR_AUTH_FAILED = 100,
    ERROR_SERVER_FULL = 101,
    ERROR_TIMEOUT = 102,
    ERROR_VERSION_MISMATCH = 103,
};

/**
 * IPC Message Header (16 bytes)
 */
#pragma pack(push, 1)
struct MessageHeader {
    uint32_t length;        // Total message size (excluding header)
    uint32_t type;          // MessageType enum value
    uint32_t sequence;      // Message sequence number
    uint64_t timestamp;     // Unix timestamp in milliseconds
};
#pragma pack(pop)

/**
 * IPC Message - complete message with header and payload
 */
class Message {
public:
    Message();
    Message(MessageType type, const std::string& jsonPayload);
    Message(MessageType type, const std::vector<uint8_t>& binaryPayload);

    // Serialization
    std::vector<uint8_t> Serialize() const;
    static std::unique_ptr<Message> Deserialize(const std::vector<uint8_t>& data);

    // Accessors
    MessageType GetType() const { return static_cast<MessageType>(m_header.type); }
    uint32_t GetSequence() const { return m_header.sequence; }
    uint64_t GetTimestamp() const { return m_header.timestamp; }
    const std::vector<uint8_t>& GetPayload() const { return m_payload; }
    std::string GetPayloadAsString() const;

    // Setters
    void SetSequence(uint32_t seq) { m_header.sequence = seq; }

private:
    MessageHeader m_header;
    std::vector<uint8_t> m_payload;

    static uint64_t GetCurrentTimestamp();
};

// Helper functions for common message types
namespace MessageBuilder {
    std::unique_ptr<Message> CreateAuthRequest(const std::string& username, const std::string& password);
    std::unique_ptr<Message> CreateServerListRequest();
    std::unique_ptr<Message> CreateConnectRequest(const std::string& serverId);
    std::unique_ptr<Message> CreateDisconnectRequest();
    std::unique_ptr<Message> CreateChatMessage(const std::string& text);
}

// Helper functions for parsing responses
namespace MessageParser {
    struct AuthResponse {
        bool success;
        std::string token;
        std::string error;
    };

    struct ServerInfo {
        std::string id;
        std::string name;
        int playerCount;
        int maxPlayers;
        int ping;
    };

    struct ServerListResponse {
        std::vector<ServerInfo> servers;
    };

    AuthResponse ParseAuthResponse(const Message& msg);
    ServerListResponse ParseServerListResponse(const Message& msg);
    ConnectionStatus ParseConnectionStatus(const Message& msg);
}

} // namespace IPC
} // namespace ReKenshi

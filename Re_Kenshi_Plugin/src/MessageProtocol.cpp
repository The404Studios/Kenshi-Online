#include "MessageProtocol.h"
#include <chrono>
#include <cstring>
#include <sstream>

// For JSON parsing - using simple string building for now
// TODO: Add RapidJSON or similar library for proper JSON handling

namespace ReKenshi {
namespace IPC {

//=============================================================================
// Message Implementation
//=============================================================================

Message::Message() {
    std::memset(&m_header, 0, sizeof(m_header));
    m_header.timestamp = GetCurrentTimestamp();
}

Message::Message(MessageType type, const std::string& jsonPayload) : Message() {
    m_header.type = static_cast<uint32_t>(type);
    m_payload.assign(jsonPayload.begin(), jsonPayload.end());
    m_header.length = static_cast<uint32_t>(m_payload.size());
}

Message::Message(MessageType type, const std::vector<uint8_t>& binaryPayload) : Message() {
    m_header.type = static_cast<uint32_t>(type);
    m_payload = binaryPayload;
    m_header.length = static_cast<uint32_t>(m_payload.size());
}

std::vector<uint8_t> Message::Serialize() const {
    std::vector<uint8_t> result;
    result.reserve(sizeof(MessageHeader) + m_payload.size());

    // Write header
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&m_header);
    result.insert(result.end(), headerBytes, headerBytes + sizeof(MessageHeader));

    // Write payload
    result.insert(result.end(), m_payload.begin(), m_payload.end());

    return result;
}

std::unique_ptr<Message> Message::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(MessageHeader)) {
        return nullptr;
    }

    auto msg = std::make_unique<Message>();

    // Read header
    std::memcpy(&msg->m_header, data.data(), sizeof(MessageHeader));

    // Validate
    if (msg->m_header.length != data.size() - sizeof(MessageHeader)) {
        return nullptr;
    }

    // Read payload
    if (msg->m_header.length > 0) {
        msg->m_payload.assign(
            data.begin() + sizeof(MessageHeader),
            data.end()
        );
    }

    return msg;
}

std::string Message::GetPayloadAsString() const {
    return std::string(m_payload.begin(), m_payload.end());
}

uint64_t Message::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ms.count());
}

//=============================================================================
// MessageBuilder Implementation
//=============================================================================

namespace MessageBuilder {

std::unique_ptr<Message> CreateAuthRequest(const std::string& username, const std::string& password) {
    // Simple JSON building (replace with proper JSON library)
    std::ostringstream json;
    json << "{"
         << "\"username\":\"" << username << "\","
         << "\"password\":\"" << password << "\""
         << "}";

    return std::make_unique<Message>(MessageType::AUTHENTICATE_REQUEST, json.str());
}

std::unique_ptr<Message> CreateServerListRequest() {
    return std::make_unique<Message>(MessageType::SERVER_LIST_REQUEST, "{}");
}

std::unique_ptr<Message> CreateConnectRequest(const std::string& serverId) {
    std::ostringstream json;
    json << "{"
         << "\"serverId\":\"" << serverId << "\""
         << "}";

    return std::make_unique<Message>(MessageType::CONNECT_SERVER_REQUEST, json.str());
}

std::unique_ptr<Message> CreateDisconnectRequest() {
    return std::make_unique<Message>(MessageType::DISCONNECT_REQUEST, "{}");
}

std::unique_ptr<Message> CreateChatMessage(const std::string& text) {
    std::ostringstream json;
    json << "{"
         << "\"text\":\"" << text << "\""
         << "}";

    return std::make_unique<Message>(MessageType::CHAT_MESSAGE, json.str());
}

} // namespace MessageBuilder

//=============================================================================
// MessageParser Implementation
//=============================================================================

namespace MessageParser {

// Simple JSON parsing helpers (replace with proper JSON library)
static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    size_t endPos = json.find("\"", pos);
    if (endPos == std::string::npos) return "";

    return json.substr(pos, endPos - pos);
}

static bool ExtractJsonBool(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;

    pos += searchKey.length();
    return json.substr(pos, 4) == "true";
}

static int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0;

    pos += searchKey.length();
    return std::stoi(json.substr(pos));
}

AuthResponse ParseAuthResponse(const Message& msg) {
    AuthResponse response;
    std::string json = msg.GetPayloadAsString();

    response.success = ExtractJsonBool(json, "success");
    response.token = ExtractJsonString(json, "token");
    response.error = ExtractJsonString(json, "error");

    return response;
}

ServerListResponse ParseServerListResponse(const Message& msg) {
    ServerListResponse response;
    std::string json = msg.GetPayloadAsString();

    // Simple parsing - find all server objects
    // TODO: Replace with proper JSON parsing
    size_t pos = 0;
    while ((pos = json.find("{\"id\":", pos)) != std::string::npos) {
        ServerInfo info;
        std::string serverJson = json.substr(pos, json.find("}", pos) - pos + 1);

        info.id = ExtractJsonString(serverJson, "id");
        info.name = ExtractJsonString(serverJson, "name");
        info.playerCount = ExtractJsonInt(serverJson, "playerCount");
        info.maxPlayers = ExtractJsonInt(serverJson, "maxPlayers");
        info.ping = ExtractJsonInt(serverJson, "ping");

        response.servers.push_back(info);
        pos++;
    }

    return response;
}

ConnectionStatus ParseConnectionStatus(const Message& msg) {
    std::string json = msg.GetPayloadAsString();
    int status = ExtractJsonInt(json, "status");
    return static_cast<ConnectionStatus>(status);
}

} // namespace MessageParser

} // namespace IPC
} // namespace ReKenshi

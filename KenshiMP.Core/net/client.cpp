#include "client.h"
#include <spdlog/spdlog.h>

namespace kmp {

bool NetworkClient::Initialize() {
    if (m_initialized) return true;

    if (enet_initialize() != 0) {
        spdlog::error("NetworkClient: Failed to initialize ENet");
        return false;
    }

    m_host = enet_host_create(nullptr, 1, KMP_CHANNEL_COUNT,
                              KMP_DOWNSTREAM_LIMIT, KMP_UPSTREAM_LIMIT);
    if (!m_host) {
        spdlog::error("NetworkClient: Failed to create ENet host");
        enet_deinitialize();
        return false;
    }

    m_initialized = true;
    spdlog::info("NetworkClient: Initialized");
    return true;
}

void NetworkClient::Shutdown() {
    Disconnect();
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    if (m_initialized) {
        enet_deinitialize();
        m_initialized = false;
    }
}

bool NetworkClient::Connect(const std::string& address, uint16_t port) {
    if (!m_initialized || m_connected) return false;

    ENetAddress addr;
    enet_address_set_host(&addr, address.c_str());
    addr.port = port;

    m_serverPeer = enet_host_connect(m_host, &addr, KMP_CHANNEL_COUNT, 0);
    if (!m_serverPeer) {
        spdlog::error("NetworkClient: Failed to initiate connection to {}:{}", address, port);
        return false;
    }

    // Wait for connection (with timeout)
    ENetEvent event;
    if (enet_host_service(m_host, &event, KMP_CONNECT_TIMEOUT_MS) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        m_connected = true;
        m_serverAddr = address;
        m_serverPort = port;
        spdlog::info("NetworkClient: Connected to {}:{}", address, port);
        return true;
    }

    enet_peer_reset(m_serverPeer);
    m_serverPeer = nullptr;
    spdlog::error("NetworkClient: Connection to {}:{} timed out", address, port);
    return false;
}

void NetworkClient::Disconnect() {
    if (!m_connected || !m_serverPeer) return;

    enet_peer_disconnect(m_serverPeer, 0);

    // Wait for disconnect acknowledgment
    ENetEvent event;
    bool disconnected = false;
    while (enet_host_service(m_host, &event, 3000) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
            break;
        }
    }

    if (!disconnected) {
        enet_peer_reset(m_serverPeer);
    }

    m_serverPeer = nullptr;
    m_connected = false;
    spdlog::info("NetworkClient: Disconnected");
}

void NetworkClient::Update() {
    if (!m_host) return;

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE:
            if (m_callback) {
                m_callback(event.packet->data, event.packet->dataLength,
                          event.channelID);
            }
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            spdlog::warn("NetworkClient: Disconnected from server (reason: {})",
                         event.data);
            m_connected = false;
            m_serverPeer = nullptr;
            break;

        default:
            break;
        }
    }
}

void NetworkClient::Send(const uint8_t* data, size_t len, int channel, uint32_t flags) {
    if (!m_connected || !m_serverPeer) return;

    std::lock_guard lock(m_sendMutex);
    ENetPacket* packet = enet_packet_create(data, len, flags);
    if (packet) {
        enet_peer_send(m_serverPeer, channel, packet);
    }
}

void NetworkClient::SendReliable(const uint8_t* data, size_t len) {
    Send(data, len, KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void NetworkClient::SendReliableUnordered(const uint8_t* data, size_t len) {
    Send(data, len, KMP_CHANNEL_RELIABLE_UNORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void NetworkClient::SendUnreliable(const uint8_t* data, size_t len) {
    Send(data, len, KMP_CHANNEL_UNRELIABLE_SEQ, ENET_PACKET_FLAG_UNSEQUENCED);
}

uint32_t NetworkClient::GetPing() const {
    if (!m_serverPeer) return 0;
    return m_serverPeer->roundTripTime;
}

} // namespace kmp

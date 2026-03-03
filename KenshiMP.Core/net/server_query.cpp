#include "server_query.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace kmp {

static float GetTimeSeconds() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - start).count();
}

bool ServerQueryClient::Initialize() {
    if (m_initialized) return true;

    // Create a separate ENet host for queries (not the game client host)
    // Allow up to 8 simultaneous query connections
    m_host = enet_host_create(nullptr, 8, KMP_CHANNEL_COUNT, 0, 0);
    if (!m_host) {
        spdlog::error("ServerQueryClient: Failed to create ENet host");
        return false;
    }

    m_initialized = true;
    return true;
}

void ServerQueryClient::Shutdown() {
    // Reset all pending peers
    for (auto& q : m_pending) {
        if (q.peer) enet_peer_reset(q.peer);
    }
    m_pending.clear();

    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    m_initialized = false;
    m_active = false;
}

void ServerQueryClient::QueryServer(const std::string& address, uint16_t port) {
    if (!m_initialized) {
        if (!Initialize()) return;
    }

    ENetAddress addr;
    enet_address_set_host(&addr, address.c_str());
    addr.port = port;

    ENetPeer* peer = enet_host_connect(m_host, &addr, KMP_CHANNEL_COUNT, 0);
    if (!peer) {
        spdlog::debug("ServerQueryClient: Failed to connect to {}:{}", address, port);
        // Add an offline result immediately
        std::lock_guard lock(m_mutex);
        ServerQueryResult result;
        result.address = address;
        result.port = port;
        result.serverName = address + ":" + std::to_string(port);
        result.online = false;
        result.pending = false;
        m_results.push_back(result);
        return;
    }

    // Short timeout for queries (2 seconds)
    enet_peer_timeout(peer, 0, 2000, 2000);

    PendingQuery q;
    q.peer = peer;
    q.address = address;
    q.port = port;
    q.startTime = GetTimeSeconds();
    m_pending.push_back(q);
    m_active = true;

    spdlog::debug("ServerQueryClient: Querying {}:{}...", address, port);
}

void ServerQueryClient::QueryMasterServer(const std::string& masterAddress, uint16_t masterPort) {
    if (!m_initialized) {
        if (!Initialize()) return;
    }

    ENetAddress addr;
    enet_address_set_host(&addr, masterAddress.c_str());
    addr.port = masterPort;

    ENetPeer* peer = enet_host_connect(m_host, &addr, 1, 0);
    if (!peer) {
        spdlog::debug("ServerQueryClient: Failed to connect to master at {}:{}", masterAddress, masterPort);
        return;
    }

    enet_peer_timeout(peer, 0, 3000, 5000);

    PendingQuery q;
    q.peer = peer;
    q.address = masterAddress;
    q.port = masterPort;
    q.startTime = GetTimeSeconds();
    q.isMasterQuery = true;
    m_pending.push_back(q);
    m_active = true;

    spdlog::debug("ServerQueryClient: Querying master server at {}:{}...", masterAddress, masterPort);
}

void ServerQueryClient::Update() {
    if (!m_host || m_pending.empty()) {
        m_active = false;
        return;
    }

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            // Find which pending query this peer belongs to
            bool isMaster = false;
            for (auto& q : m_pending) {
                if (q.peer == event.peer && q.isMasterQuery) {
                    isMaster = true;
                    break;
                }
            }

            if (isMaster) {
                // Send master server list query
                PacketWriter writer;
                writer.WriteHeader(MessageType::MS_QueryList);
                ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(),
                                                      ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, 0, pkt);
            } else {
                // Send direct server query
                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_ServerQuery);
                MsgServerQuery query{};
                query.protocolVersion = KMP_PROTOCOL_VERSION;
                writer.WriteRaw(&query, sizeof(query));
                ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(),
                                                      ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
            }
            break;
        }

        case ENET_EVENT_TYPE_RECEIVE: {
            PacketReader reader(event.packet->data, event.packet->dataLength);
            PacketHeader header;
            if (reader.ReadHeader(header)) {
                if (header.type == MessageType::S2C_ServerInfo) {
                    // Direct server query response
                    MsgServerInfo info{};
                    if (reader.ReadRaw(&info, sizeof(info))) {
                        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                            if (it->peer == event.peer) {
                                float ping = (GetTimeSeconds() - it->startTime) * 1000.f;

                                std::lock_guard lock(m_mutex);
                                ServerQueryResult result;
                                result.address = it->address;
                                result.port = it->port;
                                result.serverName = info.serverName;
                                result.currentPlayers = info.currentPlayers;
                                result.maxPlayers = info.maxPlayers;
                                result.ping = static_cast<uint32_t>(ping);
                                result.online = true;
                                result.pending = false;
                                m_results.push_back(result);

                                spdlog::debug("ServerQueryClient: {}:{} -> '{}' {}/{} {}ms",
                                             result.address, result.port, result.serverName,
                                             result.currentPlayers, result.maxPlayers, result.ping);

                                m_pending.erase(it);
                                break;
                            }
                        }
                    }
                } else if (header.type == MessageType::MS_ServerList) {
                    // Master server list response
                    uint16_t count = 0;
                    if (reader.ReadU16(count)) {
                        spdlog::info("ServerQueryClient: Master returned {} servers", count);

                        std::lock_guard lock(m_mutex);
                        for (uint16_t i = 0; i < count && reader.Remaining() >= sizeof(MsgMasterServerEntry); i++) {
                            MsgMasterServerEntry entry{};
                            reader.ReadRaw(&entry, sizeof(entry));

                            ServerQueryResult result;
                            result.address = entry.address;
                            result.port = entry.port;
                            result.serverName = entry.serverName;
                            result.currentPlayers = entry.currentPlayers;
                            result.maxPlayers = entry.maxPlayers;
                            result.online = true;
                            result.pending = false;
                            result.ping = 0; // We don't have ping to the actual server
                            m_results.push_back(result);
                        }

                        // Remove the master query from pending
                        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                            if (it->peer == event.peer) {
                                m_pending.erase(it);
                                break;
                            }
                        }
                    }
                }
            }
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT: {
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->peer == event.peer) {
                    m_pending.erase(it);
                    break;
                }
            }
            break;
        }

        default:
            break;
        }
    }

    // Timeout stale queries (3 seconds)
    float now = GetTimeSeconds();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now - it->startTime > 3.0f) {
            spdlog::debug("ServerQueryClient: {}:{} timed out", it->address, it->port);

            std::lock_guard lock(m_mutex);
            ServerQueryResult result;
            result.address = it->address;
            result.port = it->port;
            result.serverName = it->address + ":" + std::to_string(it->port);
            result.online = false;
            result.pending = false;
            m_results.push_back(result);

            enet_peer_reset(it->peer);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }

    m_active = !m_pending.empty();
}

std::vector<ServerQueryResult> ServerQueryClient::GetResults() {
    std::lock_guard lock(m_mutex);
    return m_results;
}

void ServerQueryClient::Clear() {
    // Reset pending queries
    for (auto& q : m_pending) {
        if (q.peer) enet_peer_reset(q.peer);
    }
    m_pending.clear();

    std::lock_guard lock(m_mutex);
    m_results.clear();
    m_active = false;
}

} // namespace kmp

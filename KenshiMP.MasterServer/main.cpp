// KenshiMP Master Server
// Centralized registry for the server browser.
// Game servers register via heartbeat; clients query for the live list.
// Uses ENet on port 27801 (configurable).

#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include <enet/enet.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>

using json = nlohmann::json;

namespace {

struct RegisteredServer {
    std::string serverName;
    std::string address;     // External IP (from peer address or self-reported)
    uint16_t    port = 0;
    uint8_t     currentPlayers = 0;
    uint8_t     maxPlayers = 16;
    float       timeOfDay = 0.5f;
    uint8_t     pvpEnabled = 1;
    std::chrono::steady_clock::time_point lastHeartbeat;
    ENetPeer*   peer = nullptr; // Server's connection to master
};

// Key: "ip:port"
std::unordered_map<std::string, RegisteredServer> g_servers;
std::atomic<bool> g_running{true};

constexpr float HEARTBEAT_TIMEOUT_SEC = 90.f; // Remove after 90s no heartbeat
constexpr uint16_t DEFAULT_MASTER_PORT = 27801;
constexpr int MAX_CONNECTIONS = 128;

void SignalHandler(int) {
    g_running = false;
}

std::string PeerAddressString(ENetPeer* peer) {
    char buf[64];
    enet_address_get_host_ip(&peer->address, buf, sizeof(buf));
    return std::string(buf);
}

std::string MakeKey(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

void HandleRegister(ENetPeer* peer, const uint8_t* data, size_t size) {
    using namespace kmp;
    PacketReader reader(data, size);
    PacketHeader header;
    if (!reader.ReadHeader(header)) return;

    MsgMasterRegister msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    if (msg.protocolVersion != KMP_PROTOCOL_VERSION) {
        spdlog::warn("Master: Rejected server (protocol mismatch: {} vs {})",
                     msg.protocolVersion, KMP_PROTOCOL_VERSION);
        return;
    }

    std::string peerIP = PeerAddressString(peer);

    // Use peer's actual IP if server didn't provide one
    std::string externalIP = msg.externalIP[0] ? std::string(msg.externalIP) : peerIP;
    std::string key = MakeKey(externalIP, msg.gamePort);

    RegisteredServer srv;
    srv.serverName = msg.serverName;
    srv.address = externalIP;
    srv.port = msg.gamePort;
    srv.currentPlayers = msg.currentPlayers;
    srv.maxPlayers = msg.maxPlayers;
    srv.timeOfDay = msg.timeOfDay;
    srv.pvpEnabled = msg.pvpEnabled;
    srv.lastHeartbeat = std::chrono::steady_clock::now();
    srv.peer = peer;

    bool isNew = (g_servers.find(key) == g_servers.end());
    g_servers[key] = srv;

    if (isNew) {
        spdlog::info("Master: Server registered: '{}' at {} ({}/{} players)",
                     srv.serverName, key, srv.currentPlayers, srv.maxPlayers);
    } else {
        spdlog::debug("Master: Server updated: '{}' at {}", srv.serverName, key);
    }
}

void HandleHeartbeat(ENetPeer* peer, const uint8_t* data, size_t size) {
    using namespace kmp;
    PacketReader reader(data, size);
    PacketHeader header;
    if (!reader.ReadHeader(header)) return;

    MsgMasterHeartbeat msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    std::string peerIP = PeerAddressString(peer);
    std::string key = MakeKey(peerIP, msg.gamePort);

    auto it = g_servers.find(key);
    if (it != g_servers.end()) {
        it->second.currentPlayers = msg.currentPlayers;
        it->second.maxPlayers = msg.maxPlayers;
        it->second.timeOfDay = msg.timeOfDay;
        it->second.lastHeartbeat = std::chrono::steady_clock::now();
    } else {
        // Unknown server heartbeat — check if registered under different key
        // (external IP may differ from peer IP due to NAT)
        for (auto& [k, s] : g_servers) {
            if (s.peer == peer && s.port == msg.gamePort) {
                s.currentPlayers = msg.currentPlayers;
                s.maxPlayers = msg.maxPlayers;
                s.timeOfDay = msg.timeOfDay;
                s.lastHeartbeat = std::chrono::steady_clock::now();
                return;
            }
        }
        spdlog::debug("Master: Heartbeat from unknown server {}", key);
    }
}

void HandleDeregister(ENetPeer* peer, const uint8_t* data, size_t size) {
    std::string peerIP = PeerAddressString(peer);

    // Remove all servers from this peer
    for (auto it = g_servers.begin(); it != g_servers.end(); ) {
        if (it->second.peer == peer) {
            spdlog::info("Master: Server deregistered: '{}' at {}",
                         it->second.serverName, it->first);
            it = g_servers.erase(it);
        } else {
            ++it;
        }
    }
}

void HandleQueryList(ENetPeer* peer) {
    using namespace kmp;

    // Build server list response
    PacketWriter writer;
    writer.WriteHeader(MessageType::MS_ServerList);

    // Write count
    uint16_t count = static_cast<uint16_t>(g_servers.size());
    writer.WriteU16(count);

    // Write each server entry
    for (auto& [key, srv] : g_servers) {
        MsgMasterServerEntry entry{};
        strncpy(entry.serverName, srv.serverName.c_str(), sizeof(entry.serverName) - 1);
        strncpy(entry.address, srv.address.c_str(), sizeof(entry.address) - 1);
        entry.port = srv.port;
        entry.currentPlayers = srv.currentPlayers;
        entry.maxPlayers = srv.maxPlayers;
        entry.pvpEnabled = srv.pvpEnabled;
        writer.WriteRaw(&entry, sizeof(entry));
    }

    ENetPacket* packet = enet_packet_create(writer.Data(), writer.Size(),
                                             ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);

    spdlog::debug("Master: Sent server list ({} servers) to {}",
                  count, PeerAddressString(peer));

    // Disconnect browser client after sending list (lightweight query)
    enet_peer_disconnect_later(peer, 0);
}

void PruneStaleServers() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = g_servers.begin(); it != g_servers.end(); ) {
        float elapsed = std::chrono::duration<float>(now - it->second.lastHeartbeat).count();
        if (elapsed > HEARTBEAT_TIMEOUT_SEC) {
            spdlog::info("Master: Pruned stale server '{}' at {} ({}s since heartbeat)",
                         it->second.serverName, it->first, static_cast<int>(elapsed));
            it = g_servers.erase(it);
        } else {
            ++it;
        }
    }
}

void HandlePacket(ENetPeer* peer, const uint8_t* data, size_t size) {
    using namespace kmp;

    if (size < sizeof(PacketHeader)) return;
    PacketReader reader(data, size);
    PacketHeader header;
    if (!reader.ReadHeader(header)) return;

    // Reset reader to beginning so handlers can re-read header
    PacketReader fullReader(data, size);

    switch (header.type) {
        case MessageType::MS_Register:
            HandleRegister(peer, data, size);
            break;
        case MessageType::MS_Heartbeat:
            HandleHeartbeat(peer, data, size);
            break;
        case MessageType::MS_Deregister:
            HandleDeregister(peer, data, size);
            break;
        case MessageType::MS_QueryList:
            HandleQueryList(peer);
            break;
        default:
            spdlog::debug("Master: Unknown message type 0x{:02X} from {}",
                          static_cast<uint8_t>(header.type), PeerAddressString(peer));
            break;
    }
}

struct MasterConfig {
    uint16_t    port = DEFAULT_MASTER_PORT;
    std::string logFile = "KenshiMP_Master.log";

    bool Load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        try {
            json j;
            file >> j;
            if (j.contains("port"))    port    = j["port"].get<uint16_t>();
            if (j.contains("logFile")) logFile = j["logFile"].get<std::string>();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool Save(const std::string& path) const {
        json j;
        j["port"] = port;
        j["logFile"] = logFile;
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    }
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Setup logging
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KenshiMP_Master.log", true);
    auto logger = std::make_shared<spdlog::logger>("master",
        spdlog::sinks_init_list{consoleSink, fileSink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    // Load config
    MasterConfig config;
    std::string configPath = (argc > 1) ? argv[1] : "master.json";
    if (!config.Load(configPath)) {
        spdlog::info("Master: No config found at '{}', using defaults", configPath);
        config.Save(configPath);
    }

    // Signal handler
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Initialize ENet
    if (enet_initialize() != 0) {
        spdlog::error("Master: Failed to initialize ENet");
        return 1;
    }

    // Create host
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = config.port;

    ENetHost* host = enet_host_create(&address, MAX_CONNECTIONS, 1, 0, 0);
    if (!host) {
        spdlog::error("Master: Failed to create ENet host on port {}", config.port);
        enet_deinitialize();
        return 1;
    }

    spdlog::info("=== KenshiMP Master Server ===");
    spdlog::info("Listening on port {}", config.port);
    spdlog::info("Press Ctrl+C to stop");

    float timeSincePrune = 0.f;
    auto lastTick = std::chrono::steady_clock::now();

    // Console thread for admin commands
    std::thread consoleThread([&]() {
        std::string line;
        while (g_running && std::getline(std::cin, line)) {
            if (line == "stop" || line == "quit" || line == "exit") {
                g_running = false;
            } else if (line == "status") {
                spdlog::info("=== Master Status ===");
                spdlog::info("Registered servers: {}", g_servers.size());
                for (auto& [key, srv] : g_servers) {
                    float age = std::chrono::duration<float>(
                        std::chrono::steady_clock::now() - srv.lastHeartbeat).count();
                    spdlog::info("  '{}' at {} ({}/{}) last heartbeat {:.0f}s ago",
                                 srv.serverName, key, srv.currentPlayers, srv.maxPlayers, age);
                }
            } else if (line == "help") {
                spdlog::info("Commands: status, stop/quit/exit, help");
            }
        }
    });
    consoleThread.detach();

    // Main loop
    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;

        // Poll ENet events
        ENetEvent event;
        while (enet_host_service(host, &event, 0) > 0) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    spdlog::debug("Master: Peer connected from {}", PeerAddressString(event.peer));
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    HandlePacket(event.peer, event.packet->data, event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                    break;

                case ENET_EVENT_TYPE_DISCONNECT: {
                    std::string peerIP = PeerAddressString(event.peer);
                    // Remove any servers from this peer
                    for (auto it = g_servers.begin(); it != g_servers.end(); ) {
                        if (it->second.peer == event.peer) {
                            spdlog::info("Master: Server disconnected: '{}' at {}",
                                         it->second.serverName, it->first);
                            it = g_servers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Prune stale servers every 30 seconds
        timeSincePrune += dt;
        if (timeSincePrune >= 30.f) {
            timeSincePrune = 0.f;
            PruneStaleServers();
        }

        enet_host_flush(host);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    spdlog::info("Master: Shutting down...");
    enet_host_destroy(host);
    enet_deinitialize();
    return 0;
}

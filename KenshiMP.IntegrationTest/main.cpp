// KenshiMP Integration Test
// Starts a server, connects two fake clients, and verifies the full protocol pipeline.
// Tests: handshake, entity spawn, position sync, chat relay, disconnect cleanup.
// Run from the build output directory (or Kenshi dir where KenshiMP.Server.exe lives).

#include <kmp/protocol.h>
#include <kmp/messages.h>
#include <kmp/constants.h>
#include <kmp/types.h>
#include <enet/enet.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace kmp;

// ─────────────────────────────────────────────────
//  Test Framework
// ─────────────────────────────────────────────────

static int g_testsPassed = 0;
static int g_testsFailed = 0;

static void TestAssert(bool condition, const char* testName) {
    if (condition) {
        printf("  [PASS] %s\n", testName);
        g_testsPassed++;
    } else {
        printf("  [FAIL] %s\n", testName);
        g_testsFailed++;
    }
}

// ─────────────────────────────────────────────────
//  Simple ENet Client Wrapper
// ─────────────────────────────────────────────────

struct TestClient {
    std::string name;
    ENetHost*   host = nullptr;
    ENetPeer*   peer = nullptr;
    PlayerID    playerId = 0;
    bool        connected = false;
    bool        handshakeOk = false;
    EntityID    myEntityId = 0;

    // Received data tracking
    std::vector<MsgPlayerJoined>  playersJoined;
    std::vector<MsgPlayerLeft>    playersLeft;
    std::vector<uint32_t>         entitiesSpawned;   // entity IDs
    std::vector<uint32_t>         entitiesDespawned; // entity IDs
    int                           posUpdatesReceived = 0;
    std::vector<std::string>      chatMessages;
    std::vector<std::string>      systemMessages;
    int                           timeSyncsReceived = 0;
    MsgHandshakeAck               lastAck{};
    bool                          wasRejected = false;

    // New system tracking
    std::vector<MsgInventoryUpdate>   inventoryUpdates;
    std::vector<MsgTradeResult>       tradeResults;
    std::vector<uint32_t>             squadsCreated;     // squad net IDs
    std::vector<MsgSquadMemberUpdate> squadMemberUpdates;
    std::vector<MsgFactionRelation>   factionRelations;
    std::vector<MsgBuildPlaced>       buildingsPlaced;
    std::vector<uint32_t>             buildingsDestroyed;
    std::vector<MsgBuildProgress>     buildProgress;

    bool Init(const std::string& playerName) {
        name = playerName;
        host = enet_host_create(nullptr, 1, KMP_CHANNEL_COUNT,
                                KMP_DOWNSTREAM_LIMIT, KMP_UPSTREAM_LIMIT);
        return host != nullptr;
    }

    bool Connect(const char* addr, uint16_t port) {
        ENetAddress enetAddr;
        enet_address_set_host(&enetAddr, addr);
        enetAddr.port = port;
        peer = enet_host_connect(host, &enetAddr, KMP_CHANNEL_COUNT, 0);
        return peer != nullptr;
    }

    void SendReliable(const uint8_t* data, size_t len) {
        ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
        enet_host_flush(host); // Flush immediately so packet is sent even if we poll another host next
    }

    void SendUnreliable(const uint8_t* data, size_t len) {
        ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_UNSEQUENCED);
        enet_peer_send(peer, KMP_CHANNEL_UNRELIABLE_SEQ, pkt);
    }

    void SendHandshake() {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_Handshake);
        MsgHandshake hs{};
        hs.protocolVersion = KMP_PROTOCOL_VERSION;
        strncpy(hs.playerName, name.c_str(), KMP_MAX_NAME_LENGTH);
        hs.playerName[KMP_MAX_NAME_LENGTH] = '\0';
        hs.gameVersionMajor = 1;
        hs.gameVersionMinor = 0;
        hs.gameVersionPatch = 68;
        w.WriteRaw(&hs, sizeof(hs));
        SendReliable(w.Data(), w.Size());
    }

    void SendEntitySpawn(float x, float y, float z) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_EntitySpawnReq);
        w.WriteU32(1);  // client entity ID
        w.WriteU8(static_cast<uint8_t>(EntityType::PlayerCharacter));
        w.WriteU32(playerId);
        w.WriteU32(0);  // template ID
        w.WriteF32(x);
        w.WriteF32(y);
        w.WriteF32(z);
        w.WriteU32(Quat().Compress());  // identity rotation
        w.WriteU32(0);  // faction
        std::string tmpl = "Greenlander";
        w.WriteU16(static_cast<uint16_t>(tmpl.size()));
        w.WriteRaw(tmpl.data(), tmpl.size());
        SendReliable(w.Data(), w.Size());
    }

    void SendPositionUpdate(EntityID entityId, float x, float y, float z) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_PositionUpdate);
        w.WriteU8(1);  // 1 character

        CharacterPosition cp{};
        cp.entityId = entityId;
        cp.posX = x;
        cp.posY = y;
        cp.posZ = z;
        cp.compressedQuat = Quat().Compress();
        cp.animStateId = 1;
        cp.moveSpeed = 85;
        cp.flags = 0x01;
        w.WriteRaw(&cp, sizeof(cp));

        SendUnreliable(w.Data(), w.Size());
    }

    void SendChat(const std::string& msg) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_ChatMessage);
        w.WriteU32(playerId);
        w.WriteString(msg);
        SendReliable(w.Data(), w.Size());
    }

    void SendItemPickup(EntityID entityId, uint32_t itemId, int32_t qty) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_ItemPickup);
        MsgItemPickup msg{};
        msg.entityId = entityId;
        msg.itemTemplateId = itemId;
        msg.quantity = qty;
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void SendItemDrop(EntityID entityId, uint32_t itemId, float x, float y, float z) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_ItemDrop);
        MsgItemDrop msg{};
        msg.entityId = entityId;
        msg.itemTemplateId = itemId;
        msg.posX = x; msg.posY = y; msg.posZ = z;
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void SendTradeRequest(EntityID buyerId, uint32_t itemId, int32_t qty, int32_t price) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_TradeRequest);
        MsgTradeRequest msg{};
        msg.buyerEntityId = buyerId;
        msg.sellerEntityId = 0;
        msg.itemTemplateId = itemId;
        msg.quantity = qty;
        msg.price = price;
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void SendSquadCreate(EntityID creatorId, const std::string& squadName) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_SquadCreate);
        w.WriteU32(creatorId);
        w.WriteString(squadName);
        SendReliable(w.Data(), w.Size());
    }

    void SendFactionRelation(uint32_t factionA, uint32_t factionB, float relation) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_FactionRelation);
        MsgFactionRelation msg{};
        msg.factionIdA = factionA;
        msg.factionIdB = factionB;
        msg.relation = relation;
        msg.causerEntityId = 0;
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void SendBuildRequest(uint32_t templateId, float x, float y, float z) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_BuildRequest);
        MsgBuildRequest msg{};
        msg.templateId = templateId;
        msg.posX = x; msg.posY = y; msg.posZ = z;
        msg.compressedQuat = Quat().Compress();
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void SendBuildDismantle(EntityID buildingId) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_BuildDismantle);
        MsgBuildDismantle msg{};
        msg.buildingId = buildingId;
        msg.dismantlerId = myEntityId;
        w.WriteRaw(&msg, sizeof(msg));
        SendReliable(w.Data(), w.Size());
    }

    void HandlePacket(const uint8_t* data, size_t len) {
        if (len < sizeof(PacketHeader)) return;

        PacketReader r(data, len);
        PacketHeader hdr;
        r.ReadHeader(hdr);

        switch (hdr.type) {
        case MessageType::S2C_HandshakeAck: {
            MsgHandshakeAck ack;
            if (r.ReadRaw(&ack, sizeof(ack))) {
                playerId = ack.playerId;
                handshakeOk = true;
                lastAck = ack;
            }
            break;
        }
        case MessageType::S2C_HandshakeReject: {
            wasRejected = true;
            break;
        }
        case MessageType::S2C_PlayerJoined: {
            MsgPlayerJoined pj;
            if (r.ReadRaw(&pj, sizeof(pj))) {
                playersJoined.push_back(pj);
            }
            break;
        }
        case MessageType::S2C_PlayerLeft: {
            MsgPlayerLeft pl;
            if (r.ReadRaw(&pl, sizeof(pl))) {
                playersLeft.push_back(pl);
            }
            break;
        }
        case MessageType::S2C_EntitySpawn: {
            uint32_t entId, ownerId, templateId, compQuat, factionId;
            uint8_t type;
            float px, py, pz;
            r.ReadU32(entId);
            r.ReadU8(type);
            r.ReadU32(ownerId);
            r.ReadU32(templateId);
            r.ReadF32(px); r.ReadF32(py); r.ReadF32(pz);
            r.ReadU32(compQuat);
            r.ReadU32(factionId);
            std::string tmplName;
            r.ReadString(tmplName);

            entitiesSpawned.push_back(entId);

            // Track our own entity
            if (ownerId == playerId && myEntityId == 0) {
                myEntityId = entId;
            }
            break;
        }
        case MessageType::S2C_EntityDespawn: {
            MsgEntityDespawn ds;
            if (r.ReadRaw(&ds, sizeof(ds))) {
                entitiesDespawned.push_back(ds.entityId);
            }
            break;
        }
        case MessageType::S2C_PositionUpdate: {
            posUpdatesReceived++;
            break;
        }
        case MessageType::S2C_ChatMessage: {
            uint32_t senderId;
            r.ReadU32(senderId);
            std::string msg;
            r.ReadString(msg);
            chatMessages.push_back(msg);
            break;
        }
        case MessageType::S2C_SystemMessage: {
            uint32_t senderId;
            r.ReadU32(senderId);
            std::string msg;
            r.ReadString(msg);
            systemMessages.push_back(msg);
            break;
        }
        case MessageType::S2C_TimeSync: {
            timeSyncsReceived++;
            break;
        }
        case MessageType::S2C_InventoryUpdate: {
            MsgInventoryUpdate inv;
            if (r.ReadRaw(&inv, sizeof(inv))) {
                inventoryUpdates.push_back(inv);
            }
            break;
        }
        case MessageType::S2C_TradeResult: {
            MsgTradeResult tr;
            if (r.ReadRaw(&tr, sizeof(tr))) {
                tradeResults.push_back(tr);
            }
            break;
        }
        case MessageType::S2C_SquadCreated: {
            uint32_t creator, squadId;
            r.ReadU32(creator);
            r.ReadU32(squadId);
            std::string squadName;
            r.ReadString(squadName);
            squadsCreated.push_back(squadId);
            break;
        }
        case MessageType::S2C_SquadMemberUpdate: {
            MsgSquadMemberUpdate smu;
            if (r.ReadRaw(&smu, sizeof(smu))) {
                squadMemberUpdates.push_back(smu);
            }
            break;
        }
        case MessageType::S2C_FactionRelation: {
            MsgFactionRelation fr;
            if (r.ReadRaw(&fr, sizeof(fr))) {
                factionRelations.push_back(fr);
            }
            break;
        }
        case MessageType::S2C_BuildPlaced: {
            MsgBuildPlaced bp;
            if (r.ReadRaw(&bp, sizeof(bp))) {
                buildingsPlaced.push_back(bp);
            }
            break;
        }
        case MessageType::S2C_BuildDestroyed: {
            uint32_t buildingId;
            uint8_t reason;
            r.ReadU32(buildingId);
            r.ReadU8(reason);
            buildingsDestroyed.push_back(buildingId);
            break;
        }
        case MessageType::S2C_BuildProgress: {
            MsgBuildProgress prog;
            if (r.ReadRaw(&prog, sizeof(prog))) {
                buildProgress.push_back(prog);
            }
            break;
        }
        default:
            break;
        }
    }

    // Poll ENet events for up to timeoutMs milliseconds.
    // Returns number of events processed.
    int Poll(int timeoutMs = 100) {
        int count = 0;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            ENetEvent event;
            int result = enet_host_service(host, &event, 5);
            if (result > 0) {
                count++;
                switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    connected = true;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    HandlePacket(event.packet->data, event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    connected = false;
                    break;
                default:
                    break;
                }
            }
        }
        return count;
    }

    // Poll until a condition is met or timeout
    bool PollUntil(std::function<bool()> condition, int timeoutMs = 3000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            Poll(50);
            if (condition()) return true;
        }
        return false;
    }

    void Disconnect() {
        if (peer) {
            enet_peer_disconnect(peer, 0);
            // Drain events briefly to let disconnect propagate
            ENetEvent event;
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < deadline) {
                if (enet_host_service(host, &event, 50) > 0) {
                    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                        HandlePacket(event.packet->data, event.packet->dataLength);
                        enet_packet_destroy(event.packet);
                    } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                        connected = false;
                        break;
                    }
                }
            }
            // If graceful disconnect didn't complete, force it so the server
            // sees the peer go away immediately (prevents zombie peer slots)
            if (connected && peer) {
                enet_peer_reset(peer);
                connected = false;
            }
            peer = nullptr;
        }
    }

    void Destroy() {
        if (host) {
            enet_host_destroy(host);
            host = nullptr;
        }
    }
};

// ─────────────────────────────────────────────────
//  Server Process Management
// ─────────────────────────────────────────────────

#ifdef _WIN32
static PROCESS_INFORMATION g_serverProcess{};

static bool StartServer(const char* exePath) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);

    // Build command line
    char cmdLine[512];
    sprintf_s(cmdLine, "\"%s\"", exePath);

    // Derive working directory from exe path (server needs server.json in its cwd)
    std::string exeStr(exePath);
    std::string workDir;
    auto lastSlash = exeStr.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        workDir = exeStr.substr(0, lastSlash);
    }
    const char* workDirPtr = workDir.empty() ? nullptr : workDir.c_str();

    printf("[Server] Working directory: %s\n", workDirPtr ? workDirPtr : "(current)");

    if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, workDirPtr,
                        &si, &g_serverProcess)) {
        printf("ERROR: Failed to start server (error %lu)\n", GetLastError());
        printf("  Tried: %s\n", exePath);
        return false;
    }
    printf("[Server] Started (PID %lu)\n", g_serverProcess.dwProcessId);
    return true;
}

static void StopServer() {
    if (g_serverProcess.hProcess) {
        TerminateProcess(g_serverProcess.hProcess, 0);
        WaitForSingleObject(g_serverProcess.hProcess, 2000);
        CloseHandle(g_serverProcess.hProcess);
        CloseHandle(g_serverProcess.hThread);
        g_serverProcess = {};
        printf("[Server] Stopped\n");
    }
}
#endif

// ─────────────────────────────────────────────────
//  Find Server Executable
// ─────────────────────────────────────────────────

static std::string FindServerExe() {
    // Try several locations
    const char* candidates[] = {
        // Same directory as this test exe
        "KenshiMP.Server.exe",
        // Build output
        "../KenshiMP.Server.exe",
        // Kenshi directory (post-build copy)
        "../../KenshiMP.Server.exe",
    };

    for (auto& path : candidates) {
        DWORD attrs = GetFileAttributesA(path);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }

    // Try absolute Kenshi path
    const char* kenshiPath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiMP.Server.exe";
    if (GetFileAttributesA(kenshiPath) != INVALID_FILE_ATTRIBUTES) {
        return kenshiPath;
    }

    return "";
}

// ─────────────────────────────────────────────────
//  Test Suites
// ─────────────────────────────────────────────────

static void Test_ServerConnection() {
    printf("\n=== Test: Server Connection ===\n");

    TestClient client;
    TestAssert(client.Init("TestPlayer1"), "Client init");

    TestAssert(client.Connect("127.0.0.1", KMP_DEFAULT_PORT), "Client connect call");

    // Wait for ENet connection
    bool enetConnected = client.PollUntil([&]() { return client.connected; }, 3000);
    TestAssert(enetConnected, "ENet connection established");

    if (!enetConnected) {
        client.Disconnect();
        client.Destroy();
        return;
    }

    // Send handshake
    client.SendHandshake();

    // Wait for handshake ack
    bool gotAck = client.PollUntil([&]() { return client.handshakeOk; }, 3000);
    TestAssert(gotAck, "Handshake acknowledged");

    if (gotAck) {
        TestAssert(client.playerId > 0, "Received valid player ID");
        TestAssert(client.lastAck.maxPlayers > 0, "Server reports max players > 0");
        printf("    Player ID: %u, Players: %u/%u\n",
               client.playerId, client.lastAck.currentPlayers, client.lastAck.maxPlayers);
    }

    client.Disconnect();
    client.Destroy();
}

static void Test_TwoPlayersConnect() {
    printf("\n=== Test: Two Players Connect ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    // Connect both
    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    // Wait for ENet connections
    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);
    TestAssert(client1.connected && client2.connected, "Both clients connected");

    if (!client1.connected || !client2.connected) {
        client1.Disconnect(); client2.Disconnect();
        client1.Destroy(); client2.Destroy();
        return;
    }

    // Client 1 handshake
    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    TestAssert(client1.handshakeOk, "Client 1 handshake OK");

    // Small delay so server processes client 1 fully
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client 2 handshake
    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);
    TestAssert(client2.handshakeOk, "Client 2 handshake OK");

    // Client 1 should receive "player joined" for client 2
    bool c1SawJoin = client1.PollUntil([&]() {
        return !client1.playersJoined.empty();
    }, 3000);
    TestAssert(c1SawJoin, "Client 1 received PlayerJoined for Client 2");

    if (c1SawJoin) {
        bool nameMatch = (strncmp(client1.playersJoined.back().playerName, "Bob",
                                  KMP_MAX_NAME_LENGTH) == 0);
        TestAssert(nameMatch, "PlayerJoined name matches 'Bob'");
        printf("    Client 1 saw join: '%s' (ID: %u)\n",
               client1.playersJoined.back().playerName,
               client1.playersJoined.back().playerId);
    }

    // Verify different player IDs
    TestAssert(client1.playerId != client2.playerId,
               "Clients have different player IDs");
    printf("    Alice ID: %u, Bob ID: %u\n", client1.playerId, client2.playerId);

    client1.Disconnect();
    client2.Disconnect();
    client1.Destroy();
    client2.Destroy();
}

static void Test_EntitySpawnAndBroadcast() {
    printf("\n=== Test: Entity Spawn & Broadcast ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);

    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);
    // Drain join notifications
    client1.Poll(200);
    client2.Poll(200);

    // Client 1 spawns an entity
    float spawnX = -51200.f, spawnY = 1600.f, spawnZ = 2700.f;
    client1.SendEntitySpawn(spawnX, spawnY, spawnZ);

    // Client 1 should get confirmation (S2C_EntitySpawn for own entity)
    bool c1GotEntity = client1.PollUntil([&]() {
        return client1.myEntityId != 0;
    }, 3000);
    TestAssert(c1GotEntity, "Client 1 received own entity spawn confirmation");
    if (c1GotEntity) {
        printf("    Client 1 entity ID: %u\n", client1.myEntityId);
    }

    // Client 2 should also see the entity spawn broadcast
    size_t c2SpawnsBefore = client2.entitiesSpawned.size();
    bool c2SawSpawn = client2.PollUntil([&]() {
        return client2.entitiesSpawned.size() > c2SpawnsBefore;
    }, 3000);
    TestAssert(c2SawSpawn, "Client 2 received entity spawn broadcast");

    if (c2SawSpawn) {
        // Check that the entity ID matches
        bool foundEntity = false;
        for (auto id : client2.entitiesSpawned) {
            if (id == client1.myEntityId) {
                foundEntity = true;
                break;
            }
        }
        TestAssert(foundEntity, "Client 2 sees Client 1's entity ID");
    }

    // Now Client 2 spawns
    client2.SendEntitySpawn(spawnX + 20.f, spawnY, spawnZ + 20.f);
    bool c2GotEntity = client2.PollUntil([&]() {
        return client2.myEntityId != 0;
    }, 3000);
    TestAssert(c2GotEntity, "Client 2 received own entity spawn confirmation");

    // Client 1 should see Client 2's entity
    size_t c1SpawnsBefore = client1.entitiesSpawned.size();
    bool c1SawC2Spawn = client1.PollUntil([&]() {
        return client1.entitiesSpawned.size() > c1SpawnsBefore;
    }, 3000);
    TestAssert(c1SawC2Spawn, "Client 1 received Client 2's entity spawn");

    client1.Disconnect();
    client2.Disconnect();
    client1.Destroy();
    client2.Destroy();
}

static void Test_PositionSync() {
    printf("\n=== Test: Position Sync ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);

    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);

    // Drain join notifications
    client1.Poll(200);
    client2.Poll(200);

    // Both spawn entities
    client1.SendEntitySpawn(-51200.f, 1600.f, 2700.f);
    client1.PollUntil([&]() { return client1.myEntityId != 0; }, 3000);

    client2.SendEntitySpawn(-51180.f, 1600.f, 2720.f);
    client2.PollUntil([&]() { return client2.myEntityId != 0; }, 3000);

    // Drain spawn broadcasts
    client1.Poll(300);
    client2.Poll(300);

    TestAssert(client1.myEntityId != 0 && client2.myEntityId != 0,
               "Both clients have entities");

    // Client 1 sends several position updates
    int c2PosBefore = client2.posUpdatesReceived;
    for (int i = 0; i < 5; i++) {
        float x = -51200.f + static_cast<float>(i) * 10.f;
        client1.SendPositionUpdate(client1.myEntityId, x, 1600.f, 2700.f);
        // Small delay to let server forward
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        client2.Poll(50);
    }

    // Give extra time for server forwarding
    client2.Poll(500);

    bool gotPosUpdates = (client2.posUpdatesReceived > c2PosBefore);
    TestAssert(gotPosUpdates, "Client 2 received position updates from Client 1");
    printf("    Position updates received by Client 2: %d (was %d)\n",
           client2.posUpdatesReceived, c2PosBefore);

    // Client 2 sends position updates back
    int c1PosBefore = client1.posUpdatesReceived;
    for (int i = 0; i < 5; i++) {
        float z = 2720.f + static_cast<float>(i) * 10.f;
        client2.SendPositionUpdate(client2.myEntityId, -51180.f, 1600.f, z);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        client1.Poll(50);
    }
    client1.Poll(500);

    bool gotReverse = (client1.posUpdatesReceived > c1PosBefore);
    TestAssert(gotReverse, "Client 1 received position updates from Client 2");
    printf("    Position updates received by Client 1: %d (was %d)\n",
           client1.posUpdatesReceived, c1PosBefore);

    client1.Disconnect();
    client2.Disconnect();
    client1.Destroy();
    client2.Destroy();
}

static void Test_ChatRelay() {
    printf("\n=== Test: Chat Relay ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);

    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);

    // Drain notifications
    client1.Poll(200);
    client2.Poll(200);

    // Drain any remaining notifications (join messages, system messages, etc.)
    client1.Poll(300);
    client2.Poll(300);

    // Clear chat state for clean test
    client1.chatMessages.clear();
    client2.chatMessages.clear();

    // Client 1 sends a chat message
    client1.SendChat("Hello from Alice!");

    // Poll BOTH clients — server may broadcast to sender too
    bool c2GotChat = false;
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            client1.Poll(25);
            client2.Poll(25);
            if (!client2.chatMessages.empty()) { c2GotChat = true; break; }
        }
    }
    TestAssert(c2GotChat, "Client 2 received chat from Client 1");

    if (c2GotChat) {
        bool msgMatch = false;
        for (auto& m : client2.chatMessages) {
            if (m.find("Hello from Alice!") != std::string::npos) {
                msgMatch = true;
                break;
            }
        }
        TestAssert(msgMatch, "Chat message content matches");
    }

    // Clear and test reverse direction
    size_t c1ChatBefore = client1.chatMessages.size();
    client2.SendChat("Hi Alice, Bob here!");

    bool c1GotChat = false;
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            client1.Poll(25);
            client2.Poll(25);
            if (client1.chatMessages.size() > c1ChatBefore) { c1GotChat = true; break; }
        }
    }
    TestAssert(c1GotChat, "Client 1 received chat from Client 2");

    if (c1GotChat) {
        bool msgMatch = false;
        for (auto& m : client1.chatMessages) {
            if (m.find("Hi Alice, Bob here!") != std::string::npos) {
                msgMatch = true;
                break;
            }
        }
        TestAssert(msgMatch, "Reply chat message content matches");
    }

    client1.Disconnect();
    client2.Disconnect();
    client1.Destroy();
    client2.Destroy();
}

static void Test_DisconnectCleanup() {
    printf("\n=== Test: Disconnect Cleanup ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);

    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);

    // Drain
    client1.Poll(200);
    client2.Poll(200);

    // Client 2 spawns an entity
    client2.SendEntitySpawn(-51200.f, 1600.f, 2700.f);
    client2.PollUntil([&]() { return client2.myEntityId != 0; }, 3000);
    // Client 1 sees it
    client1.Poll(500);

    PlayerID bobId = client2.playerId;
    EntityID bobEntity = client2.myEntityId;
    TestAssert(bobEntity != 0, "Bob has a spawned entity");

    // Now Client 2 disconnects
    printf("    Bob disconnecting...\n");
    client2.Disconnect();
    client2.Destroy();

    // Give server time to process disconnect
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Client 1 should receive:
    // 1. S2C_EntityDespawn for Bob's entity
    // 2. S2C_PlayerLeft for Bob
    client1.Poll(2000);

    bool gotDespawn = false;
    for (auto id : client1.entitiesDespawned) {
        if (id == bobEntity) {
            gotDespawn = true;
            break;
        }
    }
    TestAssert(gotDespawn, "Client 1 received EntityDespawn for Bob's entity");

    bool gotPlayerLeft = false;
    for (auto& pl : client1.playersLeft) {
        if (pl.playerId == bobId) {
            gotPlayerLeft = true;
            break;
        }
    }
    TestAssert(gotPlayerLeft, "Client 1 received PlayerLeft for Bob");

    client1.Disconnect();
    client1.Destroy();
}

static void Test_TimeSync() {
    printf("\n=== Test: Time Sync ===\n");

    TestClient client;
    client.Init("TimeTestPlayer");

    client.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client.PollUntil([&]() { return client.connected; }, 3000);

    client.SendHandshake();
    client.PollUntil([&]() { return client.handshakeOk; }, 3000);
    TestAssert(client.handshakeOk, "Client connected and handshook");

    // Wait for time sync packets (server sends these periodically)
    bool gotTimeSync = client.PollUntil([&]() {
        return client.timeSyncsReceived > 0;
    }, 5000);
    TestAssert(gotTimeSync, "Received at least one TimeSync packet");
    printf("    TimeSyncs received: %d\n", client.timeSyncsReceived);

    client.Disconnect();
    client.Destroy();
}

static void Test_MultipleEntitiesPerPlayer() {
    printf("\n=== Test: Multiple Entities Per Player ===\n");

    TestClient client1, client2;
    client1.Init("Alice");
    client2.Init("Bob");

    client1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    client2.Connect("127.0.0.1", KMP_DEFAULT_PORT);

    client1.PollUntil([&]() { return client1.connected; }, 3000);
    client2.PollUntil([&]() { return client2.connected; }, 3000);

    client1.SendHandshake();
    client1.PollUntil([&]() { return client1.handshakeOk; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client2.SendHandshake();
    client2.PollUntil([&]() { return client2.handshakeOk; }, 3000);

    // Drain
    client1.Poll(200);
    client2.Poll(200);

    // Client 1 spawns 3 entities (simulating a squad)
    size_t c2SpawnsBefore = client2.entitiesSpawned.size();

    for (int i = 0; i < 3; i++) {
        PacketWriter w;
        w.WriteHeader(MessageType::C2S_EntitySpawnReq);
        w.WriteU32(static_cast<uint32_t>(i + 1));  // client entity ID
        w.WriteU8(static_cast<uint8_t>(EntityType::PlayerCharacter));
        w.WriteU32(client1.playerId);
        w.WriteU32(0);
        w.WriteF32(-51200.f + i * 10.f);
        w.WriteF32(1600.f);
        w.WriteF32(2700.f + i * 10.f);
        w.WriteU32(Quat().Compress());
        w.WriteU32(0);
        std::string name = "Squad_" + std::to_string(i);
        w.WriteU16(static_cast<uint16_t>(name.size()));
        w.WriteRaw(name.data(), name.size());
        client1.SendReliable(w.Data(), w.Size());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Wait for all to arrive
    client1.Poll(1000);
    client2.Poll(1000);

    size_t c1Entities = client1.entitiesSpawned.size();
    TestAssert(c1Entities >= 3, "Client 1 received confirmations for 3 entities");
    printf("    Client 1 total entities: %zu\n", c1Entities);

    size_t c2NewSpawns = client2.entitiesSpawned.size() - c2SpawnsBefore;
    TestAssert(c2NewSpawns >= 3, "Client 2 received 3 entity spawns from Client 1");
    printf("    Client 2 new entities from Client 1: %zu\n", c2NewSpawns);

    client1.Disconnect();
    client2.Disconnect();
    client1.Destroy();
    client2.Destroy();
}

// ─────────────────────────────────────────────────
//  New System Tests
// ─────────────────────────────────────────────────

// Cleanup helper: properly disconnect and destroy both clients
static void CleanupTwoClients(TestClient& c1, TestClient& c2) {
    c1.Disconnect(); c2.Disconnect();
    c1.Destroy(); c2.Destroy();
    // Give server time to fully process both disconnects
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// Helper: connect two clients, handshake, spawn entities, return ready state
static bool SetupTwoClients(TestClient& c1, TestClient& c2,
                            const char* name1 = "Alice", const char* name2 = "Bob") {
    c1.Init(name1);
    c2.Init(name2);
    c1.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    c2.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    c1.PollUntil([&]() { return c1.connected; }, 3000);
    c2.PollUntil([&]() { return c2.connected; }, 3000);
    if (!c1.connected || !c2.connected) {
        printf("    SetupTwoClients: connection failed (c1=%d, c2=%d)\n",
               c1.connected, c2.connected);
        return false;
    }

    c1.SendHandshake();
    c1.PollUntil([&]() { return c1.handshakeOk || c1.wasRejected; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    c2.SendHandshake();
    c2.PollUntil([&]() { return c2.handshakeOk || c2.wasRejected; }, 3000);
    if (!c1.handshakeOk || !c2.handshakeOk) {
        printf("    SetupTwoClients: handshake failed (c1=%d/%d, c2=%d/%d)\n",
               c1.handshakeOk, c1.wasRejected, c2.handshakeOk, c2.wasRejected);
        return false;
    }
    c1.Poll(200); c2.Poll(200);

    // Spawn entities for both
    c1.SendEntitySpawn(-51200.f, 1600.f, 2700.f);
    c1.PollUntil([&]() { return c1.myEntityId != 0; }, 3000);
    c2.SendEntitySpawn(-51180.f, 1600.f, 2720.f);
    c2.PollUntil([&]() { return c2.myEntityId != 0; }, 3000);
    c1.Poll(300); c2.Poll(300);

    if (c1.myEntityId == 0 || c2.myEntityId == 0) {
        printf("    SetupTwoClients: entity spawn failed (c1=%u, c2=%u)\n",
               c1.myEntityId, c2.myEntityId);
        return false;
    }

    return true;
}

static void Test_InventorySync() {
    printf("\n=== Test: Inventory Sync ===\n");

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2);
    TestAssert(ready, "Both clients connected with entities");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    // Client 1 picks up an item
    size_t c2InvBefore = c2.inventoryUpdates.size();
    c1.SendItemPickup(c1.myEntityId, 1001, 3); // item template 1001, qty 3

    // Client 2 should receive inventory update
    bool c2GotInvUpdate = c2.PollUntil([&]() {
        return c2.inventoryUpdates.size() > c2InvBefore;
    }, 3000);
    // Also poll c1
    c1.Poll(200);

    TestAssert(c2GotInvUpdate, "Client 2 received inventory update from Client 1");
    if (c2GotInvUpdate) {
        auto& inv = c2.inventoryUpdates.back();
        TestAssert(inv.action == 0, "Inventory action is 'add' (0)");
        TestAssert(inv.itemTemplateId == 1001, "Item template ID matches");
        TestAssert(inv.quantity == 3, "Item quantity matches");
        printf("    Received: entity=%u item=%u qty=%d action=%u\n",
               inv.entityId, inv.itemTemplateId, inv.quantity, inv.action);
    }

    // Client 1 drops an item
    size_t c2InvBefore2 = c2.inventoryUpdates.size();
    c1.SendItemDrop(c1.myEntityId, 1001, -51200.f, 1600.f, 2700.f);

    bool c2GotDrop = c2.PollUntil([&]() {
        return c2.inventoryUpdates.size() > c2InvBefore2;
    }, 3000);
    c1.Poll(200);

    TestAssert(c2GotDrop, "Client 2 received item drop update");
    if (c2GotDrop) {
        auto& inv = c2.inventoryUpdates.back();
        TestAssert(inv.action == 1, "Inventory action is 'remove' (1)");
    }

    CleanupTwoClients(c1, c2);
}

static void Test_TradeSync() {
    printf("\n=== Test: Trade Sync ===\n");

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2);
    TestAssert(ready, "Both clients connected with entities");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    // Client 1 sends a trade request
    c1.SendTradeRequest(c1.myEntityId, 2001, 1, 500);

    // Both should receive trade result (server broadcasts)
    bool c1GotResult = c1.PollUntil([&]() {
        return !c1.tradeResults.empty();
    }, 3000);
    c2.Poll(500);

    TestAssert(c1GotResult, "Client 1 received trade result");
    if (c1GotResult) {
        auto& tr = c1.tradeResults.back();
        TestAssert(tr.success == 1, "Trade was accepted");
        TestAssert(tr.itemTemplateId == 2001, "Trade item ID matches");
        printf("    Trade result: buyer=%u item=%u qty=%d success=%u\n",
               tr.buyerEntityId, tr.itemTemplateId, tr.quantity, tr.success);
    }

    CleanupTwoClients(c1, c2);
}

static void Test_SquadSync() {
    printf("\n=== Test: Squad Sync ===\n");

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2);
    TestAssert(ready, "Both clients connected with entities");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    // Client 1 creates a squad
    size_t c2SquadsBefore = c2.squadsCreated.size();
    c1.SendSquadCreate(c1.myEntityId, "Alpha Squad");

    // Both clients should receive squad created broadcast
    bool c1GotSquad = c1.PollUntil([&]() {
        return !c1.squadsCreated.empty();
    }, 3000);

    bool c2GotSquad = c2.PollUntil([&]() {
        return c2.squadsCreated.size() > c2SquadsBefore;
    }, 3000);

    TestAssert(c1GotSquad, "Client 1 received squad creation confirmation");
    TestAssert(c2GotSquad, "Client 2 received squad creation broadcast");

    if (c1GotSquad) {
        uint32_t squadId = c1.squadsCreated.back();
        TestAssert(squadId > 0, "Squad has valid net ID");
        printf("    Squad net ID: %u\n", squadId);
    }

    CleanupTwoClients(c1, c2);
}

static void Test_FactionRelationSync() {
    printf("\n=== Test: Faction Relation Sync ===\n");

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2);
    TestAssert(ready, "Both clients connected with entities");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    // Client 1 changes a faction relation
    size_t c2FactionBefore = c2.factionRelations.size();
    c1.SendFactionRelation(100, 200, -50.0f); // Faction 100 vs 200, hostile

    // Both should receive the relation change (server broadcasts to ALL)
    bool c1GotRelation = c1.PollUntil([&]() {
        return !c1.factionRelations.empty();
    }, 3000);
    bool c2GotRelation = c2.PollUntil([&]() {
        return c2.factionRelations.size() > c2FactionBefore;
    }, 3000);

    TestAssert(c1GotRelation, "Client 1 received faction relation confirmation");
    TestAssert(c2GotRelation, "Client 2 received faction relation broadcast");

    if (c2GotRelation) {
        auto& fr = c2.factionRelations.back();
        TestAssert(fr.factionIdA == 100, "Faction A ID matches");
        TestAssert(fr.factionIdB == 200, "Faction B ID matches");
        TestAssert(fr.relation == -50.0f, "Relation value matches");
        printf("    Faction %u <-> %u = %.1f\n", fr.factionIdA, fr.factionIdB, fr.relation);
    }

    CleanupTwoClients(c1, c2);
}

static void Test_BuildingSync() {
    printf("\n=== Test: Building Placement & Dismantle ===\n");

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2);
    TestAssert(ready, "Both clients connected with entities");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    // Client 1 places a building
    size_t c2BuildsBefore = c2.buildingsPlaced.size();
    c1.SendBuildRequest(5001, -51200.f, 1600.f, 2710.f);

    // Both should receive building placed
    bool c1GotBuild = c1.PollUntil([&]() {
        return !c1.buildingsPlaced.empty();
    }, 3000);
    bool c2GotBuild = c2.PollUntil([&]() {
        return c2.buildingsPlaced.size() > c2BuildsBefore;
    }, 3000);

    TestAssert(c1GotBuild, "Client 1 received building placement confirmation");
    TestAssert(c2GotBuild, "Client 2 received building placement broadcast");

    EntityID buildingId = 0;
    if (c1GotBuild) {
        auto& bp = c1.buildingsPlaced.back();
        buildingId = bp.entityId;
        TestAssert(bp.templateId == 5001, "Building template ID matches");
        TestAssert(bp.builderId == c1.playerId, "Builder ID matches Client 1");
        printf("    Building ID: %u, template: %u, builder: %u\n",
               bp.entityId, bp.templateId, bp.builderId);
    }

    if (buildingId == 0) {
        CleanupTwoClients(c1, c2);
        return;
    }

    // Client 1 dismantles the building
    size_t c2DestroyBefore = c2.buildingsDestroyed.size();
    c1.SendBuildDismantle(buildingId);

    bool c2GotDestroy = c2.PollUntil([&]() {
        return c2.buildingsDestroyed.size() > c2DestroyBefore;
    }, 3000);
    c1.Poll(500);

    TestAssert(c2GotDestroy, "Client 2 received building destruction");
    if (c2GotDestroy) {
        bool found = false;
        for (auto id : c2.buildingsDestroyed) {
            if (id == buildingId) { found = true; break; }
        }
        TestAssert(found, "Destroyed building ID matches placed building");
    }

    CleanupTwoClients(c1, c2);
}

static void Test_ServerBrowser() {
    printf("\n=== Test: Server Browser Query ===\n");

    TestClient client;
    TestAssert(client.Init("BrowserQuery"), "Client init for server query");

    client.Connect("127.0.0.1", KMP_DEFAULT_PORT);
    bool enetConnected = client.PollUntil([&]() { return client.connected; }, 3000);
    TestAssert(enetConnected, "ENet connection established for query");

    if (!enetConnected) {
        client.Disconnect();
        client.Destroy();
        return;
    }

    // Send server query (no handshake needed)
    PacketWriter w;
    w.WriteHeader(MessageType::C2S_ServerQuery);
    MsgServerQuery query{};
    query.protocolVersion = KMP_PROTOCOL_VERSION;
    w.WriteRaw(&query, sizeof(query));
    client.SendReliable(w.Data(), w.Size());

    // We should receive S2C_ServerInfo - track it manually since TestClient
    // doesn't have a handler for it. We'll just check we get a packet back.
    bool gotResponse = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
        ENetEvent event;
        if (enet_host_service(client.host, &event, 50) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                if (event.packet->dataLength >= sizeof(PacketHeader)) {
                    PacketReader r(event.packet->data, event.packet->dataLength);
                    PacketHeader hdr;
                    r.ReadHeader(hdr);
                    if (hdr.type == MessageType::S2C_ServerInfo) {
                        MsgServerInfo info;
                        if (r.ReadRaw(&info, sizeof(info))) {
                            gotResponse = true;
                            printf("    Server: '%s' (%u/%u players) port=%u pvp=%u\n",
                                   info.serverName, info.currentPlayers,
                                   info.maxPlayers, info.port, info.pvpEnabled);
                            TestAssert(info.protocolVersion == KMP_PROTOCOL_VERSION,
                                       "Server protocol version matches");
                            TestAssert(info.maxPlayers > 0, "Server reports max players > 0");
                        }
                    }
                }
                enet_packet_destroy(event.packet);
                if (gotResponse) break;
            }
        }
    }
    TestAssert(gotResponse, "Received S2C_ServerInfo response");

    client.Disconnect();
    client.Destroy();
}

static void Test_FullMultiplayerSession() {
    printf("\n=== Test: Full Multiplayer Session (End-to-End) ===\n");

    // This test simulates a complete multiplayer session:
    // 1. Two players connect and spawn
    // 2. They exchange position updates (can see each other)
    // 3. Player 1 places a building (visible to player 2)
    // 4. Player 1 picks up an item (synced to player 2)
    // 5. They chat with each other
    // 6. Player 2 disconnects cleanly

    TestClient c1, c2;
    bool ready = SetupTwoClients(c1, c2, "Host", "Joiner");
    TestAssert(ready, "Full session: both players connected and spawned");
    if (!ready) { CleanupTwoClients(c1, c2); return; }

    printf("    Host entity: %u, Joiner entity: %u\n", c1.myEntityId, c2.myEntityId);

    // Step 1: Position updates (can they see each other?)
    int c2PosBefore = c2.posUpdatesReceived;
    for (int i = 0; i < 3; i++) {
        c1.SendPositionUpdate(c1.myEntityId,
            -51200.f + i * 5.f, 1600.f, 2700.f);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        c2.Poll(50);
    }
    c2.Poll(500);
    bool seesEachOther = (c2.posUpdatesReceived > c2PosBefore);
    TestAssert(seesEachOther, "Full session: players can see each other's movement");

    // Step 2: Building placement
    c1.SendBuildRequest(9001, -51200.f, 1600.f, 2705.f);
    bool c2SawBuild = c2.PollUntil([&]() {
        return !c2.buildingsPlaced.empty();
    }, 3000);
    c1.Poll(200);
    TestAssert(c2SawBuild, "Full session: building placement synced");

    // Step 3: Inventory sync
    c1.SendItemPickup(c1.myEntityId, 3001, 5);
    bool c2SawInv = c2.PollUntil([&]() {
        return !c2.inventoryUpdates.empty();
    }, 3000);
    c1.Poll(200);
    TestAssert(c2SawInv, "Full session: inventory sync works");

    // Step 4: Chat
    c1.chatMessages.clear();
    c2.chatMessages.clear();
    c1.SendChat("Can you see me?");
    bool chatWorks = false;
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            c1.Poll(25); c2.Poll(25);
            if (!c2.chatMessages.empty()) { chatWorks = true; break; }
        }
    }
    TestAssert(chatWorks, "Full session: chat relay works");

    // Step 5: Clean disconnect
    PlayerID joinerId = c2.playerId;
    c2.Disconnect();
    c2.Destroy();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    c1.Poll(2000);

    bool gotLeave = false;
    for (auto& pl : c1.playersLeft) {
        if (pl.playerId == joinerId) { gotLeave = true; break; }
    }
    TestAssert(gotLeave, "Full session: disconnect cleanup notified");

    c1.Disconnect();
    c1.Destroy();
}

// ─────────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────────

int main(int argc, char** argv) {
    printf("======================================\n");
    printf("  KenshiMP Integration Test Suite\n");
    printf("======================================\n\n");

    // Find server executable
    std::string serverExe;
    if (argc >= 2) {
        serverExe = argv[1];
    } else {
        serverExe = FindServerExe();
    }

    if (serverExe.empty()) {
        printf("ERROR: Could not find KenshiMP.Server.exe\n");
        printf("Usage: KenshiMP.IntegrationTest.exe [path/to/KenshiMP.Server.exe]\n");
        printf("Searched: current dir, parent dir, Kenshi dir\n");
        return 1;
    }
    printf("[*] Using server: %s\n", serverExe.c_str());

    // Init ENet
    if (enet_initialize() != 0) {
        printf("ERROR: Failed to initialize ENet\n");
        return 1;
    }

    // Start server
#ifdef _WIN32
    if (!StartServer(serverExe.c_str())) {
        enet_deinitialize();
        return 1;
    }
#endif

    // Server blocks on UPnP discovery (up to 3 retries × 1s each) + world load
    // before entering its main loop. Total startup can be 5-10+ seconds.
    printf("[*] Waiting for server to start (UPnP discovery may take a few seconds)...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // Warm-up: keep trying to connect until server is ready
    {
        printf("[*] Probing server...\n");
        bool ready = false;
        for (int attempt = 0; attempt < 5 && !ready; attempt++) {
            TestClient probe;
            probe.Init("Probe");
            probe.Connect("127.0.0.1", KMP_DEFAULT_PORT);
            ready = probe.PollUntil([&]() { return probe.connected; }, 3000);
            if (ready) {
                probe.SendHandshake();
                probe.PollUntil([&]() { return probe.handshakeOk; }, 3000);
                ready = probe.handshakeOk;
            }
            probe.Disconnect();
            probe.Destroy();
            if (!ready) {
                printf("[*] Attempt %d: not ready yet, retrying...\n", attempt + 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
        if (!ready) {
            printf("ERROR: Server did not accept connections after ~17s\n");
            StopServer();
            enet_deinitialize();
            return 1;
        }
        printf("[*] Server is ready!\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // ── Run Tests ──

    Test_ServerConnection();
    // Small pause between tests to let server clean up
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_TwoPlayersConnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_EntitySpawnAndBroadcast();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_PositionSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_ChatRelay();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_DisconnectCleanup();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_TimeSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_MultipleEntitiesPerPlayer();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_InventorySync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_TradeSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_SquadSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_FactionRelationSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_BuildingSync();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_ServerBrowser();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    Test_FullMultiplayerSession();

    // ── Results ──
    printf("\n======================================\n");
    printf("  Results: %d passed, %d failed\n",
           g_testsPassed, g_testsFailed);
    printf("======================================\n");

    // Stop server
#ifdef _WIN32
    StopServer();
#endif

    enet_deinitialize();

    if (g_testsFailed > 0) {
        printf("\nSome tests FAILED. Check output above.\n");
    } else {
        printf("\nAll tests PASSED!\n");
    }

    printf("\nPress Enter to exit...\n");
    getchar();

    return g_testsFailed > 0 ? 1 : 0;
}

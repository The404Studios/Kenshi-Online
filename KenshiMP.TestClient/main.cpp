// KenshiMP Test Client
// Standalone console app that connects to the server as a fake player.
// Sends position updates (walks in a circle), receives broadcasts, prints everything.
// Use this to test multiplayer without a second Kenshi instance.

#include <kmp/protocol.h>
#include <kmp/messages.h>
#include <kmp/constants.h>
#include <kmp/types.h>
#include <enet/enet.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>
#endif

using namespace kmp;

// ── State ──
static ENetHost*   g_client = nullptr;
static ENetPeer*   g_peer   = nullptr;
static PlayerID    g_myId   = 0;
static bool        g_connected = false;
static uint32_t    g_myEntityId = 0;
static std::string g_playerName; // Set in main()

// Position: start near Kenshi's "The Hub" area
static float g_posX = -51200.f;
static float g_posY = 1600.f;
static float g_posZ = 2700.f;
static float g_angle = 0.f;
static float g_walkRadius = 50.f;
static float g_originX, g_originZ;

// Host tracking: spawn near the first entity we see from another player
static bool g_hostPosKnown = false;
static float g_hostPosX = 0.f, g_hostPosY = 0.f, g_hostPosZ = 0.f;
static bool g_needsSpawn = false;  // true after handshake, before we spawn

// Stats
static int g_packetsReceived = 0;
static int g_posUpdatesReceived = 0;
static int g_entitiesTracked = 0;

// ── Helpers ──

static void SendReliable(const uint8_t* data, size_t len) {
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(g_peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
}

static void SendUnreliable(const uint8_t* data, size_t len) {
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(g_peer, KMP_CHANNEL_UNRELIABLE_SEQ, pkt);
}

static void SendHandshake() {
    PacketWriter w;
    w.WriteHeader(MessageType::C2S_Handshake);
    MsgHandshake hs{};
    hs.protocolVersion = KMP_PROTOCOL_VERSION;
    strncpy(hs.playerName, g_playerName.c_str(), KMP_MAX_NAME_LENGTH);
    hs.playerName[KMP_MAX_NAME_LENGTH] = '\0';
    hs.gameVersionMajor = 1;
    hs.gameVersionMinor = 0;
    hs.gameVersionPatch = 68;
    w.WriteRaw(&hs, sizeof(hs));
    SendReliable(w.Data(), w.Size());
    printf("[>] Sent handshake as '%s'\n", g_playerName.c_str());
}

static void SendEntitySpawn() {
    PacketWriter w;
    w.WriteHeader(MessageType::C2S_EntitySpawnReq);
    w.WriteU32(1);  // client entity ID
    w.WriteU8(static_cast<uint8_t>(EntityType::PlayerCharacter));
    w.WriteU32(g_myId);
    w.WriteU32(0);  // template ID
    w.WriteF32(g_posX);
    w.WriteF32(g_posY);
    w.WriteF32(g_posZ);
    w.WriteU32(Quat().Compress());  // identity rotation
    w.WriteU32(0);  // faction
    std::string name = "Greenlander";
    w.WriteU16(static_cast<uint16_t>(name.size()));
    w.WriteRaw(name.data(), name.size());
    SendReliable(w.Data(), w.Size());
    printf("[>] Spawned entity at (%.0f, %.0f, %.0f)\n", g_posX, g_posY, g_posZ);
}

static float g_walkProgress = 0.f;   // 0..1 along the patrol path
static float g_walkDirection = 1.f;  // +1 forward, -1 backward
static float g_walkSpeed = 0.005f;   // Speed of patrol progress per tick

static void SendPositionUpdate() {
    if (g_myEntityId == 0) return;

    // Walk back and forth along a line (like a player clicking patrol points)
    g_walkProgress += g_walkSpeed * g_walkDirection;
    if (g_walkProgress >= 1.f) {
        g_walkProgress = 1.f;
        g_walkDirection = -1.f;  // turn around
    } else if (g_walkProgress <= 0.f) {
        g_walkProgress = 0.f;
        g_walkDirection = 1.f;   // turn around
    }

    // Patrol between two points ~100 units apart
    float startX = g_originX;
    float startZ = g_originZ;
    float endX = g_originX + 100.f;
    float endZ = g_originZ + 50.f;

    g_posX = startX + (endX - startX) * g_walkProgress;
    g_posZ = startZ + (endZ - startZ) * g_walkProgress;

    // Build rotation quaternion facing the walk direction
    float facing = (g_walkDirection > 0) ? atan2f(endZ - startZ, endX - startX)
                                          : atan2f(startZ - endZ, startX - endX);
    // Simple Y-axis rotation quat: (0, sin(a/2), 0, cos(a/2))
    Quat rot;
    rot.x = 0.f;
    rot.y = sinf(facing * 0.5f);
    rot.z = 0.f;
    rot.w = cosf(facing * 0.5f);

    PacketWriter w;
    w.WriteHeader(MessageType::C2S_PositionUpdate);
    w.WriteU8(1);  // 1 character

    CharacterPosition cp{};
    cp.entityId = g_myEntityId;
    cp.posX = g_posX;
    cp.posY = g_posY;
    cp.posZ = g_posZ;
    cp.compressedQuat = rot.Compress();
    cp.animStateId = 1;  // walking
    cp.moveSpeed = 85;   // ~5 m/s
    cp.flags = 0x01;     // moving
    w.WriteRaw(&cp, sizeof(cp));

    SendUnreliable(w.Data(), w.Size());
}

static void SendChat(const std::string& msg) {
    PacketWriter w;
    w.WriteHeader(MessageType::C2S_ChatMessage);
    w.WriteU32(g_myId);
    w.WriteString(msg);
    SendReliable(w.Data(), w.Size());
    printf("[>] Chat: %s\n", msg.c_str());
}

// ── Packet Handlers ──

static void HandlePacket(const uint8_t* data, size_t len) {
    if (len < sizeof(PacketHeader)) return;
    g_packetsReceived++;

    PacketReader r(data, len);
    PacketHeader hdr;
    r.ReadHeader(hdr);

    switch (hdr.type) {
    case MessageType::S2C_HandshakeAck: {
        MsgHandshakeAck ack;
        if (r.ReadRaw(&ack, sizeof(ack))) {
            g_myId = ack.playerId;
            g_connected = true;
            g_needsSpawn = true;  // Wait for host position before spawning
            printf("[<] Handshake OK! Player ID: %u, Players: %u/%u, Time: %.2f\n",
                   ack.playerId, ack.currentPlayers, ack.maxPlayers, ack.timeOfDay);
            printf("[*] Waiting for host entity position before spawning...\n");
        }
        break;
    }
    case MessageType::S2C_HandshakeReject: {
        MsgHandshakeReject rej;
        if (r.ReadRaw(&rej, sizeof(rej))) {
            printf("[<] REJECTED: code=%u reason='%s'\n", rej.reasonCode, rej.reasonText);
        }
        break;
    }
    case MessageType::S2C_PlayerJoined: {
        MsgPlayerJoined pj;
        if (r.ReadRaw(&pj, sizeof(pj))) {
            printf("[<] Player joined: '%s' (ID: %u)\n", pj.playerName, pj.playerId);
        }
        break;
    }
    case MessageType::S2C_PlayerLeft: {
        MsgPlayerLeft pl;
        if (r.ReadRaw(&pl, sizeof(pl))) {
            printf("[<] Player left: ID=%u reason=%u\n", pl.playerId, pl.reason);
        }
        break;
    }
    case MessageType::S2C_EntitySpawn: {
        // Read fixed part
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

        g_entitiesTracked++;

        // If this is OUR entity (owner matches), save the server-assigned ID
        if (ownerId == g_myId && g_myEntityId == 0) {
            g_myEntityId = entId;
            printf("[<] ** MY ENTITY ** serverID=%u at (%.0f,%.0f,%.0f) template='%s'\n",
                   entId, px, py, pz, tmplName.c_str());
        } else {
            // Track the first entity from another player as "host position"
            if (!g_hostPosKnown && ownerId != g_myId &&
                (px != 0.f || py != 0.f || pz != 0.f)) {
                g_hostPosKnown = true;
                g_hostPosX = px;
                g_hostPosY = py;
                g_hostPosZ = pz;
                printf("[*] HOST POSITION FOUND: (%.0f, %.0f, %.0f) from entity %u\n",
                       px, py, pz, entId);

                // Now spawn near the host
                if (g_needsSpawn) {
                    // Position ourselves 10 units away from host
                    g_posX = g_hostPosX + 10.f;
                    g_posY = g_hostPosY;
                    g_posZ = g_hostPosZ + 10.f;
                    g_originX = g_posX;
                    g_originZ = g_posZ;
                    SendEntitySpawn();
                    g_needsSpawn = false;
                }
            }

            // Only print first 10 and every 50th after
            if (g_entitiesTracked <= 10 || g_entitiesTracked % 50 == 0) {
                printf("[<] Entity spawn: id=%u type=%u owner=%u at (%.0f,%.0f,%.0f) '%s' [total: %d]\n",
                       entId, type, ownerId, px, py, pz, tmplName.c_str(), g_entitiesTracked);
            }
        }
        break;
    }
    case MessageType::S2C_EntityDespawn: {
        MsgEntityDespawn ds;
        if (r.ReadRaw(&ds, sizeof(ds))) {
            printf("[<] Entity despawn: id=%u reason=%u\n", ds.entityId, ds.reason);
        }
        break;
    }
    case MessageType::S2C_PositionUpdate: {
        uint32_t sourcePlayer;
        uint8_t count;
        r.ReadU32(sourcePlayer);
        r.ReadU8(count);
        g_posUpdatesReceived++;

        // Print periodically
        if (g_posUpdatesReceived <= 3 || g_posUpdatesReceived % 100 == 0) {
            printf("[<] Position update from player %u: %u characters [total recv: %d]\n",
                   sourcePlayer, count, g_posUpdatesReceived);
            // Print first character's position
            if (count > 0) {
                CharacterPosition cp;
                if (r.ReadRaw(&cp, sizeof(cp))) {
                    printf("     entity=%u pos=(%.1f, %.1f, %.1f) speed=%u anim=%u\n",
                           cp.entityId, cp.posX, cp.posY, cp.posZ, cp.moveSpeed, cp.animStateId);
                }
            }
        }
        break;
    }
    case MessageType::S2C_TimeSync: {
        MsgTimeSync ts;
        if (r.ReadRaw(&ts, sizeof(ts))) {
            // Print every 10th
            static int timeSyncCount = 0;
            timeSyncCount++;
            if (timeSyncCount <= 2 || timeSyncCount % 10 == 0) {
                printf("[<] TimeSync: tick=%u tod=%.2f speed=%u\n",
                       ts.serverTick, ts.timeOfDay, ts.gameSpeed);
            }
        }
        break;
    }
    case MessageType::S2C_ChatMessage: {
        uint32_t senderId;
        r.ReadU32(senderId);
        std::string msg;
        r.ReadString(msg);
        printf("[CHAT] Player %u: %s\n", senderId, msg.c_str());
        break;
    }
    case MessageType::S2C_SystemMessage: {
        uint32_t senderId;
        r.ReadU32(senderId);
        std::string msg;
        r.ReadString(msg);
        printf("[SYSTEM] %s\n", msg.c_str());
        break;
    }
    case MessageType::S2C_CombatHit: {
        MsgCombatHit hit;
        if (r.ReadRaw(&hit, sizeof(hit))) {
            printf("[<] Combat hit: %u -> %u, part=%u, cut=%.1f blunt=%.1f hp=%.1f\n",
                   hit.attackerId, hit.targetId, hit.bodyPart,
                   hit.cutDamage, hit.bluntDamage, hit.resultHealth);
        }
        break;
    }
    case MessageType::S2C_CombatDeath: {
        MsgCombatDeath death;
        if (r.ReadRaw(&death, sizeof(death))) {
            printf("[<] Death: entity=%u killed by=%u\n", death.entityId, death.killerId);
        }
        break;
    }
    case MessageType::S2C_HealthUpdate: {
        MsgHealthUpdate hu;
        if (r.ReadRaw(&hu, sizeof(hu))) {
            printf("[<] Health update: entity=%u chest=%.0f head=%.0f blood=%.0f\n",
                   hu.entityId, hu.health[1], hu.health[0], hu.bloodLevel);
        }
        break;
    }
    case MessageType::S2C_EquipmentUpdate: {
        MsgEquipmentUpdate eq;
        if (r.ReadRaw(&eq, sizeof(eq))) {
            // Quiet - too many
        }
        break;
    }
    case MessageType::S2C_InventoryUpdate: {
        MsgInventoryUpdate inv;
        if (r.ReadRaw(&inv, sizeof(inv))) {
            printf("[<] Inventory update: entity=%u action=%u item=%u qty=%d\n",
                   inv.entityId, inv.action, inv.itemTemplateId, inv.quantity);
        }
        break;
    }
    case MessageType::S2C_TradeResult: {
        MsgTradeResult tr;
        if (r.ReadRaw(&tr, sizeof(tr))) {
            printf("[<] Trade result: buyer=%u item=%u qty=%d success=%u\n",
                   tr.buyerEntityId, tr.itemTemplateId, tr.quantity, tr.success);
        }
        break;
    }
    case MessageType::S2C_SquadCreated: {
        uint32_t creator, squadId;
        r.ReadU32(creator);
        r.ReadU32(squadId);
        std::string name;
        r.ReadString(name);
        printf("[<] Squad created: '%s' id=%u by entity=%u\n", name.c_str(), squadId, creator);
        break;
    }
    case MessageType::S2C_SquadMemberUpdate: {
        MsgSquadMemberUpdate smu;
        if (r.ReadRaw(&smu, sizeof(smu))) {
            printf("[<] Squad member: squad=%u entity=%u action=%u\n",
                   smu.squadNetId, smu.memberEntityId, smu.action);
        }
        break;
    }
    case MessageType::S2C_FactionRelation: {
        MsgFactionRelation fr;
        if (r.ReadRaw(&fr, sizeof(fr))) {
            printf("[<] Faction relation: %u <-> %u = %.1f\n",
                   fr.factionIdA, fr.factionIdB, fr.relation);
        }
        break;
    }
    case MessageType::S2C_BuildPlaced: {
        MsgBuildPlaced bp;
        if (r.ReadRaw(&bp, sizeof(bp))) {
            printf("[<] Build placed: id=%u template=%u at (%.0f,%.0f,%.0f) by player %u\n",
                   bp.entityId, bp.templateId, bp.posX, bp.posY, bp.posZ, bp.builderId);
        }
        break;
    }
    case MessageType::S2C_BuildDestroyed: {
        uint32_t buildingId;
        uint8_t reason;
        r.ReadU32(buildingId);
        r.ReadU8(reason);
        printf("[<] Build destroyed: id=%u reason=%u\n", buildingId, reason);
        break;
    }
    case MessageType::S2C_BuildProgress: {
        MsgBuildProgress prog;
        if (r.ReadRaw(&prog, sizeof(prog))) {
            printf("[<] Build progress: id=%u progress=%.2f\n", prog.entityId, prog.progress);
        }
        break;
    }
    default:
        // Unknown packet type
        break;
    }
}

// ── Main ──

static void PrintHelp() {
    printf("\n=== KenshiMP Test Client ===\n");
    printf("Commands:\n");
    printf("  c <msg>  - Send chat message\n");
    printf("  s        - Print status\n");
    printf("  q        - Quit\n");
    printf("  h        - This help\n\n");
}

int main(int argc, char** argv) {
    // Parse args
    std::string serverAddr = "127.0.0.1";
    uint16_t serverPort = KMP_DEFAULT_PORT;

    if (argc >= 2) serverAddr = argv[1];
    if (argc >= 3) serverPort = static_cast<uint16_t>(atoi(argv[2]));
    if (argc >= 4) g_playerName = argv[3];

    // Generate a unique name if none provided
    if (g_playerName.empty()) {
        srand(static_cast<unsigned>(time(nullptr)));
        static const char* names[] = {
            "Beep", "Cat", "Hobbs", "Ruka", "Sadneil", "Hamut",
            "Shryke", "Burn", "Rane", "Seto", "Griffin", "Moll"
        };
        int idx = rand() % (sizeof(names) / sizeof(names[0]));
        g_playerName = std::string(names[idx]) + "_" + std::to_string(rand() % 999);
    }

    printf("=== KenshiMP Test Client ===\n");
    printf("Connecting to %s:%u as '%s'\n\n", serverAddr.c_str(), serverPort, g_playerName.c_str());

    // Save origin for circular walk
    g_originX = g_posX;
    g_originZ = g_posZ;

    // Init ENet
    if (enet_initialize() != 0) {
        printf("ERROR: Failed to initialize ENet\n");
        return 1;
    }

    g_client = enet_host_create(nullptr, 1, KMP_CHANNEL_COUNT,
                                KMP_DOWNSTREAM_LIMIT, KMP_UPSTREAM_LIMIT);
    if (!g_client) {
        printf("ERROR: Failed to create ENet client\n");
        enet_deinitialize();
        return 1;
    }

    // Connect
    ENetAddress addr;
    enet_address_set_host(&addr, serverAddr.c_str());
    addr.port = serverPort;

    g_peer = enet_host_connect(g_client, &addr, KMP_CHANNEL_COUNT, 0);
    if (!g_peer) {
        printf("ERROR: Failed to create ENet peer\n");
        enet_host_destroy(g_client);
        enet_deinitialize();
        return 1;
    }

    printf("Connecting...\n");

    // Main loop
    auto lastPosSend = std::chrono::steady_clock::now();
    auto startTime = std::chrono::steady_clock::now();
    bool running = true;

    PrintHelp();

    while (running) {
        // Process ENet events
        ENetEvent event;
        while (enet_host_service(g_client, &event, 5) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                printf("[*] Connected to server!\n");
                SendHandshake();
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                HandlePacket(event.packet->data, event.packet->dataLength);
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                printf("[*] Disconnected from server\n");
                g_connected = false;
                running = false;
                break;

            default:
                break;
            }
        }

        // If we're waiting to spawn and haven't found host position in 5 seconds, spawn at default
        if (g_connected && g_needsSpawn) {
            static auto spawnWaitStart = std::chrono::steady_clock::now();
            auto waitElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - spawnWaitStart);
            if (waitElapsed.count() >= 5) {
                printf("[*] No host position received after 5s, spawning at default position\n");
                g_originX = g_posX;
                g_originZ = g_posZ;
                SendEntitySpawn();
                g_needsSpawn = false;
            }
        }

        // Send position updates at tick rate
        if (g_connected && g_myEntityId != 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPosSend);
            if (elapsed.count() >= KMP_TICK_INTERVAL_MS) {
                SendPositionUpdate();
                lastPosSend = now;
            }
        }

        // Check for keyboard input (non-blocking)
#ifdef _WIN32
        if (_kbhit()) {
            char line[256] = {};
            printf("> ");
            fflush(stdout);

            // Read a line
            if (fgets(line, sizeof(line), stdin)) {
                // Strip newline
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') line[len-1] = 0;

                if (line[0] == 'q') {
                    running = false;
                } else if (line[0] == 'h') {
                    PrintHelp();
                } else if (line[0] == 's') {
                    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - startTime);
                    printf("\n--- Status ---\n");
                    printf("Connected: %s\n", g_connected ? "YES" : "NO");
                    printf("Player ID: %u\n", g_myId);
                    printf("Entity ID: %u\n", g_myEntityId);
                    printf("Position: (%.1f, %.1f, %.1f)\n", g_posX, g_posY, g_posZ);
                    printf("Packets received: %d\n", g_packetsReceived);
                    printf("Position updates: %d\n", g_posUpdatesReceived);
                    printf("Entities tracked: %d\n", g_entitiesTracked);
                    printf("Uptime: %llds\n", (long long)uptime.count());
                    printf("--------------\n\n");
                } else if (line[0] == 'c' && line[1] == ' ') {
                    SendChat(std::string(line + 2));
                }
            }
        }
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    if (g_peer) enet_peer_disconnect(g_peer, 0);

    // Wait for disconnect
    ENetEvent event;
    while (enet_host_service(g_client, &event, 1000) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            break;
        }
    }

    enet_host_destroy(g_client);
    enet_deinitialize();

    printf("Test client exited.\n");
    return 0;
}

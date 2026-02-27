#include "server.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>

namespace kmp {

bool GameServer::Start(const ServerConfig& config) {
    m_config = config;

    if (enet_initialize() != 0) {
        spdlog::error("GameServer: ENet initialization failed");
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = config.port;

    m_host = enet_host_create(&address, config.maxPlayers, KMP_CHANNEL_COUNT,
                              0, 0); // No bandwidth limits on server
    if (!m_host) {
        spdlog::error("GameServer: Failed to create ENet host on port {}", config.port);
        enet_deinitialize();
        return false;
    }

    spdlog::info("GameServer: Listening on port {}", config.port);
    return true;
}

void GameServer::Stop() {
    // Disconnect all players
    for (auto& [id, player] : m_players) {
        if (player.peer) {
            enet_peer_disconnect(player.peer, 0);
        }
    }

    // Flush
    ENetEvent event;
    while (enet_host_service(m_host, &event, 1000) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
    }

    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
    enet_deinitialize();
    m_players.clear();
}

void GameServer::Update(float deltaTime) {
    std::lock_guard lock(m_mutex);
    m_serverTick++;
    m_uptime += deltaTime;

    // Pump ENet events
    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            HandleConnect(event.peer);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            HandlePacket(event.peer, event.packet->data, event.packet->dataLength,
                        event.channelID);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            HandleDisconnect(event.peer);
            break;
        default:
            break;
        }
    }

    // Update game time
    m_timeOfDay += deltaTime * m_config.gameSpeed / 86400.f; // 24h cycle
    if (m_timeOfDay >= 1.f) m_timeOfDay -= 1.f;

    // Broadcast positions every tick
    BroadcastPositions();

    // Time sync every 5 seconds
    m_timeSinceTimeSync += deltaTime;
    if (m_timeSinceTimeSync >= 5.0f) {
        BroadcastTimeSync();
        m_timeSinceTimeSync = 0.f;
    }

    // Update player pings
    for (auto& [id, player] : m_players) {
        if (player.peer) {
            player.ping = player.peer->roundTripTime;
        }
    }
}

void GameServer::HandleConnect(ENetPeer* peer) {
    char addrStr[64];
    enet_address_get_host_ip(&peer->address, addrStr, sizeof(addrStr));
    spdlog::info("GameServer: Incoming connection from {}:{}", addrStr, peer->address.port);

    if (m_players.size() >= static_cast<size_t>(m_config.maxPlayers)) {
        spdlog::warn("GameServer: Server full, rejecting connection");
        enet_peer_disconnect(peer, 0);
        return;
    }

    // Connection accepted, wait for handshake
    peer->data = nullptr;
}

void GameServer::HandleDisconnect(ENetPeer* peer) {
    ConnectedPlayer* player = GetPlayer(peer);
    if (player) {
        spdlog::info("GameServer: Player '{}' (ID: {}) disconnected", player->name, player->id);

        // Notify others
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_PlayerLeft);
        MsgPlayerLeft msg;
        msg.playerId = player->id;
        msg.reason = 0; // disconnect
        writer.WriteRaw(&msg, sizeof(msg));
        BroadcastExcept(player->id, writer.Data(), writer.Size(),
                       KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);

        // Broadcast system message
        BroadcastSystemMessage(player->name + " left the game");

        m_players.erase(player->id);
    }
    peer->data = nullptr;
}

void GameServer::HandlePacket(ENetPeer* peer, const uint8_t* data, size_t size, int channel) {
    if (size < sizeof(PacketHeader)) return;

    PacketReader reader(data, size);
    PacketHeader header;
    if (!reader.ReadHeader(header)) return;

    switch (header.type) {
    case MessageType::C2S_Handshake:
        HandleHandshake(peer, reader);
        break;
    case MessageType::C2S_PositionUpdate: {
        auto* player = GetPlayer(peer);
        if (player) HandlePositionUpdate(*player, reader);
        break;
    }
    case MessageType::C2S_MoveCommand: {
        auto* player = GetPlayer(peer);
        if (player) HandleMoveCommand(*player, reader);
        break;
    }
    case MessageType::C2S_AttackIntent: {
        auto* player = GetPlayer(peer);
        if (player) HandleAttackIntent(*player, reader);
        break;
    }
    case MessageType::C2S_ChatMessage: {
        auto* player = GetPlayer(peer);
        if (player) HandleChatMessage(*player, reader);
        break;
    }
    case MessageType::C2S_BuildRequest: {
        auto* player = GetPlayer(peer);
        if (player) HandleBuildRequest(*player, reader);
        break;
    }
    case MessageType::C2S_EntitySpawnReq: {
        auto* player = GetPlayer(peer);
        if (player) HandleEntitySpawnReq(*player, reader);
        break;
    }
    case MessageType::C2S_EntityDespawnReq: {
        auto* player = GetPlayer(peer);
        if (player) HandleEntityDespawnReq(*player, reader);
        break;
    }
    case MessageType::C2S_ZoneRequest: {
        auto* player = GetPlayer(peer);
        if (player) HandleZoneRequest(*player, reader);
        break;
    }
    default:
        spdlog::debug("GameServer: Unknown message type 0x{:02X}", static_cast<uint8_t>(header.type));
        break;
    }
}

void GameServer::HandleHandshake(ENetPeer* peer, PacketReader& reader) {
    MsgHandshake msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    // Verify protocol version
    if (msg.protocolVersion != KMP_PROTOCOL_VERSION) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_HandshakeReject);
        MsgHandshakeReject reject{};
        reject.reasonCode = 1;
        snprintf(reject.reasonText, sizeof(reject.reasonText),
                "Version mismatch: server=%d, client=%d", KMP_PROTOCOL_VERSION, msg.protocolVersion);
        writer.WriteRaw(&reject, sizeof(reject));

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
        enet_peer_disconnect_later(peer, 0);
        return;
    }

    // Create player
    PlayerID id = NextPlayerId();
    ConnectedPlayer player;
    player.id = id;
    player.name = msg.playerName;
    player.peer = peer;
    player.ping = peer->roundTripTime;
    player.lastUpdate = m_uptime;

    peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    m_players[id] = player;

    spdlog::info("GameServer: Player '{}' joined (ID: {}, {} players now)",
                 player.name, id, m_players.size());

    // Send handshake ack
    {
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_HandshakeAck);
        MsgHandshakeAck ack{};
        ack.playerId = id;
        ack.serverTick = m_serverTick;
        ack.timeOfDay = m_timeOfDay;
        ack.weatherState = m_weatherState;
        ack.maxPlayers = static_cast<uint8_t>(m_config.maxPlayers);
        ack.currentPlayers = static_cast<uint8_t>(m_players.size());
        writer.WriteRaw(&ack, sizeof(ack));

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
    }

    // Notify existing players about the new player
    {
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_PlayerJoined);
        MsgPlayerJoined joined{};
        joined.playerId = id;
        strncpy(joined.playerName, msg.playerName, KMP_MAX_NAME_LENGTH);
        writer.WriteRaw(&joined, sizeof(joined));
        Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
    }

    // Send existing players to the new player
    for (auto& [existingId, existingPlayer] : m_players) {
        if (existingId == id) continue;

        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_PlayerJoined);
        MsgPlayerJoined joined{};
        joined.playerId = existingId;
        strncpy(joined.playerName, existingPlayer.name.c_str(), KMP_MAX_NAME_LENGTH);
        writer.WriteRaw(&joined, sizeof(joined));

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
    }

    // Send world snapshot to new player
    SendWorldSnapshot(m_players[id]);

    BroadcastSystemMessage(player.name + " joined the game");
}

void GameServer::HandlePositionUpdate(ConnectedPlayer& player, PacketReader& reader) {
    uint8_t count;
    if (!reader.ReadU8(count)) return;

    for (uint8_t i = 0; i < count; i++) {
        CharacterPosition pos;
        if (!reader.ReadRaw(&pos, sizeof(pos))) break;

        // Update server-side entity position
        auto it = m_entities.find(pos.entityId);
        if (it != m_entities.end() && it->second.owner == player.id) {
            it->second.position = Vec3(pos.posX, pos.posY, pos.posZ);
            it->second.rotation = Quat::Decompress(pos.compressedQuat);
            it->second.zone = ZoneCoord::FromWorldPos(it->second.position);
            it->second.animState = pos.animStateId;
            it->second.moveSpeed = pos.moveSpeed;
            it->second.flags = pos.flags;
        }

        // Update player's position for zone tracking
        player.position = Vec3(pos.posX, pos.posY, pos.posZ);
        player.zone = ZoneCoord::FromWorldPos(player.position);
    }
}

void GameServer::HandleMoveCommand(ConnectedPlayer& player, PacketReader& reader) {
    MsgMoveCommand msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    // Validate entity ownership
    auto it = m_entities.find(msg.entityId);
    if (it == m_entities.end() || it->second.owner != player.id) return;

    // Broadcast to other players
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_MoveCommand);
    writer.WriteRaw(&msg, sizeof(msg));
    BroadcastExcept(player.id, writer.Data(), writer.Size(),
                   KMP_CHANNEL_RELIABLE_UNORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::HandleAttackIntent(ConnectedPlayer& player, PacketReader& reader) {
    MsgAttackIntent msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    // Validate attacker ownership
    auto it = m_entities.find(msg.attackerId);
    if (it == m_entities.end() || it->second.owner != player.id) return;

    // Validate target exists and is alive
    auto targetIt = m_entities.find(msg.targetId);
    if (targetIt == m_entities.end() || !targetIt->second.alive) return;

    // ── Kenshi-approximated combat resolution ──
    // Select random body part (weighted)
    static const struct { BodyPart part; int weight; } partWeights[] = {
        {BodyPart::Chest, 30}, {BodyPart::Stomach, 20}, {BodyPart::Head, 10},
        {BodyPart::LeftArm, 10}, {BodyPart::RightArm, 10},
        {BodyPart::LeftLeg, 10}, {BodyPart::RightLeg, 10},
    };

    int totalWeight = 100;
    int roll = std::rand() % totalWeight + 1;
    BodyPart hitPart = BodyPart::Chest;
    int cumulative = 0;
    for (auto& w : partWeights) {
        cumulative += w.weight;
        if (roll <= cumulative) { hitPart = w.part; break; }
    }

    // Calculate damage: attack * (1 - defense/100) * random(0.8, 1.2)
    float attackStat = 20.f;
    float defenseStat = 10.f;
    float randomFactor = 0.8f + (std::rand() % 41) / 100.f; // 0.80 to 1.20
    float defenseReduction = 1.f - std::min(defenseStat / 100.f, 0.9f);
    float totalDamage = attackStat * randomFactor * defenseReduction;

    // Split into cut/blunt based on weapon type flag
    float cutRatio = 0.5f, bluntRatio = 0.5f;
    // msg.attackType: 0=melee (balanced), could be extended
    float cutDmg = totalDamage * cutRatio;
    float bluntDmg = totalDamage * bluntRatio;

    // Block check (20% base chance)
    bool wasBlocked = (std::rand() % 100) < 20;
    if (wasBlocked) {
        cutDmg *= 0.3f;
        bluntDmg *= 0.3f;
    }

    // Apply damage to target
    int partIdx = static_cast<int>(hitPart);
    targetIt->second.health[partIdx] -= (cutDmg + bluntDmg);
    float resultHealth = targetIt->second.health[partIdx];

    // Check KO threshold: any body part below -50
    bool wasKO = false;
    for (int i = 0; i < 7; i++) {
        if (targetIt->second.health[i] <= -50.f) {
            wasKO = true;
            break;
        }
    }

    // Build and broadcast hit
    MsgCombatHit hit{};
    hit.attackerId = msg.attackerId;
    hit.targetId = msg.targetId;
    hit.bodyPart = static_cast<uint8_t>(hitPart);
    hit.cutDamage = cutDmg;
    hit.bluntDamage = bluntDmg;
    hit.pierceDamage = 0.f;
    hit.resultHealth = resultHealth;
    hit.wasBlocked = wasBlocked ? 1 : 0;
    hit.wasKO = wasKO ? 1 : 0;

    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_CombatHit);
    writer.WriteRaw(&hit, sizeof(hit));
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_UNORDERED, ENET_PACKET_FLAG_RELIABLE);

    // Check death threshold: chest <= -100 or head <= -100
    bool isDead = targetIt->second.health[static_cast<int>(BodyPart::Chest)] <= -100.f ||
                  targetIt->second.health[static_cast<int>(BodyPart::Head)] <= -100.f;

    if (isDead) {
        targetIt->second.alive = false;

        PacketWriter deathWriter;
        deathWriter.WriteHeader(MessageType::S2C_CombatDeath);
        MsgCombatDeath death{};
        death.entityId = msg.targetId;
        death.killerId = msg.attackerId;
        deathWriter.WriteRaw(&death, sizeof(death));
        Broadcast(deathWriter.Data(), deathWriter.Size(),
                 KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);

        spdlog::info("GameServer: Entity {} killed by {} (owned by {})",
                     msg.targetId, msg.attackerId, player.name);
    } else if (wasKO) {
        // Broadcast KO event
        PacketWriter koWriter;
        koWriter.WriteHeader(MessageType::S2C_CombatKO);
        MsgCombatDeath ko{}; // Reusing death struct for KO
        ko.entityId = msg.targetId;
        ko.killerId = msg.attackerId;
        koWriter.WriteRaw(&ko, sizeof(ko));
        Broadcast(koWriter.Data(), koWriter.Size(),
                 KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
    }
}

void GameServer::HandleChatMessage(ConnectedPlayer& player, PacketReader& reader) {
    uint32_t senderId;
    reader.ReadU32(senderId);
    std::string message;
    reader.ReadString(message);

    spdlog::info("[Chat] {}: {}", player.name, message);

    // Broadcast to all players
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_ChatMessage);
    writer.WriteU32(player.id);
    writer.WriteString(message);
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::HandleBuildRequest(ConnectedPlayer& player, PacketReader& reader) {
    MsgBuildRequest msg;
    if (!reader.ReadRaw(&msg, sizeof(msg))) return;

    // Create building entity
    EntityID buildId = m_nextEntityId++;
    ServerEntity building;
    building.id = buildId;
    building.type = EntityType::Building;
    building.owner = player.id;
    building.position = Vec3(msg.posX, msg.posY, msg.posZ);
    building.rotation = Quat::Decompress(msg.compressedQuat);
    building.zone = ZoneCoord::FromWorldPos(building.position);
    building.templateId = msg.templateId;
    building.alive = true;
    m_entities[buildId] = building;

    // Broadcast placement
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_BuildPlaced);
    MsgBuildPlaced placed{};
    placed.entityId = buildId;
    placed.templateId = msg.templateId;
    placed.posX = msg.posX;
    placed.posY = msg.posY;
    placed.posZ = msg.posZ;
    placed.compressedQuat = msg.compressedQuat;
    placed.builderId = player.id;
    writer.WriteRaw(&placed, sizeof(placed));
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);

    spdlog::info("GameServer: Player '{}' placed building {} at ({:.1f}, {:.1f}, {:.1f})",
                 player.name, buildId, msg.posX, msg.posY, msg.posZ);
}

void GameServer::BroadcastPositions() {
    // Collect all entity positions and broadcast to relevant players
    // (zone-based interest management)
    for (auto& [playerId, player] : m_players) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_PositionUpdate);

        // Count entities in range
        std::vector<const ServerEntity*> nearby;
        for (auto& [entityId, entity] : m_entities) {
            if (entity.owner == playerId) continue; // Don't send own entities back
            if (player.zone.IsAdjacent(entity.zone)) {
                nearby.push_back(&entity);
            }
        }

        if (nearby.empty()) continue;

        writer.WriteU32(0); // sourcePlayer = server
        writer.WriteU8(static_cast<uint8_t>(std::min(nearby.size(), size_t(255))));

        for (auto* entity : nearby) {
            CharacterPosition pos;
            pos.entityId = entity->id;
            pos.posX = entity->position.x;
            pos.posY = entity->position.y;
            pos.posZ = entity->position.z;
            pos.compressedQuat = entity->rotation.Compress();
            pos.animStateId = entity->animState;
            pos.moveSpeed = entity->moveSpeed;
            pos.flags = entity->flags;
            writer.WriteRaw(&pos, sizeof(pos));
        }

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), 0);
        enet_peer_send(player.peer, KMP_CHANNEL_UNRELIABLE_SEQ, pkt);
    }
}

void GameServer::BroadcastTimeSync() {
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_TimeSync);
    MsgTimeSync msg;
    msg.serverTick = m_serverTick;
    msg.timeOfDay = m_timeOfDay;
    msg.weatherState = m_weatherState;
    msg.gameSpeed = static_cast<uint8_t>(m_config.gameSpeed);
    writer.WriteRaw(&msg, sizeof(msg));
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::HandleEntitySpawnReq(ConnectedPlayer& player, PacketReader& reader) {
    // Host client reports a character was created in-game. Server assigns a server
    // entity ID, stores it, and broadcasts S2C_EntitySpawn to all clients.
    uint32_t clientEntityId, templateId, factionId;
    uint8_t type;
    uint32_t ownerId;
    float px, py, pz;
    uint32_t compQuat;

    reader.ReadU32(clientEntityId);
    reader.ReadU8(type);
    reader.ReadU32(ownerId);
    reader.ReadU32(templateId);
    reader.ReadVec3(px, py, pz);
    reader.ReadU32(compQuat);
    reader.ReadU32(factionId);

    // Read optional template name
    std::string templateName;
    uint16_t nameLen = 0;
    if (reader.Remaining() >= 2) {
        reader.ReadU16(nameLen);
        if (nameLen > 0 && nameLen <= 255 && reader.Remaining() >= nameLen) {
            templateName.resize(nameLen);
            reader.ReadRaw(templateName.data(), nameLen);
        }
    }

    // Assign server entity ID
    EntityID serverId = m_nextEntityId++;

    // Store in server entity list
    ServerEntity entity;
    entity.id = serverId;
    entity.type = static_cast<EntityType>(type);
    entity.owner = player.id;
    entity.position = Vec3(px, py, pz);
    entity.rotation = Quat::Decompress(compQuat);
    entity.templateId = templateId;
    entity.factionId = factionId;
    entity.templateName = templateName;
    m_entities[serverId] = entity;

    spdlog::info("GameServer: Entity spawn req from '{}': serverID={} template='{}' at ({:.1f},{:.1f},{:.1f})",
                 player.name, serverId, templateName, px, py, pz);

    // Broadcast S2C_EntitySpawn to ALL clients (including the host, so they get the server ID)
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_EntitySpawn);
    writer.WriteU32(serverId);
    writer.WriteU8(type);
    writer.WriteU32(player.id);
    writer.WriteU32(templateId);
    writer.WriteF32(px);
    writer.WriteF32(py);
    writer.WriteF32(pz);
    writer.WriteU32(compQuat);
    writer.WriteU32(factionId);
    writer.WriteU16(nameLen);
    if (nameLen > 0) {
        writer.WriteRaw(templateName.data(), nameLen);
    }

    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::HandleEntityDespawnReq(ConnectedPlayer& player, PacketReader& reader) {
    uint32_t entityId;
    uint8_t reason;
    if (!reader.ReadU32(entityId)) return;
    reader.ReadU8(reason); // optional

    // Validate: entity must exist and be owned by this player
    auto it = m_entities.find(entityId);
    if (it == m_entities.end()) return;
    if (it->second.owner != player.id) {
        spdlog::warn("GameServer: Player '{}' tried to despawn entity {} they don't own", player.name, entityId);
        return;
    }

    spdlog::info("GameServer: Entity {} despawned by '{}' (reason={})", entityId, player.name, reason);

    // Remove from server
    m_entities.erase(it);

    // Broadcast despawn to all clients
    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_EntityDespawn);
    writer.WriteU32(entityId);
    writer.WriteU8(reason);
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::HandleZoneRequest(ConnectedPlayer& player, PacketReader& reader) {
    int32_t zoneX, zoneY;
    if (!reader.ReadI32(zoneX) || !reader.ReadI32(zoneY)) return;

    spdlog::debug("GameServer: Player '{}' requested zone ({}, {})", player.name, zoneX, zoneY);

    ZoneCoord requestedZone(zoneX, zoneY);

    // Send all entities in the requested zone (and adjacent zones) to this player
    for (auto& [entityId, entity] : m_entities) {
        if (entity.owner == player.id) continue; // Don't send own entities
        if (!requestedZone.IsAdjacent(entity.zone) && !(entity.zone.x == zoneX && entity.zone.y == zoneY))
            continue;

        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_EntitySpawn);
        writer.WriteU32(entity.id);
        writer.WriteU8(static_cast<uint8_t>(entity.type));
        writer.WriteU32(entity.owner);
        writer.WriteU32(entity.templateId);
        writer.WriteF32(entity.position.x);
        writer.WriteF32(entity.position.y);
        writer.WriteF32(entity.position.z);
        writer.WriteU32(entity.rotation.Compress());
        writer.WriteU32(entity.factionId);
        uint16_t nameLen = static_cast<uint16_t>(
            std::min<size_t>(entity.templateName.size(), 255));
        writer.WriteU16(nameLen);
        if (nameLen > 0) {
            writer.WriteRaw(entity.templateName.data(), nameLen);
        }

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(player.peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
    }
}

void GameServer::SendWorldSnapshot(ConnectedPlayer& player) {
    // Send all entities to the newly joined player
    for (auto& [entityId, entity] : m_entities) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::S2C_EntitySpawn);
        writer.WriteU32(entity.id);
        writer.WriteU8(static_cast<uint8_t>(entity.type));
        writer.WriteU32(entity.owner);
        writer.WriteU32(entity.templateId);
        writer.WriteF32(entity.position.x);
        writer.WriteF32(entity.position.y);
        writer.WriteF32(entity.position.z);
        writer.WriteU32(entity.rotation.Compress());
        writer.WriteU32(entity.factionId);
        // Append template name so the client can spawn via SpawnManager
        uint16_t nameLen = static_cast<uint16_t>(
            std::min<size_t>(entity.templateName.size(), 255));
        writer.WriteU16(nameLen);
        if (nameLen > 0) {
            writer.WriteRaw(entity.templateName.data(), nameLen);
        }

        ENetPacket* pkt = enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(player.peer, KMP_CHANNEL_RELIABLE_ORDERED, pkt);
    }

    spdlog::info("GameServer: Sent {} entities to player '{}'", m_entities.size(), player.name);
}

// ── Broadcasting ──

void GameServer::Broadcast(const uint8_t* data, size_t len, int channel, uint32_t flags) {
    ENetPacket* pkt = enet_packet_create(data, len, flags);
    enet_host_broadcast(m_host, channel, pkt);
}

void GameServer::BroadcastExcept(PlayerID exclude, const uint8_t* data, size_t len,
                                  int channel, uint32_t flags) {
    for (auto& [id, player] : m_players) {
        if (id == exclude) continue;
        ENetPacket* pkt = enet_packet_create(data, len, flags);
        enet_peer_send(player.peer, channel, pkt);
    }
}

void GameServer::SendTo(PlayerID id, const uint8_t* data, size_t len, int channel, uint32_t flags) {
    auto it = m_players.find(id);
    if (it != m_players.end()) {
        ENetPacket* pkt = enet_packet_create(data, len, flags);
        enet_peer_send(it->second.peer, channel, pkt);
    }
}

// ── Helpers ──

ConnectedPlayer* GameServer::GetPlayer(ENetPeer* peer) {
    if (!peer || !peer->data) return nullptr;
    PlayerID id = static_cast<PlayerID>(reinterpret_cast<uintptr_t>(peer->data));
    auto it = m_players.find(id);
    return it != m_players.end() ? &it->second : nullptr;
}

ConnectedPlayer* GameServer::GetPlayer(PlayerID id) {
    auto it = m_players.find(id);
    return it != m_players.end() ? &it->second : nullptr;
}

PlayerID GameServer::NextPlayerId() {
    return m_nextPlayerId++;
}

// ── Admin Commands ──

void GameServer::KickPlayer(PlayerID id, const std::string& reason) {
    auto* player = GetPlayer(id);
    if (!player) {
        spdlog::warn("GameServer: Player {} not found", id);
        return;
    }

    spdlog::info("GameServer: Kicking player '{}' ({})", player->name, reason);
    BroadcastSystemMessage(player->name + " was kicked: " + reason);
    enet_peer_disconnect(player->peer, 0);
}

void GameServer::BroadcastSystemMessage(const std::string& message) {
    spdlog::info("[System] {}", message);

    PacketWriter writer;
    writer.WriteHeader(MessageType::S2C_SystemMessage);
    writer.WriteU32(0); // system
    writer.WriteString(message);
    Broadcast(writer.Data(), writer.Size(), KMP_CHANNEL_RELIABLE_ORDERED, ENET_PACKET_FLAG_RELIABLE);
}

void GameServer::LoadWorld() {
    std::string savePath = m_config.savePath.empty()
        ? "kenshi_mp_world.json" : m_config.savePath;

    float loadedTime = m_timeOfDay;
    int loadedWeather = m_weatherState;
    EntityID loadedNextId = m_nextEntityId;

    if (LoadWorldFromFile(savePath, m_entities, loadedTime, loadedWeather, loadedNextId)) {
        m_timeOfDay = loadedTime;
        m_weatherState = loadedWeather;
        m_nextEntityId = loadedNextId;
        spdlog::info("GameServer: Loaded world from '{}' ({} entities, time={:.2f})",
                     savePath, m_entities.size(), m_timeOfDay);
    } else {
        spdlog::info("GameServer: No saved world at '{}', starting fresh", savePath);
    }
}

void GameServer::SaveWorld() {
    spdlog::info("GameServer: Saving world... ({} entities, {} players)",
                 m_entities.size(), m_players.size());

    std::string savePath = m_config.savePath.empty()
        ? "kenshi_mp_world.json" : m_config.savePath;

    if (SaveWorldToFile(savePath, m_entities, m_timeOfDay, m_weatherState)) {
        spdlog::info("GameServer: World saved to '{}'", savePath);
    } else {
        spdlog::error("GameServer: Failed to save world to '{}'", savePath);
    }
}

void GameServer::PrintStatus() {
    spdlog::info("=== Server Status ===");
    spdlog::info("Players: {}/{}", m_players.size(), m_config.maxPlayers);
    spdlog::info("Entities: {}", m_entities.size());
    spdlog::info("Tick: {} | Time: {:.2f} | Uptime: {:.0f}s", m_serverTick, m_timeOfDay, m_uptime);
}

void GameServer::PrintPlayers() {
    spdlog::info("=== Connected Players ({}) ===", m_players.size());
    for (auto& [id, player] : m_players) {
        spdlog::info("  [{}] {} - ping: {}ms - zone: ({},{})",
                     id, player.name, player.ping, player.zone.x, player.zone.y);
    }
}

} // namespace kmp

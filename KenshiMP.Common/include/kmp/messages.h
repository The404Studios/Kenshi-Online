#pragma once
#include "types.h"
#include "constants.h"
#include <cstdint>
#include <string>

namespace kmp {

// ── Connection Messages ──

#pragma pack(push, 1)

struct MsgHandshake {
    uint32_t protocolVersion;
    char     playerName[KMP_MAX_NAME_LENGTH + 1];
    uint8_t  gameVersionMajor;
    uint8_t  gameVersionMinor;
    uint8_t  gameVersionPatch;
    uint8_t  reserved;
};

struct MsgHandshakeAck {
    PlayerID playerId;
    uint32_t serverTick;
    float    timeOfDay;
    int32_t  weatherState;
    uint8_t  maxPlayers;
    uint8_t  currentPlayers;
    uint16_t reserved;
};

struct MsgHandshakeReject {
    uint8_t  reasonCode; // 0=full, 1=version mismatch, 2=banned, 3=other
    char     reasonText[128];
};

struct MsgPlayerJoined {
    PlayerID playerId;
    char     playerName[KMP_MAX_NAME_LENGTH + 1];
};

struct MsgPlayerLeft {
    PlayerID playerId;
    uint8_t  reason; // 0=disconnect, 1=timeout, 2=kicked
};

// ── Movement Messages ──

struct CharacterPosition {
    EntityID entityId;
    float    posX, posY, posZ;
    uint32_t compressedQuat; // Smallest-three encoded
    uint8_t  animStateId;
    uint8_t  moveSpeed;      // 0-255 mapped to 0.0-15.0 m/s
    uint16_t flags;          // Bit 0: running, Bit 1: sneaking, Bit 2: in combat
};

struct MsgC2SPositionUpdate {
    uint8_t characterCount;
    // Followed by characterCount × CharacterPosition
};

struct MsgS2CPositionUpdate {
    PlayerID sourcePlayer;
    uint8_t  characterCount;
    // Followed by characterCount × CharacterPosition
};

struct MsgMoveCommand {
    EntityID entityId;
    float    targetX, targetY, targetZ;
    uint8_t  moveType; // 0=walk, 1=run, 2=sneak
};

// ── Combat Messages ──

struct MsgAttackIntent {
    EntityID attackerId;
    EntityID targetId;
    uint8_t  attackType; // 0=melee, 1=ranged
};

struct MsgCombatHit {
    EntityID attackerId;
    EntityID targetId;
    uint8_t  bodyPart;      // BodyPart enum
    float    cutDamage;
    float    bluntDamage;
    float    pierceDamage;
    float    resultHealth;
    uint8_t  wasBlocked;    // 0=hit, 1=partial block, 2=full block
    uint8_t  wasKO;
};

struct MsgCombatBlock {
    EntityID entityId;
    EntityID attackerId;
    uint8_t  bodyPart;
    float    blockEffectiveness; // 0.0-1.0
};

struct MsgCombatKO {
    EntityID entityId;
    EntityID attackerId;
    uint8_t  bodyPart;
    float    resultHealth;
};

struct MsgCombatDeath {
    EntityID entityId;
    EntityID killerId; // 0 if environmental
};

// ── Entity Messages ──

struct MsgEntitySpawn {
    EntityID    entityId;
    EntityType  type;
    PlayerID    ownerId;      // 0 = server-owned (NPC)
    uint32_t    templateId;   // Game data template reference
    float       posX, posY, posZ;
    uint32_t    compressedQuat;
    uint32_t    factionId;
    // Variable-length data follows: name string, equipment, stats
};

struct MsgEntityDespawn {
    EntityID entityId;
    uint8_t  reason; // 0=normal, 1=killed, 2=out of range
};

// ── Stats Messages ──

struct MsgHealthUpdate {
    EntityID entityId;
    float    health[static_cast<int>(BodyPart::Count)]; // Per body part
    float    bloodLevel;
};

struct MsgEquipmentUpdate {
    EntityID entityId;
    uint8_t  slot;       // EquipSlot enum
    uint32_t itemTemplateId; // 0 = empty
};

struct MsgStatUpdate {
    EntityID entityId;
    uint8_t  statIndex;
    float    statValue; // Whole = level, decimal = XP%
};

// ── Building Messages ──

struct MsgBuildRequest {
    uint32_t templateId;
    float    posX, posY, posZ;
    uint32_t compressedQuat;
};

struct MsgBuildPlaced {
    EntityID entityId;
    uint32_t templateId;
    float    posX, posY, posZ;
    uint32_t compressedQuat;
    PlayerID builderId;
};

struct MsgBuildProgress {
    EntityID entityId;
    float    progress; // 0.0 to 1.0
};

struct MsgDoorState {
    EntityID entityId;
    uint8_t  state; // 0=closed, 1=open, 2=locked, 3=broken
};

// ── Inventory / Trade Messages ──

struct MsgItemPickup {
    EntityID entityId;      // Character who picked up
    uint32_t itemTemplateId;
    int32_t  quantity;
};

struct MsgItemDrop {
    EntityID entityId;      // Character who dropped
    uint32_t itemTemplateId;
    float    posX, posY, posZ; // Where the item was dropped
};

struct MsgTradeRequest {
    EntityID buyerEntityId;
    EntityID sellerEntityId; // 0 for NPC shop
    uint32_t itemTemplateId;
    int32_t  quantity;
    int32_t  price;          // Total cost in cats
};

struct MsgTradeResult {
    EntityID buyerEntityId;
    uint32_t itemTemplateId;
    int32_t  quantity;
    uint8_t  success;        // 0=denied, 1=accepted
};

struct MsgInventoryUpdate {
    EntityID entityId;
    uint8_t  action;         // 0=add, 1=remove, 2=modify count
    uint32_t itemTemplateId;
    int32_t  quantity;
};

// ── Squad Messages ──

struct MsgSquadCreate {
    EntityID creatorEntityId;
    uint32_t squadNetId;     // Server-assigned squad ID
    // Followed by string: squad name
};

struct MsgSquadMemberUpdate {
    uint32_t squadNetId;
    EntityID memberEntityId;
    uint8_t  action;         // 0=added, 1=removed
};

// ── Faction Messages ──

struct MsgFactionRelation {
    uint32_t factionIdA;
    uint32_t factionIdB;
    float    relation;       // -100.0 to +100.0
    EntityID causerEntityId; // Who caused the change (0=system)
};

// ── Combat Stance ──

struct MsgCombatStance {
    EntityID entityId;
    uint8_t  stance;    // 0=passive, 1=defensive, 2=aggressive, 3=hold
};

// ── Item Transfer ──

struct MsgItemTransfer {
    EntityID sourceEntityId;  // Character/container transferring FROM
    EntityID destEntityId;    // Character/container transferring TO
    uint32_t itemTemplateId;
    int32_t  quantity;
};

// ── Door Interaction ──

struct MsgDoorInteract {
    EntityID entityId;        // Building/gate entity
    EntityID actorEntityId;   // Character performing the action
    uint8_t  action;          // 0=open, 1=close, 2=lock, 3=unlock
};

// ── Admin Command ──

struct MsgAdminCommand {
    uint8_t  commandType;     // 0=kick, 1=ban, 2=setTime, 3=setWeather, 4=announce
    PlayerID targetPlayerId;  // For kick/ban
    float    floatParam;      // For setTime/setWeather
    char     textParam[128];  // For announce, kick reason
};

struct MsgAdminResponse {
    uint8_t  success;         // 0=denied, 1=ok
    char     responseText[128];
};

// ── Building Sync Messages ──

struct MsgBuildDismantle {
    EntityID buildingId;
    EntityID dismantlerId;   // Who dismantled it
};

struct MsgBuildRepair {
    EntityID buildingId;
    float    amount;         // Repair amount
};

// ── Time Sync ──

struct MsgTimeSync {
    uint32_t serverTick;
    float    timeOfDay;   // 0.0 to 1.0
    int32_t  weatherState;
    uint8_t  gameSpeed;   // 1-4
};

// ── Chat ──

struct MsgChatMessage {
    PlayerID senderId; // 0 = system
    // Followed by string: message text
};

// ── Server Query ──
// Lightweight query — client sends C2S_ServerQuery, server responds
// with S2C_ServerInfo without requiring a handshake.

struct MsgServerQuery {
    uint32_t protocolVersion;
};

struct MsgServerInfo {
    uint32_t protocolVersion;
    uint8_t  currentPlayers;
    uint8_t  maxPlayers;
    uint16_t port;
    float    timeOfDay;
    uint8_t  pvpEnabled;
    uint8_t  reserved[3];
    char     serverName[64];
};

// ── Master Server Messages ──
// Used for server browser registry. Game servers register with the master,
// clients query for the list of available games.

struct MsgMasterRegister {
    uint32_t protocolVersion;
    uint16_t gamePort;           // Port the game server listens on
    uint8_t  currentPlayers;
    uint8_t  maxPlayers;
    float    timeOfDay;
    uint8_t  pvpEnabled;
    uint8_t  reserved[3];
    char     serverName[64];
    char     externalIP[46];     // IPv4/IPv6 string (filled by master if empty)
};

struct MsgMasterHeartbeat {
    uint16_t gamePort;
    uint8_t  currentPlayers;
    uint8_t  maxPlayers;
    float    timeOfDay;
};

struct MsgMasterServerEntry {
    char     serverName[64];
    char     address[46];
    uint16_t port;
    uint8_t  currentPlayers;
    uint8_t  maxPlayers;
    uint8_t  pvpEnabled;
    uint8_t  reserved;
};

#pragma pack(pop)

} // namespace kmp

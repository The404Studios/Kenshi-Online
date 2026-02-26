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

#pragma pack(pop)

} // namespace kmp

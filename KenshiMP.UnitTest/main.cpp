// ═══════════════════════════════════════════════════════════════════════════
//  KenshiMP Unit Test — Validates game-facing logic with fake game memory
// ═══════════════════════════════════════════════════════════════════════════
// This test proves the EXACT code that runs inside Kenshi actually works:
//   - CharacterAccessor reads/writes positions, names, factions correctly
//   - EntityRegistry tracks entities by ID, owner, game object pointer
//   - Interpolation system smooths remote player movement
//   - Packet serialization round-trips data correctly
//
// We allocate fake "game objects" in our own process memory. The Memory::Read/Write
// functions use raw pointer dereference (with SEH), so they work identically
// on our fake buffers as they do on real Kenshi game objects.
// If this test passes, the in-game code WILL work (assuming correct offsets).

#include "game/game_types.h"
#include "sync/entity_registry.h"
#include "sync/interpolation.h"
#include "sync/authority_validator.h"
#include "sys/command_registry.h"
#include "kmp/types.h"
#include "kmp/constants.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <string>
#include <stdexcept>

// ── Test framework ──
static int g_passed = 0;
static int g_failed = 0;

static void TestAssert(bool condition, const char* name) {
    if (condition) {
        printf("  [PASS] %s\n", name);
        g_passed++;
    } else {
        printf("  [FAIL] %s\n", name);
        g_failed++;
    }
}

static bool FloatEq(float a, float b, float eps = 0.001f) {
    return std::abs(a - b) < eps;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Fake Kenshi character — laid out in memory exactly like the real thing
// ═══════════════════════════════════════════════════════════════════════════
// We allocate a buffer and write values at the offsets that CharacterAccessor
// expects. This simulates what Kenshi's CharacterCreate function produces.

struct FakeCharacter {
    uint8_t data[0x1000];  // 4KB — more than enough for a character struct

    FakeCharacter() { memset(data, 0, sizeof(data)); }
    uintptr_t Addr() const { return reinterpret_cast<uintptr_t>(data); }
    void* Ptr() const { return const_cast<uint8_t*>(data); }

    // Write a value at an offset (templated)
    template<typename T>
    void Set(int offset, const T& val) {
        memcpy(data + offset, &val, sizeof(T));
    }

    // Read a value from an offset (templated)
    template<typename T>
    T Get(int offset) const {
        T val{};
        memcpy(&val, data + offset, sizeof(T));
        return val;
    }

    // Write a Kenshi SSO string (MSVC std::string layout) at offset
    // Layout: [16 bytes inline buffer][uint64 size][uint64 capacity]
    void SetSSOString(int offset, const char* str) {
        size_t len = strlen(str);
        // Zero the whole 32-byte std::string area
        memset(data + offset, 0, 32);
        if (len <= 15) {
            // SSO: copy into inline buffer
            memcpy(data + offset, str, len);
            // Size at +0x10
            uint64_t size = len;
            memcpy(data + offset + 0x10, &size, sizeof(size));
            // Capacity at +0x18 = 15 (SSO mode)
            uint64_t cap = 15;
            memcpy(data + offset + 0x18, &cap, sizeof(cap));
        }
    }

    // Write a Vec3 at offset
    void SetVec3(int offset, float x, float y, float z) {
        Set<float>(offset, x);
        Set<float>(offset + 4, y);
        Set<float>(offset + 8, z);
    }

    // Write a faction pointer at offset
    void SetFactionPtr(int offset, uintptr_t factionAddr) {
        Set<uintptr_t>(offset, factionAddr);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 1: CharacterAccessor reads position from fake game memory
// ═══════════════════════════════════════════════════════════════════════════
static void Test_ReadPosition() {
    printf("\n=== Test: CharacterAccessor::GetPosition ===\n");

    auto& offsets = kmp::game::GetOffsets().character;
    FakeCharacter fake;
    fake.SetVec3(offsets.position, -51200.0f, 1600.0f, 2700.0f);

    kmp::game::CharacterAccessor accessor(fake.Ptr());
    TestAssert(accessor.IsValid(), "Accessor is valid");

    kmp::Vec3 pos = accessor.GetPosition();
    TestAssert(FloatEq(pos.x, -51200.0f), "Position.x matches (-51200)");
    TestAssert(FloatEq(pos.y, 1600.0f),   "Position.y matches (1600)");
    TestAssert(FloatEq(pos.z, 2700.0f),   "Position.z matches (2700)");
    printf("    Read position: (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 2: CharacterAccessor reads name (SSO mode) from fake game memory
// ═══════════════════════════════════════════════════════════════════════════
static void Test_ReadName() {
    printf("\n=== Test: CharacterAccessor::GetName (SSO) ===\n");

    auto& offsets = kmp::game::GetOffsets().character;
    FakeCharacter fake;
    fake.SetSSOString(offsets.name, "Beep");

    kmp::game::CharacterAccessor accessor(fake.Ptr());
    std::string name = accessor.GetName();
    TestAssert(name == "Beep", "Name reads as 'Beep'");
    printf("    Read name: '%s'\n", name.c_str());

    // Test a longer SSO name (15 chars max)
    FakeCharacter fake2;
    fake2.SetSSOString(offsets.name, "Cat-Lon_Master");

    kmp::game::CharacterAccessor accessor2(fake2.Ptr());
    std::string name2 = accessor2.GetName();
    TestAssert(name2 == "Cat-Lon_Master", "14-char name reads correctly");
    printf("    Read name: '%s'\n", name2.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 3: CharacterAccessor writes name to fake game memory
// ═══════════════════════════════════════════════════════════════════════════
static void Test_WriteName() {
    printf("\n=== Test: CharacterAccessor::WriteName ===\n");

    auto& offsets = kmp::game::GetOffsets().character;
    FakeCharacter fake;
    fake.SetSSOString(offsets.name, "OldName");

    kmp::game::CharacterAccessor accessor(fake.Ptr());

    // Write a new name
    bool ok = accessor.WriteName("RemotePlayer");
    TestAssert(ok, "WriteName returned true");

    // Read it back
    std::string result = accessor.GetName();
    TestAssert(result == "RemotePlayer", "Name written and read back correctly");
    printf("    Wrote 'RemotePlayer', read back: '%s'\n", result.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 4: CharacterAccessor writes position (fallback mode)
// ═══════════════════════════════════════════════════════════════════════════
static void Test_WritePosition() {
    printf("\n=== Test: CharacterAccessor::WritePosition (fallback) ===\n");

    auto& offsets = kmp::game::GetOffsets().character;
    FakeCharacter fake;
    fake.SetVec3(offsets.position, 0.0f, 0.0f, 0.0f);

    kmp::game::CharacterAccessor accessor(fake.Ptr());

    // Write a new position (will use fallback: writes to cached position)
    kmp::Vec3 newPos{-5000.0f, 200.0f, 8000.0f};
    bool ok = accessor.WritePosition(newPos);
    TestAssert(ok, "WritePosition returned true");

    // Read it back
    kmp::Vec3 readPos = accessor.GetPosition();
    TestAssert(FloatEq(readPos.x, -5000.0f), "Written position.x matches");
    TestAssert(FloatEq(readPos.y, 200.0f),   "Written position.y matches");
    TestAssert(FloatEq(readPos.z, 8000.0f),  "Written position.z matches");
    printf("    Wrote (-5000, 200, 8000), read back: (%.1f, %.1f, %.1f)\n",
           readPos.x, readPos.y, readPos.z);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 5: CharacterAccessor reads/writes faction pointer
// ═══════════════════════════════════════════════════════════════════════════
static void Test_FactionReadWrite() {
    printf("\n=== Test: CharacterAccessor Faction Read/Write ===\n");

    auto& offsets = kmp::game::GetOffsets().character;
    FakeCharacter fake;

    // Simulate a faction pointer (some heap address)
    uintptr_t fakeFaction = 0x0000020000001234;
    fake.SetFactionPtr(offsets.faction, fakeFaction);

    kmp::game::CharacterAccessor accessor(fake.Ptr());

    uintptr_t readFaction = accessor.GetFactionPtr();
    TestAssert(readFaction == fakeFaction, "Faction pointer reads correctly");
    printf("    Read faction ptr: 0x%llX\n", (unsigned long long)readFaction);

    // Write a different faction
    uintptr_t newFaction = 0x0000020000005678;
    bool ok = accessor.WriteFaction(newFaction);
    TestAssert(ok, "WriteFaction returned true");

    uintptr_t readBack = accessor.GetFactionPtr();
    TestAssert(readBack == newFaction, "New faction ptr reads back correctly");
    printf("    Wrote 0x%llX, read back: 0x%llX\n",
           (unsigned long long)newFaction, (unsigned long long)readBack);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 6: EntityRegistry — register, lookup, unregister
// ═══════════════════════════════════════════════════════════════════════════
static void Test_EntityRegistry() {
    printf("\n=== Test: EntityRegistry CRUD ===\n");

    kmp::EntityRegistry registry;
    FakeCharacter fake1, fake2;

    // Register two local entities
    kmp::EntityID id1 = registry.Register(fake1.Ptr(), kmp::EntityType::PlayerCharacter, 1);
    kmp::EntityID id2 = registry.Register(fake2.Ptr(), kmp::EntityType::PlayerCharacter, 1);
    TestAssert(id1 != id2, "Two entities get different IDs");
    TestAssert(registry.GetEntityCount() == 2, "Registry has 2 entities");

    // Lookup by game object pointer
    kmp::EntityID lookup1 = registry.GetNetId(fake1.Ptr());
    TestAssert(lookup1 == id1, "Lookup by pointer returns correct ID");

    // Lookup by network ID
    void* obj = registry.GetGameObject(id2);
    TestAssert(obj == fake2.Ptr(), "Lookup by netId returns correct game object");

    // Get entity info
    auto info = registry.GetInfo(id1);
    TestAssert(info.has_value(), "GetInfo returns non-empty optional");
    TestAssert(info->ownerPlayerId == 1, "Owner matches");
    TestAssert(!info->isRemote, "Local entity is not remote");

    // Get player entities
    auto playerEnts = registry.GetPlayerEntities(1);
    TestAssert(playerEnts.size() == 2, "Player 1 has 2 entities");

    // Unregister one
    registry.Unregister(id1);
    TestAssert(registry.GetEntityCount() == 1, "After unregister: 1 entity");
    TestAssert(registry.GetGameObject(id1) == nullptr, "Unregistered entity returns null");

    printf("    Registry CRUD: all operations verified\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 7: EntityRegistry — remote entity lifecycle (what happens in MP)
// ═══════════════════════════════════════════════════════════════════════════
static void Test_RemoteEntityLifecycle() {
    printf("\n=== Test: Remote Entity Lifecycle ===\n");

    kmp::EntityRegistry registry;
    FakeCharacter fakeRemote;

    // Step 1: Server tells us about a remote entity (EntitySpawn message)
    kmp::Vec3 spawnPos{-51200.0f, 1600.0f, 2700.0f};
    kmp::EntityID remoteId = registry.RegisterRemote(100, kmp::EntityType::PlayerCharacter, 2, spawnPos);
    TestAssert(remoteId == 100, "Remote entity registered with server-assigned ID");
    TestAssert(registry.GetRemoteCount() == 1, "1 remote entity in registry");
    TestAssert(registry.GetSpawnedRemoteCount() == 0, "0 spawned remotes (no game object yet)");

    // Step 2: entity_hooks replays CharacterCreate, gets a game object
    // We simulate this by linking a fake game object
    registry.SetGameObject(100, fakeRemote.Ptr());
    TestAssert(registry.GetSpawnedRemoteCount() == 1, "1 spawned remote (game object linked)");
    TestAssert(registry.GetGameObject(100) == fakeRemote.Ptr(), "Game object lookup works");

    // Step 3: Position updates from server
    kmp::Vec3 newPos{-51180.0f, 1605.0f, 2720.0f};
    registry.UpdatePosition(100, newPos);
    auto info = registry.GetInfo(100);
    TestAssert(info.has_value(), "Info still valid after position update");
    TestAssert(FloatEq(info->lastPosition.x, -51180.0f), "Updated position.x stored");

    // Step 4: Player disconnects, server sends PlayerLeft
    size_t cleared = registry.ClearRemoteEntities();
    TestAssert(cleared == 1, "ClearRemoteEntities removed 1 entity");
    TestAssert(registry.GetRemoteCount() == 0, "No remote entities after clear");
    TestAssert(registry.GetGameObject(100) == nullptr, "Game object removed after clear");

    printf("    Full remote entity lifecycle: register -> link -> update -> cleanup\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 8: EntityRegistry — ID remapping (server assigns new ID)
// ═══════════════════════════════════════════════════════════════════════════
static void Test_EntityRemap() {
    printf("\n=== Test: Entity ID Remapping ===\n");

    kmp::EntityRegistry registry;
    FakeCharacter fake;

    // Register with local ID
    kmp::EntityID localId = registry.Register(fake.Ptr(), kmp::EntityType::PlayerCharacter, 1);

    // Server assigns new ID
    kmp::EntityID serverId = 42;
    bool ok = registry.RemapEntityId(localId, serverId);
    TestAssert(ok, "RemapEntityId succeeded");

    // Old ID should be gone
    TestAssert(registry.GetGameObject(localId) == nullptr, "Old ID no longer exists");

    // New ID should work
    TestAssert(registry.GetGameObject(serverId) == fake.Ptr(), "New ID maps to same game object");

    // Pointer lookup should return new ID
    TestAssert(registry.GetNetId(fake.Ptr()) == serverId, "Pointer lookup returns new server ID");

    printf("    Remap: local ID %u -> server ID %u\n", localId, serverId);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 9: Interpolation — smooth remote player movement
// ═══════════════════════════════════════════════════════════════════════════
static void Test_Interpolation() {
    printf("\n=== Test: Interpolation System ===\n");

    kmp::Interpolation interp;

    // Simulate server sending positions at 50ms intervals (matching KMP_TICK_RATE=20Hz).
    // Use small deltas (2 units) so the snap correction system doesn't activate
    // (KMP_SNAP_THRESHOLD_MIN=5.0). First snapshot establishes velocity baseline.
    kmp::Vec3 pos0{0.0f, 0.0f, 0.0f};
    kmp::Vec3 pos1{2.0f, 0.0f, 0.0f};
    kmp::Vec3 pos2{4.0f, 0.0f, 0.0f};
    kmp::Vec3 pos3{6.0f, 0.0f, 0.0f};
    kmp::Quat rot{};

    // Warm-up snapshot establishes velocity so snap correction doesn't fire on pos1
    interp.AddSnapshot(1, 1.00f, pos0, rot);
    interp.AddSnapshot(1, 1.05f, pos1, rot);
    interp.AddSnapshot(1, 1.10f, pos2, rot);
    interp.AddSnapshot(1, 1.15f, pos3, rot);

    // The interpolation system subtracts an adaptive delay (~50ms when jitter is zero)
    // from renderTime before interpolating. So to get interpTime at the midpoint of
    // [pos1(t=1.05), pos2(t=1.10)], we need interpTime=1.075 → renderTime=1.075+0.05=1.125
    const float delay = kmp::KMP_INTERP_DELAY_MIN; // 50ms — expected with zero-jitter snapshots

    // Query at midpoint between pos1 (x=2) and pos2 (x=4) → expect x≈3.0
    kmp::Vec3 result;
    kmp::Quat resultRot;
    float queryTime1 = 1.075f + delay;
    bool ok = interp.GetInterpolated(1, queryTime1, result, resultRot);
    TestAssert(ok, "GetInterpolated returns true");
    TestAssert(FloatEq(result.x, 3.0f, 0.5f), "Interpolated X is ~3.0 (midpoint)");
    printf("    At renderTime=%.3f: interpolated X = %.2f (expected ~3.0)\n", queryTime1, result.x);

    // Query at midpoint between pos2 (x=4) and pos3 (x=6) → expect x≈5.0
    float queryTime2 = 1.125f + delay;
    ok = interp.GetInterpolated(1, queryTime2, result, resultRot);
    TestAssert(ok, "GetInterpolated at midpoint returns true");
    TestAssert(FloatEq(result.x, 5.0f, 0.5f), "Interpolated X is ~5.0");
    printf("    At renderTime=%.3f: interpolated X = %.2f (expected ~5.0)\n", queryTime2, result.x);

    // Remove entity
    interp.RemoveEntity(1);
    ok = interp.GetInterpolated(1, 1.2f, result, resultRot);
    TestAssert(!ok, "GetInterpolated returns false after RemoveEntity");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 10: Packet round-trip — write and read position update
// ═══════════════════════════════════════════════════════════════════════════
static void Test_PacketRoundTrip() {
    printf("\n=== Test: Packet Round-Trip (Position Update) ===\n");

    // Simulate what OnGameTick sends: a C2S position update with one character
    kmp::PacketWriter writer;
    writer.WriteHeader(kmp::MessageType::C2S_PositionUpdate);

    kmp::MsgC2SPositionUpdate header_msg{};
    header_msg.characterCount = 1;
    writer.WriteRaw(&header_msg, sizeof(header_msg));

    kmp::CharacterPosition charPos{};
    charPos.entityId = 42;
    charPos.posX = -51200.0f;
    charPos.posY = 1600.0f;
    charPos.posZ = 2700.0f;
    charPos.moveSpeed = 128; // ~7.5 m/s
    charPos.animStateId = 1; // walking
    writer.WriteRaw(&charPos, sizeof(charPos));

    // Read it back (simulating server receiving the packet)
    kmp::PacketReader reader(writer.Data(), writer.Size());
    kmp::PacketHeader header;
    TestAssert(reader.ReadHeader(header), "Reader reads header");
    TestAssert(header.type == kmp::MessageType::C2S_PositionUpdate, "Message type matches");

    kmp::MsgC2SPositionUpdate readHeader{};
    TestAssert(reader.ReadRaw(&readHeader, sizeof(readHeader)), "Reader reads update header");
    TestAssert(readHeader.characterCount == 1, "Character count is 1");

    kmp::CharacterPosition readPos{};
    TestAssert(reader.ReadRaw(&readPos, sizeof(readPos)), "Reader reads character position");
    TestAssert(readPos.entityId == 42, "Entity ID matches");
    TestAssert(FloatEq(readPos.posX, -51200.0f), "Position.x matches");
    TestAssert(FloatEq(readPos.posY, 1600.0f),   "Position.y matches");
    TestAssert(FloatEq(readPos.posZ, 2700.0f),   "Position.z matches");
    TestAssert(readPos.moveSpeed == 128, "MoveSpeed matches");
    TestAssert(readPos.animStateId == 1, "AnimState matches");
    printf("    Position update packet: entity=%u pos=(%.1f, %.1f, %.1f)\n",
           readPos.entityId, readPos.posX, readPos.posY, readPos.posZ);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 11: Packet round-trip — entity spawn message
// ═══════════════════════════════════════════════════════════════════════════
static void Test_SpawnPacketRoundTrip() {
    printf("\n=== Test: Packet Round-Trip (Entity Spawn) ===\n");

    kmp::PacketWriter writer;
    writer.WriteHeader(kmp::MessageType::S2C_EntitySpawn);

    kmp::MsgEntitySpawn msg{};
    msg.entityId = 100;
    msg.ownerId = 2;
    msg.type = kmp::EntityType::PlayerCharacter;
    msg.posX = -51200.0f;
    msg.posY = 1600.0f;
    msg.posZ = 2700.0f;
    msg.templateId = 12345;
    writer.WriteRaw(&msg, sizeof(msg));
    // Variable-length template name follows the struct
    std::string templateName = "Greenlander";
    writer.WriteString(templateName);

    // Read it back
    kmp::PacketReader reader(writer.Data(), writer.Size());
    kmp::PacketHeader header;
    TestAssert(reader.ReadHeader(header), "Reader reads spawn header");
    TestAssert(header.type == kmp::MessageType::S2C_EntitySpawn, "Spawn message type matches");

    kmp::MsgEntitySpawn readMsg{};
    TestAssert(reader.ReadRaw(&readMsg, sizeof(readMsg)), "Reader reads spawn payload");
    TestAssert(readMsg.entityId == 100, "Spawn entity ID matches");
    TestAssert(readMsg.ownerId == 2, "Spawn owner matches");
    TestAssert(readMsg.templateId == 12345, "Template ID matches");
    TestAssert(FloatEq(readMsg.posX, -51200.0f), "Spawn position.x matches");

    std::string readName;
    TestAssert(reader.ReadString(readName), "Reader reads template name");
    TestAssert(readName == "Greenlander", "Template name matches");
    printf("    Spawn packet: entity=%u owner=%u template='%s'\n",
           readMsg.entityId, readMsg.ownerId, readName.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 12: Full spawn flow — server spawn message → registry → accessor
// ═══════════════════════════════════════════════════════════════════════════
static void Test_FullSpawnFlow() {
    printf("\n=== Test: Full Spawn Flow (Server → Registry → Game Memory) ===\n");

    // Step 1: Receive EntitySpawn packet from server
    kmp::MsgEntitySpawn spawnMsg{};
    spawnMsg.entityId = 200;
    spawnMsg.ownerId = 3;
    spawnMsg.type = kmp::EntityType::PlayerCharacter;
    spawnMsg.posX = -51200.0f;
    spawnMsg.posY = 1600.0f;
    spawnMsg.posZ = 2700.0f;

    // Step 2: Register in entity registry (what packet_handler.cpp does)
    kmp::EntityRegistry registry;
    kmp::Vec3 spawnPos{spawnMsg.posX, spawnMsg.posY, spawnMsg.posZ};
    kmp::EntityID eid = registry.RegisterRemote(
        spawnMsg.entityId,
        spawnMsg.type,
        spawnMsg.ownerId,
        spawnPos
    );
    TestAssert(eid == 200, "Remote entity registered with server ID");
    TestAssert(registry.GetRemoteCount() == 1, "1 remote entity");

    // Step 3: entity_hooks replays CharacterCreate → we get a game object
    FakeCharacter fakeGameObj;
    auto& offsets = kmp::game::GetOffsets().character;

    // Game object starts with default values (like Kenshi would set)
    fakeGameObj.SetSSOString(offsets.name, "Greenlander");
    fakeGameObj.SetVec3(offsets.position, -51200.0f, 1600.0f, 2700.0f);

    // Link the game object to the registry (what entity_hooks does after replay)
    registry.SetGameObject(200, fakeGameObj.Ptr());
    TestAssert(registry.GetSpawnedRemoteCount() == 1, "Game object linked to remote entity");

    // Step 4: PlayerController renames the character to the remote player's name
    kmp::game::CharacterAccessor accessor(fakeGameObj.Ptr());
    bool nameOk = accessor.WriteName("Player3");
    TestAssert(nameOk, "WriteName succeeded");
    std::string readName = accessor.GetName();
    TestAssert(readName == "Player3", "Character renamed to remote player's name");

    // Step 5: Write faction pointer (allies with local player)
    uintptr_t localFaction = 0x0000020000AABB00;
    bool factionOk = accessor.WriteFaction(localFaction);
    TestAssert(factionOk, "WriteFaction succeeded");
    TestAssert(accessor.GetFactionPtr() == localFaction, "Faction set to local player's faction");

    // Step 6: Server sends position update → interpolation → write to game object
    kmp::Vec3 newPos{-51180.0f, 1605.0f, 2720.0f};
    bool posOk = accessor.WritePosition(newPos);
    TestAssert(posOk, "WritePosition succeeded");

    kmp::Vec3 readPos = accessor.GetPosition();
    TestAssert(FloatEq(readPos.x, -51180.0f), "Updated position.x in game memory");
    TestAssert(FloatEq(readPos.y, 1605.0f),   "Updated position.y in game memory");
    TestAssert(FloatEq(readPos.z, 2720.0f),   "Updated position.z in game memory");

    printf("    Full flow: spawn -> register -> link -> rename -> faction -> move\n");
    printf("    Character '%s' at (%.1f, %.1f, %.1f) with faction 0x%llX\n",
           readName.c_str(), readPos.x, readPos.y, readPos.z, (unsigned long long)localFaction);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 13: Multiple players — simulate 4-player session
// ═══════════════════════════════════════════════════════════════════════════
static void Test_MultiPlayerSession() {
    printf("\n=== Test: 4-Player Session Simulation ===\n");

    kmp::EntityRegistry registry;
    kmp::Interpolation interp;
    auto& offsets = kmp::game::GetOffsets().character;

    // Local player (us) — Player 1
    FakeCharacter localChar;
    localChar.SetSSOString(offsets.name, "LocalPlayer");
    localChar.SetVec3(offsets.position, 0.0f, 100.0f, 0.0f);
    kmp::EntityID localId = registry.Register(localChar.Ptr(), kmp::EntityType::PlayerCharacter, 1);

    // Remote players 2, 3, 4
    FakeCharacter remote2, remote3, remote4;
    const char* names[] = {"Alice", "Bob", "Charlie"};
    float xPositions[] = {100.0f, 200.0f, 300.0f};

    kmp::EntityID remoteIds[3];
    FakeCharacter* remotes[] = {&remote2, &remote3, &remote4};

    for (int i = 0; i < 3; i++) {
        kmp::Vec3 pos{xPositions[i], 100.0f, 0.0f};
        remoteIds[i] = registry.RegisterRemote(50 + i, kmp::EntityType::PlayerCharacter, 2 + i, pos);

        // Simulate spawn replay: set up game object
        remotes[i]->SetSSOString(offsets.name, "Greenlander");
        remotes[i]->SetVec3(offsets.position, xPositions[i], 100.0f, 0.0f);
        registry.SetGameObject(remoteIds[i], remotes[i]->Ptr());

        // Rename to player name
        kmp::game::CharacterAccessor acc(remotes[i]->Ptr());
        acc.WriteName(names[i]);

        // Add interpolation snapshots
        interp.AddSnapshot(remoteIds[i], 1.0f, pos, kmp::Quat{});
    }

    TestAssert(registry.GetEntityCount() == 4, "4 entities total in registry");
    TestAssert(registry.GetRemoteCount() == 3, "3 remote entities");
    TestAssert(registry.GetSpawnedRemoteCount() == 3, "3 spawned remotes");

    // Verify all names
    for (int i = 0; i < 3; i++) {
        kmp::game::CharacterAccessor acc(remotes[i]->Ptr());
        TestAssert(acc.GetName() == names[i], (std::string("Remote ") + names[i] + " name correct").c_str());
    }

    // Simulate movement: server sends new positions
    for (int i = 0; i < 3; i++) {
        kmp::Vec3 movedPos{xPositions[i] + 50.0f, 100.0f, 10.0f};
        interp.AddSnapshot(remoteIds[i], 1.1f, movedPos, kmp::Quat{});

        // Write to game memory (what OnGameTick does after interpolation)
        kmp::game::CharacterAccessor acc(remotes[i]->Ptr());
        acc.WritePosition(movedPos);
    }

    // Verify all moved
    for (int i = 0; i < 3; i++) {
        kmp::game::CharacterAccessor acc(remotes[i]->Ptr());
        kmp::Vec3 pos = acc.GetPosition();
        TestAssert(FloatEq(pos.x, xPositions[i] + 50.0f), (std::string(names[i]) + " moved to correct X").c_str());
    }

    // Simulate one player disconnecting
    registry.Unregister(remoteIds[1]); // Bob leaves
    interp.RemoveEntity(remoteIds[1]);
    TestAssert(registry.GetRemoteCount() == 2, "After Bob leaves: 2 remotes");

    // Clear all remotes (full disconnect)
    size_t cleared = registry.ClearRemoteEntities();
    TestAssert(cleared == 2, "ClearRemoteEntities removed 2 remaining");
    TestAssert(registry.GetRemoteCount() == 0, "No remotes after clear");
    // Local entity should still exist
    TestAssert(registry.GetEntityCount() == 1, "Local entity survives remote clear");

    printf("    4-player session: spawn, move, disconnect, cleanup — all verified\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Host assignment protocol tests
// ═══════════════════════════════════════════════════════════════════════════
static void TestHostAssignmentProtocol() {
    printf("\nHost assignment protocol tests:\n");

    // The enum value is load-bearing: clients of different builds must agree on 0x92.
    TestAssert(static_cast<uint8_t>(kmp::MessageType::S2C_HostAssignment) == 0x92,
               "S2C_HostAssignment enum value is 0x92");

    // Struct layout: one PlayerID field, no padding surprises.
    TestAssert(sizeof(kmp::MsgHostAssignment) == sizeof(kmp::PlayerID),
               "MsgHostAssignment is exactly sizeof(PlayerID)");

    // Round-trip: write a MsgHostAssignment, read it back, compare.
    kmp::MsgHostAssignment original{};
    original.newHostPlayerId = 42;
    uint8_t buf[sizeof(original)];
    memcpy(buf, &original, sizeof(original));
    kmp::MsgHostAssignment copy{};
    memcpy(&copy, buf, sizeof(copy));
    TestAssert(copy.newHostPlayerId == 42, "MsgHostAssignment round-trips through byte buffer");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Loopback detection test
// ═══════════════════════════════════════════════════════════════════════════
// We don't link to ENet here. Re-implement the predicate inline with the exact
// same logic the server uses, then test the bit pattern.
static bool TestOnly_IsLoopbackHost(uint32_t hostNetOrder) {
    // ENet stores address.host in network byte order. 127.0.0.1 in host byte
    // order is 0x7F000001; in network byte order on little-endian systems, the
    // bytes reverse to 0x0100007F.
    return hostNetOrder == 0x0100007Fu;
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 16: MsgLimbHealth packet round-trip — 7 limb health floats
// ═══════════════════════════════════════════════════════════════════════════
static void Test_LimbHealthRoundTrip() {
    printf("\n=== Test: MsgLimbHealth Round-Trip ===\n");

    kmp::PacketWriter writer;
    writer.WriteHeader(kmp::MessageType::C2S_LimbHealth);

    kmp::MsgLimbHealth msg{};
    msg.entityId = 42;
    msg.health[0] = 100.f;  // Head
    msg.health[1] = 80.f;   // Chest
    msg.health[2] = 90.f;   // Stomach
    msg.health[3] = 70.f;   // LeftArm
    msg.health[4] = 65.f;   // RightArm
    msg.health[5] = 85.f;   // LeftLeg
    msg.health[6] = 75.f;   // RightLeg
    writer.WriteRaw(&msg, sizeof(msg));

    kmp::PacketReader reader(writer.Data(), writer.Size());
    kmp::PacketHeader header;
    TestAssert(reader.ReadHeader(header), "Reads header");
    TestAssert(header.type == kmp::MessageType::C2S_LimbHealth, "Message type is C2S_LimbHealth");

    kmp::MsgLimbHealth readMsg{};
    TestAssert(reader.ReadRaw(&readMsg, sizeof(readMsg)), "Reads MsgLimbHealth payload");
    TestAssert(readMsg.entityId == 42, "Entity ID matches");
    TestAssert(FloatEq(readMsg.health[0], 100.f), "Health[Head]=100");
    TestAssert(FloatEq(readMsg.health[1], 80.f), "Health[Chest]=80");
    TestAssert(FloatEq(readMsg.health[6], 75.f), "Health[RightLeg]=75");

    printf("    LimbHealth round-trip: entity=%u health[0..6] verified\n", readMsg.entityId);
}

static void TestLoopbackDetection() {
    printf("\nLoopback detection tests:\n");

    // 127.0.0.1 in network byte order
    TestAssert(TestOnly_IsLoopbackHost(0x0100007Fu), "127.0.0.1 detected as loopback");

    // 192.168.1.1 (LAN) — should NOT be loopback
    TestAssert(!TestOnly_IsLoopbackHost(0x0101A8C0u), "192.168.1.1 detected as non-loopback");

    // 0.0.0.0 (INADDR_ANY) — should NOT be loopback
    TestAssert(!TestOnly_IsLoopbackHost(0u), "0.0.0.0 detected as non-loopback");

    // 127.0.0.2 — different loopback-range IP, should still be non-match for our exact check
    // (spec uses exact 127.0.0.1 match; other loopback IPs are rare in practice)
    TestAssert(!TestOnly_IsLoopbackHost(0x0200007Fu), "127.0.0.2 not matched (exact 127.0.0.1 only)");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 17: MsgInventorySnapshot packet round-trip — entity + N items
// ═══════════════════════════════════════════════════════════════════════════
static void Test_InventorySnapshotRoundTrip() {
    printf("\n=== Test: MsgInventorySnapshot Round-Trip ===\n");

    kmp::PacketWriter writer;
    writer.WriteHeader(kmp::MessageType::C2S_InventorySnapshot);

    kmp::MsgInventorySnapshot msg{};
    msg.entityId = 7;
    msg.itemCount = 3;
    writer.WriteRaw(&msg, sizeof(msg));

    const kmp::MsgInventorySnapshotItem items[3] = {
        {1001, 2},
        {1002, 1},
        {1003, 5},
    };
    writer.WriteRaw(items, sizeof(items));

    kmp::PacketReader reader(writer.Data(), writer.Size());
    kmp::PacketHeader header;
    TestAssert(reader.ReadHeader(header), "Reads header");
    TestAssert(header.type == kmp::MessageType::C2S_InventorySnapshot, "Message type is C2S_InventorySnapshot");

    kmp::MsgInventorySnapshot readMsg{};
    TestAssert(reader.ReadRaw(&readMsg, sizeof(readMsg)), "Reads MsgInventorySnapshot header");
    TestAssert(readMsg.entityId == 7, "Entity ID matches");
    TestAssert(readMsg.itemCount == 3, "Item count matches");

    kmp::MsgInventorySnapshotItem readItems[3]{};
    TestAssert(reader.ReadRaw(readItems, sizeof(readItems)), "Reads item array");
    TestAssert(readItems[0].itemTemplateId == 1001 && readItems[0].quantity == 2, "Item[0] matches");
    TestAssert(readItems[1].itemTemplateId == 1002 && readItems[1].quantity == 1, "Item[1] matches");
    TestAssert(readItems[2].itemTemplateId == 1003 && readItems[2].quantity == 5, "Item[2] matches");

    printf("    Snapshot round-trip: entity=%u items=%u verified\n", readMsg.entityId, readMsg.itemCount);
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 18: Prediction reconciliation decision — ShouldSnapToServer
// ═══════════════════════════════════════════════════════════════════════════
static void Test_ReconcileDecision() {
    printf("\n=== Test: Reconcile Decision (ShouldSnapToServer) ===\n");

    constexpr float kThreshold = 5.0f;

    // Identical positions → no snap
    kmp::Vec3 a(10.f, 20.f, 30.f);
    TestAssert(!kmp::AuthorityValidator::ShouldSnapToServer(a, a, kThreshold),
               "Identical positions → no snap");

    // Small divergence (within threshold) → tolerate (no snap)
    kmp::Vec3 near1(10.f, 20.f, 30.f);
    kmp::Vec3 near2(12.f, 20.f, 30.f); // 2m away
    TestAssert(!kmp::AuthorityValidator::ShouldSnapToServer(near1, near2, kThreshold),
               "2m divergence < 5m threshold → no snap");

    // Exactly at threshold → no snap (strict >)
    kmp::Vec3 edge1(0.f, 0.f, 0.f);
    kmp::Vec3 edge2(5.f, 0.f, 0.f); // exactly 5m
    TestAssert(!kmp::AuthorityValidator::ShouldSnapToServer(edge1, edge2, kThreshold),
               "Exact threshold → no snap (strict)");

    // Large divergence → snap
    kmp::Vec3 far1(0.f, 0.f, 0.f);
    kmp::Vec3 far2(50.f, 0.f, 0.f); // 50m away
    TestAssert(kmp::AuthorityValidator::ShouldSnapToServer(far1, far2, kThreshold),
               "50m divergence > 5m threshold → snap");

    // 3D divergence
    kmp::Vec3 far3(10.f, 10.f, 10.f);
    TestAssert(kmp::AuthorityValidator::ShouldSnapToServer(far1, far3, kThreshold),
               "3D divergence → snap");

    printf("    Reconcile decision: snap/tolerate logic verified\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 19: Authority routing — ValidateInboundSnapshot decision tree
// ═══════════════════════════════════════════════════════════════════════════
// Covers the ReconcileLocal / ApplyRemote / Reject / Queue branches that
// previously had zero test coverage (reviewer finding M5; C1/I2 regressions).
static void Test_AuthorityRouting() {
    printf("\n=== Test: Authority Routing (ValidateInboundSnapshot) ===\n");

    kmp::EntityRegistry registry;
    constexpr uint32_t myPlayerId = 1;
    constexpr uint32_t remotePlayerId = 5;

    // Own entity registered locally (owner = myPlayerId)
    void* myChar = reinterpret_cast<void*>(0x100000); // valid heap-ish pointer
    kmp::EntityID myEntity = registry.Register(myChar, kmp::EntityType::NPC, myPlayerId);
    TestAssert(myEntity != kmp::INVALID_ENTITY, "Own entity registered");

    // Remote entity registered (owner = remotePlayerId)
    kmp::Vec3 remotePos(10.f, 20.f, 30.f);
    kmp::EntityID remoteEntity = registry.RegisterRemote(100, kmp::EntityType::NPC, remotePlayerId, remotePos);
    TestAssert(remoteEntity == 100, "Remote entity registered");

    // Case 1: own entity + sourcePlayer=0 (server echo) → ReconcileLocal (C1 fix)
    {
        kmp::CharacterPosition pos{};
        pos.entityId = myEntity;
        pos.generation = 0;
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, 0, myPlayerId, registry);
        TestAssert(d == kmp::SnapshotDecision::ReconcileLocal,
                   "Own entity + server echo (src=0) → ReconcileLocal");
    }

    // Case 2: own entity + sourcePlayer=self → ReconcileLocal
    {
        kmp::CharacterPosition pos{};
        pos.entityId = myEntity;
        pos.generation = 0;
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, myPlayerId, myPlayerId, registry);
        TestAssert(d == kmp::SnapshotDecision::ReconcileLocal,
                   "Own entity + source=self → ReconcileLocal");
    }

    // Case 3: remote entity + sourcePlayer=0 (server broadcast) → ApplyRemote (I2 fix)
    {
        kmp::CharacterPosition pos{};
        pos.entityId = remoteEntity;
        pos.generation = 0;
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, 0, myPlayerId, registry);
        TestAssert(d == kmp::SnapshotDecision::ApplyRemote,
                   "Remote entity + server broadcast (src=0) → ApplyRemote (I2 fix)");
    }

    // Case 4: remote entity + sourcePlayer≠owner → still rejected (authority preserved)
    {
        kmp::CharacterPosition pos{};
        pos.entityId = remoteEntity;
        pos.generation = 0;
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, 3, myPlayerId, registry); // player 3 ≠ owner 5
        TestAssert(d == kmp::SnapshotDecision::RejectAuthorityViolation,
                   "Remote entity + wrong source → RejectAuthorityViolation (still enforced)");
    }

    // Case 5: unregistered entity → QueuePendingSpawn
    {
        kmp::CharacterPosition pos{};
        pos.entityId = 999;
        pos.generation = 0;
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, 0, myPlayerId, registry);
        TestAssert(d == kmp::SnapshotDecision::QueuePendingSpawn,
                   "Unregistered entity → QueuePendingSpawn");
    }

    // Case 6: stale generation → rejected
    {
        kmp::CharacterPosition pos{};
        pos.entityId = myEntity;
        pos.generation = 7; // mismatch (registry gen = 0)
        kmp::SnapshotDecision d = kmp::AuthorityValidator::ValidateInboundSnapshot(
            pos, 0, myPlayerId, registry);
        TestAssert(d == kmp::SnapshotDecision::RejectStaleGeneration,
                   "Stale generation → RejectStaleGeneration");
    }

    printf("    Authority routing: 6 decision branches verified\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 20: InventoryAccessor — item list, stack add/remove
// ═══════════════════════════════════════════════════════════════════════════
static void Test_InventoryAccessor() {
    printf("\n=== Test: InventoryAccessor ===\n");

    auto& inv = kmp::game::GetOffsets().inventory;
    auto& item = kmp::game::GetOffsets().item;

    FakeCharacter fake;          // fake inventory object
    FakeCharacter itemA, itemB;  // fake item objects
    uintptr_t list[2] = { itemA.Addr(), itemB.Addr() };

    fake.Set<int>(inv.itemCount, 2);
    fake.Set<uintptr_t>(inv.items, reinterpret_cast<uintptr_t>(list));
    fake.Set<int>(inv.width, 4);
    fake.Set<int>(inv.height, 3);

    itemA.Set<uint32_t>(item.templateId, 1001);
    itemA.Set<int>(item.stackCount, 2);
    itemB.Set<uint32_t>(item.templateId, 1002);
    itemB.Set<int>(item.stackCount, 5);

    kmp::game::InventoryAccessor accessor(fake.Ptr());
    TestAssert(accessor.IsValid(), "Inventory accessor valid");
    TestAssert(accessor.GetItemCount() == 2, "Item count reads 2");
    TestAssert(accessor.GetWidth() == 4, "Inventory width reads 4");
    TestAssert(accessor.GetHeight() == 3, "Inventory height reads 3");
    TestAssert(accessor.GetItem(0) == itemA.Addr(), "GetItem(0) returns item A");
    TestAssert(accessor.GetItem(1) == itemB.Addr(), "GetItem(1) returns item B");
    TestAssert(accessor.GetItem(2) == 0, "GetItem out of range returns 0");
    TestAssert(accessor.GetItem(-1) == 0, "GetItem negative index returns 0");

    // AddItem: finds matching stack and increments quantity
    TestAssert(accessor.AddItem(1001, 3), "AddItem finds existing stack");
    TestAssert(itemA.Get<int>(item.stackCount) == 5, "AddItem incremented stack 2->5");
    // AddItem: item type not in inventory -> false (no new item creation, C2 finding)
    TestAssert(!accessor.AddItem(9999, 1), "AddItem missing item returns false (no creation)");
    TestAssert(!accessor.AddItem(1001, 0), "AddItem with zero quantity rejected");
    TestAssert(!accessor.AddItem(1001, -2), "AddItem with negative quantity rejected");

    // RemoveItem: decrements stack, clamps at zero
    TestAssert(accessor.RemoveItem(1002, 2), "RemoveItem finds existing stack");
    TestAssert(itemB.Get<int>(item.stackCount) == 3, "RemoveItem decremented stack 5->3");
    TestAssert(accessor.RemoveItem(1002, 99), "RemoveItem removes more than stack");
    TestAssert(itemB.Get<int>(item.stackCount) == 0, "RemoveItem clamps quantity at 0");
    TestAssert(!accessor.RemoveItem(9999, 1), "RemoveItem missing item returns false");
    TestAssert(!accessor.RemoveItem(1001, 0), "RemoveItem with zero quantity rejected");

    // SetEquipment: equipment offset is runtime-probed (-1 in tests) -> graceful false
    TestAssert(!accessor.SetEquipment(kmp::EquipSlot::Hat, 1001),
               "SetEquipment with unknown offset returns false gracefully");

    // Invalid pointer -> all reads fail safely
    kmp::game::InventoryAccessor invalid(nullptr);
    TestAssert(!invalid.IsValid(), "Null inventory accessor invalid");
    TestAssert(invalid.GetItemCount() == 0, "Null accessor item count is 0");
    TestAssert(!invalid.AddItem(1001, 1), "Null accessor AddItem returns false");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 21: BuildingAccessor — name, position, health, state flags
// ═══════════════════════════════════════════════════════════════════════════
static void Test_BuildingAccessor() {
    printf("\n=== Test: BuildingAccessor ===\n");

    auto& b = kmp::game::GetOffsets().building;
    FakeCharacter fake;
    FakeCharacter factionBuf;

    fake.SetSSOString(b.name, "Storm House");
    fake.SetVec3(b.position, 100.0f, 200.0f, 300.0f);
    fake.Set<float>(b.health, 250.0f);
    fake.Set<float>(b.maxHealth, 1000.0f);
    fake.Set<bool>(b.isDestroyed, false);
    fake.Set<float>(b.buildProgress, 0.5f);
    fake.Set<bool>(b.isConstructed, false);

    kmp::game::BuildingAccessor accessor(fake.Ptr());
    TestAssert(accessor.GetName() == "Storm House", "Building name reads correctly");
    TestAssert(FloatEq(accessor.GetHealth(), 250.0f), "Building health reads 250");
    TestAssert(FloatEq(accessor.GetMaxHealth(), 1000.0f), "Building max health reads 1000");
    TestAssert(!accessor.IsDestroyed(), "Undamaged building not destroyed");
    TestAssert(FloatEq(accessor.GetBuildProgress(), 0.5f), "Build progress reads 0.5");
    TestAssert(!accessor.IsConstructed(), "Incomplete building not constructed");

    kmp::Vec3 pos = accessor.GetPosition();
    TestAssert(FloatEq(pos.x, 100.0f) && FloatEq(pos.y, 200.0f) && FloatEq(pos.z, 300.0f),
               "Building position reads correctly");

    // Pointer fields are sanitized (must be plausible heap pointers)
    fake.Set<uintptr_t>(b.ownerFaction, 0x1234);
    fake.Set<uintptr_t>(b.inventory, 0x1234);
    TestAssert(accessor.GetOwnerFaction() == 0, "Small owner faction pointer sanitized");
    TestAssert(accessor.GetInventoryPtr() == 0, "Small inventory pointer sanitized");

    fake.Set<uintptr_t>(b.ownerFaction, factionBuf.Addr());
    fake.Set<uintptr_t>(b.inventory, factionBuf.Addr());
    TestAssert(accessor.GetOwnerFaction() == factionBuf.Addr(), "Owner faction pointer reads");
    TestAssert(accessor.GetInventoryPtr() == factionBuf.Addr(), "Inventory pointer reads");

    // Destroyed flag path
    fake.Set<bool>(b.isDestroyed, true);
    TestAssert(accessor.IsDestroyed(), "Destroyed flag reads true");

    // Constructed flag path
    fake.Set<bool>(b.isConstructed, true);
    TestAssert(accessor.IsConstructed(), "Constructed flag reads true");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 22: FactionAccessor — name, member count, flags
// ═══════════════════════════════════════════════════════════════════════════
static void Test_FactionAccessor() {
    printf("\n=== Test: FactionAccessor ===\n");

    auto& f = kmp::game::GetOffsets().faction;
    FakeCharacter fake;
    fake.SetSSOString(f.name, "Holy Nation");
    fake.Set<int>(f.memberCount, 3);
    fake.Set<bool>(f.isPlayerFaction, true);
    fake.Set<int>(f.money, 5000);

    kmp::game::FactionAccessor accessor(fake.Ptr());
    TestAssert(accessor.GetName() == "Holy Nation", "Faction name reads correctly");
    TestAssert(accessor.GetMemberCount() == 3, "Faction member count reads 3");
    TestAssert(accessor.IsPlayerFaction(), "Player faction flag reads true");
    TestAssert(accessor.GetMoney() == 5000, "Faction money reads 5000");

    // Sanity clamps
    fake.Set<int>(f.memberCount, 99999);
    TestAssert(accessor.GetMemberCount() == 0, "Unreasonable member count clamped to 0");
    fake.Set<int>(f.memberCount, -5);
    TestAssert(accessor.GetMemberCount() == 0, "Negative member count clamped to 0");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 23: StatsAccessor — skill reads and sanity clamp
// ═══════════════════════════════════════════════════════════════════════════
static void Test_StatsAccessor() {
    printf("\n=== Test: StatsAccessor ===\n");

    auto& s = kmp::game::GetOffsets().stats;
    FakeCharacter fake;
    fake.Set<float>(s.meleeAttack, 25.5f);
    fake.Set<float>(s.strength, 40.0f);
    fake.Set<float>(s.medic, 12.0f);
    fake.Set<float>(s.meleeDefence, 5000.0f); // out of sane range -> clamped

    kmp::game::StatsAccessor accessor(fake.Ptr());
    TestAssert(FloatEq(accessor.GetMeleeAttack(), 25.5f), "Melee attack reads 25.5");
    TestAssert(FloatEq(accessor.GetStrength(), 40.0f), "Strength reads 40");
    TestAssert(FloatEq(accessor.GetMedic(), 12.0f), "Medic reads 12");
    TestAssert(FloatEq(accessor.GetMeleeDefence(), 0.0f), "Out-of-range stat clamped to 0");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 24: SquadAccessor — name, members, faction, flags
// ═══════════════════════════════════════════════════════════════════════════
static void Test_SquadAccessor() {
    printf("\n=== Test: SquadAccessor ===\n");

    auto& sq = kmp::game::GetOffsets().squad;
    // Name reads via SSO inline buffer. Note: memberList (0x28) overlaps the
    // std::string capacity field (name+0x18), so only an empty squad (list=0)
    // takes the inline read path.
    FakeCharacter namedSquad;
    namedSquad.SetSSOString(sq.name, "Squad Alpha");
    namedSquad.Set<int>(sq.memberCount, 0);
    namedSquad.Set<uintptr_t>(sq.memberList, 0);
    kmp::game::SquadAccessor named(namedSquad.Ptr());
    TestAssert(named.GetName() == "Squad Alpha", "Empty squad name reads correctly");
    TestAssert(named.GetMemberCount() == 0, "Empty squad member count is 0");

    FakeCharacter fake;
    FakeCharacter memberA, memberB;
    FakeCharacter factionBuf;
    uintptr_t members[2] = { memberA.Addr(), memberB.Addr() };

    fake.Set<int>(sq.memberCount, 2);
    fake.Set<uintptr_t>(sq.memberList, reinterpret_cast<uintptr_t>(members));
    fake.Set<uintptr_t>(sq.factionId, factionBuf.Addr());
    fake.Set<bool>(sq.isPlayerSquad, true);

    kmp::game::SquadAccessor accessor(fake.Ptr());
    TestAssert(accessor.GetMemberCount() == 2, "Squad member count reads 2");
    TestAssert(accessor.GetMember(0) == memberA.Addr(), "GetMember(0) returns member A");
    TestAssert(accessor.GetMember(1) == memberB.Addr(), "GetMember(1) returns member B");
    TestAssert(accessor.GetMember(2) == 0, "GetMember out of range returns 0");
    TestAssert(accessor.GetFactionPtr() == factionBuf.Addr(), "Squad faction pointer reads");
    TestAssert(accessor.IsPlayerSquad(), "Player squad flag reads true");

    fake.Set<int>(sq.memberCount, 300);
    TestAssert(accessor.GetMemberCount() == 0, "Unreasonable squad member count clamped to 0");
}

// ═══════════════════════════════════════════════════════════════════════════
//  TEST 25: CommandRegistry — parse, dispatch, error handling
// ═══════════════════════════════════════════════════════════════════════════
static void Test_CommandRegistry() {
    printf("\n=== Test: CommandRegistry ===\n");

    auto& reg = kmp::CommandRegistry::Get();
    reg.Register("test_echo", "Echo args", [](const kmp::CommandArgs& a) {
        std::string out = a.command;
        for (const auto& arg : a.args) {
            out += "|" + arg;
        }
        return out;
    });
    reg.Register("test_fail", "Throws", [](const kmp::CommandArgs&) -> std::string {
        throw std::runtime_error("boom");
    });

    TestAssert(reg.Execute("") == "", "Empty input returns empty string");
    TestAssert(reg.Execute("hello") == "", "Non-slash input returns empty string");
    TestAssert(reg.Execute("/") == "Empty command.", "Bare slash reports empty command");
    TestAssert(reg.Execute("/test_echo a b c") == "test_echo|a|b|c",
               "Args parsed and passed to handler");
    TestAssert(reg.Execute("/test_echo") == "test_echo", "No-arg command works");

    std::string unknown = reg.Execute("/nope");
    TestAssert(unknown.find("Unknown command: /nope") == 0, "Unknown command reported");

    std::string err = reg.Execute("/test_fail");
    TestAssert(err.find("Command error:") == 0, "Handler exception caught and reported");

    bool found = false;
    for (const auto* def : reg.GetAll()) {
        if (def->name == "test_echo") found = true;
    }
    TestAssert(found, "GetAll lists registered commands");
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════
int main() {
    printf("======================================\n");
    printf("  KenshiMP Unit Test Suite\n");
    printf("  Tests game-facing code with fake memory\n");
    printf("======================================\n");

    Test_ReadPosition();
    Test_ReadName();
    Test_WriteName();
    Test_WritePosition();
    Test_FactionReadWrite();
    Test_EntityRegistry();
    Test_RemoteEntityLifecycle();
    Test_EntityRemap();
    Test_Interpolation();
    Test_PacketRoundTrip();
    Test_SpawnPacketRoundTrip();
    Test_FullSpawnFlow();
    Test_MultiPlayerSession();
    Test_LimbHealthRoundTrip();
    Test_InventorySnapshotRoundTrip();
    Test_ReconcileDecision();
    Test_AuthorityRouting();
    TestHostAssignmentProtocol();
    TestLoopbackDetection();
    Test_InventoryAccessor();
    Test_BuildingAccessor();
    Test_FactionAccessor();
    Test_StatsAccessor();
    Test_SquadAccessor();
    Test_CommandRegistry();

    printf("\n======================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("======================================\n");

    if (g_failed > 0) {
        printf("\nSome tests FAILED!\n");
    } else {
        printf("\nAll tests PASSED!\n");
    }

    return g_failed > 0 ? 1 : 0;
}

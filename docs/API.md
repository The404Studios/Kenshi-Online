# KenshiMP Public API Reference

**Version:** 1.0.0  
**Target Audience:** Modders, contributors, and external developers

This document describes the public-facing API for the Kenshi-Online multiplayer mod. Use these interfaces to interact with the multiplayer system, extend functionality, or build integrations.

---

## Table of Contents

1. [Core System](#1-core-system)
2. [Entity Registry](#2-entity-registry)
3. [Spawn Manager](#3-spawn-manager)
4. [Network Client/Server](#4-network-clientserver)
5. [Player Controller](#5-player-controller)
6. [Kenshi SDK](#6-kenshi-sdk)
7. [Protocol & Messaging](#7-protocol--messaging)
8. [Common Types](#8-common-types)
9. [Usage Examples](#9-usage-examples)

---

## 1. Core System

**Header:** `KenshiMP.Core/core.h`  
**Namespace:** `kmp`

The `Core` class is the central orchestrator for the multiplayer system. Access it via the singleton pattern.

### Class: `Core`

#### Singleton Access
```cpp
static Core& Get();
```
Returns the global Core instance.

#### Lifecycle
```cpp
bool Initialize();
void Shutdown();
```
- **Initialize()**: Scans patterns, installs hooks, initializes network and UI. Call once at DLL load.
- **Shutdown()**: Cleans up resources, disables hooks, disconnects from server.

#### Component Accessors
```cpp
PatternScanner&      GetScanner();
GameFunctions&       GetGameFunctions();
NetworkClient&       GetClient();
EntityRegistry&      GetEntityRegistry();
Interpolation&       GetInterpolation();
SpawnManager&        GetSpawnManager();
PlayerController&    GetPlayerController();
LoadingOrchestrator& GetLoadingOrch();
LobbyManager&        GetLobbyManager();
Overlay&             GetOverlay();
NativeHud&           GetNativeHud();
ClientConfig&        GetConfig();
sdk::KenshiSDK&      GetSDK();
sdk::VisualProxy&    GetVisualProxy();
GameCommandQueue&    GetCommandQueue();
TaskOrchestrator&    GetOrchestrator();
```
Access individual subsystems. Thread-safe references.

#### State Queries
```cpp
bool IsConnected() const;
bool IsHost() const;
bool IsGameLoaded() const;
bool IsLoading() const;
bool IsSteamVersion() const;

PlayerID GetLocalPlayerId() const;
PlayerID GetHostPlayerId() const;

ClientPhase GetClientPhase() const;
```
- **IsConnected()**: True after handshake success, false after disconnect.
- **IsHost()**: True if local player is the session host.
- **IsGameLoaded()**: True after world load and character iteration is ready.
- **GetClientPhase()**: Current lifecycle phase (Startup, MainMenu, Loading, GameReady, Connecting, Connected).

#### Phase Transitions
```cpp
void TransitionTo(ClientPhase newPhase);
void OnGameLoaded();
void OnLoadingGapDetected();
void OnGameTick(float deltaTime);
```
- **TransitionTo()**: Advance client state machine (called internally by hooks).
- **OnGameLoaded()**: Called when world is ready (enables hooks, starts spawn processing).
- **OnGameTick()**: Main update loop (polls entities, processes spawn queue, sends packets).

#### Entity Management
```cpp
void SendExistingEntitiesToServer();
void FindAndClaimModCharacters();
void* FindModCharacterBySlot(int slot);
void RequestEntityRescan();
```
- **SendExistingEntitiesToServer()**: After handshake, scan local characters and register them.
- **FindAndClaimModCharacters()**: Scan for mod-defined "Player 1" through "Player 16" templates.
- **RequestEntityRescan()**: Trigger a re-scan of the game world (called after faction bootstrap).

#### Teleportation
```cpp
void SetHostSpawnPoint(const Vec3& pos);
bool HasHostSpawnPoint() const;
Vec3 GetHostSpawnPoint() const;

bool TeleportToNearestRemotePlayer();
void ForceSpawnRemotePlayers();
```
- **SetHostSpawnPoint()**: Host sets spawn position for joiners.
- **TeleportToNearestRemotePlayer()**: Instant teleport to nearest remote character. Returns true on success.
- **ForceSpawnRemotePlayers()**: Debug command to bypass all spawn gates (use via `/forcespawn` chat).

---

## 2. Entity Registry

**Header:** `KenshiMP.Core/sync/entity_registry.h`  
**Namespace:** `kmp`

The entity registry tracks all game entities (local and remote) with network IDs, ownership, authority, and state.

### Class: `EntityRegistry`

#### Registration
```cpp
EntityID Register(void* gameObject, EntityType type, PlayerID owner = 0);
EntityID RegisterRemote(EntityID netId, EntityType type, PlayerID owner, const Vec3& pos);
```
- **Register()**: Register a local game object. Returns assigned network ID.
- **RegisterRemote()**: Reserve a slot for a remote entity (pending spawn).

#### Lookup
```cpp
void* GetGameObject(EntityID netId) const;
EntityID GetNetId(void* gameObject) const;
std::optional<EntityInfo> GetInfo(EntityID netId) const;
```
- **GetGameObject()**: Get game pointer for a network ID. Returns `nullptr` if not found.
- **GetNetId()**: Get network ID for a game pointer. Returns `INVALID_ENTITY` if not found.
- **GetInfo()**: Get a thread-safe copy of entity metadata (position, health, owner, authority).

#### State Updates
```cpp
void SetGameObject(EntityID netId, void* gameObject);
void UpdatePosition(EntityID netId, const Vec3& pos);
void UpdateRotation(EntityID netId, const Quat& rot);
void UpdateOwner(EntityID netId, PlayerID newOwner);
void UpdateEquipment(EntityID netId, int slot, uint32_t itemTemplateId);
void UpdateLimbHealth(EntityID netId, const float health[7]);
void UpdateStatusEffect(EntityID netId, uint8_t effectType, bool active);
```
All updates are thread-safe and atomic.

#### Dirty Flags
```cpp
void SetDirtyFlags(EntityID netId, uint16_t flags);
void ClearDirtyFlags(EntityID netId, uint16_t mask);
```
Mark entities for selective replication (position, health, equipment, etc.).

#### Entity Remapping
```cpp
bool RemapEntityId(EntityID oldId, EntityID newId);
```
Remap a local temporary ID to a server-assigned ID. Preserves all state and game object linkage.

#### Spatial Queries
```cpp
EntityID FindLocalEntityNear(const Vec3& pos, PlayerID owner, float maxDist = 5.0f) const;
std::vector<EntityID> GetPlayerEntities(PlayerID playerId) const;
std::vector<EntityID> GetEntitiesInZone(const ZoneCoord& zone) const;
std::vector<EntityID> GetRemoteEntities() const;
```
- **FindLocalEntityNear()**: Find a local entity near a position (for spawn hijacking).
- **GetPlayerEntities()**: Get all entities owned by a specific player.
- **GetRemoteEntities()**: Get all remote entities (for interpolation).

#### Cleanup
```cpp
void Unregister(EntityID netId);
void RemoveEntitiesInZone(const ZoneCoord& zone);
size_t ClearRemoteEntities();
void Clear();
```
- **ClearRemoteEntities()**: Remove all remote entities (call on disconnect). Returns count removed.
- **Clear()**: Full reset (rarely needed).

#### Authority Validation
```cpp
bool IsLocalOwned(EntityID netId, PlayerID myPlayerId) const;
bool IsRemoteOwned(EntityID netId, PlayerID myPlayerId) const;
bool IsServerOwned(EntityID netId) const;
bool IsValidGeneration(EntityID netId, uint32_t generation) const;
PlayerID GetOwnerPlayerId(EntityID netId) const;
```
Helper methods for authority checks (Phase 6 spec: prevents ghost control bugs).

#### Statistics
```cpp
size_t GetEntityCount() const;
size_t GetRemoteCount() const;
size_t GetSpawnedRemoteCount() const;
```
- **GetSpawnedRemoteCount()**: Count of remote entities with linked game objects.

### Struct: `EntityInfo`

Full entity metadata. Returned by `GetInfo()`.

```cpp
struct EntityInfo {
    EntityID      netId;
    uint32_t      generation;
    void*         gameObject;
    EntityType    type;
    PlayerID      ownerPlayerId;

    EntityState   state;          // Inactive, Spawning, Active, Despawning, Frozen
    AuthorityType authority;      // None, Server, Player, Transferring
    LocalAuthorityState localState;

    ZoneCoord     zone;
    Vec3          lastPosition;
    Quat          lastRotation;
    Vec3          velocity;

    float         health;
    LimbHealth    limbs;
    uint8_t       statusEffects[5];
    CombatInfo    combat;
    uint8_t       moveSpeed;
    uint8_t       animState;

    uint64_t      lastUpdateTick;
    bool          isRemote;
    uint32_t      lastEquipment[14];
    uint16_t      dirtyFlags;
};
```

---

## 3. Spawn Manager

**Header:** `KenshiMP.Core/game/spawn_manager.h`  
**Namespace:** `kmp`

Handles remote player character spawning. Captures the game's RootObjectFactory, scans GameData templates, and orchestrates spawn requests.

### Class: `SpawnManager`

#### Spawn Requests
```cpp
void QueueSpawn(const SpawnRequest& request);
```
- **QueueSpawn()**: Thread-safe. Called from network thread when `S2C_EntitySpawn` arrives.
- Spawn processing is handled by the in-place replay in `entity_hooks.cpp` (Hook_CharacterCreate); `ProcessSpawnQueue()` was removed in 2026-08-01 cleanup (dead code).

#### Template Database
```cpp
void* FindTemplate(const std::string& name) const;
void* GetDefaultTemplate() const;
void* GetCharacterSourcedTemplate() const;
void* GetModTemplate(int playerSlot) const;

size_t GetTemplateCount() const;
size_t GetFactoryTemplateCount() const;
size_t GetCharacterTemplateCount() const;
int GetModTemplateCount() const;
```
- **FindTemplate()**: Lookup a GameData template by name (e.g., "Greenlander").
- **GetModTemplate()**: Get mod template for player slot 0-15 (from kenshi-online.mod).
- **GetCharacterTemplateCount()**: Count of faction-validated character templates (safe to spawn).

#### Spawn Pathways
```cpp
void* SpawnCharacterDirect(const Vec3* desiredPosition = nullptr, int modSlot = 0);
void* SpawnWithModTemplate(int playerSlot, const Vec3& position);
```
- **SpawnCharacterDirect()**: Fallback spawn using saved request struct (wrong appearance but functional).
- **SpawnWithModTemplate()**: Spawn with correct appearance using kenshi-online.mod template.
- `ProcessSpawnQueueFromHook()` was removed in 2026-08-01 cleanup (dead code).

#### Readiness
```cpp
bool IsReady() const;
bool VerifyReadiness() const;
```
- **IsReady()**: True if factory captured.
- **VerifyReadiness()**: Log detailed status of all spawn paths.

#### Callbacks
```cpp
void SetFactory(void* factory);
void SetOrigProcess(FactoryProcessFn fn);
void SetOnSpawnedCallback(std::function<void(EntityID, void*)> cb);
void OnGameCharacterCreated(void* factory, void* gameData, void* character);
```
- **OnGameCharacterCreated()**: Called by entity_hooks on every character spawn (builds template database).
- **SetOnSpawnedCallback()**: Register callback to link spawned character to entity registry.

#### Queue Management
```cpp
bool HasPendingSpawns() const;
size_t GetPendingSpawnCount() const;
void ClearSpawnQueue();
int ClearSpawnsForOwner(PlayerID owner);
bool PopNextSpawn(SpawnRequest& outReq);
void RequeueSpawn(const SpawnRequest& req);
```
- **ClearSpawnsForOwner()**: Remove pending spawns for a disconnected player.

#### Heap Scanning
```cpp
void ScanGameDataHeap();
void FindModTemplates();
static std::string ReadKenshiString(uintptr_t addr);
```
- **ScanGameDataHeap()**: One-time scan after game loads (discovers templates).
- **FindModTemplates()**: Extract mod player templates from heap scan results.

### Struct: `SpawnRequest`

```cpp
struct SpawnRequest {
    EntityID    netId;
    PlayerID    owner;
    EntityType  type;
    std::string templateName;
    Vec3        position;
    Quat        rotation;
    uint32_t    templateId;
    uint32_t    factionId;
    uint32_t    retryCount;

    bool        hasExtendedState;
    float       health[7];
    bool        alive;
};
```

---

## 4. Network Client/Server

### Client API

**Header:** `KenshiMP.Core/net/client.h`  
**Namespace:** `kmp`

### Class: `NetworkClient`

#### Lifecycle
```cpp
bool Initialize();
void Shutdown();

bool Connect(const std::string& address, uint16_t port);       // Blocking (legacy)
bool ConnectAsync(const std::string& address, uint16_t port); // Non-blocking
void Disconnect();
void Update(); // Pump ENet events - call frequently
```
- **ConnectAsync()**: Preferred. Returns immediately, connection completes in background.
- **Update()**: Call from network thread at 20-30 Hz.

#### Send Methods
```cpp
void SendReliable(const uint8_t* data, size_t len);           // Channel 0 (ordered)
void SendReliableUnordered(const uint8_t* data, size_t len);  // Channel 1 (combat)
void SendUnreliable(const uint8_t* data, size_t len);         // Channel 2 (movement)
```

#### Packet Handling
```cpp
using PacketCallback = std::function<void(const uint8_t* data, size_t size, int channel)>;
void SetPacketCallback(PacketCallback cb);
```
Register a callback for incoming packets.

#### State
```cpp
bool IsConnected() const;
bool IsConnecting() const;
uint32_t GetPing() const;
const std::string& GetServerAddress() const;
uint16_t GetServerPort() const;
```

### Server API

**Header:** `KenshiMP.Server/server.h`  
**Namespace:** `kmp`

### Class: `GameServer`

#### Lifecycle
```cpp
bool Start(const ServerConfig& config);
void Stop();
void Update(float deltaTime);
```
- **Start()**: Binds ENet host, optionally sets up UPnP port forwarding, connects to master server.
- **Update()**: Call at 20 ticks/sec. Handles packets, broadcasts positions, runs auto-save.

#### Admin Commands
```cpp
void KickPlayer(PlayerID id, const std::string& reason);
void BroadcastSystemMessage(const std::string& message);
void SaveWorld();
void LoadWorld();
void PrintStatus();
void PrintPlayers();
```

#### Broadcasting
```cpp
void Broadcast(const uint8_t* data, size_t len, int channel, uint32_t flags);
void BroadcastExcept(PlayerID exclude, const uint8_t* data, size_t len, int channel, uint32_t flags);
void SendTo(PlayerID id, const uint8_t* data, size_t len, int channel, uint32_t flags);
```

#### World State
```cpp
void BroadcastPositions();
void BroadcastTimeSync();
void BroadcastHostAssignment();
void BroadcastEntityHeartbeat();
void SendWorldSnapshot(ConnectedPlayer& player);
```

### Struct: `ServerEntity`

Server-side entity record.

```cpp
struct ServerEntity {
    EntityID    id;
    uint32_t    generation;
    EntityType  type;
    PlayerID    owner;

    EntityState   state;
    AuthorityType authority;
    uint16_t      dirtyFlags;

    Vec3        position;
    Quat        rotation;
    ZoneCoord   zone;

    uint32_t    templateId;
    uint32_t    factionId;
    std::string templateName;
    float       health[7];
    float       limbHealth[7];
    uint8_t     statusEffects[5];
    CombatInfo  combat;
    uint8_t     animState;
    uint8_t     moveSpeed;
    uint16_t    flags;
    bool        alive;
    float       buildProgress;
    uint32_t    equipment[14];
};
```

---

## 5. Player Controller

**Header:** `KenshiMP.Core/game/player_controller.h`  
**Namespace:** `kmp`

Manages local and remote player state, character renaming, faction fixing, and entity tracking.

### Class: `PlayerController`

#### Local Player
```cpp
void InitializeLocalPlayer(PlayerID localId, const std::string& playerName);
PlayerID GetLocalPlayerId() const;
const std::string& GetLocalPlayerName() const;
std::vector<EntityID> GetLocalSquadEntities() const;
void* GetPrimaryCharacter() const;
uintptr_t GetLocalFactionPtr() const;
void SetLocalFactionPtr(uintptr_t factionPtr);
```
- **InitializeLocalPlayer()**: Called after handshake. Scans local characters and registers them.
- **SetLocalFactionPtr()**: Captured from first local character (used for faction bootstrap).

#### Remote Players
```cpp
void RegisterRemotePlayer(PlayerID id, const std::string& name);
void RemoveRemotePlayer(PlayerID id);
bool OnRemoteCharacterSpawned(EntityID entityId, void* gameObject, PlayerID owner);
bool WriteGameDataNameForModLink(void* gameObject, PlayerID owner);
const RemotePlayerState* GetRemotePlayer(PlayerID id) const;
std::vector<RemotePlayerState> GetAllRemotePlayers() const;
```
- **RegisterRemotePlayer()**: Called on `S2C_PlayerJoined`.
- **OnRemoteCharacterSpawned()**: Renames spawned character to player name, fixes faction, adds visual proxy.
- **WriteGameDataNameForModLink()**: Writes player name to GameData template (mod-linked characters only).

#### Sync
```cpp
int GatherLocalEntityUpdates(float deltaTime);
void ApplyRemotePositionUpdate(EntityID entityId, const Vec3& pos, const Quat& rot, 
                                uint8_t moveSpeed, uint8_t animState);
```
- **GatherLocalEntityUpdates()**: Collect position/rotation for all local entities. Returns count of changed entities.
- **ApplyRemotePositionUpdate()**: Feed interpolation system with remote data.

#### Lifecycle
```cpp
void OnGameWorldLoaded();
void OnWorldSnapshotReceived(int entityCount);
void Reset();
```
- **Reset()**: Called on disconnect. Clears all remote player state.

### Struct: `RemotePlayerState`

```cpp
struct RemotePlayerState {
    PlayerID    playerId;
    std::string playerName;
    Vec3        lastKnownPosition;
    bool        hasSpawnedCharacter;
    uintptr_t   factionPtr;
    std::vector<EntityID> entities;
};
```

---

## 6. Kenshi SDK

**Header:** `KenshiMP.Core/sdk/kenshi_sdk.h`  
**Namespace:** `kmp::sdk`

Clean abstraction over game state. Polls game memory each tick. No hooks required for reading.

### Class: `KenshiSDK`

#### Lifecycle
```cpp
bool Initialize();
void Update();
```
- **Initialize()**: Called after game loads and globals are resolved.
- **Update()**: Call from `OnGameTick`. Polls all entities, builds snapshot, computes diff.

#### State Access
```cpp
WorldSnapshot GetCurrentSnapshot() const;
WorldDiff GetLastDiff() const;
bool GetEntityState(uintptr_t gamePtr, EntitySnapshot& out) const;
std::vector<uintptr_t> GetAllEntityPtrs() const;
size_t GetEntityCount() const;
```
- **GetCurrentSnapshot()**: Thread-safe copy of latest world state.
- **GetLastDiff()**: Delta since last Update() (added/removed/changed entities).

#### State Write
```cpp
bool WritePosition(uintptr_t gamePtr, const Vec3& pos);
bool WriteHealth(uintptr_t gamePtr, BodyPart part, float value);
bool WriteName(uintptr_t gamePtr, const std::string& name);
```
Direct game memory writes (use with caution).

#### Player Faction
```cpp
uintptr_t GetPlayerFactionPtr() const;
void SetPlayerFactionPtr(uintptr_t ptr);
```

#### Diagnostics
```cpp
uint32_t GetFrameNumber() const;
float GetLastPollTimeMs() const;
EntitySnapshot ReadEntity(uintptr_t charPtr) const;
```

### Struct: `EntitySnapshot`

Complete entity state at a point in time.

```cpp
struct EntitySnapshot {
    uintptr_t   gamePtr;
    Vec3        position;
    Quat        rotation;
    float       health[7];
    uintptr_t   factionPtr;
    uint32_t    factionId;
    std::string name;
    uint8_t     animState;
    float       moveSpeed;
    bool        alive;
    bool        playerControlled;

    uint16_t DiffAgainst(const EntitySnapshot& prev) const;
};
```

### Struct: `WorldSnapshot`

```cpp
struct WorldSnapshot {
    std::vector<EntitySnapshot> entities;
    float timeOfDay;
    float gameSpeed;
    uint32_t frameNumber;

    const EntitySnapshot* FindByPtr(uintptr_t ptr) const;
};
```

### Struct: `WorldDiff`

```cpp
struct WorldDiff {
    std::vector<EntityDelta>    changed;
    std::vector<uintptr_t>      added;
    std::vector<uintptr_t>      removed;
};

struct EntityDelta {
    uintptr_t gamePtr;
    uint16_t  dirtyFlags;
    EntitySnapshot snapshot;
};
```

---

## 7. Protocol & Messaging

**Header:** `KenshiMP.Common/include/kmp/protocol.h`  
**Namespace:** `kmp`

### Packet Serialization

#### Class: `PacketWriter`

```cpp
PacketWriter();
void WriteHeader(MessageType type, uint16_t seq = 0, uint32_t tick = 0);
void WriteU8(uint8_t v);
void WriteU16(uint16_t v);
void WriteU32(uint32_t v);
void WriteI32(int32_t v);
void WriteF32(float v);
void WriteVec3(float x, float y, float z);
void WriteString(const std::string& s);
void WriteRaw(const void* data, size_t len);

const uint8_t* Data() const;
size_t Size() const;
```

#### Class: `PacketReader`

```cpp
PacketReader(const uint8_t* data, size_t size);
bool ReadHeader(PacketHeader& h);
bool ReadU8(uint8_t& v);
bool ReadU16(uint16_t& v);
bool ReadU32(uint32_t& v);
bool ReadI32(int32_t& v);
bool ReadF32(float& v);
bool ReadVec3(float& x, float& y, float& z);
bool ReadString(std::string& s, uint16_t maxLen = 1024);
bool ReadRaw(void* out, size_t len);

size_t Remaining() const;
size_t Position() const;
```

### Message Types

See `enum class MessageType` in `protocol.h`. Key types:

- **Connection:** `C2S_Handshake`, `S2C_HandshakeAck`, `S2C_PlayerJoined`, `S2C_PlayerLeft`, `C2S_Keepalive`
- **Movement:** `C2S_PositionUpdate`, `S2C_PositionUpdate`, `C2S_MoveCommand`
- **Combat:** `C2S_AttackIntent`, `S2C_CombatHit`, `S2C_CombatDeath`, `S2C_CombatKO`, `C2S_CombatStance`
- **Entity:** `S2C_EntitySpawn`, `S2C_EntityDespawn`, `C2S_EntitySpawnReq`
- **Stats:** `S2C_HealthUpdate`, `S2C_EquipmentUpdate`, `C2S_LimbHealth`, `C2S_StatusEffect`
- **Chat:** `C2S_ChatMessage`, `S2C_ChatMessage`, `S2C_SystemMessage`
- **Lobby:** `S2C_FactionAssignment`, `C2S_LobbyReady`, `S2C_LobbyStart`, `C2S_PlayerReady`

---

## 8. Common Types

**Header:** `KenshiMP.Common/include/kmp/types.h`  
**Namespace:** `kmp`

### Type Aliases
```cpp
using EntityID = uint32_t;
using PlayerID = uint32_t;
using TickNumber = uint32_t;

constexpr EntityID INVALID_ENTITY = 0;
constexpr PlayerID INVALID_PLAYER = 0;
```

### Struct: `Vec3`
```cpp
struct Vec3 {
    float x, y, z;
    
    Vec3 operator+(const Vec3& o) const;
    Vec3 operator-(const Vec3& o) const;
    Vec3 operator*(float s) const;
    float LengthSq() const;
    float Length() const;
    float DistanceTo(const Vec3& o) const;
};
```

### Struct: `Quat`
```cpp
struct Quat {
    float w, x, y, z;
    
    uint32_t Compress() const;                // Smallest-three compression
    static Quat Decompress(uint32_t packed);
    static Quat Slerp(const Quat& a, const Quat& b, float t);
};
```

### Struct: `ZoneCoord`
```cpp
struct ZoneCoord {
    int32_t x, y;
    
    bool operator==(const ZoneCoord& o) const;
    bool IsAdjacent(const ZoneCoord& o) const;
    static ZoneCoord FromWorldPos(const Vec3& pos, float zoneSize = 750.f);
};
```

### Enums

#### `EntityState`
```cpp
enum class EntityState : uint8_t {
    Inactive,    // Not in use
    Spawning,    // Registered, waiting for game object creation
    Active,      // Fully synced and visible
    Despawning,  // Removal in progress
    Frozen,      // Suspended during zone authority handoff
};
```

#### `AuthorityType`
```cpp
enum class AuthorityType : uint8_t {
    None,         // No owner — server-managed
    Server,       // Server authoritative (NPCs, world objects)
    Player,       // Player-owned entity
    Transferring, // Authority handoff in progress
};
```

#### `EntityType`
```cpp
enum class EntityType : uint8_t {
    PlayerCharacter, NPC, Animal, Building, WorldBuilding, Item, Turret
};
```

#### `BodyPart`
```cpp
enum class BodyPart : uint8_t {
    Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg, Count = 7
};
```

#### `EquipSlot`
```cpp
enum class EquipSlot : uint8_t {
    Weapon, Back, Hair, Hat, Eyes, Body, Legs, Shirt, Boots, Gloves,
    Neck, Backpack, Beard, Belt, Count = 14
};
```

#### `DirtyFlags`
```cpp
enum DirtyFlags : uint16_t {
    Dirty_Position    = 0x001,
    Dirty_Rotation    = 0x002,
    Dirty_Animation   = 0x004,
    Dirty_Health      = 0x008,
    Dirty_Stats       = 0x010,
    Dirty_Inventory   = 0x020,
    Dirty_CombatState = 0x040,
    Dirty_LimbDamage  = 0x080,
    Dirty_SquadInfo   = 0x100,
    Dirty_FactionRel  = 0x200,
    Dirty_Equipment   = 0x400,
    Dirty_AIState     = 0x800,
    Dirty_All         = 0xFFF,
};
```

---

## 9. Usage Examples

### Example 1: Register a Local Entity

```cpp
#include "core.h"

void OnMyCharacterSpawned(void* character) {
    auto& registry = kmp::Core::Get().GetEntityRegistry();
    
    kmp::PlayerID localId = kmp::Core::Get().GetLocalPlayerId();
    kmp::EntityID netId = registry.Register(character, kmp::EntityType::PlayerCharacter, localId);
    
    spdlog::info("Registered local character: netId={}", netId);
}
```

### Example 2: Send a Chat Message

```cpp
#include "core.h"
#include "kmp/protocol.h"

void SendChatMessage(const std::string& message) {
    kmp::PacketWriter writer;
    writer.WriteHeader(kmp::MessageType::C2S_ChatMessage);
    writer.WriteString(message);
    
    auto& client = kmp::Core::Get().GetClient();
    client.SendReliable(writer.Data(), writer.Size());
}
```

### Example 3: Poll Entity Positions

```cpp
#include "core.h"
#include "sdk/kenshi_sdk.h"

void PollAllEntities() {
    auto& sdk = kmp::Core::Get().GetSDK();
    
    kmp::sdk::WorldSnapshot snapshot = sdk.GetCurrentSnapshot();
    
    for (const auto& entity : snapshot.entities) {
        spdlog::info("Entity at ({}, {}, {}): {}",
                     entity.position.x, entity.position.y, entity.position.z,
                     entity.name);
    }
}
```

### Example 4: Spawn a Remote Character

```cpp
#include "core.h"
#include "game/spawn_manager.h"

void RequestRemoteSpawn(kmp::EntityID netId, kmp::PlayerID owner, const kmp::Vec3& pos) {
    kmp::SpawnRequest req;
    req.netId = netId;
    req.owner = owner;
    req.type = kmp::EntityType::PlayerCharacter;
    req.templateName = "Greenlander";
    req.position = pos;
    req.rotation = kmp::Quat{1.f, 0.f, 0.f, 0.f};
    req.factionId = 0;  // Will be fixed by faction bootstrap
    
    auto& spawnMgr = kmp::Core::Get().GetSpawnManager();
    spawnMgr.QueueSpawn(req);
}
```

### Example 5: Handle Incoming Packet

```cpp
#include "core.h"
#include "kmp/protocol.h"

void OnPacketReceived(const uint8_t* data, size_t size, int channel) {
    kmp::PacketReader reader(data, size);
    kmp::PacketHeader header;
    
    if (!reader.ReadHeader(header)) {
        spdlog::error("Failed to read packet header");
        return;
    }
    
    if (header.type == kmp::MessageType::S2C_ChatMessage) {
        kmp::PlayerID sender;
        std::string message;
        
        if (reader.ReadU32(sender) && reader.ReadString(message)) {
            spdlog::info("Chat from {}: {}", sender, message);
        }
    }
}
```

### Example 6: Query Entity Registry

```cpp
#include "core.h"

void PrintPlayerEntities(kmp::PlayerID playerId) {
    auto& registry = kmp::Core::Get().GetEntityRegistry();
    
    std::vector<kmp::EntityID> entities = registry.GetPlayerEntities(playerId);
    
    spdlog::info("Player {} owns {} entities:", playerId, entities.size());
    
    for (kmp::EntityID id : entities) {
        auto info = registry.GetInfo(id);
        if (info) {
            spdlog::info("  - Entity {} at ({}, {}, {})",
                         id, info->lastPosition.x, info->lastPosition.y, info->lastPosition.z);
        }
    }
}
```

### Example 7: Teleport to Remote Player

```cpp
#include "core.h"

void TeleportToFriend() {
    auto& core = kmp::Core::Get();
    
    if (core.TeleportToNearestRemotePlayer()) {
        spdlog::info("Teleported to remote player");
    } else {
        spdlog::warn("No remote players found");
    }
}
```

### Example 8: Read Game State via SDK

```cpp
#include "core.h"
#include "sdk/kenshi_sdk.h"

void MonitorLocalPlayerHealth() {
    auto& sdk = kmp::Core::Get().GetSDK();
    auto& controller = kmp::Core::Get().GetPlayerController();
    
    void* primaryChar = controller.GetPrimaryCharacter();
    if (!primaryChar) return;
    
    kmp::sdk::EntitySnapshot snap;
    if (sdk.GetEntityState(reinterpret_cast<uintptr_t>(primaryChar), snap)) {
        float headHp = snap.health[static_cast<int>(kmp::BodyPart::Head)];
        float chestHp = snap.health[static_cast<int>(kmp::BodyPart::Chest)];
        
        spdlog::info("Player health: Head={:.1f}, Chest={:.1f}", headHp, chestHp);
    }
}
```

---

## Thread Safety Notes

- **EntityRegistry**: All methods are thread-safe (uses `std::shared_mutex`).
- **SpawnManager**: Queue operations are thread-safe. Spawn execution happens on the game thread via in-place replay (entity_hooks).
- **NetworkClient**: All ENet operations are protected by mutex. Safe to call from any thread.
- **Core::Get()**: Singleton is thread-safe after initialization.
- **KenshiSDK**: GetCurrentSnapshot() returns a copy (thread-safe). Update() should be called from game thread only.
- **GameCommandQueue**: ALL game memory writes from the network thread must be enqueued here and executed on the game thread.

---

## Common Pitfalls

1. **Never write game memory from the network thread** — use `Core::GetCommandQueue().Push(...)` to marshal the write to the game thread (see HandlePositionUpdate / HandleInventoryUpdate).
2. **Always check IsGameLoaded() before accessing game memory** — globals are not valid during startup/loading.
3. **Use GetInfo() instead of storing EntityInfo pointers** — entity state can change at any time.
4. **Don't spawn entities before OnGameLoaded() fires** — factory and templates are not ready.
5. **Always check return values from SDK write methods** — SEH protects against access violations but writes may fail.
6. **Clear remote entities on disconnect** — stale game objects will crash if accessed after disconnect.

---

## Contributing

To add new functionality:

1. **Extend EntityInfo** if you need new per-entity state.
2. **Add a message type** in `protocol.h` and handle it in packet handlers.
3. **Use DirtyFlags** for selective replication to save bandwidth.
4. **Hook game functions** via `HookManager` (see `hooks/` directory for examples).
5. **Add SDK methods** if you need new read/write operations on game memory.

---

## Additional Resources

- **Memory Layout:** See `reminders/01-verified-offsets.md` for game memory structure.
- **Hook Status:** See `reminders/02-hook-status.md` for all installed hooks and patterns.
- **Combat System:** See `reminders/03-combat-system.md` for combat flow and server resolution.
- **Spawn Pipeline:** See `reminders/09-spawn-pipeline-status.md` for spawn system internals.
- **Working Features:** See `reminders/11-what-works-what-doesnt.md` for tested features and known issues.

---

**Last Updated:** 2026-06-04  
**License:** MIT (see LICENSE file)  
**Contact:** the404studios@gmail.com

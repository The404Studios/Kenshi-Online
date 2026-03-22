# Kenshi-Online Technical Reference

**Version**: 0.1.0 | **Date**: 2026-03-08 | **Author**: Kenshi-Online Development Team
**Target**: Kenshi v1.0.68 (Steam x64) | **Players**: 1-16 co-op

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture](#2-architecture)
3. [KenshiMP.Common - Shared Library](#3-kenshimpcommon---shared-library)
4. [KenshiMP.Scanner - Binary Analysis & Hooking](#4-kenshimpscanner---binary-analysis--hooking)
5. [KenshiMP.Core - Client DLL](#5-kenshimpcore---client-dll)
6. [KenshiMP.Server - Dedicated Server](#6-kenshimpserver---dedicated-server)
7. [KenshiMP.Injector - Launcher](#7-kenshimpinjector---launcher)
8. [KenshiMP.MasterServer - Server Browser](#8-kenshimpmasterserver---server-browser)
9. [Testing Infrastructure](#9-testing-infrastructure)
10. [Game Memory & Offsets](#10-game-memory--offsets)
11. [Network Protocol Reference](#11-network-protocol-reference)
12. [Data Flow Diagrams](#12-data-flow-diagrams)
13. [Known Issues & Future Work](#13-known-issues--future-work)
14. [Build System](#14-build-system)
15. [Research / Legacy Code](#15-research--legacy-code)

---

## 1. Project Overview

### What is Kenshi-Online?

Kenshi-Online is a 16-player co-op multiplayer mod for Kenshi, a single-player open-world squad-based RPG. The mod enables multiple players to share the same game world with synchronized characters, combat, inventory, buildings, squads, and factions.

### Key Features

- 16 concurrent players via dedicated server
- Zone-based interest management (750m grid)
- Server-authoritative combat resolution
- Client-side interpolation for smooth remote movement
- In-place spawn replay (piggybacks on natural NPC creation)
- 14 game function hook modules
- Native MyGUI overlay (HUD, chat, menu, server browser)
- World persistence (JSON saves)
- Master server for server browser
- UPnP automatic port forwarding

### Tech Stack

- **Language**: C++17 (MSVC x64)
- **Game Engine**: Ogre3D 1.x + PhysX + MyGUI + OIS + SkyX
- **Networking**: ENet (reliable UDP)
- **Hooking**: MinHook + custom MovRaxRsp fix
- **Build**: CMake 3.20+
- **Executable**: kenshi_x64.exe (~35MB, 77,108 functions in .pdata)

### Project Structure

| Project | Type | Purpose |
|---------|------|---------|
| KenshiMP.Common | Static lib | Types, protocol, messages, config |
| KenshiMP.Scanner | Static lib | Pattern scanner, MinHook wrapper, safe memory R/W |
| KenshiMP.Core | DLL | Ogre plugin: 14 hook modules, ENet client, MyGUI overlay |
| KenshiMP.Server | EXE | Dedicated server: 16 slots, zone interest, combat |
| KenshiMP.Injector | EXE | Win32 GUI launcher |
| KenshiMP.MasterServer | EXE | Server browser backend |
| KenshiMP.TestClient | EXE | Fake player console client |
| KenshiMP.IntegrationTest | EXE | 20 automated protocol tests |
| KenshiMP.UnitTest | EXE | 13 game-code unit tests |
| KenshiMP.LiveTest | EXE | Dual-player end-to-end test |

---

## 2. Architecture

### High-Level Component Diagram

```
 [Kenshi.exe]                        [KenshiMP.Server]
     |                                      |
     | (Ogre plugin load)                   | (ENet UDP)
     v                                      |
 [KenshiMP.Core.dll]  <--- ENet --->  [Server Logic]
     |                                      |
     +-- 14 Hook Modules                    +-- Entity Manager
     +-- Sync Pipeline (7 stages)           +-- Zone Manager
     +-- Entity Registry                    +-- Combat Resolver
     +-- Interpolation Engine               +-- World Persistence
     +-- Spawn Manager                      +-- Player Manager
     +-- MyGUI Overlay                      |
     +-- Network Client                [KenshiMP.MasterServer]
                                            |
                                       (Server Browser)
```

### Threading Model

**Client (KenshiMP.Core)**:
- **Game Thread**: SyncOrchestrator 7-stage pipeline, hook callbacks, spawn processing
- **Render Thread**: Present hook, UI updates, game load detection
- **ENet Thread**: Packet pumping, decode, queue to game thread
- **Worker Threads** (2): Background entity reads, interpolation computation

**Server (KenshiMP.Server)**:
- **Main Thread**: ENet loop, Update(), tick timing
- **Console Thread**: Admin commands (getline blocking)

### Client Phase State Machine

```
Startup -> MainMenu -> Loading -> GameReady -> Connecting -> Connected
                         ^                         |
                     (reconnect)              (disconnect)
```

---

## 3. KenshiMP.Common - Shared Library

### 3.1 Type System (types.h)

**Type Aliases**:
- `EntityID = uint32_t` (unique entity identifier)
- `PlayerID = uint32_t` (1-16, 0 = invalid)
- `TickNumber = uint32_t` (server tick counter)

**Vec3**: 3D vector (12 bytes) with arithmetic operators, Length(), DistanceTo().

**Quat**: Quaternion rotation with smallest-three compression (16 bytes -> 4 bytes):
- `Compress()`: 2 bits for largest component index, 10 bits each for 3 remaining
- `Decompress()`: Reconstruct via Pythagorean identity
- `Slerp()`: Spherical linear interpolation with nlerp fallback for near-parallel, clamped dot product

**ZoneCoord**: 2D zone grid coordinate (int32_t x, y)
- `FromWorldPos()`: floor(pos / 750.0) with NaN/Inf guard
- `IsAdjacent()`: abs(delta) <= 1 in each dimension (3x3 grid)

### 3.2 Enums

**EntityState**: Inactive(0) -> Spawning(1) -> Active(2) -> Despawning(3) -> Frozen(4)

**AuthorityType**: None(0), Local(1), Remote(2), Host(3), Transferring(4)

**DirtyFlags** (uint16_t bitmask): Position, Rotation, Animation, Health, Stats, Inventory, CombatState, LimbDamage, SquadInfo, FactionRel, Equipment, AIState

**EntityType**: PlayerCharacter(0), NPC(1), Animal(2), Building(3), WorldBuilding(4), Item(5), Turret(6)

**BodyPart** (7): Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg

**EquipSlot** (14): Weapon, Back, Hair, Hat, Eyes, Body, Legs, Shirt, Boots, Gloves, Neck, Backpack, Beard, Belt

### 3.3 Constants (constants.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| KMP_PROTOCOL_VERSION | 1 | Handshake validation |
| KMP_DEFAULT_PORT | 27800 | Server listen port |
| KMP_MAX_PLAYERS | 16 | Maximum concurrent players |
| KMP_MAX_NAME_LENGTH | 31 | Player name buffer |
| KMP_TICK_RATE | 20 Hz | Server update frequency |
| KMP_TICK_INTERVAL_MS | 50 ms | Per-tick duration |
| KMP_INTERP_DELAY_SEC | 0.1 s | Default interpolation buffer |
| KMP_MAX_SNAPSHOTS | 8 | Ring buffer per entity |
| KMP_EXTRAP_MAX_SEC | 0.25 s | Max extrapolation time |
| KMP_SNAP_THRESHOLD_MIN | 5.0 | Below: smooth blend |
| KMP_SNAP_THRESHOLD_MAX | 50.0 | Above: instant teleport |
| KMP_ZONE_SIZE | 750.0 m | Zone grid cell size |
| KMP_INTEREST_RADIUS | 1 | +/-1 zone (3x3 grid) |
| KMP_MAX_SYNC_ENTITIES | 2048 | Max replicated entities |
| KMP_CHANNEL_COUNT | 3 | ENet channels |

**ENet Channels**:
- Channel 0: Reliable ordered (handshake, entity spawning, chat)
- Channel 1: Reliable unordered (combat, stats, inventory)
- Channel 2: Unreliable sequenced (position updates)

**Entity ID Ranges**:
- Players: 1-255
- NPCs: 256-8191
- Buildings: 8192-16383
- Containers: 16384-24575
- Squads: 24576-32767

### 3.4 Protocol (protocol.h)

**PacketHeader** (8 bytes, packed):
```cpp
MessageType type;      // uint8_t
uint8_t     flags;     // Bit 0: compressed
uint16_t    sequence;
uint32_t    timestamp;
```

**PacketWriter**: Binary serialization - WriteU8/U16/U32/I32/F32, WriteVec3, WriteString (length-prefixed)

**PacketReader**: Binary deserialization with bounds checking - all Read methods return false on overrun

### 3.5 Messages (messages.h)

40+ packed message structures covering:
- Connection: Handshake, HandshakeAck, HandshakeReject, PlayerJoined/Left, Keepalive
- World: WorldSnapshot, TimeSync, ZoneData, ZoneRequest
- Entity: EntitySpawn, EntityDespawn, SpawnReq, DespawnReq
- Movement: PositionUpdate (C2S/S2C), MoveCommand
- Combat: AttackIntent, CombatHit, CombatBlock, CombatDeath, CombatKO, CombatStance
- Stats: StatUpdate, HealthUpdate, EquipmentUpdate
- Inventory: ItemPickup, ItemDrop, ItemTransfer, TradeRequest, TradeResult
- Buildings: BuildRequest, BuildPlaced, BuildProgress, BuildDestroyed, DoorInteract, DoorState, BuildDismantle, BuildRepair
- Squad: SquadCreate, SquadCreated, SquadAddMember, SquadMemberUpdate
- Faction: FactionRelation (C2S/S2C)
- Chat: ChatMessage (C2S/S2C), SystemMessage
- Admin: AdminCommand, AdminResponse
- Server Query: ServerQuery, ServerInfo
- Master Server: Register, Heartbeat, Deregister, QueryList, ServerList
- Pipeline Debug: PipelineSnapshot, PipelineEvent (C2S/S2C)

### 3.6 Compression (compression.h)

- **Half-float** (float16): 32-bit float -> 16 bits (sign + 5-bit exp + 10-bit mantissa)
- **DeltaPosition**: 3 x float16 = 6 bytes (vs 12 bytes raw)
- **PackedVelocity**: 3 x int8 = 3 bytes, range +/-15 m/s mapped to +/-127

### 3.7 Configuration (config.h)

**ClientConfig**: playerName, lastServer, lastPort, autoConnect, overlayScale, masterServer, favoriteServers. Persisted to %APPDATA%/KenshiMP/client.json.

**ServerConfig**: serverName, port, maxPlayers, password, savePath, tickRate, pvpEnabled, gameSpeed, masterServer. Persisted to server.json.

---

## 4. KenshiMP.Scanner - Binary Analysis & Hooking

### 4.1 Overview

Production-grade reverse engineering library implementing a **7-phase discovery pipeline** to dynamically locate 41+ game functions across Kenshi versions. Resolves functions in ~1 second at startup.

### 4.2 Three-Tier Function Resolution

**Tier 1 - Pattern Scan** (fast, version-dependent):
- IDA-style byte patterns with `?` wildcards for operands
- SIMD-accelerated (SSE2 first-byte optimization)
- Batch scanning: single pass through .text matches 40+ patterns

**Tier 2 - String XRef** (robust, version-independent):
- Scan .rdata for unique debug strings (e.g., "Attack damage effect")
- Find RIP-relative LEA instructions referencing string
- Use .pdata to find exact function boundary
- 40+ string anchors in STRING_ANCHORS table

**Tier 3 - Hardcoded RVA** (instant, version-specific):
- Direct moduleBase + offset for known versions
- Used for critical functions and global pointers only

### 4.3 Discovery Pipeline Phases

| Phase | Component | Purpose | Time |
|-------|-----------|---------|------|
| 1 | PDataEnumerator | Parse .pdata for 77K function boundaries | ~50ms |
| 2 | StringAnalyzer | Scan .rdata for 10K+ ASCII strings | ~100ms |
| 3 | VTableScanner | Extract RTTI class hierarchies | ~50ms |
| 4 | ScannerEngine | SIMD batch scan .text for 40+ patterns | ~200ms |
| 5 | RuntimeStringScanner | String xref fallback for unresolved | ~300ms |
| 6 | CallGraphAnalyzer | Build call graph, propagate labels | ~150ms |
| 7 | GlobalPointers | Discover singleton pointers | ~100ms |
| **Total** | | **Complete function discovery** | **~1s** |

### 4.4 MovRaxRsp Fix (Critical)

**Problem**: 9/14 hooked Kenshi functions start with `mov rax, rsp` (48 8B C4). This instruction captures the caller's RSP for frame offset calculations. MinHook's trampoline adds extra stack bytes, causing RAX to capture wrong RSP, corrupting all `[rbp+XX]` accesses.

**Solution**: Two runtime-generated ASM stubs per hook:

1. **Naked Detour** (MinHook JMPs here): Saves RSP to global slot, creates 4KB stack gap, CALLs C++ hook, restores game return address
2. **Trampoline Wrapper** (C++ hook calls as "original"): Swaps to game's stack, sets RAX = RSP correctly, JMPs to trampoline+3 (skipping original mov rax, rsp)

**Page Layout** (0x200 bytes per hook):
```
+0x00: captured_rsp, stub_rsp, saved_game_ret (data slots)
+0x18: depth counter, raw_trampoline ptr, bypass_flag
+0x40: Naked detour code (~90 bytes)
+0xC0: Trampoline wrapper code (~50 bytes)
```

### 4.5 Safe Hooking (safe_hook.h)

SEH-protected call wrappers for each hook signature. MSVC C2712 restriction prevents `__try/__except` with C++ destructors, so these are C-style `__declspec(noinline)` wrappers.

```cpp
struct HookHealth {
    std::atomic<bool> trampolineFailed{false};
    std::atomic<int> failCount{0};
    const char* name;
};
```

If trampoline crashes, exception is caught, logged, and hook marked as failed.

### 4.6 Key Components

**PatternScanner** (scanner.h): Basic IDA-style pattern scanner with RIP resolution and call/jmp following.

**ScannerEngine** (scanner_engine.h): Advanced scanner with PE section enumeration, batch scanning, complex multi-part patterns, result caching.

**PDataEnumerator** (pdata_enumerator.h): Parses PE .pdata exception directory for exact function start/end boundaries. Binary search for O(log n) lookups.

**StringAnalyzer** (string_analyzer.h): Scans .rdata for strings, resolves code xrefs via RIP-relative LEA, labels functions from debug strings.

**VTableScanner** (vtable_scanner.h): Extracts C++ class hierarchies from MSVC RTTI (CompleteObjectLocator, TypeDescriptor, ClassHierarchyDescriptor).

**CallGraphAnalyzer** (call_graph.h): Builds call graph from CALL/JMP instructions, propagates labels through caller/callee chains.

**FunctionAnalyzer** (function_analyzer.h): Analyzes x64 prologues to determine parameter counts and register usage.

**HookManager** (hook_manager.h): MinHook wrapper managing all installed hooks. Auto-detects mov rax, rsp prologues and builds fix stubs. Tracks call/crash counts per hook.

**Memory** (memory.h): SEH-protected memory read/write. Pointer chain following. VirtualProtect for patching read-only memory.

### 4.7 GameFunctions Struct

41 function pointers organized by subsystem:
- **Entity**: CharacterSpawn, CharacterDestroy, CreateRandomSquad, CharacterSerialise, CharacterKO
- **Movement**: CharacterSetPosition, CharacterMoveTo
- **Combat**: ApplyDamage, StartAttack, CharacterDeath, HealthUpdate, CutDamageMod, UnarmedDamage, MartialArtsCombat
- **World**: ZoneLoad, ZoneUnload, BuildingPlace, BuildingDestroyed, SpawnCheck
- **Time**: GameFrameUpdate, TimeUpdate
- **Save**: SaveGame, LoadGame, ImportGame
- **Squad**: SquadCreate, SquadAddMember
- **Inventory**: ItemPickup, ItemDrop, BuyItem
- **Faction**: FactionRelation
- **AI**: AICreate, AIPackages
- **Turret**: GunTurret
- **Stats**: CharacterStats
- **Globals**: PlayerBase, GameWorldSingleton

---

## 5. KenshiMP.Core - Client DLL

### 5.1 Core Module (core.h/cpp)

Master orchestrator managing lifecycle, state machine, and subsystem coordination.

**Initialization Sequence**:
1. DLLMain registers Present hook (render thread entry)
2. First Present call triggers Core::Initialize()
3. InitScanner() - pattern scan for game functions
4. InitHooks() - install 14 hook modules
5. InitNetwork() - create ENet host
6. InitUI() - load MyGUI layouts
7. PollForGameLoad() - detect when world is ready
8. OnGameLoaded() - faction bootstrap, entity scan

**Key Features**:
- Host spawn point: joiner auto-teleports to host's primary character
- Breadcrumb trail: writes step/tick to file every tick for crash diagnosis
- Vectored exception handler: logs crash info before Kenshi's SEH catches

### 5.2 Sync Pipeline (sync/)

**SyncOrchestrator** - 7-stage per-frame pipeline:

| Stage | Name | Purpose |
|-------|------|---------|
| 1 | UpdateZones | Update local player zone coordinate |
| 2 | SwapBuffers | Wait for workers, swap double-buffer |
| 3 | ApplyRemotePositions | Write interpolated positions to game memory |
| 4 | PollAndSendPositions | Read local entities, send C2S_PositionUpdate |
| 5 | ProcessSpawns | Handle entity spawn queue |
| 6 | KickBackgroundWork | Post interpolation tasks to workers |
| 7 | UpdatePlayers | AFK checks, diagnostics |

**EntityRegistry** - Central entity database:
- Thread-safe (shared_mutex for concurrent reads)
- Register/RegisterRemote/SetGameObject/RemapEntityId
- Tracks: net ID, game object pointer, owner, state, position, zone, health

**Interpolation** - Smooth remote entity movement:
- 8-snapshot ring buffer per entity
- Adaptive delay via jitter EMA (50-200ms)
- Snap correction: large errors blend over 0.5s
- Extrapolation: dead reckoning up to 250ms
- Position lerp + rotation slerp

**ZoneEngine** - Spatial partitioning:
- 750m grid cells, 3x3 interest window
- Cached zone->entity mapping (rebuilt every 500ms)
- Player zone tracking for interest computation

**PlayerEngine** - Remote player session tracking:
- States: Connecting -> Loading -> InGame -> AFK (5min) -> Disconnected

**PipelineOrchestrator** - Network-replicated debugger:
- 1Hz state snapshots broadcast to all peers
- Anomaly detection: ghost entities, stuck spawns, hook failures

### 5.3 Game System (game/)

**SpawnManager** - Character creation orchestration:
- **Primary**: In-place replay in Hook_CharacterCreate (piggyback natural NPC creation)
- **Fallback**: SpawnCharacterDirect after 5s timeout (captured pre-call struct)
- Template database: heap-scanned GameData validated by factory input observation
- Faction fix: write local player's faction to char+0x10 post-spawn

**PlayerController** - Local + remote player management:
- InitializeLocalPlayer(): scan squad, register entities
- OnRemoteCharacterSpawned(): rename, fix faction, link to registry

**LoadingOrchestrator** - Spawn gate:
- Phases: Idle -> InitialLoad -> ZoneTransition -> SpawnLoad -> Idle
- IsSafeToSpawn(): phase == Idle && no pending resources && cooldown elapsed

**GameTypes** (game_types.h) - Offset tables and accessors for:
- CharacterAccessor: position, rotation, health, name, faction, stats, equipment
- SquadAccessor, InventoryAccessor, BuildingAccessor, FactionAccessor, StatsAccessor

### 5.4 Hook Modules (hooks/)

| Module | Functions Hooked | Status | Purpose |
|--------|-----------------|--------|---------|
| entity_hooks | CharacterSpawn | Enabled (post-connect) | Spawn injection, in-place replay |
| combat_hooks | ApplyDamage, Death, KO | Enabled | Damage/death/KO sync |
| movement_hooks | SetPosition, MoveTo | Disabled | Position polling instead |
| world_hooks | ZoneLoad, ZoneUnload | Enabled | Zone tracking |
| faction_hooks | FactionRelation | Enabled | Relationship sync |
| squad_hooks | SquadCreate, AddMember | Disabled | Raw ptr injection only |
| building_hooks | Place/Destroy/Repair | Enabled | Building sync |
| inventory_hooks | ItemPickup/Drop/Buy | Enabled | Item tracking |
| ai_hooks | AICreate, AIPackages | Enabled | Remote AI suppression |
| save_hooks | SaveGame, LoadGame | Disabled | Manual loading flag |
| render_hooks | Present, WndProc | Enabled | Frame driver, input |
| time_hooks | TimeUpdate | Enabled | Server time sync |
| game_tick_hooks | GameFrameUpdate | Enabled | Spawn diagnostics |
| input_hooks | (stub) | N/A | WndProc-based input |
| resource_hooks | Ogre managers | Not implemented | Future loading detection |

**Entity Hooks** (most complex):
- MovRaxRsp naked detour with raw trampoline for reentrant calls
- Thread-local s_hookDepth prevents recursive hooks
- Burst protection: >5 creates in 100ms -> passthrough mode
- Per-player spawn cap: max 4 in-place spawns per player
- SEH protection on all game memory access

**Combat Hooks**:
- Local damage applied first via trampoline, then network message sent
- Ownership check: only sends if attacker is locally owned
- SafeCall wrappers prevent trampoline crashes from killing game

**Render Hooks**:
- Phase transitions: Startup -> MainMenu (5s) -> Loading (>2s gap) -> Loaded (5s smooth)
- OnGameTick fallback: only called if time_hooks not active
- WndProc input: F1=menu, Tab=players, Enter=chat, Insert=log, `=debug

### 5.5 Network (net/)

**NetworkClient**: ENet wrapper with ConnectAsync, SendReliable/Unreliable, ping tracking.

**PacketHandler**: Dispatches all S2C messages to appropriate handlers. Runs on ENet thread, queues work to game thread.

**ServerQueryClient**: Async server browser (separate ENet host), queries individual servers and master server.

### 5.6 UI System (ui/)

**NativeHud**: In-game HUD via MyGUI:
- Status bar (connection state, phase, spawn queue)
- Chat display (10 lines, fading)
- Player list (8 rows with ping)
- Debug log panel (20 lines, toggleable)

**NativeMenu**: Multiplayer menu:
- Host, Join, Settings, Server Browser panels
- Join: IP/port/name input, connect button
- Settings: player name, auto-connect toggle
- Browser: query master server, display results

**Overlay**: Master UI controller:
- Auto-connect on game load (5s delay, 6 retry attempts)
- Chat history with timestamps

### 5.7 System (sys/)

**CommandRegistry**: Chat commands (/help, /who, /tp, /say, /pipeline, /stats, /zone, /lag)

**TaskOrchestrator**: 2-thread worker pool for background entity reads and interpolation. Frame tasks must complete before next game tick.

---

## 6. KenshiMP.Server - Dedicated Server

### 6.1 Architecture

Single-threaded main loop at 20 Hz with console thread for admin commands. All game state protected by recursive mutex.

**Main Loop**:
1. Pump ENet events (connect/disconnect/receive)
2. Advance game time (24h cycle * gameSpeed)
3. BroadcastPositions() every tick (zone-filtered, unreliable)
4. BroadcastTimeSync() every 5 seconds (reliable)
5. Auto-save every 60 seconds
6. Master server heartbeat every 30 seconds

### 6.2 Player Connection Lifecycle

1. **ENet Connect** -> HandleConnect(): IP logged, capacity check
2. **C2S_Handshake** -> HandleHandshake(): Protocol version check, name sanitization, assign PlayerID (1-16), send HandshakeAck + PlayerJoined + WorldSnapshot
3. **Active Session**: Position updates, combat, entity management
4. **Disconnect** -> HandleDisconnect(): Despawn owned entities, reassign host if needed, broadcast PlayerLeft

### 6.3 Entity Management

**ServerEntity** stores: ID, type, owner, state, authority, position, rotation, zone, templateId, templateName, factionId, health[7], alive, combat info, animation, equipment[14], buildProgress.

**Spawn Flow**:
1. Client sends C2S_EntitySpawnReq with template, position, health
2. Server assigns serverID = m_nextEntityId++
3. Store in m_entities map
4. Broadcast S2C_EntitySpawn to ALL clients

**Limits**: 2048 total entities, 64 per player (soft), 255 per position packet

### 6.4 Zone-Based Interest Management

- World divided into 750m x 750m zones
- Each player sees 3x3 grid (center + 8 adjacent)
- BroadcastPositions: only sends entities in player's interest zones
- Zone transitions trigger C2S_ZoneRequest for new zone entities

### 6.5 Combat Resolution (Server-Authoritative)

1. Client sends C2S_AttackIntent (attackerId, targetId, attackType)
2. Server validates: ownership, existence, alive state, distance (melee < 15m, ranged < 150m)
3. ResolveCombat: weighted body part selection, base damage with random factor, defense reduction, block chance (20%)
4. KO threshold: any body part <= -50 HP
5. Death threshold: chest or head <= -100 HP
6. Results broadcast via BroadcastExcept (excludes attacker to prevent double damage)

### 6.6 World Persistence

- JSON format with atomic writes (write .tmp, rename)
- Auto-save every 60 seconds
- Stores: version, timeOfDay, weather, entities array (position, rotation, health, equipment, templateName)
- Orphan cleanup on load (entities with disconnected owner skipped)

### 6.7 Message Handlers

| C2S Message | Key Validation | S2C Response |
|-------------|---------------|--------------|
| Handshake | Protocol version, server capacity | HandshakeAck, PlayerJoined |
| PositionUpdate | Entity ownership | (stored for broadcast) |
| AttackIntent | Ownership, distance, alive | CombatHit/KO/Death |
| EntitySpawnReq | Type valid, entity limit | EntitySpawn broadcast |
| EntityDespawnReq | Exists, owned by player | EntityDespawn broadcast |
| ChatMessage | Non-empty | ChatMessage broadcast |
| BuildRequest | Basic validation | BuildPlaced broadcast |
| EquipmentUpdate | Owned, slot < 14 | EquipmentUpdate broadcast |
| ZoneRequest | Zone bounds (+/-500) | EntitySpawn per entity |
| BuildRepair | Owned, amount [0,1], not NaN | BuildProgress broadcast |
| CombatKO/Death | Owned | CombatKO/Death broadcast |
| AdminCommand | Is host | AdminResponse |

### 6.8 UPnP Port Forwarding

Uses Windows COM API (IUPnPNAT) with 3 retry attempts. Falls back to Windows Firewall rules via netsh if UPnP fails.

### 6.9 Console Commands

`help`, `stop`, `status` (server stats), `players` (connected list), `kick <name>`, `say <msg>`, `save` (manual world save)

---

## 7. KenshiMP.Injector - Launcher

### Overview

Win32 GUI application (480x340 px) that prepares and launches Kenshi with the multiplayer mod.

### Launch Sequence

1. Validate game path (check kenshi_x64.exe exists)
2. Copy KenshiMP.Core.dll to game directory
3. Add `Plugin=KenshiMP.Core` to Plugins_x64.cfg (Ogre plugin system)
4. Write client.json with connection details to %APPDATA%/KenshiMP/
5. Launch via Steam protocol (`steam://rungameid/233860`) or direct exe

### Key Features

- Auto-detects Kenshi path from Steam registry
- Idempotent plugin installation (checks before adding)
- Clean uninstall: RemoveOgrePlugin() removes the config line

---

## 8. KenshiMP.MasterServer - Server Browser

### Overview

Centralized registry for game server browser. Listens on port 27801.

### Protocol

- **MS_Register**: Server registers with name, port, player count, PvP flag
- **MS_Heartbeat**: Server sends periodic keepalive (every 30s)
- **MS_Deregister**: Server shutting down
- **MS_QueryList**: Client requests server list
- **MS_ServerList**: Master responds with all registered servers

### Server Entry Expiry

Entries pruned every 30s. Timeout: 90s without heartbeat.

### Admin Console

`status` (list servers), `stop`/`quit` (shutdown), `help`

---

## 9. Testing Infrastructure

### 9.1 Unit Tests (KenshiMP.UnitTest)

13 tests using fake game memory (4KB buffers with correct offsets):

1. CharacterAccessor::GetPosition
2. CharacterAccessor::GetName (SSO mode)
3. CharacterAccessor::WriteName
4. CharacterAccessor::WritePosition (fallback)
5. CharacterAccessor Faction Read/Write
6. EntityRegistry CRUD
7. Remote Entity Lifecycle
8. Entity ID Remapping
9. Interpolation System (adaptive delay, snap correction)
10. Packet Round-Trip (Position Update)
11. Packet Round-Trip (Entity Spawn)
12. Full Spawn Flow
13. 4-Player Session Simulation

### 9.2 Integration Tests (KenshiMP.IntegrationTest)

20 automated protocol tests using real server + two fake clients:

1. Server Startup
2. Client 1 Handshake (PlayerID assigned)
3. Client 2 Handshake (mutual PlayerJoined)
4. Entity Spawn (C1, server assigns ID)
5. Entity Spawn (C2, cross-visibility)
6. Position Sync (C1 -> C2 via server)
7. Chat Message Broadcast
8. System Message
9. Time Sync
10. Inventory Update
11. Trade Result
12. Squad Creation
13. Squad Member Update
14. Faction Relation
15. Building Sync
16. Build Destroy
17. Build Progress
18. Client 1 Disconnect (PlayerLeft + EntityDespawn)
19. Client 2 Cleanup
20. Server Shutdown

### 9.3 TestClient (Fake Player Bot)

Console application simulating a player:
- Walks in patrol pattern (back-and-forth)
- Sends position updates at 20 Hz
- Receives and logs all broadcast messages
- Console commands: `c <msg>` (chat), `s` (status), `q` (quit)

### 9.4 LiveTest (End-to-End)

Launches real Kenshi + server + TestClient, monitors log files and stdout for 20 milestones:
- P1 (Kenshi): DLL loaded, config, scanner, hooks, network, game loaded, handshake, entities, spawn
- P2 (TestClient): Connected, handshake, host position, entity spawned, sees P1, position sync
- Dashboard: 15s refresh, PASS/FAIL per milestone, final report

---

## 10. Game Memory & Offsets

### 10.1 Key Function Addresses (Steam v1.0.68)

| Function | RVA | Prologue | Status |
|----------|-----|----------|--------|
| CharacterSpawn | 0x581770 | mov rax, rsp | Hooked |
| ApplyDamage | 0x7A33A0 | mov rax, rsp | Hooked |
| StartAttack | 0x7B2A20 | mov rax, rsp | Hooked |
| CharacterDeath | 0x7A6200 | mov rax, rsp | Hooked |
| HealthUpdate | 0x86B2B0 | mov rax, rsp | Hooked |
| TimeUpdate | 0x214B50 | mov rax, rsp | Hooked |
| LoadGame | 0x373F00 | mov rax, rsp | Disabled |
| ItemPickup | 0x74C8B0 | standard | Hooked |
| ItemDrop | 0x745DE0 | standard | Hooked |
| FactionRelation | 0x872E00 | mov rax, rsp | Hooked |
| SquadCreate | 0x480B50 | mov rax, rsp | Disabled |
| SquadAddMember | 0x928423 | standard | Disabled |
| AICreate | 0x622110 | standard | Hooked |
| BuildingRepair | 0x5C9E70 | standard | Hooked |

### 10.2 Character Structure Offsets

```
+0x10:  Faction* faction
+0x18:  std::string name (MSVC SSO: 16B inline, 8B size, 8B cap)
+0x40:  GameData* template
+0x48:  Vec3 position (cached, read-only)
+0x58:  Quat rotation
+0x178: Combat stats base
+0x1B8: Health float array (7 body parts)
+0x2B8: Health chain pointer 1 (CE chain: +0x2B8 -> +0x5F8 -> +0x40)
+0x2E8: Inventory*
+0x450: Stats base (50+ skill floats)
```

### 10.3 Other Structure Offsets

**Squad**: +0x10 name, +0x28 members, +0x30 count, +0x38 faction, +0x40 isPlayerSquad

**Building**: +0x18 name, +0x48 position, +0x164 condition, +0x430 inventory

**GameWorld**: +0x700 gameSpeed, +0x888 character list, +0x8B9 paused, +0x8B0 ZoneManager

**Vector3** (with padding): +0x20 x, +0x24 y, +0x28 z

### 10.4 Global Pointers

- PlayerBase: 0x01AC8A90
- GameWorldSingleton: 0x02133040
- FactionString: 0x016C2F68
- GameDataManagerMain: 0x02133060

---

## 11. Network Protocol Reference

### 11.1 Message Types (Complete)

**Connection (0x01-0x08)**: Handshake, HandshakeAck, HandshakeReject, Disconnect, PlayerJoined, PlayerLeft, Keepalive, KeepaliveAck

**World State (0x10-0x13)**: WorldSnapshot, TimeSync, ZoneData, ZoneRequest

**Entity (0x20-0x23)**: EntitySpawn, EntityDespawn, EntitySpawnReq, EntityDespawnReq

**Movement (0x30-0x33)**: C2S/S2C PositionUpdate, C2S/S2C MoveCommand

**Combat (0x40-0x47)**: AttackIntent, CombatHit, CombatBlock, CombatDeath, CombatKO, CombatStance, C2S_CombatDeath, C2S_CombatKO

**Stats (0x50-0x53)**: StatUpdate, HealthUpdate, EquipmentUpdate (S2C/C2S)

**Inventory (0x60-0x65)**: ItemPickup, ItemDrop, ItemTransfer, InventoryUpdate, TradeRequest, TradeResult

**Buildings (0x70-0x77)**: BuildRequest, BuildPlaced, BuildProgress, BuildDestroyed, DoorInteract, DoorState, BuildDismantle, BuildRepair

**Chat (0x80-0x82)**: C2S/S2C ChatMessage, SystemMessage

**Admin (0x90-0x91)**: AdminCommand, AdminResponse

**Server Query (0xA0-0xA1)**: ServerQuery, ServerInfo

**Squad (0xB0-0xB3)**: SquadCreate, SquadCreated, SquadAddMember, SquadMemberUpdate

**Faction (0xC0-0xC1)**: C2S/S2C FactionRelation

**Master Server (0xD0-0xD4)**: Register, Heartbeat, Deregister, QueryList, ServerList

**Pipeline Debug (0xE0-0xE3)**: C2S/S2C PipelineSnapshot, C2S/S2C PipelineEvent

### 11.2 Position Update Format

```
Header (8 bytes)
SourcePlayer (u32)
EntityCount (u8, max 255)
[EntityCount x CharacterPosition]:
  entityId (u32)
  posX, posY, posZ (f32 each)
  compressedQuat (u32, smallest-three)
  animStateId (u8)
  moveSpeed (u8, 0-255 mapped to 0-15 m/s)
  flags (u16: bit0=running, bit1=sneaking, bit2=inCombat)
```

### 11.3 Entity Spawn Format

```
Header
entityId (u32, server-assigned)
type (u8, EntityType)
owner (u32, PlayerID)
templateId (u32)
posX, posY, posZ (f32)
compressedQuat (u32)
factionId (u32)
templateName (string, length-prefixed)
hasExtendedState (u8)
[if hasExtendedState]:
  health[7] (f32 each, per body part)
  alive (u8)
```

---

## 12. Data Flow Diagrams

### 12.1 Connection Flow

```
User clicks "Join" -> Overlay.SetAutoConnect(ip, port)
  -> Overlay.Update() detects game loaded
  -> NetworkClient.ConnectAsync()
  -> ENet connects to server
  -> Server sends S2C_HandshakeAck (assigns PlayerID)
  -> Core::OnHandshakeAck()
  -> PlayerController.InitializeLocalPlayer() (scan squad)
  -> SendExistingEntitiesToServer() (C2S_EntitySpawnReq per entity)
  -> Server sends S2C_WorldSnapshot (all existing entities)
  -> PlayerEngine.OnWorldSnapshotReceived() (state = InGame)
  -> SyncOrchestrator enables CharacterCreate hook
  -> Entity spawning begins
```

### 12.2 Position Sync Pipeline

```
Game tick (60-120 Hz) -> SyncOrchestrator.Tick()
  Stage 4: Poll local entities every 50ms (20 Hz)
    -> Compare to last known position
    -> Build C2S_PositionUpdate packet
    -> Send unreliable (Channel 2)

  Server receives -> broadcasts to interest zones

  Remote clients receive S2C_PositionUpdate
    -> Interpolation.AddSnapshot() (estimate velocity, detect snaps)

  Stage 6: Post interpolation tasks to workers
    -> Worker: Interpolation.GetInterpolated() per remote entity
    -> Results in writeBuffer

  Next frame Stage 2: Swap buffers
  Stage 3: Write interpolated positions to game memory (SEH-protected)
    -> Visible character movement
```

### 12.3 Remote Entity Spawn Pipeline

```
Server sends S2C_EntitySpawn (netId, template, position)
  -> PacketHandler queues in SpawnManager
  -> SyncOrchestrator.StageProcessSpawns() monitors queue

Path A (In-Place Replay, primary):
  -> Game fires CharacterCreate hook (natural NPC creation)
  -> Hook detects queued spawn, calls factory with queued template
  -> Character created in game world
  -> Hook registers in EntityRegistry as remote

Path B (Direct Spawn, fallback after 5s):
  -> SpawnCharacterDirect() using captured pre-call struct
  -> Self-reference fixup, faction fix post-spawn
  -> Register in EntityRegistry as remote

Both paths:
  -> Link to interpolation system
  -> Per-frame: interpolated positions written to game memory
```

### 12.4 Double-Buffer Pipeline

```
Frame N (Game Thread):         Frame N (Workers, parallel):
  Stage 1: UpdateZones           BackgroundReadEntities -> writeBuffer
  Stage 2: SwapBuffers           BackgroundInterpolate -> writeBuffer
  Stage 3: Apply readBuffer      Signal completion
  Stage 4: Poll & Send
  Stage 5: ProcessSpawns
  Stage 6: KickBackgroundWork
  Stage 7: UpdatePlayers

Frame N+1:
  Stage 2: Wait for workers, swap (readBuffer <- writeBuffer)
  Stage 3: Use new data
```

---

## 13. Known Issues & Future Work

### 13.1 Architecture Issues (Require Significant Refactoring)

- **Packet handler runs on network thread**: All game memory writes and native function calls should be queued to game thread
- **EntityRegistry::GetInfo() returns dangling pointer**: Callers should use GetInfoCopy()
- **SpawnManager factory/template pointers can become stale**: Need revalidation on each use
- **sync_orchestrator Reset() doesn't wait for background workers**: Need WaitForFrameWork before clearing buffers
- **Direct spawn path has no SEH protection** for post-spawn setup

### 13.2 Thread Safety (Latent Data Races)

- Core::m_hostSpawnPoint (Vec3) written from network thread, read from game thread (torn read risk)
- m_writeBuffer/m_readBuffer are plain int, should be atomic
- s_spawnsPerPlayer map in entity_hooks accessed without mutex
- squad_hooks s_squadPtrToNetId map accessed from both threads without mutex

### 13.3 Robustness Gaps

- No rate limiting on any server message type
- Chat messages have no length limit
- Config values not validated (tickRate=0 causes div by zero)
- ProcessSpawnQueueFromHook reads m_preCallData without m_templateMutex
- ConnectedPlayer.ownedEntities vector is never populated (dead code)

### 13.4 Test Coverage Gaps

- No reconnection test
- No late joiner / world snapshot test
- No zone boundary interest management test
- No malformed packet test
- No entity ownership validation test
- No max capacity test
- No combat resolution test

### 13.5 Audit Fixes Applied (2026-03-08)

**CRITICAL** (6):
1. Double damage/death/KO - Changed Broadcast to BroadcastExcept in combat handlers
2. Double OnGameTick - Added !core.IsTimeHookActive() guard in render_hooks
3. Reentrant MovRaxRsp corruption - Changed to s_rawCreateTrampoline for reentrant calls
4. Double HandleDisconnect - Removed manual call, let ENet event handle it
5. Host never reassigned - Added host reassignment in HandleDisconnect
6. BroadcastPositions count/data mismatch - Capped loop to sendCount

**HIGH** (6):
7. NaN/Inf reaching game memory - Added isfinite() guard in sync_orchestrator
8. Slerp division by zero - Clamped dot, changed threshold, normalized nlerp
9. Body part OOB - Added validation in HandleCombatHit and HandleCombatKO
10. ZoneUnload entity removal order - Moved after trampoline call
11. NaN in ZoneCoord::FromWorldPos - Added isfinite() guard
12. s_serverSourced/s_loading not atomic - Changed to std::atomic<bool>

**MEDIUM** (6):
13. Inconsistent extended state - Always write health data in all paths
14. strncpy null termination - Fixed in HandleHandshake
15. Null peer in BroadcastExcept - Added null check
16. Redundant lock in HandlePacket - Removed
17. Build repair NaN/negative - Added validation
18. Self-join notification - Changed to BroadcastExcept

**Pattern Scanner** (4):
- CharacterDestroy string length: 31 -> 32
- CharacterSerialise string length: 33 -> 34
- CharacterDeath string length: 28 -> 29
- BuildingPlace string length: 45 -> 44

---

## 14. Build System

### CMake Configuration

```cmake
project(KenshiMP VERSION 0.1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
```

**Platform**: 64-bit Windows only (enforced)

**Critical ABI Fix**: `-D_ITERATOR_DEBUG_LEVEL=0` forces Release-mode container layout in Debug builds (Kenshi's Ogre/MyGUI DLLs are Release, no _Container_proxy)

**Third-party Libraries** (in lib/):
- ENet (reliable UDP)
- MinHook (API hooking)
- spdlog (logging, header-only)
- nlohmann/json (config, header-only)
- ImGui (GUI, excluded from build)

### Build Targets

| Target | Type | Dependencies |
|--------|------|-------------|
| KenshiMP.Common | Static lib | nlohmann/json |
| KenshiMP.Scanner | Static lib | MinHook, Common |
| KenshiMP.Core | DLL | Common, Scanner, ENet, spdlog |
| KenshiMP.Server | EXE | Common, ENet, spdlog, nlohmann/json |
| KenshiMP.MasterServer | EXE | Common, ENet, spdlog, nlohmann/json |
| KenshiMP.Injector | EXE | Common |
| KenshiMP.TestClient | EXE | Common, ENet, spdlog |
| KenshiMP.IntegrationTest | EXE | Common, ENet, spdlog |
| KenshiMP.UnitTest | EXE | Common, Scanner |
| KenshiMP.LiveTest | EXE | Common |

### Build Commands

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Output: build/bin/Release/
```

---

## 15. Research / Legacy Code

### Overview

Located in `research/kenshi_multiplayer/`, this is the original monolithic proof-of-concept that preceded the current modular architecture. It was a single DLL + TCP server prototype supporting 2 players.

### Evolution Path

```
research/ (monolithic, TCP, 2 players, console-only)
  -> KenshiMP.Common (extracted types/protocol)
  -> KenshiMP.Scanner (extracted pattern scanning)
  -> KenshiMP.Core (extracted hooks, ENet client, MyGUI UI)
  -> KenshiMP.Server (upgraded to 16 players, ENet, zones)
  -> KenshiMP.Injector (Win32 GUI launcher)
```

### Key Discoveries from Research

1. **Memory Architecture**: GOG v1.0.68 offsets for GameWorld, characters, buildings, squads
2. **Squad Spawning Bypass**: skipSpawningCheck1/2 flags, template substitution
3. **Heap Scanning**: Scan process heap for GameDataManagerMain references to build template database (~54,949 entries)
4. **Faction Management**: Faction string at fixed offset, must be set at character select
5. **Hooking Techniques**: Pattern-based function location, manual trampoline construction
6. **Position Access**: Never use char+0x48 directly; use AnimClass -> CharMovement -> Position (0x320)
7. **kenshiString**: Custom string type with 16-byte inline buffer + padding (critical for game function calls)

### Files in Research

| File | Purpose |
|------|---------|
| dllmain.cpp | Entry point, console, network init |
| network.h/cpp | Winsock TCP client |
| gameState.h/cpp | State cache, hooks, heap scanning |
| gameStateGetters/Setters.cpp | Memory read/write |
| commands.h/cpp | Interactive console (chars, builds, db, give) |
| func.h/cpp | Direct game function calls |
| structs.h | Raw game structure definitions |
| offsets.h | All GOG 1.0.68 memory offsets |
| server/server.cpp | TCP echo server (2 clients) |

---

## Appendix A: File Index

### KenshiMP.Common (10 files)
- include/kmp/types.h, protocol.h, messages.h, config.h, constants.h, compression.h
- src/protocol.cpp, config.cpp, serialization.cpp, compression.cpp

### KenshiMP.Scanner (25 files)
- include/kmp/scanner.h, patterns.h, memory.h, hook_manager.h, mov_rax_rsp_fix.h
- include/kmp/scanner_engine.h, string_analyzer.h, pdata_enumerator.h, call_graph.h
- include/kmp/function_analyzer.h, vtable_scanner.h, orchestrator.h, safe_hook.h
- src/ (13 .cpp files)

### KenshiMP.Core (60+ files)
- core.h/cpp, dllmain.cpp
- hooks/ (15 modules, 30 files)
- sync/ (11 files): sync_orchestrator, entity_registry, entity_resolver, interpolation, zone_engine, player_engine, sync_facilitator, pipeline_orchestrator, pipeline_state, ownership, zone_interest
- game/ (14 files): game_types, spawn_manager, player_controller, loading_orchestrator, asset_facilitator, game_character/building/faction/inventory/squad/stats/world
- net/ (4 files): client, packet_handler, server_query
- ui/ (8 files): overlay, native_hud, native_menu, mygui_bridge
- sys/ (5 files): command_registry, builtin_commands, task_orchestrator, frame_data

### KenshiMP.Server (12 files)
- server.h/cpp, main.cpp
- entity_manager, player_manager, zone_manager, game_state (h/cpp each)
- combat_resolver.cpp, world_persistence.cpp, upnp.h/cpp

### Other Projects
- KenshiMP.Injector: injector.h/cpp, main.cpp, process.h/cpp
- KenshiMP.TestClient: main.cpp
- KenshiMP.IntegrationTest: main.cpp
- KenshiMP.UnitTest: main.cpp
- KenshiMP.LiveTest: main.cpp
- KenshiMP.MasterServer: main.cpp

---

*Document generated 2026-03-08. For the latest version, see the project repository.*

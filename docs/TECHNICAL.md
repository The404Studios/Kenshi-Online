# Kenshi-Online — Complete Technical Reference

> **v1.0.2** | Last updated: March 2026

This document covers every system, class, offset, message type, hook module, and pipeline stage in the Kenshi-Online multiplayer mod. It is the authoritative reference for developers, reverse engineers, and contributors.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Game Memory Layout & Offsets](#game-memory-layout--offsets)
3. [Accessor Classes](#accessor-classes)
4. [Game Enums](#game-enums)
5. [Function Pointer Typedefs](#function-pointer-typedefs)
6. [Pattern Scanner & Discovery Orchestrator](#pattern-scanner--discovery-orchestrator)
7. [Hook Modules (14 Systems)](#hook-modules-14-systems)
8. [Hook Manager (MinHook Wrapper)](#hook-manager-minhook-wrapper)
9. [Core Initialization & Game Tick Pipeline](#core-initialization--game-tick-pipeline)
10. [Sync Orchestrator (7-Stage Pipeline)](#sync-orchestrator-7-stage-pipeline)
11. [Entity Registry](#entity-registry)
12. [Entity Resolver (Interest & Dirty Tracking)](#entity-resolver-interest--dirty-tracking)
13. [Zone Engine](#zone-engine)
14. [Player Engine (Session State Machine)](#player-engine-session-state-machine)
15. [Sync Facilitator (Facade)](#sync-facilitator-facade)
16. [Interpolation System](#interpolation-system)
17. [Pipeline Orchestrator (Diagnostics)](#pipeline-orchestrator-diagnostics)
18. [Network Protocol](#network-protocol)
19. [Message Types (Complete Enum)](#message-types-complete-enum)
20. [Message Structures](#message-structures)
21. [Packet Serialization](#packet-serialization)
22. [Constants & Limits](#constants--limits)
23. [Type Definitions](#type-definitions)
24. [Compression](#compression)
25. [Configuration](#configuration)
26. [Game Server](#game-server)
27. [Server Entity Management](#server-entity-management)
28. [Zone Manager (Server)](#zone-manager-server)
29. [Player Manager (Server)](#player-manager-server)
30. [Combat Resolver](#combat-resolver)
31. [World Persistence](#world-persistence)
32. [Master Server](#master-server)
33. [UPnP Mapper](#upnp-mapper)
34. [UI System (Native MyGUI)](#ui-system-native-mygui)
35. [Injector & Launcher](#injector--launcher)
36. [Thread Safety Model](#thread-safety-model)

---

## Architecture Overview

```
KenshiMP/
├── KenshiMP.Common/        Static lib — protocol, types, serialization, compression, config
├── KenshiMP.Scanner/       Static lib — pattern scanner, SIMD engine, orchestrator, hook manager
├── KenshiMP.Core/          DLL (Ogre plugin) — hooks, networking, sync, UI
│   ├── hooks/              14 hook modules (entity, combat, movement, world, etc.)
│   ├── game/               Reconstructed game types and accessors
│   ├── net/                ENet client + packet handler
│   ├── sync/               Entity registry, interpolation, zone engine, orchestrators
│   └── ui/                 Native MyGUI HUD + menu
├── KenshiMP.Server/        Dedicated server exe — ENet, entity management, persistence
├── KenshiMP.MasterServer/  Server browser registry (port 27801)
├── KenshiMP.Injector/      Win32 launcher — modifies Plugins_x64.cfg, launches Kenshi
├── KenshiMP.TestClient/    Console test client
├── KenshiMP.UnitTest/      Unit tests
├── KenshiMP.IntegrationTest/ Integration tests
└── KenshiMP.LiveTest/      Live integration test (--dual mode)
```

**Build:** `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`

All 10 targets build successfully. C++17, x64 only, MSVC 2022.

---

## Game Memory Layout & Offsets

### Singleton Addresses (GOG v1.0.68)

| Singleton | RVA | Notes |
|-----------|-----|-------|
| PlayerBase | `0x01AC8A90` | GOG only — Steam differs |
| GameWorldSingleton | `0x02133040` | GOG only — runtime fallback via string xref |

### CharacterOffsets

```cpp
struct CharacterOffsets {
    int name          = 0x18;    // MSVC std::string (KServerMod verified)
    int faction       = 0x10;    // Faction* (KServerMod verified)
    int position      = 0x48;    // Vec3 read-only cached position
    int rotation      = 0x58;    // Quat rotation
    int sceneNode     = -1;      // Ogre::SceneNode* (runtime probe)
    int aiPackage     = -1;      // AI package pointer (runtime probe)
    int inventory     = 0x2E8;   // Inventory*
    int stats         = 0x450;   // Stats base
    int equipment     = -1;      // Equipment array (runtime probed)
    int currentTask   = -1;      // Current task type
    int isAlive       = -1;      // Use health chain fallback
    int isPlayerControlled = -1; // Runtime probe via ProbePlayerControlledOffset()
    int health        = -1;      // Direct offset (use chain if -1)
    int moveSpeed     = -1;      // Physics-derived
    int animState     = -1;      // Animation state index
    int gameDataPtr   = 0x40;    // GameData* template backpointer
    int squad         = -1;      // KSquad* (runtime discovered)

    // Health chain: char+0x2B8 → +0x5F8 → +0x40 = health[0]
    int healthChain1  = 0x2B8;
    int healthChain2  = 0x5F8;
    int healthBase    = 0x40;
    int healthStride  = 8;       // Per body part (health + stun float)

    // Writable position chain:
    // char → AnimationClassHuman* (+animClassOffset)
    //   → CharMovement* (+0xC0)
    //     → writable Vec3 (+0x320)
    //       → x float (+0x20)
    int animClassOffset      = -1;    // Runtime probed (range 0x60-0x200)
    int charMovementOffset   = 0xC0;  // KServerMod verified
    int writablePosOffset    = 0x320; // KServerMod verified
    int writablePosVecOffset = 0x20;  // KServerMod verified

    // Money chain: char+0x298 → +0x78 → +0x88 = money (int)
    int moneyChain1   = 0x298;
    int moneyChain2   = 0x78;
    int moneyBase     = 0x88;
};
```

### WorldOffsets

```cpp
struct WorldOffsets {
    int timeOfDay      = -1;     // On TimeManager (+0x08), NOT GameWorld
    int gameSpeed      = 0x700;  // GameWorld+0x700 (KenshiLib verified)
    int weatherState   = -1;
    int characterList  = 0x0888; // GameWorld+0x0888 (KenshiLib verified)
    int buildingList   = -1;
    int zoneManager    = 0x08B0; // GameWorld+0x08B0 (KenshiLib verified)
    int characterCount = -1;     // Derived from lektor length
};
```

### TimeManager Offsets (NOT on GameWorld — captured via Hook_TimeUpdate)

| Field | Offset | Type |
|-------|--------|------|
| timeOfDay | +0x08 | float (0.0 = midnight, 0.5 = noon) |
| gameSpeed | +0x10 | float (1.0 = normal) |

### BuildingOffsets

```cpp
struct BuildingOffsets {
    int name           = 0x10;   int position       = 0x48;
    int rotation       = 0x58;   int ownerFaction   = 0x80;
    int health         = 0xA0;   int maxHealth      = 0xA4;
    int isDestroyed    = 0xA8;   int functionality  = 0xC0;
    int inventory      = 0xE0;   int townId         = 0x100;
    int buildProgress  = 0x110;  int isConstructed  = 0x114;
};
```

### InventoryOffsets

```cpp
struct InventoryOffsets {
    int items          = 0x10;   int itemCount      = 0x18;
    int width          = 0x20;   int height         = 0x24;
    int owner          = 0x28;   int maxStackMult   = 0x30;
};
```

### ItemOffsets

```cpp
struct ItemOffsets {
    int name           = 0x10;   int templateId     = 0x20;
    int stackCount     = 0x30;   int quality        = 0x38;
    int value          = 0x40;   int weight         = 0x48;
    int equipSlot      = 0x50;   int condition      = 0x58;
};
```

### FactionOffsets

```cpp
struct FactionOffsets {
    int name           = 0x10;   int members        = 0x30;
    int memberCount    = 0x38;   int relations      = 0x50;
    int color1         = 0x80;   int color2         = 0x84;
    int isPlayerFaction = 0x90;  int money          = 0xA0;
};
```

### StatsOffsets

```cpp
struct StatsOffsets {
    // Combat
    int meleeAttack    = 0x00;   int meleeDefence   = 0x04;
    int dodge          = 0x08;   int martialArts    = 0x0C;
    int strength       = 0x10;   int toughness      = 0x14;
    int dexterity      = 0x18;   int athletics      = 0x1C;
    // Ranged
    int crossbows      = 0x20;   int turrets        = 0x24;
    int precision      = 0x28;
    // Stealth
    int stealth        = 0x30;   int assassination  = 0x34;
    int lockpicking    = 0x38;   int thievery       = 0x3C;
    // Science
    int science        = 0x40;   int engineering    = 0x44;
    int medic          = 0x48;
    // Labor
    int farming        = 0x50;   int cooking        = 0x54;
    int weaponsmith    = 0x58;   int armoursmith    = 0x5C;
    int labouring      = 0x60;
};
```

### SquadOffsets

```cpp
struct SquadOffsets {
    int name           = 0x10;   int memberList     = 0x28;
    int memberCount    = 0x30;   int factionId      = 0x38;
    int isPlayerSquad  = 0x40;
};
```

### MSVC x64 std::string Layout

| Field | Offset | Notes |
|-------|--------|-------|
| Buffer/Ptr | +0x00 | 16-byte inline SSO buffer; heap ptr when capacity > 15 |
| Size | +0x10 | uint64_t |
| Capacity | +0x18 | uint64_t; if > 15 then +0x00 is heap pointer |

### ActivePlatoon (Squad Injection Target)

| Field | Offset | Source |
|-------|--------|--------|
| skipSpawnCheck2 | +0x58 | CT research |
| squad* | +0x78 | CT research |
| handleList | +0x80 | CT research |
| leader | +0xA0 | CT research |
| skipSpawnCheck1 | +0xF0 | CT research |
| faction (via platoon) | platoon+0x10 | CT research |
| activePlatoon (via platoon) | platoon+0x1D8 | CT research |

---

## Accessor Classes

### CharacterAccessor

```cpp
class CharacterAccessor {
public:
    explicit CharacterAccessor(void* characterPtr);
    bool IsValid() const;
    Vec3 GetPosition() const;
    Quat GetRotation() const;
    float GetHealth(BodyPart part) const;  // Via health chain
    bool IsAlive() const;
    bool IsPlayerControlled() const;
    std::string GetName() const;           // MSVC std::string read
    bool WriteName(const std::string& name); // SSO-safe write
    uintptr_t GetInventoryPtr() const;
    uintptr_t GetFactionPtr() const;
    bool WriteFaction(uintptr_t factionPtr);
    uintptr_t GetGameDataPtr() const;
    bool WritePosition(const Vec3& pos);   // 3 fallback methods
    TaskType GetCurrentTask() const;
    uintptr_t GetStatsPtr() const;
    uintptr_t GetEquipmentSlot(EquipSlot slot) const;
    uintptr_t GetSquadPtr() const;         // Heuristic scan
    int GetMoney() const;                  // Via money chain
    bool SetPlayerControlled(bool controlled); // Requires discovered offset
    uintptr_t GetPtr() const;
};
```

**WritePosition() — 3 Fallback Methods:**
1. Call game's `HavokCharacter::setPosition` (best — physics-aware)
2. Write via `animClassOffset → +0xC0 → +0x320 → +0x20` chain (physics engine position)
3. Write cached read-only position at +0x48 (may drift)

**GetSquadPtr() — Heuristic Discovery:**
Scans candidate offsets (0x08, 0x20, 0x28, 0x30, 0x38) for valid squad pointer patterns, validating name string at squad+0x10.

### Other Accessors

- **SquadAccessor** — GetName, GetMemberCount, GetMember(index), GetFactionPtr, IsPlayerSquad
- **InventoryAccessor** — GetItemCount, GetItem(index), AddItem, RemoveItem, SetEquipment
- **BuildingAccessor** — GetName, GetPosition, GetHealth, GetBuildProgress, IsDestroyed
- **FactionAccessor** — GetName, GetMemberCount, IsPlayerFaction, GetMoney
- **StatsAccessor** — GetMeleeAttack, GetStrength, GetToughness, GetDexterity, etc.
- **GameWorldAccessor** — GetTimeOfDay, GetGameSpeed, WriteTimeOfDay, WriteGameSpeed
- **CharacterIterator** — HasNext, Next, Reset, Count (uses PlayerBase or GameWorld+0x0888 fallback)

---

## Game Enums

```cpp
enum class TaskType : uint32_t {
    NULL_TASK=0, MOVE_ON_FREE_WILL=1, BUILD=2, PICKUP=3, MELEE_ATTACK=4,
    FOCUSED_MELEE_ATTACK=5, EQUIP_WEAPON=6, UNEQUIP_WEAPON=7,
    CHOOSE_ENEMY_AND_ATTACK=9, IDLE=13, PROTECT_ALLIES=14, ATTACK_ENEMIES=15,
    PATROL=21, FIRST_AID_ORDER=26, LOOT_TARGET=27, HOLD_POSITION=31,
    FOLLOW_PLAYER_ORDER=45, OPERATE_MACHINERY=88, USE_TRAINING_DUMMY=98,
    USE_BED=99, USE_TURRET=148, MAN_A_TURRET=151, RANGED_ATTACK=278,
    SHOOT_AT_TARGET=244,
};

enum class AttachSlot : uint8_t {
    WEAPON=0, BACK=1, HAIR=2, HAT=3, EYES=4, BODY=5, LEGS=6, NONE=7,
    SHIRT=8, BOOTS=9, GLOVES=10, NECK=11, BACKPACK=12, BEARD=13, BELT=14,
};

enum class CharacterType : uint8_t {
    OT_NONE=0, OT_MILITARY=1, OT_CIVILIAN=2, OT_TRADER=3,
    OT_SLAVE=4, OT_NOBLE=5, OT_BANDIT=6, OT_ANIMAL=7,
};

enum class WeatherType : uint8_t {
    WA_NONE=0, WA_RAIN=1, WA_ACID_RAIN=2, WA_DUST_STORM=3, WA_GAS=4,
};

enum class BuildingFunction : uint8_t {
    BF_ANY=0, BF_BED=1, BF_CAGE=2, BF_STORAGE=3, BF_CRAFTING=4,
    BF_RESEARCH=5, BF_TURRET=6, BF_GENERATOR=7, BF_FARM=8, BF_MINE=9,
    BF_TRAINING=10,
};
```

---

## Function Pointer Typedefs

All functions use `__fastcall` calling convention (MSVC x64 default).

```cpp
namespace func_types {
// Entity lifecycle
using CharacterCreateFn    = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn   = void(__fastcall*)(void* nodeList, void* building);
using CreateRandomSquadFn  = void*(__fastcall*)(void* factory, void* squadTemplate);
using CharacterSerialiseFn = void(__fastcall*)(void* character, void* stream);

// Movement
using SetPositionFn  = void(__fastcall*)(void* havokChar, float x, float y, float z);
using MoveToFn       = void(__fastcall*)(void* character, float x, float y, float z, int moveType);

// Combat
using ApplyDamageFn       = void(__fastcall*)(void* target, void* attacker, int bodyPart, float cut, float blunt, float pierce);
using StartAttackFn       = void(__fastcall*)(void* attacker, void* target, void* weapon);
using CharacterDeathFn    = void(__fastcall*)(void* character, void* killer);
using CharacterKOFn       = void(__fastcall*)(void* character, void* attacker, int reason);
using HealthUpdateFn      = void(__fastcall*)(void* character);
using MartialArtsCombatFn = void(__fastcall*)(void* attacker, void* target);

// World / Zones
using ZoneLoadFn          = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using ZoneUnloadFn        = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using BuildingPlaceFn     = void(__fastcall*)(void* world, void* building, float x, float y, float z);
using BuildingDestroyedFn = void(__fastcall*)(void* building);

// Game loop
using GameFrameUpdateFn = void(__fastcall*)(void* rcx, void* rdx);
using TimeUpdateFn      = void(__fastcall*)(void* timeManager, float deltaTime);

// Save / Load
using SaveGameFn   = void(__fastcall*)(void* saveManager, const char* saveName);
using LoadGameFn   = void(__fastcall*)(void* saveManager, const char* saveName);
using ImportGameFn = void(__fastcall*)(void* saveManager, const char* saveName);

// Squad / Platoon
using SquadCreateFn    = void*(__fastcall*)(void* squadManager, void* templateData);
using SquadAddMemberFn = void(__fastcall*)(void* squad, void* character);

// Inventory / Items
using ItemPickupFn = void(__fastcall*)(void* inventory, void* item, int quantity);
using ItemDropFn   = void(__fastcall*)(void* inventory, void* item);
using BuyItemFn    = void(__fastcall*)(void* buyer, void* seller, void* item, int quantity);

// Faction
using FactionRelationFn = void(__fastcall*)(void* factionA, void* factionB, float relation);

// AI
using AICreateFn   = void*(__fastcall*)(void* character, void* faction);
using AIPackagesFn = void(__fastcall*)(void* character, void* aiPackage);

// Turret
using GunTurretFn     = void(__fastcall*)(void* turret, void* operator_);
using GunTurretFireFn = void(__fastcall*)(void* turret, void* target);
}
```

---

## Pattern Scanner & Discovery Orchestrator

### Scanner Engine

- **SIMD acceleration**: SSE2 first-byte scanning (16 bytes at a time)
- **IDA-style patterns**: `"48 8B C4 55 ? ? 41 54"` with `?` wildcards
- **Multi-pattern batch scan**: Single pass for N patterns
- **PE section enumeration**: Maps .text, .rdata, .data, .pdata, .reloc
- **RIP-relative resolution**: Follows `[rip+disp32]` addressing
- **Call/Jump following**: `E8`/`E9` instruction targets
- **Result caching**: Repeated queries use cached results

### 8-Phase Discovery Pipeline

| Phase | Name | Method |
|-------|------|--------|
| 1 | PData | Enumerate .pdata exception entries to find all function boundaries |
| 2 | Strings | Scan for known strings, find xrefs via RIP-relative LEA, resolve function starts |
| 3 | VTables | Scan for RTTI vtables in .rdata, resolve virtual function addresses |
| 4 | PatternScan | SIMD batch pattern matching |
| 5 | StringFallback | String xref as secondary resolver for failed patterns |
| 6 | CallGraph | Build call graph from E8 xrefs; propagate labels |
| 7 | GlobalPointers | Discover PlayerBase/GameWorld via MOV/LEA [rip+disp32] in known functions |
| 8 | EmergencyCritical | Aggressive fallbacks for critical functions only |

### Resolution Methods

```cpp
enum class ResolutionMethod {
    None, PatternScan, StringXref, VTableSlot, CallGraphTrace,
    HardcodedOffset, PDataLookup, ComplexPattern, Manual
};
```

### 41 Registered Patterns

**Entity Lifecycle (5):** CharacterSpawn (0x00581770), CharacterDestroy (0x0038A720), CreateRandomSquad (0x00583A10), CharacterSerialise (0x006280A0), CharacterKO (0x00345C10)

**Movement (2):** CharacterSetPosition (0x00145E50), CharacterMoveTo (0x002EF4E3)

**Combat (7):** ApplyDamage (0x007A33A0), StartAttack (0x007B2A20), CharacterDeath (0x007A6200), HealthUpdate (0x0086B2B0), CutDamageMod (0x00889CD0), UnarmedDamage (0x000CE2D0), MartialArtsCombat (0x00892120)

**World/Zones (7):** ZoneLoad (0x00377710), ZoneUnload (0x002EF1F0), BuildingPlace (0x0057CC70), BuildingDestroyed (0x00557280), Navmesh (0x00881950), SpawnCheck (0x004FFAD0)

**Game Loop (2, CRITICAL):** GameFrameUpdate (0x00123A10), TimeUpdate (0x00214B50)

**Save/Load (4):** SaveGame (0x007EF040), LoadGame (0x00373F00), ImportGame (0x00378A30), CharacterStats (0x008BA700)

**Squad (2):** SquadCreate (0x00480B50), SquadAddMember (vtable slot 2 @ 0x00928423)

**Inventory (3):** ItemPickup (0x0074C8B0), ItemDrop (0x00745DE0), BuyItem (0x0074A630)

**Faction (1):** FactionRelation (0x00872E00)

**AI (2, CRITICAL):** AICreate (0x00622110), AIPackages (0x00271620)

**Turret (2):** GunTurret (0x0043B690), GunTurretFire (0x0043CDB0)

**Building (3):** BuildingDismantle (0x002A2860), BuildingConstruct (0x005547F0), BuildingRepair (0x00555650)

**Global Pointers (2, CRITICAL):** PlayerBase (0x01AC8A90), GameWorldSingleton (0x02133040)

### String Anchors (40 unique)

Examples: `"[RootObjectFactory::process] Character"`, `"zone.%d.%d.zone"`, `"[AI::create] No faction for"`, `"knockout"`, `"pathfind"`, `"Kenshi 1.0."`, `"quicksave"`, `"faction relation"`

---

## Hook Modules (14 Systems)

### 1. entity_hooks
- **Intercepts:** CharacterCreate, CharacterDestroy
- **Key feature:** In-place replay spawn mechanism (piggybacks on natural NPC creation)
- **Safety:** MovRaxRsp trampoline wrapper, reentrant guard, SEH on all memory ops
- **State:** Loading burst detection (5+ creates in 500ms), per-player spawn cap (4), faction bootstrap, pre-call struct capture (1024 bytes)

### 2. world_hooks
- **Intercepts:** ZoneLoad, ZoneUnload
- **Key feature:** Sends zone requests to server, cleans up zone entities on unload
- **Note:** BuildingPlace hook DISABLED (unverified signature, 300+ garbage-coordinate crashes during save load)

### 3. game_tick_hooks
- **Intercepts:** GameFrameUpdate
- **Key feature:** Main game tick driver, calls original via TRAMPOLINE (not HookBypass which froze threads)
- **Safety:** MovRaxRsp fix, diagnostic logging

### 4. combat_hooks
- **Intercepts:** ApplyDamage, CharacterDeath, CharacterKO
- **Key feature:** Sends attack intent for owned characters, death/KO notifications
- **Safety:** HookHealth diagnostics (call/crash counters)

### 5. movement_hooks
- **Intercepts:** NONE (both start with `mov rax, rsp` or have stack params)
- **Key feature:** Positions polled via CharacterAccessor instead of hooked

### 6. input_hooks
- **Intercepts:** NONE (placeholder — input handled by render_hooks WndProc)

### 7. render_hooks
- **Intercepts:** DXGI Present (vtable index 8), WndProc
- **Key feature:** Drives UI update (NativeHud, NativeMenu), OnGameTick, main menu MULTIPLAYER button detection
- **Input:** F1=menu, Tab=player list, Insert=log, Enter=chat, Backtick=debug, Esc=close

### 8. inventory_hooks
- **Intercepts:** ItemPickup, ItemDrop, BuyItem
- **Key feature:** Sends item/trade events to server
- **Safety:** Loading suppress flag

### 9. faction_hooks
- **Intercepts:** FactionRelation
- **Key feature:** Server-sourced flag prevents recursive C2S feedback on S2C faction changes
- **Safety:** Loading + server-sourced suppression

### 10. building_hooks
- **Intercepts:** BuildingPlace, BuildingDestroyed, BuildingDismantle, BuildingConstruct, BuildingRepair
- **Key feature:** Auto-disable after 3 crashes (wrong function matched)
- **Safety:** SEH, crash counters, loading suppress

### 11. ai_hooks
- **Intercepts:** AICreate, AIPackages
- **Key feature:** Remote-controlled character tracking (set + mutex), AI decision override for remote characters
- **Critical:** AI controller always created (no nullptr returns) to prevent downstream crashes

### 12. time_hooks
- **Intercepts:** TimeUpdate
- **Key feature:** Captures TimeManager pointer (one-time), server time override, direct read/write of timeOfDay and gameSpeed

### 13. resource_hooks
- **Status:** NOT IMPLEMENTED (framework only for Ogre resource manager discovery)

### 14. save_hooks
- **Status:** DISABLED (function signatures unverified, pass-through mode)

### 15. squad_hooks
- **Intercepts:** NONE (both hooks disabled — zone load crash risk)
- **Key feature:** Squad injection via `AddCharacterToLocalSquad()` — calls SquadAddMember directly (not hooked)
- **ActivePlatoon resolution:** Struct scanning (char+0x600..0x780) + RTTI vtable validation

---

## Hook Manager (MinHook Wrapper)

```cpp
class HookManager {
    Initialize();
    Shutdown();
    Install<T>(name, target, detour, original);
    InstallAt<T>(name, address, detour, original);
    Remove(name);
    RemoveAll();
    Enable(name);
    Disable(name);
    InstallVTableHook(name, vtable, index, detour, original);
    GetRawTrampoline(name);    // MinHook raw trampoline (safe for reentrant calls)
    GetCustomCaller(name);     // MovRaxRsp wrapper trampoline
    GetDiagnostics();          // vector<HookDiag> snapshot
};
```

**MovRaxRsp Fix:** Auto-applied to functions starting with `48 8B C4` (`mov rax, rsp`). Builds naked detour + trampoline wrapper to fix RAX/RBP register state.

---

## Core Initialization & Game Tick Pipeline

### Initialization Sequence

1. Logging & VEH setup
2. Platform detection (Steam vs GOG)
3. Config loading (ClientConfig from disk)
4. Game offsets initialization
5. Pattern scanner + PatternOrchestrator (8-phase discovery)
6. Hook initialization (MinHook)
7. Early hooks (CharacterCreate — captures factory during loading)
8. Network initialization (ENet client)
9. Packet handler registration
10. SpawnManager callback setup
11. UI initialization (Native MyGUI menu)
12. Command registry
13. Task orchestrator (2 background workers)
14. SyncOrchestrator construction
15. SyncFacilitator binding
16. PipelineOrchestrator initialization
17. Network thread start (background ENet pump)

### OnGameTick Pipeline (Legacy)

```
Step 1:  Entity scan (first 30s after connect)
Step 1b: Deferred re-scan after faction bootstrap
Step 2:  Wait for background frame work
Step 3:  Swap read/write buffers
Step 4:  Interpolation update
Step 5:  Apply remote positions to game objects
Step 6:  Poll local entity positions
Step 7:  Send cached packets
Step 8:  Loading orchestrator tick
Step 9:  Handle spawn queue
Step 10: Handle host teleport
Step 11: Kick background work (read entities + interpolate)
Step 12: (DISABLED) Deferred probes
Step 13: Diagnostics update
Step 14: Pipeline debugger tick
```

---

## Sync Orchestrator (7-Stage Pipeline)

```cpp
class SyncOrchestrator {
    // 7-Stage Pipeline (called from Tick)
    StageUpdateZones();           // 1: Update local zone from primary character position
    StageSwapBuffers();           // 2: Wait for BG work, swap double buffers
    StageApplyRemotePositions();  // 3: Write interpolated positions to game objects
    StagePollAndSendPositions();  // 4: Poll local entities, queue position packets
    StageProcessSpawns();         // 5: Handle spawn queue (in-place replay only)
    StageKickBackgroundWork();    // 6: Start BG read + interpolation tasks
    StageUpdatePlayers(dt);       // 7: Update player session states

    // Background workers
    BackgroundReadEntities();     // Read entity state from game memory
    BackgroundInterpolate();      // Compute interpolated positions for render
};
```

**SpawnCharacterDirect DISABLED** in StageProcessSpawns — faction use-after-free crash. Only in-place replay (entity_hooks) is active.

---

## Entity Registry

```cpp
class EntityRegistry {
    EntityID Register(void* gameObject, EntityType type, PlayerID owner);
    EntityID RegisterRemote(EntityID netId, EntityType type, PlayerID owner, const Vec3& pos);
    void* GetGameObject(EntityID netId) const;
    EntityID GetNetId(void* gameObject) const;
    void SetGameObject(EntityID netId, void* gameObject);  // Links remote → game object
    void UpdatePosition/Rotation/Owner/Equipment(...);
    bool RemapEntityId(EntityID oldId, EntityID newId);     // Server ID assignment
    EntityID FindLocalEntityNear(const Vec3& pos, PlayerID owner, float maxDist);
    void Unregister(EntityID netId);
    void ClearRemoteEntities();
    std::vector<EntityID> GetPlayerEntities(PlayerID playerId) const;
    std::vector<EntityID> GetEntitiesInZone(const ZoneCoord& zone) const;
    // Thread safety: shared_mutex (concurrent reads, exclusive writes)
    // ID space: local IDs start at 0x10000000 to avoid server collisions
};
```

---

## Entity Resolver (Interest & Dirty Tracking)

```cpp
class EntityResolver {
    std::vector<EntityID> Query(const EntityFilter& filter) const;
    std::vector<EntityID> InRadius(const Vec3& center, float radius) const;
    std::vector<EntityID> InZoneNeighborhood(const ZoneCoord& center) const;
    InterestSet ComputeInterest(PlayerID, const ZoneCoord&);  // 3x3 grid + enter/leave deltas
    void MarkDirty(EntityID id, uint16_t flags);
    std::vector<EntityID> ConsumeDirty(uint16_t mask);
    bool IsLocallyOwned(EntityID id, PlayerID localPlayer) const;
    void TransferToServer(PlayerID fromPlayer);
};
```

---

## Zone Engine

```cpp
class ZoneEngine {
    bool UpdateLocalPlayerZone(const Vec3& position);  // Returns true on zone change
    ZoneCoord GetLocalZone() const;
    std::vector<ZoneCoord> GetInterestZones() const;   // 3x3 grid
    void RebuildZoneIndex();                            // Every 500ms or on zone change
    void UpdatePlayerZone(PlayerID, const ZoneCoord&);
    bool IsInRange(const ZoneCoord& entityZone) const;
    bool ShouldSync(EntityID entityId) const;
};
```

---

## Player Engine (Session State Machine)

```cpp
enum class PlayerState : uint8_t {
    Connecting=0, Loading=1, InGame=2, AFK=3, Disconnected=4
};

// Transitions:
// Connecting → Loading (OnHandshakeAck)
// Loading → InGame (OnWorldSnapshotReceived)
// InGame → AFK (300s no activity)
// AFK → InGame (activity detected)
// Any → Disconnected (OnRemotePlayerLeft)
```

---

## Interpolation System

- **Buffer:** 8-entry ring buffer per entity
- **Adaptive delay:** Jitter EMA (20ms→50ms delay, 80ms→200ms delay)
- **Snap correction:** Large discontinuities blended over 150ms; >50m = instant teleport
- **Extrapolation:** Dead reckoning for up to 250ms past last snapshot
- **Normal case:** Linear lerp between bracket snapshots at `renderTime - adaptiveDelay`

---

## Network Protocol

### Channels
| Channel | Type | Usage |
|---------|------|-------|
| 0 | Reliable Ordered | Connection, world state, entity lifecycle, buildings, squads, chat |
| 1 | Reliable Unordered | Combat, stats, inventory, pipeline debug |
| 2 | Unreliable Sequenced | Position updates |

### Packet Header (8 bytes)

```cpp
struct PacketHeader {
    MessageType type;      // 1 byte
    uint8_t     flags;     // Bit 0: compressed
    uint16_t    sequence;
    uint32_t    timestamp; // Server tick
};
```

---

## Message Types (Complete Enum)

```
Connection:    C2S_Handshake=0x01, S2C_HandshakeAck=0x02, S2C_HandshakeReject=0x03,
               C2S_Disconnect=0x04, S2C_PlayerJoined=0x05, S2C_PlayerLeft=0x06,
               C2S_Keepalive=0x07, S2C_KeepaliveAck=0x08

World:         S2C_WorldSnapshot=0x10, S2C_TimeSync=0x11, S2C_ZoneData=0x12,
               C2S_ZoneRequest=0x13

Entity:        S2C_EntitySpawn=0x20, S2C_EntityDespawn=0x21,
               C2S_EntitySpawnReq=0x22, C2S_EntityDespawnReq=0x23

Movement:      C2S_PositionUpdate=0x30, S2C_PositionUpdate=0x31,
               C2S_MoveCommand=0x32, S2C_MoveCommand=0x33

Combat:        C2S_AttackIntent=0x40, S2C_CombatHit=0x41, S2C_CombatBlock=0x42,
               S2C_CombatDeath=0x43, S2C_CombatKO=0x44, C2S_CombatStance=0x45,
               C2S_CombatDeath=0x46

Stats:         S2C_StatUpdate=0x50, S2C_HealthUpdate=0x51,
               S2C_EquipmentUpdate=0x52, C2S_EquipmentUpdate=0x53

Inventory:     C2S_ItemPickup=0x60, C2S_ItemDrop=0x61, C2S_ItemTransfer=0x62,
               S2C_InventoryUpdate=0x63, C2S_TradeRequest=0x64, S2C_TradeResult=0x65

Buildings:     C2S_BuildRequest=0x70, S2C_BuildPlaced=0x71, S2C_BuildProgress=0x72,
               S2C_BuildDestroyed=0x73, C2S_DoorInteract=0x74, S2C_DoorState=0x75,
               C2S_BuildDismantle=0x76, C2S_BuildRepair=0x77

Chat:          C2S_ChatMessage=0x80, S2C_ChatMessage=0x81, S2C_SystemMessage=0x82

Admin:         C2S_AdminCommand=0x90, S2C_AdminResponse=0x91

Query:         C2S_ServerQuery=0xA0, S2C_ServerInfo=0xA1

Squad:         C2S_SquadCreate=0xB0, S2C_SquadCreated=0xB1,
               C2S_SquadAddMember=0xB2, S2C_SquadMemberUpdate=0xB3

Faction:       C2S_FactionRelation=0xC0, S2C_FactionRelation=0xC1

Master:        MS_Register=0xD0, MS_Heartbeat=0xD1, MS_Deregister=0xD2,
               MS_QueryList=0xD3, MS_ServerList=0xD4

Pipeline:      C2S_PipelineSnapshot=0xE0, S2C_PipelineSnapshot=0xE1,
               C2S_PipelineEvent=0xE2, S2C_PipelineEvent=0xE3
```

**Handler Coverage:** C2S 23/23 (100%), S2C 28/28 (100%)

---

## Constants & Limits

```cpp
// Protocol
KMP_PROTOCOL_VERSION = 1
KMP_DEFAULT_PORT     = 27800
KMP_MAX_PLAYERS      = 16
KMP_MAX_NAME_LENGTH  = 31

// Tick rates
KMP_TICK_RATE         = 20       // 20 Hz
KMP_TICK_INTERVAL_MS  = 50
KMP_TICK_INTERVAL_SEC = 0.05f

// Interpolation
KMP_INTERP_DELAY_SEC  = 0.1f    // 100ms default
KMP_MAX_SNAPSHOTS     = 8       // Ring buffer
KMP_INTERP_DELAY_MIN  = 0.05f   // 50ms
KMP_INTERP_DELAY_MAX  = 0.2f    // 200ms
KMP_EXTRAP_MAX_SEC    = 0.25f   // 250ms max extrapolation
KMP_SNAP_THRESHOLD_MIN = 5.0f   // Below: smooth blend
KMP_SNAP_THRESHOLD_MAX = 50.0f  // Above: instant teleport
KMP_SNAP_CORRECT_SEC   = 0.15f  // 150ms blend

// Networking
KMP_CHANNEL_COUNT = 3
KMP_UPSTREAM_LIMIT   = 128 KB/s
KMP_DOWNSTREAM_LIMIT = 256 KB/s
KMP_CONNECT_TIMEOUT_MS = 5000
KMP_KEEPALIVE_INTERVAL = 1000
KMP_TIMEOUT_MS         = 10000

// Zone system
KMP_ZONE_SIZE       = 750.f     // Meters per zone
KMP_INTEREST_RADIUS = 1         // +/-1 zone (3x3 grid)

// Position thresholds
KMP_POS_CHANGE_THRESHOLD = 0.1f
KMP_ROT_CHANGE_THRESHOLD = 0.01f

// Entity limits
KMP_MAX_ENTITIES_PER_ZONE = 512
KMP_MAX_SYNC_ENTITIES     = 2048

// ID ranges
Player:    1 - 255
NPC:       256 - 8191
Building:  8192 - 16383
Container: 16384 - 24575
Squad:     24576 - 32767
Server squads: 0x80000000+
```

---

## Type Definitions

```cpp
using EntityID = uint32_t;    constexpr EntityID INVALID_ENTITY = 0;
using PlayerID = uint32_t;    constexpr PlayerID INVALID_PLAYER = 0;
using TickNumber = uint32_t;

struct Vec3 { float x, y, z; /* +, -, *, LengthSq, Length, DistanceTo */ };
struct Quat { float w, x, y, z; /* Compress, Decompress, Slerp */ };
struct ZoneCoord { int32_t x, y; /* ==, !=, IsAdjacent, FromWorldPos */ };

enum class EntityState : uint8_t  { Inactive=0, Spawning=1, Active=2, Despawning=3, Frozen=4 };
enum class AuthorityType : uint8_t { None=0, Local=1, Remote=2, Host=3, Transferring=4 };
enum class EntityType : uint8_t { PlayerCharacter=0, NPC=1, Animal=2, Building=3, WorldBuilding=4, Item=5, Turret=6 };
enum class BodyPart : uint8_t { Head=0, Chest=1, Stomach=2, LeftArm=3, RightArm=4, LeftLeg=5, RightLeg=6, Count=7 };
enum class EquipSlot : uint8_t { Weapon=0..Belt=13, Count=14 };

enum DirtyFlags : uint16_t {
    Dirty_Position=0x001, Dirty_Rotation=0x002, Dirty_Animation=0x004,
    Dirty_Health=0x008, Dirty_Stats=0x010, Dirty_Inventory=0x020,
    Dirty_CombatState=0x040, Dirty_LimbDamage=0x080, Dirty_SquadInfo=0x100,
    Dirty_FactionRel=0x200, Dirty_Equipment=0x400, Dirty_AIState=0x800,
    Dirty_All=0xFFF
};
```

---

## Compression

```cpp
// Half-float (float16) for position deltas
struct DeltaPosition {
    uint16_t dx, dy, dz;  // 6 bytes vs 12 for full Vec3
    static DeltaPosition Encode(const Vec3& current, const Vec3& previous);
    Vec3 Decode(const Vec3& previous) const;
};

// Packed velocity (3 bytes)
struct PackedVelocity {
    int8_t vx, vy, vz;  // Scaled to [-15, 15] m/s
};

// Quaternion: smallest-three encoding (uint32_t)
// Drop largest component, encode 3 remaining as 10-bit each + 2-bit index
```

---

## Configuration

```cpp
struct ClientConfig {
    playerName     = "Player";
    lastServer     = "162.248.94.149";
    lastPort       = 27800;
    autoConnect    = true;
    overlayScale   = 1.0f;
    masterServer   = "162.248.94.149";
    masterPort     = 27801;
    favoriteServers = {"162.248.94.149:27800"};
    useSyncOrchestrator = false;
    // Persisted to %APPDATA%/KenshiMP/client.json
};

struct ServerConfig {
    serverName   = "KenshiMP Server";
    port         = 27800;
    maxPlayers   = 16;
    password     = "";
    savePath     = "world.kmpsave";
    tickRate     = 20;
    pvpEnabled   = true;
    gameSpeed    = 1.0f;
    masterServer = "162.248.94.149";
    masterPort   = 27801;
    // Loaded from server.json
};
```

---

## Game Server

```cpp
class GameServer {
    // 23 C2S message handlers (100% coverage)
    // Broadcasting: Broadcast, BroadcastExcept, SendTo
    // World snapshot: sends all entities + equipment on player join
    // Position sync: per-player zone-filtered broadcasts (20 Hz)
    // Time sync: broadcast every 5 seconds
    // Master server: register + heartbeat (30s) with exponential backoff reconnect (5s→60s)
    // UPnP: auto port mapping on startup
    // Auto-save: every 60 seconds
    // Console commands: status, players, kick, say, save, stop
};
```

### Server Security

- **Ownership validation:** Every entity modification checks `entity.owner == player.id`
- **Zone bounds:** Zone requests validated against `|zone| <= 500`
- **Combat distance:** Melee < 15m, Ranged < 150m
- **Trade validation:** Quantity 1-10000, price >= 0, seller exists
- **Rate limiting:** Per-player message rate tracking (10/second window)
- **Duplicate connection prevention:** `peer.data != nullptr` check
- **Name sanitization:** Printable ASCII, max 31 chars
- **Ban list:** IP-based banning

---

## Combat Resolver

Kenshi-approximated combat:
- Body part targeting: weighted (Chest 30%, Stomach 20%, Head 10%, Limbs 10% each)
- Damage split: cut/blunt based on weapon type
- Defense: `effective = attack * (1 - defense/100)`
- Block chance: 20% base (blocked hits do 30% damage)
- KO threshold: any body part <= -50
- Death threshold: chest or head <= -100

---

## World Persistence

JSON save format at `world.kmpsave`:
```json
{
  "version": 1,
  "timeOfDay": 0.5,
  "weather": 0,
  "entities": [{
    "id": 256, "type": 1, "owner": 1,
    "templateId": 12345, "factionId": 1,
    "position": [100.0, 50.0, 200.0],
    "rotation": [1.0, 0.0, 0.0, 0.0],
    "alive": true,
    "health": [100, 100, 100, 100, 100, 100, 100],
    "templateName": "Greenlander",
    "equipment": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
  }]
}
```

---

## Master Server

- **Port:** 27801
- **Protocol:** Register → Heartbeat (30s) → Deregister
- **Pruning:** Servers removed after 90s without heartbeat
- **Client query:** Send MS_QueryList, receive MS_ServerList, disconnect
- **Max connections:** 128

---

## UPnP Mapper

Uses Windows COM API (`IUPnPNAT`, `IStaticPortMappingCollection`) with fallback to Windows Firewall rules. Auto-maps configured port on server startup, removes on shutdown.

---

## UI System (Native MyGUI)

### MyGUI Bridge

Resolves all MyGUI DLL exports at runtime via `GetProcAddress` on `MyGUIEngine_x64.dll`. No headers or import libs needed. All calls wrapped in SEH.

Key DLL exports (mangled MSVC names):
- `LayoutManager::getInstance`, `Gui::getInstance`
- `Widget::setVisible`, `TextBox::setCaptionWithReplacing`, `TextBox::getCaption`
- `Gui::findWidgetT`, `Widget::findWidget`
- `LayoutManager::loadLayout`, `LayoutManager::unloadLayout`
- `Widget::setProperty`, `Widget::setAlpha`
- `Gui::createWidgetRealT`, `Widget::createWidgetRealT`

### NativeHud

Thread-safe in-game HUD with 4 panels:
- **Status Bar** — "KENSHI ONLINE | Connected as X | N players | Ping"
- **Log Panel** (Insert) — 20 lines, debug/loading messages
- **Chat Panel** (Enter) — 10 lines + input, 30s fade
- **Player List** (Tab) — 8 player rows

### NativeMenu

F1 multiplayer menu with 4 sub-panels:
- **Main** — Host Game, Join Game, Settings, Server Browser, Back
- **Join** — IP, Port, Player Name, Connect
- **Settings** — Player Name, Auto-Connect
- **Server Browser** — 8 server rows, Refresh from master + favorites

### Layout Files

- `Kenshi_MultiplayerHUD.layout` — HUD widgets using `Kenshi_FloatingPanelSkin`
- `Kenshi_MultiplayerPanel.layout` — Menu panel with native Kenshi buttons/fonts
- `Kenshi_MainMenu.layout` — Adds MULTIPLAYER button to main menu

---

## Injector & Launcher

1. **InstallOgrePlugin** — Adds `Plugin=KenshiMP.Core` to `Plugins_x64.cfg`
2. **CopyPluginDll** — Copies `KenshiMP.Core.dll` to game directory
3. **WriteConnectConfig** — Creates `%APPDATA%/KenshiMP/client.json`
4. **LaunchKenshi** — Via `steam://rungameid/233860` (fallback: direct CreateProcessW)
5. **FindKenshiPath** — Registry lookup (`HKLM\SOFTWARE\WOW6432Node\Valve\Steam`)

---

## Thread Safety Model

| System | Mutex Type | Protection |
|--------|-----------|-----------|
| EntityRegistry | shared_mutex | Concurrent reads, exclusive writes |
| EntityResolver | mutex | Per-player interest set |
| ZoneEngine | mutex | Zone index + player-zone mappings |
| PlayerEngine | mutex | Player state + sessions |
| Interpolation | mutex | Snapshot buffers + jitter estimators |
| PipelineOrchestrator | mutex (3x) | Events, anomalies, remote snapshots |
| NetworkClient | mutex | ENet peer send |
| NativeHud | mutex (2x) | Chat messages, log entries |

---

*Generated from source code analysis of KenshiMP v1.0.2. All offsets verified against Kenshi v1.0.68 (GOG).*

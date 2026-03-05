# Kenshi Online

**16-player co-op multiplayer mod for Kenshi**

[![Discord](https://img.shields.io/badge/Discord-Join%20Us-5865F2?logo=discord&logoColor=white)](https://discord.gg/uNN6Pwjg)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Release](https://img.shields.io/badge/Release-v0.1.0.1-blue.svg)](https://github.com/The404Studios/Kenshi-Online/releases)

[Русская версия (Russian)](README_RU.md)

Kenshi Online transforms the single-player open-world RPG **Kenshi** into a multiplayer experience. Explore the wasteland, fight, build bases, and trade together with up to 16 players on a single server. Built using native Ogre3D plugin injection, ENet networking, and MyGUI integration — no manual DLL injection required.

---

## Table of Contents

- [Features](#features)
- [Quick Start (Players)](#quick-start-players)
- [Quick Start (Server Hosts)](#quick-start-server-hosts)
- [In-Game Controls](#in-game-controls)
- [Client Commands](#client-commands)
- [Server Commands](#server-commands)
- [Configuration](#configuration)
- [How It Works](#how-it-works)
- [Architecture Overview](#architecture-overview)
- [Project Structure](#project-structure)
- [Network Protocol](#network-protocol)
- [Sync System Deep Dive](#sync-system-deep-dive)
- [Hook System](#hook-system)
- [Building from Source](#building-from-source)
- [Troubleshooting](#troubleshooting)
- [Credits](#credits)
- [License](#license)

---

## Features

- **Up to 16 players** on a single dedicated server
- **Full entity replication** — characters, NPCs, combat, buildings, items, squads, factions
- **Server-authoritative** combat resolution and world state
- **Zone-based interest management** — efficient bandwidth (~6 KB/s per player)
- **Dedicated server** with world persistence, auto-save, and console commands
- **Master server** for centralized server browser (auto-discovery)
- **Native MyGUI HUD** — status bar, timestamped chat, player list, debug panel
- **Ogre plugin injection** — just launch and play, no manual setup
- **13 client commands** — `/tp`, `/players`, `/time`, `/ping`, `/debug`, and more
- **Adaptive interpolation** — hermite spline smoothing, jitter estimation, snap correction
- **UPnP auto-mapping** — server automatically forwards ports when possible
- **Installation script** — one-click `install.bat` for players

---

## Quick Start (Players)

### Option A: One-Click Install
1. Download the latest release from [Releases](https://github.com/The404Studios/Kenshi-Online/releases)
2. Extract the `dist/` folder
3. Run `install.bat` — it auto-detects your Kenshi installation (Steam or GOG)
4. Launch Kenshi normally — you'll see **HOST GAME** and **JOIN GAME** buttons on the main menu
5. Click **JOIN GAME**, enter the server address, and play

### Option B: Injector
1. Download `KenshiMP.Injector.exe` and `KenshiMP.Core.dll` from [Releases](https://github.com/The404Studios/Kenshi-Online/releases)
2. Run `KenshiMP.Injector.exe`
3. Set your **Kenshi path**, **player name**, and **server address**
4. Click **PLAY** — Kenshi launches with multiplayer enabled

### Uninstalling
Run `uninstall.bat` from the dist folder, or manually:
1. Remove the `Plugin=KenshiMP.Core` line from `Plugins_x64.cfg`
2. Delete `KenshiMP.Core.dll` from your Kenshi directory
3. Restore `Kenshi_MainMenu.layout` from the `KenshiMP_backup/` folder

---

## Quick Start (Server Hosts)

### Running a Server

1. Download `KenshiMP.Server.exe` from [Releases](https://github.com/The404Studios/Kenshi-Online/releases)
2. Create a `server.json` config (or let it generate defaults on first run):

```json
{
  "serverName": "My Kenshi Server",
  "port": 27800,
  "maxPlayers": 16,
  "password": "",
  "pvpEnabled": true,
  "gameSpeed": 1.0,
  "savePath": "world.kmpsave",
  "tickRate": 20,
  "masterServer": "127.0.0.1",
  "masterPort": 27801
}
```

3. Run `KenshiMP.Server.exe`
4. **Forward port 27800 UDP** on your router/firewall (UPnP auto-mapping is attempted automatically)
5. Players connect via your IP address or through the in-game server browser

### Server Features
- **Auto-save** every 60 seconds to `world.kmpsave`
- **World persistence** — entities, time, weather saved as JSON
- **UPnP** — automatic port forwarding when available
- **Master server heartbeat** — registers with the server browser every 30 seconds
- **Graceful shutdown** — Ctrl+C or `stop` command saves state before exit

---

## In-Game Controls

| Key | Action |
|-----|--------|
| **F1** | Open/close multiplayer menu |
| **Insert** | Toggle debug/loading log panel |
| **Enter** | Open/close chat |
| **Tab** | Toggle player list |
| **` (backtick)** | Toggle debug info overlay |
| **Escape** | Close all multiplayer panels |

---

## Client Commands

Type these in the chat window (press Enter to open chat):

| Command | Description |
|---------|-------------|
| `/help` | List all available commands |
| `/tp [name]` | Teleport your squad to another player (case-insensitive, prefix match) |
| `/teleport [name]` | Alias for `/tp` |
| `/pos` or `/position` | Show your squad's current position |
| `/players` or `/who` | List all online players |
| `/status` | Show connection status and server info |
| `/connect` | Manually reconnect to the server |
| `/disconnect` | Disconnect from the server |
| `/time` | Show current server time (hours:minutes) |
| `/debug` | Toggle debug overlay with entity/network stats |
| `/entities` | List all synced entities |
| `/ping` | Show your current ping to the server |

---

## Server Commands

Type these in the server console:

| Command | Description |
|---------|-------------|
| `status` | Show server info (players, uptime, TPS) |
| `players` | List connected players with IDs and ping |
| `kick <id>` | Kick a player by their ID |
| `say <msg>` | Broadcast a system message to all players |
| `save` | Force-save world state |
| `stop` / `quit` / `exit` | Save and shutdown the server |
| `help` | List available commands |

---

## Configuration

### Client Config (`client.json`)

Created automatically by the injector or the in-game menu. Stored in `%APPDATA%/KenshiMP/`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `playerName` | string | `"Player"` | Your display name |
| `lastServer` | string | `"127.0.0.1"` | Last connected server IP |
| `lastPort` | uint16 | `27800` | Last connected server port |
| `autoConnect` | bool | `true` | Auto-connect on game launch |
| `overlayScale` | float | `1.0` | UI overlay scale factor |
| `masterServer` | string | `"127.0.0.1"` | Master server address |
| `masterPort` | uint16 | `27801` | Master server port |
| `favoriteServers` | string[] | `["127.0.0.1:27800"]` | Saved server list |
| `useSyncOrchestrator` | bool | `false` | Enable new 7-stage sync pipeline (experimental) |

### Server Config (`server.json`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `serverName` | string | `"KenshiMP Server"` | Server display name |
| `port` | uint16 | `27800` | UDP port to listen on |
| `maxPlayers` | int | `16` | Maximum concurrent players |
| `password` | string | `""` | Server password (empty = no password) |
| `savePath` | string | `"world.kmpsave"` | World save file path |
| `tickRate` | int | `20` | Server tick rate in Hz |
| `pvpEnabled` | bool | `true` | Allow player-vs-player combat |
| `gameSpeed` | float | `1.0` | Game speed multiplier |
| `masterServer` | string | `"127.0.0.1"` | Master server to register with |
| `masterPort` | uint16 | `27801` | Master server port |

---

## How It Works

### Injection Method

Kenshi Online uses the **Ogre3D plugin system** — the same method proven by [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi). The injector adds `Plugin=KenshiMP.Core` to Kenshi's `Plugins_x64.cfg`, and Ogre automatically loads our DLL during engine initialization. **No process injection, no manual DLL loading.**

### What Happens at Launch

1. **Ogre loads KenshiMP.Core.dll** as a plugin during engine startup
2. **Pattern scanner** scans `kenshi_x64.exe` in memory using IDA-style byte patterns with wildcards
3. **14 hook modules** install function hooks via MinHook on key game functions (character creation, combat, movement, inventory, etc.)
4. **Network client** connects to the server via ENet (UDP, 3 channels)
5. **Entity registry** begins tracking local and remote entities
6. **MyGUI integration** adds multiplayer UI elements (chat, player list, status bar)
7. **Game tick loop** synchronizes state at 20 Hz

### Multiplayer Flow

```
┌──────────┐       ENet (UDP)        ┌──────────────┐
│  Client   │◄─────────────────────►│   Server     │
│           │   3 channels, 20 Hz   │              │
│ Core.dll  │                        │ Server.exe   │
│ (plugin)  │                        │              │
├───────────┤                        ├──────────────┤
│ Scanner   │                        │ EntityMgr    │
│ 14 Hooks  │                        │ PlayerMgr    │
│ Registry  │                        │ ZoneMgr      │
│ Interp    │                        │ CombatRes    │
│ SpawnMgr  │                        │ Persistence  │
│ MyGUI UI  │                        │ UPnP         │
└───────────┘                        └──────────────┘
        │                                    │
        │         ENet (UDP)                 │
        │         Port 27801                 │
        │                                    │
        └──────►┌──────────────┐◄────────────┘
                │ MasterServer │
                │ (browser)    │
                └──────────────┘
```

### Entity Ownership Model

- **Each player** owns their squad members — they simulate locally and send updates to the server
- **The server** owns all NPCs and resolves combat/interactions
- **Authority can transfer** during interactions (e.g., when trading or entering combat)
- **Remote entities** are interpolated on each client using a 100ms buffer with hermite spline smoothing

---

## Architecture Overview

```
KenshiMP.Injector.exe     → Modifies Plugins_x64.cfg, launches Kenshi
KenshiMP.Core.dll         → Loaded by Ogre as a plugin, hooks game functions
KenshiMP.Server.exe       → Dedicated server (host on VPS or locally)
KenshiMP.MasterServer.exe → Centralized server browser registry (port 27801)
KenshiMP.Common.lib       → Shared types, protocol, serialization, config
KenshiMP.Scanner.lib      → Pattern scanning engine, MinHook wrapper
```

### Core Subsystems

| Subsystem | Description |
|-----------|-------------|
| **Scanner** | IDA-style pattern matching with wildcards, RIP-relative resolution, vtable discovery |
| **Hook Manager** | MinHook wrapper with trampoline recovery and SEH crash protection |
| **Entity Registry** | Tracks all entities (local + remote) with dirty flags and ownership |
| **Interpolation** | Ring buffer (8 snapshots), adaptive jitter estimation, snap correction |
| **Spawn Manager** | Captures game factory function, in-place replay spawning for remote entities |
| **Player Controller** | Remote player character tracking, name/faction writes |
| **Sync Orchestrator** | NEW: 7-stage pipeline (feature-flagged) for structured sync updates |
| **Task Orchestrator** | Background thread pool with frame-locked synchronization |
| **Network Client** | ENet wrapper with 3 channels (reliable ordered, reliable unordered, unreliable) |
| **Packet Handler** | 50+ message type handlers for all game events |
| **MyGUI Bridge** | Symbol resolution and widget creation for native Kenshi UI |
| **Overlay/HUD/Menu** | Chat, player list, status bar, main menu buttons, server browser |

---

## Project Structure

```
Kenshi-Online/
├── KenshiMP.Common/              # Shared library (types, protocol, config)
│   ├── include/kmp/
│   │   ├── types.h               # Vec3, Quat, EntityID, ZoneCoord, enums
│   │   ├── constants.h           # Tick rate, max players, timeouts, zone size
│   │   ├── messages.h            # 50+ network message structs
│   │   ├── protocol.h            # PacketHeader, PacketWriter, PacketReader
│   │   ├── compression.h         # Delta compression, smallest-three quaternion
│   │   └── config.h              # ClientConfig, ServerConfig
│   └── src/
│       ├── config.cpp            # JSON load/save for client.json, server.json
│       ├── protocol.cpp          # Packet serialization
│       ├── serialization.cpp     # Data serialization helpers
│       └── compression.cpp       # Compression implementation
│
├── KenshiMP.Scanner/             # Pattern scanner library
│   ├── include/kmp/
│   │   ├── scanner.h             # PatternScanner (IDA-style byte patterns)
│   │   ├── patterns.h            # Known Kenshi function signatures
│   │   ├── memory.h              # Safe memory read/write, RIP resolution
│   │   ├── hook_manager.h        # MinHook wrapper with trampoline recovery
│   │   ├── orchestrator.h        # GameFunctions (50+ function pointers)
│   │   ├── scanner_engine.h      # Pattern matching state machine
│   │   ├── vtable_scanner.h      # Virtual method table discovery
│   │   ├── function_analyzer.h   # Call graph analysis
│   │   ├── string_analyzer.h     # String cross-reference scanning
│   │   ├── pdata_enumerator.h    # PE PDATA function boundary walking
│   │   ├── call_graph.h          # Function call relationship tracking
│   │   ├── safe_hook.h           # SEH-protected hook installation
│   │   └── mov_rax_rsp_fix.h     # x64 compatibility fix
│   └── src/                      # Implementations for all above
│
├── KenshiMP.Core/                # Ogre plugin DLL (main mod)
│   ├── dllmain.cpp               # Plugin entry point
│   ├── core.h/cpp                # Master initialization & game loop
│   ├── hooks/                    # 14 game function hook modules
│   │   ├── entity_hooks.*        # CharacterCreate/Destroy, in-place replay
│   │   ├── combat_hooks.*        # Damage, attacks, death, knockout
│   │   ├── movement_hooks.*      # Position updates, move commands
│   │   ├── game_tick_hooks.*     # Main game tick, frame updates
│   │   ├── render_hooks.*        # Present hook for UI rendering
│   │   ├── world_hooks.*         # Zone load/unload, building events
│   │   ├── time_hooks.*          # Time of day, game speed sync
│   │   ├── inventory_hooks.*     # Item pickup, drop, transfer
│   │   ├── squad_hooks.*         # Squad create/destroy/members
│   │   ├── faction_hooks.*       # Faction relation changes
│   │   ├── ai_hooks.*            # AI suppression for remote characters
│   │   ├── input_hooks.*         # Keyboard capture for UI
│   │   ├── building_hooks.*      # Building place/destroy/repair
│   │   ├── save_hooks.*          # Save/load/import interception
│   │   └── resource_hooks.*      # Resource loading coordination
│   ├── game/                     # Reconstructed game types
│   │   ├── game_types.h          # All offset tables and accessor classes
│   │   ├── game_character.cpp    # Character data reading
│   │   ├── game_inventory.*      # Inventory iteration/modification
│   │   ├── game_building.cpp     # Building data queries
│   │   ├── game_squad.cpp        # Squad member iteration
│   │   ├── game_faction.cpp      # Faction relationship queries
│   │   ├── game_stats.cpp        # 23 stat offset definitions
│   │   ├── game_world.cpp        # World state queries
│   │   ├── spawn_manager.*       # Remote entity spawn queue & factory
│   │   ├── player_controller.*   # Remote player tracking
│   │   ├── loading_orchestrator.* # Game load phase detection
│   │   └── asset_facilitator.*   # Asset loading coordination
│   ├── net/                      # Networking
│   │   ├── client.*              # ENet client (3 channels, async connect)
│   │   ├── packet_handler.cpp    # 50+ message type handlers
│   │   └── server_query.*        # Server browser queries
│   ├── sync/                     # Entity synchronization
│   │   ├── entity_registry.*     # Entity tracking, dirty flags, ownership
│   │   ├── interpolation.*       # Ring buffer, jitter, snap correction
│   │   ├── sync_orchestrator.*   # 7-stage pipeline (feature-flagged)
│   │   ├── sync_facilitator.*    # Sync state management
│   │   ├── pipeline_orchestrator.* # Pipeline state debugging
│   │   ├── pipeline_state.h      # Pipeline stage definitions
│   │   ├── entity_resolver.*     # Entity template resolution
│   │   ├── zone_engine.*         # Zone-based interest management
│   │   └── player_engine.*       # Player tracking & priority
│   ├── ui/                       # User interface
│   │   ├── overlay.*             # Chat, player list, connection retry
│   │   ├── native_menu.*         # Main menu (HOST/JOIN/browser)
│   │   ├── native_hud.*          # In-game status bar, chat input
│   │   └── mygui_bridge.*       # MyGUI symbol resolution & helpers
│   └── sys/                      # System utilities
│       ├── task_orchestrator.*    # Background thread pool
│       ├── command_registry.*    # /command parsing & registration
│       ├── builtin_commands.cpp  # 13 client commands
│       └── frame_data.h          # Double-buffered frame work queue
│
├── KenshiMP.Server/              # Dedicated server
│   ├── main.cpp                  # Console entry, signal handlers, commands
│   ├── server.*                  # ENet server, message handlers, broadcasts
│   ├── player_manager.*          # Player tracking, ping, ownership
│   ├── entity_manager.*          # Server entity storage, state management
│   ├── game_state.*              # Time, weather, auto-save (60s)
│   ├── zone_manager.*            # Zone interest filtering
│   ├── combat_resolver.cpp       # Damage, death, knockout resolution
│   ├── world_persistence.cpp     # JSON save/load (entities, time, weather)
│   └── upnp.*                    # Automatic port forwarding
│
├── KenshiMP.MasterServer/        # Server browser registry
│   └── main.cpp                  # ENet server (port 27801)
│
├── KenshiMP.Injector/            # Game launcher
│   ├── main.cpp                  # Win32 GUI (path, name, server, PLAY)
│   ├── injector.*                # Plugins_x64.cfg modifier
│   └── process.*                 # Game process launcher
│
├── KenshiMP.UnitTest/            # Unit tests (registry, character, interpolation)
├── KenshiMP.IntegrationTest/     # Integration tests
├── KenshiMP.LiveTest/            # Live game testing harness
├── KenshiMP.TestClient/          # Standalone test client
│
├── dist/                         # Distribution files
│   ├── install.bat               # One-click player installer
│   ├── uninstall.bat             # Cleanup script
│   ├── Kenshi_MainMenu.layout    # UI layout with MULTIPLAYER button
│   ├── Kenshi_MultiplayerPanel.layout  # Menu panel layout
│   ├── Kenshi_MultiplayerHUD.layout    # In-game HUD layout
│   └── JOINING.md                # Player guide
│
├── docs/                         # Documentation
│   ├── PHASES.md                 # 13-phase lifecycle audit (all WORKING)
│   ├── patterns.json             # Function signature database
│   ├── offsets.json              # Kenshi v1.0.68 offset database
│   └── kenshi_entity_engine.pdf  # Engine overview
│
├── tools/
│   └── re_scanner.py             # RE pattern generation tool
│
├── lib/                          # Third-party libraries
│   ├── enet/                     # ENet networking library
│   ├── minhook/                  # MinHook function hooking
│   ├── json/                     # nlohmann/json
│   └── spdlog/                   # spdlog logging
│
├── CMakeLists.txt                # Root build configuration
├── setup.bat                     # Developer setup script
├── install.bat                   # Alternative installer
└── settings.cfg                  # Default settings
```

---

## Network Protocol

### Connection Details

| Property | Value |
|----------|-------|
| **Transport** | UDP via ENet |
| **Default Port** | 27800 |
| **Master Server Port** | 27801 |
| **Tick Rate** | 20 Hz (50ms) |
| **Max Players** | 16 |
| **Connect Timeout** | 5 seconds |
| **Keepalive Interval** | 1 second |
| **Session Timeout** | 10 seconds |
| **Connection Retry** | 6 attempts, 5s apart (30s total) |

### Channels

| Channel | Mode | Used For |
|---------|------|----------|
| 0 | Reliable Ordered | Connection, entity spawn/despawn, time sync, zone data, buildings, chat, admin |
| 1 | Reliable Unordered | Combat, stats, inventory, squad, faction, pipeline debug |
| 2 | Unreliable Sequenced | Position updates (movement) |

### Packet Header (8 bytes)

```
┌─────────────┬───────┬──────────┬────────────┐
│ MessageType │ Flags │ Sequence │ Timestamp  │
│   (1 byte)  │(1 b)  │ (2 bytes)│ (4 bytes)  │
└─────────────┴───────┴──────────┴────────────┘
Flags: Bit 0 = compressed
```

### Message Types (50+)

<details>
<summary><strong>Connection Messages (0x01–0x08)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x01 | C2S_Handshake | Client→Server | Protocol version, player name, game version |
| 0x02 | S2C_HandshakeAck | Server→Client | PlayerID, server tick, time, weather, player counts |
| 0x03 | S2C_HandshakeReject | Server→Client | Reason code and text |
| 0x04 | C2S_Disconnect | Client→Server | Clean disconnect |
| 0x05 | S2C_PlayerJoined | Server→Client | New player notification |
| 0x06 | S2C_PlayerLeft | Server→Client | Player left (disconnect/timeout/kicked) |
| 0x07 | C2S_Keepalive | Client→Server | Heartbeat |
| 0x08 | S2C_KeepaliveAck | Server→Client | Heartbeat response |

</details>

<details>
<summary><strong>World State (0x10–0x13)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x10 | S2C_WorldSnapshot | Server→Client | Full entity state dump for new players |
| 0x11 | S2C_TimeSync | Server→Client | Server tick, time of day, weather, game speed |
| 0x12 | S2C_ZoneData | Server→Client | Zone metadata |
| 0x13 | C2S_ZoneRequest | Client→Server | Zone interest update request |

</details>

<details>
<summary><strong>Entity Lifecycle (0x20–0x23)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x20 | S2C_EntitySpawn | Server→Client | Spawn entity (ID, type, owner, template, position, faction) |
| 0x21 | S2C_EntityDespawn | Server→Client | Despawn entity (normal/killed/out-of-range) |
| 0x22 | C2S_EntitySpawnReq | Client→Server | Request spawn for local squad member |
| 0x23 | C2S_EntityDespawnReq | Client→Server | Request despawn of local entity |

</details>

<details>
<summary><strong>Movement (0x30–0x33) — Channel 2, Unreliable</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x30 | C2S_PositionUpdate | Client→Server | Batch position update (all owned characters) |
| 0x31 | S2C_PositionUpdate | Server→Client | Broadcast positions from other players |
| 0x32 | C2S_MoveCommand | Client→Server | Move command (walk/run/sneak) |
| 0x33 | S2C_MoveCommand | Server→Client | Broadcast move command |

</details>

<details>
<summary><strong>Combat (0x40–0x46)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x40 | C2S_AttackIntent | Client→Server | Attack request (melee/ranged) |
| 0x41 | S2C_CombatHit | Server→Client | Hit result (damage, body part, KO, block) |
| 0x42 | S2C_CombatBlock | Server→Client | Block result with effectiveness |
| 0x43 | S2C_CombatDeath | Server→Client | Character death |
| 0x44 | S2C_CombatKO | Server→Client | Character knockout |
| 0x45 | C2S_CombatStance | Client→Server | Stance change (passive/defensive/aggressive/hold) |
| 0x46 | C2S_CombatDeath | Client→Server | Report local death |

</details>

<details>
<summary><strong>Stats & Equipment (0x50–0x53)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x50 | S2C_StatUpdate | Server→Client | Stat value update |
| 0x51 | S2C_HealthUpdate | Server→Client | Health per body part + blood level |
| 0x52 | S2C_EquipmentUpdate | Server→Client | Equipment slot change |
| 0x53 | C2S_EquipmentUpdate | Client→Server | Report equipment change |

</details>

<details>
<summary><strong>Inventory (0x60–0x65)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x60 | C2S_ItemPickup | Client→Server | Pick up item |
| 0x61 | C2S_ItemDrop | Client→Server | Drop item at position |
| 0x62 | C2S_ItemTransfer | Client→Server | Transfer between inventories |
| 0x63 | S2C_InventoryUpdate | Server→Client | Inventory change (add/remove/modify) |
| 0x64 | C2S_TradeRequest | Client→Server | Trade request with price |
| 0x65 | S2C_TradeResult | Server→Client | Trade accepted/denied |

</details>

<details>
<summary><strong>Building (0x70–0x77)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x70 | C2S_BuildRequest | Client→Server | Build placement request |
| 0x71 | S2C_BuildPlaced | Server→Client | Building placed confirmation |
| 0x72 | S2C_BuildProgress | Server→Client | Construction progress (0.0–1.0) |
| 0x73 | S2C_BuildDestroyed | Server→Client | Building destroyed |
| 0x74 | C2S_DoorInteract | Client→Server | Door open/close/lock/unlock |
| 0x75 | S2C_DoorState | Server→Client | Door state broadcast |
| 0x76 | C2S_BuildDismantle | Client→Server | Dismantle building |
| 0x77 | C2S_BuildRepair | Client→Server | Repair building |

</details>

<details>
<summary><strong>Chat & Admin (0x80–0x91)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x80 | C2S_ChatMessage | Client→Server | Chat message |
| 0x81 | S2C_ChatMessage | Server→Client | Chat broadcast with sender ID |
| 0x82 | S2C_SystemMessage | Server→Client | System notification |
| 0x90 | C2S_AdminCommand | Client→Server | Admin action (kick/ban/setTime/announce) |
| 0x91 | S2C_AdminResponse | Server→Client | Admin command result |

</details>

<details>
<summary><strong>Server Browser & Master Server (0xA0–0xD4)</strong></summary>

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0xA0 | C2S_ServerQuery | Client→Server | Lightweight server info query |
| 0xA1 | S2C_ServerInfo | Server→Client | Server info response |
| 0xB0–0xB3 | Squad messages | Both | Squad create/member updates |
| 0xC0–0xC1 | Faction messages | Both | Faction relation changes |
| 0xD0 | MS_Register | Server→Master | Register with master server |
| 0xD1 | MS_Heartbeat | Server→Master | Keepalive (30s) |
| 0xD2 | MS_Deregister | Server→Master | Unregister on shutdown |
| 0xD3 | MS_QueryList | Client→Master | Request server list |
| 0xD4 | MS_ServerList | Master→Client | Full server list response |

</details>

### Compression

- **Quaternion**: Smallest-three encoding (32-bit: 2 bits for largest component, 10 bits × 3 for remaining)
- **Position deltas**: Float16 with 0.1m threshold
- **Rotation deltas**: 0.01 radian threshold

---

## Sync System Deep Dive

### Triple-Layer Architecture

```
Layer 3: SyncOrchestrator (7-stage pipeline, feature-flagged)
         ┌──────────────────────────────────────────────┐
         │ UpdateZones → SwapBuffers → ApplyRemote →    │
         │ PollAndSend → ProcessSpawns → BackgroundWork │
         │ → UpdatePlayers                              │
         └──────────────────────────────────────────────┘

Layer 2: Interpolation (per-entity smoothing)
         ┌──────────────────────────────────────────────┐
         │ Ring Buffer (8 snapshots) → Jitter Estimator │
         │ → Adaptive Delay (50-200ms) → Hermite Spline│
         │ → Snap Correction (5-50m blend) →            │
         │ → Dead Reckoning (250ms extrapolation)       │
         └──────────────────────────────────────────────┘

Layer 1: EntityRegistry (tracking & ownership)
         ┌──────────────────────────────────────────────┐
         │ Local entities ↔ Remote entities              │
         │ Dirty flags (12 categories) → Authority model│
         │ Entity state machine (5 states) → Zone track │
         └──────────────────────────────────────────────┘
```

### Entity ID Ranges

| Range | Type | Count |
|-------|------|-------|
| 1–255 | Player characters | 255 |
| 256–8191 | NPCs | 7,936 |
| 8192–16383 | Buildings | 8,192 |
| 16384–24575 | Containers | 8,192 |
| 24576–32767 | Squads | 8,192 |

### Entity States

| State | Value | Description |
|-------|-------|-------------|
| Inactive | 0 | Not in use |
| Spawning | 1 | Registered, waiting for game object |
| Active | 2 | Fully synced and visible |
| Despawning | 3 | Removal in progress |
| Frozen | 4 | Suspended during zone authority handoff |

### Authority Types

| Authority | Value | Description |
|-----------|-------|-------------|
| None | 0 | Server-managed, no player owner |
| Local | 1 | This client owns and simulates |
| Remote | 2 | Another client owns, we interpolate |
| Host | 3 | Server/host authoritative |
| Transferring | 4 | Authority handoff in progress |

### Zone Interest Management

- Zone size: **750m × 750m**
- Interest radius: **3×3 zone grid** around each player (1,500m effective radius)
- Server filters all broadcasts by zone — only sends updates for nearby entities
- Estimated bandwidth: **~6 KB/s per player**

### Dirty Flags

The entity registry tracks 12 categories of changes:

```
Position (0x001)    Rotation (0x002)    Animation (0x004)
Health   (0x008)    Stats    (0x010)    Inventory (0x020)
Combat   (0x040)    Limbs    (0x080)    Squad     (0x100)
Faction  (0x200)    Equipment(0x400)    AIState   (0x800)
```

---

## Hook System

Kenshi Online hooks 14 categories of game functions using MinHook. All hooks are SEH-protected with crash breadcrumbs for diagnostics.

| Hook Module | Functions Hooked | Purpose |
|-------------|------------------|---------|
| **entity_hooks** | CharacterCreate, CharacterDestroy | Entity lifecycle, in-place replay spawning |
| **combat_hooks** | ApplyDamage, StartAttack, CharacterDeath, CharacterKO, MartialArts | Combat replication |
| **movement_hooks** | SetPosition, MoveCommand | Position sync, movement replication |
| **game_tick_hooks** | OnGameTick, GameFrameUpdate | Main sync loop (20 Hz) |
| **render_hooks** | Present | UI rendering, fallback tick |
| **world_hooks** | ZoneLoad, ZoneUnload, BuildingPlace, BuildingDestroyed | World events |
| **time_hooks** | TimeUpdate | Game speed and time of day sync |
| **inventory_hooks** | ItemPickup, ItemDrop, ItemTransfer | Inventory replication |
| **squad_hooks** | SquadCreate, SquadDestroy, SquadAddMember | Squad management |
| **faction_hooks** | FactionRelation | Faction relation sync (with feedback loop guards) |
| **ai_hooks** | AICreate, AIPackages | Suppress AI for remote characters |
| **input_hooks** | KeyboardInput | UI keyboard capture |
| **building_hooks** | BuildingPlace, BuildingDestroy, BuildingRepair | Building replication |
| **save_hooks** | SaveGame, LoadGame, ImportGame | Save/load interception |

### In-Place Replay Spawning

Remote characters are spawned by capturing Kenshi's internal factory function and replaying it:
1. Hook captures `CharacterCreate` and records the factory function pointer
2. When a remote entity needs spawning, the factory is called at the same stack address
3. Limited to 3 replays per call to prevent stack overflow
4. Post-spawn writes apply name, faction, position from network data

---

## Building from Source

### Requirements

- **Visual Studio 2022** with C++ Desktop Development workload
- **CMake 3.20+**
- **vcpkg** (recommended) or manual library setup

### Quick Build

```bash
# Clone with submodules
git clone --recursive https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# Option A: Use the setup script
setup.bat

# Option B: Manual setup
vcpkg install enet:x64-windows minhook:x64-windows nlohmann-json:x64-windows spdlog:x64-windows
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Build Output

```
build/bin/Release/
├── KenshiMP.Core.dll           # Ogre plugin (copy to Kenshi directory)
├── KenshiMP.Server.exe         # Dedicated server
├── KenshiMP.Injector.exe       # Game launcher
├── KenshiMP.MasterServer.exe   # Server browser registry
├── KenshiMP.UnitTest.exe       # Unit tests
├── KenshiMP.IntegrationTest.exe# Integration tests
├── KenshiMP.LiveTest.exe       # Live game tests
└── KenshiMP.TestClient.exe     # Standalone test client
```

### Manual Library Setup (without vcpkg)

Place the following in the `lib/` directory:
- `lib/enet/` — [ENet 1.3.x](https://github.com/lsalzman/enet)
- `lib/minhook/` — [MinHook 1.3.3](https://github.com/TsudaKageyu/minhook)
- `lib/json/` — [nlohmann/json](https://github.com/nlohmann/json)
- `lib/spdlog/` — [spdlog](https://github.com/gabime/spdlog)

### Running Tests

```bash
# Unit tests
build/bin/Release/KenshiMP.UnitTest.exe

# Integration tests (requires server running)
build/bin/Release/KenshiMP.IntegrationTest.exe

# Live tests (requires Kenshi running)
build/bin/Release/KenshiMP.LiveTest.exe
```

---

## Troubleshooting

### Common Issues

**"Plugin failed to load" / Kenshi crashes on startup**
- Ensure `KenshiMP.Core.dll` is in the same directory as `kenshi_x64.exe`
- Check that `Plugins_x64.cfg` contains `Plugin=KenshiMP.Core`
- Make sure you're running the **64-bit** version of Kenshi (`kenshi_x64.exe`)

**"Connection timed out"**
- Verify the server is running and reachable
- Check that port **27800 UDP** is forwarded on the server's firewall/router
- Try connecting directly by IP instead of through the server browser

**"Pattern scan failed" / hooks not installing**
- This mod is built for **Kenshi v1.0.68** (latest Steam/GOG version)
- Other versions may have different function addresses — check `docs/offsets.json`
- If you have other mods that modify `kenshi_x64.exe`, they may conflict

**Players not visible / not syncing**
- Check the debug overlay (backtick key) for entity counts
- Use `/entities` to see synced entities
- Use `/ping` to check connection quality

**Server not appearing in browser**
- Ensure the master server address is correct in `server.json`
- Check that the master server is running on port 27801
- Verify UDP port 27801 is accessible from the game server

### Logs

- **Client log**: `kenshi.log` in the Kenshi directory (spdlog output)
- **Server log**: Console output (stdout)
- **Crash diagnostics**: SEH breadcrumbs report the last completed step before crash

---

## Credits

Built on community reverse engineering work:
- [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) — Ogre plugin injection system
- [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib) — Game structure definitions
- [OpenConstructionSet](https://github.com/lmaydev/OpenConstructionSet) — Game data SDK
- [ENet](https://github.com/lsalzman/enet) — Reliable UDP networking
- [MinHook](https://github.com/TsudaKageyu/minhook) — x86/x64 function hooking
- [spdlog](https://github.com/gabime/spdlog) — Fast C++ logging
- [nlohmann/json](https://github.com/nlohmann/json) — JSON for C++

## Community

Join the community on Discord for support, feedback, and development updates:

[![Discord](https://img.shields.io/badge/Discord-Join%20Us-5865F2?logo=discord&logoColor=white)](https://discord.gg/uNN6Pwjg)

---

## License

MIT License — see [LICENSE](LICENSE) for details.

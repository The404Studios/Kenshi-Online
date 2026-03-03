# Kenshi-Online (KenshiMP) — Complete Documentation

## Table of Contents

1. [Overview](#overview)
2. [Requirements](#requirements)
3. [Installation & Setup](#installation--setup)
4. [How to Play (Client Guide)](#how-to-play-client-guide)
5. [How to Host a Server](#how-to-host-a-server)
6. [Master Server (Server Browser)](#master-server-server-browser)
7. [Controls Reference](#controls-reference)
8. [Chat & Client Commands](#chat--client-commands)
9. [In-Game HUD Layout](#in-game-hud-layout)
10. [Network Protocol Reference](#network-protocol-reference)
11. [Architecture & Project Structure](#architecture--project-structure)
12. [Injection & Plugin System](#injection--plugin-system)
13. [Hook Modules (14 Systems)](#hook-modules-14-systems)
14. [Entity Spawning Pipeline](#entity-spawning-pipeline)
15. [Position Synchronization & Interpolation](#position-synchronization--interpolation)
16. [Combat Synchronization](#combat-synchronization)
17. [Inventory & Equipment Sync](#inventory--equipment-sync)
18. [Squad & Faction Sync](#squad--faction-sync)
19. [Building & Door Sync](#building--door-sync)
20. [Time & World State Sync](#time--world-state-sync)
21. [Zone Interest Management](#zone-interest-management)
22. [Disconnect & Recovery](#disconnect--recovery)
23. [Game Memory Layout (Offsets)](#game-memory-layout-offsets)
24. [Constants & Limits Reference](#constants--limits-reference)
25. [Message Type Reference](#message-type-reference)
26. [Server Security](#server-security)
27. [Configuration Reference](#configuration-reference)
28. [Build Instructions](#build-instructions)
29. [Troubleshooting](#troubleshooting)
30. [Known Limitations](#known-limitations)
31. [Implementation Status (13 Phases)](#implementation-status-13-phases)
32. [Credits](#credits)

---

## Overview

Kenshi-Online (KenshiMP) is a co-op multiplayer mod for Kenshi that supports up to 16 players on a single dedicated server. The mod operates entirely through Kenshi's native systems — using Ogre3D's plugin loader for injection, MyGUI for all in-game UI, and ENet for UDP networking. There is no process injection, no DLL injection, and no third-party overlay.

The mod is built as 8 C++ projects (plus 2 test projects) that compile into a client DLL, dedicated server executable, master server executable, launcher/injector, and supporting libraries.

All 13 multiplayer implementation phases are fully functional, covering: injection, connection, entity registration, character spawning, position sync, combat, inventory/equipment, chat/commands, world state, squads/factions, buildings/doors, disconnect recovery, and server infrastructure.

---

## Requirements

- **Kenshi** v1.0.59 or later (tested and verified on v1.0.68)
- **Windows 10/11** 64-bit
- **Mod files:**
  - `KenshiMP.Core.dll` — Client plugin (loaded by Ogre)
  - `KenshiMP.Injector.exe` — Configures Ogre to load the plugin, launches game
  - `KenshiMP.Server.exe` — Dedicated server (only needed if hosting)
  - `KenshiMP.MasterServer.exe` — Server browser registry (optional, centralized)

---

## Installation & Setup

### For Players (Joining a Server)

1. Place `KenshiMP.Core.dll` in your Kenshi game directory (next to `kenshi_x64.exe`)
2. Run `KenshiMP.Injector.exe`
3. Set the **Game Path** to your Kenshi directory if not auto-detected
4. Enter your **Player Name**
5. Enter the **Server Address** (IP) and **Port** (default: 27800)
6. Click **PLAY**

The injector will:
- Copy `KenshiMP.Core.dll` to the game directory (if not already there)
- Modify `Plugins_x64.cfg` to add `Plugin=KenshiMP.Core`
- Write connection config to `%APPDATA%/KenshiMP/client.json`
- Launch Kenshi through Steam (appid 238960)

When Kenshi starts, Ogre's plugin system automatically loads the DLL. The multiplayer HUD appears at the top-center of the screen.

### For Server Operators

See [How to Host a Server](#how-to-host-a-server) below.

---

## How to Play (Client Guide)

### Step 1: Launch via Injector

Run `KenshiMP.Injector.exe`. It modifies `Plugins_x64.cfg` and launches Kenshi. Load your save or start a new game.

Once the game loads, you will see the **KENSHI ONLINE** status bar at the top-center of the screen. The debug/loading log panel on the right side shows initialization progress.

### Step 2: Connect to a Server

Press **F1** to open the multiplayer menu. It has three tabs:

- **Quick Connect** — Enter a server IP and port, then click Connect
- **Server Browser** — Browse servers registered with the master server. Click REFRESH to query the master server. Click a server row to select it, then JOIN
- **Settings** — Set your player name, default server address, toggle auto-connect

After connecting:
- The menu closes automatically
- The status bar updates to show: your name, player count, remote entity count, and ping
- Other players' characters spawn into your world
- Your characters are broadcast to other players

### Step 3: Interact

- **Move normally** — your character positions are synced to all players every 50ms
- **Fight** — combat is server-authoritative; damage is calculated on the server and broadcast
- **Chat** — press Enter to open chat, type a message, press Enter to send
- **Commands** — type `/help` in chat for the full command list

### Disconnecting

Press F1 to open the menu, or type `/disconnect` in chat. Remote characters are teleported underground and cleaned up. If auto-connect is enabled, the client will attempt to reconnect after 2 seconds.

---

## How to Host a Server

### Quick Start (Local)

1. Run `KenshiMP.Server.exe`
2. A `server.json` config file is created automatically on first run
3. The server starts listening on port **27800 UDP**
4. The server attempts UPnP port mapping automatically
5. Players connect using your IP address (or find you in the server browser if a master server is running)

### Server Configuration (server.json)

```json
{
  "serverName": "My Kenshi Server",
  "port": 27800,
  "maxPlayers": 16,
  "password": "",
  "tickRate": 20,
  "gameSpeed": 1.0,
  "pvpEnabled": true,
  "savePath": "world.kmpsave",
  "masterServer": "127.0.0.1",
  "masterPort": 27801
}
```

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| serverName | string | "Kenshi Server" | Display name in server browser |
| port | uint16 | 27800 | UDP port to listen on |
| maxPlayers | int | 16 | Maximum concurrent players (1-16) |
| password | string | "" | Server password (empty = no password) |
| tickRate | int | 20 | Server update frequency in Hz |
| gameSpeed | float | 1.0 | Game time multiplier (1.0 = normal) |
| pvpEnabled | bool | true | Allow player vs player combat |
| savePath | string | "world.kmpsave" | File path for world state persistence |
| masterServer | string | "127.0.0.1" | Master server address for browser registration |
| masterPort | uint16 | 27801 | Master server port |

Custom config path: `KenshiMP.Server.exe path/to/config.json`

### Server Console Commands

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | Show server status (players, entities, tick rate, uptime, memory) |
| `players` | List all connected players with ID, name, ping, zone, and entity count |
| `kick <id>` | Kick a player by their numeric ID |
| `say <message>` | Broadcast a system message to all connected players |
| `save` | Save current world state to disk immediately |
| `stop` | Save world state and shut down the server gracefully |

### Hosting on a VPS / Dedicated Machine

1. **Copy files** to the server:
   - `KenshiMP.Server.exe`
   - `server.json` (optional — auto-generated on first run)

2. **Open the firewall** for UDP port 27800:
   ```
   # Windows
   netsh advfirewall firewall add rule name="KenshiMP" dir=in action=allow protocol=UDP localport=27800

   # Linux (if using Wine)
   sudo ufw allow 27800/udp
   ```

3. **Edit server.json** with your desired settings

4. **Run the server**: `KenshiMP.Server.exe`

5. Players connect using the VPS public IP and port 27800, or find your server in the browser

### World Persistence

- The server automatically saves world state on shutdown (`stop` command or Ctrl+C)
- Use the `save` command to save manually at any time
- On startup, the server loads the previous world state if the save file exists
- The save file (`world.kmpsave`) contains:
  - All entity positions, rotations, health (per body part)
  - Entity factions, template info, and ownership
  - Time of day, weather state
  - Next entity ID counter (prevents ID collisions on restart)

---

## Master Server (Server Browser)

The `KenshiMP.MasterServer.exe` runs a centralized server registry on port **27801 UDP**.

### How It Works

1. **Game servers register** on startup by sending `MS_Register` with their name, port, player count, max players, and PvP setting
2. **Game servers heartbeat** every 30 seconds with `MS_Heartbeat` (updated player count and time of day)
3. **Clients query** the master with `MS_QueryList` and receive `MS_ServerList` containing all registered servers
4. **On shutdown**, game servers send `MS_Deregister` (or the master times them out after missed heartbeats)

### Heartbeat Reconnection

If the master server connection drops, the game server uses exponential backoff to reconnect:
- Initial retry: 5 seconds
- Maximum backoff: 60 seconds
- Reconnection is automatic and transparent

### Running a Master Server

```
KenshiMP.MasterServer.exe [port]
```

Default port: 27801. The master server is stateless — it only tracks currently-registered servers in memory.

---

## Controls Reference

| Key | Action |
|-----|--------|
| **F1** | Open/close the multiplayer menu (Host, Join, Browser, Settings) |
| **Insert** | Toggle the debug/loading log panel (right side) |
| **Enter** | Open/close chat input |
| **Tab** | Toggle the player list overlay |
| **` (backtick)** | Toggle debug info display |
| **Escape** | Close all open panels and menus |
| **Backspace** | Delete last character in chat input |

---

## Chat & Client Commands

### Chat

Press **Enter** to open the chat input. A semi-transparent dark panel appears at the bottom-left with up to 10 visible messages. Type your message and press Enter to send. Press Escape to cancel.

- **Player messages** appear in white with the format: `[HH:MM] PlayerName: message`
- **System messages** appear in amber (gold) with the format: `[HH:MM] [System] message`
- Messages auto-fade after **30 seconds**
- Chat history stores up to **50 entries**

### Client Commands

All commands are typed in chat with a `/` prefix.

| Command | Description |
|---------|-------------|
| `/help` | List all available commands |
| `/connect <ip> [port]` | Connect to a server (default port: 27800) |
| `/disconnect` | Cleanly disconnect from the current server |
| `/tp [name]` | Teleport to a player by name (or nearest player if no name given) |
| `/teleport [name]` | Alias for `/tp` |
| `/time [value]` | Read current time of day, or set it (0.0 = midnight, 0.5 = noon, 1.0 = midnight) |
| `/pos` | Show your primary character's current world position (X, Y, Z) |
| `/position` | Alias for `/pos` |
| `/players` | List all connected players with their IDs |
| `/who` | Alias for `/players` |
| `/status` | Show entity counts, spawn queue size, and loaded template count |
| `/entities` | Show entity counts (total, local, remote, spawned remote) |
| `/ping` | Show current round-trip ping to the server in milliseconds |
| `/debug` | Toggle the debug/loading log panel |
| `/kick <name> [reason]` | Kick a player by name (host only) |
| `/announce <message>` | Broadcast a system message to all players (host only) |

---

## In-Game HUD Layout

The HUD uses Kenshi's native **MyGUI** rendering — no external overlay. All widgets use Kenshi's own skins (`Kenshi_FloatingPanelSkin`, `Kenshi_TextboxStandardText`, `Kenshi_TextboxPaintedText`).

### Status Bar (Top-Center)

Always visible when the HUD is active. Shows:
- **Not connected:** `KENSHI ONLINE | NOT CONNECTED | F1 = Menu | Enter = Chat`
- **Connected:** `KENSHI ONLINE | CONNECTED as PlayerName | 3 players online | 12 remote | 45ms`

Semi-transparent dark background panel with amber text.

### Chat Panel (Bottom-Left)

Semi-transparent dark background panel (alpha 0.7) that appears when messages exist or chat input is open. Contains:
- **10 chat line slots** — most recent messages at the bottom
- **Chat input line** — visible when chat is active, amber text with `> ` prompt and `_` cursor
- System messages are colored **amber** (0.9, 0.55, 0.1), player chat is **white** (0.9, 0.9, 0.9)
- All messages prefixed with `[HH:MM]` timestamps

### Player List (Top-Right)

Toggled with **Tab**. Semi-transparent panel showing:
- Title: "PLAYERS ONLINE"
- Up to **8 player rows**
- Local player: `YourName (You)` or `YourName (You) [HOST]`
- Remote players: `PlayerName [IN WORLD]` or `PlayerName [loading]`

### Debug/Loading Log Panel (Right Side)

Toggled with **Insert**. Visible by default during loading, auto-hides 15 seconds after game loads (if not connected). Shows:
- Title: "KENSHI ONLINE"
- Up to **20 log lines** with `[TAG] message` format
- Tags include: INIT, HOOK, SCAN, NET, GAME, OK, ERR, WARN
- Bottom hint: `Insert = toggle | F1 = menu | Enter = chat`

---

## Network Protocol Reference

### Transport

- **Library:** ENet (reliable UDP)
- **Default Port:** 27800 (game), 27801 (master server)
- **Channels:** 3

| Channel | Name | Reliability | Ordering | Used For |
|---------|------|-------------|----------|----------|
| 0 | Reliable Ordered | Reliable | Ordered | Connection, entities, squads, factions, buildings, chat, admin |
| 1 | Reliable Unordered | Reliable | Unordered | Combat, stats, health, inventory, equipment, trade |
| 2 | Unreliable Sequenced | Unreliable | Sequenced | Position updates (movement) |

### Packet Format

Every packet starts with an 8-byte header:

```
PacketHeader (8 bytes, packed):
  [0] MessageType type     (uint8)   — Message type enum
  [1] uint8_t flags        (uint8)   — Bit 0: compressed
  [2] uint16_t sequence    (uint16)  — Sequence number
  [4] uint32_t timestamp   (uint32)  — Server tick
```

### Bandwidth Limits

- **Upstream:** 128 KB/s per client
- **Downstream:** 256 KB/s per client

### Timing

- **Tick Rate:** 20 Hz (50ms intervals)
- **Connect Timeout:** 5000ms
- **Keepalive Interval:** 1000ms
- **Idle Timeout:** 10000ms

---

## Architecture & Project Structure

```
[Kenshi Client 1]  <--ENet UDP-->  [Dedicated Server]  <--ENet UDP-->  [Kenshi Client 2]
     |                                    |                                   |
 KenshiMP.Core.dll              KenshiMP.Server.exe              KenshiMP.Core.dll
 (hooks + native UI)            (authority + relay)              (hooks + native UI)
                                         |
                                KenshiMP.MasterServer.exe
                                (server browser registry, port 27801)
```

### Projects (10 build targets)

| Project | Type | Description |
|---------|------|-------------|
| **KenshiMP.Common** | Static lib | Protocol, serialization, compression, config, types |
| **KenshiMP.Scanner** | Static lib | Pattern scanner, SIMD engine, orchestrator, pdata, vtable, call graph, string xref, safe hook |
| **KenshiMP.Core** | DLL (Ogre plugin) | Hooks, networking, sync, UI — loaded into Kenshi's process |
| **KenshiMP.Server** | Executable | Dedicated server: ENet host, entity management, persistence, UPnP, master registration |
| **KenshiMP.MasterServer** | Executable | Centralized server browser registry |
| **KenshiMP.Injector** | Executable | Win32 GUI launcher, modifies Plugins_x64.cfg |
| **KenshiMP.TestClient** | Executable | Console test client for protocol testing |
| **KenshiMP.UnitTest** | Executable | Unit tests (scanner, interpolation, serialization, zone math) |
| **KenshiMP.IntegrationTest** | Executable | Full client+server integration tests |
| **KenshiMP.LiveTest** | Executable | Live integration test (`--dual` mode) |

### Source Tree

```
KenshiMP/
+-- KenshiMP.Common/
|   +-- include/kmp/
|   |   +-- types.h            # Vec3, Quat, EntityID, ZoneCoord, enums
|   |   +-- constants.h        # All protocol constants and limits
|   |   +-- messages.h         # 50+ binary message structs (packed)
|   |   +-- protocol.h         # PacketHeader, PacketWriter, PacketReader
|   |   +-- compression.h      # Delta compression utilities
|   |   +-- config.h           # ClientConfig, ServerConfig
|   +-- src/
|       +-- compression.cpp, config.cpp, protocol.cpp, serialization.cpp
|
+-- KenshiMP.Scanner/
|   +-- include/kmp/
|   |   +-- scanner.h          # IDA-style byte pattern matching
|   |   +-- patterns.h         # Known Kenshi function signatures
|   |   +-- memory.h           # Safe memory read/write
|   |   +-- hook_manager.h     # MinHook wrapper
|   |   +-- orchestrator.h     # Pattern discovery orchestration
|   |   +-- scanner_engine.h   # SIMD-accelerated search
|   |   +-- call_graph.h       # Call graph analysis
|   |   +-- vtable_scanner.h   # Virtual table discovery
|   |   +-- string_analyzer.h  # String constant xref analysis
|   |   +-- pdata_enumerator.h # Exception handling table enumeration
|   |   +-- function_analyzer.h# Prologue/epilogue analysis
|   |   +-- safe_hook.h        # SEH-protected hook wrapper
|
+-- KenshiMP.Core/
|   +-- core.h / core.cpp      # Singleton orchestrating all subsystems
|   +-- dllmain.cpp            # Ogre plugin entry point
|   +-- hooks/                 # 14 game function hook modules
|   |   +-- ai_hooks.h/cpp         # AI suppression for remote entities
|   |   +-- building_hooks.h/cpp   # Building placement & destruction
|   |   +-- combat_hooks.h/cpp     # Damage, attacks, death, KO
|   |   +-- entity_hooks.h/cpp     # Character creation (spawn pipeline)
|   |   +-- faction_hooks.h/cpp    # Faction relation changes
|   |   +-- game_tick_hooks.h/cpp  # Main game loop tick
|   |   +-- input_hooks.h/cpp      # WndProc input capture
|   |   +-- inventory_hooks.h/cpp  # Item pickup, drop, equip
|   |   +-- movement_hooks.h/cpp   # Character position updates
|   |   +-- render_hooks.h/cpp     # DX11 Present (UI update)
|   |   +-- save_hooks.h/cpp       # Save/load suppression
|   |   +-- squad_hooks.h/cpp      # Squad creation/destruction
|   |   +-- time_hooks.h/cpp       # TimeManager capture
|   |   +-- world_hooks.h/cpp      # Zone load/unload
|   +-- game/                  # Reconstructed game type accessors
|   |   +-- game_types.h           # 600+ lines: all offsets, accessors, enums
|   |   +-- game_character.cpp     # CharacterAccessor (read/write all fields)
|   |   +-- game_building.cpp      # BuildingAccessor
|   |   +-- game_faction.cpp       # FactionAccessor
|   |   +-- game_inventory.cpp     # InventoryAccessor
|   |   +-- game_squad.cpp         # SquadAccessor
|   |   +-- game_stats.cpp         # StatsAccessor (23 skills)
|   |   +-- game_world.cpp         # GameWorldAccessor, CharacterIterator
|   |   +-- spawn_manager.h/cpp    # Remote entity spawning (in-place replay)
|   |   +-- player_controller.h/cpp# Local + remote player state
|   +-- net/                   # Networking
|   |   +-- client.h/cpp           # ENet client (connect, send, receive)
|   |   +-- packet_handler.cpp     # 28 S2C message handlers
|   |   +-- server_query.h/cpp     # Lightweight server info query
|   +-- sync/                  # State synchronization
|   |   +-- entity_registry.h/cpp  # Entity tracking (local + remote)
|   |   +-- interpolation.h/cpp    # Position smoothing (ring buffer, snap correction)
|   |   +-- ownership.cpp          # Authority tracking
|   +-- sys/                   # Systems
|   |   +-- command_registry.h/cpp # Chat command dispatch
|   |   +-- builtin_commands.cpp   # 16 built-in commands
|   |   +-- task_orchestrator.h/cpp# Background thread pool
|   |   +-- frame_data.h           # Double-buffered entity updates
|   +-- ui/                    # User interface (native MyGUI)
|       +-- mygui_bridge.h/cpp     # Runtime MyGUI DLL symbol resolution
|       +-- native_hud.h/cpp       # In-game HUD (status, chat, players, log)
|       +-- native_menu.h/cpp      # Main menu (host, join, browser, settings)
|       +-- overlay.h/cpp          # Connection state machine, auto-connect
|
+-- KenshiMP.Server/
|   +-- server.h/cpp           # GameServer: ENet host, 23 C2S handlers
|   +-- entity_manager.h/cpp   # Entity queries (by owner, zone, radius)
|   +-- zone_manager.h/cpp     # Zone interest management
|   +-- game_state.h/cpp       # Time, weather, day/night
|   +-- player_manager.h/cpp   # Bans, AFK, rate limiting
|   +-- combat_resolver.cpp    # Server-side damage calculation
|   +-- upnp.h/cpp             # UPnP auto port mapping
|   +-- world_persistence.cpp  # Save/load world state
|   +-- main.cpp               # Entry point, console commands
|
+-- KenshiMP.MasterServer/
|   +-- main.cpp               # Server registry (register, heartbeat, query)
|
+-- KenshiMP.Injector/
|   +-- injector.h/cpp         # Plugin installation (Plugins_x64.cfg)
|   +-- process.h/cpp          # Game process launcher
|   +-- main.cpp               # Win32 GUI
|
+-- dist/                      # Distributable assets
|   +-- Kenshi_MultiplayerHUD.layout    # MyGUI layout for in-game HUD
|   +-- Kenshi_MainMenu.layout          # MyGUI layout for main menu
|   +-- Kenshi_MultiplayerPanel.layout  # MyGUI layout for menu panels
|   +-- JOINING.md                      # Quick-start guide for players
|
+-- docs/
    +-- PHASES.md              # 13-phase implementation audit
    +-- offsets.json           # Authoritative game offset reference
    +-- patterns.json          # Pattern signatures for runtime discovery
    +-- english.md             # This file
    +-- russian.md             # Russian documentation
```

---

## Injection & Plugin System

KenshiMP uses the **Ogre3D plugin system** — the same mechanism that Kenshi uses to load its own rendering plugins. This is non-invasive and reversible.

### How It Works

1. **Injector** modifies `Plugins_x64.cfg` in the Kenshi directory to add the line:
   ```
   Plugin=KenshiMP.Core
   ```
2. **Kenshi launches** normally through Steam
3. **Ogre3D** reads `Plugins_x64.cfg` during engine initialization and loads `KenshiMP.Core.dll` as a standard Ogre plugin
4. **dllmain.cpp** implements the Ogre plugin interface (`installPlugin` / `uninstallPlugin`)
5. **Core::Initialize()** runs: pattern scanner, 14 hook modules, ENet client, MyGUI bridge, command registry

### Uninstallation

The injector can remove the plugin line from `Plugins_x64.cfg`, restoring vanilla Kenshi. No files inside the game's data are modified.

### Why Not DLL Injection?

Traditional DLL injection (CreateRemoteThread, LoadLibrary) is:
- Flagged by antivirus software
- Brittle across Windows updates
- Requires the game to be running first

The Ogre plugin approach is:
- Clean and game-native (same as all Kenshi render plugins)
- Loads at engine init (before any game logic runs)
- Not flagged by AV
- Trivially reversible

---

## Hook Modules (14 Systems)

All hooks are installed via **MinHook** with **SEH (Structured Exception Handling)** protection. Each module has `Install()` and `Uninstall()` functions. Hooks are conditional on the pattern scanner finding the target function.

| # | Module | File | Hooks | Purpose |
|---|--------|------|-------|---------|
| 1 | **Entity Hooks** | entity_hooks.cpp | CharacterCreate | Character spawning pipeline (in-place replay) |
| 2 | **Combat Hooks** | combat_hooks.cpp | ApplyDamage, StartAttack, CharacterDeath, CharacterKO | Damage interception, death/KO broadcast |
| 3 | **AI Hooks** | ai_hooks.cpp | AI behavior functions | Suppress AI on remote-controlled characters |
| 4 | **Movement Hooks** | movement_hooks.cpp | Character movement | Position update generation |
| 5 | **Building Hooks** | building_hooks.cpp | Building placement, destruction | Building sync, ghost entity tracking |
| 6 | **Inventory Hooks** | inventory_hooks.cpp | Item pickup, drop, equip | Inventory change broadcast |
| 7 | **Time Hooks** | time_hooks.cpp | TimeUpdate function | Capture TimeManager pointer, time read/write |
| 8 | **Faction Hooks** | faction_hooks.cpp | FactionRelation function | Faction relation sync with feedback loop guard |
| 9 | **Squad Hooks** | squad_hooks.cpp | Squad create, destroy | Squad lifecycle sync |
| 10 | **Game Tick Hooks** | game_tick_hooks.cpp | GameFrameUpdate | Main game loop integration |
| 11 | **Render Hooks** | render_hooks.cpp | DX11 Present | UI update trigger, post-spawn processing |
| 12 | **Input Hooks** | input_hooks.cpp | WndProc | Chat input capture, hotkey processing |
| 13 | **Save Hooks** | save_hooks.cpp | Save/Load functions | Suppress network sends during save/load |
| 14 | **World Hooks** | world_hooks.cpp | Zone load/unload | Track loaded zone boundaries |

### Technical Note: MinHook Trampoline Safety

10 of 24 hooked functions start with `mov rax, rsp` — a safe 3-byte prologue for MinHook's 5-byte trampoline. The CharacterCreate function uses a 2-parameter `__fastcall` calling convention (RCX, RDX) which is trampoline-safe.

---

## Entity Spawning Pipeline

The spawn pipeline is the most critical and delicate part of the mod. Remote characters must be created using Kenshi's own factory to avoid memory corruption.

### The Only Safe Method: In-Place Replay

When the game naturally creates an NPC (via `CharacterCreate`), the hook in `entity_hooks.cpp`:

1. **Intercepts** the factory call
2. **Checks** the spawn queue for pending remote entities
3. **Replays** the factory call at the **same stack address** with modified parameters
4. The new character is created through the exact same code path as a natural NPC
5. Post-spawn: **WriteName**, **WriteFaction**, **WritePosition** are applied to the spawned character

### Spawn Limits & Safety

- **Per-player spawn cap:** `MAX_SPAWNS_PER_PLAYER = 8` (prevents NPC flood crashes)
- **Max replays per hook call:** 3 (prevents runaway in a single frame)
- **Max spawn retries:** 200 attempts (~10 seconds at 20 Hz)
- **Fallback:** `SpawnCharacterDirect` after 5-second timeout (works but characters may destabilize after 10-20 seconds)

### Faction Bootstrap

When the player's faction pointer isn't available through `PlayerBase` (common on Steam), `entity_hooks` captures the faction from the first character created. Core then re-scans existing characters via `RequestEntityRescan()` to apply the correct faction to already-registered entities.

---

## Position Synchronization & Interpolation

### Outbound (Local -> Server -> Others)

1. **BackgroundReadEntities** scans all local squad characters every game tick
2. Positions are compared against last-sent values using thresholds:
   - Position: must change by > 0.1 meters
   - Rotation: must change by > 0.01
3. Changed positions are batched into `C2S_PositionUpdate` packets
4. Packets include: entityId, position (3 floats), compressed quaternion (smallest-three encoding), animation state, move speed (0-255 mapped to 0-15 m/s), flags (running/sneaking/combat)
5. Sent on **Channel 2** (unreliable sequenced) for minimal latency

### Server Relay

The server receives position updates, stores them, updates zone assignments, and relays to other clients filtered by **zone interest** (only players in adjacent zones receive updates).

### Inbound (Others -> Local Display)

1. **Interpolation system** receives snapshots with timestamps
2. Each entity has a **ring buffer of 8 snapshots**
3. **Adaptive jitter estimation** adjusts the interpolation delay:
   - EMA smoothing factor: 0.1
   - 20ms jitter -> 50ms delay (minimum)
   - 80ms jitter -> 200ms delay (maximum)
4. **Hermite spline interpolation** between the two bracketing snapshots
5. **Dead reckoning extrapolation** up to 250ms past the last snapshot (using velocity)
6. **Snap correction** for large position errors:
   - < 5 meters: smooth blend (ignored)
   - 5-50 meters: blend correction over 150ms
   - > 50 meters: instant teleport
7. Interpolated positions are written to game characters via `CharacterAccessor::WritePosition()`

### Double-Buffered Frame Data

To avoid contention between the game thread and network workers:
- Workers write entity states to buffer A
- Game thread reads from buffer B
- Atomic swap every game tick

---

## Combat Synchronization

Combat is **server-authoritative** — the server resolves all damage.

### Flow

1. Local character attacks -> `combat_hooks` intercepts `ApplyDamage` or `StartAttack`
2. Client sends `C2S_AttackIntent` (attackerId, targetId, attackType)
3. Server resolves combat:
   - Random body part selection (Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg)
   - Damage calculation (cut, blunt, pierce)
   - Block chance evaluation
4. Server broadcasts `S2C_CombatHit` to all relevant clients
5. Clients apply damage via the game's original `ApplyDamage` function (or direct memory write fallback)
6. If health threshold crossed:
   - `S2C_CombatKO` for knockout
   - `S2C_CombatDeath` for death

### Combat Stance Sync

Players can set stance (passive/defensive/aggressive/hold) via `C2S_CombatStance`, broadcast to all players.

---

## Inventory & Equipment Sync

### Equipment (14 Slots)

Equipment slots: Weapon, Back, Hair, Hat, Eyes, Body, Legs, Shirt, Boots, Gloves, Neck, Backpack, Beard, Belt

- Each entity tracks `lastEquipment[14]` (template IDs) in the entity registry
- Per-slot diff detection: only changed slots generate `C2S_EquipmentUpdate`
- Server stores equipment state and includes it in world snapshots for new joiners

### Inventory Operations

| Operation | Client Message | Server Response |
|-----------|---------------|-----------------|
| Pick up item | `C2S_ItemPickup` | Validates, broadcasts `S2C_InventoryUpdate` |
| Drop item | `C2S_ItemDrop` | Validates ownership, broadcasts update |
| Transfer item | `C2S_ItemTransfer` | Validates both parties, broadcasts update |
| Trade | `C2S_TradeRequest` | Validates proximity, funds, availability -> `S2C_TradeResult` |

---

## Squad & Faction Sync

### Squads

- `C2S_SquadCreate` -> server assigns a network squad ID (starting at `0x80000000`) -> `S2C_SquadCreated`
- `C2S_SquadAddMember` -> server validates -> `S2C_SquadMemberUpdate` (action: added/removed)
- Squad hooks suppress during save/load

### Factions

- Faction relation changes intercepted by `faction_hooks`
- `C2S_FactionRelation` (factionIdA, factionIdB, relation float -100 to +100, causerEntityId)
- Server validates and broadcasts `S2C_FactionRelation`
- **Feedback loop guard:** `faction_hooks::SetServerSourced(true/false)` prevents the hook from re-sending a change that was just applied from the server

---

## Building & Door Sync

### Building Placement

1. `building_hooks` intercepts placement -> `C2S_BuildRequest` (templateId, position, rotation)
2. Server assigns entity ID -> `S2C_BuildPlaced` broadcast (ghost entity)
3. Construction progress: `S2C_BuildProgress` (0.0 to 1.0)
4. Server validates building ownership for dismantle (`C2S_BuildDismantle`) and repair (`C2S_BuildRepair`)

### Door State

- `C2S_DoorInteract` (entityId, actorEntityId, action: open/close/lock/unlock)
- Server broadcasts `S2C_DoorState` (entityId, state: closed/open/locked/broken)
- Door state written via the building's functionality pointer (`functionality+0x10`)

### Limitation

Visual building spawning (creating game objects for remote buildings) is not yet implemented — requires reverse engineering the building factory. Ghost entities are tracked in the registry.

---

## Time & World State Sync

### Time System

Time of day is stored on a **TimeManager** object (NOT on GameWorld). The `time_hooks` module captures the TimeManager pointer when the `TimeUpdate` function fires.

- **Read:** `time_hooks::GetTimeOfDay()` reads `TimeManager+0x08` (float 0.0-1.0, where 0=midnight, 0.5=noon)
- **Write:** `time_hooks::WriteTimeOfDay(value)` writes `TimeManager+0x08`
- **Game speed:** `TimeManager+0x10` (float multiplier)

### Server Time Sync

- Server periodically broadcasts `S2C_TimeSync` (serverTick, timeOfDay, weatherState, gameSpeed)
- Client receives -> `time_hooks::SetServerTime()` writes the time
- Host is exempt from time overwrite (host runs the authoritative clock)

### `/time` Command

- `/time` — displays current time of day
- `/time 0.5` — sets time to noon (host only effectively writes, joiners get overwritten by next sync)

---

## Zone Interest Management

The world is divided into zones of **750 meters** each. Each player has an interest radius of **1 zone** (resulting in a 3x3 grid, or 2250m x 2250m viewable area).

### How It Works

1. Server tracks each player's current zone (computed from position)
2. When broadcasting entity updates, the server checks: is this entity in an adjacent zone to the receiving player?
3. Only entities within the 3x3 zone grid are sent
4. Zone transitions trigger re-evaluation of which entities each player should see

### Constants

- `KMP_ZONE_SIZE = 750.0` meters per zone
- `KMP_INTEREST_RADIUS = 1` (±1 zone from player)
- `KMP_AUTHORITY_HYSTERESIS = 64.0` units before zone authority transfer
- `KMP_MAX_ENTITIES_PER_ZONE = 512`
- `KMP_MAX_SYNC_ENTITIES = 2048` total per client

---

## Disconnect & Recovery

### Disconnect Detection

The client detects disconnect when `Core::IsConnected()` is true but `NetworkClient::IsConnected()` is false.

### Cleanup Flow

1. **Teleport remote entities underground** (`y = -10000`) — must happen BEFORE clearing the registry (which wipes game object pointers)
2. **SetConnected(false)** triggers full cleanup:
   - Clear entity registry (remote entities)
   - Clear interpolation system
   - Reset player controller
   - Reset connection state
3. **System message:** "Disconnected from server."
4. **Auto-reconnect:** If auto-connect is enabled, attempts reconnect after 2 seconds

### `/disconnect` Command

The `/disconnect` command performs a clean disconnect: teleports all remote entities underground, sends disconnect packet, resets all state.

---

## Game Memory Layout (Offsets)

All offsets are for **Kenshi v1.0.68** (GOG version). Steam version has different `.data` layout for singleton RVAs — runtime string xref fallback is used.

### Character Object

| Field | Offset | Type | Source |
|-------|--------|------|--------|
| faction | 0x10 | Faction* | KServerMod (verified) |
| name | 0x18 | std::string (MSVC) | KServerMod (verified) |
| gameDataPtr | 0x40 | GameData* | KServerMod (verified) |
| position | 0x48 | Vec3 (3 floats) | KServerMod (verified) |
| rotation | 0x58 | Quat (4 floats) | KServerMod (verified) |
| inventory | 0x2E8 | Inventory* | KServerMod (verified) |
| stats | 0x450 | Stats (inline, 23 floats) | KServerMod (verified) |

### Character Pointer Chains

| Chain | Path | Type | Source |
|-------|------|------|--------|
| Health | char+0x2B8 -> +0x5F8 -> +0x40 | float[7] (8-byte stride) | CE verified |
| Money | char+0x298 -> +0x78 -> +0x88 | int | CE verified |
| Writable Position | char+animClassOffset -> +0xC0 -> +0x320 -> +0x20 | Vec3 | KServerMod |

### GameWorld Singleton

| Field | Offset | Type |
|-------|--------|------|
| gameSpeed | 0x700 | float |
| characterList | 0x0888 | pointer array |
| zoneManager | 0x08B0 | ZoneManager* |

RVA: `0x02133040` (GOG v1.0.68)

### TimeManager (captured at runtime via hook, NOT on GameWorld)

| Field | Offset | Type |
|-------|--------|------|
| timeOfDay | 0x08 | float (0.0-1.0) |
| gameSpeed | 0x10 | float |

### PlayerBase Singleton

RVA: `0x01AC8A90` (GOG v1.0.68)

### Building Object

| Field | Offset | Type |
|-------|--------|------|
| name | 0x10 | std::string |
| position | 0x48 | Vec3 |
| rotation | 0x58 | Quat |
| ownerFaction | 0x80 | Faction* |
| health | 0xA0 | float |
| maxHealth | 0xA4 | float |
| isDestroyed | 0xA8 | bool |
| functionality | 0xC0 | BuildingFunctionality* |
| inventory | 0xE0 | Inventory* |
| buildProgress | 0x110 | float (0.0-1.0) |
| isConstructed | 0x114 | bool |

### Faction Object

| Field | Offset | Type |
|-------|--------|------|
| factionId | 0x08 | uint32 |
| name | 0x10 | std::string |
| members | 0x30 | pointer array |
| memberCount | 0x38 | int |
| relations | 0x50 | map pointer |
| isPlayerFaction | 0x90 | bool |
| money | 0xA0 | int |

### Stats (23 Skills, float, 4-byte stride from stats base)

| Offset | Skill | Offset | Skill |
|--------|-------|--------|-------|
| 0x00 | Melee Attack | 0x04 | Melee Defence |
| 0x08 | Dodge | 0x0C | Martial Arts |
| 0x10 | Strength | 0x14 | Toughness |
| 0x18 | Dexterity | 0x1C | Athletics |
| 0x20 | Crossbows | 0x24 | Turrets |
| 0x28 | Precision Shooting | 0x30 | Stealth |
| 0x34 | Assassination | 0x38 | Lockpicking |
| 0x3C | Thievery | 0x40 | Science |
| 0x44 | Engineering | 0x48 | Medic |
| 0x50 | Farming | 0x54 | Cooking |
| 0x58 | Weaponsmith | 0x5C | Armoursmith |
| 0x60 | Labouring | | |

### MSVC std::string Memory Layout

- **SSO threshold:** 15 characters (16 bytes including null)
- Names <= 15 chars fit inline (safe for WriteName)
- **Inline buffer:** bytes 0x00-0x0F
- **Size:** offset 0x10 (uint64_t)
- **Capacity:** offset 0x18 (uint64_t)
- When capacity > 15: bytes 0x00-0x07 contain a **heap pointer** to the character data

---

## Constants & Limits Reference

| Constant | Value | Description |
|----------|-------|-------------|
| `KMP_PROTOCOL_VERSION` | 1 | Protocol compatibility check |
| `KMP_DEFAULT_PORT` | 27800 | Default game server UDP port |
| `KMP_MAX_PLAYERS` | 16 | Maximum players per server |
| `KMP_MAX_NAME_LENGTH` | 31 | Maximum player name length |
| `KMP_TICK_RATE` | 20 Hz | State sync frequency (50ms intervals) |
| `KMP_TICK_INTERVAL_MS` | 50 | Milliseconds per tick |
| `KMP_INTERP_DELAY_SEC` | 0.1 | Default interpolation buffer (100ms) |
| `KMP_INTERP_DELAY_MIN` | 0.05 | Minimum adaptive delay (50ms) |
| `KMP_INTERP_DELAY_MAX` | 0.2 | Maximum adaptive delay (200ms) |
| `KMP_MAX_SNAPSHOTS` | 8 | Ring buffer size per entity |
| `KMP_EXTRAP_MAX_SEC` | 0.25 | Maximum extrapolation (250ms) |
| `KMP_SNAP_THRESHOLD_MIN` | 5.0 | Below = smooth blend |
| `KMP_SNAP_THRESHOLD_MAX` | 50.0 | Above = instant teleport |
| `KMP_SNAP_CORRECT_SEC` | 0.15 | Snap correction blend duration (150ms) |
| `KMP_JITTER_EMA_ALPHA` | 0.1 | Jitter smoothing factor |
| `KMP_CHANNEL_COUNT` | 3 | ENet channels |
| `KMP_UPSTREAM_LIMIT` | 128 KB/s | Client upstream bandwidth cap |
| `KMP_DOWNSTREAM_LIMIT` | 256 KB/s | Client downstream bandwidth cap |
| `KMP_CONNECT_TIMEOUT_MS` | 5000 | Connection timeout |
| `KMP_KEEPALIVE_INTERVAL` | 1000 | Keepalive frequency (1s) |
| `KMP_TIMEOUT_MS` | 10000 | Idle timeout (10s) |
| `KMP_ZONE_SIZE` | 750.0 | Zone dimension in meters |
| `KMP_INTEREST_RADIUS` | 1 | Interest grid radius (3x3) |
| `KMP_AUTHORITY_HYSTERESIS` | 64.0 | Zone transfer threshold |
| `KMP_POS_CHANGE_THRESHOLD` | 0.1 | Min position delta to send |
| `KMP_ROT_CHANGE_THRESHOLD` | 0.01 | Min rotation delta to send |
| `KMP_MAX_ENTITIES_PER_ZONE` | 512 | Per-zone entity cap |
| `KMP_MAX_SYNC_ENTITIES` | 2048 | Total synced entities per client |
| `MAX_SPAWNS_PER_PLAYER` | 8 | Per-player entity spawn cap |
| `MAX_SPAWN_RETRIES` | 200 | Spawn retry attempts (~10s) |
| `MAX_CHAT_LINES` | 10 | HUD chat display rows |
| `MAX_PLAYER_ROWS` | 8 | HUD player list rows |
| `MAX_LOG_LINES` | 20 | HUD debug log rows |
| `MAX_CHAT_HISTORY` | 50 | Total chat entries in HUD |
| `MAX_LOG_ENTRIES` | 100 | Total debug log entries |
| `CHAT_FADE_SECONDS` | 30 | Chat auto-hide timeout |

### Entity ID Partitioning

| Range | Type |
|-------|------|
| 1-255 | Players (server-assigned) |
| 256-8191 | NPCs (remote entities) |
| 8192-16383 | Buildings |
| 16384-24575 | Containers |
| 24576-32767 | Squads |
| 0x10000000+ | Client-side local IDs (pre-remap) |
| 0x80000000+ | Server-side squad IDs |

---

## Message Type Reference

### Connection (Channel 0 — Reliable Ordered)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x01 | C2S_Handshake | Client->Server | Protocol version, player name, game version |
| 0x02 | S2C_HandshakeAck | Server->Client | Player ID, server tick, time, weather, player count |
| 0x03 | S2C_HandshakeReject | Server->Client | Reason code (full/version/banned/other) + text |
| 0x04 | C2S_Disconnect | Client->Server | Clean disconnect notification |
| 0x05 | S2C_PlayerJoined | Server->All | New player ID and name |
| 0x06 | S2C_PlayerLeft | Server->All | Player ID and reason (disconnect/timeout/kicked) |
| 0x07 | C2S_Keepalive | Client->Server | Heartbeat |
| 0x08 | S2C_KeepaliveAck | Server->Client | Heartbeat response |

### World State (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x10 | S2C_WorldSnapshot | Server->Client | Bulk entity spawn for new joiners |
| 0x11 | S2C_TimeSync | Server->All | Server tick, time of day, weather, game speed |
| 0x12 | S2C_ZoneData | Server->Client | Zone population data |
| 0x13 | C2S_ZoneRequest | Client->Server | Request zone data |

### Entity Lifecycle (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x20 | S2C_EntitySpawn | Server->All | Entity ID, type, owner, template, position, faction |
| 0x21 | S2C_EntityDespawn | Server->All | Entity ID and reason (normal/killed/out of range) |
| 0x22 | C2S_EntitySpawnReq | Client->Server | Request entity registration |
| 0x23 | C2S_EntityDespawnReq | Client->Server | Request entity removal |

### Movement (Channel 2 — Unreliable Sequenced)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x30 | C2S_PositionUpdate | Client->Server | Batched character positions (count + CharacterPosition[]) |
| 0x31 | S2C_PositionUpdate | Server->Clients | Relayed positions with source player ID |
| 0x32 | C2S_MoveCommand | Client->Server | Move command (target position + move type) |
| 0x33 | S2C_MoveCommand | Server->Clients | Relayed move command |

### Combat (Channel 1 — Reliable Unordered)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x40 | C2S_AttackIntent | Client->Server | Attacker, target, attack type (melee/ranged) |
| 0x41 | S2C_CombatHit | Server->All | Full damage breakdown + result health |
| 0x42 | S2C_CombatBlock | Server->All | Block event with effectiveness |
| 0x43 | S2C_CombatDeath | Server->All | Death notification |
| 0x44 | S2C_CombatKO | Server->All | Knockout notification |
| 0x45 | C2S_CombatStance | Client->Server | Stance change (passive/defensive/aggressive/hold) |

### Stats (Channel 1)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x50 | S2C_StatUpdate | Server->Client | Single stat index + value |
| 0x51 | S2C_HealthUpdate | Server->Client | Per-body-part health array + blood level |
| 0x52 | S2C_EquipmentUpdate | Server->Client | Equipment slot + item template ID |
| 0x53 | C2S_EquipmentUpdate | Client->Server | Equipment change notification |

### Inventory (Channel 1)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x60 | C2S_ItemPickup | Client->Server | Character picked up item |
| 0x61 | C2S_ItemDrop | Client->Server | Character dropped item at position |
| 0x62 | C2S_ItemTransfer | Client->Server | Transfer between characters/containers |
| 0x63 | S2C_InventoryUpdate | Server->All | Inventory add/remove/modify |
| 0x64 | C2S_TradeRequest | Client->Server | Buy/sell request with price |
| 0x65 | S2C_TradeResult | Server->Client | Trade accepted/denied |

### Buildings (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x70 | C2S_BuildRequest | Client->Server | Build template + position + rotation |
| 0x71 | S2C_BuildPlaced | Server->All | Assigned entity ID + builder |
| 0x72 | S2C_BuildProgress | Server->All | Construction progress (0.0-1.0) |
| 0x73 | S2C_BuildDestroyed | Server->All | Building destroyed |
| 0x74 | C2S_DoorInteract | Client->Server | Open/close/lock/unlock door |
| 0x75 | S2C_DoorState | Server->All | Door state update |
| 0x76 | C2S_BuildDismantle | Client->Server | Dismantle request |
| 0x77 | C2S_BuildRepair | Client->Server | Repair request with amount |

### Squad (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0xB0 | C2S_SquadCreate | Client->Server | Creator entity + name |
| 0xB1 | S2C_SquadCreated | Server->All | Server-assigned squad net ID |
| 0xB2 | C2S_SquadAddMember | Client->Server | Squad + member entity ID |
| 0xB3 | S2C_SquadMemberUpdate | Server->All | Member added/removed |

### Faction (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0xC0 | C2S_FactionRelation | Client->Server | Faction A, B, relation (-100 to +100), causer |
| 0xC1 | S2C_FactionRelation | Server->All | Applied faction relation change |

### Chat (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x80 | C2S_ChatMessage | Client->Server | Sender ID + message string |
| 0x81 | S2C_ChatMessage | Server->All | Sender ID + message (relayed) |
| 0x82 | S2C_SystemMessage | Server->All | System broadcast message |

### Admin (Channel 0)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x90 | C2S_AdminCommand | Client->Server | Command type + target + params |
| 0x91 | S2C_AdminResponse | Server->Client | Success/denied + response text |

### Server Query

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0xA0 | C2S_ServerQuery | Client->Server | Lightweight query (no handshake needed) |
| 0xA1 | S2C_ServerInfo | Server->Client | Player count, max, port, time, PvP, name |

### Master Server

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0xD0 | MS_Register | Server->Master | Full server info registration |
| 0xD1 | MS_Heartbeat | Server->Master | Updated player count + time |
| 0xD2 | MS_Deregister | Server->Master | Server shutting down |
| 0xD3 | MS_QueryList | Client->Master | Request all servers |
| 0xD4 | MS_ServerList | Master->Client | Array of server entries |

---

## Server Security

The dedicated server implements multiple security measures:

| Feature | Description |
|---------|-------------|
| **Duplicate connection prevention** | Only one connection per player; duplicate peers are rejected |
| **Ownership validation** | Players can only control entities they own |
| **Zone bounds checking** | Position updates are validated against map boundaries |
| **Trade validation** | Both parties must be present and have required items/funds |
| **Faction validation** | Players cannot set arbitrary faction relations |
| **Spawn cap** | `MAX_SPAWNS_PER_PLAYER = 8` prevents entity flooding |
| **Entity limit** | `WouldExceedLimit()` check before spawning |
| **String length limits** | `PacketReader::ReadString` rejects strings over 1024 bytes |
| **Protocol version check** | Handshake validates `KMP_PROTOCOL_VERSION` match |
| **Rate limiting** | Player manager tracks connection rates |
| **Keepalive timeout** | 10-second idle timeout disconnects unresponsive clients |

---

## Configuration Reference

### Client Configuration (`%APPDATA%/KenshiMP/client.json`)

```json
{
  "playerName": "Player1",
  "lastServer": "192.168.1.100",
  "lastPort": 27800,
  "autoConnect": true,
  "masterServer": "127.0.0.1",
  "masterPort": 27801
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| playerName | string | "Player" | Your display name |
| lastServer | string | "127.0.0.1" | Last connected server address |
| lastPort | uint16 | 27800 | Last connected server port |
| autoConnect | bool | true | Auto-connect on game load |
| masterServer | string | "127.0.0.1" | Master server address for browser |
| masterPort | uint16 | 27801 | Master server port |

### Server Configuration (`server.json`)

See [Server Configuration](#server-configuration-serverjson) above for full reference.

---

## Build Instructions

### Requirements

- **Visual Studio 2022** with C++ Desktop Development workload
- **CMake 3.20+**
- C++17, x64 only

### Dependencies (bundled in `lib/`)

- **ENet 1.3.x** — UDP networking
- **MinHook 1.3.3** — Function hooking
- **spdlog** — Logging (header-only mode)
- **nlohmann/json** — JSON parsing (header-only)

### Build Commands

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build (Release)
cmake --build build --config Release

# Output locations:
# build/bin/Release/KenshiMP.Core.dll
# build/bin/Release/KenshiMP.Server.exe
# build/bin/Release/KenshiMP.MasterServer.exe
# build/bin/Release/KenshiMP.Injector.exe
# build/bin/Release/KenshiMP.TestClient.exe
# build/bin/Release/KenshiMP.UnitTest.exe
# build/bin/Release/KenshiMP.IntegrationTest.exe
# build/bin/Release/KenshiMP.LiveTest.exe
```

Post-build steps (automatic):
- `KenshiMP.Core.dll` is copied to the Kenshi game directory
- `KenshiMP.Server.exe` is copied to the Kenshi game directory
- MyGUI layout files (`dist/*.layout`) are deployed to Kenshi's data directory

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| HUD not appearing | Ensure Kenshi was launched via the Injector. Check that `Plugin=KenshiMP.Core` exists in `Plugins_x64.cfg` |
| "Connection failed" | Verify server IP/port. Ensure UDP port 27800 is open on the server. Check if UPnP succeeded in the server log |
| Characters not moving | Press Insert to open the debug log. Check that `SetPosition` shows as "resolved". If not, the pattern scanner failed to find the function |
| Remote characters not spawning | Check debug log for spawn queue status. The game must naturally create an NPC for the in-place replay to trigger. Walk around to trigger NPC spawns |
| High ping / lag | Server should be geographically close to players. Check server tickrate and network conditions |
| Server won't start | Check if port 27800 is already in use (`netstat -an | find "27800"`). Try a different port |
| Game crash on launch | Check `KenshiOnline.log` for details. Common cause: pattern scan failure for a critical function. Try GOG v1.0.68 for best compatibility |
| Server browser empty | Ensure the master server is running. Check that the game server's `masterServer` config points to the correct address |
| Chat not working | Ensure you press Enter (not Return on numpad). Check that input hooks are installed (debug log shows "INPUT: hooks installed") |
| Cannot kick/announce | These commands are host-only. The first player to connect is the host |

### Log Files

- **Client:** `KenshiOnline.log` (in Kenshi's game directory)
- **Server:** `KenshiOnline_Server.log` (in the server's directory)
- **Debug Output:** `OutputDebugString` messages prefixed with `KMP:` (visible in DebugView or VS debugger)

---

## Known Limitations

- **Visual building spawning** — Buildings are tracked on the server and in the entity registry, but game objects are not created for remote players. Requires reverse engineering the building factory for in-place replay spawning
- **NPC AI** — NPC sync is one-directional. Your NPCs are visible to others, but NPC AI runs independently per client. AI is suppressed on remote-controlled characters to prevent conflicts
- **Save games** — Each player uses their own Kenshi save. Save games are not synchronized between players
- **Steam singleton offsets** — The hardcoded PlayerBase and GameWorld RVAs are for GOG v1.0.68. Steam has a different `.data` layout. The runtime string xref fallback works but may fail on some Steam builds

---

## Implementation Status (13 Phases)

| Phase | Name | Status |
|-------|------|--------|
| 0 | Injection & Initialization | WORKING |
| 1 | Connection & Handshake | WORKING |
| 2 | Entity Registration & World Snapshot | WORKING |
| 3 | Entity Spawning (In-Place Replay) | WORKING |
| 4 | Position Synchronization | WORKING |
| 5 | Combat Synchronization | WORKING |
| 6 | Inventory & Equipment Sync | WORKING |
| 7 | Chat & Commands | WORKING |
| 8 | World State Sync (Time, Weather) | WORKING |
| 9 | Squad & Faction Sync | WORKING |
| 10 | Building & Door Sync | WORKING |
| 11 | Disconnect & Recovery | WORKING |
| 12 | Server Infrastructure | WORKING |

**13/13 phases fully functional.**

Handler coverage:
- **C2S (Client-to-Server):** 23/23 (100%)
- **S2C (Server-to-Client):** 28/28 (100%)

---

## Credits

Built on community reverse engineering work:

- [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) — Ogre plugin injection system
- [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib) — Game structure definitions and verified offsets
- [Kenshi Online](https://github.com/The404Studios/Kenshi-Online) — Memory address reference
- [OpenConstructionSet](https://github.com/lmaydev/OpenConstructionSet) — Game data SDK
- [KServerMod](https://github.com/) — Verified character struct offsets
- Cheat Engine community — Health chain, money chain, position write chain verification

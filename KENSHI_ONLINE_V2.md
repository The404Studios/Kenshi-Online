# Kenshi Online v2.0 - Complete Multiplayer System

## Overview

Kenshi Online v2.0 is a complete multiplayer system for Kenshi featuring:

- ✅ Full entity synchronization (players, NPCs, items)
- ✅ Server-authoritative combat system
- ✅ Inventory and equipment synchronization
- ✅ Dynamic world state (time, weather)
- ✅ Admin command system
- ✅ Session management and player lobby
- ✅ Spatial optimization for large worlds
- ✅ 20 Hz server update rate

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Kenshi Online v2.0                       │
└─────────────────────────────────────────────────────────────┘

┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐
│  C++ Plugin      │◄────►│  Client Service  │◄────►│  Game Server     │
│  (In-Game)       │ IPC  │  (Bridge)        │ TCP  │  (Authoritative) │
└──────────────────┘      └──────────────────┘      └──────────────────┘
       │                           │                          │
       │                           │                          │
  ┌────▼────┐                 ┌────▼────┐             ┌──────▼──────┐
  │ Entity  │                 │ Message │             │   Entity    │
  │ Bridge  │                 │ Forward │             │   Manager   │
  └─────────┘                 └─────────┘             └─────────────┘
       │                                                      │
  ┌────▼────┐                                          ┌──────▼──────┐
  │ Pattern │                                          │   Combat    │
  │ Coord.  │                                          │   Sync      │
  └─────────┘                                          └─────────────┘
                                                              │
                                                       ┌──────▼──────┐
                                                       │  Inventory  │
                                                       │    Sync     │
                                                       └─────────────┘
                                                              │
                                                       ┌──────▼──────┐
                                                       │World State  │
                                                       │   Manager   │
                                                       └─────────────┘
```

## Components

### 1. C++ Plugin (Re_Kenshi_Plugin)

**Location:** `Re_Kenshi_Plugin/`

**Purpose:** Runs inside Kenshi's process, reads game memory, and sends updates via IPC

**Key Files:**
- `NetworkProtocol.h` - Message format definitions
- `EntityBridge.h` - Game data to entity conversion
- `NetworkClient.h` - IPC communication
- `KenshiOnlinePlugin.cpp` - Main plugin logic

**Features:**
- Reads local player state from game memory
- Converts game data to network entities
- Sends player updates at 10 Hz
- Receives and renders remote players
- Pattern scanning for game structures

### 2. Client Service (KenshiOnline.ClientService)

**Location:** `KenshiOnline.ClientService/`

**Purpose:** Bridges C++ plugin to C# server

**Architecture:**
```
Named Pipe ←→ Client Service ←→ TCP Socket
  (Plugin)      (Bridge)         (Server)
```

**Features:**
- Named pipe server for plugin connection
- TCP client for server connection
- Bidirectional message forwarding
- Automatic reconnection
- Connection status monitoring

### 3. Core Library (KenshiOnline.Core)

**Location:** `KenshiOnline.Core/`

**Purpose:** Shared multiplayer logic

**Modules:**

#### Entities (`Entities/`)
- `Entity.cs` - Base entity with serialization
- `PlayerEntity.cs` - Player state (health, stats, skills, inventory)
- `NPCEntity.cs` - NPC with AI state
- `ItemEntity.cs` - Items with durability and stats

#### Synchronization (`Synchronization/`)
- `EntityManager.cs` - Entity registry with spatial grid
- `CombatSync.cs` - Server-authoritative combat
- `InventorySync.cs` - Server-authoritative inventory
- `WorldStateManager.cs` - Time, weather, global state

#### Session (`Session/`)
- `SessionManager.cs` - Player sessions and lobby

#### Admin (`Admin/`)
- `AdminCommands.cs` - Server administration

### 4. Game Server (KenshiOnline.Server)

**Location:** `KenshiOnline.Server/`

**Purpose:** Authoritative multiplayer server

**Features:**
- 20 Hz tick rate
- Delta synchronization (only send changes)
- TCP networking with JSON protocol
- Entity lifecycle management
- Combat validation
- Inventory validation
- Admin commands
- Console interface

## Setup & Usage

### Prerequisites

- ✅ Windows 10/11
- ✅ .NET 8.0 SDK
- ✅ CMake 3.15+
- ✅ Visual Studio 2022 (or Build Tools)
- ✅ Kenshi (GOG 1.0.68 recommended)

### Build Instructions

#### 1. Build C# Components

```batch
Build_KenshiOnline.bat
```

This builds:
- KenshiOnline.Core (library)
- KenshiOnline.Server (server executable)
- KenshiOnline.ClientService (bridge executable)

#### 2. Build C++ Plugin

```batch
cd Re_Kenshi_Plugin
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

Output: `Re_Kenshi_Plugin.dll`

### Running Kenshi Online

#### Server Host

1. Run `Host_KenshiOnline.bat`
2. Choose port (default 7777)
3. Server starts and waits for players

#### Client (Player)

1. Inject `Re_Kenshi_Plugin.dll` into `kenshi_x64.exe`
   - Use any DLL injector (e.g., Extreme Injector)
   - Inject AFTER Kenshi's main menu loads

2. Run `Join_KenshiOnline.bat`
3. Enter server IP and port
4. Client service bridges game to server

5. Launch Kenshi and load a save

### DLL Injection

**Recommended Tools:**
- Extreme Injector
- Cheat Engine
- Process Hacker

**Steps:**
1. Launch Kenshi
2. Wait for main menu to fully load
3. Open your DLL injector
4. Select `kenshi_x64.exe` process
5. Inject `Re_Kenshi_Plugin.dll`
6. Check `KenshiOnline.log` for success

## Network Protocol

### Message Types

```
Connection:
- connect       - Initial connection
- disconnect    - Graceful disconnect
- heartbeat     - Keep-alive (every 30s)

Entities:
- entity_update  - Entity state changed
- entity_create  - New entity spawned
- entity_destroy - Entity removed
- entity_snapshot - Full world snapshot

Gameplay:
- combat_event   - Combat action
- inventory_action - Inventory change
- world_state    - Time/weather update

Admin:
- admin_command  - Admin command
- response       - Server response
```

### Message Format

```json
{
  "Type": "entity_update",
  "PlayerId": "player_12345",
  "SessionId": "session_67890",
  "Data": {
    "id": "entity_uuid",
    "type": "Player",
    "position": { "x": 100.0, "y": 50.0, "z": 200.0 },
    "health": 85.5,
    ...
  },
  "Timestamp": 1234567890
}
```

## Server Commands

### Console Commands

```
help       - Show help
status     - Show server status
players    - List connected players
stop       - Stop server
```

### Admin Commands

**Player Management:**
```
kick <playerId> [reason]
ban <playerId> [reason]
setadmin <playerId> <true|false>
teleport <playerId> <x> <y> <z>
heal <playerId>
kill <playerId>
```

**World Management:**
```
settime <hour>           - Set game time (0-24)
setspeed <multiplier>    - Set game speed (0.1-10)
pause                    - Pause game
unpause                  - Unpause game
setweather <type>        - Set weather
nextday                  - Advance to next day
```

**Entity Spawning:**
```
spawnitem <name> <type> <x> <y> <z>
spawnnpc <name> <type> <x> <y> <z>
```

**Info:**
```
stats    - Server statistics
list     - Player list
info     - Server info
debug    - Debug information
```

## Configuration

### Server Config

Create `kenshi_online_config.json`:

```json
{
  "server_name": "My Kenshi Server",
  "max_players": 32,
  "port": 7777,
  "update_rate": 20,
  "world": {
    "start_time": 12.0,
    "time_scale": 1.0,
    "weather": "Clear"
  }
}
```

### Client Config

Create `kenshi_online_config.json` in game directory:

```json
{
  "player_name": "YourName",
  "player_id": "unique_id",
  "server_address": "127.0.0.1",
  "server_port": 7777
}
```

## Performance

### Server

- **Update Rate:** 20 Hz (50ms per tick)
- **Bandwidth:** ~5-10 KB/s per player
- **CPU:** Low (2-5% on modern CPU)
- **RAM:** ~100 MB + (10 MB per player)

### Client

- **Plugin Impact:** Minimal (<1% CPU)
- **IPC Latency:** <1ms
- **Network Latency:** Depends on connection

## Optimization

### Spatial Grid

Entities are organized in a spatial grid for efficient range queries:
- Grid cell size: 100m x 100m
- Only syncs entities within sync radius (default 100m)
- Reduces bandwidth and CPU usage

### Delta Synchronization

Only changed entities are sent:
- Entities mark themselves "dirty" when changed
- Server only broadcasts dirty entities
- Reduces network traffic by ~80%

### Priority System

Entities have sync priorities:
- Priority 10: Players (always sync)
- Priority 5: NPCs (medium priority)
- Priority 2: Items (low priority)

## Troubleshooting

### Plugin won't inject

- Make sure Kenshi is fully loaded (main menu visible)
- Check if you're using a compatible DLL injector
- Verify DLL is 64-bit
- Check `KenshiOnline.log` for errors

### Can't connect to server

- Check firewall settings
- Verify port is open
- Make sure server is running
- Check client service is running
- Verify IP address and port

### Poor performance

- Reduce sync radius in config
- Lower server update rate
- Reduce max players
- Check network connection

### Desync issues

- Restart client service
- Check network stability
- Verify server isn't overloaded
- Check logs for errors

## Known Limitations

1. **Visual Only:** Remote players are visual representations only (no AI)
2. **Combat Sync:** Combat is synchronized but may feel delayed
3. **Inventory:** Item containers not fully synchronized
4. **Buildings:** Building placement not synchronized
5. **Saves:** Multiplayer sessions are temporary (no persistence)

## Development

### Building from Source

```batch
# Clone repository
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# Build C# projects
dotnet build

# Build C++ plugin
cd Re_Kenshi_Plugin
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Project Structure

```
Kenshi-Online/
├── KenshiOnline.Core/           # Shared library
│   ├── Entities/
│   ├── Synchronization/
│   ├── Session/
│   └── Admin/
├── KenshiOnline.Server/         # Game server
├── KenshiOnline.ClientService/  # IPC bridge
├── Re_Kenshi_Plugin/            # C++ plugin
│   ├── include/
│   │   ├── NetworkProtocol.h
│   │   ├── EntityBridge.h
│   │   └── NetworkClient.h
│   └── src/
│       └── KenshiOnlinePlugin.cpp
└── Scripts/                     # Build & launcher scripts
```

### Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

### License

This project is for educational purposes only. Kenshi is property of Lo-Fi Games.

## Credits

- **Original Game:** Lo-Fi Games (Kenshi)
- **KServerMod:** codiren (memory offsets and patterns)
- **Development:** The404Studios

## Support

For issues, questions, or suggestions:
- GitHub Issues: https://github.com/The404Studios/Kenshi-Online/issues
- Discord: [Join our server]

## Changelog

### v2.0 (Current)
- Complete system remake
- Full entity synchronization
- Server-authoritative combat and inventory
- Dynamic world state
- Admin command system
- Spatial optimization
- 20 Hz server tick rate

### v1.0 (Previous)
- Basic multiplayer
- Simple position sync
- Example implementation

---

**Enjoy playing Kenshi with your friends!** 🎮

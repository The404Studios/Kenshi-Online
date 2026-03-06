# Kenshi-Online

**16-player co-op multiplayer mod for Kenshi**

[![Latest Release](https://img.shields.io/github/v/release/The404Studios/Kenshi-Online?style=flat-square)](https://github.com/The404Studios/Kenshi-Online/releases/latest)
[![License](https://img.shields.io/github/license/The404Studios/Kenshi-Online?style=flat-square)](LICENSE)

https://discord.gg/JJcJZxE4ma


Kenshi-Online adds seamless multiplayer to Kenshi using native MyGUI integration, ENet networking, and Ogre plugin injection. Players can explore, fight, build, and trade together in the open world of Kenshi.

> **v1.0.2 — Stability Update** (March 2026): Major crash fixes for zone loading, spawn pipeline, and disconnect handling. See [CHANGELOG.md](CHANGELOG.md) for details.

---

## Download & Install (Players)

**No building required.** Download the latest release and play:

1. Go to [**Releases**](https://github.com/The404Studios/Kenshi-Online/releases/latest)
2. Download `KenshiMP-Install.zip`
3. Extract to your Kenshi directory (`Steam/steamapps/common/Kenshi/`)
4. Run `KenshiMP.Injector.exe`
5. Enter your player name and server IP, click **PLAY**

See the full [Installation Guide (English)](docs/english.md) or [Installation Guide (Russian)](docs/russian.md) for detailed walkthrough with troubleshooting.

---

## Features

- **Up to 16 players** on a single server
- **Dedicated server** with persistence and console commands
- **Master server** with centralized server browser (auto-discovery)
- **Full network replication** — characters, NPCs, combat, buildings, items
- **Zone-based sync** — efficient bandwidth usage with interest management
- **Server-authoritative** combat and world state
- **Native MyGUI HUD** — status bar, chat with timestamps, player list, debug log
- **Client commands** — `/tp`, `/time`, `/kick`, `/announce`, `/connect`, `/disconnect`, `/pos`, `/players`, `/status`, `/entities`, `/ping`, `/debug`, `/help`
- **Just launch and play** — Ogre plugin injection, no manual setup

## Architecture

```
KenshiMP.Injector.exe    -> Modifies Plugins_x64.cfg, launches Kenshi
KenshiMP.Core.dll        -> Loaded by Ogre as a plugin, hooks game functions
KenshiMP.Server.exe      -> Dedicated server (host on VPS or locally)
KenshiMP.MasterServer.exe-> Centralized server browser registry (port 27801)
KenshiMP.Common.lib      -> Shared types, protocol, serialization
KenshiMP.Scanner.lib     -> Pattern scanning, MinHook wrapper
```

## What to Expect In-Game

After connecting, your character syncs to the server. Other players' characters will appear when you **walk near a town or NPC group** — this is by design. The mod uses Kenshi's own NPC creation events to safely spawn remote characters with valid game state.

You will see HUD messages guiding you:
- **"Connected to server"** — you're online
- **"Walk near NPCs to trigger character spawns"** — move toward a town to see other players
- **"[PlayerName] joined"** — another player connected

### Known Behavior
- Remote characters appear after you walk near NPCs (towns, patrols, caravans)
- First spawn may take 10-30 seconds while the game loads nearby NPC zones
- If a player disconnects, their character is cleaned up automatically
- The host's game speed controls time for all players

---

## Quick Start

### Player (Pre-built Release)
1. Download `KenshiMP-Install.zip` from [Releases](https://github.com/The404Studios/Kenshi-Online/releases/latest)
2. Extract to your Kenshi folder
3. Run `KenshiMP.Injector.exe`
4. Set your player name and server address
5. Click **PLAY** — Kenshi launches with multiplayer enabled

### Server (Local or VPS)
1. Copy `KenshiMP.Server.exe` to your VPS
2. Create `server.json` (or let it generate defaults):
```json
{
  "serverName": "My Kenshi Server",
  "port": 27800,
  "maxPlayers": 16,
  "pvpEnabled": true,
  "gameSpeed": 1.0
}
```
3. Run: `./KenshiMP.Server.exe`
4. Forward port **27800 UDP** on your router/firewall
5. Players connect via your IP address or the server browser

### Server Commands
```
status   - Show server info
players  - List connected players
kick <id> - Kick a player
say <msg> - Broadcast system message
save     - Save world state
stop     - Shutdown server
```

## Building (Developers Only)

Most players should use the [pre-built release](https://github.com/The404Studios/Kenshi-Online/releases/latest). Only follow these steps if you want to build from source.

### Requirements
- **Visual Studio 2022** with C++ Desktop Development workload
- **CMake 3.20+**
- **vcpkg** (for dependency management)

### Steps

```bash
# 1. Clone with submodules
git clone --recursive https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# 2. Install dependencies via vcpkg
vcpkg install enet:x64-windows
vcpkg install minhook:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install spdlog:x64-windows

# 3. Configure with CMake
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake

# 4. Build
cmake --build build --config Release

# 5. Output
# build/bin/Release/KenshiMP.Injector.exe
# build/bin/Release/KenshiMP.Core.dll
# build/bin/Release/KenshiMP.Server.exe
# build/bin/Release/KenshiMP.MasterServer.exe
```

### Manual Library Setup (without vcpkg)
Place the following in the `lib/` directory:
- `lib/enet/` - [ENet 1.3.x](https://github.com/lsalzman/enet) source
- `lib/minhook/` - [MinHook 1.3.3](https://github.com/TsudaKageyu/minhook) source
- `lib/json/` - [nlohmann/json](https://github.com/nlohmann/json) source
- `lib/spdlog/` - [spdlog](https://github.com/gabime/spdlog) source

## Controls (In-Game)

| Key | Action |
|-----|--------|
| F1 | Open/close multiplayer menu |
| Insert | Toggle debug/loading log panel |
| Enter | Open/close chat |
| Tab | Toggle player list |
| ` (backtick) | Toggle debug info |
| Escape | Close all panels |

## Network Protocol

- **Port**: 27800 UDP (ENet)
- **Channels**: 3 (reliable ordered, reliable unordered, unreliable sequenced)
- **Tick Rate**: 20 Hz (50ms)
- **Max Players**: 16

### Synced State
- Player character positions, rotations, animations
- NPC positions and AI states (zone-based)
- Combat: attacks, damage, deaths, knockouts
- Buildings: placement, construction, destruction
- Items: pickup, drop, inventory transfers
- Time of day, weather, game speed
- Chat messages

## Project Structure

```
KenshiMP/
+-- KenshiMP.Common/          # Shared library
|   +-- include/kmp/
|       +-- types.h           # Vec3, Quat, EntityID, ZoneCoord
|       +-- constants.h       # Tick rate, max players, port
|       +-- messages.h        # Network message structs
|       +-- protocol.h        # Packet reader/writer
|       +-- compression.h     # Delta compression
|       +-- config.h          # Client/server config
|
+-- KenshiMP.Scanner/         # Pattern scanner library
|   +-- include/kmp/
|       +-- scanner.h         # IDA-style pattern matching
|       +-- patterns.h        # Known Kenshi signatures
|       +-- memory.h          # Safe memory read/write
|       +-- hook_manager.h    # MinHook wrapper
|
+-- KenshiMP.Core/            # Ogre plugin DLL
|   +-- dllmain.cpp           # Plugin entry
|   +-- core.cpp              # Master initialization
|   +-- hooks/                # Game function hooks (14 modules)
|   +-- game/                 # Reconstructed game types
|   +-- net/                  # ENet client
|   +-- sync/                 # Entity registry, interpolation
|   +-- ui/                   # Native MyGUI overlay + menu
|
+-- KenshiMP.Server/          # Dedicated server
|   +-- main.cpp              # Console entry + commands
|   +-- server.cpp            # Game state, networking
|
+-- KenshiMP.MasterServer/    # Server browser registry
|   +-- main.cpp              # ENet master server (port 27801)
|
+-- KenshiMP.Injector/        # Launcher
    +-- main.cpp              # Win32 GUI
    +-- injector.cpp          # Plugins_x64.cfg modifier
    +-- process.cpp           # Game launcher
```

## Technical Details

### Injection Method
Uses the Ogre3D plugin system (proven by RE_Kenshi). The injector modifies
`Plugins_x64.cfg` to add `Plugin=KenshiMP.Core`, and Ogre loads our DLL
automatically during engine initialization. No process injection or manual
DLL loading required.

### Pattern Scanner
Scans kenshi_x64.exe in-memory using IDA-style byte patterns with wildcards.
Resolves RIP-relative addresses for x64 code. Falls back to known pointer chains
from Cheat Engine community.

### State Synchronization
- **Entity ownership**: Each player owns their squad; server owns NPCs
- **Interpolation**: 100ms buffer with hermite spline for smooth remote movement
- **Zone interest**: 3x3 zone grid around each player (only sync nearby entities)
- **Delta compression**: float16 position deltas, smallest-three quaternion encoding

## Documentation

- [English Installation & Usage Guide](docs/english.md) — full walkthrough, controls, commands, troubleshooting
- [Russian Installation & Usage Guide](docs/russian.md) — full walkthrough in Russian
- [Changelog](CHANGELOG.md) — version history and patch notes
- [Technical Documentation](docs/PHASES.md) — multiplayer phase architecture

## Credits

Built on community reverse engineering work:
- [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) — Ogre plugin injection system
- [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib) — Game structure definitions
- [OpenConstructionSet](https://github.com/lmaydev/OpenConstructionSet) — Game data SDK

## License

MIT License

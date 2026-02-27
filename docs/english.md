# Kenshi-Online (KenshiMP) — User Guide

## What is Kenshi-Online?

Kenshi-Online is a multiplayer mod that lets up to 16 players play Kenshi together. You can explore the world, build bases, fight enemies, and interact with each other in real time.

### Features
- Up to 16 players on one server
- Real-time character position sync with smooth interpolation
- Multiplayer combat with server-authoritative damage resolution
- In-game chat system
- ImGui overlay (connect UI, player list, chat, debug info)
- Dedicated server with console commands
- World state persistence (save/load between server restarts)
- Zone-based interest management (only sync nearby entities)

---

## Requirements

- Kenshi v1.0.59 or later (tested on v1.0.68)
- Windows 10/11 64-bit
- The mod files:
  - `KenshiMP.Core.dll` — the client plugin
  - `KenshiMP.Injector.exe` — injects the plugin into Kenshi
  - `KenshiMP.Server.exe` — dedicated server (only needed if hosting)

---

## How to Play (Client)

### Step 1: Launch Kenshi
Start Kenshi normally through Steam or GOG. Load your save or start a new game.

### Step 2: Inject the mod
Run `KenshiMP.Injector.exe`. It will:
- Find the running Kenshi process
- Inject `KenshiMP.Core.dll` into it
- The mod initializes automatically (hooks, overlay, networking)

You should see "KENSHI-ONLINE | Offline" in the top-right corner of the screen.

### Step 3: Connect to a server
When the mod loads, a **Main Menu** overlay appears automatically with these options:
- **Quick Connect** — enter a server IP and port, then connect
- **Server Browser** — browse available servers or use direct IP
- **Settings** — set your player name, default server, auto-connect

Enter your player name, choose a server, and click Connect. Once connected, the menu closes and the in-game HUD appears showing player count and ping.

### Controls
| Key | Action |
|-----|--------|
| F1 | Open/close Main Menu (or Connection panel if connected) |
| Tab | Open/close Player List |
| Enter | Open/close Chat |
| ` (backtick) | Open/close Debug overlay |
| Escape | Back / Close panels |

### Chat
Press **Enter** to open the chat window. Type your message and press Enter or click Send. System messages (player joins, leaves, kicks) appear automatically.

---

## How to Host a Server

### Quick Start (Local)
1. Run `KenshiMP.Server.exe`
2. A `server.json` config file is created automatically on first run
3. The server starts listening on port **27800**
4. Players connect using your IP address

### Server Configuration (server.json)
```json
{
  "serverName": "My Kenshi Server",
  "port": 27800,
  "maxPlayers": 16,
  "tickRate": 20,
  "gameSpeed": 1,
  "pvpEnabled": true,
  "savePath": "world.kmpsave"
}
```

| Setting | Description |
|---------|-------------|
| serverName | Display name for the server |
| port | UDP port to listen on (default: 27800) |
| maxPlayers | Maximum concurrent players (1-16) |
| tickRate | Server update frequency in Hz (default: 20) |
| gameSpeed | Game time multiplier (1 = normal) |
| pvpEnabled | Allow player vs player combat |
| savePath | File path for world state saves |

You can also pass a custom config path: `KenshiMP.Server.exe path/to/config.json`

### Server Console Commands
| Command | Description |
|---------|-------------|
| help | Show available commands |
| status | Show server status (players, entities, tick, uptime) |
| players | List all connected players with ping and zone |
| kick \<id\> | Kick a player by their ID |
| say \<message\> | Broadcast a system message to all players |
| save | Save current world state to disk |
| stop | Save world and shut down the server |

### Hosting on a VPS / Dedicated Machine

1. **Copy files** to the server:
   - `KenshiMP.Server.exe`
   - `server.json` (optional, auto-generated on first run)

2. **Open the firewall** for UDP port 27800:
   ```
   # Windows
   netsh advfirewall firewall add rule name="KenshiMP" dir=in action=allow protocol=UDP localport=27800

   # Linux (if using Wine)
   sudo ufw allow 27800/udp
   ```

3. **Edit server.json** with your desired settings

4. **Run the server**:
   ```
   KenshiMP.Server.exe
   ```

5. Players connect using the VPS public IP and port 27800

### World Persistence
- The server automatically saves world state on shutdown (`stop` command or Ctrl+C)
- Use the `save` command to save manually at any time
- On startup, the server loads the previous world state if the save file exists
- The save file contains all entity positions, rotations, health, factions, and template info

---

## How It Works (Technical Overview)

### Architecture
```
[Kenshi Client 1]  <--ENet UDP-->  [Dedicated Server]  <--ENet UDP-->  [Kenshi Client 2]
     |                                    |                                   |
 KenshiMP.Core.dll              KenshiMP.Server.exe              KenshiMP.Core.dll
 (hooks + overlay)              (authority + relay)              (hooks + overlay)
```

### Connection Flow
1. Client calls `ConnectAsync()` — initiates ENet connection (non-blocking)
2. On connect, client sends `C2S_Handshake` with player name and protocol version
3. Server responds with `S2C_HandshakeAck` (player ID, time of day, player count)
4. Server sends existing players via `S2C_PlayerJoined` messages
5. Server sends world snapshot (all entities) via `S2C_EntitySpawn` messages

### Position Sync
- Each client reads local character positions from game memory every tick
- Positions are batched into `C2S_PositionUpdate` packets (sent unreliable for speed)
- Server stores positions and relays them to other clients (zone-filtered)
- Receiving clients interpolate positions with a 100ms delay buffer for smoothness
- Interpolated positions are written to game characters via the physics engine

### Combat
- When a local character attacks, the client sends `C2S_AttackIntent`
- The server resolves combat (random body part, damage calculation, block chance)
- Server broadcasts `S2C_CombatHit` with damage values and result health
- Clients apply damage to their local game objects
- Death/KO is broadcast separately if health thresholds are crossed

### Entity Spawning
- When a client's game creates a new character, the hook sends `C2S_EntitySpawnReq`
- Server assigns a network ID and broadcasts `S2C_EntitySpawn` to all clients
- Receiving clients use the game's own factory function to create a real in-game character
- The SpawnManager captures the game's character factory on the first spawn hook

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Overlay not appearing | Make sure you ran the Injector after Kenshi is fully loaded |
| "Connection failed" | Check the server IP/port, ensure UDP 27800 is open |
| Characters not moving | Check Debug overlay (backtick key) — verify SetPosition is "resolved" |
| High ping / lag | Server should be geographically close to players; check network |
| Server won't start | Check if port 27800 is already in use; try a different port |
| Game crash on inject | Check KenshiOnline.log for details; may be a pattern scan failure |

### Log Files
- **Client**: `KenshiOnline.log` (in Kenshi's directory)
- **Server**: `KenshiOnline_Server.log` (in the server's directory)

---

## Known Limitations (v0.1.0)

- **Equipment sync** is not yet functional (requires further reverse engineering of equipment memory layout)
- **Building placement** is tracked on the server but not visually replicated for other players
- **Server browser** shows local and last-used servers only; no master server or LAN discovery
- **NPC sync** is one-directional (your NPCs are visible to others, but NPC AI runs independently per client)
- **Save games** are not synchronized — each player uses their own Kenshi save

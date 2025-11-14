# Re_Kenshi Multiplayer Setup Guide

## Overview

Re_Kenshi is a working multiplayer mod for Kenshi that synchronizes player positions and game state across multiple clients in real-time.

## Architecture

The system consists of three components:

```
┌─────────────────────────────────────────────────────────────────┐
│                       KENSHI GAME                                │
│                                                                   │
│   ┌───────────────────────────────────────────────────────┐    │
│   │  Re_Kenshi_Plugin.dll (C++ - Injected into game)     │    │
│   │  - Scans game memory for player data                  │    │
│   │  - Sends player updates via IPC                       │    │
│   │  - Receives remote player data via IPC                │    │
│   └────────────────────┬──────────────────────────────────┘    │
│                        │ Named Pipe IPC                         │
└────────────────────────┼────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│  ReKenshiClientService.exe (C# - Runs on client machine)        │
│  - Receives player updates from C++ plugin via IPC              │
│  - Forwards updates to game server via TCP                      │
│  - Receives remote players from server                          │
│  - Sends remote players to C++ plugin via IPC                   │
└────────────────────────┬────────────────────────────────────────┘
                         │ TCP Connection
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│  ReKenshiServer.exe (C# - Runs on server machine)               │
│  - Accepts TCP connections from multiple clients                │
│  - Synchronizes player state between all connected clients      │
│  - Broadcasts position updates to all players                   │
└─────────────────────────────────────────────────────────────────┘
```

## Installation & Setup

### Step 1: Build the C++ Plugin

```bash
cd Re_Kenshi_Plugin
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

This creates `Re_Kenshi_Plugin.dll` in `build/bin/Release/`

### Step 2: Build the C# Server

```bash
cd ReKenshi.Server
dotnet build -c Release
```

This creates `ReKenshiServer.exe` in `bin/Release/net8.0/`

### Step 3: Build the C# Client Service

```bash
cd ReKenshi.ClientService
dotnet build -c Release
```

This creates `ReKenshiClientService.exe` in `bin/Release/net8.0/`

## Running the Multiplayer System

### On the Server Machine:

1. Start the server:
```bash
cd ReKenshi.Server/bin/Release/net8.0
./ReKenshiServer.exe 7777
```

The server will listen on port 7777 for client connections.

Server commands:
- `/list` - Show connected players
- `/kick <playerId>` - Kick a player
- `/stop` - Stop the server

### On Each Client Machine:

1. Start the client service:
```bash
cd ReKenshi.ClientService/bin/Release/net8.0
./ReKenshiClientService.exe <server-ip> 7777
```

Replace `<server-ip>` with the server's IP address (use `localhost` if running on the same machine).

Example:
```bash
./ReKenshiClientService.exe 192.168.1.100 7777
```

2. Inject the plugin into Kenshi:

Use a DLL injector to inject `Re_Kenshi_Plugin.dll` into the `kenshi_x64.exe` process.

Recommended injectors:
- **Xenos Injector** (https://github.com/DarthTon/Xenos)
- **Process Hacker** (Built-in inject feature)
- **Extreme Injector**

Steps:
1. Launch Kenshi
2. Open your DLL injector
3. Select the `kenshi_x64.exe` process
4. Inject `Re_Kenshi_Plugin.dll`
5. You should see a message box confirming successful injection

## How It Works

### 1. Pattern Scanning

The C++ plugin uses the PatternCoordinator system to automatically scan and resolve Kenshi's game structures:

- **Player Character** - Position, health, name
- **World State** - Current day, time
- **Character List** - All NPCs and players

The PatternCoordinator does all the heavy lifting:
- Automatic pattern resolution with retry logic
- Smart caching (1000ms TTL by default)
- Auto-updates at 10 Hz
- RIP-relative address resolution

### 2. Data Synchronization

Every 100ms (10 Hz), the plugin:
1. Reads player character data from game memory
2. Sends JSON update to client service via Named Pipe IPC
3. Client service forwards to game server via TCP
4. Server broadcasts to all other connected clients
5. Each client receives remote player data
6. Client service sends remote players back to C++ plugin via IPC
7. Plugin stores remote player data (ready for rendering)

### 3. Message Format

**Player Update (Plugin → Client Service):**
```json
{
  "Type": "player_update",
  "Data": {
    "posX": 1234.5,
    "posY": 67.8,
    "posZ": 910.1,
    "health": 85.0,
    "isAlive": true
  }
}
```

**Remote Player (Client Service → Plugin):**
```json
{
  "Type": "remote_player",
  "PlayerId": "Player_Machine1_1234",
  "Data": {
    "posX": 5678.9,
    "posY": 12.3,
    "posZ": 456.7,
    "health": 92.0,
    "isAlive": true
  }
}
```

**Server Messages:**
```json
{
  "Type": "player_join",
  "PlayerId": "Player_Machine2_5678",
  "Data": { ... },
  "Timestamp": 1234567890123
}
```

## Debugging

### Plugin Logs

The C++ plugin writes logs to `ReKenshi.log` in the Kenshi directory.

Check this file if:
- Pattern scanning fails
- IPC connection fails
- Game crashes

### Client Service Logs

The client service outputs to console in real-time.

Look for:
- `✓ Connected to game server` - Successfully connected to server
- `✓ C++ plugin connected via IPC` - Plugin connected successfully
- `[SERVER] Player joined: ...` - Other players joining
- `[ERROR]` messages - Connection or communication errors

### Server Logs

The server outputs to console in real-time.

Look for:
- `[JOIN] Player '...' connected` - Client connections
- `[LEAVE] Player '...' disconnected` - Client disconnections
- `[ERROR]` messages - Network errors

### Common Issues

**Issue: Plugin fails to connect to client service**
- Make sure ReKenshiClientService.exe is running BEFORE injecting the plugin
- Check if Named Pipe `\\.\pipe\ReKenshi_IPC` is accessible (use Process Explorer)

**Issue: Client service can't connect to server**
- Check if server is running and listening on the correct port
- Verify firewall allows TCP connections on port 7777
- Try `telnet <server-ip> 7777` to test connectivity

**Issue: Plugin can't find game structures**
- The game may have updated, breaking the patterns
- Check `ReKenshi.log` for pattern resolution errors
- The PatternCoordinator will automatically retry pattern scanning

**Issue: Game crashes on injection**
- Make sure you're using the Release build (not Debug)
- Try a different DLL injector
- Check if antivirus is interfering

## Firewall Configuration

### Server Machine

Allow incoming TCP connections on port 7777:

**Windows:**
```bash
netsh advfirewall firewall add rule name="Re_Kenshi Server" dir=in action=allow protocol=TCP localport=7777
```

**Linux:**
```bash
sudo ufw allow 7777/tcp
```

### Client Machines

Allow outgoing TCP connections (usually enabled by default).

## Performance

- **CPU Usage:** ~1-2% per client (mostly idle)
- **Memory Usage:** ~20 MB for plugin, ~50 MB for client service, ~100 MB for server
- **Network Bandwidth:** ~1-2 KB/s per client (very lightweight)
- **Update Rate:** 10 Hz (configurable)

## Limitations

### Current Features
✅ Real-time position synchronization
✅ Health synchronization
✅ Multiple players support
✅ Automatic reconnection
✅ Pattern-based memory scanning (game update resistant)

### Not Yet Implemented
❌ Remote player rendering (models not spawned in-game yet)
❌ Combat synchronization
❌ Inventory synchronization
❌ NPC synchronization
❌ World state synchronization
❌ In-game UI/overlay
❌ Authentication system

## Next Steps

To add remote player rendering, you'll need to:

1. Hook into Kenshi's entity spawning system
2. Create visual representations for remote players
3. Update their positions based on synchronized data

The remote player data is already available via `ReKenshiPlugin::GetRemotePlayers()` in the C++ plugin.

## License

This is a fan-made mod for Kenshi. Use at your own risk.

## Support

For issues or questions, check the logs first:
- `ReKenshi.log` - Plugin logs
- Client service console output
- Server console output

## Credits

- **Pattern Scanning**: Uses signature-based memory scanning
- **IPC**: Windows Named Pipes
- **Networking**: TCP sockets with JSON protocol
- **Game**: Kenshi by Lo-Fi Games

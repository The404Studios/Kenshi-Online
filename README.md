# Kenshi Online

Multiplayer mod for Kenshi. Play with friends by connecting to a dedicated server.

## Quick Start

### Server (One person runs this)
```bash
KenshiOnline.exe --dedicated 7777
```

### Clients (Everyone else)
1. Start Kenshi
2. Run:
```bash
KenshiOnline.exe --connect <server-ip> 7777
```
3. Choose a spawn location
4. Play!

## How It Works

**Dedicated Server Mode:**
- Server tracks all players' positions
- Each player runs their own Kenshi + the client
- Server broadcasts positions so everyone sees each other
- Server does NOT need Kenshi running

**Host Mode (Alternative):**
- One player runs Kenshi and hosts
- Friends connect and control squads in the host's game
- Only ONE Kenshi instance runs (the host's)

## Building

```bash
cd Kenshi-Online
dotnet build
```

Requires .NET 8.0 SDK.

## Files

```
Kenshi-Online/
  Program.cs           - Entry point and menu
  DedicatedServer.cs   - Server and client code
  HostCoop.cs          - Host mode code
```

## Spawn Points

Default spawn locations include:
- The Hub (Border Zone) - Default
- Squin (Border Zone)
- Stack (Okran's Pride)
- Mongrel (Fog Islands)
- Shark (The Swamp)
- Way Station (Border Zone)
- Admag (Shek Kingdom)
- Heft (Great Desert)

Edit `spawnpoints.json` to add custom locations.

## Server Commands

```
/status     - Show server status
/players    - List connected players
/spawns     - List spawn points
/addspawn   - Add custom spawn point
/teleport   - Move a player
/kick       - Kick a player
/broadcast  - Message all players
/save       - Save world state
/quit       - Stop server
```

## Requirements

- Kenshi 1.0.64 (64-bit)
- Windows 10/11
- Run as Administrator
- Port 7777 open (or custom)

## License

MIT License

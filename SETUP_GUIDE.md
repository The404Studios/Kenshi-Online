# Kenshi Online - Complete Setup & Usage Guide

## 🎮 Overview

Kenshi Online is a complete multiplayer modification for Kenshi that allows you to play with friends. This remake includes:

- **Direct Game Integration** - Connects directly to Kenshi's game engine via memory injection
- **Player Spawning System** - Spawn anywhere in the world
- **Multiplayer Coordination** - Spawn with friends at the same location
- **Real Player Control** - Move, attack, interact, and play together
- **Server/Client Architecture** - Host or join games

## 📋 Prerequisites

### Required Software

1. **Kenshi** - Must be installed and working
2. **.NET 8.0 SDK** - For building the project
3. **Administrator Rights** - Required for memory injection
4. **Visual Studio 2022** (recommended) or VSCode with C# extension

### System Requirements

- Windows 10/11 (64-bit)
- 8GB+ RAM
- Kenshi v0.98.50 or later

## 🚀 Quick Start

### Step 1: Build the Project

```bash
cd Kenshi-Online/Kenshi-Online
dotnet build
```

### Step 2: Start Kenshi

1. Launch Kenshi
2. Load a save game or start a new game
3. Get to the main game screen (don't minimize!)

### Step 3: Run Kenshi Online

#### For Server (Host):

```bash
dotnet run
```

Select option **1** (Start Server)

#### For Client (Join):

```bash
dotnet run
```

Select option **2** (Start Client)

## 🎯 How to Use

### Hosting a Server

1. **Start Kenshi first!** - The server needs to connect to the game
2. Run the program and select "Start Server"
3. The program will:
   - Detect your Kenshi installation
   - Connect to the running Kenshi process
   - Initialize the game bridge
   - Start the multiplayer server

4. **Configuration Options:**
   - Server port (default: 5555)
   - Max players (default: 16)
   - Password (optional)

5. **Server Commands:**
   - `/status` - Show server status
   - `/players` - List active players
   - `/shutdown` - Stop the server

### Joining a Server

1. **Optional but recommended:** Start Kenshi first
2. Run the program and select "Start Client"
3. Enter server connection details:
   - Server address (e.g., localhost or IP)
   - Port (default: 5555)

4. **Login/Register:**
   - Choose Login (1) or Register (2)
   - Enter username and password

5. **Spawn Menu:**
   - **Spawn Solo** - Spawn by yourself
   - **Spawn with Friends** - Coordinate spawning with friends
   - **Select Spawn Location** - Choose where to spawn

### Spawning with Friends

1. All players join the server and reach the spawn menu
2. One player selects "Spawn with Friends"
3. Enter friend usernames (comma-separated): `friend1, friend2, friend3`
4. All players will receive a group spawn request
5. Each player readies up
6. When all are ready, everyone spawns together in a circle

### Available Spawn Locations

- **The Hub** - Safe trading hub (default)
- **Squin** - United Cities outpost
- **Sho-Battai** - Shek Kingdom capital
- **Heng** - Major United Cities city
- **Stack** - Neutral tech hunters base
- **Admag** - Holy Nation city
- **Bad Teeth** - Border zone outpost
- **Bark** - Swamp village
- **Stoat** - Desert village
- **World's End** - Northern outpost
- **Flats Lagoon** - Coastal city
- **Shark** - Swamp base
- **Mud Town** - Swamp settlement
- **Mongrel** - Fogmen territory outpost
- **Catun** - Nomad city
- **Spring** - Holy Nation village

## 🔧 Architecture

### New Components

#### 1. KenshiGameBridge (`Game/KenshiGameBridge.cs`)
- **Purpose:** Direct integration with Kenshi's game engine
- **Features:**
  - Process memory injection
  - Character spawning/despawning
  - Position updates
  - Game command execution (move, attack, interact)

#### 2. PlayerController (`Game/PlayerController.cs`)
- **Purpose:** High-level player control
- **Features:**
  - Player registration
  - Movement control
  - Combat control
  - Interaction control
  - Squad management

#### 3. SpawnManager (`Game/SpawnManager.cs`)
- **Purpose:** Handles all spawning logic
- **Features:**
  - Single player spawning
  - Group spawning with friends
  - Respawn system
  - Spawn location management

#### 4. GameStateManager (`Game/GameStateManager.cs`)
- **Purpose:** Central coordination of all game systems
- **Features:**
  - Game state synchronization
  - Player lifecycle management
  - Event broadcasting
  - State updates (20 Hz)

### How It Works

```
┌──────────────┐
│ Kenshi Game  │ ◄──── Memory Injection ──── KenshiGameBridge
└──────┬───────┘
       │
       │ Game State
       ▼
┌──────────────────┐
│ GameStateManager │ ◄──── Coordinates ──────► StateSynchronizer
└────────┬─────────┘
         │
         ├──► PlayerController ──► Move/Attack/Interact
         │
         └──► SpawnManager ──────► Spawn/Despawn/Groups
                  │
                  ▼
         ┌────────────────┐
         │ Multiplayer    │
         │ Server         │ ◄──── TCP ──────► Clients
         └────────────────┘
```

## ⚙️ Configuration

### Memory Addresses

Located in `KenshiGameBridge.cs` - Offsets class:

```csharp
// These are for Kenshi v0.98.50
// Update if using a different version!
public const long CHARACTER_LIST_BASE = 0x24C5A20;
public const long SPAWN_FUNCTION = 0x8B3C80;
public const long CAMERA_POSITION = 0x24E7C20;
// ... more addresses
```

**Important:** If Kenshi updates, these memory addresses will change!

### Network Configuration

Default ports:
- **Game Server:** 5555
- **WebUI:** 8080

Change in `EnhancedProgram.cs` or via command line prompts.

### Performance Tuning

In `GameStateManager.cs`:

```csharp
private const int UPDATE_RATE_MS = 50; // 20 Hz (faster = more responsive)
private const int POSITION_UPDATE_THRESHOLD_MS = 100; // Update frequency
private const float POSITION_CHANGE_THRESHOLD = 0.5f; // Sensitivity
```

## 🐛 Troubleshooting

### "Kenshi process not found"

**Solution:**
- Make sure Kenshi is running
- Make sure it's `kenshi_x64.exe` (not kenshi.exe)
- Load a save game before starting Kenshi Online

### "Failed to connect to Kenshi"

**Solution:**
- Run Kenshi Online as Administrator
- Check Windows Defender / Antivirus isn't blocking
- Make sure no other process is injecting into Kenshi

### "Failed to spawn player"

**Solution:**
- Check that GameBridge is connected
- Verify memory addresses match your Kenshi version
- Check server logs for errors

### Players not spawning together

**Solution:**
- Make sure all players are ready
- Check network connectivity
- Verify all players are on the same server

### Position desync

**Solution:**
- Reduce `POSITION_UPDATE_THRESHOLD_MS` for faster updates
- Check network latency
- Ensure both client and server are running

## 🔒 Security Notes

### Memory Injection

This mod uses memory injection to control Kenshi. Some antivirus software may flag this as suspicious.

**To allow:**
1. Add exception in Windows Defender
2. Add folder to antivirus whitelist
3. Or disable antivirus temporarily (not recommended)

### Administrator Rights

Required for:
- Opening Kenshi process with full access
- Allocating memory in target process
- Writing to process memory

### Online Play

- Use trusted servers only
- Default encryption is AES
- Passwords are hashed with PBKDF2

## 📝 Message Types

New message types for spawning and multiplayer:

```csharp
// Spawning
SpawnRequest
PlayerSpawned
PlayerDespawned
PlayerRespawn

// Group spawning
GroupSpawnRequest
GroupSpawnCreated
GroupSpawnReady
GroupSpawnCompleted

// Player events
PlayerJoined
PlayerLeft
PlayerStateUpdate

// Commands
MoveCommand
AttackCommand
FollowCommand
PickupCommand
InteractCommand

// Squads
SquadCreate
SquadJoin
SquadCommand
```

## 🎮 Gameplay Tips

### Playing with Friends

1. **Coordinate spawn location** - Agree on a location before connecting
2. **Use group spawn** - Ensures everyone spawns together
3. **Create a squad** - Makes it easier to coordinate
4. **Stay close** - Game synchronization works best when players are near

### Performance

- **Lower player count** for better performance
- **Adjust update rate** if experiencing lag
- **Use wired connections** when possible
- **Close other programs** to free memory

### Best Practices

1. **Save frequently** - Multiplayer is experimental
2. **Backup saves** before playing
3. **Use stable Kenshi version** - Avoid beta branches
4. **Report issues** on GitHub

## 🆘 Support

### Getting Help

1. Check this guide first
2. Look at server/client logs
3. Check GitHub Issues
4. Ask in Discord (if available)

### Reporting Bugs

Include:
- Kenshi version
- .NET version
- Error messages
- Steps to reproduce
- Server/client logs

## 📦 File Structure

```
Kenshi-Online/
├── Game/
│   ├── KenshiGameBridge.cs      - Game engine integration
│   ├── PlayerController.cs      - Player control
│   ├── SpawnManager.cs          - Spawning system
│   └── GameStateManager.cs      - Central coordinator
├── Networking/
│   ├── Server.cs                - TCP server
│   ├── Client.cs                - TCP client
│   ├── ServerExtensions.cs      - Server game integration
│   ├── ClientExtensions.cs      - Client game integration
│   └── StateSynchronizer.cs     - State sync
├── Data/
│   ├── PlayerData.cs            - Player data model
│   └── Position.cs              - Position & interpolation
├── Utility/
│   ├── MessageType.cs           - Message type constants
│   ├── GameMessage.cs           - Message protocol
│   └── Logger.cs                - Logging
├── EnhancedProgram.cs           - Main entry point
└── SETUP_GUIDE.md               - This file
```

## ⚡ Advanced Usage

### Custom Spawn Locations

Add new locations in `SpawnManager.cs`:

```csharp
SpawnLocations.Add("MyCity", new SpawnLocation(
    "My Custom City",
    x_coordinate,
    y_coordinate,
    z_coordinate,
    "Description"
));
```

### Custom Game Commands

Extend `PlayerController.cs` with new commands:

```csharp
public bool MyCustomCommand(string playerId, params object[] args)
{
    // Your code here
    return gameBridge.SendGameCommand(playerId, "custom", args);
}
```

### Event Handling

Subscribe to game events:

```csharp
gameStateManager.OnPlayerJoined += (playerId, playerData) =>
{
    Console.WriteLine($"Player {playerId} joined!");
    // Custom logic
};
```

## 🎯 Roadmap

### Planned Features

- [ ] NPC synchronization
- [ ] Base building sync
- [ ] Squad AI coordination
- [ ] Voice chat integration
- [ ] Steam Workshop support
- [ ] Web-based server browser

### Known Limitations

- Memory addresses are version-specific
- Max ~16 players recommended
- Some Kenshi features may not sync
- Requires administrator rights

## 📄 License

See LICENSE file in repository.

## 🙏 Credits

- Original Kenshi by Lo-Fi Games
- Community contributors
- You, for playing!

---

**Enjoy playing Kenshi with your friends!** 🎮🎉

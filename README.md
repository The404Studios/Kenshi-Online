# Kenshi Online

[![MIT License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Discord](https://img.shields.io/discord/962745762938572870?color=7289DA&label=Discord&logo=discord&logoColor=white)](https://discord.gg/W2K7GhmD)
[![.NET 8.0](https://img.shields.io/badge/.NET-8.0-purple.svg)](https://dotnet.microsoft.com/download/dotnet/8.0)

**Experimental multiplayer mod for Kenshi.**

## What This Is

Kenshi Online is a proof-of-concept multiplayer mod that enables 2-8 friends to play Kenshi together via memory injection and network synchronization.

**Current status: Experimental / In Development**

This is NOT a finished product. It is an active research project exploring how to add multiplayer to a game that was never designed for it.

## What Works

- Position synchronization between players
- Basic combat sync (health, damage)
- Server-authoritative state (anti-cheat foundation)
- DLL injection into Kenshi process
- Session hosting and joining
- Reconnection handling
- Server-owned save files

## What Does NOT Work (Yet)

- NPC AI synchronization (NPCs run locally, may diverge)
- Formation/squad commands
- In-game chat
- Full inventory sync
- Cross-zone multiplayer
- Modded content

## Honest Limitations

Kenshi was not designed for multiplayer. This mod works by:
1. Injecting into the game's memory
2. Reading and writing game state
3. Synchronizing state over a network

This is inherently fragile. Things that can break:
- **Kenshi updates** change memory offsets
- **Desync** when complex game events occur
- **NPC divergence** since AI is not synced
- **Crashes** from memory manipulation

## Requirements

- **Kenshi v1.0.64** (64-bit) - Other versions may not work
- **Windows 10/11** - The DLL injection only works on Windows
- **.NET 8.0 Runtime** - Required to run the launcher
- **Same Kenshi version** on all players
- **Port 5555** open for hosting (TCP)

## Quick Start

### Building from Source

```bash
# Clone the repository
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# Build everything
./build/build.sh        # Linux/Mac (builds C# only)
./build/build.ps1       # Windows (builds C# and C++ DLL)
```

### Hosting a Game

1. Launch Kenshi
2. Run `KenshiOnline.exe`
3. Select **Host**
4. Share your IP and port with friends
5. Wait for friends to connect

### Joining a Game

1. Launch Kenshi
2. Run `KenshiOnline.exe`
3. Select **Join**
4. Enter host's IP address
5. Connect and play

## Architecture

See [docs/MINIMAL_ARCHITECTURE.md](docs/MINIMAL_ARCHITECTURE.md) for the complete technical design.

### Core Principles

1. **Server is authoritative** - The server owns all gameplay state
2. **Sync less, validate more** - We only sync what's necessary
3. **Stability over features** - Working features beat broken ambitious ones
4. **Explicit contracts** - Every sync operation has defined behavior

### What Gets Synced

| Data | Sync Rate | Authority |
|------|-----------|-----------|
| Position | 20 Hz | Server |
| Health | On change | Server |
| Combat | 30 Hz | Server |
| Inventory | On change | Server |
| NPCs | 10 Hz (hints only) | Local |

### What Doesn't Get Synced

- NPC AI decisions
- Pathfinding
- Complex game events
- Modded content

## Project Structure

```
Kenshi-Online/
├── Kenshi-Online/          # C# multiplayer infrastructure
│   ├── Core/               # Data structures, state management
│   ├── Networking/         # Client, server, authority system
│   ├── Game/               # Memory injection, game bridge
│   └── Utility/            # Helpers, logging, encryption
├── KenshiOnlineMod/        # C++ DLL for game injection
├── build/                  # Build scripts
├── docs/                   # Documentation
└── offsets/                # Memory offset tables
```

## Contributing

This project needs help with:

1. **Memory offset discovery** for new Kenshi versions
2. **Desync debugging** and state reconciliation
3. **Testing** across different configurations
4. **Documentation** and user guides

Before contributing, please read [docs/MINIMAL_ARCHITECTURE.md](docs/MINIMAL_ARCHITECTURE.md) to understand the design constraints.

### Development Rules

- **No feature creep** - We ship what works, not what's cool
- **Server authority** - Clients never own gameplay state
- **Test on real hardware** - Memory injection is environment-sensitive
- **Document your changes** - Others need to understand your work

## Known Issues

1. **Desync after long sessions** - Full resync may be needed
2. **NPC behavior diverges** - NPCs are hints, not synchronized
3. **Combat timing varies** - Network latency affects hit registration
4. **Memory offsets break** - Kenshi updates may require new offsets

## FAQ

**Q: Why does my position keep snapping?**
A: The server is correcting your position. This happens when the client moves faster than allowed (latency) or when there's a desync.

**Q: Why do NPCs act differently on each player's screen?**
A: NPC AI runs locally. We sync position/health as hints, but each client runs its own AI. This is a fundamental limitation.

**Q: Will this work with mods?**
A: No. Mod sync is not implemented. All players must use vanilla Kenshi.

**Q: Can I play with more than 8 players?**
A: The architecture supports more, but it's untested. Expect performance issues.

**Q: The game crashes when I inject the DLL.**
A: Try running as administrator. Check that your antivirus isn't blocking the DLL. Ensure you're using Kenshi v1.0.64.

## Disclaimer

This is an unofficial mod. It is not affiliated with or endorsed by Lo-Fi Games.

Memory injection mods can cause crashes and save corruption. **Back up your saves before using this mod.**

Use at your own risk.

## License

MIT License - See [LICENSE](LICENSE) for details.

## Contact

- **Discord**: [Join our server](https://discord.gg/W2K7GhmD)
- **GitHub Issues**: [Report bugs](https://github.com/The404Studios/Kenshi-Online/issues)
- **Email**: the404studios@gmail.com

---

*Developed by The404Studios*

*Kenshi is a property of Lo-Fi Games. This modification is unofficial and not affiliated with Lo-Fi Games.*

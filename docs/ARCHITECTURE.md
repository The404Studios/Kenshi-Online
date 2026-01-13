# Kenshi Online Architecture

## Overview

Kenshi Online is a multiplayer mod for Kenshi that enables cooperative and competitive gameplay through memory injection and network synchronization. This document explains the system architecture, design decisions, and key constraints.

## Core Philosophy

**Stability > Feature Count**

Kenshi was not designed for multiplayer. This mod works by:
1. Injecting into the game's memory space
2. Reading and writing game state
3. Synchronizing state across a network

This requires being **authoritative and conservative**. Every sync operation has potential to corrupt game state.

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        KENSHI GAME PROCESS                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                   KenshiGameBridge                       │    │
│  │  - Memory injection via P/Invoke                         │    │
│  │  - Reads player position, stats, inventory               │    │
│  │  - Writes synchronized state from network                │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       CLIENT APPLICATION                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ GameState   │  │ Enhanced    │  │ ClientSaveMirror        │  │
│  │ Manager     │◄─┤ Client      │◄─┤ (Read-only save view)   │  │
│  │ (20 Hz)     │  │ (TCP)       │  │                         │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                    Encrypted TCP (Port 5555)
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       SERVER APPLICATION                         │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                    EnhancedServer                        │    │
│  │  - Accepts TCP connections                               │    │
│  │  - JWT authentication                                    │    │
│  │  - Message routing                                       │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                   │
│         ┌────────────────────┼────────────────────┐              │
│         ▼                    ▼                    ▼              │
│  ┌─────────────┐  ┌─────────────────┐  ┌─────────────────────┐  │
│  │ Authority   │  │ State           │  │ ServerSaveManager   │  │
│  │ Manager     │  │ Replicator      │  │ (Source of Truth)   │  │
│  └─────────────┘  └─────────────────┘  └─────────────────────┘  │
│         │                    │                    │              │
│         ▼                    ▼                    ▼              │
│  ┌─────────────┐  ┌─────────────────┐  ┌─────────────────────┐  │
│  │ Combat      │  │ NPC             │  │ Interest            │  │
│  │ Synchronizer│  │ Synchronizer    │  │ Manager             │  │
│  │ (30 Hz)     │  │ (10 Hz)         │  │                     │  │
│  └─────────────┘  └─────────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Authority Model

The server is authoritative for all gameplay-critical systems. This prevents cheating.

| System       | Authority | Rationale |
|--------------|-----------|-----------|
| Position     | Server    | Prevents speed hacks, teleportation |
| Combat       | Server    | Prevents damage manipulation, invincibility |
| Inventory    | Server    | Prevents item duplication |
| AI/NPCs      | Server    | Consistent behavior across clients |
| Trading      | Server    | Prevents trade exploits |
| Building     | Server    | Validates placement rules |
| Quests       | Server    | Prevents progress manipulation |
| Faction      | Server    | Prevents reputation exploits |
| World Events | Server    | Single source of truth |
| Animation    | Client    | Cosmetic only, client-predicted |

### Client Input Flow

```
1. Client captures player intent (move, attack, pickup)
2. Client sends REQUEST to server
3. Server VALIDATES request against rules
4. Server APPLIES changes to authoritative state
5. Server BROADCASTS result to all relevant clients
6. Clients UPDATE their local state from server
```

Clients never directly modify authoritative state. They request changes.

## State Replication Tiers

Not all data syncs the same way. Different data has different requirements.

### Tier 0: Transient (Position, Rotation, Velocity)

```
Rate:           20 Hz (50ms)
Persistence:    Never saved
Acknowledgment: None required
Conflicts:      Server always wins
```

- High frequency, low latency
- No retry - use latest value
- Delta compression for bandwidth

### Tier 1: Events (Combat Actions, Pickups, Abilities)

```
Rate:           30 Hz (for combat)
Persistence:    Side effects saved (damage, items)
Acknowledgment: Required
Conflicts:      Reject conflicting events
```

- Must arrive in order
- Retry on failure (3 attempts)
- Deterministic resolution

### Tier 2: Persistent (Inventory, Stats, Relations)

```
Rate:           1 Hz (changes are rare)
Persistence:    Always saved
Acknowledgment: Required
Conflicts:      Server wins
```

- Critical data
- Retry on failure (5 attempts)
- Full validation before apply

## Save Persistence Contract

**Rule: Server owns ALL saves. Clients are read-only mirrors.**

```
┌──────────────┐                    ┌──────────────┐
│    SERVER    │                    │    CLIENT    │
│              │                    │              │
│  PlayerSave  │ ───── sync ─────► │ SaveMirror   │
│  WorldSave   │                    │ (read-only)  │
│  Backups     │                    │              │
└──────────────┘                    └──────────────┘
```

### Why Server-Owned Saves?

1. **Single Source of Truth** - No conflicting game states
2. **Anti-Cheat** - Clients cannot edit save files
3. **Recovery** - Server can restore any client's state
4. **Consistency** - All players see same world

### Save Structure

```
saves/
├── players/
│   ├── player1.json      # Individual player data
│   └── player2.json
├── worlds/
│   └── world1.json       # Shared world state
└── backups/
    └── player1_123.json  # Versioned backups
```

## Network Protocol

### Transport
- TCP/IP with custom encryption
- Port 5555 (default)
- 16KB max buffer for file transfers

### Message Format
```json
{
  "Type": "position",
  "PlayerId": "abc123",
  "SessionId": "jwt-token",
  "Timestamp": 1699900000000,
  "MessageId": "guid",
  "SequenceNumber": 42,
  "Data": { ... }
}
```

### Message Types (47+ types)
- Authentication: Login, Register, Auth
- Movement: Position, MoveCommand
- Combat: CombatAction, Damage
- Inventory: InventoryDetailed, Equipment
- Social: Chat, FriendRequest, TradeRequest
- World: Building, Quest, WorldEvent

## Anti-Cheat Measures

### Input Validation
- **Speed check**: velocity > 15 units/sec rejected
- **Teleport check**: distance/time > 20 rejected
- **Cooldown enforcement**: Server tracks ability cooldowns
- **Range validation**: Combat requires target in range

### Session Security
- JWT tokens for authentication
- Session timeout on idle
- Token refresh on reconnect

### State Validation
- Inventory changes validated (has item, in range)
- Combat damage validated (weapon stats, armor)
- Building placement validated (resources, location)

## Performance Considerations

### Tick Rates
- Game state: 20 Hz
- Combat: 30 Hz
- NPCs: 10 Hz
- Persistent sync: 1 Hz

### Bandwidth Optimization
- Delta compression (only send changes)
- Interest management (only send nearby entities)
- Batching (combine small messages)
- Snapshot vs Delta strategy (full sync every 3s)

### Scalability Limits
- ~50 NPCs per update cycle
- 5km interest radius
- 100 state versions in history

## Key Files

| File | Purpose |
|------|---------|
| `Networking/Server.cs` | TCP listener, auth, message routing |
| `Networking/Client.cs` | Connection, login, message handling |
| `Networking/StateSynchronizer.cs` | Delta compression, interest management |
| `Networking/CombatSynchronizer.cs` | Deterministic combat resolution |
| `Networking/Authority/AuthoritySystem.cs` | Authority rules, validation |
| `Networking/Authority/StateReplicationTier.cs` | Tier configuration, replication |
| `Networking/Authority/SavePersistence.cs` | Server save management |
| `Game/GameStateManager.cs` | 20Hz update loop, player management |
| `Game/KenshiGameBridge.cs` | Memory injection interface |

## Design Constraints

### What Works
- Position synchronization
- Combat with server validation
- Inventory with server authority
- Basic NPC awareness

### What's Hard
- Complex AI synchronization
- Building placement edge cases
- Large squad coordination
- Cross-zone persistence

### What Doesn't Work (Yet)
- Real-time strategy commands
- Complex dialog trees
- Modded content sync
- Save file migration

## Extending the System

When adding new features:

1. **Decide authority** - Server or client?
2. **Pick replication tier** - Transient, Event, or Persistent?
3. **Define validation** - What makes input valid?
4. **Handle conflicts** - What if clients disagree?
5. **Test failure cases** - Network drops, desync, lag

## Debugging

### Common Issues
- Desync: Check authority validation logs
- Lag: Check tick rate and delta sizes
- Crashes: Check memory bridge null checks
- Auth failures: Check JWT token expiry

### Logging
- Server logs to console and file
- Client logs to file
- Enable debug mode for verbose output

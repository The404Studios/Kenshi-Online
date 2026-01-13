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

---

## Ownership Contract (Explicit)

This section defines who owns what. No ambiguity. No exceptions.

### Server Owns

| Resource | Why |
|----------|-----|
| **World State** | Single source of truth for all entities |
| **NPC Intent** | AI decisions must be consistent across clients |
| **Combat Resolution** | Damage, hits, deaths determined server-side |
| **Saves** | All persistent data lives on server |
| **Tick Clock** | Server defines simulation time |
| **Faction Reputation** | Prevents manipulation exploits |
| **Item Spawns** | Server controls what exists in world |
| **Building Placement** | Validates resources and location |

### Client Owns

| Resource | Why |
|----------|-----|
| **Input** | Mouse, keyboard, controller |
| **Rendering** | What gets drawn locally |
| **Prediction** | Local interpolation for smoothness |
| **Animation Selection** | Cosmetic, no gameplay impact |
| **UI State** | Menus, overlays, settings |

### Simultaneous Action Resolution

When two clients act at the same time:

1. **First-to-server wins** - Lower latency advantage
2. **Server tick ordering** - Actions ordered by tick received
3. **Deterministic tiebreaker** - Same tick? Use player ID alphabetically

### Disputed State

If client and server disagree:

```
Client state ≠ Server state
            ↓
Server state ALWAYS wins
            ↓
Client receives correction
            ↓
Client discards local state
```

No negotiation. No merge. Server is truth.

---

## Deterministic Tick System

### Contract

```
Server Tick Rate: 20 Hz (50ms per tick)
Combat Tick Rate: 30 Hz (33ms per tick)

Clients:
- Receive tick ID with every state update
- Interpolate BETWEEN ticks only
- NEVER advance "truth time"
- Report their current tick for drift detection
```

### Drift Handling

| Drift | Action |
|-------|--------|
| ≤ 5 ticks | Normal - within tolerance |
| 6-10 ticks | Warning - increase interpolation buffer |
| > 10 ticks | Force resync - client receives full snapshot |

### Tick Snapshot

Every tick, server stores:
- Tick ID
- Timestamp
- All entity positions
- All entity states

Used for:
- Reconciliation
- Replay
- Desync debugging

---

## Trust Boundaries

### Principle

> Injection + networking without trust rules = cheat engine with a socket

### Input Validation (Server-Side)

| Input | Validation | Rejection |
|-------|------------|-----------|
| Position delta | ≤ 3m per tick | Clamp or reject |
| Speed | ≤ 15 m/s | Reject + log |
| Attack range | ≤ 5m melee, ≤ 100m ranged | Reject |
| Attack rate | ≤ 3 per second | Rate limit |
| Inventory change | ≤ 10 per second | Rate limit |
| Chat | ≤ 30 per minute | Mute |

### Violation Escalation

| Violations | Action |
|------------|--------|
| 1-3 | Warning logged |
| 4-10 | Temporary action restriction |
| 11-25 | Kick from server |
| 25+ | Ban consideration |

### What We Don't Need (Yet)

- Kernel-level anti-cheat
- Client binary validation
- Hardware fingerprinting

What we DO need:
- **Anti-nonsense** - Reject impossible inputs
- **Rate limiting** - Prevent spam
- **Logging** - Know what happened

---

## Session Recovery

### Disconnect Flow

```
1. Client disconnects (intentional or crash)
2. Server preserves session for 5 minutes
3. After 3 seconds, AI takes control of character
4. Character becomes invulnerable for 5 seconds
5. AI behavior: Defensive (block, don't attack)

On Reconnect:
1. Client authenticates
2. Server sends full state snapshot
3. Client DISCARDS all local state
4. Client resumes from server state
5. Control restored to player
```

### AI Takeover Behaviors

| Situation | AI Behavior |
|-----------|-------------|
| Idle | Stand still |
| In combat | Defensive (block only) |
| Being attacked | Flee to safety |
| In building | Stay put |

### Inventory Lock

While disconnected:
- Inventory locked (no drops, no trades)
- Prevents exploitation of disconnect

---

## Graceful Degradation

### Latency Spikes

| Latency | Adaptation |
|---------|------------|
| < 100ms | Normal operation |
| 100-200ms | Double interpolation buffer |
| 200-500ms | Reduce update rate, show lag indicator |
| > 500ms | Disable non-essential sync, prepare for disconnect |

### Packet Loss

| Loss Rate | Action |
|-----------|--------|
| < 5% | Normal, rely on redundancy |
| 5-15% | Request retransmission of critical data |
| > 15% | Fall back to periodic snapshots |

### Server Overload

| CPU | Action |
|-----|--------|
| < 70% | Normal operation |
| 70-90% | Reduce NPC sync scope |
| > 90% | Pause non-essential systems, warn admin |

---

## Diagnostics

### What Gets Logged

| Event | Logged Data |
|-------|-------------|
| Tick | Tick ID, player count, entity count |
| Position | Player ID, coordinates, validated, corrected |
| Combat | Attacker, target, action, damage, validated |
| Violation | Player ID, type, details |
| Connect/Disconnect | Player ID, reason |
| Desync | Component, client value, server value |

### Ring Buffer

- Last 10,000 events kept in memory
- Flushed to disk every 5 seconds
- Log rotation at 100MB

### Replay Capability

Events are logged in order with:
- Event ID (monotonic)
- Server tick
- Timestamp

This enables:
- Session replay from logs
- Desync diff tool
- Post-mortem debugging

---

## File Reference (Updated)

| File | Purpose |
|------|---------|
| `Authority/AuthoritySystem.cs` | Ownership and validation rules |
| `Authority/StateReplicationTier.cs` | Three-tier replication config |
| `Authority/SavePersistence.cs` | Server save management |
| `Authority/ServerContext.cs` | Central integration point |
| `Authority/ServerTickSystem.cs` | Deterministic tick clock |
| `Authority/TrustBoundary.cs` | Input validation and rate limiting |
| `Authority/SessionRecovery.cs` | Reconnect and AI takeover |
| `Authority/DiagnosticsLogger.cs` | Structured logging and replay |

---

## Hard Truths

1. **Kenshi was not designed for multiplayer** - We're working against the grain
2. **Injection + sync = fragile** - Be authoritative and conservative
3. **"Just sync more data" makes things worse** - Sync less, validate more
4. **Stability > feature count** - A working feature beats ten broken ones
5. **Without deterministic ticking, "it works" becomes "it randomly breaks"**

This architecture makes multiplayer work - but only if treated as infrastructure, not a mod.

# Kenshi Online - Minimal Shippable Architecture

## Hard Truth

Kenshi was never designed for multiplayer. This mod **cannot** make it a perfect multiplayer game.

What we CAN ship:
- **Cooperative play for 2-8 friends** (not MMO)
- **Position + combat sync** (not perfect AI sync)
- **Server-owned saves** (not Kenshi's native save format)
- **Basic trading** (not a full economy)

What we are CUTTING:
- Perfect NPC AI synchronization (NPCs are hints, not authoritative)
- Formation/squad commands (too complex for v1)
- Cross-zone real-time sync (players must be in same area)
- Modded content sync (vanilla Kenshi only)

---

## Layer 1: Build Contract

### Requirement
Anyone must be able to build from clean clone with ONE command.

### Files
```
/build/
  build.ps1           # Windows build script
  build.sh            # Linux/Mac build script
  verify-build.ps1    # Post-build verification
```

### Build Steps
```bash
# One command does everything
./build/build.sh        # Linux/Mac
./build/build.ps1       # Windows (PowerShell)

# Output
./dist/
  KenshiOnline.exe      # Client/Server executable
  KenshiOnlineMod.dll   # Native injection DLL
  offsets.json          # Memory offset table
  README.txt            # Quick start guide
```

### Build Verification
After build, script runs:
1. Compile check (0 errors, 0 warnings)
2. Binary exists check
3. DLL architecture check (x64)
4. Offset file present check

---

## Layer 2: Deterministic State Model

### The Core Problem

Kenshi state is:
- Distributed across memory
- Non-deterministic (AI makes random choices)
- Not designed for serialization

### Our Solution: Simplified Sync Model

We do NOT sync all Kenshi state. We sync **only what multiplayer needs**.

### WorldTick (Server Source of Truth)

```csharp
struct WorldTick
{
    ulong TickId;                    // Monotonic tick counter
    long Timestamp;                  // Unix milliseconds
    List<EntityState> Entities;      // All synced entities
    List<EntityDelta> Deltas;        // Changes since last tick
    string WorldHash;                // Integrity check
}
```

### EntityState (Minimal Sync Unit)

```csharp
struct EntityState
{
    string EntityId;                 // Unique identifier
    EntityType Type;                 // Player, NPC, Item, Building

    // Position (always synced)
    float X, Y, Z;
    float RotationY;

    // Health (always synced)
    float Health;
    float MaxHealth;

    // Owner (who controls this entity)
    string OwnerId;                  // PlayerId or "SERVER"

    // Type-specific data
    Dictionary<string, object> Data; // Type-dependent payload
}
```

### EntityDelta (Change Only)

```csharp
struct EntityDelta
{
    string EntityId;
    DeltaType Type;                  // Created, Updated, Destroyed
    Dictionary<string, object> Changes;  // Only changed fields
    ulong SourceTick;                // When change occurred
}

enum DeltaType
{
    Created,    // New entity
    Updated,    // Changed fields
    Destroyed,  // Removed entity
    Authority   // Ownership changed
}
```

### What We Sync (Scope Cut)

| Entity Type | Synced Fields | NOT Synced |
|-------------|--------------|------------|
| Player | Position, Health, Inventory, Equipment, Faction | AI state, animation details |
| NPC | Position, Health, Faction, IsHostile | AI decisions, pathfinding |
| Item | Position, Owner, Container | Exact stack details |
| Building | Position, Health, Owner | Construction progress |

### Why NPCs Are "Hints"

NPCs run locally on each client. We sync:
- Position (server corrects if diverged)
- Health (server is truth)
- Faction/hostility (affects gameplay)

We do NOT sync:
- AI decisions (too expensive, too fragile)
- Pathfinding (recalculated locally)
- Dialog state (not multiplayer-relevant)

**Result**: NPCs may behave slightly differently per client, but the OUTCOME (damage, death, faction) is consistent.

---

## Layer 3: Authority Map

### Rule: Server is ALWAYS truth for gameplay. No exceptions.

### Authority Table

| System | Authority | Client Can | Server Does |
|--------|-----------|-----------|-------------|
| **Position** | Server | REQUEST move | Validate speed, apply |
| **Combat** | Server | REQUEST attack | Validate range, calculate damage |
| **Inventory** | Server | REQUEST pickup/drop | Validate existence, apply |
| **Health** | Server | Nothing | Calculate all damage/healing |
| **Trading** | Server | REQUEST trade | Lock items, execute atomically |
| **Spawning** | Server | REQUEST spawn | Validate, place in world |
| **Faction** | Server | Nothing | Manage all relations |
| **Saves** | Server | REQUEST save | Own all persistent data |

### What Client "Owns"

| System | Why Client |
|--------|-----------|
| Input | Mouse/keyboard are local |
| Rendering | What you see is local |
| Prediction | Smooth movement before server confirms |
| UI | Menus, overlays, settings |
| Animation Selection | Cosmetic only |

### Conflict Resolution

```
RULE 1: Server always wins
RULE 2: No negotiation
RULE 3: No merge
```

When client and server disagree:
1. Server sends correction
2. Client discards local state
3. Client applies server state
4. No rollback (too complex for v1)

---

## Layer 4: Session System

### Minimum Viable Session

```csharp
struct Session
{
    string SessionId;           // Unique session identifier
    string HostId;              // Player who owns session
    string WorldName;           // Descriptive name
    string WorldHash;           // Hash of save file
    string KenshiVersion;       // Must match to join
    string ModVersion;          // Must match to join
    List<string> PlayerIds;     // Connected players
    SessionState State;         // Lobby, Playing, Paused
    long CreatedAt;
    long LastActivity;
}

enum SessionState
{
    Lobby,      // Waiting for players
    Playing,    // Game active
    Paused,     // Host paused
    Closed      // Session ended
}
```

### Player Identity

```csharp
struct PlayerIdentity
{
    string PlayerId;            // Unique (UUID)
    string DisplayName;         // Shown to others
    string AuthToken;           // JWT for session
    string KenshiVersion;
    string ModVersion;
}
```

### Join Flow

```
1. Client: SendJoinRequest(SessionId, PlayerIdentity)
2. Server: ValidateVersion(KenshiVersion, ModVersion)
3. Server: ValidateWorldHash(ClientHash, ServerHash)
4. If mismatch → REJECT with reason
5. Server: AssignSpawnPoint()
6. Server: SendFullWorldSnapshot()
7. Client: ApplySnapshot()
8. Server: BroadcastPlayerJoined()
```

### Reconnect Flow

```
1. Client disconnects (crash, network)
2. Server: MarkPlayerDisconnected(PlayerId)
3. Server: StartGracePeriod(5 minutes)
4. Server: SetCharacterInvulnerable(5 seconds)
5. If reconnect within grace:
   - Client: SendReconnect(PlayerId, AuthToken)
   - Server: ValidateToken()
   - Server: SendFullSnapshot()
   - Server: RestoreControl()
6. If grace expires:
   - Server: DespawnCharacter()
   - Server: SavePlayerData()
```

---

## Layer 5: Save Canonicalization

### Rule: Server owns ALL saves. Period.

### Save Structure

```
saves/
├── sessions/
│   └── {SessionId}/
│       ├── world.json           # World state
│       ├── players/
│       │   ├── {PlayerId}.json  # Player data
│       │   └── ...
│       └── meta.json            # Session metadata
└── backups/
    └── {SessionId}/
        └── {Timestamp}/
            └── ...              # Full backup
```

### World Save Format

```csharp
struct WorldSave
{
    string SessionId;
    ulong LastTick;
    long SavedAt;
    string WorldHash;

    List<EntityState> Entities;
    Dictionary<string, FactionState> Factions;
    List<BuildingState> Buildings;

    // NOT saved: transient NPC states, pathfinding, AI
}
```

### Player Save Format

```csharp
struct PlayerSave
{
    string PlayerId;
    string DisplayName;

    // Character state
    float X, Y, Z;
    float Health, MaxHealth;
    float Hunger, Blood;

    // Inventory (authoritative)
    List<ItemStack> Inventory;
    Dictionary<string, string> Equipment; // Slot -> ItemId

    // Progression
    Dictionary<string, float> Skills;
    int Money;

    // Relations
    string FactionId;
    Dictionary<string, int> FactionStanding;

    // Meta
    long LastPlayed;
    int TotalPlayTime;
}
```

### Save Triggers

| Trigger | Action |
|---------|--------|
| Every 60 seconds | Auto-save (configurable) |
| Player disconnect | Save player data |
| Session close | Full save |
| Player request | Save if host |

### Hash Verification

Before join, client sends hash of:
- Kenshi version
- Mod version
- (Optional) Base save hash

If mismatch:
- Reject join
- Send clear error message

---

## Layer 6: Atomic Trading

### Trade Protocol

```csharp
struct TradeSession
{
    string TradeId;
    string InitiatorId;
    string TargetId;

    List<ItemStack> InitiatorOffer;
    List<ItemStack> TargetOffer;
    int InitiatorMoney;
    int TargetMoney;

    TradeState State;
    long StartedAt;
    long ExpiresAt;
}

enum TradeState
{
    Proposed,       // Initiator sent offer
    Negotiating,    // Both modifying
    InitiatorReady, // Initiator locked in
    TargetReady,    // Target locked in
    BothReady,      // Ready to execute
    Executing,      // Server processing
    Completed,      // Success
    Cancelled,      // Aborted
    Failed          // Error occurred
}
```

### Trade Flow (Atomic)

```
1. Initiator: ProposeTradeRequest(TargetId)
2. Server: CreateTradeSession(), LockNothing
3. Target: AcceptTradeRequest(TradeId)
4. Both: ModifyOffer(Items, Money)
5. Initiator: ConfirmOffer()
6. Target: ConfirmOffer()
7. Server: State = BothReady
8. Server: BEGIN TRANSACTION
   a. Validate InitiatorHasItems
   b. Validate TargetHasItems
   c. Validate InitiatorHasMoney
   d. Validate TargetHasMoney
   e. If ANY fail → ABORT, State = Failed
   f. RemoveFromInitiator(Items, Money)
   g. RemoveFromTarget(Items, Money)
   h. AddToInitiator(TargetOffer, TargetMoney)
   i. AddToTarget(InitiatorOffer, InitiatorMoney)
   j. COMMIT
9. Server: State = Completed
10. Server: BroadcastTradeComplete()
```

### Failure Handling

| Failure | Action |
|---------|--------|
| Player disconnects during trade | Cancel trade, no item loss |
| Validation fails | Cancel trade, send reason |
| Server crash during execute | On restart, check transaction log, rollback if incomplete |

### Anti-Dupe Protection

- Items locked during BothReady state
- Locked items cannot be dropped, traded elsewhere, or used
- Timeout: 30 seconds in BothReady state → auto-cancel

---

## Layer 7: Error System

### Error Categories

```csharp
enum ErrorCategory
{
    Network,        // Connection issues
    Auth,           // Authentication failures
    Validation,     // Invalid requests
    State,          // State conflicts
    Version,        // Version mismatch
    Resource,       // Missing resources
    Internal        // Server bugs
}
```

### Structured Error

```csharp
struct GameError
{
    string ErrorId;             // Unique error identifier
    ErrorCategory Category;
    string Code;                // Machine-readable (e.g., "VERSION_MISMATCH")
    string Message;             // Human-readable
    string Details;             // Technical details (for logs)
    bool Recoverable;           // Can user retry?
    string RecoveryAction;      // What to do (e.g., "Update mod to v1.2")
}
```

### Error Codes

| Code | Category | Message | Recovery |
|------|----------|---------|----------|
| `CONN_REFUSED` | Network | Cannot connect to server | Check IP and port |
| `CONN_TIMEOUT` | Network | Connection timed out | Check network, retry |
| `AUTH_INVALID` | Auth | Invalid credentials | Re-enter password |
| `AUTH_EXPIRED` | Auth | Session expired | Reconnect |
| `VERSION_KENSHI` | Version | Kenshi version mismatch | Update Kenshi |
| `VERSION_MOD` | Version | Mod version mismatch | Update mod |
| `WORLD_MISMATCH` | State | World state mismatch | Host must resync |
| `TRADE_CANCELLED` | State | Trade was cancelled | Start new trade |
| `ITEM_NOT_FOUND` | Validation | Item does not exist | Refresh inventory |
| `SPEED_VIOLATION` | Validation | Movement too fast | Position corrected |

### User-Facing Messages

Every error shown to user must have:
1. What happened (clear, non-technical)
2. Why it happened (if known)
3. What to do next (actionable)

Example:
```
[Connection Failed]
Could not connect to the server.

Reason: The server may be offline or your network is blocking the connection.

Try:
1. Check that the server is running
2. Verify the IP address and port (default: 5555)
3. Check your firewall settings
```

---

## Layer 8: Version Compatibility

### Version Contract

```csharp
struct VersionInfo
{
    string KenshiVersion;       // e.g., "1.0.64"
    string ModVersion;          // e.g., "0.5.0"
    string ProtocolVersion;     // e.g., "1"
    string OffsetTableVersion;  // e.g., "2024-01-15"
}
```

### Compatibility Check (On Connect)

```
1. Client sends VersionInfo
2. Server compares:
   a. ProtocolVersion must match EXACTLY
   b. ModVersion must match MAJOR.MINOR (patch can differ)
   c. KenshiVersion must be compatible (see offset table)
3. If incompatible → Reject with specific reason
```

### Offset Table

```json
{
  "version": "2024-01-15",
  "kenshi_versions": {
    "1.0.64": {
      "supported": true,
      "offsets": {
        "player_list": "0x24C5A20",
        "world_instance": "0x24D8F40",
        "spawn_function": "0x8B3C80"
      }
    },
    "1.0.65": {
      "supported": false,
      "message": "Not yet supported. Use 1.0.64."
    }
  }
}
```

### Runtime Validation

On game attach:
1. Read Kenshi.exe version from PE header
2. Look up in offset table
3. If unsupported → Show error, do not inject
4. If supported → Use version-specific offsets

---

## Layer 9: Trust Boundary

### Input Validation Rules

| Input | Limit | Action on Violation |
|-------|-------|-------------------|
| Position delta | ≤ 3m per tick | Clamp to max |
| Speed | ≤ 15 m/s | Reject, log |
| Attack range | ≤ 5m melee | Reject |
| Attack rate | ≤ 3/sec | Rate limit |
| Inventory ops | ≤ 10/sec | Rate limit |
| Chat messages | ≤ 30/min | Mute |

### Violation Tracking

```csharp
struct ViolationRecord
{
    string PlayerId;
    string ViolationType;
    int Count;
    long LastOccurrence;
    bool Muted;
    bool Kicked;
}
```

### Escalation

| Violations | Action |
|-----------|--------|
| 1-3 | Log only |
| 4-10 | Warn player |
| 11-25 | Temporary restriction |
| 25+ | Kick, consider ban |

### What We Don't Need (v1)

- Kernel anti-cheat
- Client binary validation
- Hardware fingerprinting

What we DO need:
- Reject impossible inputs
- Rate limit spam
- Log everything

---

## Layer 10: Scope Cuts (Critical)

### IN SCOPE (v1)

| Feature | Status |
|---------|--------|
| 2-8 player co-op | Core feature |
| Position sync | Core feature |
| Combat sync | Core feature |
| Basic inventory sync | Core feature |
| Server-owned saves | Core feature |
| Basic trading | Core feature |
| Session join/leave | Core feature |
| Reconnect handling | Core feature |
| Error messages | Core feature |
| Version checking | Core feature |

### OUT OF SCOPE (v1)

| Feature | Reason | Future? |
|---------|--------|---------|
| 16+ players | Performance untested | v2 |
| NPC AI sync | Too complex, too fragile | Maybe |
| Formation commands | Requires AI sync | No |
| Modded content | No mod enumeration | v2 |
| Cross-zone sync | Scope explosion | No |
| Chat system | Not essential | v1.1 |
| Friend lists | Not essential | v1.1 |
| Server browser | Not essential | v1.1 |
| Achievements | Not essential | No |

### Feature Freeze Rule

Before adding ANY feature, ask:
1. Is it required for basic co-op?
2. Does it affect stability?
3. Can it ship without this?

If answer to #3 is "yes" → Cut it.

---

## Summary: What Must Exist

### Files to Create

```
/build/
  build.sh
  build.ps1
  verify-build.ps1

/Kenshi-Online/Core/
  WorldTick.cs
  EntityState.cs
  EntityDelta.cs
  Session.cs
  PlayerIdentity.cs
  TradeSession.cs
  GameError.cs
  VersionInfo.cs

/Kenshi-Online/Networking/Authority/
  ConflictResolver.cs       # First-to-server wins

/offsets/
  offset-table.json         # Version-specific offsets
```

### Contracts to Enforce

1. **Build**: One command, zero errors, verified output
2. **State**: WorldTick is truth, deltas are changes only
3. **Authority**: Server always wins, no exceptions
4. **Sessions**: Version match required, graceful reconnect
5. **Saves**: Server owns all data, clients are mirrors
6. **Trading**: Atomic transactions, no item loss
7. **Errors**: Every failure has user-readable message
8. **Version**: Explicit compatibility, clear rejection
9. **Trust**: Validate all input, escalate violations
10. **Scope**: Ship working features, cut everything else

---

## The Contract

This architecture is a **contract**.

If something isn't in this document, it's not in v1.

If something IS in this document, it MUST work before release.

No exceptions.

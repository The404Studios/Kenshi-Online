# Host-Authoritative Co-op Architecture

## The Core Insight

**Only ONE machine runs Kenshi. Everyone else is a remote controller.**

This is the only realistic way to add multiplayer to Kenshi.

## Why State Sync Doesn't Work

The previous approach tried to:
1. Have each player run their own Kenshi instance
2. Sync state (positions, health, inventory) between instances
3. Hope the games stay in sync

This fails because:
- NPC AI makes non-deterministic decisions
- Physics varies slightly between machines
- Game events trigger in different orders
- You end up fighting desync forever

## The New Model: Input Replication

```
┌─────────────────────────────────────────────────────────────┐
│                    HOST MACHINE                              │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │                   KENSHI GAME                           │ │
│  │                                                         │ │
│  │   Squad A (Host)     Squad B (Player 2)    NPCs...     │ │
│  │       ▲                    ▲                            │ │
│  └───────┼────────────────────┼────────────────────────────┘ │
│          │                    │                              │
│  ┌───────┴────────────────────┴────────────────────────────┐ │
│  │              INPUT ROUTER                                │ │
│  │   Receives commands, applies to correct squad            │ │
│  └──────────────────────────────────────────────────────────┘ │
│          ▲                    ▲                              │
│          │                    │                              │
│  ┌───────┴────────────────────┴────────────────────────────┐ │
│  │              VIEW REPLICATOR                             │ │
│  │   Sends game state to clients                            │ │
│  └──────────────────────────────────────────────────────────┘ │
│          │                    │                              │
└──────────┼────────────────────┼──────────────────────────────┘
           │                    │
      Network TCP          Network TCP
           │                    │
           ▼                    ▼
┌──────────────────┐  ┌──────────────────┐
│  CLIENT 1 (Host) │  │  CLIENT 2        │
│                  │  │                  │
│  Keyboard/Mouse  │  │  Keyboard/Mouse  │
│       ↓          │  │       ↓          │
│  Send commands   │  │  Send commands   │
│  Receive view    │  │  Receive view    │
└──────────────────┘  └──────────────────┘
```

## What Clients Send (Input Commands)

Clients don't send "my position is X,Y,Z". They send intentions:

```csharp
// Move command
{
    "type": "move",
    "squad_id": "player2_squad",
    "target": { "x": 100, "y": 0, "z": 200 }
}

// Attack command
{
    "type": "attack",
    "squad_id": "player2_squad",
    "target_id": "bandit_123"
}

// Pickup command
{
    "type": "pickup",
    "character_id": "player2_char_1",
    "item_id": "katana_456"
}

// Follow command
{
    "type": "follow",
    "squad_id": "player2_squad",
    "target_id": "player1_char_1"
}
```

## What Host Sends (View State)

Host sends the current visible state to clients:

```csharp
// View update (sent every 100ms)
{
    "type": "view",
    "tick": 12345,
    "entities": [
        {
            "id": "player1_char_1",
            "name": "Beep",
            "x": 100.5, "y": 0, "z": 200.3,
            "health": 85,
            "owner": "host"
        },
        {
            "id": "player2_char_1",
            "name": "Ruka",
            "x": 105.2, "y": 0, "z": 198.7,
            "health": 100,
            "owner": "player2"
        },
        {
            "id": "bandit_123",
            "name": "Dust Bandit",
            "x": 150.0, "y": 0, "z": 220.0,
            "health": 45,
            "owner": "npc"
        }
    ],
    "events": [
        { "type": "damage", "target": "bandit_123", "amount": 15 },
        { "type": "chat", "from": "host", "text": "Watch out!" }
    ]
}
```

## Squad Ownership

Each player controls specific squads. The host assigns ownership:

```csharp
// Ownership map
{
    "host": ["squad_main"],           // Host controls their original squad
    "player2": ["squad_recruited_1"], // Player 2 controls a recruited squad
    "player3": ["squad_recruited_2"]  // Player 3 controls another squad
}
```

### How Players Get Squads

**Option 1: Recruit**
- Player requests to recruit an NPC
- Host executes recruitment
- Host assigns new squad to requesting player

**Option 2: Split**
- Player requests to split a character from host's squad
- Host creates new squad with that character
- Host assigns new squad to requesting player

**Option 3: Spawn**
- At game start, host spawns a character for each player
- Each spawned character is in their own squad
- Squads are assigned to respective players

## What This Solves

| Problem | How It's Solved |
|---------|-----------------|
| NPC AI desync | Only one AI running (host) |
| Position desync | Only one physics engine (host) |
| Inventory desync | Only one inventory (host) |
| Trading exploits | One authoritative state |
| Combat desync | Host resolves all combat |
| Save corruption | One save file (host's) |

## What's Hard

| Challenge | Solution |
|-----------|----------|
| Latency | Client-side prediction for movement |
| Who controls what | Clear ownership UI |
| Joining mid-game | Host spawns character for new player |
| Host leaves | Game ends (host-dependent) |

## Implementation Plan

### Phase 1: Basic Input Relay
1. Host runs Kenshi
2. Client connects
3. Client sends move commands
4. Host executes commands on client's squad
5. Host sends positions back

### Phase 2: Full Commands
- Attack, follow, pickup, drop
- Building placement
- Research/crafting

### Phase 3: View Replication
- Camera follows client's squad
- UI shows client's squad status
- Minimap shows relevant entities

### Phase 4: Squad Management
- Assign squads to players
- Transfer characters between squads
- Split/merge squads

## Technical Requirements

### Host Side
1. **Input Router**: Receives commands, validates ownership, executes
2. **View Replicator**: Reads game state, sends to clients
3. **Squad Manager**: Tracks who owns what

### Client Side
1. **Input Sender**: Captures inputs, sends as commands
2. **View Receiver**: Receives state, displays to user
3. **Local Prediction**: Smooth movement before server confirms

### Network
- TCP for reliability (commands must arrive)
- ~10 Hz for view updates (100ms)
- Instant for commands

## Memory Hooks Required

### Host (Read + Write)
```
- Character positions (read for view)
- Character health (read for view)
- Character inventory (read for view)
- Selected character (write for command execution)
- Move target (write for move commands)
- Attack target (write for attack commands)
```

### Client (None!)
Clients don't need Kenshi running. They just:
- Send commands over network
- Receive and display view state
- Could be a simple GUI app

**This is the key insight: Clients don't need the game.**

## Simplified Client

The client could be:
1. A simple map view showing entity positions
2. Click to move your squad
3. Click enemy to attack
4. No Kenshi installation needed on client!

Or for full experience:
1. Client runs Kenshi
2. Kenshi is just a "viewer" - displays what host sends
3. Inputs intercepted and sent to host instead of local game

## Trade Flow (Example)

```
1. Player 2 clicks "Trade" on Player 1's character
2. Client sends: { type: "trade_request", target: "player1_char" }
3. Host opens trade window (on host's Kenshi)
4. Host sends trade state to both clients
5. Player 2 adds items to trade
6. Client sends: { type: "trade_add", item: "katana_456" }
7. Host adds item to trade window
8. Both confirm
9. Host executes trade
10. Host sends updated inventories to clients
```

No item duplication possible because there's only ONE inventory.

## Summary

**Don't sync multiple Kenshis. Control one Kenshi with multiple players.**

This is the only architecture that can actually work.

# Clean Puppet Remote Player System Design

**Date**: 2026-03-10
**Status**: Approved
**Scope**: Full remake of remote player spawn, sync, AI suppression, and lifecycle

## Problem

The current remote player system has 5 fundamental bugs that make it non-functional:

1. **Spawn**: Struct clone crashes (stale pointers), NPC hijack unreliable (needs nearby NPCs)
2. **Position**: Physics engine overwrites network positions every frame (SetPosition hook doesn't block remote chars)
3. **AI**: Not fully suppressed — movement/combat/task decisions fight network input
4. **Lifecycle**: UnmarkRemoteControlled never called on despawn (UAF when memory reused)
5. **Complexity**: 3 spawn paths, 3 position write methods, deferred probing — too many fallbacks masking real issues

## Solution: Clean Puppet Architecture

Remote players are game characters under exclusive network control. The game creates them via its normal factory, then we block all local systems (physics, AI) from modifying them. Network writes are the sole authority.

### 1. Spawn — Single Path via FactoryCreate

Use `RootObjectFactory::create` (RVA 0x583400) exclusively. This is the high-level dispatcher called by 11 game systems internally. It takes `(factory, GameData*)` and builds the request struct from scratch with live pointers.

- GameData* from mod templates (kenshi-online.mod, pre-loaded at startup)
- SEH-protected call with directSpawnBypass flag
- No fallback to struct clone or NPC hijack — if create fails, log and retry next tick
- Post-spawn: position, name, faction, AI suppression, physics chain probe

**Why this works**: create() constructs all internal pointers (faction, squad, AI) from the GameData template. No stale pointer problem.

### 2. Position Sync — Block Physics, Network Writes Only

The critical missing piece: hook SetPosition to skip physics updates for remote characters.

```
SetPosition hook:
  if character in s_remoteControlled:
    return  // block physics write
  else:
    call original  // local characters proceed normally
```

Network position application:
- Write to cached position at char+0x48 every interpolation tick
- Since physics is blocked, this value persists until next network write
- Write rotation to char+0x58 with quaternion validation (NaN/Inf/magnitude)

Position write priority:
1. HavokCharacter::setPosition if available (updates physics state correctly)
2. AnimClass chain if probed (char+animClassOffset -> +0xC0 -> +0x320)
3. Direct cached write to +0x48 (acceptable since physics is blocked)

### 3. Animation — Implicit from Movement

Kenshi's animation controller reads position deltas and plays appropriate animations automatically. As long as interpolation moves the character smoothly:
- Standing still = idle animation
- Moving slowly = walk animation
- Moving fast = run animation

Data sent in position packets (already implemented):
- moveSpeed (uint8, 0-255 mapped to 0-15 m/s)
- flags (running, sneaking, combat stance)
- animStateId (uint8, derived from speed: 0=idle, 1=walk, 2=run)

No skeleton sync needed. The game handles animation from movement.

### 4. AI Suppression — Complete Blockade

Keep AI controller alive (engine needs valid pointer) but block all outputs:

| Hook | Local chars | Remote chars |
|------|-------------|--------------|
| MoveTo/SetDestination | Pass through | Block (return) |
| Attack/StartCombat | Pass through | Block (return) |
| TaskAssign | Pass through | Block (return) |
| SetPosition (physics) | Pass through | Block (return) |
| AICreate | Pass through | Pass through (controller needed) |

On despawn: UnmarkRemoteControlled MUST be called to prevent UAF.

### 5. Lifecycle — Clean State Machine

```
SPAWN:
  S2C_EntitySpawn received
  -> Queue SpawnRequest
  -> FactoryCreate(factory, modTemplate)
  -> Post-spawn:
      WritePosition(spawnPos)
      SetName(playerName)
      FixFaction(playerFaction)
      MarkRemoteControlled()  // blocks AI + physics
      ProbeAnimClass()        // synchronous, not deferred
  -> EntityRegistry: state=Active, authority=Remote

UPDATE (per tick):
  -> Interpolation::GetInterpolated(entityId)
  -> WritePosition(interpolatedPos)
  -> WriteRotation(interpolatedRot)

DESPAWN:
  S2C_EntityDespawn received
  -> UnmarkRemoteControlled()  // CRITICAL
  -> Unregister from EntityRegistry
  -> Teleport underground (-10000 y)
```

### 6. Files to Modify

| File | Change |
|------|--------|
| entity_hooks.cpp | Add SetPosition remote char bypass in existing hook |
| ai_hooks.cpp | Complete suppression: add combat + task blocking |
| spawn_manager.cpp | Remove approaches 1/2/3, keep only FactoryCreate |
| core.cpp | Simplify ApplyRemotePositions to single write path |
| packet_handler.cpp | Add UnmarkRemoteControlled to despawn/disconnect handlers |

### 7. What We Keep

- ENet networking (3 channels, all message types)
- Interpolation system (ring buffer, jitter estimation, snap correction)
- EntityRegistry (state tracking, authority model)
- SEH protection (game pointers can become invalid)
- Quaternion compression (smallest-three encoding)
- Mod template system (kenshi-online.mod GameData objects)

### 8. What We Remove

- Struct clone spawn path (crashes, stale pointers)
- NPC hijack spawn path (unreliable, wrong appearance)
- Raw GameData to process (returns null)
- createRandomChar fallback (wrong appearance)
- Deferred AnimClass probing (do it synchronously)
- Triple position write fallback cascade (single path + physics blocking)

### 9. Risk Assessment

| Risk | Mitigation |
|------|------------|
| FactoryCreate fails | SEH protection, retry next tick, log diagnostics |
| SetPosition hook misses some physics writes | Multiple physics write points may exist; monitor for drift |
| AI suppression incomplete | Add hooks incrementally; any leak causes visible jitter |
| Mod templates not loaded | Verify at connection time, reject if missing |

### 10. Testing

- Connect TestClient at 162.248.94.149:27800
- Verify: remote character appears at correct position
- Verify: remote character moves smoothly (no jitter/rubber-banding)
- Verify: remote character plays walk/run animations
- Verify: remote character stays at network position (no physics drift)
- Verify: disconnect properly cleans up (no UAF crash)

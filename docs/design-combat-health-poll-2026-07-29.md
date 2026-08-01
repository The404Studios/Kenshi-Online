# Design: Combat Health Polling v2 (Revised after Code Audit)

**Date:** 2026-07-29
**Status:** DRAFT — awaiting approval
**Based on:** audit findings from sub-agent code review

---

## What Changed from v1

| v1 (broken) | v2 (fixed) |
|-------------|------------|
| Poll REMOTE entity health | Poll LOCAL entity health |
| Server ownership check would reject | Server ownership check PASSES (owner sends own data) |
| Used non-existent `WriteHealth()` | Uses existing `registry.UpdateLimbHealth()` + leaves write to existing `HandleLimbHealth` in packet_handler |
| Used Channel 1 (wrong) | Uses `SendReliable` (Channel 0), matching existing combat_hooks usage |

## Root Cause Found

Server `HandleLimbHealth` validation (`server.cpp:1965`):
```cpp
if (it->second.owner != player.id) return;
```
Client A cannot report Client B's health — **only the owner can report**.  
Correct flow: Client reads **own** entities' health → server receives (owner match passes) → broadcasts to others.

## Design

### Approach: Periodic LOCAL Health Self-Report

Client polls its own local entities' health every ~200ms and sends `C2S_LimbHealth`.  
Server validates ownership (passes — self-reporting), stores, broadcasts to other players.

### Data Flow (Corrected)

```
Every 200ms on game thread:

Core::OnGameTick()
  └─ PollLocalHealth()
       └─ For each local entity in EntityRegistry (with gameObject != nullptr):
            ├─ SEH-wrapped CharacterAccessor::GetHealth() — 7 limb values
            ├─ Encode MsgLimbHealth {entityId, health[7]}
            └─ Core::GetClient().SendReliable() on Channel 0

Server:
  └─ HandleLimbHealth():
       ├─ Validate owner == player.id ✓ (owner reports own entity)
       ├─ Store in ServerEntity.limbHealth[7]
       └─ BroadcastExcept → S2C_LimbHealth to OTHER players

Client B:
  └─ HandleLimbHealth():
       ├─ registry.UpdateLimbHealth()
       └─ Memory::Write through health chain (implementation already exists)
```

### Files Changed

| File | Change |
|------|--------|
| `core.h` | Declare `PollLocalHealth()`, add `m_lastHealthPollTime` member |
| `core.cpp` | Implement `PollLocalHealth()` with 200ms throttle, SEH around reads, call from `OnGameTick()` |

### Not Changed

| File | Reason |
|------|--------|
| `combat_hooks.cpp` | Death/KO + piggyback LimbHealth stays — poll fills gaps between events |
| `server.cpp` | Handler already complete — ownership check is correct design |
| `packet_handler.cpp` | Handler already complete — writes through health chain |
| `messages.h`/`protocol.h` | Types already defined |

### Edge Cases

| Case | Handling |
|------|---------|
| healthChain offsets not resolved (== -1) | Skip entity, log warning once |
| entity destroyed mid-poll | SEH catches, skip |
| health unchanged | Still send (tiny overhead, overwrites same value) |
| combat_hooks simultaneously sends LimbHealth | No conflict — same values, server stores latest |

### Success Criteria (Same as v1)

1. Build: Release x64 compiles with 0 errors
2. UnitTest passes
3. IntegrationTest passes  
4. During 2-player combat, remote health bars change
5. `C2S_LimbHealth` in server log ~5/sec per player
6. No regression in death/KO sync

### Interaction with Existing LimbHealth

`combat_hooks::ProcessDeferredEvents()` already piggybacks `C2S_LimbHealth` on death/KO events.  
The poll does NOT remove this — it adds health updates **between** death/KO events.  
Both systems can coexist: the poll provides continuous updates, death/KO provides final authoritative state.

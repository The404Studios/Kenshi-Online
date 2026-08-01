# Design: Inventory Sync Phase 1

**Date:** 2026-08-01
**Status:** IMPLEMENTED (2026-08-01, commit 1617d0d on feat/inventory-sync)
**Related:** Phase 3 (Inventory Sync), CONTRIBUTING High Priority #3

---

## Problem

Current inventory sync only covers incremental add/remove events (via `C2S_ItemPickup` / `C2S_ItemDrop`). Two gaps:

1. **No full-inventory snapshot** — a joining player never sees other players' pre-existing items. Only changes *after* join are visible.
2. **Drop position hardcoded to (0,0,0)** — `Hook_ItemDrop` at `inventory_hooks.cpp:151` sends `msg.posX = msg.posY = msg.posZ = 0.f`, so remote clients can't see where an item was dropped.

## Non-Goal

- NOT adding per-item instance IDs (requires message format change across all clients — architecture-level, deferred)
- NOT adding server-side inventory state storage (transparent relay is sufficient for current scope)
- NOT handling action=2 (modify count) — add/remove already covers stack growth via `InventoryAccessor::AddItem`

## Design

### 1. Drop Position (2-line fix)

`Hook_ItemDrop` reads the owner character's position via `CharacterAccessor::GetPosition()` (already used by `PollLocalPositions`), fills `msg.posX/Y/Z` instead of hardcoded 0.

### 2. Full Inventory Snapshot (new message pair)

```
Client (every 10s + on join):
  Core::OnGameTick()
    └─ PollLocalInventory()           [new, throttled 10s, SEH-protected]
         └─ For each local entity:
              ├─ InventoryAccessor::GetItemCount()
              ├─ InventoryAccessor::GetItem(i) × N → collect {templateId, qty}
              └─ Send C2S_InventorySnapshot via SendReliable (Ch0)

Server:
  └─ HandleInventorySnapshot():
       ├─ Validate owner == player.id
       ├─ Relay as S2C_InventorySnapshot to all OTHER players

Client B:
  └─ HandleInventorySnapshot():
       ├─ Validate entity exists
       ├─ Clear existing items for that entity (remove all)
       └─ AddItem(templateId, qty) for each item in snapshot
```

### Message Structures (messages.h)

```cpp
struct MsgInventorySnapshotItem {
    uint32_t itemTemplateId;
    int32_t  quantity;
};

struct MsgInventorySnapshot {
    EntityID entityId;
    uint16_t itemCount;
    // Followed by itemCount × MsgInventorySnapshotItem
};
```

### Protocol (protocol.h)

Add to `MessageType` enum (next free values 0x66/0x67 in inventory range 0x60-0x69):
- `C2S_InventorySnapshot = 0x66`
- `S2C_InventorySnapshot = 0x67`

### Files Changed

| File | Change |
|------|--------|
| `KenshiMP.Common/include/kmp/messages.h` | Add `MsgInventorySnapshotItem`, `MsgInventorySnapshot` |
| `KenshiMP.Common/include/kmp/protocol.h` | Add `C2S_InventorySnapshot` (0x6A), `S2C_InventorySnapshot` (0x6B) |
| `KenshiMP.Core/hooks/inventory_hooks.cpp` | `Hook_ItemDrop`: read owner position, fill posX/Y/Z |
| `KenshiMP.Core/core.h` | Declare `PollLocalInventory()`, add `m_lastInventoryPollTime` |
| `KenshiMP.Core/core.cpp` | Implement `PollLocalInventory()` (10s throttle, SEH, iterate local entities, build snapshot, send); call from `OnGameTick` |
| `KenshiMP.Server/server.cpp` | Add `HandleInventorySnapshot` (owner validate → relay broadcast); dispatch in packet loop |
| `KenshiMP.Server/server.h` | Declare `HandleInventorySnapshot` |
| `KenshiMP.Core/net/packet_handler.cpp` | Add `HandleInventorySnapshot` (clear + AddItem each); dispatch |
| `KenshiMP.UnitTest/main.cpp` | Add `Test_InventorySnapshotRoundTrip` |

### Edge Cases

| Case | Handling |
|------|---------|
| Remote entity not spawned | Skip write (gameObj null check) |
| Empty inventory (itemCount=0) | Valid — clears target's inventory |
| GetItem fails mid-iteration (SEH) | Abort snapshot, skip send this cycle |
| Inventory changes during snapshot read | Best-effort — next 10s cycle reconciles |
| Entity despawned mid-write | SEH wrapper, skip |

### Success Criteria

1. Build: full solution Release x64, 0 errors
2. UnitTest: all pass (incl. new snapshot round-trip test)
3. IntegrationTest: 64/66 (no regression; 2 pre-existing failures unchanged)
4. Server log shows `HandleInventorySnapshot` relay for a connected client
5. No new crashes (SEH protects all game memory access)

---

## Alternatives Considered

### Alt A: Server stores inventory per entity
- **Cost:** ServerEntity struct change + persistence + join-time replay
- **Benefit:** New joiners get snapshots without waiting for next client poll
- **Verdict:** Deferred — periodic poll (10s) is sufficient for current co-op scope

### Alt B: Instance IDs per item
- **Cost:** Message format change across ALL inventory messages + client/server tracking
- **Benefit:** Handles identical items, durability, bag slots
- **Verdict:** Deferred — architecture-level change, out of Phase 1 scope

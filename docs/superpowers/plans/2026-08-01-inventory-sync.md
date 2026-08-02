# Plan: Full Inventory Snapshot Sync

**Date:** 2026-08-01
**Status:** COMPLETED (commit 1617d0d, branch feat/inventory-sync)
**Design:** docs/design-inventory-sync-2026-08-01.md

## Objective

Add full-inventory reconciliation (snapshot) on top of existing incremental
pickup/drop events, and fix hardcoded drop coordinates.

## Tasks

### Task 1 (RED): Snapshot round-trip test
- Test_InventorySnapshotRoundTrip() in UnitTest (RED: compile fails)
### Task 2 (GREEN): messages.h + protocol.h (0x66/0x67)
### Task 3: Drop position (SEH_FillDropPosition, C2712 helper)
### Task 4: Core::PollLocalInventory() 10s throttle + OnGameTick
### Task 5: Server HandleInventorySnapshot (owner validate + relay)
### Task 6: Client HandleInventorySnapshot (clear + re-apply)
### Task 7: Verify (build 0 errors, UnitTest 111/111, IntegrationTest 64/66)

## Success Criteria
1. Snapshot relay verified end-to-end (Test_InventorySnapshot)
2. Build 0 errors
3. No regression
4. No magic numbers (KMP_ constants)

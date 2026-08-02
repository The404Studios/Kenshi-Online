# Plan: Client Prediction Reconciliation (Phase 7 ReconcileLocal)

**Date:** 2026-08-01
**Status:** COMPLETED (commit 7777359, branch feat/inventory-sync)
**Design:** (inlined; small enough to live in plan)

---

## Objective

Implement the `ReconcileLocal` branch in `HandlePositionUpdate` — the server echoes
authoritative positions of OUR own entities; reconcile prediction by snapping to
server authority only when local has diverged significantly.

## Tasks

### Task 1 (RED): Test reconciliation decision
- Add `Test_ReconcileDecision()` to `KenshiMP.UnitTest/main.cpp`
- Test `AuthorityValidator::ShouldSnapToServer(local, server, threshold)`:
  identical → no snap; 2m < 5m → no snap; exactly 5m → no snap (strict >);
  50m → snap; 3D divergence → snap
- Add `sync/authority_validator.cpp` to `KenshiMP.UnitTest/CMakeLists.txt`
- Verify: compile FAILS (function undefined) → RED confirmed

### Task 2 (GREEN): Implement decision + branch
- `authority_validator.h/.cpp`: add `ShouldSnapToServer()` pure function
- `constants.h`: add `KMP_RECONCILE_SNAP_DIST = 5.0f`
- `packet_handler.cpp` ReconcileLocal case:
  - Get local game object
  - SEH-protected read of local position
  - If `ShouldSnapToServer(local, server, KMP_RECONCILE_SNAP_DIST)`:
    `WritePosition(serverPos)` + debug log
  - Small deltas: tolerate (local input authoritative)
- Verify: UnitTest 116/116 → GREEN

### Task 3: Verify
- Full solution build: 0 errors
- IntegrationTest: 64/66 (no regression; 2 pre-existing failures)

## Success Criteria
1. UnitTest includes Test_ReconcileDecision (5 assertions)
2. Build 0 errors
3. No regression in IntegrationTest
4. No rubber-banding for small deltas; hard snap only beyond 5m

# Phase 3 Dead Code Cleanup — Remaining Items

**Date:** 2026-08-01
**Status:** PARTIAL — items 1/2/6 of CODE_CLEANUP_REPORT done (commit 34c9a08).
The rest are tracked here as explicit follow-up tasks (NOT silently dropped).

## Done (34c9a08)
- [x] zone_interest.cpp (0 callers, verified)
- [x] ownership.cpp (0 callers, verified)
- [x] ProcessSpawnQueue + ProcessSpawnQueueFromHook (0 callers, verified)

## Remaining — explicit follow-up

| # | Item | Risk | Why deferred | Status |
|---|------|------|-------------|--------|
| 3 | sync_facilitator 15+ dead query methods | MED | Bind/Unbind used by core.cpp:546/658; deleting methods requires touching zone_engine call sites | OPEN |
| 5 | movement_hooks Hook_SetPosition/Hook_MoveTo bodies | MED | Install() is a no-op facade used by core.cpp:1456; deletion couples to core init block | OPEN |
| 7 | createRandomChar fallback (18 refs) | MED | Spawn path fallback; needs mod-template availability guarantee | OPEN |
| 8 | Legacy position path PollLocalPositions/ApplyRemotePositionsDirect | HIGH | PollLocalPositions IS the active sender (SyncOrchestrator switch incomplete) | OPEN |
| 9 | Commented-out code blocks (entity_hooks 30+, spawn_manager 796+) | LOW | Mechanical, time-consuming | OPEN |
| 10 | Unused includes (IWYU) | LOW | Needs tooling run | OPEN |
| 11 | Obsolete TODOs | DONE | packet_handler TODO replaced by DeferredSpawnQueue (verified gone) | DONE |

## Decision
Items 3/5/7/8 need functional work or architecture decisions (not just deletion).
Items 9/10 are mechanical cleanups. All tracked; pick up as separate PRs.

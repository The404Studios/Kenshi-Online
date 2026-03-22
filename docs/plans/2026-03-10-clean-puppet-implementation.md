# Clean Puppet Remote Player — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make remote players appear, move with animations, and not fight with local AI/physics.

**Architecture:** Single spawn path (FactoryCreate), continuous physics-chain position writes, combat blocked for remote chars. SetPosition/MoveTo hooks are NOT installable (mov-rax-rsp prologue), so movement suppression relies on AI behavior tree load-gating + continuous position overwrite.

**Tech Stack:** C++17, MSVC, MinHook, ENet, SEH, Ogre3D

**Design doc:** `docs/plans/2026-03-10-clean-puppet-remote-player-design.md`

---

### Task 1: Remove broken spawn paths from SpawnWithModTemplate

**Files:**
- Modify: `KenshiMP.Core/game/spawn_manager.cpp:808-926`

**Step 1: Delete approaches 1, 2, 3**

In `SpawnWithModTemplate` (line 776), delete everything from line 808 through line 926 (approaches 1, 2, 3). Keep only approach 0 (lines 788-807) and the final failure log (lines 928-929).

The function should end like:

```cpp
        spdlog::debug("SpawnManager: FactoryCreate returned null — trying fallbacks");
    }

    spdlog::warn("SpawnManager: SpawnWithModTemplate — FactoryCreate failed for slot {}", playerSlot);
    return nullptr;
}
```

Change the log message from "all approaches failed" to "FactoryCreate failed" since there's only one approach now.

**Step 2: Simplify SpawnCharacterDirect**

In `SpawnCharacterDirect` (line 130), remove the createRandomChar fallback (lines 148-167). If mod template spawn fails, just return nullptr.

The function should be:

```cpp
void* SpawnManager::SpawnCharacterDirect(const Vec3* desiredPosition, int modSlot) {
    if (m_modTemplateCount.load() > 0 && m_factory && m_origProcess) {
        int templateCount = m_modTemplateCount.load();
        if (modSlot < 0 || modSlot >= templateCount) modSlot = 0;
        Vec3 pos = desiredPosition ? *desiredPosition : Vec3{0, 0, 0};
        void* character = SpawnWithModTemplate(modSlot, pos);
        if (character) {
            spdlog::info("SpawnManager: SpawnCharacterDirect via MOD TEMPLATE — char 0x{:X}",
                         reinterpret_cast<uintptr_t>(character));
            return character;
        }
        spdlog::warn("SpawnManager: Mod template spawn failed");
    }

    static int s_noTemplateLog = 0;
    if (++s_noTemplateLog <= 5 || s_noTemplateLog % 100 == 0) {
        spdlog::warn("SpawnManager: SpawnCharacterDirect — FactoryCreate failed "
                     "(modTemplates={}, factory={})",
                     m_modTemplateCount.load(), m_factory != nullptr);
    }
    return nullptr;
}
```

**Step 3: Build to verify compilation**

Run: `MSBuild KenshiMP.sln /t:KenshiMP_Core /p:Configuration=Release /p:Platform=x64 /m`
Expected: Build succeeded

**Step 4: Commit**

```bash
git add KenshiMP.Core/game/spawn_manager.cpp
git commit -m "refactor: remove broken spawn approaches 1/2/3, keep only FactoryCreate"
```

---

### Task 2: Block combat damage from remote-controlled attackers

**Files:**
- Modify: `KenshiMP.Core/hooks/combat_hooks.cpp:33-51`

**Why:** Without MoveTo/SetPosition hooks (mov-rax-rsp prologue blocks installation), the remote character's AI behavior tree may still run and initiate attacks against nearby NPCs. We can't undo applied damage, so we must block it at the hook level.

**Step 1: Add IsRemoteControlled check to Hook_ApplyDamage**

At `combat_hooks.cpp:41` (after `auto& core = Core::Get();`), add a check before calling the original function. If the attacker is remote-controlled, skip the original — damage from remote characters should come from the network, not local AI.

Insert BEFORE line 43 (`// ALWAYS call original first`):

```cpp
    // Block local-AI-initiated damage FROM remote characters.
    // Remote char damage is applied via network S2C_CombatHit, not local AI.
    // Without this, the remote char's unsuppressed AI attacks nearby NPCs.
    if (attacker && ai_hooks::IsRemoteControlled(attacker)) {
        spdlog::debug("combat_hooks: BLOCKED ApplyDamage from remote-controlled attacker 0x{:X}",
                      reinterpret_cast<uintptr_t>(attacker));
        return;
    }
```

Add the include at the top of combat_hooks.cpp if not already present:
```cpp
#include "ai_hooks.h"
```

**Step 2: Build to verify compilation**

Run: `MSBuild KenshiMP.sln /t:KenshiMP_Core /p:Configuration=Release /p:Platform=x64 /m`
Expected: Build succeeded

**Step 3: Commit**

```bash
git add KenshiMP.Core/hooks/combat_hooks.cpp
git commit -m "fix: block local AI damage from remote-controlled characters"
```

---

### Task 3: Synchronous AnimClass probe at spawn time

**Files:**
- Modify: `KenshiMP.Core/core.cpp:2269-2276`
- Modify: `KenshiMP.Core/game/game_character.cpp:53-66` (expose ProbeAnimClassOffset)
- Modify: `KenshiMP.Core/game/game_types.h` (add declaration)

**Why:** The deferred AnimClass probe runs many frames after spawn, during which WritePosition falls back to Method 3 (cached position at +0x48, overwritten by physics). If we probe synchronously during post-spawn setup (after WritePosition has set the cached position), Method 2 (physics chain) is available immediately. Since we already wrote position to +0x48 in step 1 of the post-spawn, the probe has a valid position to match against.

**Step 1: Add a synchronous probe function to game_character.cpp**

The existing `ProbeAnimClassOffset` (line 53) is static. Add a public wrapper that forces a synchronous probe even if already probed, because the previous probe may have failed on a (0,0,0) character:

After `SEH_ProbeAnimClassOffset` (line 109-116), add:

```cpp
void ForceProbeAnimClass(uintptr_t charPtr) {
    // Reset the probed flag so we try again with this character's position
    s_animClassProbed = false;
    SEH_ProbeAnimClassOffset(charPtr);
}
```

**Step 2: Add declaration to game_types.h**

In `game_types.h`, near the existing `ScheduleDeferredAnimClassProbe` declaration (line 236), add:

```cpp
void ForceProbeAnimClass(uintptr_t charPtr);
```

**Step 3: Replace deferred probe with synchronous probe in SEH_FallbackPostSpawnSetup**

In `core.cpp:2269-2276`, replace:

```cpp
    // 6. Schedule deferred AnimClass probe
    __try {
        game::ScheduleDeferredAnimClassProbe(
            reinterpret_cast<uintptr_t>(character));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — AnimClassProbe AV\n");
        allOk = false;
    }
```

With:

```cpp
    // 6. Synchronous AnimClass probe — position was written in step 1,
    //    so the cached position at +0x48 is valid for the probe to match against.
    //    This enables Method 2 (physics chain) position writes from the first frame.
    __try {
        game::ForceProbeAnimClass(
            reinterpret_cast<uintptr_t>(character));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — AnimClassProbe AV\n");
        allOk = false;
    }
```

**Step 4: Do the same in sync_orchestrator.cpp**

In `sync_orchestrator.cpp:193`, replace `game::ScheduleDeferredAnimClassProbe` with `game::ForceProbeAnimClass`. Same pattern.

Also at `sync_orchestrator.cpp:623`, same replacement.

**Step 5: Do the same in entity_hooks.cpp**

At `entity_hooks.cpp:249`, replace `game::ScheduleDeferredAnimClassProbe` with `game::ForceProbeAnimClass`.

At `entity_hooks.cpp:692`, same replacement.

**Step 6: Build to verify compilation**

Run: `MSBuild KenshiMP.sln /t:KenshiMP_Core /p:Configuration=Release /p:Platform=x64 /m`
Expected: Build succeeded

**Step 7: Commit**

```bash
git add KenshiMP.Core/core.cpp KenshiMP.Core/game/game_character.cpp KenshiMP.Core/game/game_types.h KenshiMP.Core/sync/sync_orchestrator.cpp KenshiMP.Core/hooks/entity_hooks.cpp
git commit -m "fix: synchronous AnimClass probe at spawn time for immediate physics-chain writes"
```

---

### Task 4: Verify despawn cleanup (no code change needed)

**Files:**
- Read-only: `KenshiMP.Core/net/packet_handler.cpp:319-372, 523-540`

**Why:** The design called for adding UnmarkRemoteControlled to despawn handlers. Investigation revealed it's ALREADY there:
- `HandleEntityDespawn` (line 519 inside SEH_DespawnCleanup): calls `ai_hooks::UnmarkRemoteControlled(gameObj)`
- `HandlePlayerLeft` (line 358): calls `ai_hooks::UnmarkRemoteControlled(gameObj)`

**Step 1: Verify both paths exist**

Read packet_handler.cpp and confirm both UnmarkRemoteControlled calls exist. No code change needed.

---

### Task 5: Build full solution and verify

**Step 1: Full build**

Run: `MSBuild KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: All projects build successfully

**Step 2: Copy DLL to game directory**

Copy `build/bin/Release/KenshiMP.Core.dll` to the Kenshi game directory.

**Step 3: Verify with TestClient**

Connect TestClient at 162.248.94.149:27800:
- Remote character should appear (FactoryCreate spawn)
- Remote character should stay at network position (physics chain writes)
- Remote character should not attack NPCs (combat blocked)
- Disconnect should clean up without crash

---

## Design Deviations from Original Plan

The original design specified hooking SetPosition to block physics writes for remote characters. Investigation revealed:

1. **SetPosition hook is NOT installed** — the `mov rax, rsp` prologue causes MinHook trampoline corruption. The hook function exists in movement_hooks.cpp but Install() skips it.
2. **MoveTo hook is NOT installed** — same prologue issue.

**Mitigation:** Instead of blocking physics at the hook level, we:
- Write position via physics chain (Method 2) every frame after physics resolves — our OnGameTick fires from the Ogre render loop, which runs after game physics
- Block combat damage (the only irreversible AI action) via Hook_ApplyDamage
- Accept that AI may issue movement commands, but they're overwritten next frame by our position write

This approach is simpler and avoids the mov-rax-rsp problem entirely. If position jitter is observed in testing, we can investigate alternative physics-blocking approaches (VEH, hardware breakpoints, or finding the internal AI task clear function).

## Summary of Changes

| File | Lines Changed | What |
|------|---------------|------|
| spawn_manager.cpp | ~140 lines removed | Remove approaches 1, 2, 3 + createRandomChar fallback |
| combat_hooks.cpp | ~6 lines added | Block ApplyDamage from remote-controlled attackers |
| game_character.cpp | ~5 lines added | ForceProbeAnimClass public wrapper |
| game_types.h | ~1 line added | ForceProbeAnimClass declaration |
| core.cpp | ~2 lines changed | ScheduleDeferredAnimClassProbe → ForceProbeAnimClass |
| sync_orchestrator.cpp | ~2 lines changed | Same replacement (2 locations) |
| entity_hooks.cpp | ~2 lines changed | Same replacement (2 locations) |

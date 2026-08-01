# Combat Health Polling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remote players see intermediate health bar changes during combat (not just death/KO end-states)

**Architecture:** Add a 200ms periodic poll in `Core::OnGameTick()` that reads local entity limb health via `CharacterAccessor::GetHealth()`, bundles into `C2S_LimbHealth` messages, and sends via existing reliable channel. Server `HandleLimbHealth` already validates ownership and broadcasts to other players. Client `HandleLimbHealth` already writes to game memory.

**Tech Stack:** C++17, ENet, `std::chrono` for throttle, SEH for crash safety

**Design doc:** `docs/design-combat-health-poll-2026-07-29.md`

---

## Files Changed

| File | Responsibility | Change |
|------|---------------|--------|
| `KenshiMP.Core/core.h` | Declare `PollLocalHealth()` private method + add `m_lastHealthPollTime` member | Add 2 lines |
| `KenshiMP.Core/core.cpp` | Implement `PollLocalHealth()` + call from `OnGameTick()` | Add ~40 lines |

### Task 1: Add declaration and member variable to core.h

**Files:**
- Modify: `KenshiMP.Core/core.h:264` (add declaration next to `PollLocalPositions`)
- Modify: `KenshiMP.Core/core.h:309-310` (add member variable in member section)

- [ ] **Step 1: Add `PollLocalHealth()` declaration**

After line 264 (`void PollLocalPositions();`), add:

```cpp
    void PollLocalHealth();
```

- [ ] **Step 2: Add `m_lastHealthPollTime` member**

After the last `std::chrono` member (find `m_hostTpTimer` around line 325), add:

```cpp
    // Health polling throttle (combat health sync)
    std::chrono::steady_clock::time_point m_lastHealthPollTime{};
```

---

### Task 2: Implement PollLocalHealth() in core.cpp

**Files:**
- Modify: `KenshiMP.Core/core.cpp` (add new function after `PollLocalPositions` around line 3063)

- [ ] **Step 1: Add `PollLocalHealth()` implementation**

After `Core::PollLocalPositions()` (ends at line 3063), add:

```cpp
void Core::PollLocalHealth() {
    // Only poll while connected and game is loaded
    if (!m_connected || m_localPlayerId == 0) return;

    // Throttle to ~200ms (5 Hz) — enough for visible health bar changes, low bandwidth
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastHealthPollTime);
    if (elapsed.count() < 200) return;
    m_lastHealthPollTime = now;

    // Iterate local entities and read limb health
    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) return;

    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        // Validate gameObj pointer range
        uintptr_t objAddr = reinterpret_cast<uintptr_t>(gameObj);
        if (objAddr < 0x10000 || objAddr >= 0x00007FFFFFFFFFFF || (objAddr & 0x7) != 0) {
            continue;
        }

        // Read all 7 limb health values with SEH protection
        MsgLimbHealth msg{};
        msg.entityId = netId;
        bool readOk = false;

        __try {
            game::CharacterAccessor accessor(gameObj);
            for (int i = 0; i < 7; i++) {
                msg.health[i] = accessor.GetHealth(static_cast<BodyPart>(i));
            }
            readOk = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            spdlog::warn("PollLocalHealth: SEH caught exception reading health for entity {}", netId);
            m_entityRegistry.SetGameObject(netId, nullptr);
            continue;
        }

        if (!readOk) continue;

        // Send C2S_LimbHealth via reliable channel (Channel 0, matching combat_hooks pattern)
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_LimbHealth);
        writer.WriteRaw(&msg, sizeof(msg));
        m_client.SendReliable(writer.Data(), writer.Size());

        // Debug log (first 10 polls, then every 200th)
        static int s_healthPollsSent = 0;
        s_healthPollsSent++;
        if (s_healthPollsSent <= 10 || s_healthPollsSent % 200 == 0) {
            spdlog::debug("Core::PollLocalHealth: sent entity={} chest={:.1f} head={:.1f} (#{})",
                          netId, msg.health[1], msg.health[0], s_healthPollsSent);
        }
    }
}
```

---

### Task 3: Wire into OnGameTick()

**Files:**
- Modify: `KenshiMP.Core/core.cpp:2420-2430` (add call alongside existing PollLocalPositions call)

- [ ] **Step 1: Add PollLocalHealth() call to OnGameTick()**

Find the section where `PollLocalPositions()` is called (around line 2422). Add `PollLocalHealth()` right after it:

```cpp
        // ── Step: Poll local positions ──
        PollLocalPositions();
        // NOTE: SendCachedPackets() removed — PollLocalPositions() already

        // ── Step: Poll local health ──
        // Sends C2S_LimbHealth at ~5Hz so remote players see health bar changes.
        // combat_hooks already sends C2S_LimbHealth on death/KO events — this fills
        // the gap between those events for intermediate damage visibility.
        PollLocalHealth();
```

---

### Task 4: Build and verify

**Files:** None

- [ ] **Step 1: Build Release x64**

Run:

```bash
cd build
MSBuild.exe KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m
```

Expected: `0 errors`

- [ ] **Step 2: Run UnitTest**

Run:

```bash
cd bin/Release
KenshiMP.UnitTest.exe
```

Expected: All tests PASSED

- [ ] **Step 3: Run IntegrationTest**

Run:

```bash
KenshiMP.IntegrationTest.exe
```

Expected: All tests PASSED

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: Add periodic local health polling for cross-player damage visibility

Adds PollLocalHealth() to OnGameTick() that reads local entity limb health
at ~5Hz via CharacterAccessor::GetHealth() and sends C2S_LimbHealth.

Server already broadcasts S2C_LimbHealth to other players; client already
writes received health values to game memory via HandleLimbHealth. This
fills the gap between death/KO events so remote players see intermediate
health bar changes during combat."
```

---

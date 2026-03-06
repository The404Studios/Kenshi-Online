# Changelog

All notable changes to Kenshi-Online are documented here.

---

## [v1.0.2] — 2026-03-05

### Critical Stability Fixes

This release focuses entirely on crash prevention and stability. Both the client and server have been hardened to survive zone loading, NPC spawning, and extended play sessions without crashing.

#### Spawn Pipeline Overhaul
- **Disabled SpawnCharacterDirect fallback** — the old 10-second timeout spawn path copied a pre-call struct from loading time whose faction pointer (`char+0x10`) became a use-after-free when the source NPC's zone unloaded. The game would crash at `game+0x927E94` reading `faction+0x250` on every character update tick.
- **In-place replay is now the sole spawn mechanism** — remote characters spawn only when the host walks near NPCs (towns, patrols, caravans). `Hook_CharacterCreate` piggybacks on the natural NPC creation event and replays the spawn with fully valid game state including a live faction pointer.
- Added user-facing guidance: the HUD displays a message instructing players to walk near a town to trigger remote character spawns.

#### Pointer Validation Hardening
- **Tightened ActivePlatoon range check** from `0x10000` to `0x1000000` (16MB minimum) in `ResolveActivePlatoon`. SSO string data like `"one"` (`0x656E6F` = 6.6MB) previously passed the old range check and was dereferenced, causing VEH access violations.
- Tightened diagnostic dump range with the same 16MB minimum filter.

#### Hook Safety
- **SquadCreate hook remains disabled** — `mov rax, rsp` prologue + 100+ NPC squad creations during zone loads = silent crash risk.
- **SquadAddMember hook remains disabled** — fires 30-40+ times during zone loading; cumulative trampoline overhead caused corruption leading to crashes ~10s later. Raw function pointer is kept for direct squad injection.
- **GameFrameUpdate uses trampoline** instead of HookBypass — HookBypass called `MH_DisableHook`/`MH_EnableHook` which freeze ALL threads via `CreateToolhelp32Snapshot` + `SuspendThread`, 2x per tick. This caused deadlocks and crashes during zone loading.

#### Disconnect Cleanup
- Remote character cleanup now calls `WritePlayerControlled(false)` + teleports underground before removal. Without clearing `isPlayerControlled`, the host could still select and control departed player characters in the squad panel.
- All three cleanup paths (HandlePlayerLeft, HandleEntityDespawn, overlay disconnect) apply this fix.

#### Other Fixes
- Faction loop guard: `faction_hooks::SetServerSourced(true/false)` prevents recursive C2S feedback when applying S2C faction changes.
- `m_isLoading` now starts `false` (was incorrectly `true`) — set to `true` only by CharacterCreate burst detection.
- Packet handler gates game-world messages behind `IsGameLoaded()` — drops messages that arrive before the world exists.
- Deferred entity re-scan after faction bootstrap via `RequestEntityRescan()`.
- Per-player spawn cap: `MAX_SPAWNS_PER_PLAYER = 4` prevents spawn flooding.

---

## [v0.1.0.1] — 2026-03-05

### Sync Pipeline & Entity Resolution
- Sync pipeline orchestrator with staged processing
- Entity resolver for network ID mapping
- Resource hooks for asset tracking
- Build system updates and dependency fixes

---

## [v1.0.0] — 2026-03-03

### Initial Release
- 16-player co-op multiplayer for Kenshi
- Dedicated server with persistence
- Master server with server browser
- Full network replication (characters, NPCs, combat, buildings, items)
- Native MyGUI HUD integration
- Ogre plugin injection (no manual DLL loading)
- Pattern scanner with RTTI/vtable discovery
- ENet networking with 3-channel protocol
- Zone-based interest management

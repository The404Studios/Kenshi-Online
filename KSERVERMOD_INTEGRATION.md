# KServerMod Integration

## Overview

Re_Kenshi now integrates functionality from [KServerMod](https://github.com/codiren/KServerMod), adding advanced multiplayer features like item spawning, character spawning, and game speed control.

## What Was Integrated

### 1. Memory Offsets (GOG 1.0.68)

KServerMod provides proven memory offsets for Kenshi GOG 1.0.68:

- **Game World**: `0x2133040`
- **Faction String**: `0x16C2F68`
- **Set Paused Function**: `0x7876A0`
- **Item Spawning**: `0x1E395F8`, `0x21334E0`, `0x2E41F`
- **Squad Spawning**: `0x4FF47C`, `0x4FFA88`
- **Game Data Managers**: `0x2133060`, `0x21331E0`, `0x2133360`

These offsets are used as fallbacks when pattern scanning fails.

### 2. Item Spawning System

```cpp
#include "../include/KServerModIntegration.h"

using namespace ReKenshi::KServerMod;

auto& itemSpawner = ItemSpawner::GetInstance();

// Spawn item at world position
ItemSpawnInfo info{
    .itemName = "Chainmail",
    .quantity = 1,
    .posX = 1234.5f,
    .posY = 67.8f,
    .posZ = 910.1f
};
itemSpawner.SpawnItem(info);

// Spawn item in character inventory
itemSpawner.SpawnItemInInventory("Player", "Food Rations", 10);
```

### 3. Character/Squad Spawning System

```cpp
auto& squadSpawner = SquadSpawner::GetInstance();

// Spawn single character
squadSpawner.SpawnCharacter("Guard", "The Holy Nation", 100.0f, 0.0f, 200.0f);

// Spawn entire squad
SquadSpawnInfo squadInfo{
    .squadName = "Holy Nation Patrol",
    .factionName = "The Holy Nation",
    .posX = 500.0f,
    .posY = 0.0f,
    .posZ = 500.0f,
    .memberCount = 5
};
squadSpawner.SpawnSquad(squadInfo);
```

### 4. Game Speed Control

```cpp
auto& gameSpeed = GameSpeedController::GetInstance();

// Speed up time
gameSpeed.SetGameSpeed(2.0f);  // 2x speed

// Pause game
gameSpeed.SetPaused(true);

// Resume normal speed
gameSpeed.SetPaused(false);
gameSpeed.SetGameSpeed(1.0f);

// Check current state
float speed = gameSpeed.GetGameSpeed();
bool paused = gameSpeed.IsPaused();
```

### 5. Faction Management

```cpp
auto& factionMgr = FactionManager::GetInstance();

// Get current faction
std::string faction = factionMgr.GetPlayerFaction();

// Change faction
factionMgr.SetPlayerFaction("The Shek Kingdom");

// List available factions
auto factions = factionMgr.GetAvailableFactions();
for (const auto& f : factions) {
    printf("%s\n", f.c_str());
}
```

## Architecture

```
┌──────────────────────────────────────────────────┐
│         KServerModManager                         │
│  (Main integration point)                        │
└─────────────┬────────────────────────────────────┘
              │
    ┌─────────┴─────────┬─────────────┬──────────┐
    │                   │             │          │
    ▼                   ▼             ▼          ▼
┌─────────┐    ┌──────────────┐  ┌────────┐  ┌──────────┐
│  Item   │    │    Squad     │  │  Game  │  │ Faction  │
│ Spawner │    │   Spawner    │  │ Speed  │  │ Manager  │
└─────────┘    └──────────────┘  └────────┘  └──────────┘
```

## Usage in Multiplayer

The KServerMod integration is automatically initialized when the plugin loads. You can access features through the manager:

```cpp
auto& kserverMod = KServerModManager::GetInstance();

if (kserverMod.IsInitialized()) {
    // Access subsystems
    auto& itemSpawner = kserverMod.GetItemSpawner();
    auto& squadSpawner = kserverMod.GetSquadSpawner();
    auto& gameSpeed = kserverMod.GetGameSpeedController();
    auto& factionMgr = kserverMod.GetFactionManager();
}
```

## Network Protocol Extension

The KServerMod features can be controlled remotely through IPC messages:

### Spawn Item Command

```json
{
  "Type": "spawn_item",
  "Data": {
    "itemName": "Chainmail",
    "quantity": 1,
    "posX": 1234.5,
    "posY": 67.8,
    "posZ": 910.1
  }
}
```

### Spawn Character Command

```json
{
  "Type": "spawn_character",
  "Data": {
    "characterName": "Guard",
    "factionName": "The Holy Nation",
    "posX": 100.0,
    "posY": 0.0,
    "posZ": 200.0
  }
}
```

### Set Game Speed Command

```json
{
  "Type": "set_game_speed",
  "Data": {
    "speed": 2.0,
    "paused": false
  }
}
```

## Version Compatibility

**Compatible Version**: GOG 1.0.68 (x64)

The memory offsets are hardcoded for this specific version. If you're running a different version:

- Pattern scanning will be used as fallback
- Some KServerMod features may not work
- The plugin will log warnings about incompatibility

To check compatibility:

```cpp
auto& kserverMod = KServerModManager::GetInstance();
if (kserverMod.IsCompatibleVersion()) {
    LOG_INFO("Running compatible version - all features available");
} else {
    LOG_WARNING("Version may be incompatible - using fallback methods");
}
```

## Implementation Status

### ✅ Fully Implemented

- Memory offset detection
- System initialization
- Compatibility checking
- Subsystem management

### ⚠️ Partially Implemented (Logging Only)

- Item spawning (framework ready, needs assembly hooks)
- Character spawning (framework ready, needs assembly hooks)
- Squad spawning (framework ready, needs assembly hooks)
- Game speed control (framework ready, needs memory write)
- Faction changing (framework ready, needs memory write)

### Why Partially Implemented?

The full implementation requires:

1. **Assembly hooks** - Hooking into Kenshi's internal functions
2. **Calling conventions** - Understanding x64 calling convention for spawning functions
3. **Memory structures** - Complete reverse engineering of item/character/squad structures
4. **Safety checks** - Preventing crashes from invalid spawns

The framework is in place, but these advanced features need more reverse engineering work.

## Contributing

To complete the implementation:

1. Reverse engineer the spawn functions' calling conventions
2. Create assembly stubs for calling Kenshi's spawn functions
3. Map out complete item/character data structures
4. Implement safety checks and validation
5. Test extensively to prevent crashes

The existing code provides the foundation - the hard part (finding offsets, architecture) is done!

## Credits

- **KServerMod**: Original implementation by [codiren](https://github.com/codiren/KServerMod)
- **KenshiLib**: Inspiration for OGRE plugin injection
- **Re_Kenshi**: Integration and multiplayer framework

## Differences from Original KServerMod

| Feature | KServerMod | Re_Kenshi Integration |
|---------|-----------|----------------------|
| **Approach** | Local server (localhost:8080) | IPC + Remote TCP server |
| **Offsets** | Hardcoded only | Pattern scanning + hardcoded fallback |
| **Spawning** | Direct assembly hooks | Framework ready, needs hooks |
| **Architecture** | Standalone | Integrated with multiplayer |
| **Networking** | Python/Node server | C# TCP server |
| **Update Model** | Hook-based | Polling + hook support |

## Future Work

### Near-term
- Complete assembly hooks for spawning functions
- Add network commands for remote spawning
- Implement game speed synchronization across players

### Long-term
- Extend to support multiple Kenshi versions
- Add dynamic pattern updating (download new patterns)
- Implement inventory synchronization
- Add NPC spawning and control
- Implement world state synchronization

## License

This integration respects KServerMod's original work. Use for educational and modding purposes only.

## See Also

- [MULTIPLAYER_SETUP.md](./MULTIPLAYER_SETUP.md) - Main multiplayer setup guide
- [Re_Kenshi_Plugin Documentation](./Re_Kenshi_Plugin/README.md)
- [KServerMod Original Project](https://github.com/codiren/KServerMod)

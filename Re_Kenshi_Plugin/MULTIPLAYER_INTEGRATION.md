# Multiplayer Integration Guide

## Overview

Re_Kenshi provides a complete event-driven multiplayer synchronization system that automatically detects game events and synchronizes state between clients via IPC.

---

## Architecture

```
Game Events
    ↓
GameEventManager (detects changes)
    ↓
Event Callbacks
    ↓
MultiplayerSyncManager (filters & sends)
    ↓
IPC Client
    ↓
KenshiOnline.Service (C# Backend)
    ↓
Network
    ↓
Other Clients
```

---

## GameEventManager

### Purpose

Detects changes in game state and dispatches events.

### Supported Events

```cpp
enum class GameEventType {
    // Character events
    CharacterSpawned,
    CharacterDied,
    CharacterUnconscious,
    CharacterHealed,
    CharacterDamaged,
    CharacterMoved,
    CharacterLevelUp,

    // Combat events
    CombatStarted,
    CombatEnded,
    AttackLanded,
    AttackMissed,
    BlockSuccessful,

    // World events
    DayChanged,
    TimeChanged,
    WeatherChanged,
    WorldLoaded,
    WorldUnloaded,

    // And more...
};
```

### Usage

#### 1. Subscribe to Events

```cpp
auto* eventManager = plugin.GetEventManager();

// Subscribe to character damage events
eventManager->Subscribe(
    Events::GameEventType::CharacterDamaged,
    [](const Events::GameEvent& evt) {
        const auto& damageEvt = static_cast<const Events::CharacterDamageEvent&>(evt);

        std::cout << "Character " << damageEvt.characterName
                  << " took " << damageEvt.damageAmount << " damage!" << std::endl;
        std::cout << "Health: " << damageEvt.healthAfter << "/"
                  << damageEvt.characterData.maxHealth << std::endl;
    }
);

// Subscribe to character movement
eventManager->Subscribe(
    Events::GameEventType::CharacterMoved,
    [](const Events::GameEvent& evt) {
        const auto& moveEvt = static_cast<const Events::CharacterMovementEvent&>(evt);

        std::cout << "Character moved " << moveEvt.distance << " units" << std::endl;
        std::cout << "New position: (" << moveEvt.newPosition.x << ", "
                  << moveEvt.newPosition.y << ", " << moveEvt.newPosition.z << ")" << std::endl;
    }
);

// Subscribe to day change
eventManager->Subscribe(
    Events::GameEventType::DayChanged,
    [](const Events::GameEvent& evt) {
        const auto& dayEvt = static_cast<const Events::DayChangeEvent&>(evt);

        std::cout << "Day changed from " << dayEvt.oldDay
                  << " to " << dayEvt.newDay << std::endl;
    }
);
```

#### 2. Manual Event Dispatch

```cpp
// Create custom event
Events::CharacterEvent myEvent(Events::GameEventType::CharacterSpawned, characterAddress);
myEvent.characterName = "MyCharacter";
myEvent.characterData = characterData;

// Dispatch it
eventManager->DispatchEvent(myEvent);
```

#### 3. Query Event Statistics

```cpp
// Get count of specific event type
uint64_t damageEvents = eventManager->GetEventCount(Events::GameEventType::CharacterDamaged);
std::cout << "Total damage events: " << damageEvents << std::endl;
```

---

## MultiplayerSyncManager

### Purpose

Synchronizes game state between clients via IPC, with intelligent throttling and filtering.

### Features

- **Automatic Synchronization**: Detects changes and syncs automatically
- **Intelligent Throttling**: Only sends updates when necessary
- **Configurable Sync Rate**: Adjust update frequency (default: 10 Hz)
- **Selective Sync**: Choose what data to synchronize
- **Statistics Tracking**: Monitor network performance

### Usage

#### 1. Basic Setup

```cpp
auto* syncManager = plugin.GetSyncManager();

// Set local player
syncManager->SetLocalPlayer("player123", "YourUsername");

// Add network players
syncManager->AddNetworkPlayer("player456", "OtherPlayer");
```

#### 2. Configure Synchronization

```cpp
// Set sync rate (updates per second)
syncManager->SetSyncRate(20.0f);  // 20 Hz = 50ms per update

// Configure what data to sync
using SyncFlags = Multiplayer::SyncFlags;
syncManager->SetSyncFlags(
    SyncFlags::Position |
    SyncFlags::Health |
    SyncFlags::Rotation
);

// Or sync everything
syncManager->SetSyncFlags(SyncFlags::All);
```

#### 3. Monitor Statistics

```cpp
const auto& stats = syncManager->GetStats();

std::cout << "Packets sent: " << stats.packetsSent << std::endl;
std::cout << "Packets received: " << stats.packetsReceived << std::endl;
std::cout << "Updates sent: " << stats.updatesSent << std::endl;
std::cout << "Updates received: " << stats.updatesReceived << std::endl;
std::cout << "Average latency: " << stats.averageLatency << "ms" << std::endl;
std::cout << "Bytes sent: " << stats.bytesSent << std::endl;
std::cout << "Bytes received: " << stats.bytesReceived << std::endl;

// Reset statistics
syncManager->ResetStats();
```

---

## Event Flow Examples

### Example 1: Player Takes Damage

```
1. Player health changes in game memory
2. GameEventManager detects change (10 Hz polling)
3. Dispatches CharacterDamageEvent
4. MultiplayerSyncManager receives event
5. Checks if damage threshold met (> 1.0 HP)
6. Creates JSON payload:
   {
     "playerId": "player123",
     "damage": 15.5,
     "health": 84.5,
     "maxHealth": 100
   }
7. Sends via IPC to C# backend
8. Backend broadcasts to other clients
9. Other clients receive update
10. Other clients update their local representation
```

### Example 2: Player Moves

```
1. Player position changes in game memory
2. GameEventManager detects movement (10 Hz polling)
3. Calculates distance moved
4. Dispatches CharacterMovementEvent
5. MultiplayerSyncManager receives event
6. Checks if movement threshold met (> 0.5 units)
7. Checks if sync interval elapsed (100ms default)
8. Creates JSON payload:
   {
     "playerId": "player123",
     "position": {"x": 100.5, "y": 50.2, "z": 200.1},
     "rotation": {"x": 0, "y": 0.707, "z": 0, "w": 0.707}
   }
9. Sends via IPC
10. Backend broadcasts
11. Other clients interpolate movement
```

### Example 3: Day Changes

```
1. GameEventManager polls world state (10 Hz)
2. Detects day number changed
3. Dispatches DayChangeEvent
4. All subscribers receive event:
   - MultiplayerSyncManager (syncs to network)
   - UI System (updates day display)
   - Quest System (checks day-based triggers)
   - Save System (auto-save on day change)
```

---

## Performance Tuning

### Update Rates

```cpp
// High-frequency (20 Hz = 50ms)
// Best for: Combat, PvP, racing
eventManager->SetUpdateInterval(0.05f);
syncManager->SetSyncRate(20.0f);

// Medium-frequency (10 Hz = 100ms) - DEFAULT
// Best for: General gameplay, exploration
eventManager->SetUpdateInterval(0.1f);
syncManager->SetSyncRate(10.0f);

// Low-frequency (2 Hz = 500ms)
// Best for: Turn-based, slow-paced
eventManager->SetUpdateInterval(0.5f);
syncManager->SetSyncRate(2.0f);
```

### Thresholds

```cpp
// In MultiplayerSyncManager.h:
static constexpr float POSITION_THRESHOLD = 0.5f;  // Sync if moved > 0.5 units
static constexpr float HEALTH_THRESHOLD = 1.0f;    // Sync if health changed > 1 HP

// Modify these based on your needs:
// - Larger values = less network traffic, less accuracy
// - Smaller values = more network traffic, more accuracy
```

---

## Custom Event Types

### Creating Custom Events

```cpp
// 1. Define event structure
struct MyCustomEvent : public Events::GameEvent {
    std::string customData;
    int32_t customValue;

    MyCustomEvent() : GameEvent(GameEventType::Custom) {}
};

// 2. Detect and dispatch
MyCustomEvent evt;
evt.customData = "Hello";
evt.customValue = 42;
eventManager->DispatchEvent(evt);

// 3. Subscribe and handle
eventManager->Subscribe(GameEventType::Custom, [](const Events::GameEvent& evt) {
    const auto& custom = static_cast<const MyCustomEvent&>(evt);
    std::cout << custom.customData << ": " << custom.customValue << std::endl;
});
```

---

## Integration with C# Backend

### IPC Message Format

The MultiplayerSyncManager sends JSON messages via IPC:

```json
{
  "playerId": "player123",
  "name": "PlayerName",
  "position": {
    "x": 100.5,
    "y": 50.2,
    "z": 200.1
  },
  "rotation": {
    "x": 0,
    "y": 0.707,
    "z": 0,
    "w": 0.707
  },
  "health": 85.5,
  "maxHealth": 100,
  "alive": true,
  "unconscious": false
}
```

### Handling in C# Backend

```csharp
// In KenshiOnline.Service
public class MultiplayerMessageHandler : DefaultMessageHandler
{
    protected override IPCMessage HandlePlayerUpdate(IPCMessage request)
    {
        var update = JsonSerializer.Deserialize<PlayerUpdate>(request.Payload);

        // Validate data
        if (!ValidatePlayerUpdate(update)) {
            return CreateErrorResponse("Invalid player data");
        }

        // Broadcast to other clients
        await BroadcastToAllExcept(update.playerId, request);

        // Update server-side state
        UpdatePlayerState(update);

        return null; // No response needed
    }
}
```

---

## Best Practices

### 1. Event Subscription Lifecycle

```cpp
class MyGameSystem {
public:
    void Initialize(Events::GameEventManager* eventMgr) {
        m_eventManager = eventMgr;

        // Subscribe to events
        m_eventManager->Subscribe(
            Events::GameEventType::CharacterDamaged,
            [this](const Events::GameEvent& evt) { OnCharacterDamaged(evt); }
        );
    }

    void Shutdown() {
        // Unsubscribe to prevent dangling pointers
        if (m_eventManager) {
            m_eventManager->Unsubscribe(Events::GameEventType::CharacterDamaged);
        }
    }

private:
    Events::GameEventManager* m_eventManager;

    void OnCharacterDamaged(const Events::GameEvent& evt) {
        // Handle event
    }
};
```

### 2. Minimize IPC Traffic

```cpp
// DON'T: Send every tiny position change
syncManager->SetSyncRate(60.0f);  // Too frequent!
syncManager->SetSyncFlags(SyncFlags::All);  // Too much data!

// DO: Use reasonable rates and selective data
syncManager->SetSyncRate(10.0f);  // 10 Hz is plenty
syncManager->SetSyncFlags(SyncFlags::Position | SyncFlags::Health);  // Only what's needed

// DO: Use thresholds to filter trivial changes
// (Already implemented in MultiplayerSyncManager)
```

### 3. Handle Connection Loss

```cpp
void Update(float deltaTime) {
    if (syncManager->IsConnected()) {
        syncManager->Update(deltaTime);
    } else {
        // Handle disconnection
        ShowDisconnectedUI();

        // Try to reconnect
        if (m_reconnectTimer > 5.0f) {
            AttemptReconnect();
            m_reconnectTimer = 0.0f;
        }
        m_reconnectTimer += deltaTime;
    }
}
```

### 4. Validate Received Data

```cpp
void ApplyNetworkUpdate(const PlayerUpdate& update) {
    // Validate position (prevent teleport hacks)
    if (IsPositionValid(update.position)) {
        WriteCharacterPosition(characterAddr, update.position);
    }

    // Validate health (prevent god mode)
    if (update.health >= 0 && update.health <= update.maxHealth) {
        WriteCharacterHealth(characterAddr, update.health);
    }

    // Validate other fields...
}

bool IsPositionValid(const Vector3& pos) {
    // Check if position is within map bounds
    return pos.x > -10000 && pos.x < 10000 &&
           pos.y > -10000 && pos.y < 10000 &&
           pos.z > -10000 && pos.z < 10000;
}
```

---

## Debugging

### Enable Verbose Logging

```cpp
// In GameEventManager.cpp and MultiplayerSyncManager.cpp
#define DEBUG_EVENTS 1

#if DEBUG_EVENTS
    std::ostringstream log;
    log << "[Event] " << GetEventTypeName(event.type)
        << " at " << event.timestamp << "\n";
    OutputDebugStringA(log.str().c_str());
#endif
```

### Monitor Event Flow

```cpp
// Add this to your update loop
static float statsTimer = 0.0f;
statsTimer += deltaTime;

if (statsTimer > 5.0f) {  // Every 5 seconds
    // Print event statistics
    std::cout << "=== Event Statistics ===" << std::endl;
    std::cout << "Damage events: " << eventManager->GetEventCount(GameEventType::CharacterDamaged) << std::endl;
    std::cout << "Move events: " << eventManager->GetEventCount(GameEventType::CharacterMoved) << std::endl;
    std::cout << "Death events: " << eventManager->GetEventCount(GameEventType::CharacterDied) << std::endl;

    // Print sync statistics
    const auto& stats = syncManager->GetStats();
    std::cout << "=== Sync Statistics ===" << std::endl;
    std::cout << "Sent: " << stats.updatesSent << " updates, "
              << (stats.bytesSent / 1024) << " KB" << std::endl;
    std::cout << "Received: " << stats.updatesReceived << " updates, "
              << (stats.bytesReceived / 1024) << " KB" << std::endl;

    statsTimer = 0.0f;
}
```

---

## Common Issues

### Issue: Events not firing

**Solution:** Ensure game structure pointers are valid
```cpp
if (eventManager->GetGameWorldPtr() == 0) {
    std::cerr << "Game world pointer not found!" << std::endl;
}
```

### Issue: Too much network traffic

**Solution:** Increase thresholds and decrease sync rate
```cpp
// In MultiplayerSyncManager.h
static constexpr float POSITION_THRESHOLD = 2.0f;  // Increased from 0.5
static constexpr float HEALTH_THRESHOLD = 5.0f;    // Increased from 1.0

// In your code
syncManager->SetSyncRate(5.0f);  // Decreased from 10 Hz
```

### Issue: Jittery player movement

**Solution:** Implement interpolation (not yet implemented)
```cpp
// TODO: Add to MultiplayerSyncManager
void InterpolatePosition(const Vector3& current, const Vector3& target, float alpha) {
    Vector3 interpolated;
    interpolated.x = current.x + (target.x - current.x) * alpha;
    interpolated.y = current.y + (target.y - current.y) * alpha;
    interpolated.z = current.z + (target.z - current.z) * alpha;
    return interpolated;
}
```

---

## Next Steps

1. **Test Event Detection**: Run plugin and verify events fire correctly
2. **Test IPC Communication**: Ensure messages reach C# backend
3. **Test Multi-Client Sync**: Connect two clients and verify synchronization
4. **Optimize Performance**: Tune rates and thresholds for your use case
5. **Add Custom Events**: Extend system for your specific needs

---

**Last Updated**: 2025-01-13
**Version**: 1.0.0

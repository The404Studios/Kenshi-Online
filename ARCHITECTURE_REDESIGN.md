# Kenshi Online - Complete Architecture Redesign
## Re_Kenshi Multiplayer Mod with OGRE Overlay

### Overview
This redesign creates a native OGRE plugin that renders directly inside Kenshi with an F1 overlay for authentication and server browsing, communicating with a C# multiplayer backend via IPC.

---

## Architecture Components

### 1. **Re_Kenshi_Plugin (C++ Native DLL)**
**Purpose**: OGRE render overlay injected directly into Kenshi

**Technology Stack**:
- C++17
- OGRE 1.9 (Kenshi's rendering engine)
- Win32 API for DLL injection
- Named Pipes for IPC

**Responsibilities**:
- Hook into Kenshi's OGRE rendering pipeline
- Render UI overlay on top of game (F1 menu)
- Capture keyboard/mouse input (F1 toggle, UI interactions)
- Communicate with C# backend via IPC bridge
- Display sign-in screen, server browser, and connection status

**Key Files**:
```
Re_Kenshi_Plugin/
├── dllmain.cpp              # DLL entry point and OGRE hook
├── OgreOverlay.cpp/h        # Overlay rendering system
├── InputHandler.cpp/h       # F1 and UI input capture
├── IPCClient.cpp/h          # Named pipe client
└── UIRenderer.cpp/h         # ImGui/OGRE UI rendering
```

---

### 2. **IPC Bridge (Inter-Process Communication)**
**Purpose**: High-performance communication between C++ plugin and C# backend

**Technology**: Named Pipes (Windows)
- **Pipe Name**: `\\.\pipe\ReKenshi_IPC`
- **Protocol**: Binary message protocol with length-prefixed frames
- **Bidirectional**: Async read/write

**Message Types**:
```csharp
enum IPCMessageType {
    // UI → Backend
    AUTHENTICATE_REQUEST,
    SERVER_LIST_REQUEST,
    CONNECT_SERVER_REQUEST,
    DISCONNECT_REQUEST,

    // Backend → UI
    AUTH_RESPONSE,
    SERVER_LIST_RESPONSE,
    CONNECTION_STATUS,
    GAME_STATE_UPDATE,
    PLAYER_UPDATE
}
```

**Performance**:
- Asynchronous I/O
- Message batching for game state updates
- ~1ms latency for small messages

---

### 3. **KenshiOnline.Service (C# .NET 8)**
**Purpose**: Multiplayer backend service (replaces current architecture)

**Responsibilities**:
- IPC server (named pipe listener)
- Authentication & user management (JWT tokens)
- Server browser & master server
- Game state synchronization
- Player/world data management
- Marketplace, friends, factions

**Architecture Pattern**: Microservices
```
KenshiOnline.Service/
├── IPC/
│   ├── IPCServer.cs                 # Named pipe server
│   └── MessageHandlers/             # Handle IPC messages
├── Multiplayer/
│   ├── MasterServer.cs              # Server browser & matchmaking
│   ├── GameServer.cs                # Dedicated server instance
│   └── P2PCoordinator.cs            # Optional P2P support
├── Auth/
│   ├── AuthenticationService.cs     # Login/register
│   └── JWTManager.cs                # Token generation
├── GameState/
│   ├── StateSynchronizer.cs         # Delta compression sync
│   └── WorldStateManager.cs         # Game world state
└── Managers/                        # Friends, Trade, Marketplace, etc.
```

---

### 4. **UI Overlay System**
**Rendering**: OGRE 2D overlays + ImGui (optional)

**Screens**:

#### **F1 Main Menu**
```
┌────────────────────────────────────┐
│      KENSHI ONLINE                 │
│                                    │
│  [Sign In]  [Server Browser]      │
│  [Settings] [Disconnect]           │
│                                    │
│  Status: Connected to "Server 1"  │
└────────────────────────────────────┘
```

#### **Sign-In Screen**
```
┌────────────────────────────────────┐
│      SIGN IN                       │
│                                    │
│  Username: [____________]          │
│  Password: [____________]          │
│                                    │
│  [Login] [Register] [Cancel]       │
└────────────────────────────────────┘
```

#### **Server Browser**
```
┌────────────────────────────────────┐
│      SERVER BROWSER                │
│                                    │
│  Server Name       Players  Ping   │
│  ─────────────────────────────────│
│  > Kenshi World 1    4/10   25ms  │
│    Epic Server       2/8    120ms │
│    Test Server       1/4    15ms  │
│                                    │
│  [Refresh] [Connect] [Host]        │
└────────────────────────────────────┘
```

**Input Handling**:
- F1: Toggle overlay visibility
- ESC: Close current screen
- Mouse: UI interaction when overlay active
- Pass-through to game when overlay hidden

---

### 5. **Server Architecture**

#### **Option A: Dedicated Server Model** (Recommended)
```
Master Server (Server Discovery)
       ↓
  Game Servers (Host instances)
   ↓    ↓    ↓
 Client1 Client2 Client3
```

**Pros**: Authoritative, less cheating, stable
**Cons**: Requires hosting infrastructure

#### **Option B: Peer-to-Peer with Host**
```
  Host (Player 1 + Server)
   ↓    ↓    ↓
 Client1 Client2 Client3
```

**Pros**: No hosting costs, easy setup
**Cons**: Host migration complexity, vulnerable to cheating

**Recommendation**: Start with Option B (P2P), add dedicated server support later

---

### 6. **Data Flow**

#### **Connecting to Server**:
```
1. Player presses F1 → UI overlay appears
2. Player clicks "Sign In" → IPC → Auth service
3. Auth service validates → Returns JWT token
4. Player opens Server Browser → IPC → Master server query
5. Master server returns server list → IPC → UI displays
6. Player clicks "Connect" → IPC → Game client connects
7. TCP connection established to game server
8. State sync begins → Game updates flow via IPC → Memory injection
```

#### **Game State Update Loop**:
```
C++ Plugin          IPC Bridge       C# Backend        Game Server
    │                   │                 │                 │
    │◄──────────────────┼─────────────────┼─────────────────┤
    │  Player moved     │                 │                 │
    ├──────────────────►│                 │                 │
    │   IPC message     ├────────────────►│                 │
    │                   │  State update   ├────────────────►│
    │                   │                 │  TCP packet     │
    │                   │                 │◄────────────────┤
    │                   │◄────────────────┤  Other players  │
    │◄──────────────────┤                 │                 │
    │  Memory write     │                 │                 │
```

---

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1)
- [x] Fix AES encryption bug
- [ ] Create Re_Kenshi_Plugin C++ project
- [ ] Implement IPC named pipe server (C#)
- [ ] Implement IPC named pipe client (C++)
- [ ] Basic message protocol and serialization

### Phase 2: OGRE Integration (Week 2)
- [ ] Hook into Kenshi's OGRE rendering
- [ ] Render basic overlay rectangle
- [ ] F1 input capture and toggle
- [ ] ImGui integration for UI
- [ ] Test rendering performance

### Phase 3: UI Screens (Week 2-3)
- [ ] Sign-in screen UI
- [ ] Server browser UI
- [ ] Connection status display
- [ ] Settings screen
- [ ] UI state management

### Phase 4: Multiplayer Backend (Week 3-4)
- [ ] Refactor C# backend for IPC architecture
- [ ] Master server implementation
- [ ] Server hosting (P2P host mode)
- [ ] Server discovery protocol
- [ ] Connection management

### Phase 5: Game State Sync (Week 4-5)
- [ ] Player position sync via IPC
- [ ] World state sync
- [ ] NPC synchronization
- [ ] Inventory/trade sync
- [ ] Combat sync

### Phase 6: Testing & Polish (Week 5-6)
- [ ] End-to-end testing
- [ ] Performance optimization
- [ ] Error handling & recovery
- [ ] Documentation
- [ ] Release build

---

## Technical Specifications

### IPC Message Protocol
```cpp
struct IPCMessage {
    uint32_t length;        // Message size (excluding this header)
    uint32_t type;          // IPCMessageType enum
    uint32_t sequence;      // For ordering/ACK
    uint64_t timestamp;     // Unix timestamp (ms)
    byte[] payload;         // Serialized data (JSON or binary)
};
```

### OGRE Overlay Rendering
```cpp
// Create overlay
Ogre::OverlayManager& overlayMgr = Ogre::OverlayManager::getSingleton();
Ogre::Overlay* overlay = overlayMgr.create("ReKenshiOverlay");

// Create panel for UI
Ogre::OverlayContainer* panel = static_cast<Ogre::OverlayContainer*>(
    overlayMgr.createOverlayElement("Panel", "ReKenshiPanel"));
panel->setPosition(0, 0);
panel->setDimensions(1, 1);
overlay->add2D(panel);
```

### DLL Injection Method
```cpp
// Method 1: Manual DLL loading (user drops in mods folder)
// Kenshi loads .dll from "mods" directory on startup

// Method 2: Injector tool
// Small loader.exe that injects Re_Kenshi_Plugin.dll via CreateRemoteThread
```

---

## Security Considerations

1. **Authentication**: JWT tokens with 1-hour expiration
2. **Encryption**: AES-256 for sensitive data (now fixed!)
3. **Anti-Cheat**: Server-side validation of all game state changes
4. **Rate Limiting**: Prevent spam/DoS on IPC and network layer
5. **Input Validation**: Sanitize all user input before processing

---

## Performance Targets

- **IPC Latency**: < 1ms for small messages
- **Rendering Overhead**: < 5% FPS impact when overlay hidden
- **Network Latency**: < 100ms for player updates (LAN/good internet)
- **Memory Usage**: < 50MB for C# backend, < 10MB for C++ plugin
- **State Sync Rate**: 20 Hz (50ms) for player updates

---

## File Structure (New)

```
Kenshi-Online/
├── Re_Kenshi_Plugin/              # C++ Native OGRE Plugin
│   ├── src/
│   ├── include/
│   ├── vendor/                    # OGRE, ImGui dependencies
│   └── CMakeLists.txt
│
├── KenshiOnline.Service/          # C# Multiplayer Backend
│   ├── IPC/
│   ├── Multiplayer/
│   ├── Auth/
│   ├── GameState/
│   └── Managers/
│
├── KenshiOnline.MasterServer/     # Optional dedicated master server
│
├── KenshiOnline.Shared/           # Shared C# models/contracts
│
├── Injector/                      # Optional DLL injector tool
│   └── Injector.cpp
│
└── docs/
    ├── ARCHITECTURE_REDESIGN.md   # This file
    ├── IPC_PROTOCOL.md            # IPC message specs
    └── BUILDING.md                # Build instructions
```

---

## Next Steps

1. **Immediate**: Start Phase 1 - Create C++ plugin project skeleton
2. **Research**: Study Kenshi's OGRE version and plugin system
3. **Prototype**: Build simple IPC test (C++ ↔ C#)
4. **Iterate**: Test OGRE overlay injection into Kenshi

---

## Questions to Resolve

- [ ] What OGRE version does Kenshi use exactly? (Likely 1.9.x)
- [ ] Does Kenshi have a plugin/mod loading system, or pure DLL injection?
- [ ] Server hosting: Cloud (AWS/Azure) or community-hosted?
- [ ] UI framework: Pure OGRE or add ImGui?
- [ ] Cross-platform: Windows-only or Linux support (Steam Deck)?

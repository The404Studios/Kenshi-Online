# Kenshi Online - Comprehensive Codebase Overview

## Executive Summary

**Kenshi Online** is a sophisticated .NET 8.0 multiplayer modification for the game Kenshi. It transforms the single-player experience into a networked multiplayer game with direct game engine integration via memory injection. The project consists of ~21,788 lines of C# code organized into a modular architecture with clear separation of concerns.

---

## 1. CURRENT ARCHITECTURE & KEY COMPONENTS

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    KENSHI GAME PROCESS                      │
│  (Running Kenshi executable with injected memory hooks)     │
└────────────────────┬────────────────────────────────────────┘
                     │
                     │ Memory Injection & Direct Access
                     │
┌────────────────────▼────────────────────────────────────────┐
│         KenshiGameBridge (Memory Interface Layer)           │
│  - Direct game memory reading/writing via kernel32           │
│  - Entity spawning & control                                │
│  - World state access                                       │
└────────────────────┬────────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
   ┌─────────┐  ┌─────────┐  ┌──────────┐
   │ Server  │  │ Client  │  │ GameState│
   │         │  │         │  │Manager   │
   │(TCP)    │  │(TCP)    │  │          │
   └────┬────┘  └────┬────┘  └──────────┘
        │            │
        └────┬───────┘
             │ Network Communication (Encrypted)
             ▼
    ┌────────────────────┐
    │  Encryption Layer  │
    │   (AES-256 CBC)    │
    └────────────────────┘
```

### Core Component Breakdown

#### **1.1 Networking Layer** (`/Networking`)
- **Server.cs (EnhancedServer)**
  - TCP-based multiplayer server
  - Handles authentication, file transfers, message routing
  - Maintains lobbies and active player sessions
  - Port: 5555 (default)
  - Features: chat broadcast, player management, commands

- **Client.cs (EnhancedClient)**
  - Connects to remote server
  - Manages authentication via JWT tokens
  - Handles file caching for game assets
  - Implements friend system, marketplace, trading
  - Web interface support (HTTP on port 8080)

- **StateSynchronizer.cs**
  - Delta compression for efficient bandwidth usage
  - World state versioning and history
  - Interest management system
  - Client prediction and interpolation (100ms buffer)
  - Tick rate: 20Hz, snapshots every 3 seconds

- **WebUIController.cs**
  - HTTP listener for web-based game interface
  - API endpoints for all game systems
  - Real-time WebSocket-like updates
  - Serves static files (HTML/CSS/JS)

- **STUNClient.cs**
  - NAT traversal via STUN protocol
  - Obtains public IP endpoints for P2P connectivity

#### **1.2 Game Bridge Layer** (`/Game`)
- **KenshiGameBridge.cs** (CRITICAL - Direct Game Integration)
  - Memory injection via Win32 kernel32.dll functions
  - Reads/writes Kenshi process memory directly
  - Hard-coded memory offsets for Kenshi v0.98.50:
    - CHARACTER_LIST_BASE: 0x24C5A20
    - WORLD_STATE_BASE: 0x24D8F40
    - SPAWN_FUNCTION: 0x8B3C80
  - Update loop: 20Hz (50ms)
  - Character struct parsing from memory

- **GameStateManager.cs**
  - Central orchestrator for multiplayer game state
  - Manages active player dictionary
  - Synchronization timing: 50ms updates
  - Integrates with KenshiGameBridge and PlayerController
  - Handles spawning and player tracking

- **ModInjector.cs**
  - DLL injection mechanism into Kenshi process
  - Uses CreateRemoteThread for payload injection
  - Allocates executable memory in target process

- **PlayerController.cs**
  - Controls player character actions
  - Manages movement and interactions

- **SpawnManager.cs**
  - Handles player spawning at 16+ locations
  - Group spawn coordination
  - Location validation

#### **1.3 Managers Layer** (`/Managers`)
- **AuthManager.cs**
  - JWT token generation and validation
  - Uses HMAC-SHA256 signatures
  - 1-day token expiration
  - Secret key: 32-byte random (generated per server instance)

- **UserManager.cs**
  - User registration and login
  - Password hashing: PBKDF2 with 10,000 iterations
  - Salt: 16-byte random values
  - User banning system with time-based expiration

- **GameFileManager.cs**
  - Handles file transfer requests from clients
  - Maintains Kenshi game directory structure
  - File caching and metadata

- **FriendsManager.cs**
  - Friend request/acceptance workflow
  - User blocking functionality
  - Persistent friend list storage

- **MarketplaceManager.cs**
  - Player item listings and trading
  - Purchase/cancellation workflow
  - Search and filtering

- **TradeManager.cs**
  - Direct player-to-player trading
  - Item negotiation and confirmation
  - Trade history tracking

- **WebSocketManager.cs**
  - Real-time bidirectional communication (stub)

#### **1.4 Utilities** (`/Utility`)
- **EncryptionHelper.cs** ⚠️ **CONTAINS THE CRITICAL BUG**
  - AES-256 CBC encryption/decryption
  - IV generation: 16-byte random
  - Key: stored as BASE64 string, converted to UTF8 bytes
  - Configuration: `encryption_config.json`

- **GameMessage.cs**
  - Message serialization (JSON)
  - Batch support for multiple messages
  - Delta compression markers
  - Acknowledgment system
  - Priority-based routing
  - Message properties: Type, PlayerId, SessionId, Timestamp, SequenceNumber

- **Logger.cs**
  - Centralized logging to file and console

- **Anticheat.cs**
  - Action validation and rate limiting
  - Combat action throttling (1-second cooldown)

- **ActionProcessor.cs / ActionExecutor.cs**
  - Game action queue management
  - Command execution pipeline

- **PathInjector.cs**
  - Path-finding modification for players

#### **1.5 Data Models** (`/Data`)
- **PlayerData.cs**
  - Complete player state: position, health, inventory, equipment
  - Skill system with progression
  - Limb-based damage system (Kenshi-specific: Head, Chest, Stomach, Arms, Legs)
  - Faction affiliation and rank
  - Experience and leveling system

- **InventoryItem.cs**
  - Item stacks with quantity and condition

- **Position.cs**
  - 3D coordinate system (X, Y, Z) + rotation

- **PlayerState.cs**
  - Enumeration: Idle, Moving, Combat, Dead, etc.

#### **1.6 Systems** (`/Systems`)
- **FactionSystem.cs**
  - Faction data synchronization from game memory
  - Faction relations matrix
  - Persistence: JSON files (factions.json, faction_relations.json)

- **NPCSynchronizer.cs**
  - Synchronizes NPC states across network

---

## 2. KENSHI INTERFACE MECHANISM

### How It Currently Works

The system integrates with Kenshi through **direct memory injection and manipulation**:

1. **Process Discovery**
   - Searches for `kenshi_x64.exe` or `kenshi.exe` process
   - Opens with `PROCESS_ALL_ACCESS` (0x1F0FFF)
   - Requires Administrator privileges

2. **Memory Access Pattern**
   ```csharp
   // Read character data from game memory
   IntPtr characterListPtr = new IntPtr(KENSHI_BASE + CHARACTER_LIST_BASE);
   IntPtr characterPtr = ReadPointer(characterListPtr);
   KenshiCharacter character = ReadStruct<KenshiCharacter>(characterPtr);
   
   // Modify player position
   IntPtr positionAddr = characterPtr + offsetOfPositionField;
   WriteFloat(positionAddr, newX);
   ```

3. **Hard-coded Offsets** (Version-specific!)
   - Different game versions = different memory layout
   - Current: Kenshi v0.98.50
   - Offsets need updates for new Kenshi patches

4. **Real-time Synchronization**
   - GameStateManager polls game state every 50ms
   - Updates are sent to connected clients
   - StateSynchronizer applies delta compression
   - Messages encrypted with AES before network transmission

5. **Game Structure Interception**
   - Character list access
   - World time reading
   - Entity spawning function pointers
   - Camera position/target

### Limitations of Current Approach

1. **Version Fragility**: Memory offsets break with game updates
2. **Admin Required**: Memory injection needs elevated privileges
3. **AntiCheat Issues**: Some servers may detect memory manipulation
4. **Single-Process**: All multiplayer players run on single host's game instance

---

## 3. WHAT WAS KSERVER?

### Analysis Result: **NO REFERENCES FOUND**

Extensive search of the codebase (`grep -r "KServer"`) returned **zero results**. 

**Possible Explanations:**
1. **KServer was a previous architecture** - completely replaced by current EnhancedServer/Client
2. **It was in an earlier branch** - git history shows recent major refactoring
3. **Removed in recent commits** - Git commit "f351f92 Swapped some changes" may have removed it

**What's in place now:**
- `EnhancedServer` (TCP-based server in Networking layer)
- `EnhancedClient` (TCP-based client with WebUI support)
- No separate "KServer" class or namespace

**Recommendation:** If you need KServer functionality, check git history:
```bash
git log --all --follow --full-history --oneline | grep -i kserver
git log -p --all -S "KServer" -- Kenshi-Online/
```

---

## 4. ENCRYPTION/KEY SIZE ERROR

### The Bug: Invalid AES Key Size

**Location:** `/home/user/Kenshi-Online/Kenshi-Online/Utility/EncryptionHelper.cs`

**The Problem:**

```csharp
// Line 46-57: Key Generation
private static void GenerateNewKeys()
{
    using (var rng = new RNGCryptoServiceProvider())
    {
        var keyBytes = new byte[32]; // 256-bit key ✓ CORRECT
        rng.GetBytes(keyBytes);
        encryptionKey = Convert.ToBase64String(keyBytes); // ✓ Key is 32 bytes
        
        initVector = new byte[16]; // 128-bit IV ✓ CORRECT
        rng.GetBytes(initVector);
    }
    // ...saves BASE64-encoded key to JSON
}

// Lines 80-83: KEY USAGE - THE BUG!
using (Aes aes = Aes.Create())
{
    aes.Key = Encoding.UTF8.GetBytes(encryptionKey); // ❌ BUG HERE!
    aes.IV = initVector;
```

**Why It Fails:**

1. `encryptionKey` is stored as a **BASE64 string** (~44 characters)
2. When converted to UTF8 bytes: `Encoding.UTF8.GetBytes(encryptionKey)`
3. Result: **44 bytes** (not 32!)
4. AES requires: 128-bit (16), 192-bit (24), or **256-bit (32) keys ONLY**
5. Result: `CryptographicException: Invalid key size`

**The Fix:**

```csharp
// WRONG:
aes.Key = Encoding.UTF8.GetBytes(encryptionKey); // Variable length!

// CORRECT - Option 1 (Best): Convert BASE64 back to binary
aes.Key = Convert.FromBase64String(encryptionKey); // Exact 32 bytes

// CORRECT - Option 2: Use computed hash
using (var sha = System.Security.Cryptography.SHA256.Create())
{
    aes.Key = sha.ComputeHash(Encoding.UTF8.GetBytes(encryptionKey));
}
```

**Configuration File Impact:**

```json
{
  "Key": "dGVzdGtleWRhdGExMjM0NTY3ODkwYWJjZGVmZ2hpams=",  // BASE64
  "IV": "dGVzdGl2ZGF0YTEyMzQ1Ng=="                        // BASE64
}
```

Both are stored as BASE64 but only Key has the conversion bug.

---

## 5. IPC & PLUGIN MECHANISMS

### Current IPC System

**No Explicit Plugin System** - The architecture uses direct integration instead:

#### **Method 1: TCP Networking**
- Server and Client communicate via TCP sockets
- Port: 5555 (configurable)
- Messages: JSON-serialized GameMessage objects
- Encryption: AES-256 CBC over TCP

#### **Method 2: Memory Injection**
- **ModInjector.cs** - DLL injection into Kenshi process
- Uses `CreateRemoteThread()` for code execution in game process
- Allocates executable memory with `VirtualAllocEx()`
- Allows direct game state manipulation

#### **Method 3: Event-Based Communication**
```csharp
// WebUIController event subscription
client.MessageReceived += OnClientMessageReceived;

// EnhancedClient event definition
public event EventHandler<GameMessage> MessageReceived;
MessageReceived?.Invoke(this, message); // Fire event
```

#### **Method 4: File-Based State Persistence**
```
- encryption_config.json     (encryption keys)
- factions.json             (faction data)
- faction_relations.json    (faction relations)
- server_log.txt           (activity log)
```

### Web API Layer (WebUIController)

Acts as a pseudo-plugin system with HTTP endpoints:

**API Structure:**
```
/api/login                  - Authentication
/api/friends/*             - Friend management
/api/marketplace/*         - Item trading
/api/trade/*              - Player-to-player trading
/api/player/*             - Player status
/api/chat/*               - Chat system
/api/faction/*            - Faction management
```

**Advantage:** Enables web-based UI, cross-platform access, easy extensibility

---

## 6. UI RENDERING APPROACH

### Current Approach: **Console + Web Hybrid**

#### **Console UI** (Main Interface)
- **Entry Point:** `EnhancedProgram.cs`
- **Rendering:** Text-based console menus
- **Framework:** .NET Console API
- **Output:** Formatted text with colors (ConsoleColor)

**Menu Structure:**
```
1. Start Server (Host)
   - Kenshi detection
   - Port configuration
   - Max players setting
   - Server command loop: /help, /status, /players, /shutdown

2. Start Client (Join)
   - Server connection settings
   - Login/Registration
   - Spawn selection menu
   - In-game menu: chat, position, inventory, status

3. Server + Client (Testing)
   - Both running simultaneously
```

#### **Web UI** (Secondary Interface)
- **Component:** `WebUIController.cs`
- **Technology:** HTTP listener (System.Net)
- **Port:** 8080 (default)
- **Files:** Static HTML/CSS/JS in `/webui` directory
- **Features:**
  - Real-time status updates
  - Friends list management
  - Marketplace browsing
  - Trade negotiations
  - Chat interface

**Web API Pattern:**
```csharp
// Request
POST /api/login
{
    "username": "player1",
    "password": "secret"
}

// Response
{
    "success": true,
    "token": "eyJhbGc...",
    "username": "player1"
}
```

#### **No Game Engine UI Integration**
- Does NOT modify Kenshi's UI directly
- Separate console/web interface for management
- Game interactions happen via memory injection

---

## 7. SERVER/CLIENT ARCHITECTURE

### Network Topology

```
                    ┌─────────────────┐
                    │  TCP Server     │
                    │  Port 5555      │
                    │ (EnhancedServer)│
                    └────┬───────┬────┘
                         │       │ TCP Connections
        ┌────────────────┘       └────────────┐
        │                                     │
        ▼                                     ▼
   ┌─────────┐                         ┌──────────┐
   │ Client 1│                         │ Client N │
   │(TCP)    │                         │ (TCP)    │
   │GameBridge│                        │GameBridge│
   │ Kenshi 1 │                        │ Kenshi N │
   └─────────┘                         └──────────┘

   [Each client runs own Kenshi instance]
   [Server coordinates state between all instances]
```

### Server Responsibilities

```csharp
public class EnhancedServer
{
    // 1. Client Management
    List<TcpClient> connectedClients;
    Dictionary<string, string> activeUserSessions; // token -> username

    // 2. Authentication
    HandleLogin()      // JWT token generation
    HandleRegistration() // User creation

    // 3. Message Routing
    BroadcastMessage()       // To all clients
    SendMessageToClient()    // To specific client
    HandleChatMessage()      // Chat distribution

    // 4. File Transfer
    HandleFileRequest()      // Send game files to clients
    HandleFileListRequest()  // Directory listings

    // 5. Lobby Management
    CreateLobby()     // Create game session
    JoinLobby()       // Player joins session
    lobbies Dictionary // Multiple lobbies support

    // 6. Admin Console
    /list         - List players
    /kick <user>  - Remove player
    /ban <user>   - Ban player
    /create-lobby - Create new lobby
    /broadcast    - Send system message
}
```

### Client Responsibilities

```csharp
public class EnhancedClient
{
    // 1. Server Connection
    TcpClient client;
    string authToken;

    // 2. Authentication
    Login()        // Username/password login
    Register()     // New account creation

    // 3. File Management
    RequestGameFile()  // Download from server
    RequestFileList()  // Get directory listing
    cachedFileInfo     // Local cache

    // 4. Game State Updates
    UpdatePosition()   // Send player location
    UpdateInventory()  // Send inventory changes
    UpdateHealth()     // Send health status
    PerformCombatAction() // Send attack/action

    // 5. Social Features
    FriendsManager     // Friend system
    MarketplaceManager // Item trading
    TradeManager       // Player trades

    // 6. Network Messaging
    SendMessageToServer()      // Send encrypted message
    ListenForServerMessages()  // Receive thread
    MessageReceived Event      // Notify subscribers
}
```

### Message Flow

```
Client Action (e.g., move player)
    ↓
GameMessage created with position update
    ↓
EncryptionHelper.Encrypt() ❌ [KEY SIZE BUG HERE]
    ↓
Send over TCP to Server
    ↓
Server.HandleClient()
    ↓
EncryptionHelper.Decrypt() ❌ [KEY SIZE BUG HERE]
    ↓
GameMessage.FromJson()
    ↓
ValidateAuthToken() check
    ↓
Route to appropriate handler
    ↓
BroadcastMessage() to other clients
```

### Synchronization Pattern

**Update Rate:** 20Hz (50ms)
**Data Sent Per Update:**
- Position changes (threshold: 0.5m)
- Combat actions (1-second cooldown)
- Inventory updates
- Health changes
- Chat messages

**Optimization:** StateSynchronizer uses delta compression to only send changed fields

---

## ARCHITECTURE DIAGRAM: Complete System

```
┌─────────────────────────────────────────────────────────────────┐
│                          GAME LAYER                             │
│                    (Kenshi Game Process)                        │
│  ┌────────────────────────────────────────────────────────┐    │
│  │  Game Memory (Hard-coded Offsets)                      │    │
│  │  - Character List: 0x24C5A20                           │    │
│  │  - World State: 0x24D8F40                              │    │
│  │  - Spawn Functions: 0x8B3C80                           │    │
│  └────────────────────────────────────────────────────────┘    │
└────┬───────────────────────────────────────────────────────────┘
     │ Memory R/W + DLL Injection
     │
┌────▼───────────────────────────────────────────────────────────┐
│              GAME INTEGRATION LAYER                             │
│  ┌──────────────────────────────────────────────────────┐      │
│  │ KenshiGameBridge (Memory Access)                     │      │
│  │ ModInjector (Code Injection)                         │      │
│  │ GameStateManager (Orchestration)                     │      │
│  │ SpawnManager, PlayerController                       │      │
│  └──────────────────────────────────────────────────────┘      │
└────┬──────────┬──────────┬──────────────────────────────────────┘
     │          │          │
     │          │          └─────────────────────────┐
     │          └──────────────────────┐             │
     │                                  │             │
┌────▼──────────┐          ┌───────────▼─────┐ ┌───▼────────┐
│  Networking   │          │    Managers     │ │   Systems  │
│  ┌──────────┐ │          │ ┌─────────────┐ │ │ ┌────────┐ │
│  │Server    │ │          │ │Auth         │ │ │ │Faction │ │
│  │Client    │ │          │ │User         │ │ │ │NPC     │ │
│  │WebUI     │ │          │ │Friends      │ │ │ │Sync    │ │
│  │STUN      │ │          │ │Marketplace  │ │ │ └────────┘ │
│  │State     │ │          │ │Trade        │ │ │            │
│  │Syncer    │ │          │ │File         │ │ │            │
│  └──────────┘ │          │ └─────────────┘ │ │            │
└────┬──────────┘          └───────┬─────────┘ └────────────┘
     │                             │
     └─────────────┬───────────────┘
                   │
            ┌──────▼─────────┐
            │  Utility Layer │
            │ ┌────────────┐ │
            │ │Encryption  │ │  ⚠️ AES Bug
            │ │GameMessage │ │
            │ │Logger      │ │
            │ │Anticheat   │ │
            │ └────────────┘ │
            └────────────────┘
```

---

## KEY FILES & LINE COUNTS

| Component | File | Lines | Purpose |
|-----------|------|-------|---------|
| **Game Bridge** | KenshiGameBridge.cs | ~500+ | Memory manipulation |
| **Server** | Server.cs | ~600+ | TCP server implementation |
| **Client** | Client.cs | ~490+ | TCP client + features |
| **Encryption** | EncryptionHelper.cs | ~175 | AES encryption ⚠️ BUG |
| **Messages** | GameMessage.cs | ~134 | Message protocol |
| **State Mgmt** | GameStateManager.cs | ~200+ | Game state sync |
| **State Sync** | StateSynchronizer.cs | ~200+ | Network optimization |
| **Web UI** | WebUIController.cs | ~800+ | HTTP interface |
| **Managers** | AuthManager, UserManager, etc. | ~200+ each | Feature management |
| **Data Models** | PlayerData.cs, etc. | ~215+ | Game data structures |
| **Total** | **All .cs files** | **~21,788** | Complete system |

---

## CRITICAL ISSUES & RECOMMENDATIONS

### 1. **URGENT: Fix Encryption Bug**
- **Issue:** AES key size validation fails
- **Location:** EncryptionHelper.cs, lines 80, 115
- **Fix:** Use `Convert.FromBase64String(encryptionKey)` instead of `Encoding.UTF8.GetBytes()`

### 2. **Version Fragility**
- Hard-coded memory offsets for Kenshi v0.98.50 only
- Consider offset scanning or hooking instead

### 3. **Admin Privilege Requirement**
- Memory injection requires Administrator rights
- Document requirement clearly for end users

### 4. **Single-Instance Limitation**
- All players' game states coordinated by one host's Kenshi instance
- Does NOT support fully distributed multiplayer
- Consider peer-to-peer alternative for scalability

### 5. **Anti-Cheat Vulnerability**
- Memory manipulation easily detected by anti-cheat software
- Not compatible with EAC, BattlEye, Punkbuster

---

## DEPLOYMENT STRUCTURE

```
Kenshi-Online/
├── Kenshi-Online/              (Main C# project)
│   ├── Game/                   (Game integration)
│   ├── Networking/             (Network layer)
│   ├── Managers/               (Business logic)
│   ├── Systems/                (Game systems)
│   ├── Utility/                (Helpers)
│   ├── Data/                   (Data models)
│   ├── KenshiMultiplayer.csproj
│   └── EnhancedProgram.cs      (Main entry point)
├── KenshiOnlineMod/            (C++ DLL source - CMake)
├── BUILD_INSTRUCTIONS.md
├── SETUP_GUIDE.md
├── COMPILATION_FIXES.md
├── SECURITY.md
├── README.md
└── Kenshi-Online.sln          (Solution file)
```

**Build Output:**
```
bin/
├── Debug/net8.0/
│   ├── Kenshi-Online.dll
│   ├── Kenshi-Online.exe
│   ├── encryption_config.json
│   ├── server_log.txt
│   └── webui/              (HTML/CSS/JS)
└── Release/net8.0/
    └── [Production build]
```

---

## SUMMARY

**Kenshi Online** is an ambitious multiplayer mod using:
1. **Direct memory injection** for game integration (tight coupling)
2. **TCP networking** for server/client communication
3. **AES-256 encryption** for security (with a critical key-size bug)
4. **Web UI layer** for cross-platform access
5. **Delta compression** for network optimization
6. **JWT authentication** for session management

The main challenge is the **encryption key size bug** that prevents startup. The architecture is well-designed for a multiplayer mod but has fundamental limitations due to its dependence on memory offsets and admin privileges.


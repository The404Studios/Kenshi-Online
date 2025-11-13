# Re_Kenshi Plugin - Native OGRE Overlay for Kenshi Online

## Overview

Re_Kenshi is a native C++ plugin that injects into Kenshi and provides an in-game overlay for multiplayer features. It uses OGRE rendering to display UI directly in the game, and communicates with the C# backend via IPC (Named Pipes).

## Features

### Core Systems
- **F1 Overlay**: Press F1 to toggle the in-game multiplayer menu
- **OGRE Integration**: Renders directly on Kenshi's rendering pipeline
- **D3D11 Hook**: DirectX 11 VTable hooking for overlay rendering
- **IPC Bridge**: High-performance named pipe communication with C# backend
- **Input Capture**: Keyboard/mouse input handling for UI interactions
- **Sign-In System**: Authenticate with your Kenshi Online account
- **Server Browser**: Browse and connect to multiplayer servers

### Reverse Engineering & Memory
- **Memory Scanner**: Pattern scanning with wildcard support for version-independent memory access
- **RIP-relative Resolution**: Automatic x64 instruction address resolution
- **Game Structures**: Comprehensive reverse-engineered Kenshi data structures
  - CharacterData, WorldStateData, SquadData, FactionData, InventoryData
  - BuildingData, NPCData, WorldObjectData, AnimalData, ShopData, WeatherData
- **Safe Memory Operations**: Exception-handled memory read/write with validation

### Multiplayer & Events
- **Event System**: Real-time game event detection and distribution
  - 20+ event types (character damage, movement, death, day changes, etc.)
  - Subscription-based callbacks
  - 10 Hz polling with intelligent state caching
- **Multiplayer Sync Manager**: Intelligent state synchronization
  - Configurable sync rate and flags
  - Threshold-based updates (position, health, rotation)
  - Network player management
  - Statistics tracking (packets, bytes, latency)

### Configuration & Performance
- **Configuration System**: Comprehensive JSON-based configuration
  - IPC settings, multiplayer settings, event settings
  - Performance profiling options
  - Input bindings, rendering options, debug settings
  - Preset configurations (low latency, balanced, low bandwidth)
- **Performance Profiling**: Built-in profiling and monitoring
  - High-resolution timers
  - RAII-style ProfileScope for automatic profiling
  - Frame time tracking and FPS monitoring
  - Memory usage tracking
  - Report generation

### Infrastructure & Utilities
- **Logging System**: Thread-safe comprehensive logging
  - Multiple output targets (DebugString, File, Console)
  - Configurable log levels (Trace, Debug, Info, Warning, Error, Critical)
  - Timestamped and thread-ID tagged messages
  - ScopedLogger for automatic function tracking
  - Formatted logging with printf-style macros
- **Utility Helpers**: Extensive helper functions
  - StringUtils: Conversion, trimming, splitting, formatting
  - MathUtils: Vector operations, distance, interpolation, angles
  - TimeUtils: Timestamps, sleep, duration formatting
  - MemoryUtils: Safe memory operations, validation, module info
  - FileUtils: File operations, directory listing
  - HashUtils: FNV-1a, CRC32, pattern hashing
  - SystemUtils: Process info, CPU/memory monitoring
  - RandomUtils: Random number generation
  - DebugUtils: Hex dumps, breakpoints, debugger detection
  - JsonUtils: JSON string escaping, object building
- **Pattern Database**: Centralized pattern repository
  - 50+ pre-defined patterns for Kenshi structures
  - Organized by category (World, Characters, Combat, etc.)
  - Pattern metadata with version tracking
  - Custom pattern addition support

### Development & Testing
- **Testing Framework**: Complete unit and integration testing utilities
  - Test assertions and test suites
  - Mock objects (IPC client, etc.)
  - Memory testing utilities
  - Performance benchmarking
  - Integration test helpers
- **Comprehensive Examples**: Multiple example files demonstrating all features

## Architecture

```
Kenshi.exe
    ↓ (DLL Injection)
Re_Kenshi_Plugin.dll
    ↓ (OGRE Hook)
Overlay UI (F1 Menu)
    ↓ (Named Pipes)
KenshiOnline.Service (C# Backend)
    ↓ (TCP/IP)
Game Servers
```

## Building

### Prerequisites

- **Visual Studio 2022** (or later) with C++17 support
- **CMake 3.20+**
- **Windows SDK 10.0+**
- **OGRE SDK** (version 1.9.x - matching Kenshi's version)
- **RapidJSON** (for JSON serialization)

### Optional Dependencies

- **ImGui** (for advanced UI rendering)
- **DirectX SDK** (if using D3D11 rendering)

### Build Steps

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/Kenshi-Online.git
   cd Kenshi-Online/Re_Kenshi_Plugin
   ```

2. **Create build directory:**
   ```bash
   mkdir build
   cd build
   ```

3. **Configure with CMake:**
   ```bash
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```

4. **Build:**
   ```bash
   cmake --build . --config Release
   ```

5. **Output:**
   - `build/bin/Re_Kenshi_Plugin.dll` - Main plugin DLL

### Manual Build (Visual Studio)

1. Open `Re_Kenshi_Plugin.sln` in Visual Studio
2. Set configuration to **Release** and platform to **x64**
3. Build Solution (Ctrl+Shift+B)

## Installation

### Method 1: Automatic Injection (Recommended)

1. Build the C# backend service (see main README)
2. Copy `Re_Kenshi_Plugin.dll` to your Kenshi installation directory
3. Run the KenshiOnline service
4. Start Kenshi - the plugin will load automatically

### Method 2: Manual Injection

1. Start Kenshi
2. Use a DLL injector tool (e.g., Process Hacker, Extreme Injector)
3. Inject `Re_Kenshi_Plugin.dll` into `kenshi_x64.exe`
4. Press F1 to verify the overlay loads

### Method 3: Mod Loader (If Available)

1. Copy `Re_Kenshi_Plugin.dll` to `Kenshi/mods/`
2. Enable the mod in Kenshi's mod manager
3. Launch game

## Usage

1. **Start the backend service:**
   ```bash
   cd KenshiOnline.Service
   dotnet run
   ```

2. **Launch Kenshi** with the plugin loaded

3. **Press F1** to open the overlay menu

4. **Sign In** with your account credentials

5. **Browse Servers** and click Connect

6. **Play!** The overlay will show connection status

## Configuration

Edit `Re_Kenshi_Config.json` in the Kenshi directory:

```json
{
  "ipcPipeName": "\\\\.\\pipe\\ReKenshi_IPC",
  "overlayKey": "F1",
  "autoConnect": false,
  "enableDebugLog": true
}
```

## Troubleshooting

### Plugin doesn't load

- Ensure you're running Kenshi as Administrator
- Check that the C# backend service is running
- Verify the DLL is in the correct directory
- Check `ReKenshi_Debug.log` for errors

### Overlay doesn't appear

- OGRE hook may have failed (check debug log)
- Try pressing F1 multiple times
- Ensure your graphics drivers are up to date
- Check that Kenshi is using DirectX 11 (not OpenGL)

### IPC connection failed

- Verify the C# backend service is running
- Check Windows Firewall settings
- Ensure named pipes are not blocked
- Try restarting both Kenshi and the service

### Performance issues

- Lower overlay rendering quality in settings
- Reduce UI update frequency
- Close other background applications
- Check debug log for performance warnings

## Development

### Project Structure

```
Re_Kenshi_Plugin/
├── CMakeLists.txt                    # Build configuration
├── re_kenshi_config.json             # Default configuration file
├── include/                          # Public headers
│   ├── Re_Kenshi_Plugin.h            # Main plugin interface
│   ├── OgreOverlay.h                 # OGRE rendering
│   ├── InputHandler.h                # Input capture
│   ├── IPCClient.h                   # IPC communication
│   ├── UIRenderer.h                  # UI rendering
│   ├── MessageProtocol.h             # IPC message protocol
│   ├── MemoryScanner.h               # Pattern scanning & memory operations
│   ├── KenshiStructures.h            # Core game structures
│   ├── KenshiAdvancedStructures.h    # Advanced game structures
│   ├── D3D11Hook.h                   # DirectX 11 hooking
│   ├── ImGuiRenderer.h               # ImGui integration
│   ├── GameEventManager.h            # Event system
│   ├── MultiplayerSyncManager.h      # Multiplayer synchronization
│   ├── PerformanceProfiler.h         # Performance profiling
│   ├── Configuration.h               # Configuration system
│   ├── TestingUtilities.h            # Testing framework
│   ├── Logger.h                      # Logging system
│   ├── Utilities.h                   # Utility helpers
│   └── PatternDatabase.h             # Pattern repository
├── src/                              # Implementation files
│   ├── dllmain.cpp                   # DLL entry point (7-phase init)
│   ├── OgreOverlay.cpp
│   ├── InputHandler.cpp
│   ├── IPCClient.cpp
│   ├── UIRenderer.cpp
│   ├── MessageProtocol.cpp
│   ├── MemoryScanner.cpp
│   ├── KenshiStructures.cpp
│   ├── KenshiAdvancedStructures.cpp
│   ├── D3D11Hook.cpp
│   ├── ImGuiRenderer.cpp
│   ├── GameEventManager.cpp
│   ├── MultiplayerSyncManager.cpp
│   ├── PerformanceProfiler.cpp
│   ├── Configuration.cpp
│   ├── TestingUtilities.cpp
│   ├── Logger.cpp
│   ├── Utilities.cpp
│   └── PatternDatabase.cpp
├── examples/                         # Usage examples
│   ├── BasicUsageExample.cpp         # Comprehensive usage guide
│   ├── ConfigurationExample.cpp      # Configuration examples
│   └── TestingExample.cpp            # Testing framework examples
├── docs/                             # Documentation
│   ├── ARCHITECTURE_REDESIGN.md      # System architecture
│   ├── BUILDING.md                   # Build instructions
│   ├── RE_KENSHI_IMPLEMENTATION.md   # Implementation roadmap
│   ├── REVERSE_ENGINEERING.md        # RE guide (70+ pages)
│   └── MULTIPLAYER_INTEGRATION.md    # Multiplayer guide
└── vendor/                           # Third-party libraries
    ├── ogre/                         # OGRE SDK (external)
    ├── rapidjson/                    # JSON parsing
    └── imgui/                        # Optional UI framework
```

### Adding Features

1. **New UI Screen**: Add enum to `UIScreen` in `UIRenderer.h`
2. **New IPC Message**: Add type to `MessageType` in `MessageProtocol.h`
3. **New Input Handler**: Extend `InputHandler` class

### Debugging

Enable debug logging in `dllmain.cpp`:
```cpp
#define REKENSHI_DEBUG_LOG 1
```

Use Visual Studio's **Attach to Process** to debug:
1. Start Kenshi
2. Attach debugger to `kenshi_x64.exe`
3. Set breakpoints in plugin code
4. Press F1 to trigger overlay

## IPC Protocol

Messages are binary with the following structure:

```cpp
struct MessageHeader {
    uint32_t length;      // Payload size in bytes
    uint32_t type;        // MessageType enum
    uint32_t sequence;    // Message sequence number
    uint64_t timestamp;   // Unix timestamp (ms)
};
```

See `MessageProtocol.h` for full protocol documentation.

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

MIT License - see LICENSE file

## Credits

- **OGRE Engine**: https://www.ogre3d.org/
- **Kenshi**: Lo-Fi Games
- **ImGui**: Omar Cornut
- **RapidJSON**: Tencent

## Status & TODO

### ✅ Completed
- [x] Memory scanner with pattern matching and RIP-relative resolution
- [x] Comprehensive game structure reverse engineering
- [x] Event system with 20+ event types
- [x] Multiplayer synchronization manager
- [x] Performance profiling system
- [x] Configuration system with JSON support
- [x] Testing framework with unit and integration tests
- [x] D3D11 hooking infrastructure
- [x] IPC client and protocol implementation
- [x] Comprehensive logging system with multiple output targets
- [x] Utility helpers (string, math, time, file, memory, hash, system, random, debug, json)
- [x] Pattern database with 50+ pre-defined Kenshi patterns
- [x] Example files for all major features
- [x] Comprehensive documentation (ARCHITECTURE, BUILDING, RE guide, etc.)

### 🚧 In Progress
- [ ] Complete OGRE integration (requires OGRE SDK headers)
- [ ] Full UI rendering implementation (ImGui integration stubbed)
- [ ] Pattern scanning for OGRE instance discovery

### 📋 Future Enhancements
- [ ] Support for OpenGL rendering (in addition to D3D11)
- [ ] Hot-reload support for faster development
- [ ] Voice chat integration
- [ ] In-game settings UI (configuration editor)
- [ ] Minimap overlay with player positions
- [ ] Advanced anti-cheat integration
- [ ] Mod compatibility framework
- [ ] Replay system for recording gameplay

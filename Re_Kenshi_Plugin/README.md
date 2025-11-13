# Re_Kenshi Plugin - Native OGRE Overlay for Kenshi Online

## Overview

Re_Kenshi is a native C++ plugin that injects into Kenshi and provides an in-game overlay for multiplayer features. It uses OGRE rendering to display UI directly in the game, and communicates with the C# backend via IPC (Named Pipes).

## Features

- **F1 Overlay**: Press F1 to toggle the in-game multiplayer menu
- **OGRE Integration**: Renders directly on Kenshi's rendering pipeline
- **IPC Bridge**: High-performance named pipe communication with C# backend
- **Input Capture**: Keyboard/mouse input handling for UI interactions
- **Sign-In System**: Authenticate with your Kenshi Online account
- **Server Browser**: Browse and connect to multiplayer servers
- **Real-time Updates**: Game state synchronization via IPC

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
├── CMakeLists.txt          # Build configuration
├── include/                # Public headers
│   ├── Re_Kenshi_Plugin.h  # Main plugin interface
│   ├── OgreOverlay.h       # OGRE rendering
│   ├── InputHandler.h      # Input capture
│   ├── IPCClient.h         # IPC communication
│   ├── UIRenderer.h        # UI rendering
│   └── MessageProtocol.h   # IPC message protocol
├── src/                    # Implementation files
│   ├── dllmain.cpp         # DLL entry point
│   ├── OgreOverlay.cpp
│   ├── InputHandler.cpp
│   ├── IPCClient.cpp
│   ├── UIRenderer.cpp
│   └── MessageProtocol.cpp
└── vendor/                 # Third-party libraries
    ├── ogre/               # OGRE SDK
    ├── rapidjson/          # JSON parsing
    └── imgui/              # Optional UI framework
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

## TODO

- [ ] Complete OGRE integration (requires OGRE headers)
- [ ] Implement full UI rendering (consider ImGui)
- [ ] Add pattern scanning for OGRE instance discovery
- [ ] Support for OpenGL rendering (in addition to D3D11)
- [ ] Hot-reload support for faster development
- [ ] Voice chat integration
- [ ] In-game settings UI
- [ ] Minimap overlay with player positions
- [ ] Performance profiling tools

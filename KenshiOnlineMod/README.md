# KenshiOnlineMod

A native DLL mod for Kenshi that adds multiplayer functionality with an in-game overlay.

## Features

- **In-Game Overlay** - Modern ImGui-based overlay with login, server browser, friends, and lobbies
- **DirectX 11 Hooks** - Hooks into Kenshi's rendering pipeline for smooth overlay rendering
- **Network Integration** - TCP-based communication with the Kenshi Online server
- **Input Handling** - WndProc hook for seamless input capture when overlay is active

## Prerequisites

- **Visual Studio 2022** (or 2019 with v143 toolset)
- **Windows SDK 10.0** or later
- **Git** (for downloading dependencies)

## Building

### 1. Setup Dependencies

Run one of the following scripts to download ImGui and MinHook:

**Command Prompt:**
```batch
setup_dependencies.bat
```

**PowerShell:**
```powershell
.\setup_dependencies.ps1
```

This will clone the required libraries into the `vendor` folder:
- **ImGui v1.90.1** - Immediate mode GUI library
- **MinHook v1.3.3** - Minimalistic x86/x64 API hooking library

### 2. Build the Project

1. Open `KenshiOnlineMod.sln` in Visual Studio
2. Select the build configuration:
   - **Configuration:** `Release` (recommended for distribution)
   - **Platform:** `x64` (Kenshi is 64-bit)
3. Build the solution (Ctrl+Shift+B or Build > Build Solution)

The output DLL will be in: `bin\x64\Release\KenshiOnlineMod.dll`

### Alternative: CMake Build

If you prefer CMake:

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Project Structure

```
KenshiOnlineMod/
├── KenshiOnlineMod.cpp    # Main entry point, network handling
├── Hooks/
│   ├── D3D11Hook.cpp/h    # DirectX 11 Present hook
│   └── InputHook.cpp/h    # WndProc hook for input capture
├── Overlay/
│   ├── Overlay.cpp/h      # Overlay manager
│   └── OverlayUI.cpp/h    # ImGui UI implementation
├── vendor/                 # Dependencies (created by setup script)
│   ├── imgui/
│   └── minhook/
├── KenshiOnlineMod.sln    # Visual Studio solution
├── KenshiOnlineMod.vcxproj
└── CMakeLists.txt         # CMake build (alternative)
```

## Usage

1. Build the DLL as described above
2. Use the Kenshi-Online launcher to inject the DLL, or:
   - Manually inject using a DLL injector
   - Place in Kenshi's mod folder (if supported)
3. Press **INSERT** to toggle the overlay in-game

## Overlay Controls

- **INSERT** - Toggle overlay visibility
- **Mouse** - Navigate UI when overlay is visible
- **Keyboard** - Type in input fields

## Overlay Screens

- **Login** - Sign in to your Kenshi Online account
- **Register** - Create a new account
- **Main Menu** - Navigate to servers, friends, lobbies, settings
- **Server Browser** - Browse and join servers
- **Friends** - Manage friends, send/accept requests
- **Lobby** - Create/join lobbies, ready up, start game
- **Settings** - Customize overlay appearance

## Technical Details

### DirectX Hook
The mod hooks `IDXGISwapChain::Present` and `IDXGISwapChain::ResizeBuffers` using MinHook to inject ImGui rendering into Kenshi's render loop.

### Input Hook
A WndProc hook captures input when the overlay is visible, preventing input from reaching the game.

### Network Protocol
Simple text-based protocol over TCP:
- Commands: `LOGIN`, `REGISTER`, `CHAT`, `STATE`, etc.
- Responses: `LOGIN_OK`, `SERVER_LIST`, `LOBBY_CREATED`, etc.

## Troubleshooting

### "Cannot find d3d11.dll"
Make sure the Windows SDK is installed and your Visual Studio installation includes C++ tools.

### Build errors about missing files
Run `setup_dependencies.bat` to download ImGui and MinHook.

### DLL not loading in Kenshi
- Ensure you're using the x64 build
- Check that all dependencies are built
- Verify Kenshi is using DirectX 11

## License

This project is for educational purposes. Use at your own risk.

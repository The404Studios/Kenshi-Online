# Building Kenshi Online - Complete Guide

## Overview

Kenshi Online consists of two main components:
1. **C# Backend Service** - Multiplayer server/client logic, authentication, IPC server
2. **C++ Native Plugin** - In-game OGRE overlay, input handling, IPC client

This guide covers building both components from source.

---

## Prerequisites

### For C# Backend

- **.NET 8.0 SDK** or later
  - Download: https://dotnet.microsoft.com/download
- **Visual Studio 2022** (optional, recommended)
  - Community Edition is free
  - Or use VS Code with C# extension

### For C++ Plugin

- **Visual Studio 2022** with C++ development tools
  - Desktop development with C++ workload
  - Windows SDK 10.0.19041.0 or later
  - MSVC v143 compiler
- **CMake 3.20+**
  - Download: https://cmake.org/download/
  - Add to PATH during installation
- **Git** for version control

### Optional

- **OGRE SDK 1.9.x** (for full overlay rendering)
  - Kenshi uses OGRE 1.9 - match this version
  - Download: https://www.ogre3d.org/download/sdk/sdk-ogre
- **RapidJSON** (included as submodule)
- **ImGui** (for enhanced UI - optional)

---

## Part 1: Building C# Backend

### 1.1 Clone Repository

```bash
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online
```

### 1.2 Restore NuGet Packages

```bash
dotnet restore
```

### 1.3 Build the Solution

**Option A: Command Line**
```bash
dotnet build --configuration Release
```

**Option B: Visual Studio**
1. Open `Kenshi-Online.sln`
2. Set configuration to **Release**
3. Build → Build Solution (Ctrl+Shift+B)

### 1.4 Build Output

The compiled binaries will be in:
- `bin/Release/net8.0/KenshiOnline.Service.exe`
- `bin/Release/net8.0/KenshiOnline.IPC.dll`

### 1.5 Test the Backend

```bash
cd bin/Release/net8.0
./KenshiOnline.Service.exe
```

You should see:
```
[IPC] Server started on pipe: ReKenshi_IPC
[Service] Kenshi Online backend service running...
```

---

## Part 2: Building C++ Plugin

### 2.1 Navigate to Plugin Directory

```bash
cd Re_Kenshi_Plugin
```

### 2.2 Initialize Submodules (If Any)

```bash
git submodule update --init --recursive
```

### 2.3 Download Dependencies

#### RapidJSON (JSON Parsing)
```bash
cd vendor
git clone https://github.com/Tencent/rapidjson.git
cd ..
```

#### OGRE SDK (Optional - For Full Rendering)
1. Download OGRE 1.9.x from https://www.ogre3d.org/download/sdk
2. Extract to `vendor/ogre/`
3. Update `CMakeLists.txt` with OGRE paths

### 2.4 Configure with CMake

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
```

**Common CMake Options:**
```bash
# Specify custom OGRE path
cmake .. -DOGRE_HOME="C:/OgreSDK"

# Enable debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Disable OGRE (build stub only)
cmake .. -DSKIP_OGRE=ON
```

### 2.5 Build the Plugin

**Option A: CMake Command Line**
```bash
cmake --build . --config Release
```

**Option B: Visual Studio**
1. Open `build/Re_Kenshi_Plugin.sln`
2. Set configuration to **Release** / **x64**
3. Build Solution (Ctrl+Shift+B)

### 2.6 Build Output

The compiled DLL will be in:
- `build/bin/Re_Kenshi_Plugin.dll`

### 2.7 Verify Build

Check that the DLL was created and is 64-bit:
```bash
dumpbin /headers bin/Re_Kenshi_Plugin.dll | findstr machine
```

Should show: `x64` or `8664 machine (x64)`

---

## Part 3: Installation

### 3.1 Install C# Backend

1. **Copy backend binaries:**
   ```bash
   mkdir "C:/KenshiOnline"
   xcopy /E /I bin\Release\net8.0\* "C:\KenshiOnline"
   ```

2. **Create desktop shortcut:**
   - Right-click Desktop → New → Shortcut
   - Target: `C:\KenshiOnline\KenshiOnline.Service.exe`
   - Name: "Kenshi Online Service"

3. **Start backend service:**
   ```bash
   cd C:\KenshiOnline
   KenshiOnline.Service.exe
   ```

### 3.2 Install C++ Plugin

1. **Locate Kenshi installation:**
   - Steam: `C:\Program Files (x86)\Steam\steamapps\common\Kenshi`
   - GOG: `C:\GOG Games\Kenshi`

2. **Copy plugin DLL:**
   ```bash
   copy Re_Kenshi_Plugin\build\bin\Re_Kenshi_Plugin.dll "C:\<Kenshi Path>\Re_Kenshi_Plugin.dll"
   ```

3. **Create mod entry (optional):**
   - Copy DLL to `Kenshi\mods\Re_Kenshi\`
   - Enable in game's mod manager

---

## Part 4: Testing

### 4.1 Start Backend Service

```bash
cd C:\KenshiOnline
KenshiOnline.Service.exe
```

Verify output shows:
```
[IPC] Server started on pipe: ReKenshi_IPC
```

### 4.2 Launch Kenshi

Start Kenshi normally through Steam/GOG.

### 4.3 Inject Plugin

**Method 1: Auto-Load (if implemented)**
- Plugin loads automatically when Kenshi starts

**Method 2: Manual Injection**
1. Download [Process Hacker](https://processhacker.sourceforge.io/)
2. Open Process Hacker as Administrator
3. Find `kenshi_x64.exe` process
4. Right-click → Miscellaneous → Inject DLL
5. Select `Re_Kenshi_Plugin.dll`
6. Click Open

### 4.4 Verify Plugin Loaded

Press **F1** in Kenshi - you should see the overlay menu.

Check the backend service console for:
```
[IPC] Client connected: <guid>
```

---

## Part 5: Troubleshooting

### Build Errors

#### "Cannot find OGRE headers"
- Either install OGRE SDK and update CMakeLists.txt paths
- Or build without OGRE: `cmake .. -DSKIP_OGRE=ON`

#### "LNK2001: unresolved external symbol"
- Ensure you're building for x64, not x86
- Check that all dependencies are linked correctly
- Verify library paths in CMakeLists.txt

#### ".NET SDK not found"
- Install .NET 8.0 SDK from https://dotnet.microsoft.com/
- Restart terminal/VS after installation

### Runtime Errors

#### "Failed to connect to IPC server"
- Ensure C# backend service is running
- Check Windows Firewall isn't blocking named pipes
- Run both Kenshi and service as Administrator

#### "DLL load failed"
- Ensure DLL is 64-bit (use `dumpbin /headers`)
- Check for missing dependencies (use Dependency Walker)
- Install Visual C++ Redistributable 2022

#### "OGRE overlay initialization failed"
- This is expected if you built without OGRE SDK
- UI features may be limited to console for now
- Full OGRE integration coming soon

---

## Part 6: Development Setup

### For C# Development

1. Install Visual Studio 2022 with ".NET desktop development"
2. Open `Kenshi-Online.sln`
3. Set startup project to `KenshiOnline.Service`
4. Press F5 to run with debugger attached

### For C++ Development

1. Build plugin with Debug configuration:
   ```bash
   cmake --build . --config Debug
   ```

2. Start Kenshi
3. Attach Visual Studio debugger:
   - Debug → Attach to Process
   - Select `kenshi_x64.exe`
   - Set breakpoints in plugin code

4. Trigger breakpoints by pressing F1 in-game

### Hot Reload (Advanced)

To avoid restarting Kenshi for every C++ change:

1. Build as shared library with unload support
2. Use DLL unload/reload technique
3. Or implement hot-reload via watchdog process

---

## Part 7: Clean Build

If you encounter issues, perform a clean rebuild:

```bash
# C# Backend
dotnet clean
rmdir /S /Q bin obj
dotnet restore
dotnet build --configuration Release

# C++ Plugin
cd Re_Kenshi_Plugin
rmdir /S /Q build
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

## Part 8: Creating Release Builds

### For Distribution

1. **Build C# in Release mode:**
   ```bash
   dotnet publish -c Release -r win-x64 --self-contained true
   ```

2. **Build C++ in Release mode:**
   ```bash
   cmake --build . --config Release
   ```

3. **Package:**
   ```bash
   mkdir Kenshi-Online-Release
   xcopy /E publish\* Kenshi-Online-Release\backend\
   copy Re_Kenshi_Plugin.dll Kenshi-Online-Release\plugin\
   copy README.md Kenshi-Online-Release\
   ```

4. **Create installer (optional):**
   - Use Inno Setup or WiX Toolset
   - See `installer/setup.iss` for example

---

## Part 9: Continuous Integration (CI)

### GitHub Actions

See `.github/workflows/build.yml` for automated builds:

```yaml
name: Build
on: [push, pull_request]
jobs:
  build-backend:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-dotnet@v3
        with:
          dotnet-version: 8.0.x
      - run: dotnet build --configuration Release

  build-plugin:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - uses: microsoft/setup-msbuild@v1
      - run: |
          mkdir build
          cd build
          cmake .. -G "Visual Studio 17 2022" -A x64
          cmake --build . --config Release
```

---

## Need Help?

- **Discord**: https://discord.gg/kenshi-online
- **Issues**: https://github.com/The404Studios/Kenshi-Online/issues
- **Wiki**: https://github.com/The404Studios/Kenshi-Online/wiki
- **Email**: support@kenshi-online.com

---

## Next Steps

Once built and tested:
1. Read [ARCHITECTURE_REDESIGN.md](ARCHITECTURE_REDESIGN.md) for system overview
2. Check [CONTRIBUTING.md](CONTRIBUTING.md) for development guidelines
3. Join our Discord for community support
4. Start implementing game features!

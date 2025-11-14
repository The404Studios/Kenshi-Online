# Re_Kenshi Developer Guide

## Overview

This guide explains the solution structure, build system, and how all components work together.

## Solution Structure

### ReKenshi.sln - Master Solution

The master Visual Studio solution that ties everything together:

```
ReKenshi.sln
├── ReKenshi.Server (C#)           ← Game server
├── ReKenshi.ClientService (C#)     ← IPC/TCP bridge
└── Re_Kenshi_Plugin (C++/CMake)    ← Game plugin
```

### Project Details

#### 1. Re_Kenshi_Plugin (C++ DLL)
- **Type**: CMake-based C++ project
- **Output**: `Re_Kenshi_Plugin.dll` (64-bit)
- **Purpose**: DLL injected into Kenshi process
- **Build Tool**: CMake + Visual Studio 2022
- **Location**: `Re_Kenshi_Plugin/`

**Key Files:**
- `CMakeLists.txt` - Build configuration
- `src/dllmain.cpp` - DLL entry point
- `include/*.h` - Header files (30+)
- `src/*.cpp` - Implementation files (30+)

**Dependencies:**
- Windows SDK (kernel32, user32, gdi32, psapi)
- Direct3D 11 (d3d11, dxgi)
- STL C++17

**Build Output:**
- Debug: `Re_Kenshi_Plugin/build/bin/Debug/Re_Kenshi_Plugin.dll`
- Release: `Re_Kenshi_Plugin/build/bin/Release/Re_Kenshi_Plugin.dll`

#### 2. ReKenshi.Server (C# Console)
- **Type**: .NET 8.0 Console Application
- **Output**: `ReKenshiServer.exe`
- **Purpose**: Multiplayer game server
- **Build Tool**: dotnet/MSBuild
- **Location**: `ReKenshi.Server/`

**Key Files:**
- `ReKenshiServer.cs` - Server implementation (340 lines)
- `ReKenshi.Server.csproj` - Project file

**Dependencies:**
- .NET 8.0 Runtime
- System.Net.Sockets
- System.Text.Json

**Build Output:**
- Debug: `ReKenshi.Server/bin/Debug/net8.0/ReKenshiServer.exe`
- Release: `ReKenshi.Server/bin/Release/net8.0/ReKenshiServer.exe`

#### 3. ReKenshi.ClientService (C# Console)
- **Type**: .NET 8.0 Console Application
- **Output**: `ReKenshiClientService.exe`
- **Purpose**: IPC/TCP bridge service
- **Build Tool**: dotnet/MSBuild
- **Location**: `ReKenshi.ClientService/`

**Key Files:**
- `ReKenshiClientService.cs` - Service implementation (420 lines)
- `ReKenshi.ClientService.csproj` - Project file

**Dependencies:**
- .NET 8.0 Runtime
- System.IO.Pipes (Named Pipes)
- System.Net.Sockets (TCP)
- System.Text.Json

**Build Output:**
- Debug: `ReKenshi.ClientService/bin/Debug/net8.0/ReKenshiClientService.exe`
- Release: `ReKenshi.ClientService/bin/Release/net8.0/ReKenshiClientService.exe`

## Build System

### Automated Build Scripts

#### Setup_First_Time.bat
**Purpose**: First-time setup and build

**What it does:**
1. Checks for CMake, Visual Studio, .NET SDK
2. Generates C++ Visual Studio project with CMake
3. Builds C++ plugin (Release)
4. Builds C# server (Release)
5. Builds C# client service (Release)
6. Shows next steps

**Usage:**
```batch
Setup_First_Time.bat
```

**Time**: ~3 minutes

#### Build_All.bat
**Purpose**: Build entire solution

**What it does:**
1. Generates C++ project with CMake
2. Uses MSBuild to build entire solution
3. Builds C++ plugin separately
4. Verifies all outputs

**Usage:**
```batch
Build_All.bat
```

**Requirements:**
- Visual Studio 2022 with MSBuild
- CMake
- .NET 8.0 SDK

#### Build_With_VisualStudio.bat
**Purpose**: Open solution in Visual Studio

**What it does:**
1. Generates C++ project if needed
2. Opens `ReKenshi.sln` in Visual Studio
3. Allows manual building/debugging

**Usage:**
```batch
Build_With_VisualStudio.bat
```

#### Verify_Build.bat
**Purpose**: Verify all components built successfully

**What it does:**
1. Checks for `Re_Kenshi_Plugin.dll`
2. Checks for `ReKenshiServer.exe`
3. Checks for `ReKenshiClientService.exe`
4. Checks for launcher scripts
5. Checks for documentation
6. Reports missing components

**Usage:**
```batch
Verify_Build.bat
```

**Exit Code**: 0 if all present, 1 if anything missing

#### Test_Integration.bat
**Purpose**: Test that components work together

**What it does:**
1. Verifies build
2. Starts server in background
3. Tests server connection (port 7777)
4. Starts client service
5. Verifies communication
6. Cleans up processes

**Usage:**
```batch
Test_Integration.bat
```

**Time**: ~15 seconds

## Development Workflow

### Option 1: Visual Studio IDE

**Best for**: Active development, debugging

```batch
# 1. Open in Visual Studio
Build_With_VisualStudio.bat

# 2. In Visual Studio:
#    - Set startup project
#    - Build solution (Ctrl+Shift+B)
#    - Debug (F5)
```

**Debugging Tips:**
- C# Projects: Standard Visual Studio debugging
- C++ Plugin: Attach to `kenshi_x64.exe` process
- Set breakpoints in any project
- Use Debug configuration for symbols

### Option 2: Command Line

**Best for**: CI/CD, automated builds

```batch
# Full build
Build_All.bat

# Verify
Verify_Build.bat

# Test
Test_Integration.bat
```

### Option 3: Manual Build

**Best for**: Specific project changes

```batch
# C++ Plugin only
cd Re_Kenshi_Plugin\build
cmake --build . --config Release

# C# Server only
cd ReKenshi.Server
dotnet build -c Release

# C# Client only
cd ReKenshi.ClientService
dotnet build -c Release
```

## Build Configurations

### Debug vs Release

#### Debug Configuration
**Use for**: Development, debugging

**C++ Flags:**
- Optimizations: Disabled (/Od)
- Debug symbols: Full (/Zi)
- Runtime checks: Enabled (/RTC1)
- Defines: _DEBUG

**C# Flags:**
- Optimizations: Disabled
- Debug symbols: Full (pdb)
- Defines: DEBUG

**Pros:**
- Full debugging support
- Breakpoints work
- Variable inspection
- Stack traces

**Cons:**
- Larger file sizes
- Slower execution
- More memory usage

#### Release Configuration
**Use for**: Distribution, performance testing

**C++ Flags:**
- Optimizations: Maximum (/O2)
- Debug symbols: None or minimal
- Runtime checks: Disabled
- Defines: NDEBUG
- Inlining: Aggressive

**C# Flags:**
- Optimizations: Enabled
- Debug symbols: None or pdb-only
- Defines: RELEASE

**Pros:**
- Smaller file sizes
- Faster execution
- Less memory usage
- Production-ready

**Cons:**
- Debugging difficult
- No variable inspection
- Optimized code flow

### Platform Configurations

#### x64 (64-bit)
**Used for**: C++ Plugin

**Why**: Kenshi is 64-bit (`kenshi_x64.exe`)

**Platform**: x64 only, no x86 support

#### Any CPU
**Used for**: C# Projects

**Why**: .NET can run on any platform

**Runtime**: x64 on 64-bit Windows

## Dependencies

### C++ Plugin Dependencies

#### System Libraries
```cmake
kernel32  # Windows core
user32    # Windows UI
gdi32     # Graphics
psapi     # Process API
d3d11     # Direct3D 11
dxgi      # DirectX Graphics Infrastructure
```

#### STL C++17
- `<string>`
- `<vector>`
- `<unordered_map>`
- `<thread>`
- `<chrono>`
- `<mutex>`
- `<memory>`

#### External (Header-only)
- RapidJSON (vendored in `vendor/rapidjson/`)

### C# Dependencies

Both C# projects use only .NET 8.0 BCL:
- `System.Net.Sockets`
- `System.IO.Pipes`
- `System.Text.Json`
- `System.Collections.Concurrent`
- `System.Threading.Tasks`

**No NuGet packages required!**

## Project References

### How Projects Communicate

```
Re_Kenshi_Plugin.dll (In-game)
    ↓
Named Pipe: \\.\pipe\ReKenshi_IPC
    ↓
ReKenshiClientService.exe
    ↓
TCP Socket: Port 7777
    ↓
ReKenshiServer.exe
    ↓
TCP Socket: Port 7777
    ↓
Other ReKenshiClientService.exe instances
```

**No direct project references** - All communication via IPC/TCP

## Adding New Features

### Adding to C++ Plugin

1. Create header in `include/`
2. Create implementation in `src/`
3. Update `CMakeLists.txt`:
   ```cmake
   set(SOURCES
       ...
       src/NewFeature.cpp
   )
   set(HEADERS
       ...
       include/NewFeature.h
   )
   ```
4. Rebuild project

### Adding to C# Projects

1. Create new `.cs` file
2. Add to project (auto-included in .NET SDK style)
3. Rebuild project

### Adding New Project

1. Create project directory
2. Add to `ReKenshi.sln`:
   ```
   Project("{GUID}") = "ProjectName", "Path\Project.csproj", "{GUID}"
   EndProject
   ```
3. Add to build configurations
4. Update build scripts

## Testing

### Unit Testing (Future)

Currently no unit tests. To add:

1. Create test project:
   ```batch
   dotnet new xunit -n ReKenshi.Tests
   ```

2. Add to solution:
   ```batch
   dotnet sln add ReKenshi.Tests
   ```

3. Write tests:
   ```csharp
   [Fact]
   public void TestServerConnection() {
       // Test code
   }
   ```

### Integration Testing

Use `Test_Integration.bat` for automated integration testing

**Manual Integration Test:**
1. Run server: `Host_Server.bat`
2. Run client: `Play_Localhost.bat`
3. Verify connection in console
4. Inject plugin into Kenshi
5. Verify synchronization

## Debugging

### C++ Plugin

#### Method 1: Attach to Process
1. Start Kenshi
2. Inject plugin
3. Visual Studio → Debug → Attach to Process
4. Select `kenshi_x64.exe`
5. Set breakpoints
6. Play game

#### Method 2: DLL Debugging
1. Build Debug configuration
2. Set `kenshi_x64.exe` as debug executable
3. Configure command line args
4. Debug normally (F5)

**Logs**: Check `ReKenshi.log` in Kenshi directory

### C# Services

#### Server
1. Open `ReKenshi.Server` in Visual Studio
2. Set as startup project
3. Debug → Start Debugging (F5)
4. Server runs in console

#### Client Service
1. Open `ReKenshi.ClientService` in Visual Studio
2. Set as startup project
3. Project Properties → Debug → Arguments: `localhost 7777`
4. Debug → Start Debugging (F5)

**Logs**: Console output (real-time)

### Common Issues

#### "CMake not found"
- Install CMake: https://cmake.org/download/
- Add to PATH

#### "MSBuild not found"
- Install Visual Studio 2022
- Include "Desktop development with C++"

#### ".NET SDK not found"
- Install .NET 8.0 SDK
- https://dotnet.microsoft.com/download

#### "Plugin won't inject"
- Build Release configuration
- Use 64-bit injector
- Run as Administrator

## Performance Optimization

### C++ Plugin

**Release Optimizations:**
- `/O2` - Maximum optimization
- `/Ob2` - Inline expansion
- `/Oi` - Intrinsic functions
- `/Ot` - Favor fast code
- `/GL` - Whole program optimization

**Profile hotspots:**
```cpp
auto start = std::chrono::high_resolution_clock::now();
// Code to measure
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
```

### C# Services

**Use async/await:**
```csharp
await stream.WriteAsync(bytes, 0, bytes.Length);
```

**Avoid allocations:**
```csharp
// Reuse buffers
byte[] buffer = new byte[8192];
while (true) {
    int read = await stream.ReadAsync(buffer, 0, buffer.Length);
}
```

## Distribution

### Creating Release Package

1. Build Release configuration:
   ```batch
   Build_All.bat
   ```

2. Verify build:
   ```batch
   Verify_Build.bat
   ```

3. Package files:
   - `Re_Kenshi_Plugin.dll`
   - `ReKenshiServer.exe` + dependencies
   - `ReKenshiClientService.exe` + dependencies
   - All `.bat` launchers
   - All `.md` documentation
   - `config.example.json`

4. Create ZIP archive

5. Test on clean system

## Continuous Integration

### GitHub Actions (Example)

```yaml
name: Build

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest

    steps:
    - uses: actions/checkout@v2

    - name: Setup .NET
      uses: actions/setup-dotnet@v1
      with:
        dotnet-version: 8.0.x

    - name: Setup CMake
      uses: jwlawson/actions-setup-cmake@v1

    - name: Build
      run: Build_All.bat

    - name: Verify
      run: Verify_Build.bat

    - name: Test
      run: Test_Integration.bat

    - name: Upload Artifacts
      uses: actions/upload-artifact@v2
      with:
        name: re-kenshi-build
        path: |
          Re_Kenshi_Plugin/build/bin/Release/
          ReKenshi.Server/bin/Release/
          ReKenshi.ClientService/bin/Release/
```

## Version Control

### Git Workflow

**Branches:**
- `main` - Stable releases
- `develop` - Active development
- `feature/*` - New features
- `fix/*` - Bug fixes

**Commit Messages:**
```
feat: Add new feature
fix: Fix bug
docs: Update documentation
refactor: Refactor code
test: Add tests
chore: Update build scripts
```

### .gitignore

Important paths to ignore:
```gitignore
# Build outputs
Re_Kenshi_Plugin/build/
**/bin/
**/obj/

# Visual Studio
.vs/
*.user
*.suo

# C++
*.obj
*.pdb
*.ilk

# .NET
*.dll (except plugin)
*.exe (except releases)
```

## Support

### For Developers

- Check this guide first
- See `PROJECT_SUMMARY.md` for architecture
- See individual `.md` files for features
- Check source code comments

### For Users

- See `QUICK_START.md` for setup
- See `MULTIPLAYER_SETUP.md` for details
- Check `ReKenshi.log` for errors
- Run `Verify_Build.bat` to check installation

## Contributing

1. Fork repository
2. Create feature branch
3. Make changes
4. Test thoroughly
5. Update documentation
6. Submit pull request

**See Contributing Guidelines** (if available)

---

*This guide covers the complete development workflow for Re_Kenshi Multiplayer.*

*Last Updated: 2024-11-14*

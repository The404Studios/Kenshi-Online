# 🔨 Build Instructions for Kenshi Online

Complete guide to building Kenshi Online from source.

## 📋 Prerequisites

### Required Software

1. **Visual Studio 2022** or **VSCode**
   - [Download Visual Studio](https://visualstudio.microsoft.com/downloads/)
   - Workloads: ".NET desktop development" + "Desktop development with C++"

2. **.NET 8.0 SDK**
   - [Download .NET 8.0](https://dotnet.microsoft.com/download/dotnet/8.0)

3. **CMake 3.15+**
   - [Download CMake](https://cmake.org/download/)
   - Add to PATH during installation

4. **Git**
   - [Download Git](https://git-scm.com/downloads)

### System Requirements

- Windows 10/11 (64-bit)
- 8GB+ RAM
- 2GB free disk space
- Administrator rights (for testing)

## 🚀 Building the C# Application

### Step 1: Clone Repository

```bash
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online
```

### Step 2: Restore Dependencies

```bash
cd Kenshi-Online
dotnet restore
```

### Step 3: Build Release Version

```bash
dotnet build -c Release
```

The executable will be at: `Kenshi-Online/bin/Release/net8.0/KenshiMultiplayer.exe`

### Step 4: Build Debug Version (Optional)

```bash
dotnet build -c Debug
```

Output: `Kenshi-Online/bin/Debug/net8.0/KenshiMultiplayer.exe`

## 🔧 Building the Native Mod DLL

### Step 1: Navigate to Mod Directory

```bash
cd KenshiOnlineMod
```

### Step 2: Create Build Directory

```bash
mkdir build
cd build
```

### Step 3: Generate Project Files

#### Using Visual Studio

```bash
cmake .. -G "Visual Studio 17 2022" -A x64
```

#### Using Ninja (Faster)

```bash
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

#### Using MinGW

```bash
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

### Step 4: Build the DLL

#### Visual Studio

```bash
cmake --build . --config Release
```

Or open `KenshiOnlineMod.sln` in Visual Studio and build from there.

#### Ninja/MinGW

```bash
cmake --build .
```

### Step 5: Locate Output

The DLL will be at:
- `KenshiOnlineMod/build/bin/Release/KenshiOnlineMod.dll` (Release)
- `KenshiOnlineMod/build/bin/Debug/KenshiOnlineMod.dll` (Debug)

## 📦 Creating a Release Package

### Step 1: Build Everything

```bash
# Build C# app
cd Kenshi-Online
dotnet publish -c Release -r win-x64 --self-contained false

# Build native DLL
cd ../KenshiOnlineMod/build
cmake --build . --config Release
```

### Step 2: Create Package Directory

```bash
mkdir KenshiOnline-Release
cd KenshiOnline-Release
```

### Step 3: Copy Files

```powershell
# PowerShell
Copy-Item ../Kenshi-Online/bin/Release/net8.0/publish/* .
Copy-Item ../KenshiOnlineMod/build/bin/Release/KenshiOnlineMod.dll .
Copy-Item ../README.md .
Copy-Item ../SETUP_GUIDE.md .
Copy-Item ../LICENSE .
```

### Step 4: Create ZIP

```powershell
Compress-Archive -Path * -DestinationPath KenshiOnline-v1.0.0.zip
```

## 🐛 Troubleshooting Build Issues

### CMake Not Found

**Error:** `'cmake' is not recognized as an internal or external command`

**Solution:**
1. Install CMake from https://cmake.org/download/
2. During installation, select "Add CMake to system PATH"
3. Restart terminal
4. Verify: `cmake --version`

### .NET SDK Not Found

**Error:** `The command could not be loaded`

**Solution:**
1. Install .NET 8.0 SDK from https://dotnet.microsoft.com/download/dotnet/8.0
2. Restart terminal
3. Verify: `dotnet --version`

### MSVC Compiler Not Found

**Error:** `No CMAKE_CXX_COMPILER could be found`

**Solution:**
1. Install Visual Studio 2022
2. During installation, select "Desktop development with C++"
3. Restart and try again

### WS2_32.lib Not Found

**Error:** `LINK : fatal error LNK1104: cannot open file 'ws2_32.lib'`

**Solution:**
- Install Windows SDK via Visual Studio Installer
- Select "Windows 10/11 SDK" in Individual Components

### Access Denied During Build

**Error:** `Access to the path '...' is denied`

**Solution:**
- Close Visual Studio if open
- Close any running Kenshi processes
- Run terminal as Administrator
- Try build again

### DLL Fails to Inject

**Error:** Injection fails when testing

**Solution:**
1. Build with `/MD` runtime (not `/MT`)
2. Ensure DLL is 64-bit
3. Check DLL with: `dumpbin /headers KenshiOnlineMod.dll | findstr "machine"`
   - Should show "machine (x64)"

## 🧪 Testing Your Build

### Test C# Application

```bash
cd Kenshi-Online/bin/Release/net8.0
./KenshiMultiplayer.exe
```

Should show the main menu without errors.

### Test DLL Injection

1. Start Kenshi
2. Load a save game
3. Run as Administrator:
   ```bash
   ./KenshiMultiplayer.exe
   ```
4. Select "Start Server"
5. Check if mod injects successfully

### Verify DLL Exports

```bash
dumpbin /exports KenshiOnlineMod.dll
```

Should show `DllMain` export.

## 🔍 Debug Builds

### Building with Debug Symbols

#### C# Debug Build

```bash
dotnet build -c Debug
```

Attach Visual Studio debugger to `KenshiMultiplayer.exe`

#### C++ Debug Build

```bash
cd KenshiOnlineMod/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

### Debugging the DLL

1. Build debug DLL
2. Inject into Kenshi
3. Attach Visual Studio to `kenshi_x64.exe`
4. Set breakpoints in C++ code
5. Play game to trigger breakpoints

### Logging

Both C# and C++ components write logs:
- C#: `server_log.txt`
- C++: Console window (if `AllocConsole()` is enabled)

## 📝 Build Configurations

### Release Configuration

- Optimizations enabled
- No debug symbols
- Smaller binary size
- Used for distribution

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
dotnet build -c Release
```

### Debug Configuration

- No optimizations
- Full debug symbols
- Larger binary size
- Used for development

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
dotnet build -c Debug
```

### Profile Configuration

- Optimizations enabled
- With debug symbols
- Best for profiling performance

```bash
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

## 🎯 Continuous Integration

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

    - name: Setup MSBuild
      uses: microsoft/setup-msbuild@v1

    - name: Build C# App
      run: dotnet build -c Release

    - name: Build C++ DLL
      run: |
        cd KenshiOnlineMod
        mkdir build
        cd build
        cmake .. -G "Visual Studio 17 2022"
        cmake --build . --config Release
```

## 🛠️ Development Workflow

### Recommended Workflow

1. **Make changes** to C# or C++ code
2. **Build** using commands above
3. **Test** in Kenshi
4. **Debug** if needed
5. **Commit** changes
6. **Push** to your branch

### Quick Rebuild Script

Create `rebuild.bat`:

```batch
@echo off
echo Building C# Application...
cd Kenshi-Online
dotnet build -c Release
if %errorlevel% neq 0 exit /b %errorlevel%

echo.
echo Building Native Mod DLL...
cd ../KenshiOnlineMod/build
cmake --build . --config Release
if %errorlevel% neq 0 exit /b %errorlevel%

echo.
echo Build Complete!
pause
```

Run with: `rebuild.bat`

## 📚 Additional Resources

- [.NET Build Documentation](https://docs.microsoft.com/en-us/dotnet/core/tools/dotnet-build)
- [CMake Documentation](https://cmake.org/documentation/)
- [Visual Studio C++ Guide](https://docs.microsoft.com/en-us/cpp/)

## 🆘 Getting Help

If you encounter build issues:

1. Check this guide
2. Search [GitHub Issues](https://github.com/The404Studios/Kenshi-Online/issues)
3. Ask on [Discord](https://discord.gg/62aDDmtkgb)
4. Create a new issue with:
   - Your OS version
   - Build command used
   - Complete error message
   - Steps to reproduce

---

**Happy Building!** 🎉

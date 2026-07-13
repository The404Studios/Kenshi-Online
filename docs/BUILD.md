# Build Guide

**Comprehensive contributor build guide for Kenshi Online**

This document covers CMake configuration, dependencies, build targets, project dependencies, and troubleshooting common build errors.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Quick Start](#quick-start)
3. [External Dependencies](#external-dependencies)
4. [Project Structure](#project-structure)
5. [CMake Configuration](#cmake-configuration)
6. [Building Individual Projects](#building-individual-projects)
7. [Build Targets](#build-targets)
8. [Project Dependencies](#project-dependencies)
9. [Build Output](#build-output)
10. [Troubleshooting](#troubleshooting)
11. [Advanced Topics](#advanced-topics)

---

## Prerequisites

### Required Tools

- **Visual Studio 2022** (17.0 or later)
  - Desktop development with C++ workload
  - MSVC v143 or later
  - Windows 10 SDK (10.0.19041.0 or later recommended)
- **CMake 3.20+** (tested with 4.2.0)
- **Git** (for cloning dependencies)
- **Windows 10/11 x64**

### System Requirements

- **Architecture:** x64 only (enforced at CMake configure time)
- **Build Mode:** Release recommended (Debug has ABI compatibility issues, see below)
- **Disk Space:** ~2GB for full build with all targets
- **RAM:** 8GB minimum, 16GB recommended for parallel builds

---

## Quick Start

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online
```

### 2. Initialize Submodules

Third-party dependencies are pinned Git submodules in `lib/`. If the repository was cloned without `--recursive`, initialize them before configuring CMake:

```bash
git submodule update --init --recursive
```

### 3. Generate Build Files

```bash
# Create build directory
mkdir build
cd build

# Generate Visual Studio solution
cmake .. -G "Visual Studio 17 2022" -A x64

# Alternative: Use default generator
cmake ..
```

### 4. Build All Projects

```bash
# Using MSBuild (from build directory)
MSBuild.exe KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m

# Alternative: Using CMake
cmake --build . --config Release --parallel
```

### 5. Run Tests

```bash
# Integration tests
bin\Release\KenshiMP.IntegrationTest.exe

# Unit tests
bin\Release\KenshiMP.UnitTest.exe
```

---

## External Dependencies

All third-party libraries are located in `lib/` and built automatically via CMake `add_subdirectory()`.

### 1. ENet (Networking)

- **Version:** Pinned commit `8be2368a8001f28db44e81d5939de5e613025023` from https://github.com/lsalzman/enet
- **Purpose:** Reliable UDP networking library for client-server communication
- **Build Type:** Static library (`enet.lib`)
- **CMake Target:** `enet`
- **Features Used:**
  - 3 ENet channels (reliable-ordered, unreliable, sequenced)
  - Compression enabled
  - 16 peer slots on server

**CMake Integration:**
```cmake
add_subdirectory(lib/enet)
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/enet/include)
```

### 2. MinHook (Function Hooking)

- **Version:** Pinned commit `1e9ad1eb42db11bfcb65461f687c656612d1b555` from https://github.com/TsudaKageyu/minhook
- **Purpose:** x64 function hooking via trampoline technique
- **Build Type:** Static library (`minhook.lib`)
- **CMake Target:** `minhook`
- **Features Used:**
  - 14 Kenshi function hooks
  - Special handling for `mov rax, rsp` prologue (requires naked detour)
  - Thread-safe hook installation

**CMake Integration:**
```cmake
add_subdirectory(lib/minhook)
```

### 3. spdlog (Logging)

- **Version:** Pinned commit `48bcf39a661a13be22666ac64db8a7f886f2637e` from https://github.com/gabime/spdlog
- **Purpose:** Fast C++ logging library
- **Build Type:** Header-only mode
- **CMake Target:** `spdlog::spdlog`
- **Features Used:**
  - Async logging (1MB queue)
  - Rotating file sinks (5MB x 3 files)
  - Colored console output
  - Thread-safe

**CMake Integration:**
```cmake
add_subdirectory(lib/spdlog EXCLUDE_FROM_ALL)
```

### 4. nlohmann/json (JSON Parsing)

- **Version:** Pinned commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` from https://github.com/nlohmann/json
- **Purpose:** JSON serialization/deserialization
- **Build Type:** Header-only
- **CMake Target:** `nlohmann_json::nlohmann_json`
- **Features Used:**
  - Server config (`server.json`)
  - Network protocol serialization
  - Save file persistence

**CMake Integration:**
```cmake
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(lib/json EXCLUDE_FROM_ALL)
```

### 5. imgui (Optional, currently unused)

- **Location:** `lib/imgui/`
- **Version:** Pinned commit `dbb5eeaadffb6a3ba6a60de1290312e5802dba5a` from https://github.com/ocornut/imgui
- **Status:** Included but not currently linked to any project
- **Note:** Native MyGUI overlay is used instead for in-game UI

### System Libraries (Windows)

The following Windows SDK libraries are linked directly:

- **d3d11, dxgi, d3dcompiler** - DirectX 11 for rendering hooks
- **dbghelp** - Symbol enumeration, stack walking
- **ws2_32, winmm** - Winsock networking, multimedia timers
- **ole32, oleaut32, iphlpapi** - COM, UPnP, network interfaces
- **shlwapi, comctl32** - Shell utilities, common controls (Injector GUI)

---

## Project Structure

The solution contains **9 projects** organized into 5 categories:

### Core Libraries (Static)

1. **KenshiMP.Common** - Shared protocol, messages, config, serialization
2. **KenshiMP.Scanner** - Memory scanning, pattern matching, hook management

### Main Projects (Executables/DLL)

3. **KenshiMP.Core** - Ogre plugin DLL (injected into Kenshi process)
4. **KenshiMP.Server** - Dedicated server executable
5. **KenshiMP.Injector** - Win32 GUI launcher (modifies `Plugins_x64.cfg`)

### Server Infrastructure

6. **KenshiMP.MasterServer** - Server browser/lobby backend

### Testing & Development

7. **KenshiMP.TestClient** - Fake player console client (walks, chats, tests protocol)
8. **KenshiMP.IntegrationTest** - Automated protocol tests (15 tests, auto-starts server)
9. **KenshiMP.UnitTest** - Unit tests for core logic
10. **KenshiMP.LiveTest** - Manual testing harness

---

## CMake Configuration

### Top-Level CMakeLists.txt

Located at `KenshiMP/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(KenshiMP VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # .exe, .dll
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # .so (unused on Windows)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # .lib
```

### Critical Build Settings

#### 1. x64 Enforcement

```cmake
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KenshiMP requires a 64-bit build")
endif()
```

**Why:** Kenshi is 64-bit only. Pointer sizes must match for memory manipulation.

#### 2. Windows Defines

```cmake
add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
```

- `WIN32_LEAN_AND_MEAN` - Exclude rarely-used Windows headers (faster compile)
- `NOMINMAX` - Prevent `min`/`max` macro conflicts with `std::min`/`std::max`
- `_CRT_SECURE_NO_WARNINGS` - Suppress warnings about `strcpy`, `sprintf`

#### 3. ABI Compatibility Fix (CRITICAL)

```cmake
add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
```

**PROBLEM:** Kenshi's DLLs (MyGUI, Ogre, etc.) are Release builds. Their `std::string`, `std::vector` use the Release CRT layout:
- `std::string` = 32 bytes (16-byte SSO buffer + 8-byte size + 8-byte capacity)
- `std::vector` = 24 bytes (3 pointers: begin, end, capacity)

In Visual Studio **Debug mode**, Microsoft's STL adds an 8-byte `_Container_proxy*` pointer to track iterators:
- `std::string` = 40 bytes
- `std::vector` = 32 bytes

**SOLUTION:** Force Release container layout even in Debug builds via `_ITERATOR_DEBUG_LEVEL=0`. This prevents crashes when passing containers across DLL boundaries.

**TRADE-OFF:** Debug iterator validation is disabled. Use address sanitizer or manual bounds checking during development.

---

## Building Individual Projects

Each project can be built independently once dependencies are compiled.

### KenshiMP.Common (Static Library)

**Dependencies:** None (depends only on nlohmann/json and spdlog headers)

```bash
cmake --build build --target KenshiMP.Common --config Release
```

**Output:** `build/lib/KenshiMP.Common.lib`

**Contents:**
- `protocol.cpp` - Network message types (29 message types)
- `serialization.cpp` - Binary serialization helpers
- `compression.cpp` - Optional message compression (zlib)
- `config.cpp` - JSON config file parsing (`server.json`)

### KenshiMP.Scanner (Static Library)

**Dependencies:** KenshiMP.Common, minhook, dbghelp

```bash
cmake --build build --target KenshiMP.Scanner --config Release
```

**Output:** `build/lib/KenshiMP.Scanner.lib`

**Contents:**
- `scanner.cpp` - High-level pattern search API
- `scanner_engine.cpp` - Boyer-Moore-Horspool implementation
- `patterns.cpp` - 14 function signature patterns for Kenshi
- `memory.cpp` - Safe memory read/write with PAGE_GUARD protection
- `hook_manager.cpp` - MinHook wrapper (install/remove/queue hooks)
- `mov_rax_rsp_fix.cpp` - Naked detour for `mov rax, rsp` prologue
- `function_analyzer.cpp` - Disassemble to find function boundaries
- `pdata_enumerator.cpp` - Parse PE .pdata section for exception handlers
- `string_analyzer.cpp` - Extract `[Class::Method]` debug strings
- `vtable_scanner.cpp` - RTTI-based vtable extraction
- `call_graph.cpp` - Build call graph for cross-reference analysis
- `orchestrator.cpp` - Multi-strategy scanning coordinator

### KenshiMP.Core (Shared Library / DLL)

**Dependencies:** KenshiMP.Common, KenshiMP.Scanner, enet, spdlog, nlohmann_json, d3d11, dxgi, d3dcompiler

```bash
cmake --build build --target KenshiMP.Core --config Release
```

**Output:** 
- `build/bin/Release/KenshiMP.Core.dll` (1.4MB)
- `build/bin/Release/KenshiMP.Core.map` (4.3MB debug map file for crash reports)

**Auto-Deploy:**
CMake automatically copies DLL to Kenshi directory if `kenshi_x64.exe` is found:

```cmake
set(KENSHI_DIR "${CMAKE_SOURCE_DIR}/..")
if(EXISTS "${KENSHI_DIR}/kenshi_x64.exe")
    add_custom_command(TARGET KenshiMP.Core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:KenshiMP.Core>"
            "${KENSHI_DIR}/KenshiMP.Core.dll"
    )
endif()
```

**Contents:**
- 14 hook modules (entity, movement, combat, world, save, etc.)
- ENet client (3 channels: reliable, unreliable, sequenced)
- Entity registry + interpolation
- Native MyGUI overlay (main menu, HUD)
- Kenshi SDK (polling-based game state reader)

### KenshiMP.Server (Executable)

**Dependencies:** KenshiMP.Common, enet, spdlog, nlohmann_json, ws2_32, winmm, ole32, iphlpapi

```bash
cmake --build build --target KenshiMP.Server --config Release
```

**Output:** `build/bin/Release/KenshiMP.Server.exe` (515KB)

**Auto-Deploy:**
Copied to Kenshi directory if detected (so "Host" button can spawn it).

**Contents:**
- ENet server (16 peer slots)
- Zone interest management (1500 unit radius)
- Combat resolver (server-authoritative damage)
- World persistence (JSON save/load)
- UPnP port forwarding (automatic NAT traversal)

### KenshiMP.Injector (GUI Executable)

**Dependencies:** KenshiMP.Common, spdlog, nlohmann_json, shlwapi, comctl32

```bash
cmake --build build --target KenshiMP.Injector --config Release
```

**Output:** `build/bin/Release/KenshiMP.Injector.exe` (99KB)

**Build Flags:**
```cmake
target_compile_definitions(KenshiMP.Injector PRIVATE UNICODE _UNICODE)
```
- Unicode Win32 API (`wchar_t` strings)

**Contents:**
- Win32 GUI (server browser, IP/port input)
- Modifies `Plugins_x64.cfg` to load `KenshiMP.Core.dll`
- Launches Kenshi via `CreateProcess`

### Test Projects

```bash
# TestClient - Fake player for protocol testing
cmake --build build --target KenshiMP.TestClient --config Release

# IntegrationTest - Automated protocol tests (15 scenarios)
cmake --build build --target KenshiMP.IntegrationTest --config Release

# UnitTest - Unit tests for core logic
cmake --build build --target KenshiMP.UnitTest --config Release

# LiveTest - Manual testing harness
cmake --build build --target KenshiMP.LiveTest --config Release
```

---

## Build Targets

### Configuration Types

CMake generates a multi-config Visual Studio solution supporting:

- **Debug** - Slow, full symbols, iterator validation DISABLED (see ABI section)
- **Release** - Fast, optimized, minimal symbols, recommended for testing
- **RelWithDebInfo** - Optimized + full symbols, best for profiling
- **MinSizeRel** - Smallest binary size (not commonly used)

**Default:** Release (set in spdlog's CMakeLists if unspecified)

### Build Commands

```bash
# Build everything (Release)
cmake --build build --config Release --parallel

# Build specific target
cmake --build build --target KenshiMP.Core --config Release

# Build with maximum parallelism
cmake --build build --config Release -- /m:16

# Clean
cmake --build build --target clean --config Release

# Rebuild all
cmake --build build --target rebuild --config Release
```

### MSBuild Specific

```bash
# Build from solution file
MSBuild.exe build\KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m

# Build single project
MSBuild.exe build\KenshiMP.Core\KenshiMP.Core.vcxproj /p:Configuration=Release /p:Platform=x64

# Verbose output
MSBuild.exe build\KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /v:detailed

# Clean
MSBuild.exe build\KenshiMP.sln /t:Clean /p:Configuration=Release /p:Platform=x64
```

### Custom Targets

```bash
# Install (copies to CMAKE_INSTALL_PREFIX, not commonly used)
cmake --build build --target INSTALL --config Release

# Generate documentation (if Doxygen is configured)
cmake --build build --target docs
```

---

## Project Dependencies

### Dependency Graph

```
                        ┌─────────────────┐
                        │ External Libs   │
                        │ enet, minhook,  │
                        │ spdlog, json    │
                        └────────┬────────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
            ┌───────▼────────┐       ┌───────▼────────┐
            │ KenshiMP.Common│       │      (used      │
            │  (protocol,    │       │    directly)    │
            │   config)      │       │                 │
            └───────┬────────┘       └─────────────────┘
                    │
            ┌───────▼────────┐
            │ KenshiMP.Scanner│
            │  (patterns,    │
            │   hooks)       │
            └───────┬────────┘
                    │
        ┌───────────┴───────────┬─────────────┬─────────────┐
        │                       │             │             │
┌───────▼────────┐  ┌───────────▼──────┐  ┌──▼──────┐  ┌───▼──────┐
│ KenshiMP.Core  │  │ KenshiMP.Server  │  │Injector │  │  Tests   │
│   (DLL)        │  │     (EXE)        │  │  (EXE)  │  │  (EXE)   │
└────────────────┘  └──────────────────┘  └─────────┘  └──────────┘
```

### Build Order

CMake automatically resolves dependencies. Manual build order:

1. **External Libraries** (parallel)
   - enet
   - minhook
   - spdlog (header-only, no build step)
   - nlohmann_json (header-only, no build step)

2. **KenshiMP.Common** (depends on spdlog, json headers)

3. **KenshiMP.Scanner** (depends on Common, minhook)

4. **Final Targets** (parallel, each depends on Common/Scanner/externals)
   - KenshiMP.Core
   - KenshiMP.Server
   - KenshiMP.Injector
   - KenshiMP.MasterServer
   - KenshiMP.TestClient
   - KenshiMP.IntegrationTest
   - KenshiMP.UnitTest
   - KenshiMP.LiveTest

### Link Dependencies (Detailed)

```cmake
# KenshiMP.Common
target_link_libraries(KenshiMP.Common PUBLIC 
    nlohmann_json::nlohmann_json 
    spdlog::spdlog
)

# KenshiMP.Scanner
target_link_libraries(KenshiMP.Scanner PUBLIC 
    KenshiMP.Common 
    minhook 
    spdlog::spdlog 
    dbghelp
)

# KenshiMP.Core
target_link_libraries(KenshiMP.Core PRIVATE
    KenshiMP.Common
    KenshiMP.Scanner
    enet
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11      # DirectX 11
    dxgi       # DirectX Graphics Infrastructure
    d3dcompiler # Shader compiler
)

# KenshiMP.Server
target_link_libraries(KenshiMP.Server PRIVATE
    KenshiMP.Common
    enet
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ws2_32     # Winsock 2
    winmm      # Multimedia timer
    ole32      # COM
    oleaut32   # COM automation
    iphlpapi   # IP Helper (UPnP)
)

# KenshiMP.Injector
target_link_libraries(KenshiMP.Injector PRIVATE
    KenshiMP.Common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    shlwapi    # Shell lightweight API
    comctl32   # Common controls
)
```

---

## Build Output

### Directory Structure

After a successful build:

```
KenshiMP/
├── build/
│   ├── bin/
│   │   └── Release/
│   │       ├── KenshiMP.Core.dll         (1.4 MB)
│   │       ├── KenshiMP.Core.map         (4.3 MB, debug symbols)
│   │       ├── KenshiMP.Server.exe       (515 KB)
│   │       ├── KenshiMP.Injector.exe     (99 KB)
│   │       ├── KenshiMP.TestClient.exe   (54 KB)
│   │       ├── KenshiMP.IntegrationTest.exe (127 KB)
│   │       ├── KenshiMP.UnitTest.exe     (247 KB)
│   │       ├── KenshiMP.LiveTest.exe     (112 KB)
│   │       ├── KenshiMP.MasterServer.exe (356 KB)
│   │       ├── Kenshi_MainMenu.layout    (auto-copied)
│   │       ├── Kenshi_MultiplayerHUD.layout
│   │       └── Kenshi_MultiplayerPanel.layout
│   └── lib/
│       ├── KenshiMP.Common.lib
│       ├── KenshiMP.Scanner.lib
│       ├── enet.lib
│       └── minhook.lib
├── dist/
│   ├── KenshiMP.Core.dll           (auto-copied from build)
│   ├── KenshiMP.Server.exe
│   ├── KenshiMP.Injector.exe
│   ├── kenshi-online.mod           (game mod file)
│   ├── server.json
│   ├── install.bat
│   ├── uninstall.bat
│   └── README.md
└── C:/Program Files (x86)/Steam/steamapps/common/Kenshi/
    ├── KenshiMP.Core.dll           (auto-deployed if detected)
    ├── KenshiMP.Server.exe         (auto-deployed if detected)
    └── data/gui/layout/
        ├── Kenshi_MainMenu.layout  (auto-deployed if detected)
        ├── Kenshi_MultiplayerHUD.layout
        └── Kenshi_MultiplayerPanel.layout
```

### Auto-Deploy Behavior

If `kenshi_x64.exe` is found in `../` relative to source root, CMake will:

1. Copy `KenshiMP.Core.dll` to Kenshi directory (POST_BUILD)
2. Copy `KenshiMP.Server.exe` to Kenshi directory (POST_BUILD)
3. Copy 3 MyGUI layout files to `data/gui/layout/` (POST_BUILD)

**Manual Deploy (if auto-deploy fails):**

```bash
cd build/bin/Release
copy KenshiMP.Core.dll "C:\Program Files (x86)\Steam\steamapps\common\Kenshi\"
copy KenshiMP.Server.exe "C:\Program Files (x86)\Steam\steamapps\common\Kenshi\"
copy *.layout "C:\Program Files (x86)\Steam\steamapps\common\Kenshi\data\gui\layout\"
```

---

## Troubleshooting

### Common Build Errors

#### 1. "KenshiMP requires a 64-bit build"

**Error:**
```
CMake Error: KenshiMP requires a 64-bit build
```

**Cause:** Generating for Win32 (x86) instead of x64.

**Fix:**
```bash
# Specify x64 architecture explicitly
cmake .. -G "Visual Studio 17 2022" -A x64
```

---

#### 2. LNK2019: Unresolved External Symbol (ENet)

**Error:**
```
KenshiMP.Core.lib(client.obj) : error LNK2019: unresolved external symbol enet_initialize
```

**Cause:** ENet include directory not exported to consumers.

**Fix:** Already fixed in `CMakeLists.txt`:
```cmake
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/enet/include)
```

If still broken, manually add to `KenshiMP.Core/CMakeLists.txt`:
```cmake
target_include_directories(KenshiMP.Core PRIVATE ${CMAKE_SOURCE_DIR}/lib/enet/include)
```

---

#### 3. C2039: 'string': is not a member of 'std'

**Error:**
```
error C2039: 'string': is not a member of 'std'
```

**Cause:** Missing `#include <string>` in header.

**Fix:** Add to top of file:
```cpp
#include <string>
#include <vector>
#include <memory>
```

---

#### 4. C2712: Cannot use __try in functions that require object unwinding

**Error:**
```
hooks/entity_hooks.cpp(42): error C2712: cannot use __try in functions that require object unwinding
```

**Cause:** Using `__try/__except` (SEH) in a function with C++ objects (RAII destructors).

**Fix:** Move `__try` block to a separate `__declspec(noinline)` function with no C++ locals:
```cpp
// BAD: C++ object in __try scope
void Hook() {
    std::string name = "test";  // Has destructor
    __try {
        RiskyFunction();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// GOOD: Separate SEH wrapper
__declspec(noinline) static void SafeCall(void* ptr) {
    __try {
        RiskyFunction(ptr);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void Hook() {
    std::string name = "test";
    SafeCall(&name);
}
```

---

#### 5. Crash on std::string Access Across DLL Boundary (Debug Mode)

**Error:**
Game crashes when passing `std::string` from KenshiMP.Core to Kenshi's MyGUI/Ogre DLLs.

**Cause:** ABI mismatch. Debug STL containers have extra fields. See [CMake Configuration](#cmake-configuration) section.

**Fix:** Already applied globally via:
```cmake
add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
```

**Verification:**
```cpp
// sizeof(std::string) should be 32 bytes in both Debug and Release
static_assert(sizeof(std::string) == 32, "ABI mismatch detected");
```

---

#### 6. Missing `dbghelp.lib`

**Error:**
```
LINK : fatal error LNK1181: cannot open input file 'dbghelp.lib'
```

**Cause:** Windows SDK not installed or SDK version mismatch.

**Fix:**
1. Install Windows 10 SDK via Visual Studio Installer
2. Or manually link in `KenshiMP.Scanner/CMakeLists.txt`:
```cmake
target_link_libraries(KenshiMP.Scanner PUBLIC 
    KenshiMP.Common 
    minhook 
    spdlog::spdlog 
    dbghelp  # May need full path: "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.19041.0/um/x64/dbghelp.lib"
)
```

---

#### 7. MinHook Build Fails

**Error:**
```
MinHook: Could not find hde64.c
```

**Cause:** MinHook submodule is not initialized or is incomplete.

**Fix:**
```bash
git submodule update --init --checkout lib/minhook
cmake --build build --target minhook --config Release
```

---

#### 8. nlohmann/json Not Found

**Error:**
```
error: nlohmann/json.hpp: No such file or directory
```

**Cause:** json library not initialized.

**Fix:**
```bash
# Check if lib/json exists
ls lib/json/

# If empty, restore the pinned submodule revision
git submodule update --init --checkout lib/json

# Rebuild
cmake --build build --config Release
```

---

#### 9. CMake Cache Corruption

**Error:**
```
CMake Error: Could not find CMAKE_ROOT !!!
```

**Cause:** Corrupted `CMakeCache.txt`.

**Fix:**
```bash
# Delete cache and regenerate
rm -rf build/*
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
```

---

#### 10. Git LFS Files Not Downloaded

**Error:**
Binary files (e.g., `kenshi-online.mod`) are placeholder text pointers instead of actual data.

**Cause:** Git LFS not installed or not initialized.

**Fix:**
```bash
# Install Git LFS
git lfs install

# Pull LFS objects
git lfs pull

# Verify
file dist/kenshi-online.mod  # Should show "data" not "ASCII text"
```

---

### Build Performance Tips

#### 1. Parallel Builds

```bash
# CMake parallel build (uses all cores)
cmake --build build --config Release --parallel

# MSBuild parallel (specify core count)
MSBuild.exe build\KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m:16
```

#### 2. Incremental Builds

After first build, only modified files are recompiled. To force full rebuild:

```bash
cmake --build build --target clean --config Release
cmake --build build --config Release
```

#### 3. Unity Builds (Jumbo Files)

Combine multiple `.cpp` files into one translation unit for faster compilation (not currently enabled):

```cmake
# Add to CMakeLists.txt
set(CMAKE_UNITY_BUILD ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
```

**Trade-off:** Faster builds, but increases memory usage and hides include issues.

#### 4. Precompiled Headers (PCH)

Not currently configured. Can add to `KenshiMP.Core` for ~20% build speedup:

```cmake
target_precompile_headers(KenshiMP.Core PRIVATE
    <string>
    <vector>
    <memory>
    <spdlog/spdlog.h>
    <enet/enet.h>
)
```

---

## Advanced Topics

### Custom CMake Variables

Override defaults at configure time:

```bash
# Change output directory
cmake .. -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=C:/KenshiMP/bin

# Change install prefix
cmake .. -DCMAKE_INSTALL_PREFIX=C:/KenshiMP/dist

# Disable tests
cmake .. -DBUILD_TESTING=OFF

# Enable static runtime (not recommended, causes issues with Kenshi DLLs)
cmake .. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
```

### Cross-Compilation (Not Supported)

KenshiMP is Windows x64 only. Cross-compiling from Linux is not supported due to:
- Direct3D 11 rendering hooks (no Linux equivalent)
- Windows-specific APIs (MyGUI, Ogre Windows build, SEH exceptions)
- Kenshi itself is Windows-only

### Static Analysis

Run Visual Studio Code Analysis:

```bash
MSBuild.exe build\KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true
```

Or use external tools:
```bash
# cppcheck
cppcheck --enable=all --project=build/KenshiMP.sln

# clang-tidy
clang-tidy KenshiMP.Core/**/*.cpp -- -std=c++17
```

### Sanitizers

**NOT SUPPORTED** in Visual Studio MSVC. Use Clang-cl for AddressSanitizer:

```bash
cmake .. -T ClangCL -DCMAKE_CXX_FLAGS="/fsanitize=address"
```

**Warning:** May conflict with `_ITERATOR_DEBUG_LEVEL=0` ABI hack.

### Debug Symbols

Generate PDB files for crash analysis:

```cmake
# Already enabled for Release builds via:
target_link_options(KenshiMP.Core PRIVATE /MAP)
```

Output: `build/bin/Release/KenshiMP.Core.map` (4.3 MB)

For full PDB in Release:
```cmake
target_compile_options(KenshiMP.Core PRIVATE /Zi)
target_link_options(KenshiMP.Core PRIVATE /DEBUG:FULL)
```

---

## Additional Resources

- **CMake Documentation:** https://cmake.org/documentation/
- **Visual Studio MSBuild Reference:** https://docs.microsoft.com/en-us/visualstudio/msbuild/
- **ENet Documentation:** http://sauerbraten.org/enet/
- **MinHook Documentation:** https://github.com/TsudaKageyu/minhook
- **spdlog Documentation:** https://github.com/gabime/spdlog/wiki
- **nlohmann/json Documentation:** https://json.nlohmann.me/

---

## Contributing

After setting up your build environment:

1. Make changes in a feature branch
2. Build with `cmake --build build --config Release`
3. Run tests: `bin/Release/KenshiMP.IntegrationTest.exe`
4. Verify in-game: Launch via `KenshiMP.Injector.exe`
5. Submit PR with build verification

See `README.md` for full contribution guidelines.

---

## Changelog

- **2026-06-04** - Initial build guide created
- Document covers CMake 3.20+, Visual Studio 2022, all 9 projects
- Includes ABI compatibility warnings, auto-deploy behavior, common errors
- Verified against current build system (CMake 4.2.0, MSVC 19.42)

---

**Questions?** Open an issue on GitHub or ask in Discord.

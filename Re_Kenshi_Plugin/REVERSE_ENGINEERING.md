# Reverse Engineering Guide for Kenshi Online

This document describes the reverse engineering techniques used to create the Re_Kenshi plugin and how to discover more game structures.

## Table of Contents

1. [Tools Required](#tools-required)
2. [Pattern Scanning](#pattern-scanning)
3. [Game Structures](#game-structures)
4. [OGRE Integration](#ogre-integration)
5. [Function Hooking](#function-hooking)
6. [Memory Layout](#memory-layout)

---

## Tools Required

### Essential Tools

1. **Cheat Engine** - Memory scanning and debugging
   - Download: https://www.cheatengine.org/
   - Used for: Finding memory addresses, pointer chains, and patterns

2. **x64dbg** - Debugger
   - Download: https://x64dbg.com/
   - Used for: Disassembly, breakpoints, and runtime analysis

3. **IDA Pro / Ghidra** - Disassembler/Decompiler
   - IDA: Commercial (expensive) https://hex-rays.com/ida-pro/
   - Ghidra: Free https://ghidra-sre.org/
   - Used for: Static analysis, function discovery

4. **ReClass.NET** - Structure viewer/editor
   - Download: https://github.com/ReClassNET/ReClass.NET
   - Used for: Visualizing and documenting memory structures

5. **Process Hacker** - Process analysis
   - Download: https://processhacker.sourceforge.io/
   - Used for: DLL injection, memory viewing

### Optional Tools

- **API Monitor** - API call tracking
- **Dependency Walker** - DLL dependencies
- **PE Explorer** - PE file analysis

---

## Pattern Scanning

Pattern scanning (signature scanning) finds unique byte sequences in memory to locate important addresses across game updates.

### How to Find Patterns

#### Step 1: Find the Memory Address

Use Cheat Engine to find what you're looking for:

```
Example: Finding player position
1. Open Kenshi in Cheat Engine
2. Search for Float value
3. Move in game, search for "Changed value"
4. Repeat until you have 1-2 addresses
5. Right-click → "Find out what accesses this address"
6. Move in game to trigger the code
```

#### Step 2: Analyze the Code

Once you have a code location:

```asm
Example instruction found:
kenshi_x64.exe+5A3B10 - F3 0F 10 83 70000000  - movss xmm0,[rbx+00000070]
kenshi_x64.exe+5A3B18 - F3 0F 11 45 10        - movss [rbp+10],xmm0
```

This shows that player position is at `[rbx+0x70]`.

#### Step 3: Find Where RBX Comes From

Scroll up in the disassembly to find where `rbx` is loaded:

```asm
kenshi_x64.exe+5A3B00 - 48 8B 1D AB123400    - mov rbx,[kenshi_x64.exe+18E4AB0]
kenshi_x64.exe+5A3B07 - 48 85 DB             - test rbx,rbx
kenshi_x64.exe+5A3B0A - 74 15                - je kenshi_x64.exe+5A3B21
kenshi_x64.exe+5A3B0C - 48 8B CB             - mov rcx,rbx
kenshi_x64.exe+5A3B0F - E8 1C340000          - call kenshi_x64.exe+5A6F30
```

#### Step 4: Create a Pattern

The pattern for the `mov rbx,[...]` instruction:

```
Original bytes: 48 8B 1D AB 12 34 00
Pattern:        48 8B 1D ?? ?? ?? ??

Explanation:
- 48 8B 1D = mov rbx,[rip+offset] (fixed opcode)
- ?? ?? ?? ?? = relative offset (changes with game updates)
```

#### Step 5: Implement in Code

```cpp
// In MemoryScanner.h
constexpr const char* PLAYER_CONTROLLER = "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ??";

// In your code
auto pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::PLAYER_CONTROLLER);
uintptr_t address = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);

if (address) {
    // Resolve RIP-relative address
    uintptr_t playerPtr = Memory::MemoryScanner::ResolveRelativeAddress(address, 7);

    // Read pointer
    uintptr_t playerObject = 0;
    Memory::MemoryScanner::ReadMemory(playerPtr, playerObject);

    // Now playerObject points to player data
}
```

### Common Pattern Types

**1. RIP-Relative MOV (most common)**
```asm
48 8B 0D ?? ?? ?? ??    ; mov rcx,[rip+offset]
48 8B 15 ?? ?? ?? ??    ; mov rdx,[rip+offset]
48 8B 05 ?? ?? ?? ??    ; mov rax,[rip+offset]
```

**2. Relative CALL**
```asm
E8 ?? ?? ?? ??          ; call function
```

**3. Function Prologue**
```asm
40 53 48 83 EC 20       ; push rbx; sub rsp,0x20
48 89 5C 24 ??          ; mov [rsp+offset],rbx
```

---

## Game Structures

### Discovering Structures

#### Method 1: Pointer Chain

```
Example: Finding character health
1. Find health value address: 0x12345678
2. Right-click → "Pointer scan for this address"
3. Wait for results
4. Test pointers by restarting game
5. Find stable pointer chain: [[[base+0x10]+0x20]+0xA0]
```

#### Method 2: ReClass.NET

```
1. Attach ReClass.NET to kenshi_x64.exe
2. Create new class
3. Set address to your found pointer
4. Add fields and observe values
5. Move character, check which fields change
6. Document structure
```

#### Example Structure Discovery

```cpp
// Initial discovery - all unknown
struct Character {
    char unknown_0x00[0x200];
};

// After analysis in ReClass:
struct Character {
    void* vtable;              // 0x00 - Virtual function table
    char pad_0x08[0x08];       // 0x08 - Unknown
    char name[64];             // 0x10 - Name changes when selecting different characters
    char pad_0x50[0x20];       // 0x50 - Unknown
    Vector3 position;          // 0x70 - X/Y/Z changes when moving
    Quaternion rotation;       // 0x7C - Changes when rotating
    char pad_0x8C[0x14];       // 0x8C - Unknown
    float health;              // 0xA0 - Decreases when damaged
    float maxHealth;           // 0xA4 - Static value
    // ... continue discovering
};
```

### Validating Structures

Always validate your structures:

```cpp
// Test reading
Character character;
if (GameDataReader::ReadCharacter(characterAddress, character)) {
    std::cout << "Name: " << character.name << std::endl;
    std::cout << "Health: " << character.health << "/" << character.maxHealth << std::endl;
    std::cout << "Position: " << character.position.x << ", "
              << character.position.y << ", " << character.position.z << std::endl;
}

// Test writing
Vector3 newPosition(100.0f, 50.0f, 200.0f);
if (GameDataReader::WriteCharacterPosition(characterAddress, newPosition)) {
    std::cout << "Teleported character!" << std::endl;
}
```

---

## OGRE Integration

Kenshi uses OGRE 1.9.x for rendering. We need to hook into this.

### Finding OGRE Root

```cpp
// Pattern for Ogre::Root singleton
constexpr const char* OGRE_ROOT = "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 90 ?? ?? ?? ??";

// In OgreOverlay.cpp
bool OgreOverlay::FindOgreInstance() {
    auto pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::OGRE_ROOT);
    uintptr_t ogreRootPattern = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);

    if (ogreRootPattern) {
        uintptr_t ogreRootPtr = Memory::MemoryScanner::ResolveRelativeAddress(ogreRootPattern, 7);
        Memory::MemoryScanner::ReadMemory(ogreRootPtr, m_ogreRoot);

        if (m_ogreRoot) {
            // Get OverlayManager from Root
            // This requires knowing OGRE's vtable layout
            m_overlayManager = Ogre::OverlayManager::getSingletonPtr();
            return true;
        }
    }

    return false;
}
```

### Alternative: D3D11 Device Hook

Instead of hooking OGRE, we can hook D3D11 directly:

```cpp
// Pattern for D3D11 device
constexpr const char* D3D11_DEVICE = "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 8B F8";

// Hook Present() to render overlay
void D3D11Hook::PresentHook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    // Render ImGui overlay here
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Draw UI
    ImGui::Begin("Kenshi Online");
    ImGui::Text("Hello from overlay!");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Call original Present
    return originalPresent(swapChain, syncInterval, flags);
}
```

---

## Function Hooking

### Types of Hooks

**1. Inline Hook (Detour)**
```cpp
// Replace first bytes of function with JMP to your hook
// Requires saving original bytes and implementing trampoline
```

**2. VTable Hook**
```cpp
// Replace virtual function pointer in vtable
DWORD oldProtect;
void** vtable = *(void***)object;
VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
vtable[index] = &MyHookFunction;
VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
```

**3. Import Address Table (IAT) Hook**
```cpp
// Replace function pointer in import table
// Useful for hooking WinAPI calls
```

### Using MinHook (Recommended)

```cpp
#include <MinHook.h>

// Function pointer type
typedef void (*UpdateWorldFn)(float deltaTime);
UpdateWorldFn originalUpdateWorld = nullptr;

// Hook function
void UpdateWorldHook(float deltaTime) {
    // Our code
    std::cout << "World update called!" << std::endl;

    // Call original
    originalUpdateWorld(deltaTime);
}

// Install hook
MH_Initialize();
MH_CreateHook((void*)updateWorldAddress, &UpdateWorldHook, (void**)&originalUpdateWorld);
MH_EnableHook((void*)updateWorldAddress);
```

---

## Memory Layout

### Kenshi Memory Map

```
Base Address: 0x140000000 (typical for x64 exe)

Sections:
.text    - 0x140001000 - Executable code
.rdata   - 0x141000000 - Read-only data (strings, vtables)
.data    - 0x142000000 - Global variables
.pdata   - 0x143000000 - Exception handling

Important Regions:
- Game World State: [base + 0x24D8F40] (example offset)
- Character List: [base + 0x24C5A20]
- Player Controller: [base + 0x18E4AB0]
```

### Pointer Chains

```
Example pointer chain for player health:
kenshi_x64.exe + 0x18E4AB0 → [+0x0] → [+0x10] → [+0xA0] = health value

In code:
uintptr_t base = GetModuleBase("kenshi_x64.exe");
uintptr_t playerPtr = base + 0x18E4AB0;
std::vector<uintptr_t> offsets = {0x0, 0x10, 0xA0};
uintptr_t healthAddr = Memory::MemoryScanner::FollowPointerChain(playerPtr, offsets);

float health = 0;
Memory::MemoryScanner::ReadMemory(healthAddr, health);
```

---

## Best Practices

### 1. Version Independence

Always use pattern scanning instead of hard-coded offsets:

```cpp
// BAD
uintptr_t playerPtr = base + 0x18E4AB0;  // Breaks on game update

// GOOD
auto pattern = Memory::MemoryScanner::ParsePattern("48 8B 1D ?? ?? ?? ??");
uintptr_t address = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);
uintptr_t playerPtr = Memory::MemoryScanner::ResolveRelativeAddress(address, 7);
```

### 2. Error Handling

Always validate pointers before use:

```cpp
uintptr_t character = 0;
if (!Memory::MemoryScanner::ReadMemory(characterPtr, character)) {
    return;  // Invalid pointer
}

if (character == 0) {
    return;  // Null pointer
}

// Safe to use character now
```

### 3. Structure Documentation

Document every field you discover:

```cpp
struct Character {
    void* vtable;              // 0x00 - Points to character vtable at .rdata+0x1234
    int32_t id;                // 0x08 - Unique character ID
    char name[64];             // 0x10 - Null-terminated UTF-8 string
    // ... etc
};
```

### 4. Test Across Game Versions

Test your patterns on multiple Kenshi versions:
- v1.0.50
- v1.0.55
- v1.0.60 (latest)

If a pattern breaks, make it more unique or add more context bytes.

---

## Useful Patterns

Here are some useful patterns for Kenshi (may need adjustment):

```cpp
// Player spawning
"40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 85 C0"

// World update tick
"40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24"

// Character movement
"F3 0F 10 83 ?? ?? ?? ?? F3 0F 11 45 ?? F3 0F 10 8B"

// Combat damage calculation
"F3 0F 59 C1 F3 0F 58 C2 0F 2F C3 76 ?? F3 0F 11 45"

// Inventory operations
"48 89 5C 24 ?? 57 48 83 EC 20 48 8B FA 48 8B D9 E8"
```

---

## Advanced Topics

### Anti-Cheat Considerations

Kenshi doesn't have traditional anti-cheat, but be aware:
- Memory scanners can detect modified values
- Some servers might check memory integrity
- Always test in single-player first

### Performance

Pattern scanning is slow. Cache results:

```cpp
class PatternCache {
    static std::map<std::string, uintptr_t> cache;

public:
    static uintptr_t Get(const char* name, const char* pattern) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            return it->second;
        }

        auto parsed = Memory::MemoryScanner::ParsePattern(pattern);
        uintptr_t addr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", parsed);
        cache[name] = addr;
        return addr;
    }
};
```

### Multi-Threading

Be careful with game memory access from multiple threads:
- Game state can change between reads
- Use mutexes or atomic operations
- Read entire structures at once, not field by field

---

## Resources

- **OGRE Documentation**: https://www.ogre3d.org/docs/
- **x64 Calling Convention**: https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention
- **Cheat Engine Forum**: https://forum.cheatengine.org/
- **Guided Hacking**: https://guidedhacking.com/

---

## Contributing

If you discover new patterns or structures, please document them:

1. Add pattern to `KenshiPatterns` namespace
2. Add structure to `KenshiStructures.h`
3. Test on multiple game versions
4. Submit pull request with documentation

---

**Last Updated**: 2025-01-13
**Game Version**: Kenshi v1.0.60
**Contributors**: The404Studios, Community

# Pattern Verification Report - kenshi_x64.exe v1.0.68
Date: 2026-03-04

## Binary Details
- File: C:\Program Files (x86)\Steam\steamapps\common\Kenshi\kenshi_x64.exe
- Size: 36,718,592 bytes (35.0 MB)
- PE32+ (x64), MSVC compiled
- ImageBase: 0x0000000140000000

## Function 1: CharacterDestroy
- RVA: 0x0038A720 (file offset 0x00389B20)
- Pattern: `48 8B C4 44 88 40 18 48 89 50 10 48 89 48 08 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 48 48`
- Bytes at RVA: MATCH
- String "NodeList::destroyNodesByBuilding": FOUND in .rdata at RVA 0x016C6620
- Uniqueness: UNIQUE (1 match in .text)
- Status: PASS

## Function 2: AICreate
- RVA: 0x00622110 (file offset 0x00621510)
- Pattern (from patterns.h): `40 57 48 81 EC 90 00 00 00 48 C7 44 24 28 FE FF FF FF 48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ?`
- Bytes at RVA: MATCH (29 fixed bytes match)
- String "[AI::create] No faction for ": FOUND in .rdata at RVA 0x016F9B30
- Uniqueness: NOT UNIQUE - 2 matches:
  - 0x000AF870 (false positive - different function)
  - 0x00622110 (correct target)
- Divergence: Byte 40 (0x88 vs 0x80), Byte 46 (0xF9 vs 0xFA), Byte 49+ fully diverge
- Fix: Extend to 41 bytes: `... 48 33 C4 48 89 84 24 80` (adds 8 fixed bytes after wildcards)
- Status: NEEDS FIX (pattern not unique)

## Function 3: GameFrameUpdate
- RVA: 0x00123A10 (file offset 0x00122E10)
- Pattern: `48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 38 FE FF FF FF`
- Bytes at RVA: MATCH
- String "Kenshi 1.0.68 - x64 (Newland)": FOUND in .rdata at RVA 0x01692288
- Uniqueness: NOT UNIQUE - 2 matches:
  - 0x00123A10 (correct target)
  - 0x00788100 (false positive - different function)
- Divergence point: Byte 44 (0x48 vs 0x0F = MOV vs MOVAPS)
- Fix: Extend to 45 bytes: `... 48 89 58 10 48 89 70 18 48 89 78 20 48`
- Status: NEEDS FIX (pattern not unique)

## Function 4: TimeUpdate
- RVA: 0x00214B50 (file offset 0x00213F50)
- Pattern: `40 55 56 48 83 EC 28 48 8B F2 48 8B E9 BA 02 00 00 00 48 8B CE E8 ? ? ? ? 84 C0 0F 84 ? ?`
- Bytes at RVA: MATCH (26 fixed bytes match)
- String "timeScale": FOUND in .rdata at RVA 0x016A6F48
- Uniqueness: UNIQUE (1 match in .text)
- Status: PASS

## Function 5: PlayerBase (global pointer)
- RVA: 0x01AC8A90 (file offset 0x01AC7490)
- Section: .rdata (not .data as might be expected)
- Static value: 0x0002EF3700000000 (not a valid runtime pointer)
- No code xrefs found in .text (searched MOV/LEA RIP-relative patterns)
- At runtime: resolved via `(uintptr_t)GetModuleHandle(NULL) + 0x01AC8A90`
- The QWORD at this address is populated by the game at runtime with a heap pointer
- Status: PASS (correctly used as base+offset, no pattern needed)

## False Positive Analysis

### AICreate false positive at 0x000AF870
Both functions share identical prologue through `48 8B 05 [RIP-rel]` (29 bytes).
The false positive is a different function that happens to use the same stack frame
setup (0x90 bytes, same cookie pattern). After the security cookie XOR at byte 33,
the functions diverge: the false positive stores to [RSP+0x88] while AICreate
stores to [RSP+0x80], and register usage differs (RDI vs RDX for param, etc).

### GameFrameUpdate false positive at 0x00788100
Both functions share identical prologue through the stack frame setup (44 bytes).
At byte 44, the real GameFrameUpdate does `48 89 78 20` (MOV [RAX+20h], RDI)
while the false positive does `0F 29 70 C8` (MOVAPS [RAX-38h], XMM6) - saving
an XMM register instead. This is a clear structural difference.

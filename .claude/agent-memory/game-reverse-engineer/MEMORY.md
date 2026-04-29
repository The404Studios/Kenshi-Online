# Game Reverse Engineer - Agent Memory

## Pattern Verification (v1.0.68)
See [pattern-verification.md](pattern-verification.md) for detailed results.

### Critical Issues Found
- **AICreate** pattern is NOT unique (2 matches). Must extend to 41 bytes. See fix below.
- **GameFrameUpdate** pattern is NOT unique (2 matches). Must extend to 45 bytes. See fix below.
- **CharacterDestroy** and **TimeUpdate** patterns are confirmed UNIQUE and correct.
- **PlayerBase** (0x01AC8A90) is in .rdata section, resolved at runtime via base+offset. No code xrefs in static file.

### Required Pattern Fixes
AICreate (current 33-token pattern has false positive at 0x000AF870):
```
OLD: "40 57 48 81 EC 90 00 00 00 48 C7 44 24 28 FE FF FF FF 48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ?"
NEW: "40 57 48 81 EC 90 00 00 00 48 C7 44 24 28 FE FF FF FF 48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 80"
```

GameFrameUpdate (current 32-byte pattern has false positive at 0x00788100):
```
OLD: "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 38 FE FF FF FF"
NEW: "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 38 FE FF FF FF 48 89 58 10 48 89 70 18 48 89 78 20 48"
```

## PE Layout (kenshi_x64.exe v1.0.68)
- .text:  VA 0x00001000, Size 0x01671412 (22.4 MB)
- .rdata: VA 0x01673000, Size 0x0054A4CB (5.3 MB)
- .data:  VA 0x01BBE000, Size 0x0058E000
- ImageBase: 0x0000000140000000

## Verified Debug Strings in .rdata
- "NodeList::destroyNodesByBuilding" at RVA 0x016C6620
- "[AI::create] No faction for " at RVA 0x016F9B30
- "Kenshi 1.0.68 - x64 (Newland)" at RVA 0x01692288
- "timeScale" at RVA 0x016A6F48
- "[RootObjectFactory::process] Character '" at RVA 0x016EEB10

## MovRaxRsp Fix (SOLVED)
See [mov-rax-rsp-fix.md](mov-rax-rsp-fix.md) for design details.
- **Problem**: `mov rax, rsp` prologue functions crash when hooked via MinHook trampoline
- **Solution**: Two-part ASM stub approach in `mov_rax_rsp_fix.cpp`:
  1. **Naked detour**: Captures RSP at hook entry (before C++ prologue), JMPs to C++ hook
  2. **Trampoline wrapper**: Swaps RSP to captured value, pushes return addr, JMPs to tramp+3
- HookManager auto-applies fix for any function starting with `48 8B C4`
- Patches MinHook's relay to route through naked detour
- **All previously disabled hooks re-enabled**: GameFrameUpdate, Combat, Inventory, Building, Faction

## Building/Inventory/Faction/Squad Class Analysis
See [class-analysis-building-inventory-faction-squad.md](class-analysis-building-inventory-faction-squad.md).
Key findings:
- **CRITICAL BUG**: inventory_hooks passes inventory ptr (not owner char ptr) to GetNetId()
- Faction +0x08 = factionId, hardcoded in hook but missing from FactionOffsets
- SquadAddMember at 0x00928423 is mid-function (not aligned), hooking may be fragile
- ALL offset tables for these classes are UNVERIFIED guesses
- VTableScanner never applied to Building/Inventory/Faction/Squad classes

## Character Class Analysis
See [character-class-analysis.md](character-class-analysis.md) for full struct layout, known offsets, and missing fields.

## CharacterSpawn / RootObjectFactory::process Verification
- RVA: 0x00581770, confirmed via pattern + .pdata + string xref
- Pattern: exact match, unique (1 hit in .text)
- .pdata: function 0x00581770-0x0058307A, size 0x190A (6410 bytes)
- String xref (LEA rdx) at RVA 0x00582324, +0xBB4 into function
- **RuntimeStringScanner::FindFunctionStart backward walk FAILS** for this function
  (2996 bytes from xref to prologue, exceeds 2048-byte limit + internal CC bytes confuse it)
- **Orchestrator TryStringXref SUCCEEDS** via .pdata-based resolution (StringAnalyzer uses
  PDataEnumerator::FindContaining, not the backward walk)
- IsPrologue() does NOT recognize "48 8B C4" (mov rax, rsp) as a prologue pattern
- Previously documented RVA 0x0089A560 in MEMORY.md was WRONG (old/stale), corrected to 0x00581770

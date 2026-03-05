# Game Reverse Engineer Memory

## Critical Bugs Found

### SquadAddMember at RVA 0x928423 is WRONG FUNCTION (corrected 2026-03-05)
- RVA 0x928423 IS a valid .pdata function entry (196 bytes, ends 0x9284E7) -- NOT mid-function
- But it is the WRONG function: a small "delayedSpawningChecks" helper, not squad member addition
- HookManager::InstallRaw() .pdata validation (RtlLookupFunctionEntry) correctly PASSES because it IS a function
- The hook installs without error but hooks the wrong function entirely
- Root cause: "delayedSpawningChecks" is wrong string anchor for SquadAddMember
- Need to find callers of 0x928423 -- the parent function is likely the real squad manager
- Promising alternative strings: "change_squad", "Recruiting", "squad size min/max"
- See detailed analysis: [squad-addmember-analysis.md](squad-addmember-analysis.md)

### IsPrologue/FindFunctionStart limitation (not a bug for this case)
- Both C++ (patterns.cpp:265) and Python (re_scanner.py:199) match `48 89 XX 24` as prologue without checking stack offset
- In THIS case, the walk-back to prologue happened to land on a real .pdata function entry (0x928423)
- The real problem is that the function is semantically wrong, not that the address is invalid
- The IsPrologue check still lacks stack offset validation (could cause issues elsewhere)

## Key File Locations
- Scanner: KenshiMP.Scanner/src/ (orchestrator.cpp, patterns.cpp, pdata_enumerator.cpp, hook_manager.cpp)
- Hooks: KenshiMP.Core/hooks/ (squad_hooks.cpp, etc.)
- Patterns header: KenshiMP.Scanner/include/kmp/patterns.h
- Python RE tool: KenshiMP/tools/re_scanner.py
- Logs: C:/Program Files (x86)/Steam/steamapps/common/Kenshi/KenshiOnline_*.log

## Resolution Pipeline (orchestrator.cpp)
Phase 1: .pdata enumeration
Phase 2: String discovery + xref
Phase 3: VTable scanning + RTTI
Phase 4: SIMD batch AOB pattern scan (primary method, 100% confidence)
Phase 5: String xref fallback (85% confidence)
Phase 6: Call graph analysis
Phase 7: Global pointer validation
Phase 8: Emergency (direct string search + prologue-validated RVA)

## StringAnchor struct
`{label, searchString, searchStringLen}` -- third field is STRING LENGTH not function distance

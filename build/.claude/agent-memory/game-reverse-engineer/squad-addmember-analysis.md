# SquadAddMember Wrong Function Analysis (updated 2026-03-05)

## Summary
RVA 0x928423 is a valid .pdata function entry (196 bytes) but is the WRONG function.
It is a "delayedSpawningChecks" helper, NOT the squad member addition function.
The hook installs without error but hooks irrelevant code.

## Key Correction
Previous analysis said 0x928423 was "mid-function". This is WRONG.
.pdata analysis confirms: BeginRVA=0x928423, EndRVA=0x9284E7, Size=196 bytes.
The MSVC compiler carved this out as a separate function entry (common with loop
extraction/function splitting). RtlLookupFunctionEntry correctly returns this as
a function start, so the HookManager .pdata validation PASSES.

## Evidence

### 1. .pdata entry (CONFIRMED)
```
Function containing 0x00928423:
  Begin RVA: 0x00928423
  End RVA:   0x009284E7
  Size:      196 bytes (0xC4)
  Unwind:    0x01AB0090
  Alignment: 0x3 (NOT 16-byte aligned, but still valid .pdata entry)
```

### 2. Bytes at 0x928423
```
48 89 5C 24 38    mov [rsp+38h], rbx
48 89 74 24 48    mov [rsp+48h], rsi
48 89 7C 24 20    mov [rsp+20h], rdi
33 FF             xor edi, edi
85 C0             test eax, eax    ; EAX set by caller, not __fastcall param
0F 84 95 00 00 00 jz <skip>
8B F7             mov esi, edi
66 90             nop
48 8B 05 ...      mov rax, [rip+...]
```
This is a 196-byte helper -- likely a loop body extracted by MSVC optimizer.
test eax,eax as first meaningful op suggests it receives a "count" or "result"
from its caller (likely the squad's member count or a validation result).

### 3. Root cause: wrong string anchor
"delayedSpawningChecks" relates to SPAWN TIMING, not squad membership.
The string-xref scanner found this string, walked back to the helper function,
and incorrectly identified it as the squad member addition function.

### 4. HookManager DID NOT refuse it
Log shows:
```
HookManager: 'SquadAddMember' prologue at 0x7FF6F9CC8423: 48 89 5C 24 38 48 89 74
HookManager: Installed hook 'SquadAddMember' at 0x7FF6F9CC8423
```
No REFUSING message -- because RtlLookupFunctionEntry returns the .pdata entry.

## Finding the Real SquadAddMember

### Step 1: Find callers of 0x928423
Scan .text for CALL rel32 (E8) where target = 0x928423.
The calling function is likely the real squad management function.

### Step 2: Better string anchors
Promising strings found in .rdata:
- "change_squad" (0x016C2F58) -- serialization key for squad membership changes
- "Recruiting" (0x016BC870) -- AI recruitment state
- "squad size min" (0x016B8450) -- squad size validation
- "Maximum number of squads reached." (0x016DCC38) -- capacity check
- "Add squad" (0x016DDCA8) -- UI button handler

### Step 3: Check adjacent .pdata entries
The function immediately BEFORE 0x928423 in .pdata is likely related code.

### Step 4: Expected signature of real SquadAddMember
- Takes Squad* (RCX) and Character* (RDX) in __fastcall
- Modifies squad member list (squad+0x28 or squad+0x30)
- Should be substantially larger than 196 bytes
- Should have a proper function prologue (mov rax,rsp / push rbx / sub rsp)
- Should be 16-byte aligned (likely)

## Files to Update When Found
- patterns.h line 205-206: SQUAD_ADD_MEMBER pattern and RVA comment
- patterns.h line 354: StringAnchor searchString and length
- orchestrator.cpp line 234-236: reg() call for SquadAddMember
- patterns.cpp line 444: fallback entry
- squad_hooks.cpp line 16: verify typedef matches real signature

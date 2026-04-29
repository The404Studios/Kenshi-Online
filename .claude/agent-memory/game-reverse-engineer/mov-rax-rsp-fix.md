# MovRaxRsp Hooking Fix

## Problem
Many Kenshi functions (MSVC x64) use this prologue:
```asm
mov rax, rsp          ; 48 8B C4
push rbp/rsi/rdi/r12-r15
lea rbp, [rax-0xNN]   ; derive frame pointer from RAX
sub rsp, 0xMM
```

MinHook copies `mov rax, rsp` into the trampoline. When our hook CALLs the
trampoline, RAX captures RSP deep in the hook's stack (hundreds of bytes off).
`lea rbp, [rax-0x158]` computes wrong RBP; all `[rbp+XX]` accesses corrupt memory.

## Why Previous Approaches Failed
1. **BuildCustomCaller** (`mov rax,rsp; jmp original+3`): CALL to stub pushes return
   address, so RAX captures hook's RSP, not game's RSP.
2. **BuildRelayThunk** (`add rax,8`): Shifts all [rax+XX] stores by one slot.
3. **HookBypass** (disable/enable per call): Thread-unsafe, suspends all threads.
4. **Raw trampoline**: RAX=wrong RSP -> crash after ~300 calls.

## Solution: Two-Part ASM Stub
Files: `KenshiMP.Scanner/include/kmp/mov_rax_rsp_fix.h`, `KenshiMP.Scanner/src/mov_rax_rsp_fix.cpp`

### Part 1: Naked Detour (MinHook JMPs here)
```asm
mov [rip+captured_rsp], rsp  ; Save RSP (= game caller RSP - 8)
jmp [rip+0]                  ; JMP to C++ hook (not CALL!)
dq cpp_hook_addr
```
- MinHook uses JMP (not CALL), so RSP is still at game caller level
- JMP to C++ hook preserves RSP; C++ hook's RET returns to game caller
- The C++ hook function is the "real" function from the game's perspective

### Part 2: Trampoline Wrapper (C++ hook calls this as "original")
```asm
mov [rip+stub_rsp], rsp     ; Save C++ hook's RSP
mov rax, [rip+captured_rsp] ; Load game caller's RSP
mov rsp, rax                ; Swap to game caller's stack
lea r11, [rip+return_point] ; Load return address
push r11                    ; Push return addr (RSP = captured_rsp - 8)
jmp [rip+0]                 ; JMP to trampoline+3
dq trampoline_plus_3
return_point:
mov [rip+saved_rax], rax    ; Save return value
mov rsp, [rip+stub_rsp]     ; Restore C++ hook's RSP
mov rax, [rip+saved_rax]    ; Restore return value
ret                         ; Return to C++ hook
```

### Why Stack Swap is Necessary
The compiler lays out the stack so that push slots and [rbp+XX] slots overlap
by design: `[rbp+0x150]` IS the same slot as where `push rbp` stores its value.
If RSP is wrong when pushes execute, pushes write to wrong slots, corrupting
[rbp+XX] locals. The stack swap ensures RSP matches what RAX captures.

### Integration
HookManager::InstallRaw auto-detects `48 8B C4` and:
1. Creates hook with MH_CreateHook(target, cppDetour, &trampoline)
2. Builds MovRaxRsp stubs via BuildMovRaxRspHook()
3. Patches MinHook's relay (FF 25 + 8-byte addr) to point to naked detour
4. Returns trampoline wrapper as *original (not raw trampoline)

### Thread Safety
Uses per-hook global captured_rsp slot (not TLS). Safe because Kenshi game logic
is single-threaded. If multi-thread needed later, switch to TlsAlloc.

## Affected Functions (start with 48 8B C4)
CharacterSpawn, CharacterDestroy, CreateRandomSquad, CharacterSetPosition,
ApplyDamage, StartAttack, CharacterDeath, HealthUpdate, MartialArtsCombat,
ZoneUnload, BuildingPlace, BuildingDestroyed, GameFrameUpdate, ImportGame,
CharacterStats, SquadCreate, ItemPickup, FactionRelation, GunTurret,
BuildingDismantle, BuildingConstruct, BuildingRepair (22 of 41 patterns)

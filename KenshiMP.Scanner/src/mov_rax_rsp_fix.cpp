#include "kmp/mov_rax_rsp_fix.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cstring>

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  ARCHITECTURE — Return-Address Patching with Stack Gap
// ═══════════════════════════════════════════════════════════════════════════
//
//  Problem: MSVC x64 functions with `mov rax, rsp` prologue:
//      mov rax, rsp          ; 48 8B C4
//      push rbp              ; 55
//      push rsi/rdi/r12-r15  ; register saves
//      lea rbp, [rax-0xNN]   ; derive RBP from RAX
//      sub rsp, 0xMM         ; allocate locals
//
//  The compiler ALIASES push-saved register slots with [rbp+XX] frame
//  offsets. E.g., [rbp+0x150] IS the push rbp slot. They occupy the same
//  memory by design. Any extra data on the stack (like an extra return
//  address from CALL or PUSH) shifts the push slots by 8 bytes, making
//  them no longer alias the [rbp+XX] slots. This corrupts every access.
//
//  Solution: Two runtime-generated ASM stubs per hook.
//
//  NAKED DETOUR (MinHook JMPs here instead of C++ hook):
//  ─────────────────────────────────────────────────────
//  RSP = game_caller_RSP - 8 (game's CALL pushed return address).
//
//      mov [captured_rsp], rsp      ; Save RSP for trampoline wrapper
//      mov r11, [rsp]               ; Read game's return address
//      mov [saved_game_ret], r11    ; Save it (trampoline wrapper will patch [RSP])
//      sub rsp, 0x1008              ; 4KB+8 gap: separates stacks + fixes alignment
//      mov r11, <cpp_hook_addr>     ; Load C++ hook address
//      call r11                     ; CALL (not JMP!) so hook returns HERE
//      add rsp, 0x1008              ; Pop gap → RSP = captured_rsp
//      mov r11, [saved_game_ret]    ; Load game's original return address
//      mov [rsp], r11               ; Restore it (trampoline wrapper patched it)
//      ret                          ; Return to game caller
//
//  Why CALL (not JMP)?  With JMP, the C++ hook's RET would go directly to
//  the game caller.  But the trampoline wrapper patches [captured_rsp] to
//  point to return_point.  When the C++ hook RETs, it would pop the PATCHED
//  value and jump to return_point a second time.  CALL ensures the C++ hook
//  returns to us, and we restore [RSP] before the final RET.
//
//  Why the 4KB gap?  The trampoline wrapper swaps RSP to captured_rsp so
//  the original function's pushes land at the correct addresses.  Without
//  a gap, the C++ hook's stack frame (also starting near captured_rsp)
//  would be overwritten by the original function's pushes.
//
//  TRAMPOLINE WRAPPER (C++ hook calls this as "original"):
//  ──────────────────────────────────────────────────────
//      mov [stub_rsp], rsp          ; Save C++ hook's RSP
//      mov rsp, [captured_rsp]      ; Swap to game's stack
//      lea r11, [rip+return_point]  ; Compute our return address
//      mov [rsp], r11               ; Patch [RSP] (overwrite game's ret addr)
//      mov rax, rsp                 ; Set RAX = RSP (what function expects!)
//      jmp trampoline+3             ; Enter function body (skip mov rax,rsp)
//
//  RSP = captured_rsp, RAX = captured_rsp.  ZERO extra bytes on stack.
//  Push slots and [rbp+XX] frame offsets are perfectly aligned.
//  The original function RETs to return_point (via patched [RSP]).
//
//      return_point:
//      mov rsp, [stub_rsp]          ; Swap back to C++ hook's stack
//      ret                          ; Return to C++ hook
//
//  RAX (return value) is preserved — only RSP is modified.
//
// ═══════════════════════════════════════════════════════════════════════════

// Memory layout of allocated page:
//   +0x00: captured_rsp    (uint64_t) - game caller's RSP at hook entry
//   +0x08: stub_rsp        (uint64_t) - C++ hook's RSP before stack swap
//   +0x10: saved_game_ret  (uint64_t) - game's original return address
//   +0x18: depth           (int32_t)  - reentrancy depth counter
//   +0x20: raw_trampoline  (uint64_t) - MinHook raw trampoline for reentrant bypass
//   +0x40: NAKED DETOUR stub (machine code, ~90 bytes)
//   +0xC0: TRAMPOLINE WRAPPER stub (machine code, ~50 bytes)

static constexpr int OFF_CAPTURED_RSP   = 0x00;
static constexpr int OFF_STUB_RSP       = 0x08;
static constexpr int OFF_SAVED_GAME_RET = 0x10;
static constexpr int OFF_DEPTH          = 0x18;  // int32 reentrancy depth counter
static constexpr int OFF_RAW_TRAMP      = 0x20;  // uint64 raw MinHook trampoline address
static constexpr int OFF_BYPASS         = 0x28;  // int32 bypass flag (1=passthrough, 0=hook active)
static constexpr int OFF_NAKED_STUB     = 0x40;
static constexpr int OFF_TRAMP_WRAP     = 0xC0;
static constexpr int ALLOC_SIZE         = 0x200;
static constexpr int STACK_GAP          = 0x1008;   // 4KB+8 gap — stays within one guard page (64KB jumped past guard → crash)

// ─── Emit Helpers ──────────────────────────────────────────────────────────

static void EmitByte(uint8_t* buf, int& off, uint8_t b) { buf[off++] = b; }

static void EmitU32(uint8_t* buf, int& off, uint32_t v) {
    memcpy(&buf[off], &v, 4); off += 4;
}

static void EmitU64(uint8_t* buf, int& off, uint64_t v) {
    memcpy(&buf[off], &v, 8); off += 8;
}

// RIP-relative displacement: target - (base + instrEnd)
static int32_t RipDisp(uintptr_t base, int instrEnd, int dataOff) {
    return (int32_t)((base + dataOff) - (base + instrEnd));
}

// mov [rip+disp32], rsp = 48 89 25 disp32 (7 bytes)
static void EmitMovMemRsp(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 7;
    EmitByte(buf, off, 0x48); EmitByte(buf, off, 0x89); EmitByte(buf, off, 0x25);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// mov rsp, [rip+disp32] = 48 8B 25 disp32 (7 bytes)
static void EmitMovRspMem(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 7;
    EmitByte(buf, off, 0x48); EmitByte(buf, off, 0x8B); EmitByte(buf, off, 0x25);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// mov r11, [rip+disp32] = 4C 8B 1D disp32 (7 bytes)
static void EmitMovR11Mem(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 7;
    EmitByte(buf, off, 0x4C); EmitByte(buf, off, 0x8B); EmitByte(buf, off, 0x1D);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// mov [rip+disp32], r11 = 4C 89 1D disp32 (7 bytes)
static void EmitMovMemR11(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 7;
    EmitByte(buf, off, 0x4C); EmitByte(buf, off, 0x89); EmitByte(buf, off, 0x1D);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// mov r11, [rsp] = 4C 8B 1C 24 (4 bytes)
static void EmitMovR11FromStack(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x4C); EmitByte(buf, off, 0x8B);
    EmitByte(buf, off, 0x1C); EmitByte(buf, off, 0x24);
}

// mov [rsp], r11 = 4C 89 1C 24 (4 bytes)
static void EmitMovStackFromR11(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x4C); EmitByte(buf, off, 0x89);
    EmitByte(buf, off, 0x1C); EmitByte(buf, off, 0x24);
}

// sub rsp, imm32 = 48 81 EC imm32 (7 bytes)
static void EmitSubRspImm32(uint8_t* buf, int& off, uint32_t imm) {
    EmitByte(buf, off, 0x48); EmitByte(buf, off, 0x81); EmitByte(buf, off, 0xEC);
    EmitU32(buf, off, imm);
}

// add rsp, imm32 = 48 81 C4 imm32 (7 bytes)
static void EmitAddRspImm32(uint8_t* buf, int& off, uint32_t imm) {
    EmitByte(buf, off, 0x48); EmitByte(buf, off, 0x81); EmitByte(buf, off, 0xC4);
    EmitU32(buf, off, imm);
}

// mov r11, imm64 = 49 BB imm64 (10 bytes)
static void EmitMovR11Imm64(uint8_t* buf, int& off, uint64_t imm) {
    EmitByte(buf, off, 0x49); EmitByte(buf, off, 0xBB);
    EmitU64(buf, off, imm);
}

// call r11 = 41 FF D3 (3 bytes)
static void EmitCallR11(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x41); EmitByte(buf, off, 0xFF); EmitByte(buf, off, 0xD3);
}

// mov rax, rsp = 48 89 E0 (3 bytes)
// Using opcode 89 (MOV r/m64, r64) to avoid confusion with the game's 48 8B C4
static void EmitMovRaxRsp(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x48); EmitByte(buf, off, 0x89); EmitByte(buf, off, 0xE0);
}

// lea r11, [rip+disp32] = 4C 8D 1D disp32 (7 bytes)
// Returns the offset of the disp32 field for later patching
static int EmitLeaR11Rip(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x4C); EmitByte(buf, off, 0x8D); EmitByte(buf, off, 0x1D);
    int dispOff = off;
    EmitU32(buf, off, 0x00000000);  // placeholder — caller patches this
    return dispOff;
}

// jmp [rip+0]; dq target (14 bytes)
static void EmitJmpAbs(uint8_t* buf, int& off, uintptr_t target) {
    EmitByte(buf, off, 0xFF); EmitByte(buf, off, 0x25);
    EmitU32(buf, off, 0x00000000);
    EmitU64(buf, off, target);
}

// ret = C3 (1 byte)
static void EmitRet(uint8_t* buf, int& off) { EmitByte(buf, off, 0xC3); }

// inc dword ptr [rip+disp32] = FF 05 disp32 (6 bytes)
static void EmitIncMemDword(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 6;
    EmitByte(buf, off, 0xFF); EmitByte(buf, off, 0x05);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// dec dword ptr [rip+disp32] = FF 0D disp32 (6 bytes)
static void EmitDecMemDword(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 6;
    EmitByte(buf, off, 0xFF); EmitByte(buf, off, 0x0D);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// cmp dword ptr [rip+disp32], imm8 = 83 3D disp32 imm8 (7 bytes)
static void EmitCmpMemDwordImm8(uint8_t* buf, int& off, uintptr_t base, int dataOff, uint8_t imm) {
    int end = off + 7;
    EmitByte(buf, off, 0x83); EmitByte(buf, off, 0x3D);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
    EmitByte(buf, off, imm);
}

// jne rel8 = 75 rel8 (2 bytes) — returns offset of rel8 for patching
static int EmitJneShort(uint8_t* buf, int& off) {
    EmitByte(buf, off, 0x75);
    int rel8Off = off;
    EmitByte(buf, off, 0x00);  // placeholder
    return rel8Off;
}

// jmp qword ptr [rip+disp32] = FF 25 disp32 (6 bytes)
static void EmitJmpMemAbs(uint8_t* buf, int& off, uintptr_t base, int dataOff) {
    int end = off + 6;
    EmitByte(buf, off, 0xFF); EmitByte(buf, off, 0x25);
    EmitU32(buf, off, (uint32_t)RipDisp(base, end, dataOff));
}

// ─── Public API ────────────────────────────────────────────────────────────

bool TrampolineHasMovRaxRsp(void* trampoline) {
    if (!trampoline) return false;
    auto* p = static_cast<const uint8_t*>(trampoline);
    return (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0xC4);
}

void* AllocMovRaxRspPage() {
    void* mem = VirtualAlloc(nullptr, ALLOC_SIZE, MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
    if (!mem) return nullptr;

    auto* buf = static_cast<uint8_t*>(mem);
    memset(buf, 0xCC, ALLOC_SIZE);   // INT3 fill for safety
    memset(buf, 0, OFF_NAKED_STUB);  // Zero data slots

    return mem;
}

// Internal: emit both stubs into a pre-allocated page.
// Shared by BuildMovRaxRspHookAt (pre-allocated) and BuildMovRaxRspHook (legacy).
static MovRaxRspHook EmitStubs(
    void* page,
    const std::string& name,
    void* cppDetour,
    void* trampoline,
    int trampolineOffset)
{
    MovRaxRspHook result = {};

    if (!page) {
        spdlog::error("MovRaxRspFix: '{}' null page", name);
        return result;
    }

    if (!TrampolineHasMovRaxRsp(trampoline)) {
        spdlog::error("MovRaxRspFix: '{}' trampoline does NOT start with 48 8B C4", name);
        return result;
    }

    auto* buf = static_cast<uint8_t*>(page);
    uintptr_t base = reinterpret_cast<uintptr_t>(page);

    uintptr_t cppDetourAddr = reinterpret_cast<uintptr_t>(cppDetour);
    uintptr_t trampPlus3 = reinterpret_cast<uintptr_t>(trampoline) + trampolineOffset;

    // ═══════════════════════════════════════════════════════════════════
    //  NAKED DETOUR at OFF_NAKED_STUB
    // ═══════════════════════════════════════════════════════════════════
    //
    //  MinHook JMPs here. RSP = game_caller_RSP - 8, [RSP] = game ret addr.
    //  We CALL (not JMP) the C++ hook so it returns HERE, letting us
    //  restore [RSP] before the final RET to the game caller.
    //
    //  REENTRANCY GUARD: If this hook fires while already active (e.g., the
    //  original function calls itself), the global data slots would be corrupted.
    //  We use a depth counter (at OFF_DEPTH) to detect this. If depth > 0,
    //  skip everything and JMP to the raw MinHook trampoline (at OFF_RAW_TRAMP)
    //  which runs the original function without touching any global state.

    int off = OFF_NAKED_STUB;

    // ── Software bypass check (avoids MH_DisableHook/MH_EnableHook) ──
    // If bypass flag is set, skip everything and JMP to raw trampoline (pure passthrough).
    // This is used to disable the hook during loading without touching MinHook state.
    EmitCmpMemDwordImm8(buf, off, base, OFF_BYPASS, 0);       // 7 bytes: bypass == 0?
    int jneBypassOff = EmitJneShort(buf, off);                 // 2 bytes: jne .bypass

    // ── Reentrancy check (game logic is single-threaded) ──
    EmitIncMemDword(buf, off, base, OFF_DEPTH);                // 6 bytes: depth++
    EmitCmpMemDwordImm8(buf, off, base, OFF_DEPTH, 1);        // 7 bytes: depth == 1?
    int jneOff = EmitJneShort(buf, off);                       // 2 bytes: jne .reentrant

    // ── Normal path (depth == 1, first call) ──

    // 1. Save game's RSP
    EmitMovMemRsp(buf, off, base, OFF_CAPTURED_RSP);           // 7 bytes

    // 2. Save game's return address (trampoline wrapper will patch [RSP])
    EmitMovR11FromStack(buf, off);                             // 4 bytes
    EmitMovMemR11(buf, off, base, OFF_SAVED_GAME_RET);         // 7 bytes

    // 3. Create stack gap so C++ hook's stack doesn't collide with game's stack.
    //    The trampoline wrapper will swap RSP back to captured_rsp, so the
    //    original function's pushes go to captured_rsp-8, -16, etc.  Without
    //    this gap those pushes would overwrite the C++ hook's stack frame.
    //
    //    Gap is 0x1008 for correct 16-byte alignment:
    //      game_caller_RSP ≡ 0 (mod 16)          [x64 convention]
    //      After game's CALL: RSP ≡ 8 (mod 16)   [return addr pushed]
    //      sub 0x1008:        RSP ≡ 0 (mod 16)   [ready for our CALL]
    //      Our CALL pushes:   RSP ≡ 8 (mod 16)   [correct at hook entry]
    EmitSubRspImm32(buf, off, STACK_GAP);                      // 7 bytes

    // 4. Call C++ hook.  RCX/RDX/R8/R9 are untouched (only R11/RSP modified).
    EmitMovR11Imm64(buf, off, cppDetourAddr);                  // 10 bytes
    EmitCallR11(buf, off);                                     // 3 bytes

    // 5. C++ hook returned.  RAX = return value (preserved through cleanup).
    //    Pop the gap → RSP = captured_rsp.
    EmitAddRspImm32(buf, off, STACK_GAP);                      // 7 bytes

    // 6. Restore game's original return address at [RSP].
    //    (Trampoline wrapper patched [captured_rsp] to return_point.  The
    //     original function already consumed that via RET.  But the memory
    //     still contains return_point.  We must overwrite it back.)
    EmitMovR11Mem(buf, off, base, OFF_SAVED_GAME_RET);         // 7 bytes
    EmitMovStackFromR11(buf, off);                             // 4 bytes

    // 7. Decrement depth and return to game caller.
    EmitDecMemDword(buf, off, base, OFF_DEPTH);                // 6 bytes: depth--
    EmitRet(buf, off);                                         // 1 byte

    // ── Reentrant path (depth > 1, nested call) ──
    int reentrantOff = off;
    // Patch the JNE rel8 to jump here
    buf[jneOff] = (uint8_t)(reentrantOff - (jneOff + 1));

    EmitDecMemDword(buf, off, base, OFF_DEPTH);                // 6 bytes: depth--
    EmitJmpMemAbs(buf, off, base, OFF_RAW_TRAMP);             // 6 bytes: jmp [raw_tramp]

    // ── Bypass path (bypass flag != 0) ──
    int bypassOff = off;
    buf[jneBypassOff] = (uint8_t)(bypassOff - (jneBypassOff + 1));
    EmitJmpMemAbs(buf, off, base, OFF_RAW_TRAMP);             // 6 bytes: jmp [raw_tramp]

    int nakedSize = off - OFF_NAKED_STUB;  // ~100 bytes (with reentrancy + bypass)
    spdlog::info("MovRaxRspFix: '{}' naked detour at 0x{:X}, {} bytes",
                 name, base + OFF_NAKED_STUB, nakedSize);

    // ═══════════════════════════════════════════════════════════════════
    //  TRAMPOLINE WRAPPER at OFF_TRAMP_WRAP
    // ═══════════════════════════════════════════════════════════════════
    //
    //  Called by the C++ hook as "original function".  Swaps RSP to the
    //  game's stack, patches the return address, sets RAX = RSP, and
    //  JMPs to trampoline+3.  The original function runs with PERFECT
    //  stack alignment (zero extra bytes).  It RETs to return_point,
    //  which swaps back and returns to the C++ hook.

    off = OFF_TRAMP_WRAP;

    // 1. Save C++ hook's RSP (so return_point can swap back)
    EmitMovMemRsp(buf, off, base, OFF_STUB_RSP);               // 7 bytes

    // 2. Swap to game's stack: RSP = captured_rsp
    EmitMovRspMem(buf, off, base, OFF_CAPTURED_RSP);           // 7 bytes
    // Now RSP = captured_rsp = game_caller_RSP - 8
    // [RSP] = game's return address (or stale data — doesn't matter, we overwrite)

    // 3. Patch [RSP] with return_point address
    int leaDispOff = EmitLeaR11Rip(buf, off);                 // 7 bytes (disp32 TBD)
    EmitMovStackFromR11(buf, off);                             // 4 bytes

    // 4. Set RAX = RSP — this is what `mov rax, rsp` would have set
    EmitMovRaxRsp(buf, off);                                   // 3 bytes

    // 5. Enter original function body (skip `mov rax, rsp`)
    EmitJmpAbs(buf, off, trampPlus3);                          // 14 bytes

    // ─── return_point ────────────────────────────────────────────────
    // Original function RET'd here.  RSP = captured_rsp + 8.
    // RAX = return value from original function (preserved — we don't touch it).
    int returnPointOff = off;

    // Patch the LEA R11 displacement now that we know return_point's offset
    {
        int leaEnd = leaDispOff + 4;  // RIP after the LEA instruction
        int32_t d = (int32_t)((base + returnPointOff) - (base + leaEnd));
        memcpy(&buf[leaDispOff], &d, 4);
    }

    // 6. Swap back to C++ hook's stack.  Only RSP is modified; RAX preserved.
    EmitMovRspMem(buf, off, base, OFF_STUB_RSP);               // 7 bytes

    // 7. Return to C++ hook
    EmitRet(buf, off);                                         // 1 byte

    int wrapSize = off - OFF_TRAMP_WRAP;  // should be 50
    spdlog::info("MovRaxRspFix: '{}' trampoline wrapper at 0x{:X}, {} bytes, "
                 "return_point at +0x{:X}",
                 name, base + OFF_TRAMP_WRAP, wrapSize, returnPointOff);

    // Store raw trampoline pointer in data page for the reentrant JMP
    uintptr_t rawTrampAddr = reinterpret_cast<uintptr_t>(trampoline);
    memcpy(buf + OFF_RAW_TRAMP, &rawTrampAddr, 8);

    // Populate result
    result.nakedDetour = buf + OFF_NAKED_STUB;
    result.trampolineWrapper = buf + OFF_TRAMP_WRAP;
    result.rawTrampoline = trampoline;
    result.capturedRspSlot = buf + OFF_CAPTURED_RSP;
    result.bypassFlag = reinterpret_cast<volatile int32_t*>(buf + OFF_BYPASS);
    result.allocBase = page;
    result.allocSize = ALLOC_SIZE;

    spdlog::info("MovRaxRspFix: '{}' BUILT SUCCESSFULLY", name);
    spdlog::info("  nakedDetour       = 0x{:X}", reinterpret_cast<uintptr_t>(result.nakedDetour));
    spdlog::info("  trampolineWrapper = 0x{:X}", reinterpret_cast<uintptr_t>(result.trampolineWrapper));
    spdlog::info("  trampoline+3      = 0x{:X}", trampPlus3);
    spdlog::info("  capturedRspSlot   = 0x{:X}", reinterpret_cast<uintptr_t>(result.capturedRspSlot));

    return result;
}

MovRaxRspHook BuildMovRaxRspHookAt(
    void* page,
    const std::string& name,
    void* cppDetour,
    void* trampoline,
    int trampolineOffset)
{
    return EmitStubs(page, name, cppDetour, trampoline, trampolineOffset);
}

MovRaxRspHook BuildMovRaxRspHook(
    const std::string& name,
    void* cppDetour,
    void* trampoline,
    int trampolineOffset)
{
    void* page = AllocMovRaxRspPage();
    if (!page) {
        spdlog::error("MovRaxRspFix: VirtualAlloc failed for '{}': error {}", name, GetLastError());
        return {};
    }

    MovRaxRspHook result = EmitStubs(page, name, cppDetour, trampoline, trampolineOffset);
    if (!result.trampolineWrapper) {
        VirtualFree(page, 0, MEM_RELEASE);
    }
    return result;
}

void FreeMovRaxRspHook(MovRaxRspHook& hook) {
    if (hook.allocBase) {
        VirtualFree(hook.allocBase, 0, MEM_RELEASE);
        hook = {};
    }
}

} // namespace kmp

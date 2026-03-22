#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  MOV RAX, RSP HOOKING FIX — Return-Address Patching with Stack Gap
// ═══════════════════════════════════════════════════════════════════════════
//
//  Problem: Many Kenshi functions (MSVC x64) start with:
//      mov rax, rsp          ; 48 8B C4 — snapshot caller's RSP
//      push rbp              ; 55
//      push rsi/rdi/r12-r15  ; register saves
//      lea rbp, [rax-0xNN]   ; derive RBP from RAX
//      sub rsp, 0xMM         ; allocate locals
//
//  The compiler ALIASES push-saved register slots with [rbp+XX] frame
//  offsets (e.g., [rbp+0x150] IS the push rbp slot).  Any extra data on
//  the stack shifts pushes by 8 bytes, breaking the aliasing.
//
//  Solution: Two runtime-generated ASM stubs per hook.
//
//  1. NAKED DETOUR (what MinHook JMPs to):
//     Saves RSP and game's return address, creates a 4KB+8 stack gap
//     (extra 8 for 16-byte alignment), then CALLs the C++ hook.
//     When the C++ hook returns, restores the game's return address
//     at [RSP] and RETs to the game caller.
//
//  2. TRAMPOLINE WRAPPER (what the C++ hook calls as "original"):
//     Swaps RSP to captured game RSP, patches [RSP] with a return_point
//     address, sets RAX = RSP, JMPs to trampoline+3 (past mov rax,rsp).
//     The original function runs with ZERO extra bytes on stack — perfect
//     alignment.  It RETs to return_point, which swaps back and returns
//     to the C++ hook.
//
//  Thread safety: Each hook has its own data slots.  Kenshi game logic is
//  single-threaded.  For multi-thread safety, switch to TLS.
//
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>

namespace kmp {

struct MovRaxRspHook {
    void* nakedDetour       = nullptr;  // MinHook relay should JMP here
    void* trampolineWrapper = nullptr;  // C++ hook should CALL this as "original"
    void* rawTrampoline     = nullptr;  // MinHook's raw trampoline (starts with mov rax,rsp)
                                        // Safe for REENTRANT calls — no global slot manipulation
    void* capturedRspSlot   = nullptr;  // Pointer to uint64_t holding captured RSP
    volatile int32_t* bypassFlag = nullptr; // Pointer to bypass flag (1=passthrough, 0=hook active)
    void* allocBase         = nullptr;  // VirtualAlloc base (for cleanup)
    size_t allocSize        = 0;        // Allocation size
};

// Build the two ASM stubs for a mov-rax-rsp function hook.
//
// Usage:
//   1. MH_CreateHook(target, cppDetour, &trampoline)
//   2. BuildMovRaxRspHook(name, cppDetour, trampoline, 3)
//   3. Patch MinHook's relay to JMP to hook.nakedDetour
//   4. Set *original = hook.trampolineWrapper
//
// Parameters:
//   name             - Hook name for logging
//   cppDetour        - Your C++ __fastcall hook function
//   trampoline       - MinHook's trampoline (from MH_CreateHook's ppOriginal)
//   trampolineOffset - Bytes to skip in trampoline (3 for `mov rax, rsp`)
// Page layout constants
constexpr int MOVRAXRSP_PAGE_SIZE    = 0x200;
constexpr int MOVRAXRSP_NAKED_OFFSET = 0x40;   // naked detour stub starts here

// Allocate a page for MovRaxRsp fix stubs.
// Returns executable memory (PAGE_EXECUTE_READWRITE) with INT3 fill + zeroed data slots.
// The naked detour address is at (returned_ptr + MOVRAXRSP_NAKED_OFFSET).
// Pass that address to MH_CreateHook as the detour, then call BuildMovRaxRspHookAt.
void* AllocMovRaxRspPage();

// Build naked detour + trampoline wrapper into a pre-allocated page.
// The page must have been allocated by AllocMovRaxRspPage().
// Call MH_CreateHook FIRST with (page + MOVRAXRSP_NAKED_OFFSET) as detour,
// then call this with the resulting trampoline.
//
// On success, result.allocBase == page (ownership transferred to result).
// On failure, returns empty result — caller must VirtualFree the page.
MovRaxRspHook BuildMovRaxRspHookAt(
    void* page,
    const std::string& name,
    void* cppDetour,
    void* trampoline,
    int trampolineOffset = 3
);

// Legacy API — allocates its own page internally.
// For new code, prefer AllocMovRaxRspPage + BuildMovRaxRspHookAt.
MovRaxRspHook BuildMovRaxRspHook(
    const std::string& name,
    void* cppDetour,
    void* trampoline,
    int trampolineOffset = 3
);

void FreeMovRaxRspHook(MovRaxRspHook& hook);

bool TrampolineHasMovRaxRsp(void* trampoline);

} // namespace kmp

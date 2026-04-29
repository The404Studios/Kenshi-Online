#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Assertion Framework
// ═══════════════════════════════════════════════════════════════════════════
// Comprehensive assertion macros for the Kenshi-Online engine pipeline.
// Every boundary, every memory access, every state transition gets checked.
//
// Macro Summary:
//   KMP_ASSERT(expr)           — Debug-only assertion (removed in Release)
//   KMP_ASSERT_MSG(expr, msg)  — Debug assertion with custom message
//   KMP_VERIFY(expr)           — Always checked (stays in Release)
//   KMP_VERIFY_MSG(expr, msg)  — Always checked with message
//   KMP_ENSURE(expr)           — Returns expr, warns if null/zero
//   KMP_PRECONDITION(expr)     — Semantic: function entry check
//   KMP_POSTCONDITION(expr)    — Semantic: function exit check
//   KMP_INVARIANT(expr)        — Semantic: loop/class invariant check
//   KMP_UNREACHABLE()          — Marks code that should never execute
//   KMP_FAIL(msg)              — Unconditional failure
//   KMP_ASSERT_VALID_PTR(p)    — Validates a pointer looks reasonable
//   KMP_ASSERT_OFFSET(off)     — Validates an offset is not -1 (unresolved)
//   KMP_ASSERT_ENTITY(id)      — Validates an entity ID is not INVALID_ENTITY
//   KMP_ASSERT_PLAYER(id)      — Validates a player ID is not INVALID_PLAYER
//   KMP_ASSERT_RANGE(v,lo,hi)  — Validates v is in [lo, hi]
//   KMP_ASSERT_FINITE(v)       — Validates float is not NaN/Inf
//   KMP_COND(cond, action)     — Conditional execution with logging
//   KMP_COND_OR(cond, fallback)— Conditional with fallback value

#include <cstdint>
#include <cmath>
#include <spdlog/spdlog.h>

// ── Build Configuration ──
#ifdef NDEBUG
#define KMP_DEBUG 0
#else
#define KMP_DEBUG 1
#endif

namespace kmp::detail {

// Assertion failure handler — logs and optionally breaks into debugger
inline void AssertFailed(const char* expr, const char* file, int line, const char* func) {
    spdlog::critical("ASSERT FAILED: `{}` at {}:{} in {}", expr, file, line, func);
#if KMP_DEBUG
    __debugbreak();
#endif
}

inline void AssertFailedMsg(const char* expr, const char* msg, const char* file, int line, const char* func) {
    spdlog::critical("ASSERT FAILED: `{}` — {} at {}:{} in {}", expr, msg, file, line, func);
#if KMP_DEBUG
    __debugbreak();
#endif
}

// Verify failure handler — always active, no debugbreak in Release
inline void VerifyFailed(const char* expr, const char* file, int line, const char* func) {
    spdlog::error("VERIFY FAILED: `{}` at {}:{} in {}", expr, file, line, func);
#if KMP_DEBUG
    __debugbreak();
#endif
}

inline void VerifyFailedMsg(const char* expr, const char* msg, const char* file, int line, const char* func) {
    spdlog::error("VERIFY FAILED: `{}` — {} at {}:{} in {}", expr, msg, file, line, func);
#if KMP_DEBUG
    __debugbreak();
#endif
}

// Ensure helper — returns the value, warns if it evaluates to false
template<typename T>
inline T EnsureNotNull(T val, const char* expr, const char* file, int line, const char* func) {
    if (!val) {
        spdlog::warn("ENSURE: `{}` is null/zero at {}:{} in {}", expr, file, line, func);
    }
    return val;
}

// Pointer validation — checks for obviously invalid addresses
inline bool IsValidPointer(uintptr_t ptr) {
    // Null check
    if (ptr == 0) return false;
    // Below minimum user-space address (first 64KB is reserved on Windows)
    if (ptr < 0x10000) return false;
    // Above maximum user-space address (Windows x64 canonical limit)
    if (ptr > 0x00007FFFFFFFFFFF) return false;
    // Uninitialized memory patterns
    if (ptr == 0xCCCCCCCCCCCCCCCC) return false; // MSVC debug fill
    if (ptr == 0xCDCDCDCDCDCDCDCD) return false; // MSVC heap fill
    if (ptr == 0xDDDDDDDDDDDDDDDD) return false; // MSVC freed memory
    if (ptr == 0xFDFDFDFDFDFDFDFD) return false; // MSVC fence bytes
    if (ptr == 0xBAADF00DBAADF00D) return false; // Windows LocalAlloc
    if (ptr == 0xDEADBEEFDEADBEEF) return false; // Common sentinel
    return true;
}

inline bool IsValidPointer(const void* ptr) {
    return IsValidPointer(reinterpret_cast<uintptr_t>(ptr));
}

// Conditional execution with logging
inline void CondFailed(const char* cond, const char* file, int line, const char* func) {
    spdlog::debug("KMP_COND skipped: `{}` at {}:{} in {}", cond, file, line, func);
}

} // namespace kmp::detail

// ═══════════════════════════════════════════════════════════════════════════
//  Debug-only assertions (compiled out in Release)
// ═══════════════════════════════════════════════════════════════════════════

#if KMP_DEBUG
#define KMP_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            kmp::detail::AssertFailed(#expr, __FILE__, __LINE__, __FUNCTION__); \
        } \
    } while (0)

#define KMP_ASSERT_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            kmp::detail::AssertFailedMsg(#expr, msg, __FILE__, __LINE__, __FUNCTION__); \
        } \
    } while (0)
#else
#define KMP_ASSERT(expr)          ((void)0)
#define KMP_ASSERT_MSG(expr, msg) ((void)0)
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Always-active assertions (remain in Release builds)
// ═══════════════════════════════════════════════════════════════════════════

#define KMP_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            kmp::detail::VerifyFailed(#expr, __FILE__, __LINE__, __FUNCTION__); \
        } \
    } while (0)

#define KMP_VERIFY_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            kmp::detail::VerifyFailedMsg(#expr, msg, __FILE__, __LINE__, __FUNCTION__); \
        } \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
//  Ensure — returns value, warns on null/zero
// ═══════════════════════════════════════════════════════════════════════════

#define KMP_ENSURE(expr) \
    kmp::detail::EnsureNotNull((expr), #expr, __FILE__, __LINE__, __FUNCTION__)

// ═══════════════════════════════════════════════════════════════════════════
//  Semantic assertions — same mechanics, different intent
// ═══════════════════════════════════════════════════════════════════════════

#define KMP_PRECONDITION(expr)  KMP_ASSERT_MSG(expr, "Precondition violated")
#define KMP_POSTCONDITION(expr) KMP_ASSERT_MSG(expr, "Postcondition violated")
#define KMP_INVARIANT(expr)     KMP_ASSERT_MSG(expr, "Invariant violated")

// ═══════════════════════════════════════════════════════════════════════════
//  Unreachable / Fail
// ═══════════════════════════════════════════════════════════════════════════

#define KMP_UNREACHABLE() \
    do { \
        kmp::detail::AssertFailed("UNREACHABLE", __FILE__, __LINE__, __FUNCTION__); \
        __assume(0); \
    } while (0)

#define KMP_FAIL(msg) \
    do { \
        kmp::detail::AssertFailedMsg("FAIL", msg, __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

// ═══════════════════════════════════════════════════════════════════════════
//  Domain-specific assertions
// ═══════════════════════════════════════════════════════════════════════════

// Pointer validation — catches null, freed, uninitialized, and out-of-range pointers
#define KMP_ASSERT_VALID_PTR(ptr) \
    KMP_ASSERT_MSG(kmp::detail::IsValidPointer(reinterpret_cast<uintptr_t>(ptr)), \
                   "Invalid pointer (null, freed, or out of user-space range)")

// Offset validation — catches unresolved offsets (-1 sentinel from game_types.h)
#define KMP_ASSERT_OFFSET(offset) \
    KMP_ASSERT_MSG((offset) >= 0, "Unresolved offset (-1): not yet discovered by scanner or runtime probe")

// Entity ID validation
#define KMP_ASSERT_ENTITY(id) \
    KMP_ASSERT_MSG((id) != 0, "Invalid entity ID (INVALID_ENTITY)")

// Player ID validation
#define KMP_ASSERT_PLAYER(id) \
    KMP_ASSERT_MSG((id) != 0, "Invalid player ID (INVALID_PLAYER)")

// Range check
#define KMP_ASSERT_RANGE(val, lo, hi) \
    KMP_ASSERT_MSG((val) >= (lo) && (val) <= (hi), "Value out of range")

// Float sanity — catches NaN and Inf from corrupt memory or bad math
#define KMP_ASSERT_FINITE(val) \
    KMP_ASSERT_MSG(std::isfinite(val), "Non-finite float (NaN or Inf)")

// ═══════════════════════════════════════════════════════════════════════════
//  Conditional execution — run action only if condition holds, log skip
// ═══════════════════════════════════════════════════════════════════════════

// Execute action only if condition is true; log when skipped
#define KMP_COND(cond, action) \
    do { \
        if (cond) { action; } \
        else { kmp::detail::CondFailed(#cond, __FILE__, __LINE__, __FUNCTION__); } \
    } while (0)

// Return value if condition true, otherwise return fallback (with log)
#define KMP_COND_OR(cond, value, fallback) \
    ((cond) ? (value) : (kmp::detail::CondFailed(#cond, __FILE__, __LINE__, __FUNCTION__), (fallback)))

// ═══════════════════════════════════════════════════════════════════════════
//  Guard — early return if condition fails
// ═══════════════════════════════════════════════════════════════════════════

// Return from function if condition is false (for void functions)
#define KMP_GUARD(cond) \
    do { \
        if (!(cond)) { \
            spdlog::debug("KMP_GUARD: `{}` failed at {}:{} in {}", #cond, __FILE__, __LINE__, __FUNCTION__); \
            return; \
        } \
    } while (0)

// Return specific value from function if condition is false
#define KMP_GUARD_RET(cond, retval) \
    do { \
        if (!(cond)) { \
            spdlog::debug("KMP_GUARD: `{}` failed at {}:{} in {}", #cond, __FILE__, __LINE__, __FUNCTION__); \
            return (retval); \
        } \
    } while (0)

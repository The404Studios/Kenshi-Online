#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Safe Memory — Validated memory access for the engine pipeline
// ═══════════════════════════════════════════════════════════════════════════
// Wraps the raw Memory class with comprehensive validation:
//   - Pointer validation before every dereference
//   - Offset validation (catches unresolved -1 offsets)
//   - SEH protection against access violations
//   - Diagnostic logging for failed reads/writes
//   - Pointer chain following with per-step validation
//   - Result<T> return types for composable error handling
//
// Use SafeMemory instead of raw Memory for all game memory access.
// The raw Memory class remains for low-level / performance-critical paths
// where validation overhead is unacceptable (e.g., inner interpolation loop).

#include "kmp/assert.h"
#include "kmp/result.h"
#include "kmp/types.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <Windows.h>
#include <spdlog/spdlog.h>

namespace kmp {

class SafeMemory {
public:
    // ── Pointer Validation ──

    // Check if a pointer looks like a valid user-space address
    static bool IsValid(uintptr_t addr) {
        return detail::IsValidPointer(addr);
    }

    static bool IsValid(const void* ptr) {
        return detail::IsValidPointer(ptr);
    }

    // Validate a pointer and log why it's invalid
    static bool Validate(uintptr_t addr, const char* context = nullptr) {
        if (addr == 0) {
            if (context) spdlog::debug("SafeMemory: null pointer in {}", context);
            return false;
        }
        if (addr < 0x10000) {
            if (context) spdlog::debug("SafeMemory: low address 0x{:X} in {}", addr, context);
            return false;
        }
        if (addr > 0x00007FFFFFFFFFFF) {
            if (context) spdlog::debug("SafeMemory: kernel address 0x{:X} in {}", addr, context);
            return false;
        }
        if (addr == 0xCCCCCCCCCCCCCCCC || addr == 0xCDCDCDCDCDCDCDCD ||
            addr == 0xDDDDDDDDDDDDDDDD || addr == 0xFDFDFDFDFDFDFDFD) {
            if (context) spdlog::debug("SafeMemory: debug fill pattern 0x{:X} in {}", addr, context);
            return false;
        }
        return true;
    }

    // ── Validated Read ──

    // Read a value with full validation. Returns Result<T>.
    template<typename T>
    static Result<T> Read(uintptr_t addr, const char* context = nullptr) {
        if (!Validate(addr, context)) {
            return Err(fmt::format("Invalid read address 0x{:X}{}{}", addr,
                                   context ? " in " : "", context ? context : ""));
        }

        T value{};
        __try {
            value = *reinterpret_cast<const T*>(addr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Err(fmt::format("Access violation reading 0x{:X}{}{}", addr,
                                   context ? " in " : "", context ? context : ""));
        }
        return Ok(value);
    }

    // Read into an output parameter (legacy-compatible). Returns bool.
    template<typename T>
    static bool ReadInto(uintptr_t addr, T& out, const char* context = nullptr) {
        auto result = Read<T>(addr, context);
        if (result) {
            out = *result;
            return true;
        }
        return false;
    }

    // ── Validated Read at Offset ──

    // Read a value at base + offset with validation of both.
    // Checks offset is not -1 (unresolved from game_types.h).
    template<typename T>
    static Result<T> ReadAt(uintptr_t base, int offset, const char* context = nullptr) {
        if (offset < 0) {
            return Err(fmt::format("Unresolved offset ({}) — not yet discovered{}{}", offset,
                                   context ? " in " : "", context ? context : ""));
        }
        return Read<T>(base + static_cast<uintptr_t>(offset), context);
    }

    // Read at offset into output parameter. Returns bool.
    template<typename T>
    static bool ReadAtInto(uintptr_t base, int offset, T& out, const char* context = nullptr) {
        auto result = ReadAt<T>(base, offset, context);
        if (result) {
            out = *result;
            return true;
        }
        return false;
    }

    // ── Validated Write ──

    template<typename T>
    static Result<void> Write(uintptr_t addr, const T& value, const char* context = nullptr) {
        if (!Validate(addr, context)) {
            return Err(fmt::format("Invalid write address 0x{:X}{}{}", addr,
                                   context ? " in " : "", context ? context : ""));
        }

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T),
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return Err(fmt::format("VirtualProtect failed at 0x{:X}{}{}", addr,
                                   context ? " in " : "", context ? context : ""));
        }

        bool ok = true;
        __try {
            *reinterpret_cast<T*>(addr) = value;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }

        VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), oldProtect, &oldProtect);

        if (!ok) {
            return Err(fmt::format("Access violation writing 0x{:X}{}{}", addr,
                                   context ? " in " : "", context ? context : ""));
        }
        return Ok();
    }

    template<typename T>
    static Result<void> WriteAt(uintptr_t base, int offset, const T& value, const char* context = nullptr) {
        if (offset < 0) {
            return Err(fmt::format("Unresolved offset ({}) for write{}{}", offset,
                                   context ? " in " : "", context ? context : ""));
        }
        return Write(base + static_cast<uintptr_t>(offset), value, context);
    }

    // ── Validated Pointer Chain ──

    // Follow a chain of pointer dereferences with validation at each step.
    // Each offset dereferences the current address, reads a pointer, and moves to it.
    // The final offset is NOT dereferenced — it's added to return the target address.
    //
    // Example: FollowChain(charPtr, {0x2B8, 0x5F8}, 0x40)
    //   → Read pointer at charPtr+0x2B8
    //   → Read pointer at result+0x5F8
    //   → Return result+0x40 (target address, not dereferenced)
    static Result<uintptr_t> FollowChain(uintptr_t base,
                                          std::initializer_list<int> ptrOffsets,
                                          int finalOffset = 0,
                                          const char* context = nullptr) {
        if (!Validate(base, context)) {
            return Err(fmt::format("Invalid chain base 0x{:X}", base));
        }

        uintptr_t addr = base;
        int step = 0;

        for (int offset : ptrOffsets) {
            if (offset < 0) {
                return Err(fmt::format("Unresolved offset at chain step {} (offset={}){}{}", step, offset,
                                       context ? " in " : "", context ? context : ""));
            }

            uintptr_t next = 0;
            __try {
                next = *reinterpret_cast<uintptr_t*>(addr + offset);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return Err(fmt::format("AV at chain step {} (0x{:X}+0x{:X}){}{}", step, addr, offset,
                                       context ? " in " : "", context ? context : ""));
            }

            if (!Validate(next, context)) {
                return Err(fmt::format("Invalid pointer at chain step {} (0x{:X}+0x{:X} -> 0x{:X}){}{}",
                                       step, addr, offset, next,
                                       context ? " in " : "", context ? context : ""));
            }

            addr = next;
            step++;
        }

        if (finalOffset < 0) {
            return Err(fmt::format("Unresolved final offset ({}) in chain{}{}", finalOffset,
                                   context ? " in " : "", context ? context : ""));
        }

        return Ok(addr + static_cast<uintptr_t>(finalOffset));
    }

    // Follow chain and read a value at the end
    template<typename T>
    static Result<T> ReadChain(uintptr_t base,
                                std::initializer_list<int> ptrOffsets,
                                int finalOffset,
                                const char* context = nullptr) {
        KMP_TRY(addr, FollowChain(base, ptrOffsets, finalOffset, context));
        return Read<T>(addr, context);
    }

    // ── Vec3 Read/Write ──

    static Result<Vec3> ReadVec3(uintptr_t addr, const char* context = nullptr) {
        if (!Validate(addr, context)) {
            return Err(fmt::format("Invalid Vec3 address 0x{:X}", addr));
        }

        struct { float x, y, z; } raw{};
        __try {
            raw = *reinterpret_cast<decltype(raw)*>(addr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Err(fmt::format("AV reading Vec3 at 0x{:X}", addr));
        }

        Vec3 v{raw.x, raw.y, raw.z};

        // Validate no NaN/Inf — corrupt memory often manifests as non-finite floats
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            spdlog::warn("SafeMemory: Non-finite Vec3 ({}, {}, {}) at 0x{:X}{}{}",
                         v.x, v.y, v.z, addr, context ? " in " : "", context ? context : "");
            return Ok(Vec3{0.f, 0.f, 0.f}); // Return zero vector rather than corrupt data
        }

        return Ok(v);
    }

    static Result<Vec3> ReadVec3At(uintptr_t base, int offset, const char* context = nullptr) {
        if (offset < 0) return Err("Unresolved Vec3 offset");
        return ReadVec3(base + static_cast<uintptr_t>(offset), context);
    }

    static Result<void> WriteVec3(uintptr_t addr, const Vec3& v, const char* context = nullptr) {
        KMP_TRY_VOID(Write<float>(addr, v.x, context));
        KMP_TRY_VOID(Write<float>(addr + 4, v.y, context));
        KMP_TRY_VOID(Write<float>(addr + 8, v.z, context));
        return Ok();
    }

    // ── Kenshi std::string Read ──
    // MSVC x64 Release std::string layout:
    //   +0x00: union { char buf[16]; char* ptr; }  (SSO buffer or heap pointer)
    //   +0x10: size_t size
    //   +0x18: size_t capacity
    // If capacity >= 16, the string is heap-allocated and buf is a pointer.
    // If capacity < 16, the string is stored inline in buf.

    static Result<std::string> ReadKenshiString(uintptr_t addr, const char* context = nullptr) {
        if (!Validate(addr, context)) {
            return Err(fmt::format("Invalid string address 0x{:X}", addr));
        }

        size_t size = 0, capacity = 0;
        if (!ReadInto(addr + 0x10, size, context)) return Err("Failed to read string size");
        if (!ReadInto(addr + 0x18, capacity, context)) return Err("Failed to read string capacity");

        // Sanity bounds — Kenshi strings should never be this long
        if (size > 4096) {
            return Err(fmt::format("String size {} exceeds sanity limit at 0x{:X}", size, addr));
        }

        const char* strData = nullptr;
        if (capacity >= 16) {
            // Heap-allocated: read pointer
            uintptr_t heapPtr = 0;
            if (!ReadInto(addr, heapPtr, context)) return Err("Failed to read string heap pointer");
            if (!Validate(heapPtr, context)) return Err("Invalid string heap pointer");
            strData = reinterpret_cast<const char*>(heapPtr);
        } else {
            // SSO: data is inline
            strData = reinterpret_cast<const char*>(addr);
        }

        std::string result;
        __try {
            result.assign(strData, size);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Err(fmt::format("AV reading string data at 0x{:X}", reinterpret_cast<uintptr_t>(strData)));
        }

        return Ok(std::move(result));
    }

    // ── Health Array Read ──
    // Kenshi health chain: character+chain1 -> +chain2 -> +healthBase
    // Each body part: stride bytes apart (float health + float stun)

    static Result<LimbHealth> ReadHealthArray(uintptr_t charPtr,
                                               int chain1, int chain2,
                                               int healthBase, int stride,
                                               const char* context = "health") {
        KMP_TRY(healthAddr, FollowChain(charPtr, {chain1, chain2}, healthBase, context));

        LimbHealth limbs;
        for (int i = 0; i < static_cast<int>(BodyPart::Count); i++) {
            auto result = Read<float>(healthAddr + i * stride, context);
            if (result) {
                float hp = *result;
                // Clamp to valid range — corrupt memory can give wild values
                if (!std::isfinite(hp)) hp = 0.f;
                if (hp < -200.f) hp = -200.f;
                if (hp > 200.f) hp = 200.f;
                limbs.hp[i] = hp;
            } else {
                limbs.hp[i] = 0.f; // Default to 0 on read failure
            }
        }

        return Ok(limbs);
    }

    // ── Batch operations ──

    // Read an array of pointers (e.g., squad member list, entity list)
    static Result<std::vector<uintptr_t>> ReadPointerArray(uintptr_t arrayBase, int count,
                                                            const char* context = nullptr) {
        if (!Validate(arrayBase, context)) {
            return Err("Invalid array base pointer");
        }
        if (count < 0 || count > 10000) {
            return Err(fmt::format("Suspicious array count: {}", count));
        }

        std::vector<uintptr_t> result;
        result.reserve(count);

        for (int i = 0; i < count; i++) {
            uintptr_t elem = 0;
            __try {
                elem = *reinterpret_cast<uintptr_t*>(arrayBase + i * sizeof(uintptr_t));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::debug("SafeMemory: AV reading array element {} at 0x{:X}", i,
                              arrayBase + i * sizeof(uintptr_t));
                break;
            }
            result.push_back(elem);
        }

        return Ok(std::move(result));
    }
};

} // namespace kmp

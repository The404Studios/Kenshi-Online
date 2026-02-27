#pragma once
// SEH-protected trampoline call wrappers.
//
// Each hook's trampoline (original function) call is wrapped in __try/__except
// so that if the function signature is wrong (wrong parameter count, wrong types),
// the game doesn't crash — instead we catch the exception, log it, and return
// a safe default.
//
// MSVC restriction: __try/__except cannot be used in functions that contain
// C++ objects with destructors. These wrapper functions use only C types.

#include <cstdint>
#include <atomic>
#include <Windows.h>

namespace kmp {

// Per-hook health status. If a hook's trampoline crashes, it gets marked
// as failed and the hook stops calling the trampoline.
struct HookHealth {
    std::atomic<bool> trampolineFailed{false};
    std::atomic<int>  failCount{0};
    const char* name = "";
};

// ── Safe call wrappers for different function signatures ──
// These are __declspec(noinline) C-style functions that wrap trampoline calls
// in SEH. They must NOT contain any C++ objects with destructors.

// void fn(void*)
__declspec(noinline)
inline bool SafeCall_Void_Ptr(void* fn, void* a1, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*);
    __try {
        reinterpret_cast<Fn>(fn)(a1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void* fn(void*, void*) — CharacterCreate
__declspec(noinline)
inline void* SafeCall_Ptr_PtrPtr(void* fn, void* a1, void* a2, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return nullptr;
    using Fn = void*(__fastcall*)(void*, void*);
    __try {
        return reinterpret_cast<Fn>(fn)(a1, a2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return nullptr;
    }
}

// void fn(void*, float, float, float) — SetPosition
__declspec(noinline)
inline bool SafeCall_Void_PtrFFF(void* fn, void* a1, float a2, float a3, float a4, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, float, float, float);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2, a3, a4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, float, float, float, int) — MoveTo
__declspec(noinline)
inline bool SafeCall_Void_PtrFFFI(void* fn, void* a1, float a2, float a3, float a4, int a5, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, float, float, float, int);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2, a3, a4, a5);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, void*, int, float, float, float) — ApplyDamage
__declspec(noinline)
inline bool SafeCall_Void_PtrPtrIFFF(void* fn, void* a1, void* a2, int a3,
                                       float a4, float a5, float a6, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, void*, int, float, float, float);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2, a3, a4, a5, a6);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, void*) — CharacterDeath
__declspec(noinline)
inline bool SafeCall_Void_PtrPtr(void* fn, void* a1, void* a2, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, void*);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, int, int) — ZoneLoad/ZoneUnload
__declspec(noinline)
inline bool SafeCall_Void_PtrII(void* fn, void* a1, int a2, int a3, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, int, int);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2, a3);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, void*, float, float, float) — BuildingPlace
__declspec(noinline)
inline bool SafeCall_Void_PtrPtrFFF(void* fn, void* a1, void* a2,
                                      float a3, float a4, float a5, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, void*, float, float, float);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2, a3, a4, a5);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

// void fn(void*, const char*) — SaveGame/LoadGame
__declspec(noinline)
inline bool SafeCall_Void_PtrStr(void* fn, void* a1, const char* a2, HookHealth* health) {
    if (!fn || (health && health->trampolineFailed.load())) return false;
    using Fn = void(__fastcall*)(void*, const char*);
    __try {
        reinterpret_cast<Fn>(fn)(a1, a2);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (health) {
            health->trampolineFailed.store(true);
            health->failCount.fetch_add(1);
        }
        return false;
    }
}

} // namespace kmp

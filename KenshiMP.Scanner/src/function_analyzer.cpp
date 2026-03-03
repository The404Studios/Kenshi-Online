#include "kmp/function_analyzer.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <Windows.h>

namespace kmp {

// C-style SEH wrapper — no C++ objects with destructors allowed
static bool ReadPrologueBytes(uintptr_t address, uint8_t* outBytes, int count) {
    __try {
        auto* code = reinterpret_cast<const uint8_t*>(address);
        std::memcpy(outBytes, code, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

FunctionSignature FunctionAnalyzer::Analyze(uintptr_t address, const std::string& name) {
    FunctionSignature sig;
    sig.address = address;
    sig.name = name;

    if (address == 0) return sig;

    // Read prologue bytes safely via SEH wrapper
    sig.prologueLen = 64;
    if (!ReadPrologueBytes(address, sig.prologueBytes, 64)) {
        spdlog::warn("FunctionAnalyzer: Failed to read prologue for '{}' at 0x{:X}", name, address);
        return sig;
    }

    // Run analysis passes
    DetectShadowSpaceSaves(sig.prologueBytes, sig.prologueLen, sig);
    DetectStackFrame(sig.prologueBytes, sig.prologueLen, sig);
    DetectPushedRegisters(sig.prologueBytes, sig.prologueLen, sig);
    DetectRegisterUsage(sig.prologueBytes, sig.prologueLen, sig);

    // Estimate parameter count
    sig.estimatedParams = CountParamsFromRegisters(sig);

    return sig;
}

bool FunctionAnalyzer::ValidateSignature(const FunctionSignature& sig, int hookParamCount) {
    if (!sig.IsValid()) return false;

    // If we detected parameters, check if hook matches
    if (sig.estimatedParams > 0) {
        // Allow some tolerance — we might miss params that are only used deep in the function
        int diff = std::abs(sig.estimatedParams - hookParamCount);
        if (diff > 2) {
            spdlog::warn("FunctionAnalyzer: Signature mismatch for '{}': "
                         "hook has {} params, analysis detected ~{} params",
                         sig.name, hookParamCount, sig.estimatedParams);
            return false;
        }
    }

    return true;
}

void FunctionAnalyzer::LogAnalysis(const std::vector<FunctionSignature>& signatures) {
    spdlog::info("FunctionAnalyzer: === Prologue Analysis Results ===");

    for (const auto& sig : signatures) {
        if (!sig.IsValid()) continue;

        // Build prologue hex string (first 16 bytes for log)
        char hexBuf[128];
        int pos = 0;
        for (int i = 0; i < 16 && i < sig.prologueLen; i++) {
            pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", sig.prologueBytes[i]);
        }
        if (pos > 0) hexBuf[pos - 1] = '\0'; // remove trailing space

        spdlog::info("  {} @ 0x{:X}: params~{}, stack={}, pushed={}, shadow={}, regs=[{}{}{}{}] bytes=[{}]",
                     sig.name, sig.address,
                     sig.estimatedParams,
                     sig.stackFrameSize,
                     sig.pushedRegs,
                     sig.shadowSpaceUsed,
                     sig.usesRCX ? "RCX " : "",
                     sig.usesRDX ? "RDX " : "",
                     sig.usesR8  ? "R8 "  : "",
                     sig.usesR9  ? "R9 "  : "",
                     hexBuf);
    }

    spdlog::info("FunctionAnalyzer: === End Analysis ===");
}

int FunctionAnalyzer::CountParamsFromRegisters(const FunctionSignature& sig) {
    // In x64 __fastcall: RCX=1st, RDX=2nd, R8=3rd, R9=4th
    // For floating point: XMM0-XMM3 are used instead (in the same positional slots)
    //
    // Count the highest parameter register used.
    // If R9 is used, at least 4 params. If R8 is used, at least 3. Etc.
    // Also count float registers in the same positional slots.

    int count = 0;

    // Check integer registers
    if (sig.usesR9  || sig.usesXMM3) count = 4;
    else if (sig.usesR8  || sig.usesXMM2) count = 3;
    else if (sig.usesRDX || sig.usesXMM1) count = 2;
    else if (sig.usesRCX || sig.usesXMM0) count = 1;

    // Shadow space saves are a strong indicator
    if (sig.shadowSpaceUsed > count) {
        count = sig.shadowSpaceUsed;
    }

    // If the function accesses stack parameters, there are 5+ params
    if (sig.accessesStackParams && count < 5) {
        count = 5;
    }

    return count;
}

void FunctionAnalyzer::DetectShadowSpaceSaves(const uint8_t* code, int len, FunctionSignature& sig) {
    // Scan first 32 bytes for shadow space saves.
    // These are typically the very first instructions in the prologue.
    //
    // mov [rsp+08h], rcx   → 48 89 4C 24 08
    // mov [rsp+10h], rdx   → 48 89 54 24 10
    // mov [rsp+18h], r8    → 4C 89 44 24 18
    // mov [rsp+20h], r9    → 4C 89 4C 24 20
    //
    // Also detect: mov [rsp+08h], rbx (48 89 5C 24 08) — this is saving
    // a non-volatile register, NOT a parameter. We need to distinguish.

    int maxScan = (len < 32) ? len : 32;

    for (int i = 0; i + 5 <= maxScan; i++) {
        // 48 89 XX 24 YY — mov [rsp+YY], reg64
        if (code[i] == 0x48 && code[i + 1] == 0x89 && code[i + 3] == 0x24) {
            uint8_t modrm = code[i + 2];
            uint8_t disp = code[i + 4];
            uint8_t reg = (modrm >> 3) & 7;

            if (disp == 0x08 && reg == 1) { sig.usesRCX = true; sig.shadowSpaceUsed = std::max(sig.shadowSpaceUsed, 1); }
            if (disp == 0x10 && reg == 2) { sig.usesRDX = true; sig.shadowSpaceUsed = std::max(sig.shadowSpaceUsed, 2); }
        }

        // 4C 89 XX 24 YY — mov [rsp+YY], r8/r9/etc
        if (code[i] == 0x4C && code[i + 1] == 0x89 && code[i + 3] == 0x24) {
            uint8_t modrm = code[i + 2];
            uint8_t disp = code[i + 4];
            uint8_t reg = (modrm >> 3) & 7;

            if (disp == 0x18 && reg == 0) { sig.usesR8 = true;  sig.shadowSpaceUsed = std::max(sig.shadowSpaceUsed, 3); }
            if (disp == 0x20 && reg == 1) { sig.usesR9 = true;  sig.shadowSpaceUsed = std::max(sig.shadowSpaceUsed, 4); }
        }
    }
}

void FunctionAnalyzer::DetectStackFrame(const uint8_t* code, int len, FunctionSignature& sig) {
    // Look for:
    //   sub rsp, imm8:  48 83 EC XX
    //   sub rsp, imm32: 48 81 EC XX XX XX XX

    for (int i = 0; i + 4 <= len; i++) {
        // sub rsp, imm8
        if (code[i] == 0x48 && code[i + 1] == 0x83 && code[i + 2] == 0xEC) {
            sig.stackFrameSize = code[i + 3];
            return;
        }
        // sub rsp, imm32
        if (i + 7 <= len && code[i] == 0x48 && code[i + 1] == 0x81 && code[i + 2] == 0xEC) {
            int32_t frameSize;
            std::memcpy(&frameSize, &code[i + 3], 4);
            sig.stackFrameSize = frameSize;
            return;
        }
    }
}

void FunctionAnalyzer::DetectPushedRegisters(const uint8_t* code, int len, FunctionSignature& sig) {
    // Count push instructions at the start of the function.
    // Standard pushes: 50-57 (rax-rdi), 41 50-57 (r8-r15)
    // REX pushes: 40 50-57

    int i = 0;
    while (i < len) {
        // Skip shadow space saves (mov [rsp+xx], reg) — they are 5 bytes: 48/4C 89 XX 24 XX
        if (i + 5 <= len && (code[i] == 0x48 || code[i] == 0x4C) && code[i + 1] == 0x89 && code[i + 3] == 0x24) {
            i += 5;
            continue;
        }

        // Standard push (50-57)
        if (code[i] >= 0x50 && code[i] <= 0x57) {
            sig.pushedRegs++;
            i++;
            continue;
        }

        // REX push (40 50-57) — push with REX prefix
        if (code[i] == 0x40 && i + 1 < len && code[i + 1] >= 0x50 && code[i + 1] <= 0x57) {
            sig.pushedRegs++;
            i += 2;
            continue;
        }

        // REX.B push (41 50-57) — push r8-r15
        if (code[i] == 0x41 && i + 1 < len && code[i + 1] >= 0x50 && code[i + 1] <= 0x57) {
            sig.pushedRegs++;
            i += 2;
            continue;
        }

        // mov rax, rsp pattern (48 8B C4) — common prologue start
        if (i + 3 <= len && code[i] == 0x48 && code[i + 1] == 0x8B && code[i + 2] == 0xC4) {
            i += 3;
            continue;
        }

        // Once we hit something else (sub rsp, lea, mov, etc.), stop counting pushes
        break;
    }
}

void FunctionAnalyzer::DetectRegisterUsage(const uint8_t* code, int len, FunctionSignature& sig) {
    // Scan the first 64 bytes for any use of parameter registers,
    // beyond shadow space saves. This includes:
    //   - mov reg, rcx/rdx/r8/r9 (register copy)
    //   - test rcx, rcx
    //   - cmp rcx, imm
    //   - Any instruction using these registers as source

    // Also check for XMM register usage (movss, movsd, etc.)
    for (int i = 0; i + 3 <= len; i++) {
        // REX.W mov reg, rcx: 48 8B XX (where src field = 1 for RCX)
        if (code[i] == 0x48 && code[i + 1] == 0x8B) {
            uint8_t modrm = code[i + 2];
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t src = modrm & 7;
            if (mod == 3) {
                if (src == 1) sig.usesRCX = true;
                if (src == 2) sig.usesRDX = true;
            }
        }

        // 4C 8B XX — REX.WR mov r-reg, reg64 (e.g., mov r8, rcx)
        // 49 8B XX — REX.WB mov reg, r-reg (e.g., mov rcx, r8)
        if (code[i] == 0x49 && code[i + 1] == 0x8B) {
            uint8_t modrm = code[i + 2];
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t src = modrm & 7;
            if (mod == 3) {
                if (src == 0) sig.usesR8 = true;
                if (src == 1) sig.usesR9 = true;
            }
        }

        // test rcx, rcx: 48 85 C9
        if (i + 3 <= len && code[i] == 0x48 && code[i + 1] == 0x85 && code[i + 2] == 0xC9) {
            sig.usesRCX = true;
        }
        // test rdx, rdx: 48 85 D2
        if (i + 3 <= len && code[i] == 0x48 && code[i + 1] == 0x85 && code[i + 2] == 0xD2) {
            sig.usesRDX = true;
        }

        // movss/movsd with XMM0-3 (F3 0F 10/11 or F2 0F 10/11)
        if (i + 4 <= len) {
            if ((code[i] == 0xF3 || code[i] == 0xF2) && code[i + 1] == 0x0F &&
                (code[i + 2] == 0x10 || code[i + 2] == 0x11)) {
                uint8_t modrm = code[i + 3];
                uint8_t reg = (modrm >> 3) & 7;
                if (reg == 0) sig.usesXMM0 = true;
                if (reg == 1) sig.usesXMM1 = true;
                if (reg == 2) sig.usesXMM2 = true;
                if (reg == 3) sig.usesXMM3 = true;
            }
        }

        // Check for stack parameter access: [rsp+28h] or higher (beyond shadow space)
        // This indicates 5+ parameters.
        // mov reg, [rsp+28h]: 48 8B XX 24 28
        if (i + 5 <= len && (code[i] == 0x48 || code[i] == 0x4C) &&
            (code[i + 1] == 0x8B || code[i + 1] == 0x89) &&
            code[i + 3] == 0x24 && code[i + 4] >= 0x28) {
            sig.accessesStackParams = true;
        }
    }
}

} // namespace kmp

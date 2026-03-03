#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace kmp {

// Analysis result for a single function
struct FunctionSignature {
    uintptr_t address = 0;
    std::string name;

    // Prologue analysis
    int stackFrameSize = 0;      // Total sub rsp, N
    int shadowSpaceUsed = 0;     // How many of RCX/RDX/R8/R9 are saved to shadow space
    int pushedRegs = 0;          // Number of push instructions in prologue
    int estimatedParams = 0;     // Best guess at parameter count

    // Detected register usage in prologue (first 64 bytes)
    bool usesRCX = false;  // 1st param
    bool usesRDX = false;  // 2nd param
    bool usesR8  = false;  // 3rd param
    bool usesR9  = false;  // 4th param
    bool usesXMM0 = false; // float 1st param
    bool usesXMM1 = false; // float 2nd param
    bool usesXMM2 = false; // float 3rd param
    bool usesXMM3 = false; // float 4th param

    // Whether the function accesses stack parameters (beyond shadow space)
    bool accessesStackParams = false;

    // Raw prologue bytes for debugging
    uint8_t prologueBytes[64] = {};
    int prologueLen = 0;

    bool IsValid() const { return address != 0; }
};

// Analyzes x64 function prologues to determine signatures.
// This helps validate that our hook function signatures match the
// actual game functions we're hooking.
class FunctionAnalyzer {
public:
    // Analyze a function at the given address
    static FunctionSignature Analyze(uintptr_t address, const std::string& name);

    // Validate that our hook signature has the right parameter count.
    // Returns true if the hook signature matches (or is close enough).
    // hookParamCount is the number of parameters in our hook typedef.
    static bool ValidateSignature(const FunctionSignature& sig, int hookParamCount);

    // Log analysis results for all functions
    static void LogAnalysis(const std::vector<FunctionSignature>& signatures);

private:
    // Count parameters from prologue register usage
    static int CountParamsFromRegisters(const FunctionSignature& sig);

    // Detect if a byte sequence stores a parameter register to shadow space
    // mov [rsp+08], rcx = 48 89 4C 24 08
    // mov [rsp+10], rdx = 48 89 54 24 10
    // mov [rsp+18], r8  = 4C 89 44 24 18
    // mov [rsp+20], r9  = 4C 89 4C 24 20
    static void DetectShadowSpaceSaves(const uint8_t* code, int len, FunctionSignature& sig);

    // Detect sub rsp, N for stack frame size
    static void DetectStackFrame(const uint8_t* code, int len, FunctionSignature& sig);

    // Detect pushed registers
    static void DetectPushedRegisters(const uint8_t* code, int len, FunctionSignature& sig);

    // Detect parameter register usage beyond shadow space saves
    static void DetectRegisterUsage(const uint8_t* code, int len, FunctionSignature& sig);
};

} // namespace kmp

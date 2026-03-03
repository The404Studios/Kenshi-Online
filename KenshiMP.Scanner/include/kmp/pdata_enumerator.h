#pragma once
// .pdata Function Boundary Enumerator
//
// Uses the PE exception directory (.pdata) to enumerate EVERY function
// in the executable with exact start/end addresses. On x64 Windows,
// every non-leaf function MUST have a RUNTIME_FUNCTION entry for
// structured exception handling (SEH). This gives us a nearly complete
// function table without any heuristics.

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace kmp {

// A single function as described by .pdata
struct FunctionEntry {
    uintptr_t   startRVA    = 0;   // Function start (RVA from module base)
    uintptr_t   endRVA      = 0;   // Function end (exclusive)
    uintptr_t   startVA     = 0;   // Virtual address (base + RVA)
    uintptr_t   endVA       = 0;   // End VA
    uint32_t    unwindRVA   = 0;   // UNWIND_INFO pointer
    size_t      size        = 0;   // Function size in bytes

    // Labels applied by other analyzers
    std::string label;              // Human-readable name (from strings, RTTI, etc.)
    std::string category;           // Category tag (e.g., "combat", "ui", "ai")
    float       confidence  = 0.0f; // Label confidence (0-1)

    // Prologue info (extracted from UNWIND_INFO)
    uint8_t     prologueSize = 0;
    uint8_t     frameRegister = 0;
    uint8_t     frameOffset = 0;
    uint16_t    unwindCodeCount = 0;

    bool IsValid() const { return startVA != 0 && size > 0; }
};

// Unwind code types from the PE spec
enum class UnwindOpCode : uint8_t {
    PUSH_NONVOL     = 0,
    ALLOC_LARGE     = 1,
    ALLOC_SMALL     = 2,
    SET_FPREG       = 3,
    SAVE_NONVOL     = 4,
    SAVE_NONVOL_FAR = 5,
    SAVE_XMM128     = 8,
    SAVE_XMM128_FAR = 9,
    PUSH_MACHFRAME  = 10,
};

struct UnwindCode {
    uint8_t       codeOffset;
    UnwindOpCode  opCode;
    uint8_t       opInfo;
    uint16_t      extraData = 0;   // For ALLOC_LARGE, SAVE_NONVOL_FAR, etc.
};

// UNWIND_INFO parsed from .pdata
struct UnwindInfo {
    uint8_t                 version     = 0;
    uint8_t                 flags       = 0;
    uint8_t                 prologueSize = 0;
    uint8_t                 frameRegister = 0;
    uint8_t                 frameOffset  = 0;
    std::vector<UnwindCode> codes;
    uintptr_t               handlerRVA  = 0;  // If UNW_FLAG_EHANDLER/UHANDLER
    uintptr_t               chainedRVA  = 0;  // If UNW_FLAG_CHAININFO
};

class PDataEnumerator {
public:
    // Initialize from a module
    bool Init(uintptr_t moduleBase, size_t moduleSize);

    // Enumerate all functions from .pdata
    bool Enumerate();

    // Access results
    const std::vector<FunctionEntry>& GetFunctions() const { return m_functions; }
    size_t GetFunctionCount() const { return m_functions.size(); }

    // Lookup by address (binary search)
    const FunctionEntry* FindFunction(uintptr_t address) const;

    // Find function containing an address
    const FunctionEntry* FindContaining(uintptr_t address) const;

    // Get functions in a size range
    std::vector<const FunctionEntry*> GetFunctionsBySize(size_t minSize, size_t maxSize) const;

    // Get functions in an address range
    std::vector<const FunctionEntry*> GetFunctionsInRange(uintptr_t start, uintptr_t end) const;

    // Parse UNWIND_INFO for a function
    UnwindInfo ParseUnwindInfo(const FunctionEntry& func) const;

    // Iterate all functions
    void ForEach(const std::function<void(const FunctionEntry&)>& callback) const;

    // Statistics
    struct Stats {
        size_t totalFunctions   = 0;
        size_t labeledFunctions = 0;
        size_t minSize          = 0;
        size_t maxSize          = 0;
        size_t avgSize          = 0;
        size_t totalCodeBytes   = 0;
    };
    Stats GetStats() const;

private:
    uintptr_t m_moduleBase = 0;
    size_t    m_moduleSize = 0;
    std::vector<FunctionEntry> m_functions;  // Sorted by startVA

    // Build sorted index for binary search
    void BuildIndex();
};

} // namespace kmp

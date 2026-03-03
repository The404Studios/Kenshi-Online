#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace kmp {

struct PatternResult {
    uintptr_t address = 0;
    bool      valid   = false;

    operator bool() const { return valid; }
    operator uintptr_t() const { return address; }
};

class PatternScanner {
public:
    // Initialize scanner for a loaded module (nullptr = main executable)
    bool Init(const char* moduleName = nullptr);

    // Scan for an IDA-style byte pattern
    // Pattern format: "48 89 5C 24 ? 57 48 83 EC 20"
    // '?' or '??' = wildcard byte
    PatternResult Find(const char* pattern) const;

    // Find with offset from match start
    PatternResult Find(const char* pattern, int offset) const;

    // Find all occurrences
    std::vector<uintptr_t> FindAll(const char* pattern) const;

    // Resolve a RIP-relative address
    // Example: LEA RAX, [RIP+0x12345] at address X with operand at offset 3, instruction length 7
    // Returns: X + 7 + *(int32_t*)(X + 3)
    static uintptr_t ResolveRIP(uintptr_t instructionAddr, int operandOffset, int instructionLength);

    // Follow a CALL instruction (E8 xx xx xx xx) to get the target
    static uintptr_t FollowCall(uintptr_t callAddr);

    // Follow a JMP instruction (E9 xx xx xx xx) to get the target
    static uintptr_t FollowJmp(uintptr_t jmpAddr);

    // Get module info
    uintptr_t GetBase() const { return m_moduleBase; }
    size_t    GetSize() const { return m_moduleSize; }

    // Restrict scanning to .text section for speed
    void SetTextOnly(bool textOnly) { m_textOnly = textOnly; }

private:
    uintptr_t m_moduleBase = 0;
    size_t    m_moduleSize = 0;
    uintptr_t m_textBase   = 0;
    size_t    m_textSize   = 0;
    bool      m_textOnly   = true;

    struct ParsedPattern {
        std::vector<uint8_t> bytes;
        std::vector<bool>    mask; // true = must match, false = wildcard
    };

    static std::optional<ParsedPattern> Parse(const char* pattern);

    uintptr_t ScanRegion(uintptr_t start, size_t size,
                         const ParsedPattern& pattern) const;

    bool FindTextSection();
};

} // namespace kmp

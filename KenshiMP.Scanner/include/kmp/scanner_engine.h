#pragma once
// Advanced Pattern Scanner Engine — SIMD-accelerated, multi-pattern, PE-aware
//
// Improvements over the original PatternScanner:
// 1. SSE2 first-byte acceleration (16 bytes at a time)
// 2. Multi-pattern batch scanning (single pass for N patterns)
// 3. Full PE section enumeration (.text, .rdata, .data, .pdata, .reloc, etc.)
// 4. Pattern composition (combine sub-patterns with offsets)
// 5. Scan result caching for repeated queries
// 6. Thread-safe scan with parallel region support

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  PE SECTION INFO
// ═══════════════════════════════════════════════════════════════════════════

struct PESection {
    char        name[9]  = {};
    uintptr_t   base     = 0;
    size_t      size     = 0;
    uint32_t    characteristics = 0;

    bool IsExecutable() const { return (characteristics & 0x20000000) != 0; }
    bool IsReadable()   const { return (characteristics & 0x40000000) != 0; }
    bool IsWritable()   const { return (characteristics & 0x80000000) != 0; }
    bool Contains(uintptr_t addr) const { return addr >= base && addr < base + size; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  SCAN RESULT
// ═══════════════════════════════════════════════════════════════════════════

struct ScanResult {
    uintptr_t   address     = 0;
    bool        valid       = false;
    const char* section     = "";   // Which PE section it was found in
    float       confidence  = 0.0f; // 0.0 - 1.0

    operator bool()      const { return valid; }
    operator uintptr_t() const { return address; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  PARSED PATTERN (public for composition)
// ═══════════════════════════════════════════════════════════════════════════

struct ParsedPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    mask;      // true = must match
    std::string          original;  // Original pattern string
    int                  offset = 0;// Offset from match start

    bool IsValid() const { return !bytes.empty(); }
    size_t Size() const { return bytes.size(); }
};

// ═══════════════════════════════════════════════════════════════════════════
//  COMPLEX PATTERN — composed of sub-patterns with relative offsets
// ═══════════════════════════════════════════════════════════════════════════

struct PatternComponent {
    ParsedPattern pattern;
    int           relativeOffset = 0;  // Offset from anchor match
    bool          required       = true;
};

struct ComplexPattern {
    std::string                   name;
    std::vector<PatternComponent> components; // First = anchor
    int                           resultOffset = 0; // Offset from anchor for final address

    // Instructions for post-processing the match
    enum class PostProcess {
        None,
        FollowCall,     // Follow E8 call at result
        FollowJmp,      // Follow E9 jmp at result
        ResolveRIP,     // Resolve RIP-relative at result
        FindPrologue,   // Walk back to function start
    };
    PostProcess postProcess = PostProcess::None;
    int         ripOperandOffset  = 0;
    int         ripInstructionLen = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  BATCH SCAN REQUEST / RESULT
// ═══════════════════════════════════════════════════════════════════════════

struct BatchEntry {
    std::string  id;
    ParsedPattern pattern;
    ScanResult   result;
};

// ═══════════════════════════════════════════════════════════════════════════
//  SCANNER ENGINE
// ═══════════════════════════════════════════════════════════════════════════

class ScannerEngine {
public:
    // Initialize for a module (nullptr = main exe)
    bool Init(const char* moduleName = nullptr);

    // ── PE Section Access ──
    const std::vector<PESection>& GetSections() const { return m_sections; }
    const PESection* FindSection(const char* name) const;
    uintptr_t GetBase() const { return m_moduleBase; }
    size_t    GetSize() const { return m_moduleSize; }

    // ── Single Pattern Scanning ──
    ScanResult Find(const char* pattern) const;
    ScanResult Find(const char* pattern, int offset) const;
    ScanResult Find(const ParsedPattern& pattern) const;
    std::vector<uintptr_t> FindAll(const char* pattern) const;
    std::vector<uintptr_t> FindAll(const ParsedPattern& pattern) const;

    // ── Section-specific scanning ──
    ScanResult FindInSection(const char* sectionName, const char* pattern) const;
    ScanResult FindInSection(const char* sectionName, const ParsedPattern& pattern) const;
    std::vector<uintptr_t> FindAllInSection(const char* sectionName, const char* pattern) const;

    // ── Complex Pattern Scanning ──
    ScanResult FindComplex(const ComplexPattern& pattern) const;

    // ── Multi-pattern batch scanning (single pass) ──
    // Scans .text once and matches all patterns simultaneously.
    // Far more efficient than N individual scans.
    void BatchScan(std::vector<BatchEntry>& entries) const;

    // ── Address Resolution ──
    static uintptr_t ResolveRIP(uintptr_t instrAddr, int operandOffset, int instrLen);
    static uintptr_t FollowCall(uintptr_t callAddr);
    static uintptr_t FollowJmp(uintptr_t jmpAddr);
    static uintptr_t FollowConditionalJmp(uintptr_t jmpAddr);

    // ── Pattern Parsing ──
    static std::optional<ParsedPattern> Parse(const char* pattern);

    // ── Cache Management ──
    void ClearCache();
    size_t CacheSize() const;

    // ── Scan Constraints ──
    void SetScanSections(const std::vector<std::string>& sectionNames);
    void SetScanAll() { m_restrictSections.clear(); }

private:
    uintptr_t m_moduleBase = 0;
    size_t    m_moduleSize = 0;
    std::vector<PESection> m_sections;
    std::vector<std::string> m_restrictSections; // Empty = scan .text

    // Result cache (pattern string → result)
    mutable std::unordered_map<std::string, ScanResult> m_cache;
    mutable std::mutex m_cacheMutex;

    // ── Internal scanning ──
    bool EnumerateSections();

    // SSE2-accelerated scan of a memory region
    uintptr_t ScanRegionSSE2(uintptr_t start, size_t size,
                              const ParsedPattern& pattern) const;

    // Fallback linear scan
    uintptr_t ScanRegionLinear(uintptr_t start, size_t size,
                                const ParsedPattern& pattern) const;

    // Find all matches in a region
    std::vector<uintptr_t> ScanRegionAll(uintptr_t start, size_t size,
                                          const ParsedPattern& pattern) const;

    // Get scan regions based on constraints
    struct ScanRegion { uintptr_t start; size_t size; const char* section; };
    std::vector<ScanRegion> GetScanRegions() const;
};

} // namespace kmp

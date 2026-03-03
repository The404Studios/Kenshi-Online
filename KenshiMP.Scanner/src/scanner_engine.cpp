#include "kmp/scanner_engine.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <intrin.h>  // SSE2 intrinsics

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

bool ScannerEngine::Init(const char* moduleName) {
    HMODULE hModule = moduleName ? GetModuleHandleA(moduleName) : GetModuleHandleA(nullptr);
    if (!hModule) {
        spdlog::error("ScannerEngine: Failed to get module handle for '{}'",
                      moduleName ? moduleName : "main");
        return false;
    }

    m_moduleBase = reinterpret_cast<uintptr_t>(hModule);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m_moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m_moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    m_moduleSize = nt->OptionalHeader.SizeOfImage;

    if (!EnumerateSections()) return false;

    spdlog::info("ScannerEngine: Module '{}' at 0x{:X}, size 0x{:X}, {} sections",
                 moduleName ? moduleName : "main",
                 m_moduleBase, m_moduleSize, m_sections.size());

    for (const auto& sec : m_sections) {
        spdlog::info("  Section '{}': base=0x{:X} size=0x{:X} [{}{}{}]",
                     sec.name, sec.base, sec.size,
                     sec.IsExecutable() ? "X" : "-",
                     sec.IsReadable() ? "R" : "-",
                     sec.IsWritable() ? "W" : "-");
    }

    return true;
}

bool ScannerEngine::EnumerateSections() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m_moduleBase);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m_moduleBase + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);

    m_sections.clear();
    m_sections.reserve(nt->FileHeader.NumberOfSections);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        PESection sec;
        std::memcpy(sec.name, section->Name, 8);
        sec.name[8] = '\0';
        sec.base = m_moduleBase + section->VirtualAddress;
        sec.size = section->Misc.VirtualSize;
        sec.characteristics = section->Characteristics;
        m_sections.push_back(sec);
    }

    return !m_sections.empty();
}

const PESection* ScannerEngine::FindSection(const char* name) const {
    for (const auto& sec : m_sections) {
        if (std::strcmp(sec.name, name) == 0) return &sec;
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PATTERN PARSING
// ═══════════════════════════════════════════════════════════════════════════

std::optional<ParsedPattern> ScannerEngine::Parse(const char* pattern) {
    ParsedPattern result;
    result.original = pattern;
    std::istringstream stream(pattern);
    std::string token;

    while (stream >> token) {
        if (token == "?" || token == "??") {
            result.bytes.push_back(0);
            result.mask.push_back(false);
        } else {
            unsigned long byte = std::strtoul(token.c_str(), nullptr, 16);
            result.bytes.push_back(static_cast<uint8_t>(byte));
            result.mask.push_back(true);
        }
    }

    if (result.bytes.empty()) return std::nullopt;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SCAN REGIONS
// ═══════════════════════════════════════════════════════════════════════════

std::vector<ScannerEngine::ScanRegion> ScannerEngine::GetScanRegions() const {
    std::vector<ScanRegion> regions;

    if (!m_restrictSections.empty()) {
        for (const auto& name : m_restrictSections) {
            if (auto* sec = FindSection(name.c_str())) {
                regions.push_back({sec->base, sec->size, sec->name});
            }
        }
    } else {
        // Default: scan .text only
        if (auto* text = FindSection(".text")) {
            regions.push_back({text->base, text->size, ".text"});
        } else {
            // Fallback: scan entire module
            regions.push_back({m_moduleBase, m_moduleSize, "module"});
        }
    }

    return regions;
}

void ScannerEngine::SetScanSections(const std::vector<std::string>& sectionNames) {
    m_restrictSections = sectionNames;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SSE2-ACCELERATED SCANNING
// ═══════════════════════════════════════════════════════════════════════════

uintptr_t ScannerEngine::ScanRegionSSE2(uintptr_t start, size_t size,
                                         const ParsedPattern& pattern) const {
    if (pattern.bytes.empty() || size < pattern.bytes.size()) return 0;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(start);
    const size_t patLen = pattern.bytes.size();
    const size_t scanEnd = size - patLen;

    // Find first non-wildcard byte for SSE2 acceleration
    int firstFixed = -1;
    for (size_t i = 0; i < patLen; i++) {
        if (pattern.mask[i]) { firstFixed = static_cast<int>(i); break; }
    }

    if (firstFixed < 0) {
        // All wildcards — match at start
        return start;
    }

    const uint8_t needle = pattern.bytes[firstFixed];

    // SSE2: broadcast the first fixed byte to all 16 lanes
    __m128i needleVec = _mm_set1_epi8(static_cast<char>(needle));

    size_t i = 0;

    // SSE2 pass: scan 16 bytes at a time looking for the first fixed byte
    const size_t simdEnd = (scanEnd > 16) ? scanEnd - 16 : 0;
    while (i <= simdEnd) {
        // Load 16 bytes from scan position (offset by firstFixed)
        __m128i chunk = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(data + i + firstFixed));

        // Compare: result has 0xFF where bytes match
        __m128i cmp = _mm_cmpeq_epi8(chunk, needleVec);

        // Collapse to bitmask — bit N is set if byte N matched
        int mask = _mm_movemask_epi8(cmp);

        while (mask != 0) {
            // Find lowest set bit
            unsigned long bitIdx;
            _BitScanForward(&bitIdx, mask);

            size_t candidate = i + bitIdx;
            if (candidate > scanEnd) return 0;

            // Full pattern verification
            bool match = true;
            for (size_t j = 0; j < patLen; j++) {
                if (pattern.mask[j] && data[candidate + j] != pattern.bytes[j]) {
                    match = false;
                    break;
                }
            }

            if (match) return start + candidate;

            // Clear this bit and continue
            mask &= mask - 1;
        }

        i += 16;
    }

    // Linear fallback for remaining bytes
    for (; i <= scanEnd; i++) {
        if (data[i + firstFixed] != needle) continue;

        bool match = true;
        for (size_t j = 0; j < patLen; j++) {
            if (pattern.mask[j] && data[i + j] != pattern.bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) return start + i;
    }

    return 0;
}

uintptr_t ScannerEngine::ScanRegionLinear(uintptr_t start, size_t size,
                                           const ParsedPattern& pattern) const {
    if (pattern.bytes.empty() || size < pattern.bytes.size()) return 0;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(start);
    const size_t patLen = pattern.bytes.size();
    const size_t scanEnd = size - patLen;

    const uint8_t firstByte = pattern.bytes[0];
    const bool firstIsWild = !pattern.mask[0];

    for (size_t i = 0; i <= scanEnd; i++) {
        if (!firstIsWild && data[i] != firstByte) continue;

        bool found = true;
        for (size_t j = 1; j < patLen; j++) {
            if (pattern.mask[j] && data[i + j] != pattern.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) return start + i;
    }

    return 0;
}

std::vector<uintptr_t> ScannerEngine::ScanRegionAll(uintptr_t start, size_t size,
                                                      const ParsedPattern& pattern) const {
    std::vector<uintptr_t> results;
    if (pattern.bytes.empty() || size < pattern.bytes.size()) return results;

    uintptr_t current = start;
    size_t remaining = size;

    while (remaining > pattern.bytes.size()) {
        uintptr_t addr = ScanRegionSSE2(current, remaining, pattern);
        if (addr == 0) break;

        results.push_back(addr);

        size_t advance = (addr - current) + 1;
        current += advance;
        remaining -= advance;
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SINGLE-PATTERN API
// ═══════════════════════════════════════════════════════════════════════════

ScanResult ScannerEngine::Find(const char* pattern) const {
    return Find(pattern, 0);
}

ScanResult ScannerEngine::Find(const char* pattern, int offset) const {
    // Check cache
    std::string cacheKey = std::string(pattern) + "@" + std::to_string(offset);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end()) return it->second;
    }

    auto parsed = Parse(pattern);
    if (!parsed) {
        spdlog::error("ScannerEngine: Failed to parse pattern '{}'", pattern);
        return {};
    }

    parsed->offset = offset;
    ScanResult result = Find(*parsed);

    // Cache result
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[cacheKey] = result;
    }

    return result;
}

ScanResult ScannerEngine::Find(const ParsedPattern& pattern) const {
    auto regions = GetScanRegions();

    for (const auto& region : regions) {
        uintptr_t addr = ScanRegionSSE2(region.start, region.size, pattern);
        if (addr != 0) {
            ScanResult result;
            result.address = addr + pattern.offset;
            result.valid = true;
            result.section = region.section;
            result.confidence = 1.0f;
            return result;
        }
    }

    return {};
}

std::vector<uintptr_t> ScannerEngine::FindAll(const char* pattern) const {
    auto parsed = Parse(pattern);
    if (!parsed) return {};
    return FindAll(*parsed);
}

std::vector<uintptr_t> ScannerEngine::FindAll(const ParsedPattern& pattern) const {
    std::vector<uintptr_t> allResults;
    auto regions = GetScanRegions();

    for (const auto& region : regions) {
        auto results = ScanRegionAll(region.start, region.size, pattern);
        allResults.insert(allResults.end(), results.begin(), results.end());
    }

    return allResults;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SECTION-SPECIFIC SCANNING
// ═══════════════════════════════════════════════════════════════════════════

ScanResult ScannerEngine::FindInSection(const char* sectionName, const char* pattern) const {
    auto parsed = Parse(pattern);
    if (!parsed) return {};
    return FindInSection(sectionName, *parsed);
}

ScanResult ScannerEngine::FindInSection(const char* sectionName,
                                         const ParsedPattern& pattern) const {
    auto* sec = FindSection(sectionName);
    if (!sec) return {};

    uintptr_t addr = ScanRegionSSE2(sec->base, sec->size, pattern);
    if (addr == 0) return {};

    ScanResult result;
    result.address = addr + pattern.offset;
    result.valid = true;
    result.section = sec->name;
    result.confidence = 1.0f;
    return result;
}

std::vector<uintptr_t> ScannerEngine::FindAllInSection(const char* sectionName,
                                                         const char* pattern) const {
    auto parsed = Parse(pattern);
    if (!parsed) return {};

    auto* sec = FindSection(sectionName);
    if (!sec) return {};

    return ScanRegionAll(sec->base, sec->size, *parsed);
}

// ═══════════════════════════════════════════════════════════════════════════
//  COMPLEX PATTERN SCANNING
// ═══════════════════════════════════════════════════════════════════════════

ScanResult ScannerEngine::FindComplex(const ComplexPattern& pattern) const {
    if (pattern.components.empty()) return {};

    // Find the anchor (first component)
    const auto& anchor = pattern.components[0];
    ScanResult anchorResult = Find(anchor.pattern);
    if (!anchorResult) return {};

    uintptr_t anchorAddr = anchorResult.address;

    // Validate all additional components at their relative offsets
    for (size_t i = 1; i < pattern.components.size(); i++) {
        const auto& comp = pattern.components[i];
        uintptr_t expectedAddr = anchorAddr + comp.relativeOffset;

        const auto& pat = comp.pattern;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(expectedAddr);

        bool match = true;
        __try {
            for (size_t j = 0; j < pat.bytes.size(); j++) {
                if (pat.mask[j] && data[j] != pat.bytes[j]) {
                    match = false;
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            match = false;
        }

        if (!match) {
            if (comp.required) return {};
            // Non-required component failed — reduce confidence
        }
    }

    // Calculate result address
    uintptr_t resultAddr = anchorAddr + pattern.resultOffset;

    // Post-processing
    switch (pattern.postProcess) {
        case ComplexPattern::PostProcess::FollowCall:
            resultAddr = FollowCall(resultAddr);
            if (resultAddr == 0) return {};
            break;
        case ComplexPattern::PostProcess::FollowJmp:
            resultAddr = FollowJmp(resultAddr);
            if (resultAddr == 0) return {};
            break;
        case ComplexPattern::PostProcess::ResolveRIP:
            resultAddr = ResolveRIP(resultAddr, pattern.ripOperandOffset, pattern.ripInstructionLen);
            break;
        case ComplexPattern::PostProcess::FindPrologue: {
            // Walk backwards to function start
            __try {
                for (uintptr_t addr = resultAddr - 1; addr > resultAddr - 4096; addr--) {
                    uint8_t b = *reinterpret_cast<const uint8_t*>(addr);
                    if (b == 0xCC || b == 0xC3) {
                        uintptr_t candidate = addr + 1;
                        while (*reinterpret_cast<const uint8_t*>(candidate) == 0xCC)
                            candidate++;
                        resultAddr = candidate;
                        break;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return {};
            }
            break;
        }
        default:
            break;
    }

    ScanResult result;
    result.address = resultAddr;
    result.valid = true;
    result.section = anchorResult.section;
    result.confidence = 0.9f;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  BATCH SCANNING (single pass)
// ═══════════════════════════════════════════════════════════════════════════

void ScannerEngine::BatchScan(std::vector<BatchEntry>& entries) const {
    if (entries.empty()) return;

    auto regions = GetScanRegions();

    // Build a first-byte index for fast dispatch
    // Map: first non-wildcard byte → list of entry indices
    struct FirstByteEntry { size_t entryIdx; int fixedByteOffset; };
    std::unordered_map<uint8_t, std::vector<FirstByteEntry>> byteIndex;

    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].result.valid) continue; // Already resolved

        const auto& pat = entries[i].pattern;
        int firstFixed = -1;
        for (size_t j = 0; j < pat.bytes.size(); j++) {
            if (pat.mask[j]) { firstFixed = static_cast<int>(j); break; }
        }
        if (firstFixed >= 0) {
            byteIndex[pat.bytes[firstFixed]].push_back({i, firstFixed});
        }
    }

    // Count how many patterns remain unresolved
    auto unresolvedCount = [&]() {
        size_t count = 0;
        for (const auto& e : entries)
            if (!e.result.valid) count++;
        return count;
    };

    for (const auto& region : regions) {
        if (unresolvedCount() == 0) break;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(region.start);
        const size_t size = region.size;

        // Scan byte by byte, checking the first-byte index
        for (size_t pos = 0; pos < size; pos++) {
            uint8_t b = data[pos];

            auto it = byteIndex.find(b);
            if (it == byteIndex.end()) continue;

            // Check each candidate pattern that starts with this byte
            auto& candidates = it->second;
            for (auto& cand : candidates) {
                auto& entry = entries[cand.entryIdx];
                if (entry.result.valid) continue;

                const auto& pat = entry.pattern;
                size_t patStart = pos - cand.fixedByteOffset;

                // Bounds check
                if (patStart + pat.bytes.size() > size) continue;

                // Full pattern match
                bool match = true;
                for (size_t j = 0; j < pat.bytes.size(); j++) {
                    if (pat.mask[j] && data[patStart + j] != pat.bytes[j]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    entry.result.address = region.start + patStart + pat.offset;
                    entry.result.valid = true;
                    entry.result.section = region.section;
                    entry.result.confidence = 1.0f;
                    spdlog::info("BatchScan: '{}' found at 0x{:X}", entry.id, entry.result.address);
                }
            }
        }
    }

    size_t found = 0, missed = 0;
    for (const auto& e : entries) {
        if (e.result.valid) found++;
        else missed++;
    }
    spdlog::info("BatchScan: {}/{} patterns resolved ({} missed)",
                 found, entries.size(), missed);
}

// ═══════════════════════════════════════════════════════════════════════════
//  ADDRESS RESOLUTION UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

uintptr_t ScannerEngine::ResolveRIP(uintptr_t instrAddr, int operandOffset, int instrLen) {
    int32_t relative;
    std::memcpy(&relative, reinterpret_cast<void*>(instrAddr + operandOffset), sizeof(int32_t));
    return instrAddr + instrLen + relative;
}

uintptr_t ScannerEngine::FollowCall(uintptr_t callAddr) {
    uint8_t opcode = *reinterpret_cast<uint8_t*>(callAddr);
    if (opcode != 0xE8) return 0;
    return ResolveRIP(callAddr, 1, 5);
}

uintptr_t ScannerEngine::FollowJmp(uintptr_t jmpAddr) {
    uint8_t opcode = *reinterpret_cast<uint8_t*>(jmpAddr);
    if (opcode != 0xE9) return 0;
    return ResolveRIP(jmpAddr, 1, 5);
}

uintptr_t ScannerEngine::FollowConditionalJmp(uintptr_t jmpAddr) {
    uint8_t b0 = *reinterpret_cast<uint8_t*>(jmpAddr);
    uint8_t b1 = *reinterpret_cast<uint8_t*>(jmpAddr + 1);

    // Short conditional jumps: 7x rel8
    if ((b0 & 0xF0) == 0x70) {
        int8_t rel;
        std::memcpy(&rel, reinterpret_cast<void*>(jmpAddr + 1), 1);
        return jmpAddr + 2 + rel;
    }

    // Near conditional jumps: 0F 8x rel32
    if (b0 == 0x0F && (b1 & 0xF0) == 0x80) {
        return ResolveRIP(jmpAddr, 2, 6);
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CACHE
// ═══════════════════════════════════════════════════════════════════════════

void ScannerEngine::ClearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.clear();
}

size_t ScannerEngine::CacheSize() const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cache.size();
}

} // namespace kmp

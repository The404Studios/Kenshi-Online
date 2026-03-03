#include "kmp/string_analyzer.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <regex>

namespace kmp {

// =========================================================================
//  INITIALIZATION
// =========================================================================

bool StringAnalyzer::Init(uintptr_t moduleBase, size_t moduleSize,
                           const PDataEnumerator* pdata) {
    m_moduleBase = moduleBase;
    m_moduleSize = moduleSize;
    m_pdata = pdata;
    FindSections();
    return m_textBase != 0 && m_rdataBase != 0;
}

void StringAnalyzer::FindSections() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m_moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m_moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);
        if (std::strcmp(name, ".text") == 0) {
            m_textBase = m_moduleBase + section->VirtualAddress;
            m_textSize = section->Misc.VirtualSize;
        } else if (std::strcmp(name, ".rdata") == 0) {
            m_rdataBase = m_moduleBase + section->VirtualAddress;
            m_rdataSize = section->Misc.VirtualSize;
        }
    }
}

// =========================================================================
//  STRING DISCOVERY
// =========================================================================

bool StringAnalyzer::IsASCIIPrintable(const uint8_t* data, size_t len) const {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c < 0x20 || c > 0x7E) {
            // Allow tab, newline, carriage return
            if (c != '\t' && c != '\n' && c != '\r') return false;
        }
    }
    return true;
}

// ── ScanStrings SEH helper ──────────────────────────────────────────────
// POD struct for raw ASCII string hit (no destructors)
struct RawStringHit {
    size_t offsetInSection;  // offset from rdataBase
    size_t length;           // string length (excluding null terminator)
};

// Static C-style helper: scans .rdata for ASCII strings inside __try.
// Takes only raw pointers and POD types. Writes results to outHits array.
// Returns the number of hits found (capped at maxHits).
static size_t ScanStrings_SEH(const uint8_t* data, size_t size, int minLength,
                               RawStringHit* outHits, size_t maxHits) {
    size_t found = 0;
    __try {
        size_t i = 0;
        while (i < size) {
            // Find start of a string (printable ASCII)
            if (data[i] < 0x20 || data[i] > 0x7E) {
                i++;
                continue;
            }

            // Measure string length
            size_t start = i;
            while (i < size && data[i] >= 0x20 && data[i] <= 0x7E) {
                i++;
            }

            // Check for null terminator and minimum length
            size_t len = i - start;
            if (len >= static_cast<size_t>(minLength) && i < size && data[i] == 0x00) {
                if (found < maxHits) {
                    outHits[found].offsetInSection = start;
                    outHits[found].length = len;
                }
                found++;
            }

            i++; // Skip null terminator or non-printable
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during string scan - return what we have so far
    }
    return found;
}

size_t StringAnalyzer::ScanStrings(int minLength) {
    if (!m_rdataBase || !m_rdataSize) return 0;

    m_strings.clear();
    const uint8_t* data = reinterpret_cast<const uint8_t*>(m_rdataBase);
    size_t size = m_rdataSize;

    // First pass: count how many hits (use a generous pre-allocated buffer)
    // Estimate: at most size/minLength strings possible
    static constexpr size_t kMaxHits = 1024 * 1024; // 1M strings max
    size_t bufSize = (std::min)(size / (static_cast<size_t>(minLength) + 1) + 1, kMaxHits);
    auto* hits = new RawStringHit[bufSize];

    size_t found = ScanStrings_SEH(data, size, minLength, hits, bufSize);
    size_t usable = (std::min)(found, bufSize);

    // Convert raw hits into GameString objects (safe C++ code, no __try needed)
    m_strings.reserve(usable);
    for (size_t i = 0; i < usable; i++) {
        GameString gs;
        gs.address = m_rdataBase + hits[i].offsetInSection;
        gs.value.assign(reinterpret_cast<const char*>(data + hits[i].offsetInSection),
                        hits[i].length);
        gs.length = hits[i].length;
        gs.isWide = false;
        m_strings.push_back(std::move(gs));
    }

    delete[] hits;

    spdlog::info("StringAnalyzer: Found {} ASCII strings (min length {})", usable, minLength);
    return usable;
}

// ── ScanWideStrings SEH helper ──────────────────────────────────────────
// Static C-style helper: scans .rdata for wide (UTF-16) strings inside __try.
static size_t ScanWideStrings_SEH(const uint16_t* data, size_t count, int minLength,
                                   RawStringHit* outHits, size_t maxHits) {
    size_t found = 0;
    __try {
        size_t i = 0;
        while (i < count) {
            if (data[i] < 0x20 || data[i] > 0x7E) {
                i++;
                continue;
            }

            size_t start = i;
            while (i < count && data[i] >= 0x20 && data[i] <= 0x7E) {
                i++;
            }

            size_t len = i - start;
            if (len >= static_cast<size_t>(minLength) && i < count && data[i] == 0x0000) {
                if (found < maxHits) {
                    outHits[found].offsetInSection = start; // offset in uint16_t units
                    outHits[found].length = len;            // length in characters
                }
                found++;
            }

            i++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during wide string scan - return what we have so far
    }
    return found;
}

size_t StringAnalyzer::ScanWideStrings(int minLength) {
    if (!m_rdataBase || !m_rdataSize) return 0;

    const uint16_t* data = reinterpret_cast<const uint16_t*>(m_rdataBase);
    size_t count = m_rdataSize / 2;

    // Allocate buffer for raw hits
    static constexpr size_t kMaxHits = 1024 * 1024;
    size_t bufSize = (std::min)(count / (static_cast<size_t>(minLength) + 1) + 1, kMaxHits);
    auto* hits = new RawStringHit[bufSize];

    size_t found = ScanWideStrings_SEH(data, count, minLength, hits, bufSize);
    size_t usable = (std::min)(found, bufSize);

    // Convert raw hits into GameString objects
    for (size_t i = 0; i < usable; i++) {
        GameString gs;
        gs.address = m_rdataBase + hits[i].offsetInSection * 2;
        gs.length = hits[i].length;
        gs.isWide = true;
        // Convert to narrow for storage
        gs.value.reserve(hits[i].length);
        for (size_t j = hits[i].offsetInSection; j < hits[i].offsetInSection + hits[i].length; j++) {
            gs.value += static_cast<char>(data[j] & 0xFF);
        }
        m_strings.push_back(std::move(gs));
    }

    delete[] hits;

    spdlog::info("StringAnalyzer: Found {} wide strings (min length {})", usable, minLength);
    return usable;
}

// =========================================================================
//  CROSS-REFERENCE RESOLUTION
// =========================================================================

// ── ResolveXrefs SEH helper ─────────────────────────────────────────────
// POD struct for a raw LEA xref hit (no destructors)
struct RawLeaHit {
    size_t   instrOffset;  // offset from textBase
    uintptr_t targetAddr;  // resolved target address
};

// Static C-style helper: scans .text for RIP-relative LEA instructions inside __try.
// Returns the number of LEA hits found (capped at maxHits).
static size_t ResolveXrefs_SEH(const uint8_t* text, size_t textSize,
                                uintptr_t textBase,
                                RawLeaHit* outHits, size_t maxHits) {
    size_t found = 0;
    __try {
        for (size_t i = 0; i + 7 < textSize; i++) {
            // LEA reg, [RIP+disp32]
            // REX.W LEA: 48 8D xx (mod=0, rm=5)
            // REX.WR LEA: 4C 8D xx (mod=0, rm=5)
            bool isLEA = false;
            if ((text[i] == 0x48 || text[i] == 0x4C) && text[i + 1] == 0x8D) {
                uint8_t modrm = text[i + 2];
                if ((modrm & 0xC7) == 0x05) { // mod=0, rm=5 (RIP-relative)
                    isLEA = true;
                }
            }

            if (!isLEA) continue;

            // Decode RIP-relative displacement
            int32_t disp;
            std::memcpy(&disp, &text[i + 3], 4);
            uintptr_t instrAddr = textBase + i;
            uintptr_t target = instrAddr + 7 + disp;

            if (found < maxHits) {
                outHits[found].instrOffset = i;
                outHits[found].targetAddr = target;
            }
            found++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during xref resolution - return what we have so far
    }
    return found;
}

size_t StringAnalyzer::ResolveXrefs() {
    if (m_strings.empty() || !m_textBase || !m_textSize) return 0;

    m_totalXrefs = 0;
    m_funcStringIndex.clear();

    // Build a hash set of all string addresses for O(1) lookup
    std::unordered_map<uintptr_t, size_t> stringAddrIndex;
    for (size_t i = 0; i < m_strings.size(); i++) {
        stringAddrIndex[m_strings[i].address] = i;
    }

    // Scan .text for ALL RIP-relative LEA instructions (inside __try via helper)
    const uint8_t* text = reinterpret_cast<const uint8_t*>(m_textBase);

    // Estimate: LEA instructions are ~1 per 16 bytes in code on average
    static constexpr size_t kMaxLeaHits = 4 * 1024 * 1024; // 4M LEAs max
    size_t bufSize = (std::min)(m_textSize / 7 + 1, kMaxLeaHits);
    auto* leaHits = new RawLeaHit[bufSize];

    size_t leaCount = ResolveXrefs_SEH(text, m_textSize, m_textBase, leaHits, bufSize);
    size_t usable = (std::min)(leaCount, bufSize);

    // Now match LEA targets against known strings (safe C++ code, no __try needed)
    for (size_t h = 0; h < usable; h++) {
        uintptr_t target = leaHits[h].targetAddr;
        uintptr_t instrAddr = m_textBase + leaHits[h].instrOffset;

        // Check if target points to a known string
        auto it = stringAddrIndex.find(target);
        if (it == stringAddrIndex.end()) continue;

        // Found a cross-reference!
        size_t strIdx = it->second;
        auto& gs = m_strings[strIdx];

        GameString::XRef xref;
        xref.codeAddress = instrAddr;

        // Resolve to containing function via .pdata
        if (m_pdata) {
            auto* func = m_pdata->FindContaining(instrAddr);
            if (func) {
                xref.funcAddress = func->startVA;
                xref.funcLabel = func->label;

                // Update function->string index
                m_funcStringIndex[func->startVA].push_back(strIdx);
            }
        }

        gs.xrefs.push_back(xref);
        m_totalXrefs++;
    }

    delete[] leaHits;

    spdlog::info("StringAnalyzer: Resolved {} total xrefs across {} strings",
                 m_totalXrefs, m_strings.size());
    return m_totalXrefs;
}

// ── FindXrefs SEH helper ────────────────────────────────────────────────
// Static C-style helper: scans .text for LEA instructions targeting a specific address.
// Returns the number of hits found (capped at maxHits).
static size_t FindXrefs_SEH(const uint8_t* text, size_t textSize,
                             uintptr_t textBase, uintptr_t stringAddr,
                             uintptr_t* outCodeAddrs, size_t maxHits) {
    size_t found = 0;
    __try {
        for (size_t i = 0; i + 7 < textSize; i++) {
            if ((text[i] == 0x48 || text[i] == 0x4C) && text[i + 1] == 0x8D) {
                uint8_t modrm = text[i + 2];
                if ((modrm & 0xC7) != 0x05) continue;

                int32_t disp;
                std::memcpy(&disp, &text[i + 3], 4);
                uintptr_t instrAddr = textBase + i;
                uintptr_t target = instrAddr + 7 + disp;

                if (target == stringAddr) {
                    if (found < maxHits) {
                        outCodeAddrs[found] = instrAddr;
                    }
                    found++;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during xref search - return what we have so far
    }
    return found;
}

std::vector<GameString::XRef> StringAnalyzer::FindXrefs(uintptr_t stringAddr) const {
    std::vector<GameString::XRef> xrefs;
    if (!m_textBase || !m_textSize) return xrefs;

    const uint8_t* text = reinterpret_cast<const uint8_t*>(m_textBase);

    // A single string rarely has more than a few hundred xrefs
    static constexpr size_t kMaxHits = 4096;
    uintptr_t codeAddrs[kMaxHits];

    size_t found = FindXrefs_SEH(text, m_textSize, m_textBase, stringAddr,
                                  codeAddrs, kMaxHits);
    size_t usable = (std::min)(found, kMaxHits);

    // Convert raw code addresses into XRef objects (safe C++ code, no __try needed)
    xrefs.reserve(usable);
    for (size_t i = 0; i < usable; i++) {
        GameString::XRef xref;
        xref.codeAddress = codeAddrs[i];
        if (m_pdata) {
            auto* func = m_pdata->FindContaining(codeAddrs[i]);
            if (func) {
                xref.funcAddress = func->startVA;
                xref.funcLabel = func->label;
            }
        }
        xrefs.push_back(xref);
    }

    return xrefs;
}

// =========================================================================
//  STRING CLASSIFICATION
// =========================================================================

StringCategory StringAnalyzer::ClassifyString(const std::string& str) const {
    if (str.empty()) return StringCategory::Unknown;

    // Debug log: "[ClassName::method]" or "ClassName::method"
    if (str.find("::") != std::string::npos) {
        if (str[0] == '[') return StringCategory::DebugLog;
        return StringCategory::FunctionName;
    }

    // Error messages
    if (str.find("Error") != std::string::npos ||
        str.find("error") != std::string::npos ||
        str.find("Failed") != std::string::npos ||
        str.find("failed") != std::string::npos ||
        str.find("Cannot") != std::string::npos ||
        str.find("Invalid") != std::string::npos) {
        return StringCategory::ErrorMessage;
    }

    // Format strings
    if (str.find("%d") != std::string::npos ||
        str.find("%s") != std::string::npos ||
        str.find("%f") != std::string::npos ||
        str.find("%x") != std::string::npos ||
        str.find("{0}") != std::string::npos ||
        str.find("{1}") != std::string::npos) {
        return StringCategory::FormatString;
    }

    // File paths
    if ((str.find('/') != std::string::npos || str.find('\\') != std::string::npos) &&
        (str.find('.') != std::string::npos)) {
        return StringCategory::FilePath;
    }

    // UI strings (MyGUI layout names, widget names)
    if (str.find("Widget") != std::string::npos ||
        str.find("Layout") != std::string::npos ||
        str.find("Panel") != std::string::npos ||
        str.find("Button") != std::string::npos ||
        str.find("_") != std::string::npos) {
        return StringCategory::UIString;
    }

    // Config keys (camelCase or snake_case, no spaces)
    if (str.find(' ') == std::string::npos && str.length() > 3 &&
        str.length() < 64) {
        bool hasLower = false, hasUpper = false;
        for (char c : str) {
            if (c >= 'a' && c <= 'z') hasLower = true;
            if (c >= 'A' && c <= 'Z') hasUpper = true;
        }
        if (hasLower && hasUpper) return StringCategory::ConfigKey;
    }

    return StringCategory::GameData;
}

std::pair<std::string, std::string> StringAnalyzer::ExtractClassMethod(
    const std::string& str) const {
    // Pattern: "[ClassName::methodName] ..."
    // or: "ClassName::methodName ..."
    size_t colonPos = str.find("::");
    if (colonPos == std::string::npos) return {"", ""};

    // Find class name start
    size_t classStart = colonPos;
    while (classStart > 0 && (std::isalnum(str[classStart - 1]) || str[classStart - 1] == '_')) {
        classStart--;
    }
    // Skip leading '[' if present
    if (classStart > 0 && str[classStart - 1] == '[') {
        // classStart is correct (after '[')
    }

    std::string className = str.substr(classStart, colonPos - classStart);

    // Find method name end
    size_t methodStart = colonPos + 2;
    size_t methodEnd = methodStart;
    while (methodEnd < str.length() &&
           (std::isalnum(str[methodEnd]) || str[methodEnd] == '_')) {
        methodEnd++;
    }

    std::string methodName = str.substr(methodStart, methodEnd - methodStart);

    return {className, methodName};
}

// =========================================================================
//  FUNCTION LABELING
// =========================================================================

std::string StringAnalyzer::GenerateLabel(const std::vector<std::string>& strings) const {
    if (strings.empty()) return "";

    // Priority 1: "[ClassName::method]" debug logs
    for (const auto& s : strings) {
        auto [cls, method] = ExtractClassMethod(s);
        if (!cls.empty() && !method.empty()) {
            return cls + "::" + method;
        }
    }

    // Priority 2: Unique-looking identifier strings
    for (const auto& s : strings) {
        if (s.length() > 3 && s.length() < 80 &&
            ClassifyString(s) != StringCategory::Unknown) {
            // Use first meaningful string as label
            std::string label = s;
            // Truncate at 60 chars
            if (label.length() > 60) label = label.substr(0, 57) + "...";
            return label;
        }
    }

    // Priority 3: First string truncated
    std::string label = strings[0];
    if (label.length() > 60) label = label.substr(0, 57) + "...";
    return label;
}

size_t StringAnalyzer::LabelFunctions() {
    m_labeledFunctions.clear();

    if (m_funcStringIndex.empty()) return 0;

    for (const auto& [funcAddr, strIndices] : m_funcStringIndex) {
        // Collect all strings for this function
        std::vector<std::string> funcStrings;
        for (size_t idx : strIndices) {
            if (idx < m_strings.size()) {
                funcStrings.push_back(m_strings[idx].value);
            }
        }

        if (funcStrings.empty()) continue;

        LabeledFunction lf;
        lf.address = funcAddr;
        lf.strings = funcStrings;
        lf.label = GenerateLabel(funcStrings);

        // Extract class/method if possible
        for (const auto& s : funcStrings) {
            auto [cls, method] = ExtractClassMethod(s);
            if (!cls.empty()) {
                lf.className = cls;
                lf.methodName = method;
                lf.confidence = 0.9f;
                break;
            }
        }

        // Classify
        if (!lf.className.empty()) {
            lf.category = StringCategory::DebugLog;
            lf.confidence = 0.9f;
        } else {
            lf.category = ClassifyString(lf.label);
            lf.confidence = 0.5f;
        }

        m_labeledFunctions.push_back(std::move(lf));
    }

    // Sort by address
    std::sort(m_labeledFunctions.begin(), m_labeledFunctions.end(),
              [](const LabeledFunction& a, const LabeledFunction& b) {
                  return a.address < b.address;
              });

    spdlog::info("StringAnalyzer: Labeled {} functions", m_labeledFunctions.size());
    return m_labeledFunctions.size();
}

// =========================================================================
//  QUERY API
// =========================================================================

std::vector<const GameString*> StringAnalyzer::FindStrings(const std::string& substring) const {
    std::vector<const GameString*> results;
    for (const auto& gs : m_strings) {
        if (gs.value.find(substring) != std::string::npos) {
            results.push_back(&gs);
        }
    }
    return results;
}

std::vector<const GameString*> StringAnalyzer::FindByCategory(StringCategory category) const {
    std::vector<const GameString*> results;
    for (const auto& gs : m_strings) {
        if (ClassifyString(gs.value) == category) {
            results.push_back(&gs);
        }
    }
    return results;
}

const LabeledFunction* StringAnalyzer::FindLabeledFunction(const std::string& name) const {
    // Exact match first
    for (const auto& lf : m_labeledFunctions) {
        if (lf.label == name) return &lf;
    }
    // Substring match
    for (const auto& lf : m_labeledFunctions) {
        if (lf.label.find(name) != std::string::npos) return &lf;
    }
    // Class::method match
    for (const auto& lf : m_labeledFunctions) {
        if (!lf.className.empty() && lf.className.find(name) != std::string::npos) return &lf;
        if (!lf.methodName.empty() && lf.methodName.find(name) != std::string::npos) return &lf;
    }
    return nullptr;
}

std::vector<uintptr_t> StringAnalyzer::FindFunctionsReferencingString(
    const std::string& str) const {
    std::unordered_set<uintptr_t> funcSet;
    for (const auto& gs : m_strings) {
        if (gs.value.find(str) == std::string::npos) continue;
        for (const auto& xref : gs.xrefs) {
            if (xref.funcAddress) funcSet.insert(xref.funcAddress);
        }
    }
    return std::vector<uintptr_t>(funcSet.begin(), funcSet.end());
}

std::vector<std::string> StringAnalyzer::GetFunctionStrings(uintptr_t funcAddr) const {
    std::vector<std::string> result;
    auto it = m_funcStringIndex.find(funcAddr);
    if (it != m_funcStringIndex.end()) {
        for (size_t idx : it->second) {
            if (idx < m_strings.size()) {
                result.push_back(m_strings[idx].value);
            }
        }
    }
    return result;
}

void StringAnalyzer::ForEachString(
    const std::function<void(const GameString&)>& callback) const {
    for (const auto& gs : m_strings) callback(gs);
}

void StringAnalyzer::ForEachLabeledFunction(
    const std::function<void(const LabeledFunction&)>& callback) const {
    for (const auto& lf : m_labeledFunctions) callback(lf);
}

} // namespace kmp

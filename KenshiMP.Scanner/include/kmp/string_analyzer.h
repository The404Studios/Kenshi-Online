#pragma once
// Comprehensive String Cross-Reference Analyzer
//
// Scans ALL strings in .rdata, finds ALL code xrefs for each string,
// and resolves to function boundaries via .pdata. This auto-labels
// thousands of functions from debug strings, class names, error messages,
// and format strings embedded in the executable.

#include "kmp/pdata_enumerator.h"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace kmp {

// A string found in the binary
struct GameString {
    uintptr_t   address    = 0;     // Address in memory
    std::string value;               // The string content
    size_t      length     = 0;     // String length (not including null)
    bool        isWide     = false; // UTF-16 string

    // Cross-references from code
    struct XRef {
        uintptr_t   codeAddress = 0;  // Address of the LEA instruction
        uintptr_t   funcAddress = 0;  // Function containing this xref (via .pdata)
        std::string funcLabel;         // Label of the containing function
    };
    std::vector<XRef> xrefs;
};

// Classification of a string for categorization
enum class StringCategory {
    Unknown,
    DebugLog,       // "[ClassName::method] message"
    ErrorMessage,   // "Error: ...", "Failed to ..."
    ClassName,      // C++ class names from RTTI or debug strings
    FunctionName,   // "functionName" in debug logs
    FormatString,   // Contains %d, %s, %f, etc.
    FilePath,       // Contains / or \ and file extensions
    GameData,       // Item names, stat names, etc.
    UIString,       // UI element names, layout references
    ConfigKey,      // Configuration key names
};

// A labeled function discovered via string analysis
struct LabeledFunction {
    uintptr_t       address     = 0;
    std::string     label;
    std::string     className;      // Extracted class name if applicable
    std::string     methodName;     // Extracted method name if applicable
    StringCategory  category    = StringCategory::Unknown;
    float           confidence  = 0.0f;
    std::vector<std::string> strings; // All strings referenced by this function
};

class StringAnalyzer {
public:
    // Initialize with module info and .pdata function table
    bool Init(uintptr_t moduleBase, size_t moduleSize,
              const PDataEnumerator* pdata = nullptr);

    // ── String Discovery ──

    // Scan .rdata for all ASCII strings (min length 4)
    size_t ScanStrings(int minLength = 4);

    // Scan for wide (UTF-16) strings
    size_t ScanWideStrings(int minLength = 4);

    // ── Cross-Reference Analysis ──

    // Find ALL code xrefs for all discovered strings
    // This is the main heavy-lifting function.
    size_t ResolveXrefs();

    // Find xrefs for a single string at a known address
    std::vector<GameString::XRef> FindXrefs(uintptr_t stringAddr) const;

    // ── Function Labeling ──

    // Label functions based on their string references.
    // Uses heuristics to extract class/method names from debug strings.
    size_t LabelFunctions();

    // ── Query API ──

    // Find strings matching a pattern (substring search)
    std::vector<const GameString*> FindStrings(const std::string& substring) const;

    // Find strings by category
    std::vector<const GameString*> FindByCategory(StringCategory category) const;

    // Get all labeled functions
    const std::vector<LabeledFunction>& GetLabeledFunctions() const { return m_labeledFunctions; }

    // Find a labeled function by name (partial match)
    const LabeledFunction* FindLabeledFunction(const std::string& name) const;

    // Find all functions referencing a specific string
    std::vector<uintptr_t> FindFunctionsReferencingString(const std::string& str) const;

    // Get function containing address, returning its strings
    std::vector<std::string> GetFunctionStrings(uintptr_t funcAddr) const;

    // ── Statistics ──
    size_t GetStringCount() const { return m_strings.size(); }
    size_t GetXrefCount() const { return m_totalXrefs; }
    size_t GetLabeledFunctionCount() const { return m_labeledFunctions.size(); }

    // ── Iteration ──
    void ForEachString(const std::function<void(const GameString&)>& callback) const;
    void ForEachLabeledFunction(const std::function<void(const LabeledFunction&)>& callback) const;

    // ── Raw access for orchestrator ──
    const std::vector<GameString>& GetAllStrings() const { return m_strings; }

private:
    uintptr_t   m_moduleBase = 0;
    size_t      m_moduleSize = 0;
    uintptr_t   m_textBase   = 0;
    size_t      m_textSize   = 0;
    uintptr_t   m_rdataBase  = 0;
    size_t      m_rdataSize  = 0;

    const PDataEnumerator* m_pdata = nullptr;

    std::vector<GameString>       m_strings;
    std::vector<LabeledFunction>  m_labeledFunctions;
    size_t                        m_totalXrefs = 0;

    // Index: function address → list of string indices
    std::unordered_map<uintptr_t, std::vector<size_t>> m_funcStringIndex;

    // Internal helpers
    void FindSections();
    bool IsASCIIPrintable(const uint8_t* data, size_t len) const;
    StringCategory ClassifyString(const std::string& str) const;
    std::pair<std::string, std::string> ExtractClassMethod(const std::string& str) const;
    std::string GenerateLabel(const std::vector<std::string>& strings) const;
};

} // namespace kmp

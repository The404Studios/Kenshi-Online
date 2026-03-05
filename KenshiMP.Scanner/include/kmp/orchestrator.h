#pragma once
// Pattern Orchestrator — Central Intelligence for Game Reverse Engineering
//
// Manages all discovery phases in a pipeline:
//   Phase 1: PE analysis (.pdata enumeration, section mapping)
//   Phase 2: String discovery and cross-reference analysis
//   Phase 3: VTable scanning and RTTI class hierarchy
//   Phase 4: Pattern-based function resolution (SIMD batch scan)
//   Phase 5: String xref fallback for unresolved patterns
//   Phase 6: Call graph analysis and label propagation
//   Phase 7: Global pointer discovery
//
// Features:
//   - Data-driven pattern registry (add/remove patterns at runtime)
//   - Multi-method resolution with confidence tracking
//   - Per-pattern retry and deferred resolution
//   - Comprehensive reporting and statistics
//   - Individual section/component queries

#include "kmp/scanner_engine.h"
#include "kmp/pdata_enumerator.h"
#include "kmp/string_analyzer.h"
#include "kmp/vtable_scanner.h"
#include "kmp/call_graph.h"
#include "kmp/patterns.h"

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  PATTERN ENTRY — A single function/pointer to discover
// ═══════════════════════════════════════════════════════════════════════════

enum class ResolutionMethod {
    None,
    PatternScan,        // IDA-style byte pattern match
    StringXref,         // String cross-reference to function start
    VTableSlot,         // Virtual function from RTTI vtable
    CallGraphTrace,     // Found via call graph from a known function
    HardcodedOffset,    // Hardcoded RVA offset (version-specific)
    PDataLookup,        // Direct .pdata lookup by RVA
    ComplexPattern,     // Multi-component composed pattern
    Manual,             // Manually set by user code
};

const char* ResolutionMethodName(ResolutionMethod method);

struct PatternEntry {
    // ── Identity ──
    std::string id;                 // Unique identifier (e.g., "CharacterSpawn")
    std::string category;           // Category (e.g., "entity", "combat", "ai")
    std::string description;        // Human-readable description

    // ── Resolution Config ──
    const char* pattern         = nullptr;  // IDA byte pattern (can be nullptr)
    const char* stringAnchor    = nullptr;  // Fallback string to search for
    int         stringAnchorLen = 0;
    uint32_t    hardcodedRVA    = 0;        // Version-specific RVA (0 = unknown)
    std::string vtableClass;                // VTable class name (for vtable resolution)
    int         vtableSlot      = -1;       // VTable slot index

    // ── Complex pattern support ──
    ComplexPattern complexPattern;

    // ── Resolution State ──
    uintptr_t       resolvedAddress = 0;
    ResolutionMethod resolvedMethod  = ResolutionMethod::None;
    float           confidence      = 0.0f;
    bool            isResolved      = false;
    bool            isGlobalPointer = false; // True if this is a global pointer, not a function
    bool            critical        = false; // Must-resolve: enables aggressive fallbacks
    int             retryCount      = 0;

    // ── Target pointer (for GameFunctions integration) ──
    void**          targetPtr       = nullptr;    // If set, writes resolved address here
    uintptr_t*      targetUintptr   = nullptr;    // For global pointers

    bool NeedsResolution() const { return !isResolved; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  ORCHESTRATOR CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

struct OrchestratorConfig {
    bool enablePData        = true;
    bool enableStrings      = true;
    bool enableVTables      = true;
    bool enableCallGraph    = true;
    bool enableBatchScan    = true;
    bool enableLabelPropagation = true;

    int  stringMinLength    = 4;
    int  callGraphDepth     = 2;    // Label propagation depth
    int  maxRetries         = 3;

    // Performance tuning
    bool scanWideStrings    = false; // UTF-16 strings (slower)
    bool fullCallGraph      = false; // Full graph vs targeted graph
};

// ═══════════════════════════════════════════════════════════════════════════
//  ORCHESTRATOR REPORT
// ═══════════════════════════════════════════════════════════════════════════

struct OrchestratorReport {
    // Timing
    struct PhaseTimingMs {
        double pdata        = 0;
        double strings      = 0;
        double vtables      = 0;
        double patternScan  = 0;
        double stringFallback = 0;
        double callGraph    = 0;
        double globalPtrs   = 0;
        double total        = 0;
    } timing;

    // Resolution counts
    int totalEntries        = 0;
    int resolvedByPattern   = 0;
    int resolvedByString    = 0;
    int resolvedByVTable    = 0;
    int resolvedByCallGraph = 0;
    int resolvedByHardcoded = 0;
    int resolvedByComplex   = 0;
    int totalResolved       = 0;
    int totalFailed         = 0;

    // Discovery stats
    size_t pdataFunctions   = 0;
    size_t stringsFound     = 0;
    size_t xrefsResolved    = 0;
    size_t vtablesFound     = 0;
    size_t callGraphNodes   = 0;
    size_t callGraphEdges   = 0;
    size_t labeledFunctions = 0;

    // Failed entries
    std::vector<std::string> failedEntries;
};

// ═══════════════════════════════════════════════════════════════════════════
//  THE ORCHESTRATOR
// ═══════════════════════════════════════════════════════════════════════════

class PatternOrchestrator {
public:
    PatternOrchestrator() = default;

    // ── Initialization ──
    bool Init(const char* moduleName = nullptr, const OrchestratorConfig& config = {});

    // ── Pattern Registry ──

    // Register a pattern entry for discovery
    void Register(PatternEntry entry);

    // Register all built-in Kenshi patterns (from patterns.h)
    void RegisterBuiltinPatterns(GameFunctions& funcs);

    // Remove a pattern entry
    void Unregister(const std::string& id);

    // Get a registered entry
    const PatternEntry* GetEntry(const std::string& id) const;
    PatternEntry* GetMutableEntry(const std::string& id);

    // Get all entries
    const std::vector<PatternEntry>& GetEntries() const { return m_entries; }

    // Get entries by category
    std::vector<const PatternEntry*> GetByCategory(const std::string& category) const;

    // ── Execution ──

    // Run the full discovery pipeline
    OrchestratorReport Run();

    // Run individual phases (for incremental/retry)
    void RunPhase1_PData();
    void RunPhase2_Strings();
    void RunPhase3_VTables();
    void RunPhase4_PatternScan();
    void RunPhase5_StringFallback();
    void RunPhase6_CallGraph();
    void RunPhase7_GlobalPointers();
    void RunPhase8_EmergencyCritical();

    // Retry resolution for failed entries
    int RetryFailed();

    // Retry a specific entry
    bool RetryEntry(const std::string& id);

    // ── Query API ──

    // Resolve a single address by ID
    uintptr_t GetAddress(const std::string& id) const;

    // Check if resolved
    bool IsResolved(const std::string& id) const;

    // Get resolution method used
    ResolutionMethod GetMethod(const std::string& id) const;

    // Get confidence
    float GetConfidence(const std::string& id) const;

    // Count resolved
    int CountResolved() const;
    int CountTotal() const { return static_cast<int>(m_entries.size()); }

    // ── Component Access ──
    const ScannerEngine&     GetScanner()       const { return m_scanner; }
    const PDataEnumerator&   GetPData()         const { return m_pdata; }
    const StringAnalyzer&    GetStringAnalyzer() const { return m_strings; }
    const VTableScanner&     GetVTableScanner()  const { return m_vtables; }
    const CallGraphAnalyzer& GetCallGraph()      const { return m_callGraph; }

    // Non-const access for advanced usage
    ScannerEngine&     GetMutableScanner()  { return m_scanner; }
    PDataEnumerator&   GetMutablePData()    { return m_pdata; }
    StringAnalyzer&    GetMutableStrings()  { return m_strings; }
    VTableScanner&     GetMutableVTables()  { return m_vtables; }
    CallGraphAnalyzer& GetMutableCallGraph() { return m_callGraph; }

    // ── Reporting ──
    OrchestratorReport GenerateReport() const;
    void LogReport(const OrchestratorReport& report) const;

    // ── Discovered Functions (beyond registered patterns) ──
    // All functions discovered via string analysis
    const std::vector<LabeledFunction>& GetDiscoveredFunctions() const;

    // Find any function by name across all analyzers
    uintptr_t FindFunction(const std::string& name) const;

    // Get all strings referencing a function
    std::vector<std::string> GetFunctionStrings(uintptr_t addr) const;

private:
    OrchestratorConfig  m_config;
    ScannerEngine       m_scanner;
    PDataEnumerator     m_pdata;
    StringAnalyzer      m_strings;
    VTableScanner       m_vtables;
    CallGraphAnalyzer   m_callGraph;

    std::vector<PatternEntry> m_entries;
    std::unordered_map<std::string, size_t> m_entryIndex; // id → index

    OrchestratorReport m_lastReport;
    bool m_initialized = false;

    // Timing helper
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    double ElapsedMs(TimePoint start) const;

    // Internal resolution methods
    bool TryPatternScan(PatternEntry& entry);
    bool TryStringXref(PatternEntry& entry);
    bool TryVTableSlot(PatternEntry& entry);
    bool TryHardcodedOffset(PatternEntry& entry);
    bool TryComplexPattern(PatternEntry& entry);
    bool TryCallGraphTrace(PatternEntry& entry);
    bool TryDirectStringSearch(PatternEntry& entry);
    bool TryPrologueValidatedRVA(PatternEntry& entry);
    void ResolveEntry(PatternEntry& entry, uintptr_t address,
                      ResolutionMethod method, float confidence);

    // Walk backwards from a code address to find the function start
    uintptr_t WalkBackToPrologue(uintptr_t codeAddr, int maxDistance = 4096) const;
};

} // namespace kmp

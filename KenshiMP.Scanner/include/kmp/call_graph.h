#pragma once
// Call Graph Analyzer
//
// For each known function, scans its body for CALL (E8) and JMP (E9)
// instructions to build a directed call graph. Propagates labels from
// known functions to callees and callers.

#include "kmp/pdata_enumerator.h"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace kmp {

// An edge in the call graph
struct CallEdge {
    uintptr_t   callerAddr  = 0;   // Function making the call
    uintptr_t   calleeAddr  = 0;   // Function being called
    uintptr_t   callSite    = 0;   // Address of the CALL/JMP instruction
    bool        isDirectCall = true; // E8 call vs indirect call
    bool        isJmp       = false; // E9 jmp (tail call)
};

// A node in the call graph
struct CallNode {
    uintptr_t                   address  = 0;
    std::string                 label;
    std::vector<uintptr_t>      callers;   // Functions that call this one
    std::vector<uintptr_t>      callees;   // Functions this one calls
    std::vector<CallEdge>       outEdges;  // Detailed outgoing edges
    size_t                      callCount = 0; // How many times this is called
};

class CallGraphAnalyzer {
public:
    // Initialize with module info and .pdata
    bool Init(uintptr_t moduleBase, size_t moduleSize,
              const PDataEnumerator* pdata);

    // ── Analysis ──

    // Build call graph for all functions in .pdata
    // This scans every function body for CALL/JMP instructions
    size_t BuildFullGraph();

    // Build call graph for a subset of functions
    size_t BuildGraphFor(const std::vector<uintptr_t>& functionAddrs);

    // Analyze a single function's calls
    std::vector<CallEdge> AnalyzeFunction(uintptr_t funcAddr, size_t funcSize) const;

    // ── Label Propagation ──

    // Propagate labels from labeled functions to their callees/callers
    // depth = how many hops to propagate
    size_t PropagateLabels(
        const std::unordered_map<uintptr_t, std::string>& knownLabels,
        int depth = 2);

    // ── Query API ──

    // Get node for a function
    const CallNode* GetNode(uintptr_t funcAddr) const;

    // Get all callers of a function
    std::vector<uintptr_t> GetCallers(uintptr_t funcAddr) const;

    // Get all callees of a function
    std::vector<uintptr_t> GetCallees(uintptr_t funcAddr) const;

    // Find most-called functions (hot functions)
    std::vector<std::pair<uintptr_t, size_t>> GetMostCalled(size_t topN = 50) const;

    // Find leaf functions (call nothing)
    std::vector<uintptr_t> GetLeafFunctions() const;

    // Find root functions (called by nothing in our graph)
    std::vector<uintptr_t> GetRootFunctions() const;

    // Trace call path from source to target (BFS)
    std::vector<uintptr_t> FindCallPath(uintptr_t source, uintptr_t target,
                                         int maxDepth = 10) const;

    // Get functions within N calls of a target
    std::unordered_set<uintptr_t> GetNeighborhood(uintptr_t funcAddr,
                                                    int radius = 2) const;

    // ── Statistics ──
    size_t GetNodeCount() const { return m_nodes.size(); }
    size_t GetEdgeCount() const { return m_totalEdges; }

    // ── Iteration ──
    void ForEachNode(const std::function<void(const CallNode&)>& callback) const;

private:
    uintptr_t m_moduleBase = 0;
    size_t    m_moduleSize = 0;
    uintptr_t m_textBase   = 0;
    size_t    m_textSize   = 0;

    const PDataEnumerator* m_pdata = nullptr;

    std::unordered_map<uintptr_t, CallNode> m_nodes;
    size_t m_totalEdges = 0;

    void FindTextSection();
    bool IsInText(uintptr_t addr) const;
    CallNode& GetOrCreateNode(uintptr_t addr);
};

} // namespace kmp

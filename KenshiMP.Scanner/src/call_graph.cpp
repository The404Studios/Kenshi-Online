#include "kmp/call_graph.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <queue>
#include <cstring>

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  SEH HELPER — No C++ objects with destructors allowed
// ═══════════════════════════════════════════════════════════════════════════

struct RawCallEdge {
    uintptr_t callerAddr;
    uintptr_t calleeAddr;
    uintptr_t callSite;
    bool      isJmp;
};

// Scans a function body for CALL/JMP instructions and writes results
// into a plain-C array. Returns the number of edges found.
static size_t SEH_AnalyzeFunction(uintptr_t funcAddr, size_t funcSize,
                                   uintptr_t textBase, size_t textSize,
                                   RawCallEdge* outEdges, size_t maxEdges) {
    size_t count = 0;

    __try {
        const uint8_t* code = reinterpret_cast<const uint8_t*>(funcAddr);

        for (size_t i = 0; i + 5 <= funcSize && count < maxEdges; i++) {
            uintptr_t instrAddr = funcAddr + i;

            // E8 xx xx xx xx — CALL rel32
            if (code[i] == 0xE8) {
                int32_t rel;
                std::memcpy(&rel, &code[i + 1], 4);
                uintptr_t target = instrAddr + 5 + rel;

                if (target >= textBase && target < textBase + textSize) {
                    outEdges[count].callerAddr = funcAddr;
                    outEdges[count].calleeAddr = target;
                    outEdges[count].callSite = instrAddr;
                    outEdges[count].isJmp = false;
                    count++;
                }
                i += 4;
                continue;
            }

            // E9 xx xx xx xx — JMP rel32 (tail call)
            if (code[i] == 0xE9) {
                int32_t rel;
                std::memcpy(&rel, &code[i + 1], 4);
                uintptr_t target = instrAddr + 5 + rel;

                if (target >= textBase && target < textBase + textSize && target != funcAddr) {
                    outEdges[count].callerAddr = funcAddr;
                    outEdges[count].calleeAddr = target;
                    outEdges[count].callSite = instrAddr;
                    outEdges[count].isJmp = true;
                    count++;
                }
                i += 4;
                continue;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Access violation — return what we found so far
    }

    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CALL GRAPH ANALYZER
// ═══════════════════════════════════════════════════════════════════════════

bool CallGraphAnalyzer::Init(uintptr_t moduleBase, size_t moduleSize,
                              const PDataEnumerator* pdata) {
    m_moduleBase = moduleBase;
    m_moduleSize = moduleSize;
    m_pdata = pdata;
    FindTextSection();
    return m_textBase != 0 && m_pdata != nullptr;
}

void CallGraphAnalyzer::FindTextSection() {
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
            return;
        }
    }
}

bool CallGraphAnalyzer::IsInText(uintptr_t addr) const {
    return addr >= m_textBase && addr < m_textBase + m_textSize;
}

CallNode& CallGraphAnalyzer::GetOrCreateNode(uintptr_t addr) {
    auto it = m_nodes.find(addr);
    if (it != m_nodes.end()) return it->second;

    CallNode node;
    node.address = addr;
    auto result = m_nodes.emplace(addr, std::move(node));
    return result.first->second;
}

// ═══════════════════════════════════════════════════════════════════════════
//  FUNCTION ANALYSIS
// ═══════════════════════════════════════════════════════════════════════════

std::vector<CallEdge> CallGraphAnalyzer::AnalyzeFunction(uintptr_t funcAddr,
                                                          size_t funcSize) const {
    std::vector<CallEdge> edges;
    if (!funcAddr || funcSize == 0) return edges;

    // Use SEH helper to scan function body
    constexpr size_t MAX_EDGES_PER_FUNC = 4096;
    RawCallEdge rawEdges[MAX_EDGES_PER_FUNC];

    size_t count = SEH_AnalyzeFunction(funcAddr, funcSize, m_textBase, m_textSize,
                                        rawEdges, MAX_EDGES_PER_FUNC);

    edges.reserve(count);
    for (size_t i = 0; i < count; i++) {
        CallEdge edge;
        edge.callerAddr = rawEdges[i].callerAddr;
        edge.calleeAddr = rawEdges[i].calleeAddr;
        edge.callSite = rawEdges[i].callSite;
        edge.isDirectCall = true;
        edge.isJmp = rawEdges[i].isJmp;
        edges.push_back(edge);
    }

    return edges;
}

// ═══════════════════════════════════════════════════════════════════════════
//  FULL GRAPH BUILD
// ═══════════════════════════════════════════════════════════════════════════

size_t CallGraphAnalyzer::BuildFullGraph() {
    if (!m_pdata) return 0;

    m_nodes.clear();
    m_totalEdges = 0;

    const auto& functions = m_pdata->GetFunctions();
    size_t analyzed = 0;

    for (const auto& func : functions) {
        if (func.size < 5) continue;

        auto edges = AnalyzeFunction(func.startVA, func.size);
        if (edges.empty()) continue;

        auto& callerNode = GetOrCreateNode(func.startVA);
        callerNode.label = func.label;

        for (const auto& edge : edges) {
            uintptr_t calleeStart = edge.calleeAddr;
            if (auto* calleeFunc = m_pdata->FindFunction(edge.calleeAddr)) {
                calleeStart = calleeFunc->startVA;
            }

            auto& calleeNode = GetOrCreateNode(calleeStart);
            calleeNode.callCount++;
            calleeNode.callers.push_back(func.startVA);

            callerNode.callees.push_back(calleeStart);
            callerNode.outEdges.push_back(edge);

            m_totalEdges++;
        }

        analyzed++;
    }

    spdlog::info("CallGraphAnalyzer: Analyzed {} functions, {} nodes, {} edges",
                 analyzed, m_nodes.size(), m_totalEdges);
    return m_totalEdges;
}

size_t CallGraphAnalyzer::BuildGraphFor(const std::vector<uintptr_t>& functionAddrs) {
    size_t edges = 0;

    for (uintptr_t addr : functionAddrs) {
        auto* func = m_pdata->FindFunction(addr);
        if (!func) {
            func = m_pdata->FindContaining(addr);
        }
        if (!func) continue;

        auto funcEdges = AnalyzeFunction(func->startVA, func->size);
        auto& callerNode = GetOrCreateNode(func->startVA);
        callerNode.label = func->label;

        for (const auto& edge : funcEdges) {
            uintptr_t calleeStart = edge.calleeAddr;
            if (auto* calleeFunc = m_pdata->FindFunction(edge.calleeAddr)) {
                calleeStart = calleeFunc->startVA;
            }

            auto& calleeNode = GetOrCreateNode(calleeStart);
            calleeNode.callCount++;
            calleeNode.callers.push_back(func->startVA);

            callerNode.callees.push_back(calleeStart);
            callerNode.outEdges.push_back(edge);

            edges++;
        }
    }

    m_totalEdges += edges;
    return edges;
}

// ═══════════════════════════════════════════════════════════════════════════
//  LABEL PROPAGATION
// ═══════════════════════════════════════════════════════════════════════════

size_t CallGraphAnalyzer::PropagateLabels(
    const std::unordered_map<uintptr_t, std::string>& knownLabels, int depth) {
    for (const auto& [addr, label] : knownLabels) {
        auto it = m_nodes.find(addr);
        if (it != m_nodes.end()) {
            it->second.label = label;
        }
    }

    size_t labeled = 0;

    for (int d = 0; d < depth; d++) {
        std::unordered_map<uintptr_t, std::string> newLabels;

        for (const auto& [addr, node] : m_nodes) {
            if (node.label.empty()) continue;

            for (uintptr_t callee : node.callees) {
                auto calleeIt = m_nodes.find(callee);
                if (calleeIt != m_nodes.end() && calleeIt->second.label.empty()) {
                    newLabels[callee] = "callee_of_" + node.label;
                }
            }

            for (uintptr_t caller : node.callers) {
                auto callerIt = m_nodes.find(caller);
                if (callerIt != m_nodes.end() && callerIt->second.label.empty()) {
                    newLabels[caller] = "caller_of_" + node.label;
                }
            }
        }

        for (const auto& [addr, label] : newLabels) {
            m_nodes[addr].label = label;
            labeled++;
        }
    }

    spdlog::info("CallGraphAnalyzer: Propagated labels to {} additional functions", labeled);
    return labeled;
}

// ═══════════════════════════════════════════════════════════════════════════
//  QUERY API
// ═══════════════════════════════════════════════════════════════════════════

const CallNode* CallGraphAnalyzer::GetNode(uintptr_t funcAddr) const {
    auto it = m_nodes.find(funcAddr);
    return it != m_nodes.end() ? &it->second : nullptr;
}

std::vector<uintptr_t> CallGraphAnalyzer::GetCallers(uintptr_t funcAddr) const {
    auto* node = GetNode(funcAddr);
    return node ? node->callers : std::vector<uintptr_t>{};
}

std::vector<uintptr_t> CallGraphAnalyzer::GetCallees(uintptr_t funcAddr) const {
    auto* node = GetNode(funcAddr);
    return node ? node->callees : std::vector<uintptr_t>{};
}

std::vector<std::pair<uintptr_t, size_t>> CallGraphAnalyzer::GetMostCalled(size_t topN) const {
    std::vector<std::pair<uintptr_t, size_t>> result;
    for (const auto& [addr, node] : m_nodes) {
        result.push_back({addr, node.callCount});
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (result.size() > topN) result.resize(topN);
    return result;
}

std::vector<uintptr_t> CallGraphAnalyzer::GetLeafFunctions() const {
    std::vector<uintptr_t> result;
    for (const auto& [addr, node] : m_nodes) {
        if (node.callees.empty()) result.push_back(addr);
    }
    return result;
}

std::vector<uintptr_t> CallGraphAnalyzer::GetRootFunctions() const {
    std::vector<uintptr_t> result;
    for (const auto& [addr, node] : m_nodes) {
        if (node.callers.empty()) result.push_back(addr);
    }
    return result;
}

std::vector<uintptr_t> CallGraphAnalyzer::FindCallPath(uintptr_t source,
                                                         uintptr_t target,
                                                         int maxDepth) const {
    std::queue<std::vector<uintptr_t>> queue;
    std::unordered_set<uintptr_t> visited;

    queue.push({source});
    visited.insert(source);

    while (!queue.empty()) {
        auto path = queue.front();
        queue.pop();

        if (static_cast<int>(path.size()) > maxDepth + 1) break;

        uintptr_t current = path.back();

        auto* node = GetNode(current);
        if (!node) continue;

        for (uintptr_t callee : node->callees) {
            if (callee == target) {
                path.push_back(callee);
                return path;
            }
            if (visited.count(callee) == 0) {
                visited.insert(callee);
                auto newPath = path;
                newPath.push_back(callee);
                queue.push(std::move(newPath));
            }
        }
    }

    return {};
}

std::unordered_set<uintptr_t> CallGraphAnalyzer::GetNeighborhood(uintptr_t funcAddr,
                                                                   int radius) const {
    std::unordered_set<uintptr_t> result;
    std::queue<std::pair<uintptr_t, int>> queue;

    queue.push({funcAddr, 0});
    result.insert(funcAddr);

    while (!queue.empty()) {
        auto [addr, depth] = queue.front();
        queue.pop();

        if (depth >= radius) continue;

        auto* node = GetNode(addr);
        if (!node) continue;

        for (uintptr_t callee : node->callees) {
            if (result.count(callee) == 0) {
                result.insert(callee);
                queue.push({callee, depth + 1});
            }
        }
        for (uintptr_t caller : node->callers) {
            if (result.count(caller) == 0) {
                result.insert(caller);
                queue.push({caller, depth + 1});
            }
        }
    }

    return result;
}

void CallGraphAnalyzer::ForEachNode(
    const std::function<void(const CallNode&)>& callback) const {
    for (const auto& [addr, node] : m_nodes) callback(node);
}

} // namespace kmp

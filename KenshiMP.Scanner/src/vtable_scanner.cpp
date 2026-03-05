#include "kmp/vtable_scanner.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <DbgHelp.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  SEH HELPER FUNCTIONS — No C++ objects with destructors allowed
// ═══════════════════════════════════════════════════════════════════════════

// Raw COL data extracted inside SEH
struct RawCOLData {
    bool        valid           = false;
    uint32_t    colSignature    = 0;
    uintptr_t   colAddress      = 0;
    uintptr_t   typeDescAddr    = 0;
    uintptr_t   pVFTable        = 0;
    int32_t     classDescRVA    = 0;
    char        mangledName[256] = {};
};

static bool SEH_ReadCOL(uintptr_t colAddr, uintptr_t moduleBase, size_t moduleSize,
                         RawCOLData* out) {
    __try {
        auto* col = reinterpret_cast<const RTTICompleteObjectLocator*>(colAddr);
        if (col->signature != 1) return false;

        out->colSignature = col->signature;
        out->colAddress = colAddr;

        uintptr_t typeDescAddr = moduleBase + col->typeDescRVA;
        if (typeDescAddr < moduleBase || typeDescAddr >= moduleBase + moduleSize) return false;

        out->typeDescAddr = typeDescAddr;

        auto* typeDesc = reinterpret_cast<const RTTITypeDescriptor*>(typeDescAddr);
        if (typeDesc->pVFTable < moduleBase || typeDesc->pVFTable >= moduleBase + moduleSize)
            return false;

        out->pVFTable = typeDesc->pVFTable;
        out->classDescRVA = col->classDescRVA;

        // Copy mangled name safely
        size_t nameLen = 0;
        while (nameLen < 255 && typeDesc->name[nameLen] != '\0') {
            out->mangledName[nameLen] = typeDesc->name[nameLen];
            nameLen++;
        }
        out->mangledName[nameLen] = '\0';
        out->valid = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Raw base class data extracted inside SEH
struct RawBaseClassEntry {
    char mangledName[256];
    bool valid;
};

static int SEH_ReadBaseClasses(uintptr_t moduleBase, size_t moduleSize,
                                int32_t classDescRVA,
                                RawBaseClassEntry* outBases, int maxBases) {
    __try {
        uintptr_t chAddr = moduleBase + classDescRVA;
        if (chAddr < moduleBase || chAddr >= moduleBase + moduleSize) return 0;

        auto* ch = reinterpret_cast<const RTTIClassHierarchyDescriptor*>(chAddr);
        if (ch->numBaseClasses == 0 || ch->numBaseClasses > 64) return 0;

        uintptr_t baseArrayAddr = moduleBase + ch->baseClassArrayRVA;
        if (baseArrayAddr < moduleBase || baseArrayAddr >= moduleBase + moduleSize) return 0;

        auto* baseArray = reinterpret_cast<const int32_t*>(baseArrayAddr);
        int count = 0;

        // Skip index 0 (self)
        for (uint32_t i = 1; i < ch->numBaseClasses && i < 32 && count < maxBases; i++) {
            uintptr_t bcdAddr = moduleBase + baseArray[i];
            if (bcdAddr < moduleBase || bcdAddr >= moduleBase + moduleSize) continue;

            auto* bcd = reinterpret_cast<const RTTIBaseClassDescriptor*>(bcdAddr);
            uintptr_t baseTypeAddr = moduleBase + bcd->typeDescRVA;
            if (baseTypeAddr < moduleBase || baseTypeAddr >= moduleBase + moduleSize) continue;

            auto* baseType = reinterpret_cast<const RTTITypeDescriptor*>(baseTypeAddr);
            size_t nameLen = 0;
            while (nameLen < 255 && baseType->name[nameLen] != '\0') {
                outBases[count].mangledName[nameLen] = baseType->name[nameLen];
                nameLen++;
            }
            outBases[count].mangledName[nameLen] = '\0';
            outBases[count].valid = (nameLen > 0);
            count++;
        }
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static size_t SEH_CountVTableSlots(uintptr_t vtableAddr, uintptr_t textBase, size_t textSize) {
    __try {
        auto* slots = reinterpret_cast<const uintptr_t*>(vtableAddr);
        size_t count = 0;
        while (count < 512) {
            uintptr_t funcPtr = slots[count];
            if (funcPtr == 0) break;
            if (funcPtr < textBase || funcPtr >= textBase + textSize) break;
            count++;
        }
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Read vtable slot pointers into a plain array
static size_t SEH_ReadVTableSlots(uintptr_t vtableAddr, uintptr_t* outSlots,
                                   size_t maxSlots) {
    __try {
        auto* slots = reinterpret_cast<const uintptr_t*>(vtableAddr);
        size_t count = 0;
        for (size_t i = 0; i < maxSlots; i++) {
            outSlots[i] = slots[i];
            count++;
        }
        return count;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Scan .rdata for vtable candidates: returns pairs of (possibleCOL, firstSlot)
struct VTableCandidate {
    uintptr_t colPtr;
    uintptr_t vtableAddr;
    size_t    rdataIndex;
};

static size_t SEH_ScanRdataForCandidates(uintptr_t rdataBase, size_t rdataSize,
                                           uintptr_t textBase, size_t textSize,
                                           VTableCandidate* outCandidates, size_t maxCandidates) {
    __try {
        const uintptr_t* rdataSlots = reinterpret_cast<const uintptr_t*>(rdataBase);
        size_t numSlots = rdataSize / sizeof(uintptr_t);
        size_t found = 0;

        for (size_t i = 1; i < numSlots && found < maxCandidates; i++) {
            uintptr_t possibleCOL = rdataSlots[i - 1];
            uintptr_t firstSlot   = rdataSlots[i];

            // vtable[0] must be a code pointer
            if (firstSlot < textBase || firstSlot >= textBase + textSize) continue;

            // vtable[-1] must point into .rdata (where COL lives)
            if (possibleCOL < rdataBase || possibleCOL >= rdataBase + rdataSize) continue;

            outCandidates[found].colPtr = possibleCOL;
            outCandidates[found].vtableAddr = rdataBase + i * sizeof(uintptr_t);
            outCandidates[found].rdataIndex = i;
            found++;
        }
        return found;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  VTABLE SCANNER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

bool VTableScanner::Init(uintptr_t moduleBase, size_t moduleSize,
                          const PDataEnumerator* pdata) {
    m_moduleBase = moduleBase;
    m_moduleSize = moduleSize;
    m_pdata = pdata;
    FindSections();
    return m_textBase != 0 && m_rdataBase != 0;
}

void VTableScanner::FindSections() {
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
        } else if (std::strcmp(name, ".data") == 0) {
            m_dataBase = m_moduleBase + section->VirtualAddress;
            m_dataSize = section->Misc.VirtualSize;
        }
    }
}

bool VTableScanner::IsCodePointer(uintptr_t addr) const {
    return addr >= m_textBase && addr < m_textBase + m_textSize;
}

bool VTableScanner::IsInModule(uintptr_t addr) const {
    return addr >= m_moduleBase && addr < m_moduleBase + m_moduleSize;
}

std::string VTableScanner::DemangleName(const char* mangledName) const {
    if (!mangledName || mangledName[0] == '\0') return "";

    std::string mangled(mangledName);

    // RTTI type info names: ".?AVClassName@@" or ".?AUStructName@@"
    // Extract the class name directly — UnDecorateSymbolName often returns these as-is.
    if (mangled.size() > 4 && (mangled.substr(0, 4) == ".?AV" || mangled.substr(0, 4) == ".?AU")) {
        size_t end = mangled.find("@@");
        if (end != std::string::npos && end > 4) {
            return mangled.substr(4, end - 4);
        }
    }

    // Try MSVC demangler for other decorated names
    char demangled[512] = {};
    DWORD result = UnDecorateSymbolName(mangledName, demangled, sizeof(demangled),
                                         UNDNAME_NAME_ONLY);
    if (result > 0) return demangled;

    if (mangled[0] == '.') return mangled.substr(1);
    return mangled;
}

bool VTableScanner::ReadCOL(uintptr_t colAddr, VTableInfo& info) {
    RawCOLData raw;
    if (!SEH_ReadCOL(colAddr, m_moduleBase, m_moduleSize, &raw)) return false;

    info.colAddress = raw.colAddress;
    info.typeDescAddr = raw.typeDescAddr;
    info.mangledName = raw.mangledName;
    info.className = DemangleName(raw.mangledName);

    if (info.className.empty()) return false;

    if (raw.classDescRVA != 0) {
        info.baseClasses = ReadBaseClasses(raw.classDescRVA);
        info.inheritanceDepth = static_cast<int>(info.baseClasses.size());
    }

    return true;
}

std::vector<std::string> VTableScanner::ReadBaseClasses(int32_t classDescRVA) {
    std::vector<std::string> bases;

    RawBaseClassEntry rawBases[32];
    int count = SEH_ReadBaseClasses(m_moduleBase, m_moduleSize, classDescRVA, rawBases, 32);

    for (int i = 0; i < count; i++) {
        if (rawBases[i].valid) {
            std::string baseName = DemangleName(rawBases[i].mangledName);
            if (!baseName.empty()) {
                bases.push_back(baseName);
            }
        }
    }

    return bases;
}

size_t VTableScanner::CountVTableSlots(uintptr_t vtableAddr) const {
    return SEH_CountVTableSlots(vtableAddr, m_textBase, m_textSize);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN VTABLE SCAN
// ═══════════════════════════════════════════════════════════════════════════

size_t VTableScanner::ScanVTables() {
    if (!m_rdataBase || !m_rdataSize) return 0;

    m_vtables.clear();
    m_classNameIndex.clear();

    // Phase 1: SEH-protected scan for vtable candidates in .rdata
    // Allocate a reasonable buffer for candidates
    constexpr size_t MAX_CANDIDATES = 65536;
    auto candidates = std::make_unique<VTableCandidate[]>(MAX_CANDIDATES);

    size_t numCandidates = SEH_ScanRdataForCandidates(
        m_rdataBase, m_rdataSize, m_textBase, m_textSize,
        candidates.get(), MAX_CANDIDATES);

    spdlog::info("VTableScanner: Found {} vtable candidates in .rdata", numCandidates);

    // Phase 2: For each candidate, read COL and build VTableInfo (C++ objects safe here)
    size_t found = 0;
    for (size_t c = 0; c < numCandidates; c++) {
        const auto& cand = candidates[c];

        VTableInfo info;
        if (!ReadCOL(cand.colPtr, info)) continue;

        info.address = cand.vtableAddr;
        info.slotCount = CountVTableSlots(cand.vtableAddr);
        if (info.slotCount == 0) continue;

        // Read slot function pointers via SEH
        constexpr size_t MAX_SLOTS = 512;
        uintptr_t rawSlots[MAX_SLOTS] = {};
        size_t slotsRead = SEH_ReadVTableSlots(cand.vtableAddr, rawSlots,
                                                 info.slotCount < MAX_SLOTS ? info.slotCount : MAX_SLOTS);

        info.slots.resize(slotsRead);
        for (size_t s = 0; s < slotsRead; s++) {
            info.slots[s].index = static_cast<int>(s);
            info.slots[s].funcAddress = rawSlots[s];

            if (m_pdata) {
                auto* func = m_pdata->FindFunction(rawSlots[s]);
                if (func && !func->label.empty()) {
                    info.slots[s].funcLabel = func->label;
                }
            }
        }

        m_classNameIndex[info.className] = m_vtables.size();
        m_vtables.push_back(std::move(info));
        found++;
    }

    spdlog::info("VTableScanner: Found {} vtables with RTTI", found);
    return found;
}

// ═══════════════════════════════════════════════════════════════════════════
//  QUERY API
// ═══════════════════════════════════════════════════════════════════════════

const VTableInfo* VTableScanner::FindByClassName(const std::string& name) const {
    auto it = m_classNameIndex.find(name);
    if (it != m_classNameIndex.end()) return &m_vtables[it->second];

    for (const auto& vt : m_vtables) {
        if (vt.className.find(name) != std::string::npos) return &vt;
    }
    return nullptr;
}

std::vector<const VTableInfo*> VTableScanner::FindDerivedClasses(
    const std::string& baseName) const {
    std::vector<const VTableInfo*> result;
    for (const auto& vt : m_vtables) {
        for (const auto& base : vt.baseClasses) {
            if (base.find(baseName) != std::string::npos) {
                result.push_back(&vt);
                break;
            }
        }
    }
    return result;
}

uintptr_t VTableScanner::GetVirtualFunction(const std::string& className, int slotIndex) const {
    auto* vt = FindByClassName(className);
    if (!vt || slotIndex < 0 || slotIndex >= static_cast<int>(vt->slotCount)) return 0;
    return vt->slots[slotIndex].funcAddress;
}

const std::vector<VTableSlot>* VTableScanner::GetVirtualFunctions(
    const std::string& className) const {
    auto* vt = FindByClassName(className);
    return vt ? &vt->slots : nullptr;
}

void VTableScanner::ForEach(const std::function<void(const VTableInfo&)>& callback) const {
    for (const auto& vt : m_vtables) callback(vt);
}

VTableScanner::Stats VTableScanner::GetStats() const {
    Stats stats;
    stats.totalVTables = m_vtables.size();
    stats.uniqueClasses = m_classNameIndex.size();

    for (const auto& vt : m_vtables) {
        stats.totalSlots += vt.slotCount;
        if (vt.slotCount > stats.maxSlotCount) stats.maxSlotCount = vt.slotCount;
        if (static_cast<size_t>(vt.inheritanceDepth) > stats.maxInheritDepth)
            stats.maxInheritDepth = vt.inheritanceDepth;
    }

    return stats;
}

} // namespace kmp

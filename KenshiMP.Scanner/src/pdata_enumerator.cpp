#include "kmp/pdata_enumerator.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace kmp {

// ---------------------------------------------------------------------------
// SEH helper for Enumerate()
// MSVC C2712: __try cannot be in a function that has C++ objects with dtors.
// This static C-style helper takes only raw pointers / POD and does the
// SEH-protected memory reads, writing results into a caller-supplied buffer.
// ---------------------------------------------------------------------------
struct EnumerateEntryPOD {
    uint32_t startRVA;
    uint32_t endRVA;
    uintptr_t startVA;
    uintptr_t endVA;
    uint32_t unwindRVA;
    size_t   size;
    uint8_t  prologueSize;
    uint16_t unwindCodeCount;
    uint8_t  frameRegister;
    uint8_t  frameOffset;
};

static bool SehEnumeratePData(
    uintptr_t moduleBase,
    size_t moduleSize,
    const RUNTIME_FUNCTION* runtimeFuncs,
    size_t numEntries,
    EnumerateEntryPOD* outBuf,   // caller-allocated array of at least numEntries
    size_t* outCount)            // how many were actually written
{
    *outCount = 0;
    __try {
        for (size_t i = 0; i < numEntries; i++) {
            const auto& rf = runtimeFuncs[i];
            if (rf.BeginAddress == 0) continue;

            EnumerateEntryPOD e;
            e.startRVA      = rf.BeginAddress;
            e.endRVA        = rf.EndAddress;
            e.startVA       = moduleBase + rf.BeginAddress;
            e.endVA         = moduleBase + rf.EndAddress;
            e.unwindRVA     = rf.UnwindInfoAddress;
            e.size          = rf.EndAddress - rf.BeginAddress;
            e.prologueSize  = 0;
            e.unwindCodeCount = 0;
            e.frameRegister = 0;
            e.frameOffset   = 0;

            // Parse minimal unwind info for prologue size
            auto* unwindPtr = reinterpret_cast<const uint8_t*>(
                moduleBase + (rf.UnwindInfoAddress & ~1u)); // Mask off chain bit

            if (unwindPtr && (reinterpret_cast<uintptr_t>(unwindPtr) < moduleBase + moduleSize)) {
                e.prologueSize   = unwindPtr[1];
                e.unwindCodeCount = unwindPtr[2];
                e.frameRegister  = unwindPtr[3] & 0x0F;
                e.frameOffset    = (unwindPtr[3] >> 4) & 0x0F;
            }

            outBuf[*outCount] = e;
            (*outCount)++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SEH helper for ParseUnwindInfo()
// Reads UNWIND_INFO into a flat POD structure. The variable-length unwind
// codes are written into a caller-supplied fixed-size buffer.
// ---------------------------------------------------------------------------
struct UnwindCodePOD {
    uint8_t  codeOffset;
    uint8_t  opCode;
    uint8_t  opInfo;
    uint16_t extraData;
};

struct ParsedUnwindPOD {
    uint8_t  version;
    uint8_t  flags;
    uint8_t  prologueSize;
    uint8_t  frameRegister;
    uint8_t  frameOffset;
    uintptr_t handlerRVA;
    uintptr_t chainedRVA;
    UnwindCodePOD codes[256]; // max 255 unwind codes in practice
    size_t   codeCount;
    bool     valid;
};

static void SehParseUnwindInfo(
    uintptr_t moduleBase,
    uint32_t unwindRVA,
    ParsedUnwindPOD* out)
{
    out->valid = false;
    out->codeCount = 0;
    out->handlerRVA = 0;
    out->chainedRVA = 0;

    auto* data = reinterpret_cast<const uint8_t*>(
        moduleBase + (unwindRVA & ~1u));

    __try {
        out->version       = data[0] & 0x07;
        out->flags         = (data[0] >> 3) & 0x1F;
        out->prologueSize  = data[1];
        uint8_t codeCount  = data[2];
        out->frameRegister = data[3] & 0x0F;
        out->frameOffset   = (data[3] >> 4) & 0x0F;

        // Parse unwind codes (each is 2 bytes)
        const uint16_t* codes = reinterpret_cast<const uint16_t*>(data + 4);
        size_t outIdx = 0;
        for (uint8_t i = 0; i < codeCount; ) {
            uint16_t codeWord = codes[i];
            UnwindCodePOD code;
            code.codeOffset = static_cast<uint8_t>(codeWord & 0xFF);
            code.opCode     = static_cast<uint8_t>((codeWord >> 8) & 0x0F);
            code.opInfo     = static_cast<uint8_t>((codeWord >> 12) & 0x0F);
            code.extraData  = 0;

            switch (static_cast<UnwindOpCode>(code.opCode)) {
                case UnwindOpCode::ALLOC_LARGE:
                    if (code.opInfo == 0) {
                        code.extraData = codes[i + 1];
                        i += 2;
                    } else {
                        code.extraData = static_cast<uint16_t>(
                            codes[i + 1] | (codes[i + 2] << 16));
                        i += 3;
                    }
                    break;
                case UnwindOpCode::SAVE_NONVOL_FAR:
                case UnwindOpCode::SAVE_XMM128_FAR:
                    code.extraData = static_cast<uint16_t>(
                        codes[i + 1] | (codes[i + 2] << 16));
                    i += 3;
                    break;
                case UnwindOpCode::SAVE_NONVOL:
                case UnwindOpCode::SAVE_XMM128:
                    code.extraData = codes[i + 1];
                    i += 2;
                    break;
                default:
                    i++;
                    break;
            }

            if (outIdx < 256) {
                out->codes[outIdx++] = code;
            }
        }
        out->codeCount = outIdx;

        // Handler/chain info follows the codes (aligned to 4 bytes)
        size_t codeBytes = 4 + codeCount * 2;
        if (codeCount & 1) codeBytes += 2; // Align

        if (out->flags & 0x01) { // UNW_FLAG_EHANDLER
            uint32_t handlerRVA;
            std::memcpy(&handlerRVA, data + codeBytes, 4);
            out->handlerRVA = handlerRVA;
        }
        if (out->flags & 0x04) { // UNW_FLAG_CHAININFO
            uint32_t chainRVA;
            std::memcpy(&chainRVA, data + codeBytes, 4);
            out->chainedRVA = chainRVA;
        }

        out->valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Failed to parse — out->valid remains false
    }
}

// ---------------------------------------------------------------------------
// PDataEnumerator implementation
// ---------------------------------------------------------------------------

bool PDataEnumerator::Init(uintptr_t moduleBase, size_t moduleSize) {
    m_moduleBase = moduleBase;
    m_moduleSize = moduleSize;
    return moduleBase != 0 && moduleSize > 0;
}

bool PDataEnumerator::Enumerate() {
    if (!m_moduleBase) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(m_moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(m_moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    // Get exception directory (.pdata)
    auto& exceptDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptDir.VirtualAddress == 0 || exceptDir.Size == 0) {
        spdlog::error("PDataEnumerator: No exception directory found");
        return false;
    }

    auto* runtimeFuncs = reinterpret_cast<RUNTIME_FUNCTION*>(
        m_moduleBase + exceptDir.VirtualAddress);
    size_t numEntries = exceptDir.Size / sizeof(RUNTIME_FUNCTION);

    spdlog::info("PDataEnumerator: Found {} RUNTIME_FUNCTION entries", numEntries);

    // Allocate a POD buffer for the SEH helper to fill
    auto* podBuf = new (std::nothrow) EnumerateEntryPOD[numEntries];
    if (!podBuf) {
        spdlog::error("PDataEnumerator: Failed to allocate POD buffer for {} entries", numEntries);
        return false;
    }

    size_t podCount = 0;
    bool ok = SehEnumeratePData(m_moduleBase, m_moduleSize, runtimeFuncs,
                                numEntries, podBuf, &podCount);

    // Convert POD results into the real FunctionEntry vector (C++ objects are fine here)
    m_functions.clear();
    m_functions.reserve(podCount);

    for (size_t i = 0; i < podCount; i++) {
        const auto& p = podBuf[i];
        FunctionEntry entry;
        entry.startRVA       = p.startRVA;
        entry.endRVA         = p.endRVA;
        entry.startVA        = p.startVA;
        entry.endVA          = p.endVA;
        entry.unwindRVA      = p.unwindRVA;
        entry.size           = p.size;
        entry.prologueSize   = p.prologueSize;
        entry.unwindCodeCount = p.unwindCodeCount;
        entry.frameRegister  = p.frameRegister;
        entry.frameOffset    = p.frameOffset;
        m_functions.push_back(entry);
    }

    delete[] podBuf;

    if (!ok) {
        spdlog::error("PDataEnumerator: Exception while reading .pdata");
        return false;
    }

    BuildIndex();

    auto stats = GetStats();
    spdlog::info("PDataEnumerator: {} functions, {} total code bytes, sizes {}-{} (avg {})",
                 stats.totalFunctions, stats.totalCodeBytes,
                 stats.minSize, stats.maxSize, stats.avgSize);

    return true;
}

void PDataEnumerator::BuildIndex() {
    // Sort by start address for binary search
    std::sort(m_functions.begin(), m_functions.end(),
              [](const FunctionEntry& a, const FunctionEntry& b) {
                  return a.startVA < b.startVA;
              });
}

const FunctionEntry* PDataEnumerator::FindFunction(uintptr_t address) const {
    // Binary search for exact start address match
    auto it = std::lower_bound(m_functions.begin(), m_functions.end(), address,
                                [](const FunctionEntry& f, uintptr_t addr) {
                                    return f.startVA < addr;
                                });

    if (it != m_functions.end() && it->startVA == address) {
        return &(*it);
    }
    return nullptr;
}

const FunctionEntry* PDataEnumerator::FindContaining(uintptr_t address) const {
    if (m_functions.empty()) return nullptr;

    // Find first function with startVA > address, then go back one
    auto it = std::upper_bound(m_functions.begin(), m_functions.end(), address,
                                [](uintptr_t addr, const FunctionEntry& f) {
                                    return addr < f.startVA;
                                });

    if (it != m_functions.begin()) {
        --it;
        if (address >= it->startVA && address < it->endVA) {
            return &(*it);
        }
    }
    return nullptr;
}

std::vector<const FunctionEntry*> PDataEnumerator::GetFunctionsBySize(
    size_t minSize, size_t maxSize) const {
    std::vector<const FunctionEntry*> result;
    for (const auto& f : m_functions) {
        if (f.size >= minSize && f.size <= maxSize) {
            result.push_back(&f);
        }
    }
    return result;
}

std::vector<const FunctionEntry*> PDataEnumerator::GetFunctionsInRange(
    uintptr_t start, uintptr_t end) const {
    std::vector<const FunctionEntry*> result;

    auto it = std::lower_bound(m_functions.begin(), m_functions.end(), start,
                                [](const FunctionEntry& f, uintptr_t addr) {
                                    return f.startVA < addr;
                                });

    while (it != m_functions.end() && it->startVA < end) {
        result.push_back(&(*it));
        ++it;
    }
    return result;
}

UnwindInfo PDataEnumerator::ParseUnwindInfo(const FunctionEntry& func) const {
    UnwindInfo info;
    if (!func.unwindRVA) return info;

    // Call the SEH helper that uses only POD types
    ParsedUnwindPOD pod;
    pod.version = 0;
    pod.flags = 0;
    pod.prologueSize = 0;
    pod.frameRegister = 0;
    pod.frameOffset = 0;

    SehParseUnwindInfo(m_moduleBase, func.unwindRVA, &pod);

    if (pod.valid) {
        info.version       = pod.version;
        info.flags         = pod.flags;
        info.prologueSize  = pod.prologueSize;
        info.frameRegister = pod.frameRegister;
        info.frameOffset   = pod.frameOffset;
        info.handlerRVA    = pod.handlerRVA;
        info.chainedRVA    = pod.chainedRVA;

        info.codes.reserve(pod.codeCount);
        for (size_t i = 0; i < pod.codeCount; i++) {
            UnwindCode code;
            code.codeOffset = pod.codes[i].codeOffset;
            code.opCode     = static_cast<UnwindOpCode>(pod.codes[i].opCode);
            code.opInfo     = pod.codes[i].opInfo;
            code.extraData  = pod.codes[i].extraData;
            info.codes.push_back(code);
        }
    }

    return info;
}

void PDataEnumerator::ForEach(const std::function<void(const FunctionEntry&)>& callback) const {
    for (const auto& f : m_functions) {
        callback(f);
    }
}

PDataEnumerator::Stats PDataEnumerator::GetStats() const {
    Stats stats;
    stats.totalFunctions = m_functions.size();

    if (m_functions.empty()) return stats;

    stats.minSize = SIZE_MAX;
    stats.maxSize = 0;

    for (const auto& f : m_functions) {
        if (!f.label.empty()) stats.labeledFunctions++;
        stats.totalCodeBytes += f.size;
        if (f.size < stats.minSize) stats.minSize = f.size;
        if (f.size > stats.maxSize) stats.maxSize = f.size;
    }

    stats.avgSize = stats.totalCodeBytes / stats.totalFunctions;
    return stats;
}

} // namespace kmp

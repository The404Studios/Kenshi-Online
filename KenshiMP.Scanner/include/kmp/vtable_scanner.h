#pragma once
// VTable Scanner and C++ Class Hierarchy Mapper
//
// Scans .rdata for vtable arrays (consecutive code pointers),
// resolves RTTI type_info for C++ class names, maps inheritance
// hierarchies, and extracts virtual function slots.
//
// MSVC x64 RTTI layout:
//   vtable[-1] = pointer to CompleteObjectLocator
//   COL → TypeDescriptor → mangled class name
//   COL → ClassHierarchyDescriptor → base class list

#include "kmp/pdata_enumerator.h"
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace kmp {

// A virtual function slot
struct VTableSlot {
    int         index       = 0;    // Slot index in vtable
    uintptr_t   funcAddress = 0;    // Function pointer value
    std::string funcLabel;           // Label if known
    bool        isPureVirtual = false;
};

// A discovered vtable
struct VTableInfo {
    uintptr_t   address     = 0;    // Address of vtable in .rdata
    std::string className;           // Demangled class name (from RTTI)
    std::string mangledName;         // Raw mangled name
    size_t      slotCount   = 0;    // Number of virtual functions
    std::vector<VTableSlot> slots;

    // RTTI pointers
    uintptr_t   colAddress  = 0;    // CompleteObjectLocator
    uintptr_t   typeDescAddr = 0;   // TypeDescriptor

    // Inheritance
    std::vector<std::string> baseClasses;  // Direct and indirect bases
    int         inheritanceDepth = 0;

    bool IsValid() const { return address != 0 && !className.empty(); }
};

// MSVC RTTI structures (x64)
#pragma pack(push, 1)
struct RTTITypeDescriptor {
    uintptr_t   pVFTable;      // Always points to type_info::vftable
    uintptr_t   spare;         // Internal runtime data
    char        name[1];       // Mangled type name (variable length, null-terminated)
};

struct RTTICompleteObjectLocator {
    uint32_t    signature;     // Always 1 for x64
    uint32_t    offset;        // Offset of this vtable in the class
    uint32_t    cdOffset;      // Constructor displacement offset
    int32_t     typeDescRVA;   // RVA of TypeDescriptor
    int32_t     classDescRVA;  // RVA of ClassHierarchyDescriptor
    int32_t     selfRVA;       // RVA of this COL (for x64)
};

struct RTTIClassHierarchyDescriptor {
    uint32_t    signature;     // Always 0
    uint32_t    attributes;    // Bit 0: multiple inheritance, Bit 1: virtual inheritance
    uint32_t    numBaseClasses;
    int32_t     baseClassArrayRVA;
};

struct RTTIBaseClassDescriptor {
    int32_t     typeDescRVA;
    uint32_t    numContainedBases;
    int32_t     memberDisplacement;
    int32_t     vbtableDisplacement;
    int32_t     displacementWithinVBTable;
    uint32_t    attributes;
    int32_t     classDescRVA;
};
#pragma pack(pop)

class VTableScanner {
public:
    // Initialize with module info
    bool Init(uintptr_t moduleBase, size_t moduleSize,
              const PDataEnumerator* pdata = nullptr);

    // ── Scanning ──

    // Scan for all vtables with RTTI
    size_t ScanVTables();

    // ── Query API ──

    // Find vtable by class name (partial match)
    const VTableInfo* FindByClassName(const std::string& name) const;

    // Find all vtables for classes inheriting from a base
    std::vector<const VTableInfo*> FindDerivedClasses(const std::string& baseName) const;

    // Get virtual function at a specific slot
    uintptr_t GetVirtualFunction(const std::string& className, int slotIndex) const;

    // Get all virtual functions for a class
    const std::vector<VTableSlot>* GetVirtualFunctions(const std::string& className) const;

    // Get all discovered vtables
    const std::vector<VTableInfo>& GetAllVTables() const { return m_vtables; }
    size_t GetVTableCount() const { return m_vtables.size(); }

    // ── Iteration ──
    void ForEach(const std::function<void(const VTableInfo&)>& callback) const;

    // ── Statistics ──
    struct Stats {
        size_t totalVTables     = 0;
        size_t totalSlots       = 0;
        size_t uniqueClasses    = 0;
        size_t maxSlotCount     = 0;
        size_t maxInheritDepth  = 0;
    };
    Stats GetStats() const;

private:
    uintptr_t m_moduleBase = 0;
    size_t    m_moduleSize = 0;
    uintptr_t m_textBase   = 0;
    size_t    m_textSize   = 0;
    uintptr_t m_rdataBase  = 0;
    size_t    m_rdataSize  = 0;
    uintptr_t m_dataBase   = 0;
    size_t    m_dataSize   = 0;

    const PDataEnumerator* m_pdata = nullptr;

    std::vector<VTableInfo> m_vtables;
    std::unordered_map<std::string, size_t> m_classNameIndex; // className → vtable index

    // Internal helpers
    void FindSections();
    bool IsCodePointer(uintptr_t addr) const;
    bool IsInModule(uintptr_t addr) const;
    std::string DemangleName(const char* mangledName) const;
    bool ReadCOL(uintptr_t colAddr, VTableInfo& info);
    std::vector<std::string> ReadBaseClasses(int32_t classDescRVA);
    size_t CountVTableSlots(uintptr_t vtableAddr) const;
};

} // namespace kmp

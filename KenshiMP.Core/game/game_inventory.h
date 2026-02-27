#pragma once
#include "game_types.h"
#include "kmp/memory.h"
#include <cstdint>
#include <vector>

namespace kmp::game {

struct InventoryItem {
    uint32_t templateId = 0;
    int      quantity   = 0;
    float    condition  = 1.0f;
};

class InventoryAccessor {
public:
    explicit InventoryAccessor(uintptr_t inventoryPtr)
        : m_ptr(inventoryPtr) {}

    bool IsValid() const { return m_ptr != 0; }

    int GetItemCount() const {
        if (!IsValid()) return 0;
        int count = 0;
        Memory::Read(m_ptr + 0x08, count);
        return (count >= 0 && count < 1000) ? count : 0;
    }

    InventoryItem GetItem(int index) const {
        InventoryItem item;
        if (!IsValid() || index < 0 || index >= GetItemCount()) return item;

        uintptr_t listPtr = 0;
        Memory::Read(m_ptr + 0x00, listPtr);
        if (listPtr == 0) return item;

        uintptr_t itemPtr = 0;
        Memory::Read(listPtr + index * sizeof(uintptr_t), itemPtr);
        if (itemPtr == 0) return item;

        Memory::Read(itemPtr + 0x08, item.templateId);
        Memory::Read(itemPtr + 0x10, item.quantity);
        Memory::Read(itemPtr + 0x14, item.condition);

        return item;
    }

    std::vector<InventoryItem> GetAllItems() const {
        std::vector<InventoryItem> items;
        int count = GetItemCount();
        items.reserve(count);
        for (int i = 0; i < count; i++) {
            auto item = GetItem(i);
            if (item.templateId != 0) {
                items.push_back(item);
            }
        }
        return items;
    }

    uint32_t GetEquipment(EquipSlot slot) const {
        if (!IsValid()) return 0;

        int slotIndex = static_cast<int>(slot);
        if (slotIndex < 0 || slotIndex >= static_cast<int>(EquipSlot::Count)) return 0;

        uintptr_t equipArray = m_ptr + 0x40;
        uintptr_t itemPtr = 0;
        Memory::Read(equipArray + slotIndex * sizeof(uintptr_t), itemPtr);

        uint32_t templateId = 0;
        if (itemPtr != 0) {
            Memory::Read(itemPtr + 0x08, templateId);
        }
        return templateId;
    }

    bool SetEquipment(EquipSlot slot, uint32_t templateId) const {
        if (!IsValid()) return false;

        int slotIndex = static_cast<int>(slot);
        if (slotIndex < 0 || slotIndex >= static_cast<int>(EquipSlot::Count)) return false;

        uintptr_t equipArray = m_ptr + 0x40;
        uintptr_t itemPtr = 0;
        Memory::Read(equipArray + slotIndex * sizeof(uintptr_t), itemPtr);

        if (itemPtr != 0) {
            return Memory::Write(itemPtr + 0x08, templateId);
        }
        return false;
    }

private:
    uintptr_t m_ptr;
};

} // namespace kmp::game

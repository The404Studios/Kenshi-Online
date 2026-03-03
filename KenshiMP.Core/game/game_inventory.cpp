#include "game_inventory.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

// ── InventoryAccessor base method implementations ──

int InventoryAccessor::GetItemCount() const {
    if (!IsValid()) return 0;
    auto& offsets = GetOffsets().inventory;
    if (offsets.itemCount < 0) return 0;

    int count = 0;
    Memory::Read(m_ptr + offsets.itemCount, count);
    return (count >= 0 && count < 10000) ? count : 0;
}

uintptr_t InventoryAccessor::GetItem(int index) const {
    if (!IsValid() || index < 0 || index >= GetItemCount()) return 0;
    auto& offsets = GetOffsets().inventory;
    if (offsets.items < 0) return 0;

    uintptr_t listPtr = 0;
    Memory::Read(m_ptr + offsets.items, listPtr);
    if (listPtr == 0) return 0;

    uintptr_t itemPtr = 0;
    Memory::Read(listPtr + index * sizeof(uintptr_t), itemPtr);
    return itemPtr;
}

int InventoryAccessor::GetWidth() const {
    if (!IsValid()) return 0;
    auto& offsets = GetOffsets().inventory;
    if (offsets.width < 0) return 0;

    int width = 0;
    Memory::Read(m_ptr + offsets.width, width);
    return width;
}

int InventoryAccessor::GetHeight() const {
    if (!IsValid()) return 0;
    auto& offsets = GetOffsets().inventory;
    if (offsets.height < 0) return 0;

    int height = 0;
    Memory::Read(m_ptr + offsets.height, height);
    return height;
}

// ── InventoryAccessor item manipulation ──
// These modify inventory items by finding existing stacks and adjusting quantities.
// Creating entirely new item objects requires the game's allocator, which we don't have.

bool InventoryAccessor::AddItem(uint32_t templateId, int quantity) {
    if (!IsValid() || quantity <= 0) return false;

    auto& offsets = GetOffsets().inventory;
    int count = GetItemCount();

    uintptr_t listPtr = 0;
    if (offsets.items >= 0) {
        Memory::Read(m_ptr + offsets.items, listPtr);
    }
    if (listPtr == 0) return false;

    auto& itemOffsets = GetOffsets().item;

    for (int i = 0; i < count; i++) {
        uintptr_t itemPtr = 0;
        Memory::Read(listPtr + i * sizeof(uintptr_t), itemPtr);
        if (itemPtr == 0) continue;

        uint32_t existingId = 0;
        if (itemOffsets.templateId >= 0) {
            Memory::Read(itemPtr + itemOffsets.templateId, existingId);
        }

        if (existingId == templateId) {
            int existingQty = 0;
            if (itemOffsets.stackCount >= 0) {
                Memory::Read(itemPtr + itemOffsets.stackCount, existingQty);
                return Memory::Write(itemPtr + itemOffsets.stackCount, existingQty + quantity);
            }
        }
    }
    return false; // Item type not found in inventory
}

bool InventoryAccessor::RemoveItem(uint32_t templateId, int quantity) {
    if (!IsValid() || quantity <= 0) return false;

    auto& offsets = GetOffsets().inventory;
    int count = GetItemCount();

    uintptr_t listPtr = 0;
    if (offsets.items >= 0) {
        Memory::Read(m_ptr + offsets.items, listPtr);
    }
    if (listPtr == 0) return false;

    auto& itemOffsets = GetOffsets().item;

    for (int i = 0; i < count; i++) {
        uintptr_t itemPtr = 0;
        Memory::Read(listPtr + i * sizeof(uintptr_t), itemPtr);
        if (itemPtr == 0) continue;

        uint32_t existingId = 0;
        if (itemOffsets.templateId >= 0) {
            Memory::Read(itemPtr + itemOffsets.templateId, existingId);
        }

        if (existingId == templateId) {
            int existingQty = 0;
            if (itemOffsets.stackCount >= 0) {
                Memory::Read(itemPtr + itemOffsets.stackCount, existingQty);
                int newQty = existingQty - quantity;
                if (newQty < 0) newQty = 0;
                return Memory::Write(itemPtr + itemOffsets.stackCount, newQty);
            }
        }
    }
    return false; // Item type not found
}

bool InventoryAccessor::SetEquipment(EquipSlot slot, uint32_t templateId) {
    if (!IsValid()) return false;

    int slotIndex = static_cast<int>(slot);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(EquipSlot::Count)) return false;

    auto& offsets = GetOffsets().character;
    if (offsets.equipment < 0) {
        spdlog::debug("InventoryAccessor::SetEquipment: equipment offset unknown, skipping");
        return false;
    }

    // Equipment array is at the character's equipment offset.
    // Each slot is a pointer to an item object. We scan existing inventory items
    // to find one matching the templateId and write its pointer to the equipment slot.
    // This is best-effort — if the item isn't in inventory, we can't equip it.
    auto& itemOffsets = GetOffsets().item;
    int count = GetItemCount();
    uintptr_t listPtr = 0;
    if (GetOffsets().inventory.items >= 0) {
        Memory::Read(m_ptr + GetOffsets().inventory.items, listPtr);
    }

    if (listPtr != 0 && count > 0 && itemOffsets.templateId >= 0) {
        for (int i = 0; i < count; i++) {
            uintptr_t itemPtr = 0;
            Memory::Read(listPtr + i * sizeof(uintptr_t), itemPtr);
            if (itemPtr == 0) continue;

            uint32_t existingId = 0;
            Memory::Read(itemPtr + itemOffsets.templateId, existingId);
            if (existingId == templateId) {
                // Found matching item — write its pointer to the equipment slot
                // Equipment is relative to the character, not the inventory
                // We need the character pointer, which is the inventory owner
                uintptr_t ownerPtr = 0;
                if (GetOffsets().inventory.owner >= 0) {
                    Memory::Read(m_ptr + GetOffsets().inventory.owner, ownerPtr);
                }
                if (ownerPtr != 0 && offsets.equipment >= 0) {
                    return Memory::Write(ownerPtr + offsets.equipment + slotIndex * sizeof(uintptr_t), itemPtr);
                }
                break;
            }
        }
    }

    spdlog::debug("InventoryAccessor::SetEquipment slot={} templateId={} — item not found in inventory",
                   slotIndex, templateId);
    return false;
}

} // namespace kmp::game

#pragma once
// game_inventory.h - Inventory helper types and implementations.
// The InventoryAccessor class itself is declared in game_types.h.
// This header provides additional utility types used by inventory code.

#include "game_types.h"
#include <cstdint>
#include <vector>

namespace kmp::game {

struct InventoryItem {
    uint32_t templateId = 0;
    int      quantity   = 0;
    float    condition  = 1.0f;
};

} // namespace kmp::game

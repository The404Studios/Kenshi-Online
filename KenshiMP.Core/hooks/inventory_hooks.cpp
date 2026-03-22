#include "inventory_hooks.h"
#include "kmp/hook_manager.h"
#include "kmp/patterns.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include "kmp/safe_hook.h"
#include "../core.h"
#include "../game/game_types.h"
#include <spdlog/spdlog.h>

namespace kmp::inventory_hooks {

// ── Function typedefs ──
using ItemPickupFn = void(__fastcall*)(void* inventory, void* item, int quantity);
using ItemDropFn   = void(__fastcall*)(void* inventory, void* item);
using BuyItemFn    = void(__fastcall*)(void* buyer, void* seller, void* item, int quantity);

// ── State ──
static ItemPickupFn s_origItemPickup = nullptr;
static ItemDropFn   s_origItemDrop   = nullptr;
static BuyItemFn    s_origBuyItem    = nullptr;
static int s_pickupCount = 0;
static int s_dropCount = 0;
static int s_buyCount = 0;
static bool s_loading = false;

// ── HookHealth tracking (auto-disables trampoline on crash) ──
static HookHealth s_pickupHealth{"ItemPickup"};
static HookHealth s_dropHealth{"ItemDrop"};
static HookHealth s_buyHealth{"BuyItem"};

// ── SEH wrappers using SafeCall pattern (handles MovRaxRsp trampolines safely) ──

// void fn(void*, void*, int) — ItemPickup
static bool SEH_ItemPickup(void* inventory, void* item, int quantity) {
    return SafeCall_Void_PtrPtrI(reinterpret_cast<void*>(s_origItemPickup),
                                  inventory, item, quantity, &s_pickupHealth);
}

// void fn(void*, void*) — ItemDrop
static bool SEH_ItemDrop(void* inventory, void* item) {
    return SafeCall_Void_PtrPtr(reinterpret_cast<void*>(s_origItemDrop),
                                 inventory, item, &s_dropHealth);
}

// void fn(void*, void*, void*, int) — BuyItem (buyer, seller, item, qty)
__declspec(noinline)
static bool SEH_BuyItem(void* buyer, void* seller, void* item, int quantity) {
    if (!s_origBuyItem || s_buyHealth.trampolineFailed.load()) return false;
    __try {
        s_origBuyItem(buyer, seller, item, quantity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_buyHealth.trampolineFailed.store(true);
        s_buyHealth.failCount.fetch_add(1);
        return false;
    }
}

// ── Helpers ──

// Best-effort item template ID extraction from item pointer
static uint32_t TryGetItemTemplateId(void* item) {
    if (!item) return 0;
    static const game::ItemOffsets offsets;
    uint32_t templateId = 0;
    if (Memory::Read(reinterpret_cast<uintptr_t>(item) + offsets.templateId, templateId)) {
        return templateId;
    }
    return 0;
}

// ── Hooks ──

static void __fastcall Hook_ItemPickup(void* inventory, void* item, int quantity) {
    s_pickupCount++;

    if (!SEH_ItemPickup(inventory, item, quantity)) {
        if (s_pickupHealth.trampolineFailed.load()) {
            spdlog::error("inventory_hooks: ItemPickup trampoline CRASHED — hook auto-disabled");
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    // Registry maps CHARACTER pointers, not inventory pointers.
    // Read the inventory owner at inventory+0x28 (InventoryOffsets::owner).
    auto& registry = core.GetEntityRegistry();
    uintptr_t ownerPtr = 0;
    Memory::Read(reinterpret_cast<uintptr_t>(inventory) + game::GetOffsets().inventory.owner, ownerPtr);
    void* owner = reinterpret_cast<void*>(ownerPtr);
    EntityID netId = (owner != nullptr) ? registry.GetNetId(owner) : INVALID_ENTITY;
    if (netId == INVALID_ENTITY) {
        if (s_pickupCount % 100 == 1) {
            spdlog::debug("inventory_hooks: ItemPickup #{} (inv=0x{:X}, owner=0x{:X}, not tracked)",
                           s_pickupCount, (uintptr_t)inventory, ownerPtr);
        }
        return;
    }

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_ItemPickup);
    MsgItemPickup msg{};
    msg.entityId = netId;
    msg.itemTemplateId = TryGetItemTemplateId(item);
    msg.quantity = quantity;
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());

    spdlog::debug("inventory_hooks: ItemPickup #{} sent (entity={}, qty={})",
                   s_pickupCount, netId, quantity);
}

static void __fastcall Hook_ItemDrop(void* inventory, void* item) {
    s_dropCount++;

    if (!SEH_ItemDrop(inventory, item)) {
        if (s_dropHealth.trampolineFailed.load()) {
            spdlog::error("inventory_hooks: ItemDrop trampoline CRASHED — hook auto-disabled");
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    // Registry maps CHARACTER pointers, not inventory pointers.
    auto& registry = core.GetEntityRegistry();
    uintptr_t ownerPtr = 0;
    Memory::Read(reinterpret_cast<uintptr_t>(inventory) + game::GetOffsets().inventory.owner, ownerPtr);
    void* owner = reinterpret_cast<void*>(ownerPtr);
    EntityID netId = (owner != nullptr) ? registry.GetNetId(owner) : INVALID_ENTITY;
    if (netId == INVALID_ENTITY) return;

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_ItemDrop);
    MsgItemDrop msg{};
    msg.entityId = netId;
    msg.itemTemplateId = TryGetItemTemplateId(item);
    msg.posX = msg.posY = msg.posZ = 0.f;
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());

    spdlog::debug("inventory_hooks: ItemDrop #{} sent (entity={})", s_dropCount, netId);
}

static void __fastcall Hook_BuyItem(void* buyer, void* seller, void* item, int quantity) {
    s_buyCount++;

    if (!SEH_BuyItem(buyer, seller, item, quantity)) {
        if (s_buyHealth.trampolineFailed.load()) {
            spdlog::error("inventory_hooks: BuyItem trampoline CRASHED — hook auto-disabled");
        }
        return;
    }

    if (s_loading) return;

    auto& core = Core::Get();
    if (!core.IsConnected()) return;

    auto& registry = core.GetEntityRegistry();
    EntityID buyerNetId = registry.GetNetId(buyer);
    if (buyerNetId == INVALID_ENTITY) return;

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_TradeRequest);
    MsgTradeRequest msg{};
    msg.buyerEntityId = buyerNetId;
    msg.sellerEntityId = registry.GetNetId(seller);
    msg.itemTemplateId = TryGetItemTemplateId(item);
    msg.quantity = quantity;
    msg.price = 0;
    writer.WriteRaw(&msg, sizeof(msg));
    core.GetClient().SendReliable(writer.Data(), writer.Size());

    spdlog::info("inventory_hooks: BuyItem #{} sent (buyer={}, qty={})",
                  s_buyCount, buyerNetId, quantity);
}

// ── Install / Uninstall ──

bool Install() {
    auto& funcs = Core::Get().GetGameFunctions();
    auto& hooks = HookManager::Get();
    int installed = 0;

    if (funcs.ItemPickup) {
        if (hooks.InstallAt("ItemPickup", reinterpret_cast<uintptr_t>(funcs.ItemPickup),
                            &Hook_ItemPickup, &s_origItemPickup)) {
            installed++;
            spdlog::info("inventory_hooks: ItemPickup hook installed");
        }
    }

    if (funcs.ItemDrop) {
        if (hooks.InstallAt("ItemDrop", reinterpret_cast<uintptr_t>(funcs.ItemDrop),
                            &Hook_ItemDrop, &s_origItemDrop)) {
            installed++;
            spdlog::info("inventory_hooks: ItemDrop hook installed");
        }
    }

    if (funcs.BuyItem) {
        if (hooks.InstallAt("BuyItem", reinterpret_cast<uintptr_t>(funcs.BuyItem),
                            &Hook_BuyItem, &s_origBuyItem)) {
            installed++;
            spdlog::info("inventory_hooks: BuyItem hook installed");
        }
    }

    spdlog::info("inventory_hooks: {}/3 hooks installed", installed);
    return installed > 0;
}

void Uninstall() {
    auto& hooks = HookManager::Get();
    if (s_origItemPickup) hooks.Remove("ItemPickup");
    if (s_origItemDrop)   hooks.Remove("ItemDrop");
    if (s_origBuyItem)    hooks.Remove("BuyItem");
    s_origItemPickup = nullptr;
    s_origItemDrop = nullptr;
    s_origBuyItem = nullptr;
}

void SetLoading(bool loading) {
    s_loading = loading;
}

} // namespace kmp::inventory_hooks

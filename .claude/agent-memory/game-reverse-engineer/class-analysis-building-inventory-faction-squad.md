# Building, Inventory, Faction, Squad Class Analysis
Date: 2026-03-04, kenshi_x64.exe v1.0.68

## Building Class

### Hooked Functions (5)
| Function | RVA | String Anchor |
|----------|-----|-------------|
| BuildingPlace | 0x0057CC70 | "[RootObjectFactory::createBuilding] Building" |
| BuildingDestroyed | 0x00557280 | "Building::setDestroyed" |
| BuildingDismantle | 0x002A2860 | "dismantle" |
| BuildingConstruct | 0x005547F0 | "construction progress" |
| BuildingRepair | 0x00555650 | *(none, pattern-only)* |

### Known Offsets (BuildingOffsets, UNVERIFIED)
+0x10=name(std::string), +0x48=position(Vec3), +0x58=rotation(Quat), +0x80=ownerFaction(ptr),
+0xA0=health(float), +0xA4=maxHealth(float), +0xA8=isDestroyed(bool), +0xC0=functionality(ptr),
+0xE0=inventory(ptr), +0x100=townId(u32), +0x110=buildProgress(float), +0x114=isConstructed(bool)

### Issues
- BuildingRepair has NO string anchor fallback
- BuildingDismantle prologue is too complex for single-param function; likely has extra params
- GameData backpointer hardcoded at +0x28 in hook but not in BuildingOffsets
- WorldOffsets.buildingList = -1 (no way to iterate buildings)

## Inventory/Item Classes

### Hooked Functions (3)
| Function | RVA | String Anchor |
|----------|-----|-------------|
| ItemPickup | 0x0074C8B0 | "addItem" |
| ItemDrop | 0x00745DE0 | "removeItem" |
| BuyItem | 0x0074A630 | "buyItem" |

### Known Offsets (UNVERIFIED)
Inventory: +0x10=items(ptr*), +0x18=itemCount(int), +0x20=width, +0x24=height, +0x28=owner(ptr), +0x30=maxStackMult
Item: +0x10=name(std::string), +0x20=templateId(u32?/ptr?), +0x30=stackCount(int), +0x38=quality(float),
+0x40=value(int), +0x48=weight(float), +0x50=equipSlot(u8), +0x58=condition(float)

### CRITICAL BUG
Hook_ItemPickup/Hook_ItemDrop pass `void* inventory` to registry.GetNetId(), but registry maps
CHARACTER pointers not inventory pointers. Must read owner at inventory+0x28 first.

### Issues
- ItemDrop at 0x00745DE0 starts with vtable call, not a simple removeItem; may be wrapper
- Item+0x20 templateId may be a GameData* pointer (8 bytes), not a uint32_t (4 bytes)
- No hooks for EquipItem, UnequipItem, SellItem, TransferItem

## Faction Class

### Hooked Functions (1)
| Function | RVA | String Anchor |
|----------|-----|-------------|
| FactionRelation | 0x00872E00 | "faction relation" |

### Known Offsets (UNVERIFIED)
+0x08=factionId(u32 - used in hook but NOT in FactionOffsets!), +0x10=name(std::string),
+0x30=members(ptr), +0x38=memberCount(int), +0x50=relations(map ptr),
+0x80=color1(u32), +0x84=color2(u32), +0x90=isPlayerFaction(bool), +0xA0=money(int)

### Issues
- factionId at +0x08 hardcoded in hook, missing from FactionOffsets struct
- FactionRelation function is very large (0x380 stack); likely has more params than assumed
- Relations map structure at +0x50 never dereferenced (can't read initial state)
- No faction enumeration; no faction creation/destruction hooks
- relation float range mismatch: game uses 0-100, protocol says -100 to +100

## Squad Class

### Hooked Functions (2, one disabled)
| Function | RVA | String Anchor | Status |
|----------|-----|-------------|--------|
| SquadCreate | 0x00480B50 | "Reset squad positions" | DISABLED (48 8B C4 crash) |
| SquadAddMember | 0x00928423 | "delayedSpawningChecks" | Active |

### Known Offsets (UNVERIFIED)
+0x10=name(std::string), +0x28=memberList(ptr*), +0x30=memberCount(int),
+0x38=faction(ptr), +0x40=isPlayerSquad(bool)

### Issues
- SquadCreate may not be actual creation fn (string says "Reset squad positions")
- SquadAddMember at 0x00928423 is NOT 16-byte aligned; looks like mid-function entry
- SquadAddMember pattern has no standard prologue; hooking may be unreliable
- Character->squad backpointer discovered by heuristic (fragile)
- No hooks for member removal or squad dissolution
- Squad name hardcoded as "Squad" in hook instead of reading from game object

## VTableScanner Not Applied
None of these classes have been looked up via VTableScanner::FindByClassName().
Running at runtime would give vtable addresses, slot counts, inheritance chains.
PatternEntry.vtableClass/vtableSlot fields are unused for all building/inventory/faction/squad patterns.

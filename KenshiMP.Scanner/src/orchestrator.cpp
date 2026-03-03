#include "kmp/orchestrator.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>

namespace kmp {

// ═══════════════════════════════════════════════════════════════════════════
//  RESOLUTION METHOD NAMES
// ═══════════════════════════════════════════════════════════════════════════

const char* ResolutionMethodName(ResolutionMethod method) {
    switch (method) {
        case ResolutionMethod::None:            return "None";
        case ResolutionMethod::PatternScan:     return "PatternScan";
        case ResolutionMethod::StringXref:      return "StringXref";
        case ResolutionMethod::VTableSlot:      return "VTableSlot";
        case ResolutionMethod::CallGraphTrace:  return "CallGraph";
        case ResolutionMethod::HardcodedOffset: return "Hardcoded";
        case ResolutionMethod::PDataLookup:     return "PData";
        case ResolutionMethod::ComplexPattern:  return "Complex";
        case ResolutionMethod::Manual:          return "Manual";
        default: return "Unknown";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

bool PatternOrchestrator::Init(const char* moduleName, const OrchestratorConfig& config) {
    m_config = config;

    if (!m_scanner.Init(moduleName)) {
        spdlog::error("Orchestrator: Failed to initialize scanner engine");
        return false;
    }

    uintptr_t base = m_scanner.GetBase();
    size_t size = m_scanner.GetSize();

    m_pdata.Init(base, size);
    m_strings.Init(base, size, &m_pdata);
    m_vtables.Init(base, size, &m_pdata);
    m_callGraph.Init(base, size, &m_pdata);

    m_initialized = true;
    spdlog::info("Orchestrator: Initialized for module at 0x{:X}, size 0x{:X}", base, size);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PATTERN REGISTRY
// ═══════════════════════════════════════════════════════════════════════════

void PatternOrchestrator::Register(PatternEntry entry) {
    // Check for duplicate
    auto it = m_entryIndex.find(entry.id);
    if (it != m_entryIndex.end()) {
        // Update existing
        m_entries[it->second] = std::move(entry);
        return;
    }

    m_entryIndex[entry.id] = m_entries.size();
    m_entries.push_back(std::move(entry));
}

void PatternOrchestrator::Unregister(const std::string& id) {
    auto it = m_entryIndex.find(id);
    if (it == m_entryIndex.end()) return;

    size_t idx = it->second;
    m_entries.erase(m_entries.begin() + idx);
    m_entryIndex.erase(it);

    // Rebuild index
    m_entryIndex.clear();
    for (size_t i = 0; i < m_entries.size(); i++) {
        m_entryIndex[m_entries[i].id] = i;
    }
}

const PatternEntry* PatternOrchestrator::GetEntry(const std::string& id) const {
    auto it = m_entryIndex.find(id);
    return it != m_entryIndex.end() ? &m_entries[it->second] : nullptr;
}

PatternEntry* PatternOrchestrator::GetMutableEntry(const std::string& id) {
    auto it = m_entryIndex.find(id);
    return it != m_entryIndex.end() ? &m_entries[it->second] : nullptr;
}

std::vector<const PatternEntry*> PatternOrchestrator::GetByCategory(
    const std::string& category) const {
    std::vector<const PatternEntry*> result;
    for (const auto& entry : m_entries) {
        if (entry.category == category) result.push_back(&entry);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  REGISTER BUILT-IN PATTERNS
// ═══════════════════════════════════════════════════════════════════════════

void PatternOrchestrator::RegisterBuiltinPatterns(GameFunctions& funcs) {
    auto reg = [&](const char* id, const char* cat, const char* desc,
                   const char* pat, const char* str, int strLen,
                   uint32_t rva, void** target) {
        PatternEntry e;
        e.id = id;
        e.category = cat;
        e.description = desc;
        e.pattern = pat;
        e.stringAnchor = str;
        e.stringAnchorLen = strLen;
        e.hardcodedRVA = rva;
        e.targetPtr = target;
        Register(std::move(e));
    };

    auto regGlobal = [&](const char* id, const char* cat, const char* desc,
                         uint32_t rva, uintptr_t* target) {
        PatternEntry e;
        e.id = id;
        e.category = cat;
        e.description = desc;
        e.hardcodedRVA = rva;
        e.isGlobalPointer = true;
        e.targetUintptr = target;
        Register(std::move(e));
    };

    // ── Entity Lifecycle ──
    reg("CharacterSpawn", "entity", "RootObjectFactory::process",
        patterns::CHARACTER_SPAWN, "[RootObjectFactory::process] Character", 38,
        0x00581770, &funcs.CharacterSpawn);
    reg("CharacterDestroy", "entity", "NodeList destroy / character cleanup",
        patterns::CHARACTER_DESTROY, "NodeList::destroyNodesByBuilding", 31,
        0x0038A720, &funcs.CharacterDestroy);
    reg("CreateRandomSquad", "entity", "RootObjectFactory::createRandomSquad",
        patterns::CREATE_RANDOM_SQUAD,
        "[RootObjectFactory::createRandomSquad] Missing squad leader", 59,
        0x00583A10, &funcs.CreateRandomSquad);
    reg("CharacterSerialise", "entity", "Character save/load serialization",
        patterns::CHARACTER_SERIALISE, "[Character::serialise] Character '", 33,
        0x006280A0, &funcs.CharacterSerialise);
    reg("CharacterKO", "entity", "Character knockout handler",
        patterns::CHARACTER_KO, "knockout", 8,
        0x00345C10, &funcs.CharacterKO);

    // ── Movement ──
    reg("CharacterSetPosition", "movement", "HavokCharacter::setPosition",
        patterns::CHARACTER_SET_POSITION,
        "HavokCharacter::setPosition moved someone off the world", 55,
        0x00145E50, &funcs.CharacterSetPosition);
    reg("CharacterMoveTo", "movement", "Pathfinding move command",
        patterns::CHARACTER_MOVE_TO, "pathfind", 8,
        0x002EF4E3, &funcs.CharacterMoveTo);

    // ── Combat ──
    reg("ApplyDamage", "combat", "Attack damage effect handler",
        patterns::APPLY_DAMAGE, "Attack damage effect", 20,
        0x007A33A0, &funcs.ApplyDamage);
    reg("StartAttack", "combat", "Cut/blunt damage calculation",
        patterns::START_ATTACK, "Cutting damage", 14,
        0x007B2A20, &funcs.StartAttack);
    reg("CharacterDeath", "combat", "Death from blood loss handler",
        patterns::CHARACTER_DEATH, "{1} has died from blood loss.", 28,
        0x007A6200, &funcs.CharacterDeath);
    reg("HealthUpdate", "combat", "Health/blood system + block chance",
        patterns::HEALTH_UPDATE, "block chance", 12,
        0x0086B2B0, &funcs.HealthUpdate);
    reg("CutDamageMod", "combat", "Cut damage modifier calculation",
        patterns::CUT_DAMAGE_MOD, "cut damage mod", 14,
        0x00889CD0, &funcs.CutDamageMod);
    reg("UnarmedDamage", "combat", "Unarmed damage bonus",
        patterns::UNARMED_DAMAGE, "unarmed damage bonus", 20,
        0x000CE2D0, &funcs.UnarmedDamage);
    reg("MartialArtsCombat", "combat", "Martial arts full combat handler",
        patterns::MARTIAL_ARTS_COMBAT, "Martial Arts", 12,
        0x00892120, &funcs.MartialArtsCombat);

    // ── World / Zones ──
    reg("ZoneLoad", "world", "Zone loading",
        patterns::ZONE_LOAD, "zone.%d.%d.zone", 15,
        0x00377710, &funcs.ZoneLoad);
    reg("ZoneUnload", "world", "Zone unload / navmesh teardown",
        patterns::ZONE_UNLOAD, "destroyed navmesh", 17,
        0x002EF1F0, &funcs.ZoneUnload);
    reg("BuildingPlace", "world", "Building placement / creation",
        patterns::BUILDING_PLACE,
        "[RootObjectFactory::createBuilding] Building", 45,
        0x0057CC70, &funcs.BuildingPlace);
    reg("BuildingDestroyed", "world", "Building destroyed handler",
        patterns::BUILDING_DESTROYED, "Building::setDestroyed", 22,
        0x00557280, &funcs.BuildingDestroyed);
    reg("Navmesh", "world", "Navmesh system",
        patterns::NAVMESH, nullptr, 0,
        0x00881950, &funcs.Navmesh);
    reg("SpawnCheck", "world", "Spawn collision check",
        patterns::SPAWN_CHECK, " tried to spawn inside walls!", 29,
        0x004FFAD0, &funcs.SpawnCheck);

    // ── Game Loop / Time ──
    reg("GameFrameUpdate", "core", "Main game frame tick",
        patterns::GAME_FRAME_UPDATE, "Kenshi 1.0.", 11,
        0x00123A10, &funcs.GameFrameUpdate);
    reg("TimeUpdate", "core", "Time scale handler",
        patterns::TIME_UPDATE, "timeScale", 9,
        0x00214B50, &funcs.TimeUpdate);

    // ── Save / Load ──
    reg("SaveGame", "save", "Save game function",
        patterns::SAVE_GAME, "quicksave", 9,
        0x007EF040, &funcs.SaveGame);
    reg("LoadGame", "save", "Load game function",
        patterns::LOAD_GAME, "[SaveManager::loadGame] No towns loaded.", 40,
        0x00373F00, &funcs.LoadGame);
    reg("ImportGame", "save", "Import game (new from template)",
        patterns::IMPORT_GAME, "[SaveManager::importGame] No towns loaded.", 42,
        0x00378A30, &funcs.ImportGame);
    reg("CharacterStats", "ui", "Character stats UI",
        patterns::CHARACTER_STATS, "CharacterStats_Attributes", 25,
        0x008BA700, &funcs.CharacterStats);

    // ── Squad / Platoon ──
    reg("SquadCreate", "squad", "Squad position reset / management",
        patterns::SQUAD_CREATE, "Reset squad positions", 21,
        0x00480B50, &funcs.SquadCreate);
    reg("SquadAddMember", "squad", "Delayed spawning / member additions",
        patterns::SQUAD_ADD_MEMBER, "delayedSpawningChecks", 21,
        0x00928423, &funcs.SquadAddMember);

    // ── Inventory / Items ──
    reg("ItemPickup", "inventory", "Inventory addItem",
        patterns::ITEM_PICKUP, "addItem", 7,
        0x0074C8B0, &funcs.ItemPickup);
    reg("ItemDrop", "inventory", "Inventory removeItem",
        patterns::ITEM_DROP, "removeItem", 10,
        0x00745DE0, &funcs.ItemDrop);
    reg("BuyItem", "inventory", "Shop purchase",
        patterns::BUY_ITEM, "buyItem", 7,
        0x0074A630, &funcs.BuyItem);

    // ── Faction / Diplomacy ──
    reg("FactionRelation", "faction", "Faction relation handler",
        patterns::FACTION_RELATION, "faction relation", 16,
        0x00872E00, &funcs.FactionRelation);

    // ── AI System ──
    reg("AICreate", "ai", "AI controller creation",
        patterns::AI_CREATE, "[AI::create] No faction for", 27,
        0x00622110, &funcs.AICreate);
    reg("AIPackages", "ai", "AI behavior package loader",
        patterns::AI_PACKAGES, "AI packages", 11,
        0x00271620, &funcs.AIPackages);

    // ── Turret / Ranged ──
    reg("GunTurret", "combat", "Turret operation handler",
        patterns::GUN_TURRET, "gun turret", 10,
        0x0043B690, &funcs.GunTurret);
    reg("GunTurretFire", "combat", "Turret targeting/fire handler",
        patterns::GUN_TURRET_FIRE, nullptr, 0,
        0x0043CDB0, &funcs.GunTurretFire);

    // ── Building Management ──
    reg("BuildingDismantle", "building", "Building dismantle handler",
        patterns::BUILDING_DISMANTLE, "dismantle", 9,
        0x002A2860, &funcs.BuildingDismantle);
    reg("BuildingConstruct", "building", "Building construction progress",
        patterns::BUILDING_CONSTRUCT, "construction progress", 21,
        0x005547F0, &funcs.BuildingConstruct);
    reg("BuildingRepair", "building", "Building repair handler",
        patterns::BUILDING_REPAIR, nullptr, 0,
        0x00555650, &funcs.BuildingRepair);

    // ── Global Pointers ──
    regGlobal("PlayerBase", "global", "Player data base pointer",
              0x01AC8A90, &funcs.PlayerBase);
    regGlobal("GameWorldSingleton", "global", "GameWorld singleton pointer",
              0x02133040, &funcs.GameWorldSingleton);

    // ═══════════════════════════════════════════════════════════════════════
    //  EXPANDED PATTERNS — New game systems discovered via RE
    // ═══════════════════════════════════════════════════════════════════════

    // ── Research System ──
    reg("ResearchComplete", "research", "Research item completion",
        nullptr, "Research complete", 17, 0, nullptr);
    reg("ResearchProgress", "research", "Research progress update",
        nullptr, "research progress", 17, 0, nullptr);
    reg("TechUnlock", "research", "Technology unlock handler",
        nullptr, "tech unlock", 11, 0, nullptr);

    // ── Crafting System ──
    reg("CraftItem", "crafting", "Item crafting handler",
        nullptr, "crafting item", 13, 0, nullptr);
    reg("CraftingQueue", "crafting", "Crafting queue management",
        nullptr, "crafting queue", 14, 0, nullptr);
    reg("AutoCraft", "crafting", "Automatic crafting handler",
        nullptr, "auto-craft", 10, 0, nullptr);

    // ── Dialogue System ──
    reg("DialogueStart", "dialogue", "Dialogue initiation",
        nullptr, "StartDialogue", 13, 0, nullptr);
    reg("DialogueChoice", "dialogue", "Dialogue choice handler",
        nullptr, "dialogue choice", 15, 0, nullptr);
    reg("DialogueLine", "dialogue", "Dialogue line display",
        nullptr, "DialogueLine", 12, 0, nullptr);

    // ── Weather System ──
    reg("WeatherUpdate", "weather", "Weather state update",
        nullptr, "weather", 7, 0, nullptr);
    reg("WeatherChange", "weather", "Weather transition handler",
        nullptr, "weather change", 14, 0, nullptr);
    reg("DustStorm", "weather", "Dust storm event handler",
        nullptr, "dust storm", 10, 0, nullptr);
    reg("AcidRain", "weather", "Acid rain damage handler",
        nullptr, "acid rain", 9, 0, nullptr);

    // ── Terrain / World Generation ──
    reg("TerrainLoad", "terrain", "Terrain chunk loading",
        nullptr, "terrain", 7, 0, nullptr);
    reg("WorldGenerate", "terrain", "World generation",
        nullptr, "world generation", 16, 0, nullptr);
    reg("BiomeLoad", "terrain", "Biome data loading",
        nullptr, "biome", 5, 0, nullptr);

    // ── Animation System ──
    reg("AnimationPlay", "animation", "Animation playback",
        nullptr, "playAnimation", 13, 0, nullptr);
    reg("AnimationBlend", "animation", "Animation blend/transition",
        nullptr, "animation blend", 15, 0, nullptr);
    reg("RagdollActivate", "animation", "Ragdoll physics activation",
        nullptr, "ragdoll", 7, 0, nullptr);

    // ── Pathfinding Internals ──
    reg("PathfindRequest", "pathfinding", "Pathfinding request",
        nullptr, "pathfind request", 16, 0, nullptr);
    reg("PathfindComplete", "pathfinding", "Pathfinding completion",
        nullptr, "pathfind complete", 17, 0, nullptr);
    reg("NavmeshBuild", "pathfinding", "Navmesh construction",
        nullptr, "build navmesh", 13, 0, nullptr);

    // ── Audio System ──
    reg("SoundPlay", "audio", "Sound playback",
        nullptr, "playSound", 9, 0, nullptr);
    reg("MusicChange", "audio", "Music track change",
        nullptr, "music", 5, 0, nullptr);

    // ── Mod / Plugin Loading ──
    reg("ModLoad", "modding", "Mod loading handler",
        nullptr, "loadMod", 7, 0, nullptr);
    reg("PluginInit", "modding", "Plugin initialization",
        nullptr, "Plugin_x64", 10, 0, nullptr);

    // ── UI / MyGUI ──
    reg("MyGUICreateWidget", "ui", "MyGUI widget creation",
        nullptr, "createWidget", 12, 0, nullptr);
    reg("MyGUILayoutLoad", "ui", "MyGUI layout loading",
        nullptr, "loadLayout", 10, 0, nullptr);

    // ── Ogre3D / Rendering ──
    reg("OgreRenderFrame", "render", "Ogre render frame",
        nullptr, "renderOneFrame", 14, 0, nullptr);
    reg("OgreLoadMesh", "render", "Ogre mesh loading",
        nullptr, "loadMesh", 8, 0, nullptr);
    reg("OgreLoadMaterial", "render", "Ogre material loading",
        nullptr, "loadMaterial", 12, 0, nullptr);

    // ── Havok Physics ──
    reg("HavokStep", "physics", "Havok physics step",
        nullptr, "stepDeltaTime", 13, 0, nullptr);
    reg("HavokAddBody", "physics", "Havok add rigid body",
        nullptr, "addEntity", 9, 0, nullptr);
    reg("HavokCollision", "physics", "Havok collision callback",
        nullptr, "contactPointCallback", 20, 0, nullptr);

    // ── Economy / Trade ──
    reg("TradeOpen", "economy", "Trade window open",
        nullptr, "trade window", 12, 0, nullptr);
    reg("PriceCalculate", "economy", "Item price calculation",
        nullptr, "price", 5, 0, nullptr);
    reg("MoneyTransfer", "economy", "Money transfer handler",
        nullptr, "money transfer", 14, 0, nullptr);

    // ── Stealth / Detection ──
    reg("StealthCheck", "stealth", "Stealth detection check",
        nullptr, "stealth", 7, 0, nullptr);
    reg("DetectionUpdate", "stealth", "Detection meter update",
        nullptr, "detection", 9, 0, nullptr);
    reg("TheftAttempt", "stealth", "Theft attempt handler",
        nullptr, "steal", 5, 0, nullptr);
    reg("LockpickAttempt", "stealth", "Lockpicking handler",
        nullptr, "lockpick", 8, 0, nullptr);

    // ── Medical / Healing ──
    reg("HealWound", "medical", "Wound healing handler",
        nullptr, "heal wound", 10, 0, nullptr);
    reg("BandageApply", "medical", "Bandage application",
        nullptr, "bandage", 7, 0, nullptr);
    reg("RobotRepair", "medical", "Robot repair handler",
        nullptr, "robot repair", 12, 0, nullptr);

    // ── Slavery System ──
    reg("SlaveryCapture", "slavery", "Slave capture handler",
        nullptr, "enslave", 7, 0, nullptr);
    reg("SlaveryEscape", "slavery", "Slave escape handler",
        nullptr, "escape", 6, 0, nullptr);
    reg("BountyUpdate", "slavery", "Bounty system update",
        nullptr, "bounty", 6, 0, nullptr);

    // ── Camp / Outpost ──
    reg("CampSetup", "camp", "Camp setup handler",
        nullptr, "camp", 4, 0, nullptr);
    reg("WallBuild", "camp", "Defensive wall building",
        nullptr, "wall", 4, 0, nullptr);
    reg("GateToggle", "camp", "Gate open/close handler",
        nullptr, "gate", 4, 0, nullptr);

    // ── Limb System ──
    reg("LimbLoss", "limbs", "Limb loss handler",
        nullptr, "limb loss", 9, 0, nullptr);
    reg("ProstheticAttach", "limbs", "Prosthetic limb attachment",
        nullptr, "prosthetic", 10, 0, nullptr);

    // ── Hunger / Needs ──
    reg("HungerUpdate", "needs", "Hunger update handler",
        nullptr, "hunger", 6, 0, nullptr);
    reg("FeedCharacter", "needs", "Character feeding handler",
        nullptr, "feed", 4, 0, nullptr);

    // ── World State / Events ──
    reg("WorldStateChange", "worldstate", "World state change event",
        nullptr, "world state", 11, 0, nullptr);
    reg("FactionEvent", "worldstate", "Faction event trigger",
        nullptr, "faction event", 13, 0, nullptr);
    reg("RaidTrigger", "worldstate", "Raid trigger handler",
        nullptr, "raid", 4, 0, nullptr);

    spdlog::info("Orchestrator: Registered {} pattern entries", m_entries.size());
}

// ═══════════════════════════════════════════════════════════════════════════
//  INTERNAL RESOLUTION METHODS
// ═══════════════════════════════════════════════════════════════════════════

void PatternOrchestrator::ResolveEntry(PatternEntry& entry, uintptr_t address,
                                        ResolutionMethod method, float confidence) {
    entry.resolvedAddress = address;
    entry.resolvedMethod = method;
    entry.confidence = confidence;
    entry.isResolved = true;

    // Write to target pointers
    if (entry.targetPtr) {
        *entry.targetPtr = reinterpret_cast<void*>(address);
    }
    if (entry.targetUintptr) {
        *entry.targetUintptr = address;
    }

    spdlog::info("  Resolved '{}' = 0x{:X} via {} (confidence {:.0f}%)",
                 entry.id, address, ResolutionMethodName(method), confidence * 100);
}

bool PatternOrchestrator::TryPatternScan(PatternEntry& entry) {
    if (!entry.pattern) return false;
    auto result = m_scanner.Find(entry.pattern);
    if (!result) return false;
    ResolveEntry(entry, result.address, ResolutionMethod::PatternScan, 1.0f);
    return true;
}

bool PatternOrchestrator::TryStringXref(PatternEntry& entry) {
    if (!entry.stringAnchor || entry.stringAnchorLen <= 0) return false;

    std::string searchStr(entry.stringAnchor, entry.stringAnchorLen);

    // Strategy 1: Use the string analyzer's pre-built xref database
    auto funcs = m_strings.FindFunctionsReferencingString(searchStr);
    if (!funcs.empty()) {
        spdlog::info("  StringXref '{}': found {} referencing functions via analyzer (using 0x{:X})",
                     entry.id, funcs.size(), funcs[0]);
        ResolveEntry(entry, funcs[0], ResolutionMethod::StringXref, 0.85f);
        return true;
    }

    // Strategy 2: Direct string lookup + xref check
    auto matchingStrings = m_strings.FindStrings(searchStr);
    if (!matchingStrings.empty()) {
        spdlog::info("  StringXref '{}': found {} matching strings, first has {} xrefs",
                     entry.id, matchingStrings.size(),
                     matchingStrings[0]->xrefs.size());
        if (!matchingStrings[0]->xrefs.empty()) {
            uintptr_t funcAddr = matchingStrings[0]->xrefs[0].funcAddress;
            if (funcAddr) {
                ResolveEntry(entry, funcAddr, ResolutionMethod::StringXref, 0.8f);
                return true;
            }
            spdlog::warn("  StringXref '{}': xref exists but funcAddress=0 (no .pdata?)", entry.id);
        } else {
            spdlog::warn("  StringXref '{}': string found at 0x{:X} but no code xrefs "
                         "(no LEA referencing it)", entry.id, matchingStrings[0]->address);
        }
    } else {
        spdlog::warn("  StringXref '{}': string '{}' NOT FOUND in binary",
                     entry.id, searchStr);
    }

    return false;
}

bool PatternOrchestrator::TryVTableSlot(PatternEntry& entry) {
    if (entry.vtableClass.empty() || entry.vtableSlot < 0) return false;
    uintptr_t func = m_vtables.GetVirtualFunction(entry.vtableClass, entry.vtableSlot);
    if (!func) return false;
    ResolveEntry(entry, func, ResolutionMethod::VTableSlot, 0.9f);
    return true;
}

bool PatternOrchestrator::TryHardcodedOffset(PatternEntry& entry) {
    if (entry.hardcodedRVA == 0) return false;
    uintptr_t addr = m_scanner.GetBase() + entry.hardcodedRVA;

    if (entry.isGlobalPointer) {
        // Validate that we can read through the pointer
        uintptr_t val = 0;
        if (Memory::Read(addr, val)) {
            // Accept if value is 0 (game not loaded yet) or a valid user-mode pointer
            if (val == 0 || (val > 0x10000 && val < 0x00007FFFFFFFFFFF &&
                             val != 0xFFFFFFFFFFFFFFFF && val != 0xCCCCCCCCCCCCCCCC)) {
                ResolveEntry(entry, addr, ResolutionMethod::HardcodedOffset,
                             val == 0 ? 0.5f : 0.95f);
                return true;
            }
        }
        return false;
    }

    // ═══ SAFETY: Don't blindly trust GOG hardcoded RVAs on Steam ═══
    // Hardcoded RVAs are version-specific (GOG v1.0.68). On Steam, these addresses
    // point to DIFFERENT functions. If we have a pattern for this function and the
    // pattern scan already failed (Phase 4), the bytes at this address don't match
    // our expectations — meaning this is a different binary. Hooking a random function
    // at this address would cause a CRASH.
    //
    // Only accept hardcoded RVAs when:
    //   1. No pattern exists (can't validate byte-level) — still risky, so skip too
    //   2. OR pattern scan succeeded (handled in Phase 4, won't reach here)
    if (entry.pattern) {
        // Pattern exists but scan failed → wrong binary version → skip hardcoded RVA
        spdlog::debug("  Skipping hardcoded RVA 0x{:X} for '{}' — pattern didn't match "
                      "(binary version mismatch, likely Steam vs GOG)", addr, entry.id);
        return false;
    }

    // No pattern available — validate via .pdata as a weaker check
    auto* func = m_pdata.FindFunction(addr);
    if (func) {
        spdlog::warn("  Accepting hardcoded RVA 0x{:X} for '{}' via .pdata (no pattern to validate — "
                     "may be wrong on Steam)", addr, entry.id);
        ResolveEntry(entry, addr, ResolutionMethod::HardcodedOffset, 0.6f);
        return true;
    }

    return false;
}

bool PatternOrchestrator::TryComplexPattern(PatternEntry& entry) {
    if (entry.complexPattern.components.empty()) return false;
    auto result = m_scanner.FindComplex(entry.complexPattern);
    if (!result) return false;
    ResolveEntry(entry, result.address, ResolutionMethod::ComplexPattern, result.confidence);
    return true;
}

bool PatternOrchestrator::TryCallGraphTrace(PatternEntry& entry) {
    if (!entry.stringAnchor) return false;

    // Try to find via call graph: find a nearby labeled function,
    // then trace calls to find the target
    auto* labeled = m_strings.FindLabeledFunction(entry.id);
    if (labeled) {
        ResolveEntry(entry, labeled->address, ResolutionMethod::CallGraphTrace, 0.6f);
        return true;
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PIPELINE PHASES
// ═══════════════════════════════════════════════════════════════════════════

double PatternOrchestrator::ElapsedMs(TimePoint start) const {
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void PatternOrchestrator::RunPhase1_PData() {
    if (!m_config.enablePData) return;
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 1: .pdata enumeration");
    m_pdata.Enumerate();

    m_lastReport.pdataFunctions = m_pdata.GetFunctionCount();
    m_lastReport.timing.pdata = ElapsedMs(start);
    spdlog::info("  Phase 1 complete: {} functions in {:.1f}ms",
                 m_lastReport.pdataFunctions, m_lastReport.timing.pdata);
}

void PatternOrchestrator::RunPhase2_Strings() {
    if (!m_config.enableStrings) return;
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 2: String discovery + xref analysis");

    // Re-init strings with .pdata now available
    m_strings.Init(m_scanner.GetBase(), m_scanner.GetSize(), &m_pdata);
    m_strings.ScanStrings(m_config.stringMinLength);
    if (m_config.scanWideStrings) {
        m_strings.ScanWideStrings(m_config.stringMinLength);
    }
    m_strings.ResolveXrefs();
    m_strings.LabelFunctions();

    m_lastReport.stringsFound = m_strings.GetStringCount();
    m_lastReport.xrefsResolved = m_strings.GetXrefCount();
    m_lastReport.labeledFunctions = m_strings.GetLabeledFunctionCount();
    m_lastReport.timing.strings = ElapsedMs(start);
    spdlog::info("  Phase 2 complete: {} strings, {} xrefs, {} labeled in {:.1f}ms",
                 m_lastReport.stringsFound, m_lastReport.xrefsResolved,
                 m_lastReport.labeledFunctions, m_lastReport.timing.strings);
}

void PatternOrchestrator::RunPhase3_VTables() {
    if (!m_config.enableVTables) return;
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 3: VTable scanning + RTTI");
    m_vtables.Init(m_scanner.GetBase(), m_scanner.GetSize(), &m_pdata);
    m_vtables.ScanVTables();

    m_lastReport.vtablesFound = m_vtables.GetVTableCount();
    m_lastReport.timing.vtables = ElapsedMs(start);
    spdlog::info("  Phase 3 complete: {} vtables in {:.1f}ms",
                 m_lastReport.vtablesFound, m_lastReport.timing.vtables);
}

void PatternOrchestrator::RunPhase4_PatternScan() {
    if (!m_config.enableBatchScan) return;
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 4: SIMD batch pattern scan");

    // Build batch entries for all patterns
    std::vector<BatchEntry> batchEntries;
    std::vector<size_t> entryIndices; // Maps batch index → entry index

    for (size_t i = 0; i < m_entries.size(); i++) {
        auto& entry = m_entries[i];
        if (entry.isResolved) continue;
        if (!entry.pattern) continue;

        auto parsed = ScannerEngine::Parse(entry.pattern);
        if (!parsed) continue;

        BatchEntry be;
        be.id = entry.id;
        be.pattern = *parsed;
        batchEntries.push_back(std::move(be));
        entryIndices.push_back(i);
    }

    if (!batchEntries.empty()) {
        m_scanner.BatchScan(batchEntries);

        // Apply results
        for (size_t i = 0; i < batchEntries.size(); i++) {
            if (batchEntries[i].result.valid) {
                auto& entry = m_entries[entryIndices[i]];
                ResolveEntry(entry, batchEntries[i].result.address,
                             ResolutionMethod::PatternScan, 1.0f);
                m_lastReport.resolvedByPattern++;
            }
        }
    }

    m_lastReport.timing.patternScan = ElapsedMs(start);
    spdlog::info("  Phase 4 complete: {} resolved via pattern in {:.1f}ms",
                 m_lastReport.resolvedByPattern, m_lastReport.timing.patternScan);
}

void PatternOrchestrator::RunPhase5_StringFallback() {
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 5: String xref fallback");

    for (auto& entry : m_entries) {
        if (entry.isResolved) continue;

        // Try string xref
        if (TryStringXref(entry)) {
            m_lastReport.resolvedByString++;
            continue;
        }

        // Try vtable slot
        if (TryVTableSlot(entry)) {
            m_lastReport.resolvedByVTable++;
            continue;
        }

        // Try hardcoded offset
        if (TryHardcodedOffset(entry)) {
            m_lastReport.resolvedByHardcoded++;
            continue;
        }

        // Try complex pattern
        if (TryComplexPattern(entry)) {
            m_lastReport.resolvedByComplex++;
            continue;
        }
    }

    m_lastReport.timing.stringFallback = ElapsedMs(start);
    spdlog::info("  Phase 5 complete: string={}, vtable={}, hardcoded={}, complex={} in {:.1f}ms",
                 m_lastReport.resolvedByString, m_lastReport.resolvedByVTable,
                 m_lastReport.resolvedByHardcoded, m_lastReport.resolvedByComplex,
                 m_lastReport.timing.stringFallback);
}

void PatternOrchestrator::RunPhase6_CallGraph() {
    if (!m_config.enableCallGraph) return;
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 6: Call graph analysis");

    if (m_config.fullCallGraph) {
        m_callGraph.BuildFullGraph();
    } else {
        // Build graph only for resolved functions
        std::vector<uintptr_t> knownAddrs;
        for (const auto& entry : m_entries) {
            if (entry.isResolved) knownAddrs.push_back(entry.resolvedAddress);
        }
        m_callGraph.BuildGraphFor(knownAddrs);
    }

    // Propagate labels
    if (m_config.enableLabelPropagation) {
        std::unordered_map<uintptr_t, std::string> labels;
        for (const auto& entry : m_entries) {
            if (entry.isResolved) {
                labels[entry.resolvedAddress] = entry.id;
            }
        }
        m_callGraph.PropagateLabels(labels, m_config.callGraphDepth);
    }

    // Try to resolve remaining entries via call graph
    for (auto& entry : m_entries) {
        if (entry.isResolved) continue;
        if (TryCallGraphTrace(entry)) {
            m_lastReport.resolvedByCallGraph++;
        }
    }

    m_lastReport.callGraphNodes = m_callGraph.GetNodeCount();
    m_lastReport.callGraphEdges = m_callGraph.GetEdgeCount();
    m_lastReport.timing.callGraph = ElapsedMs(start);
    spdlog::info("  Phase 6 complete: {} nodes, {} edges, {} resolved in {:.1f}ms",
                 m_lastReport.callGraphNodes, m_lastReport.callGraphEdges,
                 m_lastReport.resolvedByCallGraph, m_lastReport.timing.callGraph);
}

void PatternOrchestrator::RunPhase7_GlobalPointers() {
    auto start = Clock::now();

    spdlog::info("Orchestrator Phase 7: Global pointer validation");

    // Re-validate global pointers
    for (auto& entry : m_entries) {
        if (!entry.isGlobalPointer || !entry.isResolved) continue;

        uintptr_t val = 0;
        if (Memory::Read(entry.resolvedAddress, val)) {
            if (val > 0x10000 && val < 0x00007FFFFFFFFFFF) {
                entry.confidence = 1.0f;
                spdlog::info("  Global '{}' validated: 0x{:X} → 0x{:X}",
                             entry.id, entry.resolvedAddress, val);
            } else if (val == 0) {
                entry.confidence = 0.5f;
                spdlog::warn("  Global '{}' at 0x{:X} is null (game not loaded yet)",
                             entry.id, entry.resolvedAddress);
            }
        }
    }

    m_lastReport.timing.globalPtrs = ElapsedMs(start);
}

// ═══════════════════════════════════════════════════════════════════════════
//  FULL PIPELINE
// ═══════════════════════════════════════════════════════════════════════════

OrchestratorReport PatternOrchestrator::Run() {
    if (!m_initialized) {
        spdlog::error("Orchestrator: Not initialized — call Init() first");
        return {};
    }

    auto totalStart = Clock::now();
    m_lastReport = {};
    m_lastReport.totalEntries = static_cast<int>(m_entries.size());

    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("  Pattern Orchestrator — Starting {} entries", m_entries.size());
    spdlog::info("════════════════════════════════════════════════════════════");

    RunPhase1_PData();
    RunPhase2_Strings();
    RunPhase3_VTables();
    RunPhase4_PatternScan();
    RunPhase5_StringFallback();
    RunPhase6_CallGraph();
    RunPhase7_GlobalPointers();

    // Compile final stats
    m_lastReport.totalResolved = CountResolved();
    m_lastReport.totalFailed = m_lastReport.totalEntries - m_lastReport.totalResolved;

    for (const auto& entry : m_entries) {
        if (!entry.isResolved) {
            m_lastReport.failedEntries.push_back(entry.id);
        }
    }

    m_lastReport.timing.total = ElapsedMs(totalStart);

    LogReport(m_lastReport);
    return m_lastReport;
}

// ═══════════════════════════════════════════════════════════════════════════
//  RETRY
// ═══════════════════════════════════════════════════════════════════════════

int PatternOrchestrator::RetryFailed() {
    int resolved = 0;
    for (auto& entry : m_entries) {
        if (entry.isResolved) continue;
        if (entry.retryCount >= m_config.maxRetries) continue;
        entry.retryCount++;

        if (TryPatternScan(entry) || TryStringXref(entry) ||
            TryVTableSlot(entry) || TryHardcodedOffset(entry) ||
            TryComplexPattern(entry) || TryCallGraphTrace(entry)) {
            resolved++;
        }
    }
    spdlog::info("Orchestrator: Retry resolved {} additional entries", resolved);
    return resolved;
}

bool PatternOrchestrator::RetryEntry(const std::string& id) {
    auto* entry = GetMutableEntry(id);
    if (!entry || entry->isResolved) return false;

    entry->retryCount++;
    return TryPatternScan(*entry) || TryStringXref(*entry) ||
           TryVTableSlot(*entry) || TryHardcodedOffset(*entry) ||
           TryComplexPattern(*entry) || TryCallGraphTrace(*entry);
}

// ═══════════════════════════════════════════════════════════════════════════
//  QUERY API
// ═══════════════════════════════════════════════════════════════════════════

uintptr_t PatternOrchestrator::GetAddress(const std::string& id) const {
    auto* entry = GetEntry(id);
    return entry ? entry->resolvedAddress : 0;
}

bool PatternOrchestrator::IsResolved(const std::string& id) const {
    auto* entry = GetEntry(id);
    return entry && entry->isResolved;
}

ResolutionMethod PatternOrchestrator::GetMethod(const std::string& id) const {
    auto* entry = GetEntry(id);
    return entry ? entry->resolvedMethod : ResolutionMethod::None;
}

float PatternOrchestrator::GetConfidence(const std::string& id) const {
    auto* entry = GetEntry(id);
    return entry ? entry->confidence : 0.0f;
}

int PatternOrchestrator::CountResolved() const {
    int count = 0;
    for (const auto& entry : m_entries) {
        if (entry.isResolved) count++;
    }
    return count;
}

const std::vector<LabeledFunction>& PatternOrchestrator::GetDiscoveredFunctions() const {
    return m_strings.GetLabeledFunctions();
}

uintptr_t PatternOrchestrator::FindFunction(const std::string& name) const {
    // Check registered entries first
    auto* entry = GetEntry(name);
    if (entry && entry->isResolved) return entry->resolvedAddress;

    // Check string analyzer
    auto* labeled = m_strings.FindLabeledFunction(name);
    if (labeled) return labeled->address;

    // Check vtables
    auto* vt = m_vtables.FindByClassName(name);
    if (vt && !vt->slots.empty()) return vt->slots[0].funcAddress;

    return 0;
}

std::vector<std::string> PatternOrchestrator::GetFunctionStrings(uintptr_t addr) const {
    return m_strings.GetFunctionStrings(addr);
}

// ═══════════════════════════════════════════════════════════════════════════
//  REPORTING
// ═══════════════════════════════════════════════════════════════════════════

OrchestratorReport PatternOrchestrator::GenerateReport() const {
    return m_lastReport;
}

void PatternOrchestrator::LogReport(const OrchestratorReport& report) const {
    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("  Pattern Orchestrator — Final Report");
    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("  Resolution: {}/{} ({:.1f}%)",
                 report.totalResolved, report.totalEntries,
                 report.totalEntries > 0
                     ? 100.0f * report.totalResolved / report.totalEntries
                     : 0.0f);
    spdlog::info("    Pattern scan:  {}", report.resolvedByPattern);
    spdlog::info("    String xref:   {}", report.resolvedByString);
    spdlog::info("    VTable slot:   {}", report.resolvedByVTable);
    spdlog::info("    Call graph:    {}", report.resolvedByCallGraph);
    spdlog::info("    Hardcoded RVA: {}", report.resolvedByHardcoded);
    spdlog::info("    Complex:       {}", report.resolvedByComplex);
    spdlog::info("  Discovery:");
    spdlog::info("    .pdata funcs:  {}", report.pdataFunctions);
    spdlog::info("    Strings:       {}", report.stringsFound);
    spdlog::info("    Xrefs:         {}", report.xrefsResolved);
    spdlog::info("    VTables:       {}", report.vtablesFound);
    spdlog::info("    Call graph:    {} nodes, {} edges", report.callGraphNodes, report.callGraphEdges);
    spdlog::info("    Labeled funcs: {}", report.labeledFunctions);
    spdlog::info("  Timing:");
    spdlog::info("    .pdata:        {:.1f}ms", report.timing.pdata);
    spdlog::info("    Strings:       {:.1f}ms", report.timing.strings);
    spdlog::info("    VTables:       {:.1f}ms", report.timing.vtables);
    spdlog::info("    Pattern scan:  {:.1f}ms", report.timing.patternScan);
    spdlog::info("    String fb:     {:.1f}ms", report.timing.stringFallback);
    spdlog::info("    Call graph:    {:.1f}ms", report.timing.callGraph);
    spdlog::info("    Global ptrs:   {:.1f}ms", report.timing.globalPtrs);
    spdlog::info("    TOTAL:         {:.1f}ms", report.timing.total);

    if (!report.failedEntries.empty()) {
        spdlog::warn("  Failed ({}):", report.failedEntries.size());
        for (const auto& id : report.failedEntries) {
            spdlog::warn("    - {}", id);
        }
    }

    spdlog::info("════════════════════════════════════════════════════════════");
}

} // namespace kmp

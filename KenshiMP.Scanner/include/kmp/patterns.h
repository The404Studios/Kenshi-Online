#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>

namespace kmp {

// Known pattern signatures for Kenshi functions.
// Patterns use IDA notation: hex bytes with '?' or '??' for wildcard.
// Wildcards cover operand bytes that change between game versions
// (LEA offsets, stack sizes, call targets, immediate values).
//
// All patterns were discovered from kenshi_x64.exe using .pdata-based
// function boundary detection, string xref analysis, and binary scanning.
// The runtime string scanner (RuntimeStringScanner) is the PRIMARY resolver
// for critical functions; patterns serve as secondary validation/fallback.
//
// Developed against v1.0.68; wildcards added for cross-version portability.

// Detect the game version from the PE file version resource.
// Returns version string like "1.0.68.0" or "unknown" on failure.
std::string DetectGameVersion();

namespace patterns {

// ── D3D11/DXGI (for overlay) ──
// IDXGISwapChain::Present is found via vtable, not pattern.

// ═══════════════════════════════════════════════════════════════════════════
//  ENTITY LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════

// RootObjectFactory::process - processes character creation
// Found via "[RootObjectFactory::process] Character '"
// RVA: 0x00581770
constexpr const char* CHARACTER_SPAWN = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 20 02 00 00 48 C7 45 C0";

// NodeList destroy / character cleanup path
// Found via "NodeList::destroyNodesByBuilding"
// RVA: 0x0038A720
constexpr const char* CHARACTER_DESTROY = "48 8B C4 44 88 40 18 48 89 50 10 48 89 48 08 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 48 48";

// RootObjectFactory::createRandomSquad - squad+character batch spawning
// Found via "[RootObjectFactory::createRandomSquad] Missing squad leader"
// RVA: 0x00583A10
constexpr const char* CREATE_RANDOM_SQUAD = "48 8B C4 55 53 56 57 48 8D A8 F8 FD FF FF 48 81 EC E8 02 00 00 48 C7 45 40 FE FF FF FF 0F 29 70";

// Character serialise (save/load character data)
// Found via "[Character::serialise] Character '"
// RVA: 0x006280A0
constexpr const char* CHARACTER_SERIALISE = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 38 FF FF FF 48 81 EC C8 01 00 00 48 C7 45 C0";

// Character knockout
// Found via "knockout"
// RVA: 0x00345C10
constexpr const char* CHARACTER_KO = "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 02 48 8B E9 ? ? ? ? 8B FA";

// ═══════════════════════════════════════════════════════════════════════════
//  MOVEMENT
// ═══════════════════════════════════════════════════════════════════════════

// HavokCharacter::setPosition
// Found via "HavokCharacter::setPosition moved someone off the world"
// RVA: 0x00145E50
constexpr const char* CHARACTER_SET_POSITION = "48 8B C4 55 57 41 54 48 8D 68 C8 48 81 EC 20 01 00 00 48 C7 44 24 30 FE FF FF FF 48 89 58 18 48";

// Player move command (pathfinding)
// Found via "pathfind"
// RVA: 0x002EF4E3
constexpr const char* CHARACTER_MOVE_TO = "48 89 4C 24 ? C7 41 58 01 00 00 00 B8 0A 00 00 00 8B C8 FF 15 6C 55 F5 01 41 83 7E 58 01 B8 0A";

// ═══════════════════════════════════════════════════════════════════════════
//  COMBAT
// ═══════════════════════════════════════════════════════════════════════════

// Attack damage effect handler
// Found via "Attack damage effect"
// RVA: 0x007A33A0
constexpr const char* APPLY_DAMAGE = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 68 FC FF FF 48 81 EC 60 04 00 00 48 C7 45 98";

// Cut/blunt damage calculation
// Found via "Cutting damage" / "Blunt damage"
// RVA: 0x007B2A20
constexpr const char* START_ATTACK = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FC FF FF 48 81 EC C0 04 00 00 48 C7 44 24";

// Death from blood loss / starvation handler
// Found via "{1} has died from blood loss."
// RVA: 0x007A6200
constexpr const char* CHARACTER_DEATH = "48 8B C4 55 57 41 54 41 55 41 56 48 8D A8 28 FE FF FF 48 81 EC B0 02 00 00 48 C7 44 24 28 FE FF";

// Health/blood system - combat damage resolution + block chance
// Found via "block chance" and "damage resistance max"
// RVA: 0x0086B2B0
constexpr const char* HEALTH_UPDATE = "48 8B C4 55 57 41 54 41 55 41 56 48 8D 68 A1 48 81 EC F0 00 00 00 48 C7 44 24 28 FE FF FF FF 48";

// Cut damage modifier calculation
// Found via "cut damage mod"
// RVA: 0x00889CD0
constexpr const char* CUT_DAMAGE_MOD = "40 55 53 56 57 41 54 41 55 41 56 48 8B EC 48 83 EC 70 48 C7 45 B0 FE FF FF FF 0F 29 74 24 60 48";

// Unarmed damage bonus
// Found via "unarmed damage bonus"
// RVA: 0x000CE2D0
constexpr const char* UNARMED_DAMAGE = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC B0 00 00 00 48 C7 45 BF FE FF FF FF";

// Martial Arts full combat handler
// Found via "Martial Arts"
// RVA: 0x00892120
constexpr const char* MARTIAL_ARTS_COMBAT = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FB FF FF 48 81 EC 10 05 00 00 48 C7 45 98";

// ═══════════════════════════════════════════════════════════════════════════
//  WORLD / ZONES
// ═══════════════════════════════════════════════════════════════════════════

// Zone load function
// Found via "zone.%d.%d.zone"
// RVA: 0x00377710
constexpr const char* ZONE_LOAD = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 E0 FC FF FF 48 81 EC ? ? ? ? 48 C7 45 88 FE";

// Zone unload / navmesh teardown
// Found via "destroyed navmesh" (first xref)
// RVA: 0x002EF1F0
constexpr const char* ZONE_UNLOAD = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 50 FE FF FF FF";

// Building placement / creation
// Found via "[RootObjectFactory::createBuilding] Building '"
// RVA: 0x0057CC70
constexpr const char* BUILDING_PLACE = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FB FF FF 48 81 EC D0 04 00 00 48 C7 45 F8";

// Building destroyed handler
// Found via "Building::setDestroyed"
// RVA: 0x00557280
constexpr const char* BUILDING_DESTROYED = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 48 C7 45 30 FE FF FF FF";

// Navmesh system
// Found via "navmesh"
// RVA: 0x00881950
constexpr const char* NAVMESH = "48 89 54 24 ? 57 48 83 EC ? 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 ? 49 8B F8 48 8B DA 48 89";

// Spawn in buildings check
// Found via " tried to spawn inside walls!"
// RVA: 0x004FFAD0
constexpr const char* SPAWN_CHECK = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 30 FF FF FF 48 81 EC D0 01 00 00 48 C7 45 C0 FE";

// ═══════════════════════════════════════════════════════════════════════════
//  GAME LOOP / TIME
// ═══════════════════════════════════════════════════════════════════════════

// Game frame update / initialization
// Found via "Kenshi 1.0."
// RVA: 0x00123A10
// Extended to 45 tokens for uniqueness (false positive at 0x00788100 shares first 32 bytes)
constexpr const char* GAME_FRAME_UPDATE = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 38 FE FF FF FF 48 89 58 10 48 89 70 18 48 89 78 20 48";

// Time scale handler - processes game speed changes
// Found via "timeScale"
// RVA: 0x00214B50
constexpr const char* TIME_UPDATE = "40 55 56 48 83 EC 28 48 8B F2 48 8B E9 BA 02 00 00 00 48 8B CE E8 ? ? ? ? 84 C0 0F 84 ? ?";

// ═══════════════════════════════════════════════════════════════════════════
//  SAVE / LOAD
// ═══════════════════════════════════════════════════════════════════════════

// Save function
// Found via "quicksave"
// RVA: 0x007EF040
constexpr const char* SAVE_GAME = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F9 48 81 EC ? ? ? ? 48 C7 45 97 FE FF FF FF";

// Load function
// Found via "[SaveManager::loadGame] No towns loaded."
// RVA: 0x00373F00
constexpr const char* LOAD_GAME = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 FC FF FF 48 81 EC 40 04 00 00 48 C7 45 A8 FE";

// Import game (new game from template)
// Found via "[SaveManager::importGame] No towns loaded."
// RVA: 0x00378A30
constexpr const char* IMPORT_GAME = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 78 FC FF FF 48 81 EC 50 04 00 00 48 C7 45 C0";

// Character stats UI
// Found via "CharacterStats_Attributes"
// RVA: 0x008BA700
constexpr const char* CHARACTER_STATS = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FF FF FF 48 81 EC C0 01 00 00 48 C7 44 24";

// ═══════════════════════════════════════════════════════════════════════════
//  INPUT (handled via WndProc hook in render_hooks, patterns optional)
// ═══════════════════════════════════════════════════════════════════════════

// OIS keyboard/mouse input is handled via vtable dispatch, not hookable patterns.
// Kenshi routes input through MyGUI::InputManager::injectKeyPress/injectMouseMove.
// We intercept input at the Win32 WndProc level in render_hooks.cpp instead.
constexpr const char* INPUT_KEY_PRESSED = nullptr;
constexpr const char* INPUT_MOUSE_MOVED = nullptr;

// ═══════════════════════════════════════════════════════════════════════════
//  SQUAD / PLATOON MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

// Squad position reset / squad management entry
// Found via "Reset squad positions"
// RVA: 0x00480B50
constexpr const char* SQUAD_CREATE = "48 8B C4 55 41 54 41 55 48 8D A8 28 FF FF FF 48 81 EC C0 01 00 00 48 C7 44 24 48 FE FF FF FF 48";

// Delayed spawning checks - processes new squad member additions
// Found via "delayedSpawningChecks"
// RVA: 0x00928423
constexpr const char* SQUAD_ADD_MEMBER = "48 89 5C 24 38 48 89 74 24 48 48 89 7C 24 20 33 FF 85 C0 0F 84 ? ? ? ? 8B F7 66 90 48 8B 05";

// ═══════════════════════════════════════════════════════════════════════════
//  INVENTORY / ITEMS
// ═══════════════════════════════════════════════════════════════════════════

// Inventory addItem - adds item to character/container inventory
// Found via "addItem"
// RVA: 0x0074C8B0
constexpr const char* ITEM_PICKUP = "48 8B C4 44 89 40 18 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 40 48 C7 45 E0 FE FF FF FF 48";

// Inventory removeItem - removes item from inventory
// Found via "removeItem"
// RVA: 0x00745DE0
constexpr const char* ITEM_DROP = "40 53 48 83 EC 20 48 8B 01 45 33 C9 FF 50 28 48 8B D8 48 85 C0 74 19 4C 8D 0D ? ? ? ? 48 8D";

// Buy item from shop
// Found via "buyItem"
// RVA: 0x0074A630
constexpr const char* BUY_ITEM = "40 55 56 57 41 54 41 55 48 81 EC 00 01 00 00 48 C7 44 24 20 FE FF FF FF 48 89 9C 24 48 01 00 00";

// ═══════════════════════════════════════════════════════════════════════════
//  FACTION / DIPLOMACY
// ═══════════════════════════════════════════════════════════════════════════

// Faction relation handler - processes faction relation changes
// Found via "faction relation"
// RVA: 0x00872E00
constexpr const char* FACTION_RELATION = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 58 FD FF FF 48 81 EC 80 03 00 00 48 C7 85 90 00 00";

// ═══════════════════════════════════════════════════════════════════════════
//  AI SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

// AI::create - creates AI controller for a character
// Found via "[AI::create] No faction for"
// RVA: 0x00622110
// Extended to 41 tokens for uniqueness (false positive at 0x000AF870 shares first 29 fixed bytes)
constexpr const char* AI_CREATE = "40 57 48 81 EC 90 00 00 00 48 C7 44 24 28 FE FF FF FF 48 89 9C 24 B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 80";

// AI packages loader - loads and assigns AI behavior packages
// Found via "AI packages"
// RVA: 0x00271620
constexpr const char* AI_PACKAGES = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC 00 01 00 00 48 C7 45 9F FE FF FF FF";

// ═══════════════════════════════════════════════════════════════════════════
//  TURRET / RANGED COMBAT
// ═══════════════════════════════════════════════════════════════════════════

// Gun turret operation handler
// Found via "gun turret"
// RVA: 0x0043B690
constexpr const char* GUN_TURRET = "48 8B C4 55 41 54 41 55 48 8D 68 A1 48 81 EC C0 00 00 00 48 C7 45 E7 FE FF FF FF 48 89 58 10 48";

// Gun turret targeting/fire handler
// Found via "gun turret" (second function)
// RVA: 0x0043CDB0
constexpr const char* GUN_TURRET_FIRE = "40 55 53 56 57 41 54 41 55 48 8D AC 24 F8 FE FF FF 48 81 EC 08 02 00 00 48 C7 44 24 58 FE FF FF";


// ═══════════════════════════════════════════════════════════════════════════
//  BUILDING MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

// Building dismantle handler
// Found via "dismantle"
// RVA: 0x002A2860
constexpr const char* BUILDING_DISMANTLE = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 38 FF FF FF 48 81 EC A0 01 00 00 48 C7 45 B0 FE FF";

// Building construction progress handler
// Found via "construction progress"
// RVA: 0x005547F0
constexpr const char* BUILDING_CONSTRUCT = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 48 C7 44 24 58 FE";

// Building repair handler
// Found via "construction progress" (second function - repair path)
// RVA: 0x00555650
constexpr const char* BUILDING_REPAIR = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC 00 01 00 00 48 C7 45 87 FE FF FF";

// ═══════════════════════════════════════════════════════════════════════════
//  KNOWN POINTER CHAINS (from Cheat Engine community)
// ═══════════════════════════════════════════════════════════════════════════
// Base offsets change per version; chain offsets are stable.

struct PointerChain {
    const char* name;
    uint32_t    baseOffset;    // From kenshi_x64.exe base
    int         offsets[8];    // -1 terminated chain
    const char* description;
};

// v1.0.68 base offsets
constexpr PointerChain KNOWN_CHAINS[] = {
    {"PlayerBase",  0x01AC8A90, {-1},                          "Player data base pointer"},
    {"Health",      0x01AC8A90, {0x2B8, 0x5F8, 0x40, -1},     "Character health (float)"},
    {"StunDamage",  0x01AC8A90, {0x2B8, 0x5F8, 0x44, -1},     "Character stun damage (float)"},
    {"Money",       0x01AC8A90, {0x298, 0x78, 0x88, -1},       "Player money (int)"},
    {"CharList",    0x01AC8A90, {0x0, -1},                      "First character, +8 per next"},
};

constexpr size_t NUM_KNOWN_CHAINS = sizeof(KNOWN_CHAINS) / sizeof(KNOWN_CHAINS[0]);

// ═══════════════════════════════════════════════════════════════════════════
//  STRING ANCHORS FOR RUNTIME FALLBACK SCANNER
// ═══════════════════════════════════════════════════════════════════════════
// These unique strings are searched at runtime if patterns fail.
// Updated to match actual strings found in kenshi_x64.exe v1.0.68.

struct StringAnchor {
    const char* label;
    const char* searchString;
    int         searchStringLen;
};

constexpr StringAnchor STRING_ANCHORS[] = {
    // Entity lifecycle
    {"CharacterSpawn",       "[RootObjectFactory::process] Character",              38},
    {"CharacterDestroy",     "NodeList::destroyNodesByBuilding",                    32},
    {"CreateRandomSquad",    "[RootObjectFactory::createRandomSquad] Missing squad leader", 59},
    {"CharacterSerialise",   "[Character::serialise] Character '",                  34},
    {"CharacterKO",          "knockout",                                             8},
    // Movement
    {"CharacterSetPosition", "HavokCharacter::setPosition moved someone off the world", 55},
    {"CharacterMoveTo",      "pathfind",                                             8},
    // Combat
    {"ApplyDamage",          "Attack damage effect",                                20},
    {"StartAttack",          "Cutting damage",                                      14},
    {"CharacterDeath",       "{1} has died from blood loss.",                        29},
    {"HealthUpdate",         "block chance",                                         12},
    {"CutDamageMod",         "cut damage mod",                                      14},
    {"UnarmedDamage",        "unarmed damage bonus",                                20},
    {"MartialArtsCombat",    "Martial Arts",                                        12},
    // World / Zones
    {"ZoneLoad",             "zone.%d.%d.zone",                                     15},
    {"ZoneUnload",           "destroyed navmesh",                                   17},
    {"BuildingPlace",        "[RootObjectFactory::createBuilding] Building",         44},
    {"BuildingDestroyed",    "Building::setDestroyed",                              22},
    {"SpawnCheck",           " tried to spawn inside walls!",                        29},
    // Game loop / Time
    {"GameFrameUpdate",      "Kenshi 1.0.",                                         11},
    {"TimeUpdate",           "timeScale",                                            9},
    // Save/Load
    {"SaveGame",             "quicksave",                                             9},
    {"LoadGame",             "[SaveManager::loadGame] No towns loaded.",             40},
    {"ImportGame",           "[SaveManager::importGame] No towns loaded.",           42},
    {"CharacterStats",       "CharacterStats_Attributes",                            25},
    // Squad / Platoon
    {"SquadCreate",          "Reset squad positions",                                21},
    {"SquadAddMember",       "delayedSpawningChecks",                               21},
    // Inventory / Items
    {"ItemPickup",           "addItem",                                               7},
    {"ItemDrop",             "removeItem",                                            10},
    {"BuyItem",              "buyItem",                                                7},
    // Faction
    {"FactionRelation",      "faction relation",                                     16},
    // AI
    {"AICreate",             "[AI::create] No faction for",                          27},
    {"AIPackages",           "AI packages",                                          11},
    // Turret
    {"GunTurret",            "gun turret",                                           10},
};

constexpr size_t NUM_STRING_ANCHORS = sizeof(STRING_ANCHORS) / sizeof(STRING_ANCHORS[0]);

} // namespace patterns

// ═══════════════════════════════════════════════════════════════════════════
//  RESOLVED FUNCTION POINTERS - filled at runtime by the scanner
// ═══════════════════════════════════════════════════════════════════════════

struct GameFunctions {
    // ── Entity Lifecycle ──
    void*  CharacterSpawn       = nullptr;  // RootObjectFactory::process (0x581770)
    void*  FactoryCreate        = nullptr;  // RootObjectFactory::create (0x583400) — dispatcher, builds request struct internally
    void*  CreateRandomChar     = nullptr;  // RootObjectFactory::createRandomChar (0x5836E0) — creates random NPC
    void*  CharacterDestroy     = nullptr;  // NodeList destroy path
    void*  CreateRandomSquad    = nullptr;  // RootObjectFactory::createRandomSquad
    void*  CharacterSerialise   = nullptr;  // Character save/load serialization
    void*  CharacterKO          = nullptr;  // Knockout handler

    // ── Movement ──
    void*  CharacterSetPosition = nullptr;  // HavokCharacter::setPosition
    void*  CharacterMoveTo      = nullptr;  // Pathfinding move command

    // ── Combat ──
    void*  ApplyDamage          = nullptr;  // Attack damage effect
    void*  StartAttack          = nullptr;  // Cut/blunt damage calculation
    void*  CharacterDeath       = nullptr;  // Death from blood loss
    void*  HealthUpdate         = nullptr;  // Health/damage resolution
    void*  CutDamageMod         = nullptr;  // Cut damage modifier
    void*  UnarmedDamage        = nullptr;  // Unarmed damage bonus
    void*  MartialArtsCombat    = nullptr;  // Martial arts combat handler

    // ── World / Zones ──
    void*  ZoneLoad             = nullptr;  // Zone loading
    void*  ZoneUnload           = nullptr;  // Zone unloading / navmesh teardown
    void*  BuildingPlace        = nullptr;  // Building placement
    void*  BuildingDestroyed    = nullptr;  // Building destruction
    void*  Navmesh              = nullptr;  // Navmesh system
    void*  SpawnCheck           = nullptr;  // Spawn collision check
    void*  SquadSpawnBypass    = nullptr;  // Squad spawn check bypass (research mod: GOG 0x4FF47C)
    void*  SquadSpawnCall      = nullptr;  // Squad spawn function call site (research mod: GOG 0x4FFA88)
    void*  CharAnimUpdate      = nullptr;  // Character animation update callback (research mod: GOG 0x65F6C7)

    // ── Game Loop / Time ──
    void*  GameFrameUpdate      = nullptr;  // Main game frame tick
    void*  TimeUpdate           = nullptr;  // Time scale handler

    // ── Save / Load ──
    void*  SaveGame             = nullptr;  // Save game
    void*  LoadGame             = nullptr;  // Load game
    void*  ImportGame           = nullptr;  // Import game (new from template)
    void*  CharacterStats       = nullptr;  // Character stats UI

    // ── Input ──
    void*  InputKeyPressed      = nullptr;  // OIS key input (optional, WndProc used)
    void*  InputMouseMoved      = nullptr;  // OIS mouse input (optional, WndProc used)

    // ── Squad / Platoon ──
    void*  SquadCreate          = nullptr;  // Squad position reset / management
    void*  SquadAddMember       = nullptr;  // Delayed spawning / member additions

    // ── Inventory / Items ──
    void*  ItemPickup           = nullptr;  // Inventory addItem
    void*  ItemDrop             = nullptr;  // Inventory removeItem
    void*  BuyItem              = nullptr;  // Shop purchase

    // ── Faction / Diplomacy ──
    void*  FactionRelation      = nullptr;  // Faction relation changes

    // ── AI System ──
    void*  AICreate             = nullptr;  // AI controller creation
    void*  AIPackages           = nullptr;  // AI behavior package loader

    // ── Turret / Ranged ──
    void*  GunTurret            = nullptr;  // Turret operation
    void*  GunTurretFire        = nullptr;  // Turret targeting/fire

    // ── Building Management ──
    void*  BuildingDismantle    = nullptr;  // Dismantle building
    void*  BuildingConstruct    = nullptr;  // Construction progress
    void*  BuildingRepair       = nullptr;  // Building repair

    // ── Known Base Pointers ──
    uintptr_t PlayerBase         = 0;
    uintptr_t GameWorldSingleton = 0;

    bool IsMinimallyResolved() const {
        // On Steam, PlayerBase may not be found during init (singletons are
        // discovered later when the game loads and the values become non-null).
        // Consider resolved if we have either PlayerBase OR the critical hooks.
        if (PlayerBase != 0) return true;
        // Fallback: at least CharacterSpawn + GameFrameUpdate/TimeUpdate resolved
        // means string xref worked and hooks can install — singletons can be retried later.
        return (CharacterSpawn != nullptr) &&
               (GameFrameUpdate != nullptr || TimeUpdate != nullptr);
    }

    int CountResolved() const {
        int count = 0;
        const void* const* ptrs[] = {
            &CharacterSpawn, &CharacterDestroy, &CreateRandomSquad,
            &CharacterSerialise, &CharacterKO,
            &CharacterSetPosition, &CharacterMoveTo,
            &ApplyDamage, &StartAttack, &CharacterDeath,
            &HealthUpdate, &CutDamageMod, &UnarmedDamage, &MartialArtsCombat,
            &ZoneLoad, &ZoneUnload, &BuildingPlace, &BuildingDestroyed,
            &Navmesh, &SpawnCheck,
            &GameFrameUpdate, &TimeUpdate,
            &SaveGame, &LoadGame, &ImportGame, &CharacterStats,
            &SquadCreate, &SquadAddMember,
            &ItemPickup, &ItemDrop, &BuyItem,
            &FactionRelation,
            &AICreate, &AIPackages,
            &GunTurret, &GunTurretFire,
            &BuildingDismantle, &BuildingConstruct, &BuildingRepair,
        };
        for (auto* p : ptrs) {
            if (*p != nullptr) count++;
        }
        if (PlayerBase) count++;
        if (GameWorldSingleton) count++;
        return count;
    }

    static constexpr int TotalFunctions() { return 41; }
};

// Forward declaration for PatternScanner (defined in scanner.h)
class PatternScanner;

// Resolve game function pointers using patterns + runtime string fallback
bool ResolveGameFunctions(const PatternScanner& scanner, GameFunctions& funcs);

// Re-run global pointer discovery after game has loaded.
bool RetryGlobalDiscovery(const PatternScanner& scanner, GameFunctions& funcs);

} // namespace kmp

#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>

namespace kmp {

// Known pattern signatures for Kenshi v1.0.68 functions.
// Patterns use IDA notation: hex bytes with '?' for wildcard.
//
// All patterns were discovered from kenshi_x64.exe using .pdata-based
// function boundary detection and string xref analysis.
// When a pattern is not yet discovered, it's set to nullptr.
// The runtime string scanner (RuntimeStringScanner) provides a fallback.

namespace patterns {

// ── D3D11/DXGI (for overlay) ──
// IDXGISwapChain::Present is found via vtable, not pattern.

// ── Entity Lifecycle ──
// RootObjectFactory::process - processes character creation
// Found via "[RootObjectFactory::process] Character '"
// RVA: 0x00581770
constexpr const char* CHARACTER_SPAWN = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 20 02 00 00 48 C7 45 C0";

// Character destructor - not yet discovered
constexpr const char* CHARACTER_DESTROY = nullptr;

// ── Movement ──
// HavokCharacter::setPosition
// Found via "HavokCharacter::setPosition moved someone off the world"
// RVA: 0x00145E50
constexpr const char* CHARACTER_SET_POSITION = "48 8B C4 55 57 41 54 48 8D 68 C8 48 81 EC 20 01 00 00 48 C7 44 24 30 FE FF FF FF 48 89 58 18 48";

// Player move command (pathfinding)
// Found via "pathfind"
// RVA: 0x002EF4E3
constexpr const char* CHARACTER_MOVE_TO = "48 89 4C 24 ? C7 41 58 01 00 00 00 B8 0A 00 00 00 8B C8 FF 15 6C 55 F5 01 41 83 7E 58 01 B8 0A";

// ── Combat ──
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

// ── World ──
// Zone load function
// Found via "zone.%d.%d.zone"
// RVA: 0x00377710
constexpr const char* ZONE_LOAD = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 E0 FC FF FF 48 81 EC ? ? ? ? 48 C7 45 88 FE";

// Zone unload - not yet discovered
constexpr const char* ZONE_UNLOAD = nullptr;

// Building placement / creation
// Found via "[RootObjectFactory::createBuilding] Building '"
// RVA: 0x0057CC70
constexpr const char* BUILDING_PLACE = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FB FF FF 48 81 EC D0 04 00 00 48 C7 45 F8";

// ── Game Loop ──
// Game initialization
// Found via "Kenshi 1.0."
// RVA: 0x00123A10
constexpr const char* GAME_FRAME_UPDATE = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00 48 C7 44 24 38 FE FF FF FF";

// Time update - not yet discovered standalone
constexpr const char* TIME_UPDATE = nullptr;

// ── Save/Load ──
// Save function
// Found via "quicksave"
// RVA: 0x007EF040
constexpr const char* SAVE_GAME = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F9 48 81 EC ? ? ? ? 48 C7 45 97 FE FF FF FF";

// Load function
// Found via "[SaveManager::loadGame] No towns loaded."
// RVA: 0x00373F00
constexpr const char* LOAD_GAME = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 FC FF FF 48 81 EC 40 04 00 00 48 C7 45 A8 FE";

// ── Input ──
constexpr const char* INPUT_KEY_PRESSED = nullptr;
constexpr const char* INPUT_MOUSE_MOVED = nullptr;

// ── Additional discovered patterns ──

// Squad creation - not yet discovered
constexpr const char* SQUAD_CREATE = nullptr;
// Squad member add - not yet discovered
constexpr const char* SQUAD_ADD_MEMBER = nullptr;
// Item pickup - not yet discovered
constexpr const char* ITEM_PICKUP = nullptr;
// Item drop - not yet discovered
constexpr const char* ITEM_DROP = nullptr;

// Health/blood system - CHARACTER_DEATH covers death from blood loss
// Found via "damage resistance max"
// RVA: 0x0086B2B0
constexpr const char* HEALTH_UPDATE = "48 8B C4 55 57 41 54 41 55 41 56 48 8D 68 A1 48 81 EC F0 00 00 00 48 C7 44 24 28 FE FF FF FF 48";

// Character knockout
// Found via "knockout"
// RVA: 0x00345C10
constexpr const char* CHARACTER_KO = "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B 02 48 8B E9 ? ? ? ? 8B FA";

// Character serialise (save/load character data)
// Found via "[Character::serialise] Character '"
// RVA: 0x006280A0
constexpr const char* CHARACTER_SERIALISE = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 38 FF FF FF 48 81 EC C8 01 00 00 48 C7 45 C0";

// Character stats UI
// Found via "CharacterStats_Attributes"
// RVA: 0x008BA700
constexpr const char* CHARACTER_STATS = "48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FF FF FF 48 81 EC C0 01 00 00 48 C7 44 24";

// Navmesh system
// Found via "navmesh"
// RVA: 0x00881950
constexpr const char* NAVMESH = "48 89 54 24 ? 57 48 83 EC ? 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 ? 49 8B F8 48 8B DA 48 89";

// Building destroyed handler
// Found via "Building::setDestroyed"
// RVA: 0x00557280
constexpr const char* BUILDING_DESTROYED = "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 48 C7 45 30 FE FF FF FF";

// Cut damage modifier calculation
// Found via "cut damage mod"
// RVA: 0x00889CD0
constexpr const char* CUT_DAMAGE_MOD = "40 55 53 56 57 41 54 41 55 41 56 48 8B EC 48 83 EC 70 48 C7 45 B0 FE FF FF FF 0F 29 74 24 60 48";

// Spawn in buildings check
// Found via " tried to spawn inside walls!"
// RVA: 0x004FFAD0
constexpr const char* SPAWN_CHECK = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 30 FF FF FF 48 81 EC D0 01 00 00 48 C7 45 C0 FE";

// Unarmed damage bonus
// Found via "unarmed damage bonus"
// RVA: 0x000CE2D0
constexpr const char* UNARMED_DAMAGE = "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC B0 00 00 00 48 C7 45 BF FE FF FF FF";

// ── Known Pointer Chains (from Cheat Engine community) ──
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

// ── String anchors for runtime fallback scanner ──
// These unique strings are searched at runtime if patterns fail.
// Updated to match actual strings found in kenshi_x64.exe v1.0.68.
struct StringAnchor {
    const char* label;
    const char* searchString;
    int         searchStringLen;
};

constexpr StringAnchor STRING_ANCHORS[] = {
    {"CharacterSpawn",       "[RootObjectFactory::process] Character",    38},
    {"CharacterDestroy",     "NodeList::destroyNodesByBuilding",          32},
    {"CharacterSetPosition", "HavokCharacter::setPosition moved someone off the world", 55},
    {"CharacterMoveTo",      "pathfind",                                   8},
    {"ApplyDamage",          "Attack damage effect",                      20},
    {"StartAttack",          "Cutting damage",                            14},
    {"CharacterDeath",       "{1} has died from blood loss.",             28},
    {"ZoneLoad",             "zone.%d.%d.zone",                           15},
    {"ZoneUnload",           "destroyed navmesh",                         17},
    {"BuildingPlace",        "[RootObjectFactory::createBuilding] Building", 45},
    {"GameFrameUpdate",      "Kenshi 1.0.",                               11},
    {"TimeUpdate",           "dayTime",                                    7},
    {"SaveGame",             "quicksave",                                  9},
    {"LoadGame",             "[SaveManager::loadGame] No towns loaded.",  40},
    {"SquadCreate",          "Reset squad positions",                     21},
    {"SquadAddMember",       "delayedSpawningChecks",                    21},
    {"ItemPickup",           "unarmed damage bonus",                     20},
    {"ItemDrop",             "destroyed",                                  9},
    {"HealthUpdate",         "damage resistance max",                    21},
    {"CharacterKO",          "knockout",                                   8},
    {"CharacterSerialise",   "[Character::serialise] Character '",       33},
    {"CharacterStats",       "CharacterStats_Attributes",                25},
    {"BuildingDestroyed",    "Building::setDestroyed",                   22},
};

constexpr size_t NUM_STRING_ANCHORS = sizeof(STRING_ANCHORS) / sizeof(STRING_ANCHORS[0]);

} // namespace patterns

// Resolved function pointers filled at runtime by the scanner
struct GameFunctions {
    // Entity
    void*  CharacterSpawn       = nullptr;
    void*  CharacterDestroy     = nullptr;

    // Movement
    void*  CharacterSetPosition = nullptr;
    void*  CharacterMoveTo      = nullptr;

    // Combat
    void*  ApplyDamage          = nullptr;
    void*  StartAttack          = nullptr;
    void*  CharacterDeath       = nullptr;
    void*  CharacterKO          = nullptr;

    // World
    void*  ZoneLoad             = nullptr;
    void*  ZoneUnload           = nullptr;
    void*  BuildingPlace        = nullptr;
    void*  BuildingDestroyed    = nullptr;

    // Game loop
    void*  GameFrameUpdate      = nullptr;
    void*  TimeUpdate           = nullptr;

    // Save/Load
    void*  SaveGame             = nullptr;
    void*  LoadGame             = nullptr;
    void*  CharacterSerialise   = nullptr;

    // Input
    void*  InputKeyPressed      = nullptr;
    void*  InputMouseMoved      = nullptr;

    // Squad
    void*  SquadCreate          = nullptr;
    void*  SquadAddMember       = nullptr;

    // Inventory
    void*  ItemPickup           = nullptr;
    void*  ItemDrop             = nullptr;

    // Health & Stats
    void*  HealthUpdate         = nullptr;
    void*  CharacterStats       = nullptr;
    void*  CutDamageMod        = nullptr;

    // Navigation
    void*  Navmesh              = nullptr;

    // Known base pointers
    uintptr_t PlayerBase        = 0;
    uintptr_t GameWorldSingleton = 0;

    bool IsMinimallyResolved() const {
        // At minimum we need the player base to read positions
        return PlayerBase != 0;
    }

    int CountResolved() const {
        int count = 0;
        if (CharacterSpawn) count++;
        if (CharacterDestroy) count++;
        if (CharacterSetPosition) count++;
        if (CharacterMoveTo) count++;
        if (ApplyDamage) count++;
        if (StartAttack) count++;
        if (CharacterDeath) count++;
        if (CharacterKO) count++;
        if (ZoneLoad) count++;
        if (ZoneUnload) count++;
        if (BuildingPlace) count++;
        if (BuildingDestroyed) count++;
        if (GameFrameUpdate) count++;
        if (TimeUpdate) count++;
        if (SaveGame) count++;
        if (LoadGame) count++;
        if (CharacterSerialise) count++;
        if (HealthUpdate) count++;
        if (CharacterStats) count++;
        if (Navmesh) count++;
        if (PlayerBase) count++;
        if (GameWorldSingleton) count++;
        return count;
    }
};

// Forward declaration for PatternScanner (defined in scanner.h)
class PatternScanner;

// Resolve game function pointers using patterns + runtime string fallback
bool ResolveGameFunctions(const PatternScanner& scanner, GameFunctions& funcs);

} // namespace kmp

#pragma once
#include "kmp/types.h"
#include <cstdint>
#include <string>

// Reconstructed Kenshi game class layouts.
// Based on reverse engineering kenshi_x64.exe v1.0.68 via:
//   - Cheat Engine pointer chains (CE community verified)
//   - String xref analysis and binary scanning
//   - KServerMod and RE_Kenshi reference data
//   - fcs.def game data schema
//
// Use the pattern scanner to verify offsets at runtime.
// If offsets are -1, the accessor returns safe defaults.

namespace kmp::game {

// Forward declarations
struct KCharacter;
struct KSquad;
struct KGameWorld;
struct KBuilding;
struct KInventory;
struct KStats;
struct KFaction;
struct KItem;
struct KAIPackage;

// ═══════════════════════════════════════════════════════════════════════════
//  OFFSET TABLES
// ═══════════════════════════════════════════════════════════════════════════
// Filled at runtime by the scanner, or using hardcoded CE-verified fallbacks.

struct CharacterOffsets {
    // Core character data — KServerMod / CE verified for v1.0.68
    int name          = 0x18;    // Kenshi std::string (KServerMod verified)
    int faction       = 0x10;    // Faction* (KServerMod verified)
    int position      = 0x48;    // Vec3 read-only cached position (KServerMod verified)
    int rotation      = 0x58;    // Quat rotation (KServerMod verified)
    int sceneNode     = -1;      // Ogre::SceneNode* (not yet verified — runtime probe)
    int aiPackage     = -1;      // AI package pointer (not yet verified)
    int inventory     = 0x2E8;   // Inventory* (KServerMod verified)
    int stats         = 0x450;   // Stats base (KServerMod verified)
    int equipment     = -1;      // Equipment array (runtime probed)
    int currentTask   = -1;      // Current task type (not yet verified)
    int isAlive       = -1;      // Alive flag — use health chain fallback
    int isPlayerControlled = -1; // Player-controlled flag (not yet verified)

    // Direct health offset (if scanner finds it; otherwise use chain below)
    int health            = -1;      // Direct offset to health array (use chain if -1)

    // Movement
    int moveSpeed     = -1;      // Offset to current move speed float (derived from physics)
    int animState     = -1;      // Offset to animation state index

    // CE-verified health chain: character+2B8 -> +5F8 -> +40 = health[0]
    // Each body part is +8 stride (health float + stun float per part)
    int healthChain1  = 0x2B8;   // First pointer dereference offset
    int healthChain2  = 0x5F8;   // Second pointer dereference offset
    int healthBase    = 0x40;    // Final offset to health float
    int healthStride  = 8;       // Stride between body parts (health+stun = 2 floats)

    // Writable position chain (from KServerMod RE):
    // character -> AnimationClassHuman ptr (+animClassOffset)
    //   -> CharMovement ptr (+charMovementOffset from AnimClass)
    //     -> writable Vec3 (+writablePosOffset from CharMovement)
    //       -> x,y,z floats (+writablePosVecOffset within Vec3 struct)
    // Writing here actually moves the character in the physics engine.
    int animClassOffset      = -1;    // Offset to AnimationClassHuman* on character
    int charMovementOffset   = 0xC0;  // AnimClass -> CharMovement* (KServerMod verified)
    int writablePosOffset    = 0x320; // CharMovement -> writable position struct
    int writablePosVecOffset = 0x20;  // position struct -> x float

    // Squad pointer (heuristic: near faction in struct)
    int squad         = -1;      // Offset to KSquad* (discovered at runtime)

    // GameData backpointer (template/archetype data)
    int gameDataPtr   = 0x40;    // Offset to GameData* template

    // Money chain (CE-verified)
    // character+0x298 -> +0x78 -> +0x88 = money (int)
    int moneyChain1   = 0x298;
    int moneyChain2   = 0x78;
    int moneyBase     = 0x88;
};

struct SquadOffsets {
    int name           = 0x10;   // Offset to name string
    int memberList     = 0x28;   // Offset to member pointer array
    int memberCount    = 0x30;   // Offset to member count (int)
    int factionId      = 0x38;   // Offset to faction pointer/id
    int isPlayerSquad  = 0x40;   // Offset to is-player-squad flag
};

struct WorldOffsets {
    int timeOfDay      = -1;     // On TimeManager (+0x08), NOT GameWorld — use time_hooks
    int gameSpeed      = 0x700;  // GameWorld+0x700 (KenshiLib verified)
    int weatherState   = -1;     // Not yet verified on GameWorld
    int characterList  = 0x0888; // GameWorld+0x0888 characterArray (KenshiLib verified)
    int buildingList   = -1;     // Not yet verified
    int paused         = 0x08B9; // GameWorld+0x08B9 — bool paused flag (RE verified)
    int zoneManager    = 0x08B0; // GameWorld+0x08B0 (KenshiLib verified)
    int characterCount = -1;     // Derived from list (lektor length at +0x00)
};

struct BuildingOffsets {
    int name           = 0x10;   // Building name string
    int position       = 0x48;   // Vec3 world position
    int rotation       = 0x58;   // Quat rotation
    int ownerFaction   = 0x80;   // Faction pointer
    int health         = 0xA0;   // Building health float
    int maxHealth      = 0xA4;   // Building max health float
    int isDestroyed    = 0xA8;   // Destroyed flag (bool)
    int functionality  = 0xC0;   // BuildingFunctionality pointer
    int inventory      = 0xE0;   // Inventory pointer (if has_inventory)
    int townId         = 0x100;  // Town/settlement ID
    int buildProgress  = 0x110;  // Construction progress 0.0-1.0
    int isConstructed  = 0x114;  // Fully constructed flag
};

struct InventoryOffsets {
    int items          = 0x10;   // Item array pointer
    int itemCount      = 0x18;   // Number of items (int)
    int width          = 0x20;   // Grid width
    int height         = 0x24;   // Grid height
    int owner          = 0x28;   // Owner character/building pointer
    int maxStackMult   = 0x30;   // Stackable bonus multiplier
};

struct ItemOffsets {
    int name           = 0x10;   // Item name string
    int templateId     = 0x20;   // GameData template pointer
    int stackCount     = 0x30;   // Number in stack (int)
    int quality        = 0x38;   // Quality level (0-100)
    int value          = 0x40;   // Monetary value (int)
    int weight         = 0x48;   // Weight in kg (float)
    int equipSlot      = 0x50;   // AttachSlot enum
    int condition      = 0x58;   // Item condition / durability (float 0-1)
};

struct FactionOffsets {
    int id             = 0x08;   // Faction ID (used in 6+ files: entity_hooks, faction_hooks, etc.)
    int name           = 0x10;   // Faction name string
    int members        = 0x30;   // Member list pointer
    int memberCount    = 0x38;   // Member count
    int relations      = 0x50;   // Relations map pointer (faction->relation value)
    int color1         = 0x80;   // Primary uniform color (uint32_t ARGB)
    int color2         = 0x84;   // Secondary uniform color
    int isPlayerFaction = 0x90;  // Is this the player's faction
    int money          = 0xA0;   // Faction wealth (int)
};

struct GameDataOffsets {
    int id             = 0x08;   // Template ID (uint32_t)
    int managerPtr     = 0x10;   // GameDataManager* backpointer
    int name           = 0x28;   // Template name (Kenshi std::string)
};

struct TimeManagerOffsets {
    int timeOfDay      = 0x08;   // Float 0.0-1.0 (day cycle)
    int gameSpeed      = 0x10;   // Float (current game speed multiplier)
};

struct StatsOffsets {
    // Core combat stats
    int meleeAttack    = 0x00;   // Melee attack skill (float 0-100)
    int meleeDefence   = 0x04;   // Melee defence skill
    int dodge          = 0x08;   // Dodge skill
    int martialArts    = 0x0C;   // Martial arts skill
    int strength       = 0x10;   // Strength
    int toughness      = 0x14;   // Toughness
    int dexterity      = 0x18;   // Dexterity
    int athletics      = 0x1C;   // Athletics (movement speed)
    // Ranged
    int crossbows      = 0x20;   // Crossbow skill
    int turrets        = 0x24;   // Turret skill
    int precision      = 0x28;   // Precision shooting
    // Stealth
    int stealth        = 0x30;   // Stealth skill
    int assassination  = 0x34;   // Assassination skill
    int lockpicking    = 0x38;   // Lockpicking
    int thievery       = 0x3C;   // Thievery
    // Science/Tech
    int science        = 0x40;   // Science
    int engineering    = 0x44;   // Engineering / robotics
    int medic          = 0x48;   // Field medic
    // Labor
    int farming        = 0x50;   // Farming
    int cooking        = 0x54;   // Cooking
    int weaponsmith    = 0x58;   // Weapon smithing
    int armoursmith    = 0x5C;   // Armour smithing
    int labouring      = 0x60;   // Labouring (mining, hauling)
};

// Combined offsets structure
struct GameOffsets {
    CharacterOffsets    character;
    SquadOffsets        squad;
    WorldOffsets        world;
    BuildingOffsets     building;
    InventoryOffsets    inventory;
    ItemOffsets         item;
    FactionOffsets      faction;
    StatsOffsets        stats;
    GameDataOffsets     gameData;
    TimeManagerOffsets  timeManager;

    // Version string found in memory (for validation)
    char gameVersion[32] = {};

    // Whether offsets were discovered by scanner (vs hardcoded)
    bool discoveredByScanner = false;
};

// Singleton accessor for offsets
GameOffsets& GetOffsets();

// Initialize offsets from scanner results (call early in startup)
void InitOffsetsFromScanner();

// PlayerBase bridge: set by Core after pattern resolution, read by CharacterIterator.
uintptr_t GetResolvedPlayerBase();
void SetResolvedPlayerBase(uintptr_t addr);

// GameWorld bridge: set by Core after pattern resolution, read by CharacterIterator as fallback.
uintptr_t GetResolvedGameWorld();
void SetResolvedGameWorld(uintptr_t addr);

// SetPosition bridge: set by Core with the resolved CharacterSetPosition function ptr.
void SetGameSetPositionFn(void* fn);

// ── Deferred AnimClass Probing ──
// Schedule a character for animClassOffset discovery on subsequent game ticks.
// The probe needs a non-zero cached position to validate the chain, so freshly
// spawned characters may need several frames before the position settles.
void ScheduleDeferredAnimClassProbe(uintptr_t charPtr);

// Process all deferred probes. Call once per game tick from game_tick_hooks.
// Returns true if animClassOffset was successfully discovered.
bool ProcessDeferredAnimClassProbes();

// Reset all probe statics (animClass, equipment, writePos logging, deferred queue).
// Call on disconnect/reconnect or second game load to prevent stale state.
void ResetProbeState();

// ── Player Controlled Offset Discovery ──
// Discovers the isPlayerControlled bool offset by comparing a known player-controlled
// character with an NPC. Call after game load when both types are available.
void ProbePlayerControlledOffset(uintptr_t playerCharPtr, uintptr_t npcCharPtr);

// Write the isPlayerControlled flag on a character (requires discovered offset).
bool WritePlayerControlled(uintptr_t charPtr, bool controlled);

// ═══════════════════════════════════════════════════════════════════════════
//  GAME ENUMS (from fcs_enums.def)
// ═══════════════════════════════════════════════════════════════════════════

// Task types - subset of the 250+ task types relevant to multiplayer
enum class TaskType : uint32_t {
    NULL_TASK = 0,
    MOVE_ON_FREE_WILL = 1,
    BUILD = 2,
    PICKUP = 3,
    MELEE_ATTACK = 4,
    FOCUSED_MELEE_ATTACK = 5,
    EQUIP_WEAPON = 6,
    UNEQUIP_WEAPON = 7,
    CHOOSE_ENEMY_AND_ATTACK = 9,
    IDLE = 13,
    PROTECT_ALLIES = 14,
    ATTACK_ENEMIES = 15,
    PATROL = 21,
    FIRST_AID_ORDER = 26,
    LOOT_TARGET = 27,
    HOLD_POSITION = 31,
    FOLLOW_PLAYER_ORDER = 45,
    OPERATE_MACHINERY = 88,
    USE_TRAINING_DUMMY = 98,
    USE_BED = 99,
    USE_TURRET = 148,
    MAN_A_TURRET = 151,
    RANGED_ATTACK = 278,
    SHOOT_AT_TARGET = 244,
};

// Attach slots for equipment (from fcs_enums.def)
enum class AttachSlot : uint8_t {
    WEAPON    = 0,
    BACK      = 1,
    HAIR      = 2,
    HAT       = 3,
    EYES      = 4,
    BODY      = 5,
    LEGS      = 6,
    NONE      = 7,
    SHIRT     = 8,
    BOOTS     = 9,
    GLOVES    = 10,
    NECK      = 11,
    BACKPACK  = 12,
    BEARD     = 13,
    BELT      = 14,
};

// NPC type classification
enum class CharacterType : uint8_t {
    OT_NONE       = 0,
    OT_MILITARY   = 1,
    OT_CIVILIAN   = 2,
    OT_TRADER     = 3,
    OT_SLAVE      = 4,
    OT_NOBLE      = 5,
    OT_BANDIT     = 6,
    OT_ANIMAL     = 7,
};

// Weather affecting types
enum class WeatherType : uint8_t {
    WA_NONE       = 0,
    WA_RAIN       = 1,
    WA_ACID_RAIN  = 2,
    WA_DUST_STORM = 3,
    WA_GAS        = 4,
};

// Building function types
enum class BuildingFunction : uint8_t {
    BF_ANY           = 0,
    BF_BED           = 1,
    BF_CAGE          = 2,
    BF_STORAGE       = 3,
    BF_CRAFTING      = 4,
    BF_RESEARCH      = 5,
    BF_TURRET        = 6,
    BF_GENERATOR     = 7,
    BF_FARM          = 8,
    BF_MINE          = 9,
    BF_TRAINING      = 10,
};

// ═══════════════════════════════════════════════════════════════════════════
//  CHARACTER ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════
// Safe accessor that reads game memory using the offset table.

class CharacterAccessor {
public:
    explicit CharacterAccessor(void* characterPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(characterPtr)) {}

    bool IsValid() const { return m_ptr != 0; }

    Vec3 GetPosition() const;
    Quat GetRotation() const;
    float GetHealth(BodyPart part) const;
    bool IsAlive() const;
    bool IsPlayerControlled() const;
    float GetMoveSpeed() const;
    uint8_t GetAnimState() const;

    // Read the character's name (MSVC std::string)
    std::string GetName() const;

    // Write the character's name (SSO-safe for names <= 15 chars, heap for longer)
    bool WriteName(const std::string& name);

    // Get the inventory pointer for InventoryAccessor
    uintptr_t GetInventoryPtr() const;

    // Get the faction pointer
    uintptr_t GetFactionPtr() const;

    // Write faction pointer (point character to a different faction)
    bool WriteFaction(uintptr_t factionPtr);

    // Get the GameData* template pointer (backpointer at +0x40)
    uintptr_t GetGameDataPtr() const;

    // Write the character's writable (physics) position.
    bool WritePosition(const Vec3& pos);

    // Get current task type
    TaskType GetCurrentTask() const;

    // Get stats pointer for StatsAccessor
    uintptr_t GetStatsPtr() const;

    // Get equipment slot item pointer
    uintptr_t GetEquipmentSlot(EquipSlot slot) const;

    // Get squad pointer (if character is in a squad)
    uintptr_t GetSquadPtr() const;

    // Get AI package pointer
    uintptr_t GetAIPackagePtr() const;

    // Get money via pointer chain
    int GetMoney() const;

    // Set the isPlayerControlled flag (requires discovered offset from ProbePlayerControlledOffset)
    bool SetPlayerControlled(bool controlled);

    // Get raw pointer for comparison
    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  SQUAD ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════

class SquadAccessor {
public:
    explicit SquadAccessor(void* squadPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(squadPtr)) {}

    bool IsValid() const { return m_ptr != 0; }

    std::string GetName() const;
    int GetMemberCount() const;
    uintptr_t GetMember(int index) const;
    uintptr_t GetFactionPtr() const;
    bool IsPlayerSquad() const;

    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  INVENTORY ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════

class InventoryAccessor {
public:
    explicit InventoryAccessor(void* invPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(invPtr)) {}
    explicit InventoryAccessor(uintptr_t invAddr)
        : m_ptr(invAddr) {}

    bool IsValid() const { return m_ptr != 0; }

    int GetItemCount() const;
    uintptr_t GetItem(int index) const;
    int GetWidth() const;
    int GetHeight() const;

    // Modify quantity of existing item stack (cannot create new items)
    bool AddItem(uint32_t templateId, int quantity);
    bool RemoveItem(uint32_t templateId, int quantity);
    bool SetEquipment(EquipSlot slot, uint32_t templateId);

    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  BUILDING ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════

class BuildingAccessor {
public:
    explicit BuildingAccessor(void* bldPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(bldPtr)) {}

    bool IsValid() const { return m_ptr != 0; }

    std::string GetName() const;
    Vec3 GetPosition() const;
    float GetHealth() const;
    float GetMaxHealth() const;
    bool IsDestroyed() const;
    float GetBuildProgress() const;
    bool IsConstructed() const;
    uintptr_t GetOwnerFaction() const;
    uintptr_t GetInventoryPtr() const;

    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  FACTION ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════

class FactionAccessor {
public:
    explicit FactionAccessor(void* factionPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(factionPtr)) {}

    bool IsValid() const { return m_ptr != 0; }

    std::string GetName() const;
    int GetMemberCount() const;
    bool IsPlayerFaction() const;
    int GetMoney() const;

    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  STATS ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════

class StatsAccessor {
public:
    explicit StatsAccessor(void* statsPtr)
        : m_ptr(reinterpret_cast<uintptr_t>(statsPtr)) {}

    bool IsValid() const { return m_ptr != 0; }

    float GetMeleeAttack() const;
    float GetMeleeDefence() const;
    float GetDodge() const;
    float GetMartialArts() const;
    float GetStrength() const;
    float GetToughness() const;
    float GetDexterity() const;
    float GetAthletics() const;
    float GetStealth() const;
    float GetCrossbows() const;
    float GetMedic() const;

    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ═══════════════════════════════════════════════════════════════════════════
//  CHARACTER LIST ITERATOR
// ═══════════════════════════════════════════════════════════════════════════
// Iterates over all characters in the game world

class CharacterIterator {
public:
    CharacterIterator();

    bool HasNext() const;
    CharacterAccessor Next();
    void Reset();

    int Count() const { return m_count; }

private:
    uintptr_t m_listBase = 0;
    int       m_index    = 0;
    int       m_count    = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  GAME WORLD ACCESSOR
// ═══════════════════════════════════════════════════════════════════════════
// Reads global game state from the GameWorld singleton

class GameWorldAccessor {
public:
    explicit GameWorldAccessor(uintptr_t worldSingletonAddr)
        : m_addr(worldSingletonAddr) {}

    bool IsValid() const { return m_addr != 0; }

    float GetTimeOfDay() const;    // 0.0-1.0 (0=midnight, 0.5=noon)
    float GetGameSpeed() const;    // 1.0=normal
    int GetWeatherState() const;

    // Write time of day directly (for server sync)
    bool WriteTimeOfDay(float time);
    // Write game speed directly (for server sync)
    bool WriteGameSpeed(float speed);

private:
    uintptr_t m_addr;  // Address of the GameWorld singleton pointer
};

// ═══════════════════════════════════════════════════════════════════════════
//  FUNCTION POINTER TYPEDEFS
// ═══════════════════════════════════════════════════════════════════════════
// Signatures for hooked game functions. __fastcall = Microsoft x64 calling convention.

namespace func_types {

// Entity lifecycle
using CharacterCreateFn   = void*(__fastcall*)(void* factory, void* templateData);
using CharacterDestroyFn  = void(__fastcall*)(void* nodeList, void* building);
using CreateRandomSquadFn = void*(__fastcall*)(void* factory, void* squadTemplate);
using CharacterSerialiseFn = void(__fastcall*)(void* character, void* stream);

// Movement
using SetPositionFn       = void(__fastcall*)(void* havokChar, float x, float y, float z);
using MoveToFn            = void(__fastcall*)(void* character, float x, float y, float z, int moveType);

// Combat
using ApplyDamageFn       = void(__fastcall*)(void* target, void* attacker, int bodyPart, float cut, float blunt, float pierce);
using StartAttackFn       = void(__fastcall*)(void* attacker, void* target, void* weapon);
using CharacterDeathFn    = void(__fastcall*)(void* character, void* killer);
using CharacterKOFn       = void(__fastcall*)(void* character, void* attacker, int reason);
using HealthUpdateFn      = void(__fastcall*)(void* character);
using MartialArtsCombatFn = void(__fastcall*)(void* attacker, void* target);

// World / Zones
using ZoneLoadFn          = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using ZoneUnloadFn        = void(__fastcall*)(void* zoneMgr, int zoneX, int zoneY);
using BuildingPlaceFn     = void(__fastcall*)(void* world, void* building, float x, float y, float z);
using BuildingDestroyedFn = void(__fastcall*)(void* building);

// Game loop / Time
using GameFrameUpdateFn   = void(__fastcall*)(void* rcx, void* rdx);
using TimeUpdateFn        = void(__fastcall*)(void* timeManager, float deltaTime);

// Save / Load
using SaveGameFn          = void(__fastcall*)(void* saveManager, const char* saveName);
using LoadGameFn          = void(__fastcall*)(void* saveManager, const char* saveName);
using ImportGameFn        = void(__fastcall*)(void* saveManager, const char* saveName);

// Squad / Platoon
using SquadCreateFn       = void*(__fastcall*)(void* squadManager, void* templateData);
using SquadAddMemberFn    = void(__fastcall*)(void* squad, void* character);

// Inventory / Items
using ItemPickupFn        = void(__fastcall*)(void* inventory, void* item, int quantity);
using ItemDropFn          = void(__fastcall*)(void* inventory, void* item);
using BuyItemFn           = void(__fastcall*)(void* buyer, void* seller, void* item, int quantity);

// Faction / Diplomacy
using FactionRelationFn   = void(__fastcall*)(void* factionA, void* factionB, float relation);

// AI
using AICreateFn          = void*(__fastcall*)(void* character, void* faction);
using AIPackagesFn        = void(__fastcall*)(void* character, void* aiPackage);

// Turret
using GunTurretFn         = void(__fastcall*)(void* turret, void* operator_);
using GunTurretFireFn     = void(__fastcall*)(void* turret, void* target);

} // namespace func_types

} // namespace kmp::game

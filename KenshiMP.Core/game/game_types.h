#pragma once
#include "kmp/types.h"
#include <cstdint>

// Reconstructed Kenshi game class layouts.
// Based on reverse engineering, Cheat Engine pointer chains, and KenshiLib.
// Offsets may vary between game versions; use the pattern scanner to verify.
//
// IMPORTANT: These offsets target Kenshi v1.0.59+.
// Use the pattern scanner to verify at runtime.
// If offsets are -1, the accessor returns safe defaults.

namespace kmp::game {

// Forward declarations
struct KCharacter;
struct KSquad;
struct KGameWorld;
struct KBuilding;
struct KInventory;
struct KStats;

// ── Offset Tables ──
// Filled at runtime by the scanner, or using hardcoded CE-verified fallbacks.

struct CharacterOffsets {
    int name          = -1;    // Offset to name string (MSVC std::string)
    int faction       = -1;    // Offset to faction pointer
    int position      = -1;    // Offset to Vec3 position (3 floats)
    int rotation      = -1;    // Offset to Quat rotation (4 floats)
    int sceneNode     = -1;    // Offset to Ogre::SceneNode*
    int aiPackage     = -1;    // Offset to AI package
    int inventory     = -1;    // Offset to inventory pointer
    int stats         = -1;    // Offset to stats object
    int equipment     = -1;    // Offset to equipment array
    int currentTask   = -1;    // Offset to current task type
    int health        = -1;    // Offset to health array (per body part)
    int isAlive       = -1;    // Offset to alive flag
    int isPlayerControlled = -1;
    int moveSpeed     = -1;    // Offset to current move speed float
    int animState     = -1;    // Offset to animation state index

    // CE-verified health chain: character+2B8 -> +5F8 -> +40 = health[0]
    // Each body part is +8 stride (health + stun per part)
    int healthChain1  = -1;    // First pointer dereference offset (0x2B8)
    int healthChain2  = -1;    // Second pointer dereference offset (0x5F8)
    int healthBase    = -1;    // Final offset to health float (0x40)
    int healthStride  = 8;     // Stride between body parts (health+stun = 2 floats)

    // Writable position chain (from KServerMod RE):
    // character → AnimationClassHuman ptr (+animClassOffset)
    //   → CharMovement ptr (+charMovementOffset from AnimClass)
    //     → writable Vec3 (+writablePosOffset from CharMovement)
    //       → x,y,z floats (+writablePosVecOffset within Vec3 struct)
    // Writing here actually moves the character in the physics engine.
    int animClassOffset      = -1;    // Offset to AnimationClassHuman* on character
    int charMovementOffset   = 0xC0;  // AnimClass → CharMovement* (KServerMod verified)
    int writablePosOffset    = 0x320; // CharMovement → writable position struct (KServerMod verified)
    int writablePosVecOffset = 0x20;  // position struct → x float (KServerMod verified)
};

struct SquadOffsets {
    int name           = -1;
    int memberList     = -1;   // Offset to member pointer array
    int memberCount    = -1;
    int factionId      = -1;
    int isPlayerSquad  = -1;
};

struct WorldOffsets {
    int timeOfDay      = -1;   // float 0-1
    int gameSpeed      = -1;   // float
    int weatherState   = -1;   // int
    int zoneManager    = -1;   // Offset to zone management
    int characterList  = -1;   // Global character list pointer
    int buildingList   = -1;   // Global building list pointer
    int characterCount = -1;   // Count of characters in list
};

// Combined offsets structure
struct GameOffsets {
    CharacterOffsets character;
    SquadOffsets     squad;
    WorldOffsets     world;

    // Version string found in memory (for validation)
    char gameVersion[32] = {};

    // Whether offsets were discovered by scanner (vs hardcoded)
    bool discoveredByScanner = false;
};

// Singleton accessor for offsets — fills with CE fallbacks on first call
GameOffsets& GetOffsets();

// Initialize offsets from scanner results (call early in startup)
void InitOffsetsFromScanner();

// PlayerBase bridge: set by Core after pattern resolution, read by CharacterIterator.
// This avoids circular includes between game_character.cpp and core.h.
uintptr_t GetResolvedPlayerBase();
void SetResolvedPlayerBase(uintptr_t addr);

// SetPosition bridge: set by Core with the resolved CharacterSetPosition function ptr.
void SetGameSetPositionFn(void* fn);

// ── Character Accessor ──
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

    // Get the inventory pointer for InventoryAccessor
    uintptr_t GetInventoryPtr() const;

    // Get the faction pointer
    uintptr_t GetFactionPtr() const;

    // Write the character's writable (physics) position.
    // Uses the AnimationClassHuman → CharMovement → Vector3 chain.
    // Also writes to the cached read-only position at +0x48 as fallback.
    bool WritePosition(const Vec3& pos);

    // Get raw pointer for comparison
    uintptr_t GetPtr() const { return m_ptr; }

private:
    uintptr_t m_ptr;
};

// ── Character List Iterator ──
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

} // namespace kmp::game

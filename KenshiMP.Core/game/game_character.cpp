#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

namespace kmp::game {

static GameOffsets s_offsets;
static bool s_offsetsInitialized = false;

GameOffsets& GetOffsets() {
    if (!s_offsetsInitialized) {
        // Apply CE-verified fallback offsets on first access.
        // These are based on Kenshi v1.0.59+ Cheat Engine tables.
        // The scanner may override these with discovered values later.

        // ── KServerMod / KenshiLib verified offsets (v1.0.68) ──
        auto& c = s_offsets.character;
        // CharacterHuman struct (from KServerMod structs.h)
        c.faction           = 0x10;    // Faction* (verified KServerMod)
        c.name              = 0x18;    // Kenshi std::string (verified KServerMod)
        c.position          = 0x48;    // Vec3 read-only cached position (verified KServerMod)
        c.rotation          = 0x58;    // Quat rotation (after position: 3 floats + pad = 0x10)
        c.sceneNode         = -1;      // Not yet verified
        c.aiPackage         = -1;      // Not yet verified
        c.inventory         = 0x2E8;   // Inventory* (verified KServerMod)
        c.stats             = 0x450;   // Stats base (verified KServerMod)
        c.equipment         = -1;      // Not yet verified
        c.currentTask       = -1;      // Not yet verified
        c.isAlive           = -1;      // Use health check fallback
        c.isPlayerControlled = -1;     // Use squad check fallback
        c.moveSpeed         = -1;      // Not yet verified
        c.animState         = -1;      // Not yet verified

        // CE-verified health chain: char+2B8 -> +5F8 -> +40
        c.healthChain1      = 0x2B8;
        c.healthChain2      = 0x5F8;
        c.healthBase        = 0x40;
        c.healthStride      = 8;       // health(float) + stun(float) per part
        c.health            = -1;      // Use chain instead of direct offset

        auto& sq = s_offsets.squad;
        sq.name             = 0x10;
        sq.memberList       = -1;      // Not yet verified
        sq.memberCount      = -1;      // Not yet verified
        sq.factionId        = -1;      // Not yet verified
        sq.isPlayerSquad    = -1;      // Not yet verified

        // GameWorld offsets (from KenshiLib GameWorld.h)
        auto& w = s_offsets.world;
        w.timeOfDay         = -1;      // Discovered at runtime via GameWorld singleton
        w.gameSpeed         = 0x700;   // GameWorld+0x700 (verified KenshiLib)
        w.weatherState      = -1;      // Not yet verified
        w.zoneManager       = 0x08B0;  // GameWorld+0x08B0 (verified KenshiLib)
        w.characterList     = 0x0888;  // GameWorld+0x0888 characterArray (verified KenshiLib)
        w.buildingList      = -1;      // Not yet verified
        w.characterCount    = -1;      // Derived from list (lektor length at +0x00)

        s_offsetsInitialized = true;
        spdlog::info("GameOffsets: Initialized with CE fallback values");
    }
    return s_offsets;
}

void InitOffsetsFromScanner() {
    // This would be called with values from the re_scanner.py output
    // or from the runtime string scanner's offset discovery.
    // For now, the CE fallbacks in GetOffsets() are used.
    // When the scanner provides JSON, we can parse it here.
    s_offsets.discoveredByScanner = false;
    spdlog::debug("InitOffsetsFromScanner: Using CE fallback offsets");
}

// ── Runtime Offset Discovery ──

// Probe the character struct to find animClassOffset by searching for
// a pointer chain that leads to a position matching the character's cached position.
// Chain: character+X → AnimClass → +charMovementOffset → +writablePosOffset+writablePosVecOffset → Vec3
// This is called lazily on the first WritePosition attempt for a character.
static bool s_animClassProbed = false;
static int  s_discoveredAnimClassOffset = -1;

static void ProbeAnimClassOffset(uintptr_t charPtr) {
    if (s_animClassProbed) return;
    s_animClassProbed = true;

    auto& offsets = GetOffsets().character;

    // Read the character's known cached position for validation
    Vec3 cachedPos;
    if (offsets.position < 0) return;
    Memory::ReadVec3(charPtr + offsets.position, cachedPos.x, cachedPos.y, cachedPos.z);
    if (cachedPos.x == 0.f && cachedPos.y == 0.f && cachedPos.z == 0.f) return;

    // Scan offsets 0x60 through 0x200 in 8-byte steps (pointer alignment)
    // looking for a pointer that leads through the known chain to a matching position.
    for (int probe = 0x60; probe <= 0x200; probe += 8) {
        uintptr_t candidate = 0;
        if (!Memory::Read(charPtr + probe, candidate) || candidate == 0) continue;

        // Validate: candidate should be a valid heap pointer (above 0x10000, below user limit)
        if (candidate < 0x10000 || candidate > 0x00007FFFFFFFFFFF) continue;

        // Follow the chain: candidate → +charMovementOffset → CharMovement
        uintptr_t charMovement = 0;
        if (!Memory::Read(candidate + offsets.charMovementOffset, charMovement) ||
            charMovement == 0) continue;
        if (charMovement < 0x10000 || charMovement > 0x00007FFFFFFFFFFF) continue;

        // Read position at the known writable offset
        uintptr_t posAddr = charMovement + offsets.writablePosOffset + offsets.writablePosVecOffset;
        float px = 0.f, py = 0.f, pz = 0.f;
        if (!Memory::Read(posAddr, px)) continue;
        if (!Memory::Read(posAddr + 4, py)) continue;
        if (!Memory::Read(posAddr + 8, pz)) continue;

        // Check if the position matches the cached position (within tolerance)
        float dx = std::abs(px - cachedPos.x);
        float dy = std::abs(py - cachedPos.y);
        float dz = std::abs(pz - cachedPos.z);

        if (dx < 1.0f && dy < 1.0f && dz < 1.0f) {
            s_discoveredAnimClassOffset = probe;
            offsets.animClassOffset = probe;
            spdlog::info("GameOffsets: Discovered animClassOffset = 0x{:X} via runtime probe", probe);
            return;
        }
    }

    spdlog::debug("GameOffsets: animClassOffset probe failed — Method 2 unavailable");
}

// ── CharacterAccessor ──

Vec3 CharacterAccessor::GetPosition() const {
    Vec3 pos;
    int offset = GetOffsets().character.position;
    if (offset >= 0) {
        Memory::ReadVec3(m_ptr + offset, pos.x, pos.y, pos.z);
    }
    return pos;
}

Quat CharacterAccessor::GetRotation() const {
    Quat rot;
    int offset = GetOffsets().character.rotation;
    if (offset >= 0) {
        // Ogre quaternion layout: w, x, y, z (4 consecutive floats)
        Memory::Read(m_ptr + offset, rot);
    }
    return rot;
}

float CharacterAccessor::GetHealth(BodyPart part) const {
    auto& offsets = GetOffsets().character;

    // Method 1: Direct offset (if scanner found it)
    if (offsets.health >= 0) {
        float health = 0.f;
        Memory::Read(m_ptr + offsets.health + static_cast<int>(part) * sizeof(float), health);
        return health;
    }

    // Method 2: CE pointer chain: char+2B8 -> +5F8 -> +40 + (part * stride)
    if (offsets.healthChain1 >= 0 && offsets.healthChain2 >= 0 && offsets.healthBase >= 0) {
        uintptr_t ptr1 = 0;
        if (!Memory::Read(m_ptr + offsets.healthChain1, ptr1) || ptr1 == 0) return 0.f;

        uintptr_t ptr2 = 0;
        if (!Memory::Read(ptr1 + offsets.healthChain2, ptr2) || ptr2 == 0) return 0.f;

        float health = 0.f;
        int partOffset = offsets.healthBase + static_cast<int>(part) * offsets.healthStride;
        Memory::Read(ptr2 + partOffset, health);
        return health;
    }

    return 0.f;
}

bool CharacterAccessor::IsAlive() const {
    // First check the alive flag if available
    int offset = GetOffsets().character.isAlive;
    if (offset >= 0) {
        bool alive = false;
        Memory::Read(m_ptr + offset, alive);
        return alive;
    }

    // Fallback: check if chest health > -100 (Kenshi KO/death threshold)
    float chestHealth = GetHealth(BodyPart::Chest);
    float headHealth = GetHealth(BodyPart::Head);
    // In Kenshi, death occurs when chest or head health drops to approximately -100
    return chestHealth > -100.f && headHealth > -100.f;
}

bool CharacterAccessor::IsPlayerControlled() const {
    int offset = GetOffsets().character.isPlayerControlled;
    if (offset >= 0) {
        bool controlled = false;
        Memory::Read(m_ptr + offset, controlled);
        return controlled;
    }
    return false;
}

float CharacterAccessor::GetMoveSpeed() const {
    int offset = GetOffsets().character.moveSpeed;
    if (offset < 0) return 0.f;

    float speed = 0.f;
    Memory::Read(m_ptr + offset, speed);
    return speed;
}

uint8_t CharacterAccessor::GetAnimState() const {
    int offset = GetOffsets().character.animState;
    if (offset < 0) return 0;

    uint8_t state = 0;
    Memory::Read(m_ptr + offset, state);
    return state;
}

std::string CharacterAccessor::GetName() const {
    int offset = GetOffsets().character.name;
    if (offset < 0) return "Unknown";

    // MSVC x64 std::string layout:
    // +0x00: buf[16] (small string optimization buffer)
    // +0x10: size (uint64_t)
    // +0x18: capacity (uint64_t)
    // If capacity > 15, buf[0..7] is a pointer to heap-allocated data

    uintptr_t strAddr = m_ptr + offset;
    uint64_t size = 0, capacity = 0;
    Memory::Read(strAddr + 0x10, size);
    Memory::Read(strAddr + 0x18, capacity);

    if (size == 0 || size > 256) return "Unknown";

    char buffer[257] = {};
    if (capacity > 15) {
        // Heap-allocated: first 8 bytes are a pointer to the string data
        uintptr_t dataPtr = 0;
        Memory::Read(strAddr, dataPtr);
        if (dataPtr == 0) return "Unknown";
        for (size_t i = 0; i < size && i < 256; i++) {
            Memory::Read(dataPtr + i, buffer[i]);
        }
    } else {
        // SSO: data is inline in the buffer
        for (size_t i = 0; i < size && i < 256; i++) {
            Memory::Read(strAddr + i, buffer[i]);
        }
    }

    return std::string(buffer, size);
}

uintptr_t CharacterAccessor::GetInventoryPtr() const {
    int offset = GetOffsets().character.inventory;
    if (offset < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offset, ptr);
    return ptr;
}

// Function pointer for HavokCharacter::setPosition (resolved by patterns.cpp)
// Prologue analysis confirms: params~2 (RCX=this, RDX=Vec3*), stack=288
using SetPositionFn = void(__fastcall*)(void* character, const Vec3* pos);
static SetPositionFn s_setPositionFn = nullptr;

void SetGameSetPositionFn(void* fn) {
    s_setPositionFn = reinterpret_cast<SetPositionFn>(fn);
}

bool CharacterAccessor::WritePosition(const Vec3& pos) {
    auto& offsets = GetOffsets().character;

    // Method 1 (best): Call the game's own HavokCharacter::setPosition function.
    // This properly moves the character through the physics engine.
    // Signature: void __fastcall setPosition(this, const Vec3* pos)
    if (s_setPositionFn) {
        s_setPositionFn(reinterpret_cast<void*>(m_ptr), &pos);
        return true;
    }

    // Method 2: Try the writable physics position chain.
    // If animClassOffset hasn't been found yet, probe for it at runtime.
    if (offsets.animClassOffset < 0 && !s_animClassProbed) {
        ProbeAnimClassOffset(m_ptr);
    }
    if (offsets.animClassOffset >= 0) {
        uintptr_t animClass = 0;
        if (Memory::Read(m_ptr + offsets.animClassOffset, animClass) && animClass != 0) {
            uintptr_t charMovement = 0;
            if (Memory::Read(animClass + offsets.charMovementOffset, charMovement) && charMovement != 0) {
                uintptr_t posAddr = charMovement + offsets.writablePosOffset + offsets.writablePosVecOffset;
                Memory::Write(posAddr, pos.x);
                Memory::Write(posAddr + 4, pos.y);
                Memory::Write(posAddr + 8, pos.z);
                return true;
            }
        }
    }

    // Method 3 (fallback): Write to the cached read-only position.
    // This may be overwritten by the physics engine next frame.
    if (offsets.position >= 0) {
        Memory::Write(m_ptr + offsets.position, pos.x);
        Memory::Write(m_ptr + offsets.position + 4, pos.y);
        Memory::Write(m_ptr + offsets.position + 8, pos.z);
    }

    return offsets.position >= 0;
}

uintptr_t CharacterAccessor::GetFactionPtr() const {
    int offset = GetOffsets().character.faction;
    if (offset < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offset, ptr);
    return ptr;
}

// ── CharacterIterator ──

CharacterIterator::CharacterIterator() {
    Reset();
}

void CharacterIterator::Reset() {
    m_index = 0;
    m_count = 0;
    m_listBase = 0;

    // Read PlayerBase from the runtime-resolved address (found by patterns.cpp).
    // This works on both Steam and GOG versions since the address is discovered
    // at runtime via string xref scanning, not hardcoded.
    uintptr_t playerBaseAddr = GetResolvedPlayerBase();
    if (playerBaseAddr == 0) return; // Not resolved yet (scanner hasn't run)

    uintptr_t playerBase = 0;
    if (!Memory::Read(playerBaseAddr, playerBase) || playerBase == 0) {
        return; // Player base not available yet (game still loading)
    }

    // Validate: playerBase must be a valid user-mode pointer
    if (playerBase < 0x10000 || playerBase > 0x00007FFFFFFFFFFF) {
        // Throttle: log once, then every 30 seconds
        static auto s_lastLog = std::chrono::steady_clock::time_point{};
        static bool s_firstLog = true;
        auto now = std::chrono::steady_clock::now();
        if (s_firstLog || std::chrono::duration_cast<std::chrono::seconds>(now - s_lastLog).count() >= 30) {
            spdlog::debug("CharacterIterator: PlayerBase value 0x{:X} is not a valid pointer — game still loading?", playerBase);
            s_lastLog = now;
            s_firstLog = false;
        }
        return;
    }

    // The character list starts at the dereferenced playerBase
    m_listBase = playerBase;

    // Walk the pointer array to count valid entries
    // Each entry is a pointer to a character object, stride = sizeof(uintptr_t)
    // Limit to reasonable count and validate each pointer
    int estimatedCount = 0;
    for (int j = 0; j < 10000; j++) {
        uintptr_t charPtr = 0;
        if (!Memory::Read(m_listBase + j * sizeof(uintptr_t), charPtr) || charPtr == 0) {
            break;
        }
        // Validate each character pointer
        if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) {
            break; // Invalid pointer, end of valid list
        }
        estimatedCount++;
    }
    m_count = estimatedCount;
}

bool CharacterIterator::HasNext() const {
    return m_index < m_count;
}

CharacterAccessor CharacterIterator::Next() {
    if (!HasNext()) return CharacterAccessor(nullptr);

    uintptr_t charPtr = 0;
    Memory::Read(m_listBase + m_index * sizeof(uintptr_t), charPtr);
    m_index++;

    return CharacterAccessor(reinterpret_cast<void*>(charPtr));
}

} // namespace kmp::game

// ── Bridge function to get PlayerBase without circular Core include ──
// Core sets this via the game functions resolver. game_character.cpp reads it.
static uintptr_t s_resolvedPlayerBase = 0;

namespace kmp::game {

uintptr_t GetResolvedPlayerBase() {
    if (s_resolvedPlayerBase != 0) return s_resolvedPlayerBase;

    // Lazy init: read from Core's GameFunctions (only need to do this once)
    // We avoid including core.h by using a one-time read from the patterns resolver.
    // The patterns resolver stores PlayerBase as an address (pointer TO the pointer).
    // Since we can't include core.h here, we use an init function called from core.cpp.
    return s_resolvedPlayerBase;
}

void SetResolvedPlayerBase(uintptr_t addr) {
    s_resolvedPlayerBase = addr;
}

} // namespace kmp::game

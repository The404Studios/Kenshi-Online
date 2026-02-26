#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp::game {

static GameOffsets s_offsets;
static bool s_offsetsInitialized = false;

GameOffsets& GetOffsets() {
    if (!s_offsetsInitialized) {
        // Apply CE-verified fallback offsets on first access.
        // These are based on Kenshi v1.0.59+ Cheat Engine tables.
        // The scanner may override these with discovered values later.

        auto& c = s_offsets.character;
        c.name              = 0x10;    // MSVC std::string
        c.faction           = 0x50;    // Faction pointer
        c.position          = 0x0A0;   // Vec3 (3 floats)
        c.rotation          = 0x0B0;   // Quat (4 floats, w,x,y,z)
        c.sceneNode         = 0x100;   // Ogre::SceneNode*
        c.aiPackage         = 0x1A0;   // AI package pointer
        c.inventory         = 0x200;   // Inventory pointer
        c.stats             = 0x300;   // Stats object pointer
        c.equipment         = 0x380;   // Equipment array
        c.currentTask       = 0x400;   // Current task type
        c.isAlive           = 0x408;   // Alive flag (bool)
        c.isPlayerControlled = 0x410;  // Player-controlled flag
        c.moveSpeed         = 0x0C0;   // Move speed float
        c.animState         = 0x0C8;   // Animation state index

        // CE-verified health chain: char+2B8 -> +5F8 -> +40
        c.healthChain1      = 0x2B8;
        c.healthChain2      = 0x5F8;
        c.healthBase        = 0x40;
        c.healthStride      = 8;       // health(float) + stun(float) per part
        c.health            = -1;      // Use chain instead of direct offset

        auto& sq = s_offsets.squad;
        sq.name             = 0x10;
        sq.memberList       = 0x28;
        sq.memberCount      = 0x30;
        sq.factionId        = 0x38;
        sq.isPlayerSquad    = 0x40;

        auto& w = s_offsets.world;
        w.timeOfDay         = 0x48;
        w.gameSpeed         = 0x50;
        w.weatherState      = 0x58;
        w.zoneManager       = 0x70;
        w.characterList     = 0x60;
        w.buildingList      = 0x68;
        w.characterCount    = -1;      // Derived from list

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

    uintptr_t base = Memory::GetModuleBase();
    auto& offsets = GetOffsets();

    if (offsets.world.characterList >= 0) {
        uintptr_t listPtr = 0;
        // The character list pointer is typically in the GameWorld singleton
        // at a known offset from the player base
        if (Memory::Read(base + offsets.world.characterList, listPtr) && listPtr != 0) {
            m_listBase = listPtr;

            // Try to read count from adjacent memory
            if (offsets.world.characterCount >= 0) {
                Memory::Read(base + offsets.world.characterCount, m_count);
            } else {
                // Heuristic: count is often stored 8 bytes before the array pointer
                // or we can walk the array until we hit a null
                int estimatedCount = 0;
                Memory::Read(listPtr - 8, estimatedCount);
                if (estimatedCount >= 0 && estimatedCount < 10000) {
                    m_count = estimatedCount;
                } else {
                    // Walk array to count non-null entries (expensive but safe)
                    for (int j = 0; j < 10000; j++) {
                        uintptr_t charPtr = 0;
                        if (!Memory::Read(listPtr + j * sizeof(uintptr_t), charPtr) || charPtr == 0) {
                            break;
                        }
                        estimatedCount++;
                    }
                    m_count = estimatedCount;
                }
            }

            if (m_count < 0 || m_count > 10000) m_count = 0;
        }
    }
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

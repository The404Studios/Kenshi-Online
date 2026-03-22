#include "game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>
#include <vector>
#include <mutex>

namespace kmp::game {

static GameOffsets s_offsets;
static bool s_offsetsInitialized = false;

GameOffsets& GetOffsets() {
    if (!s_offsetsInitialized) {
        // All verified offsets are now set as struct defaults in game_types.h.
        // Sources: KServerMod structs.h, KenshiLib GameWorld.h, CE pointer chains.
        // The scanner may override these with runtime-discovered values later.
        //
        // Offset status legend:
        //   Verified values: faction=0x10, name=0x18, position=0x48, rotation=0x58,
        //                    inventory=0x2E8, stats=0x450, gameSpeed=0x700,
        //                    characterList=0x0888, zoneManager=0x08B0
        //   Runtime probed:  equipment, squad, animClassOffset
        //   Chain-based:     health (2B8→5F8→40), money (298→78→88), position write
        //   Unknown (-1):    sceneNode, aiPackage, currentTask, isAlive,
        //                    isPlayerControlled, moveSpeed, animState,
        //                    timeOfDay (on TimeManager not GameWorld), weatherState
        s_offsetsInitialized = true;
        spdlog::info("GameOffsets: Initialized with KServerMod/KenshiLib verified values");
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

    auto& offsets = GetOffsets().character;

    // Read the character's known cached position for validation
    Vec3 cachedPos;
    if (offsets.position < 0) return;
    Memory::ReadVec3(charPtr + offsets.position, cachedPos.x, cachedPos.y, cachedPos.z);
    // Don't set s_animClassProbed if position is (0,0,0) — let another character try
    if (cachedPos.x == 0.f && cachedPos.y == 0.f && cachedPos.z == 0.f) return;

    // Only mark probed AFTER we have a valid position to test against
    s_animClassProbed = true;

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

// SEH wrapper for ProbeAnimClassOffset — character pointers in the deferred queue
// may have been freed by the game engine during zone transitions.  Without SEH,
// an access violation here causes a silent crash.
static bool SEH_ProbeAnimClassOffset(uintptr_t charPtr) {
    __try {
        ProbeAnimClassOffset(charPtr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Probe for equipment array offset.
// Equipment is an array of 14 item pointers (EquipSlot::Count).
// We look for a region between inventory (0x2E8) and stats (0x450) containing
// an array where entries are either 0 (empty slot) or valid heap pointers.
static bool s_equipmentProbed = false;

static void ProbeEquipmentOffset(uintptr_t charPtr) {
    if (s_equipmentProbed) return;
    s_equipmentProbed = true;

    auto& offsets = GetOffsets().character;
    constexpr int SLOT_COUNT = 14;
    constexpr int ARRAY_SIZE = SLOT_COUNT * static_cast<int>(sizeof(uintptr_t)); // 14 * 8 = 112 bytes

    // Scan from after inventory to before stats, 8-byte aligned
    for (int probe = 0x2F0; probe <= 0x440 - ARRAY_SIZE; probe += 8) {
        int validPtrs = 0;
        int nullPtrs = 0;
        bool invalid = false;

        for (int slot = 0; slot < SLOT_COUNT; slot++) {
            uintptr_t itemPtr = 0;
            if (!Memory::Read(charPtr + probe + slot * sizeof(uintptr_t), itemPtr)) {
                invalid = true;
                break;
            }
            if (itemPtr == 0) {
                nullPtrs++;
            } else if (itemPtr > 0x10000 && itemPtr < 0x00007FFFFFFFFFFF) {
                validPtrs++;
            } else {
                invalid = true;
                break;
            }
        }

        if (invalid) continue;

        // A valid equipment array should have: some null + some valid ptrs,
        // and not ALL null (that could be any zeroed memory).
        // At minimum, a character should have at least 1 equipped item (usually body/legs).
        if (validPtrs >= 1 && (validPtrs + nullPtrs == SLOT_COUNT)) {
            offsets.equipment = probe;
            spdlog::info("GameOffsets: Discovered equipment offset = 0x{:X} ({} equipped, {} empty)",
                         probe, validPtrs, nullPtrs);
            return;
        }
    }

    spdlog::debug("GameOffsets: equipment offset probe failed");
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
        Memory::Read(m_ptr + offsets.health + static_cast<int>(part) * offsets.healthStride, health);
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

bool CharacterAccessor::WriteName(const std::string& name) {
    int offset = GetOffsets().character.name;
    if (offset < 0 || name.empty()) return false;

    uintptr_t strAddr = m_ptr + offset;

    // Read current capacity to determine SSO vs heap
    uint64_t currentCapacity = 0;
    Memory::Read(strAddr + 0x18, currentCapacity);

    uint64_t newSize = name.size();

    if (newSize <= 15) {
        // SSO: write directly into the inline buffer (first 16 bytes)
        // Zero the buffer first
        char zeroBuf[16] = {};
        for (int i = 0; i < 16; i++) {
            Memory::Write(strAddr + i, zeroBuf[i]);
        }
        // Write new name
        for (size_t i = 0; i < newSize; i++) {
            Memory::Write(strAddr + i, name[i]);
        }
        // Update size
        Memory::Write(strAddr + 0x10, newSize);
        // Set capacity to 15 (SSO mode)
        uint64_t ssoCapacity = 15;
        Memory::Write(strAddr + 0x18, ssoCapacity);
        return true;
    }

    // For names > 15 chars, we can only overwrite if there's already heap allocation
    // with enough capacity. We don't want to allocate game heap memory from our DLL.
    if (currentCapacity >= newSize) {
        uintptr_t dataPtr = 0;
        Memory::Read(strAddr, dataPtr);
        if (dataPtr == 0 || dataPtr < 0x10000) return false;

        for (size_t i = 0; i < newSize; i++) {
            Memory::Write(dataPtr + i, name[i]);
        }
        // Null-terminate
        char nul = 0;
        Memory::Write(dataPtr + newSize, nul);
        // Update size
        Memory::Write(strAddr + 0x10, newSize);
        return true;
    }

    // Can't fit the name — truncate to 15 chars (SSO)
    std::string truncated = name.substr(0, 15);
    return WriteName(truncated);
}

bool CharacterAccessor::WriteFaction(uintptr_t factionPtr) {
    int offset = GetOffsets().character.faction;
    if (offset < 0) return false;
    // Validate faction pointer: must be heap-allocated, aligned, outside module
    if (factionPtr < 0x10000 || factionPtr >= 0x00007FFFFFFFFFFF || (factionPtr & 0x7) != 0) {
        spdlog::warn("WriteFaction: Rejected invalid faction ptr 0x{:X}", factionPtr);
        return false;
    }
    uintptr_t modBase = Memory::GetModuleBase();
    if (factionPtr >= modBase && factionPtr < modBase + 0x4000000) {
        spdlog::warn("WriteFaction: Rejected in-module faction ptr 0x{:X}", factionPtr);
        return false;
    }
    Memory::Write(m_ptr + offset, factionPtr);
    return true;
}

uintptr_t CharacterAccessor::GetGameDataPtr() const {
    int offset = GetOffsets().character.gameDataPtr;
    if (offset < 0) return 0;

    uintptr_t gdPtr = 0;
    Memory::Read(m_ptr + offset, gdPtr);
    if (gdPtr < 0x10000 || gdPtr >= 0x00007FFFFFFFFFFF || (gdPtr & 0x7) != 0) return 0;
    // GameData is heap-allocated — reject if inside module image
    uintptr_t modBase = Memory::GetModuleBase();
    if (gdPtr >= modBase && gdPtr < modBase + 0x4000000) return 0;
    return gdPtr;
}

uintptr_t CharacterAccessor::GetInventoryPtr() const {
    int offset = GetOffsets().character.inventory;
    if (offset < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offset, ptr);
    if (ptr < 0x10000 || ptr >= 0x00007FFFFFFFFFFF || (ptr & 0x7) != 0) return 0;
    return ptr;
}

// Function pointer for HavokCharacter::setPosition (resolved by patterns.cpp)
// Prologue analysis confirms: params~2 (RCX=this, RDX=Vec3*), stack=288
using SetPositionFn = void(__fastcall*)(void* character, const Vec3* pos);
static SetPositionFn s_setPositionFn = nullptr;

void SetGameSetPositionFn(void* fn) {
    s_setPositionFn = reinterpret_cast<SetPositionFn>(fn);
}

// Track WritePosition method transitions: log when method changes, not just first call.
// This ensures we see if characters silently fall back to worse methods.
static int s_lastWritePosMethod = 0;  // 0=none, 1=setPositionFn, 2=physicsChain, 3=cached
static int s_writePosMethodCount = 0; // Total calls since last method change

bool CharacterAccessor::WritePosition(const Vec3& pos) {
    auto& offsets = GetOffsets().character;

    // Method 1 (best): Call the game's own HavokCharacter::setPosition function.
    // This properly moves the character through the physics engine.
    // Signature: void __fastcall setPosition(this, const Vec3* pos)
    if (s_setPositionFn) {
        if (s_lastWritePosMethod != 1) {
            spdlog::info("WritePosition: Using Method 1 (setPosition fn) at 0x{:X} for char 0x{:X} (prev method={})",
                         reinterpret_cast<uintptr_t>(s_setPositionFn), m_ptr, s_lastWritePosMethod);
            s_lastWritePosMethod = 1;
            s_writePosMethodCount = 0;
        }
        s_writePosMethodCount++;
        s_setPositionFn(reinterpret_cast<void*>(m_ptr), &pos);
        return true;
    }

    // Method 2: Try the writable physics position chain.
    // Eagerly probe on first access for ANY character, not just this one.
    if (offsets.animClassOffset < 0 && !s_animClassProbed) {
        spdlog::info("WritePosition: Method 1 unavailable (no setPosition fn), probing physics chain...");
        ProbeAnimClassOffset(m_ptr);
    }
    if (offsets.animClassOffset >= 0) {
        uintptr_t animClass = 0;
        if (Memory::Read(m_ptr + offsets.animClassOffset, animClass) && animClass != 0 &&
            animClass > 0x10000 && animClass < 0x00007FFFFFFFFFFF && (animClass & 0x7) == 0) {
            uintptr_t charMovement = 0;
            if (Memory::Read(animClass + offsets.charMovementOffset, charMovement) && charMovement != 0 &&
                charMovement > 0x10000 && charMovement < 0x00007FFFFFFFFFFF && (charMovement & 0x7) == 0) {
                uintptr_t posAddr = charMovement + offsets.writablePosOffset + offsets.writablePosVecOffset;
                Memory::Write(posAddr, pos.x);
                Memory::Write(posAddr + 4, pos.y);
                Memory::Write(posAddr + 8, pos.z);
                if (s_lastWritePosMethod != 2) {
                    spdlog::info("WritePosition: Using Method 2 (physics chain) animClass=0x{:X} for char 0x{:X} (prev method={})",
                                 offsets.animClassOffset, m_ptr, s_lastWritePosMethod);
                    s_lastWritePosMethod = 2;
                    s_writePosMethodCount = 0;
                }
                s_writePosMethodCount++;
                return true;
            }
        }
    }

    // Method 3 (fallback): Write to the cached read-only position.
    // This may be overwritten by the physics engine next frame, but for remote
    // characters that are continuously updated it's acceptable.
    if (offsets.position >= 0) {
        Memory::Write(m_ptr + offsets.position, pos.x);
        Memory::Write(m_ptr + offsets.position + 4, pos.y);
        Memory::Write(m_ptr + offsets.position + 8, pos.z);
        if (s_lastWritePosMethod != 3) {
            spdlog::warn("WritePosition: Using Method 3 (cached position fallback) for char 0x{:X} "
                         "— position may drift due to physics engine overwrite (prev method={})",
                         m_ptr, s_lastWritePosMethod);
            s_lastWritePosMethod = 3;
            s_writePosMethodCount = 0;
        }
        s_writePosMethodCount++;
    } else if (s_lastWritePosMethod != -1) {
        spdlog::error("WritePosition: ALL methods failed for char 0x{:X} — "
                      "no setPosition fn, no physics chain, no cached position offset", m_ptr);
        s_lastWritePosMethod = -1;
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

TaskType CharacterAccessor::GetCurrentTask() const {
    int offset = GetOffsets().character.currentTask;
    if (offset < 0) return TaskType::NULL_TASK;

    uint32_t task = 0;
    Memory::Read(m_ptr + offset, task);
    return static_cast<TaskType>(task);
}

uintptr_t CharacterAccessor::GetStatsPtr() const {
    int offset = GetOffsets().character.stats;
    if (offset < 0) return 0;

    // Stats are stored inline in the character (not a pointer),
    // so return character address + offset
    return m_ptr + offset;
}

uintptr_t CharacterAccessor::GetEquipmentSlot(EquipSlot slot) const {
    int offset = GetOffsets().character.equipment;
    if (offset < 0) {
        // Try runtime probe on first access
        ProbeEquipmentOffset(m_ptr);
        offset = GetOffsets().character.equipment;
        if (offset < 0) return 0;
    }

    int slotIndex = static_cast<int>(slot);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(EquipSlot::Count)) return 0;

    uintptr_t itemPtr = 0;
    Memory::Read(m_ptr + offset + slotIndex * sizeof(uintptr_t), itemPtr);
    return itemPtr;
}

uintptr_t CharacterAccessor::GetSquadPtr() const {
    auto& offsets = GetOffsets().character;

    // If squad offset already discovered, use it directly
    if (offsets.squad >= 0) {
        uintptr_t ptr = 0;
        Memory::Read(m_ptr + offsets.squad, ptr);
        return (ptr > 0x10000 && ptr < 0x00007FFFFFFFFFFF) ? ptr : 0;
    }

    // Heuristic probe: scan pointers near faction offset (0x10) for a KSquad*.
    // In Kenshi, squad pointers are typically at +0x08 or within the first 0x40 bytes.
    // A valid KSquad* will have a name string at squad+0x10.
    uintptr_t factionPtr = GetFactionPtr();
    if (factionPtr == 0) return 0; // Can't validate without faction context

    // Check common squad offsets: 0x08, 0x20, 0x28, 0x30, 0x38
    static const int candidateOffsets[] = { 0x08, 0x20, 0x28, 0x30, 0x38 };
    for (int off : candidateOffsets) {
        uintptr_t candidate = 0;
        if (!Memory::Read(m_ptr + off, candidate)) continue;
        if (candidate < 0x10000 || candidate > 0x00007FFFFFFFFFFF || (candidate & 0x7) != 0) continue;
        if (candidate == factionPtr) continue; // Skip the faction pointer itself
        // Overflow guard: candidate + 0x30 must not wrap
        if (candidate > 0x00007FFFFFFFFFE0ULL) continue;

        // Validate: a KSquad should have a readable name at +0x10
        // MSVC std::string layout: if size <= 15 (SSO), chars at +0x00; else ptr at +0x00
        // Check if squad+0x10 looks like a valid std::string (capacity field at +0x18 should be small)
        uint64_t capacity = 0;
        if (!Memory::Read(candidate + 0x10 + 0x18, capacity)) continue;
        if (capacity < 1 || capacity > 256) continue; // Reasonable string capacity

        uint64_t size = 0;
        if (!Memory::Read(candidate + 0x10 + 0x10, size)) continue;
        if (size > capacity || size > 64) continue; // Name shouldn't be > 64 chars

        // Looks like a valid squad pointer — cache the offset
        GetOffsets().character.squad = off;
        spdlog::info("CharacterAccessor: Discovered squad offset at +0x{:X} (squad=0x{:X})",
                     off, candidate);
        return candidate;
    }

    return 0;
}

uintptr_t CharacterAccessor::GetAIPackagePtr() const {
    int offset = GetOffsets().character.aiPackage;
    if (offset < 0) return 0;

    uintptr_t ptr = 0;
    Memory::Read(m_ptr + offset, ptr);
    if (ptr < 0x10000 || ptr >= 0x00007FFFFFFFFFFF || (ptr & 0x7) != 0) return 0;
    return ptr;
}

int CharacterAccessor::GetMoney() const {
    auto& offsets = GetOffsets().character;

    // Money via pointer chain: char+0x298 -> +0x78 -> +0x88
    if (offsets.moneyChain1 >= 0 && offsets.moneyChain2 >= 0 && offsets.moneyBase >= 0) {
        uintptr_t ptr1 = 0;
        if (!Memory::Read(m_ptr + offsets.moneyChain1, ptr1) || ptr1 == 0) return 0;

        uintptr_t ptr2 = 0;
        if (!Memory::Read(ptr1 + offsets.moneyChain2, ptr2) || ptr2 == 0) return 0;

        int money = 0;
        Memory::Read(ptr2 + offsets.moneyBase, money);
        return money;
    }

    return 0;
}

// ── CharacterIterator ──

CharacterIterator::CharacterIterator() {
    Reset();
}

void CharacterIterator::Reset() {
    m_index = 0;
    m_count = 0;
    m_listBase = 0;

    // Get module range for heap-vs-module discrimination
    uintptr_t modBase = Memory::GetModuleBase();
    size_t modSize = 0x4000000; // 64MB fallback
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(modBase);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(modBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                modSize = nt->OptionalHeader.SizeOfImage;
        }
    }

    auto isValidHeapPtr = [modBase, modSize](uintptr_t val) -> bool {
        if (val < 0x10000 || val >= 0x00007FFFFFFFFFFF) return false;
        // Must be 8-byte aligned (x64 heap allocations are at least 16-byte aligned)
        if ((val & 0x7) != 0) return false;
        // Must be outside module image — game objects are heap-allocated
        if (val >= modBase && val < modBase + modSize) return false;
        return true;
    };

    // ── Strategy 1: PlayerBase ──
    // Read PlayerBase from the runtime-resolved address (found by patterns.cpp).
    uintptr_t playerBaseAddr = GetResolvedPlayerBase();
    if (playerBaseAddr != 0) {
        uintptr_t playerBase = 0;
        if (Memory::Read(playerBaseAddr, playerBase) && isValidHeapPtr(playerBase)) {
            m_listBase = playerBase;
        }
    }

    // If PlayerBase gave a pointer, validate it by walking the array.
    // If the first entry is 0 or invalid, PlayerBase isn't a character array —
    // clear m_listBase so we fall through to the GameWorld path.
    if (m_listBase != 0) {
        uintptr_t firstEntry = 0;
        if (!Memory::Read(m_listBase, firstEntry) || !isValidHeapPtr(firstEntry)) {
            spdlog::debug("CharacterIterator: PlayerBase dereference 0x{:X} has no valid entries — trying GameWorld",
                         m_listBase);
            m_listBase = 0;
        }
    }

    // ── Strategy 2: GameWorld + characterList fallback ──
    // If PlayerBase failed or yielded no characters, try GameWorld + 0x0888 characterList.
    // The characterList at GameWorld+0x0888 is a lektor (dynamic array).
    if (m_listBase == 0) {
        uintptr_t gameWorldAddr = GetResolvedGameWorld();
        if (gameWorldAddr != 0) {
            const auto& offsets = GetOffsets();
            int charListOff = offsets.world.characterList; // 0x0888

            // GameWorldSingleton might be:
            // (a) The address of a pointer TO GameWorld — dereference once
            // (b) The address of GameWorld directly (static object)
            // Try (a) first: dereference to get GameWorld*
            uintptr_t gameWorld = 0;
            if (Memory::Read(gameWorldAddr, gameWorld) && isValidHeapPtr(gameWorld)) {
                // Got a valid pointer — this is case (a)
                // Read lektor at gameWorld + characterList offset
                // Try lektor format: +0x00 = pointer array, count derived by walking
                uintptr_t lektorBase = gameWorld + charListOff;
                uintptr_t arrayPtr = 0;
                int lektorCount = 0;

                // Lektor format 1: count at +0x00, array at +0x08
                Memory::Read(lektorBase, lektorCount);
                Memory::Read(lektorBase + 0x08, arrayPtr);
                if (lektorCount > 0 && lektorCount < 10000 && isValidHeapPtr(arrayPtr)) {
                    m_listBase = arrayPtr;
                    m_count = lektorCount;
                    spdlog::info("CharacterIterator: Using GameWorld lektor (format1) — {} characters at 0x{:X}",
                                 m_count, m_listBase);
                    return;
                }

                // Lektor format 2: array at +0x00, count at +0x08
                Memory::Read(lektorBase, arrayPtr);
                Memory::Read(lektorBase + 0x08, lektorCount);
                if (lektorCount > 0 && lektorCount < 10000 && isValidHeapPtr(arrayPtr)) {
                    m_listBase = arrayPtr;
                    m_count = lektorCount;
                    spdlog::info("CharacterIterator: Using GameWorld lektor (format2) — {} characters at 0x{:X}",
                                 m_count, m_listBase);
                    return;
                }
            }

            // Try (b): gameWorldAddr IS GameWorld directly (static object)
            uintptr_t lektorBase = gameWorldAddr + charListOff;
            uintptr_t arrayPtr = 0;
            int lektorCount = 0;

            // Lektor format 1
            Memory::Read(lektorBase, lektorCount);
            Memory::Read(lektorBase + 0x08, arrayPtr);
            if (lektorCount > 0 && lektorCount < 10000 && isValidHeapPtr(arrayPtr)) {
                m_listBase = arrayPtr;
                m_count = lektorCount;
                spdlog::info("CharacterIterator: Using GameWorld-direct lektor (format1) — {} characters at 0x{:X}",
                             m_count, m_listBase);
                return;
            }

            // Lektor format 2
            Memory::Read(lektorBase, arrayPtr);
            Memory::Read(lektorBase + 0x08, lektorCount);
            if (lektorCount > 0 && lektorCount < 10000 && isValidHeapPtr(arrayPtr)) {
                m_listBase = arrayPtr;
                m_count = lektorCount;
                spdlog::info("CharacterIterator: Using GameWorld-direct lektor (format2) — {} characters at 0x{:X}",
                             m_count, m_listBase);
                return;
            }

            static bool s_loggedFail = false;
            if (!s_loggedFail) {
                spdlog::warn("CharacterIterator: GameWorld fallback failed — "
                             "addr=0x{:X}, charListOff=0x{:X}", gameWorldAddr, charListOff);
                s_loggedFail = true;
            }
        }
    }

    if (m_listBase == 0) {
        static bool s_loggedNone = false;
        if (!s_loggedNone) {
            spdlog::warn("CharacterIterator: No character list source available (PlayerBase and GameWorld both failed)");
            s_loggedNone = true;
        }
        return;
    }

    // Walk the pointer array to count valid entries (only needed for PlayerBase path)
    if (m_count == 0) {
        int estimatedCount = 0;
        for (int j = 0; j < 10000; j++) {
            uintptr_t charPtr = 0;
            if (!Memory::Read(m_listBase + j * sizeof(uintptr_t), charPtr) || charPtr == 0) {
                break;
            }
            if (!isValidHeapPtr(charPtr)) {
                break;
            }
            estimatedCount++;
        }
        m_count = estimatedCount;
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

    // Validate: must be non-zero, 8-byte aligned, in user-space
    if (charPtr == 0 || (charPtr & 0x7) != 0 || charPtr >= 0x00007FFFFFFFFFFF)
        return CharacterAccessor(nullptr);

    // Vtable check: first 8 bytes must point into the module (game class vtable in .rdata)
    uintptr_t vtable = 0;
    if (!Memory::Read(charPtr, vtable))
        return CharacterAccessor(nullptr);
    uintptr_t modBase = Memory::GetModuleBase();
    // Accept vtable within module image (typically in .rdata section)
    if (vtable < modBase || vtable >= modBase + 0x4000000)
        return CharacterAccessor(nullptr);

    return CharacterAccessor(reinterpret_cast<void*>(charPtr));
}

// ── SetPlayerControlled ──
bool CharacterAccessor::SetPlayerControlled(bool controlled) {
    int offset = GetOffsets().character.isPlayerControlled;
    if (offset < 0) return false;
    uint8_t val = controlled ? 1 : 0;
    Memory::Write(m_ptr + offset, val);
    return true;
}

} // namespace kmp::game

// ── Deferred AnimClass Probing ──
// Characters spawned via in-place replay may not have a settled position on the
// first frame. We queue them and re-probe each game tick until the animClassOffset
// is discovered or the queue is exhausted.
static std::vector<uintptr_t> s_deferredProbeChars;
static std::mutex s_deferredProbeMutex;
static int s_deferredTotalAttempts = 0;

namespace kmp::game {

void ScheduleDeferredAnimClassProbe(uintptr_t charPtr) {
    std::lock_guard lock(s_deferredProbeMutex);
    // Avoid duplicates
    for (auto c : s_deferredProbeChars) {
        if (c == charPtr) return;
    }
    s_deferredProbeChars.push_back(charPtr);
    spdlog::info("game_character: Scheduled deferred AnimClass probe for char 0x{:X} ({} queued)",
                 charPtr, s_deferredProbeChars.size());
}

bool ProcessDeferredAnimClassProbes() {
    // Already discovered — no need to probe
    if (GetOffsets().character.animClassOffset >= 0) {
        std::lock_guard lock(s_deferredProbeMutex);
        s_deferredProbeChars.clear();
        return true;
    }

    std::lock_guard lock(s_deferredProbeMutex);
    if (s_deferredProbeChars.empty()) return false;

    // Try each queued character — stop as soon as one succeeds
    for (auto it = s_deferredProbeChars.begin(); it != s_deferredProbeChars.end(); ) {
        uintptr_t charPtr = *it;

        // Validate pointer still looks sane
        if (charPtr < 0x10000 || charPtr > 0x00007FFFFFFFFFFF) {
            it = s_deferredProbeChars.erase(it);
            continue;
        }

        // Reset the probed flag so ProbeAnimClassOffset will actually try
        s_animClassProbed = false;
        if (!SEH_ProbeAnimClassOffset(charPtr)) {
            spdlog::warn("game_character: Deferred AnimClass probe CRASHED on char 0x{:X} "
                         "— pointer likely freed, removing from queue", charPtr);
            it = s_deferredProbeChars.erase(it);
            continue;
        }

        if (GetOffsets().character.animClassOffset >= 0) {
            spdlog::info("game_character: Deferred AnimClass probe SUCCEEDED on char 0x{:X} "
                         "— animClassOffset=0x{:X}",
                         charPtr, GetOffsets().character.animClassOffset);
            s_deferredProbeChars.clear();
            return true;
        }

        ++it;
    }

    // Prune queue after 50 failed attempts total
    s_deferredTotalAttempts++;
    if (s_deferredTotalAttempts > 50) {
        spdlog::warn("game_character: Deferred AnimClass probe exhausted (50 ticks, {} chars) — giving up",
                     s_deferredProbeChars.size());
        s_deferredProbeChars.clear();
    }

    return false;
}

// ── Reset all probe state for reconnect or second game load ──
// Without this, statics like s_animClassProbed, s_deferredTotalAttempts, s_writePosLogged
// persist across game loads, causing probes to never fire on second load.
void ResetProbeState() {
    s_animClassProbed = false;
    s_discoveredAnimClassOffset = -1;
    s_equipmentProbed = false;
    s_lastWritePosMethod = 0;
    s_writePosMethodCount = 0;
    s_deferredTotalAttempts = 0;
    {
        std::lock_guard lock(s_deferredProbeMutex);
        s_deferredProbeChars.clear();
    }
    spdlog::info("game_character: ResetProbeState — all probe statics cleared");
}

// ── Player Controlled Offset Discovery ──
// Exploits the fact that the local player's primary character has isPlayerControlled=true
// while NPCs have it as false. Scans a range of offsets looking for this distinguishing byte.
static int s_discoveredPlayerControlledOffset = -1;

void ProbePlayerControlledOffset(uintptr_t playerCharPtr, uintptr_t npcCharPtr) {
    if (s_discoveredPlayerControlledOffset >= 0) return; // Already found
    if (playerCharPtr == 0 || npcCharPtr == 0) return;

    // Scan offsets between 0x100 and 0x500 (well past known fields, before stats at 0x450)
    // Looking for a byte that is 1 on the player char and 0 on the NPC
    for (int off = 0x100; off <= 0x500; off += 1) {
        uint8_t playerVal = 0, npcVal = 0;
        if (!Memory::Read(playerCharPtr + off, playerVal)) continue;
        if (!Memory::Read(npcCharPtr + off, npcVal)) continue;

        if (playerVal == 1 && npcVal == 0) {
            // Cross-validate: check bytes around this offset aren't part of a larger value
            // A true bool flag should have neighboring bytes that are NOT consistently 0/1
            uint8_t playerPrev = 0, playerNext = 0;
            Memory::Read(playerCharPtr + off - 1, playerPrev);
            Memory::Read(playerCharPtr + off + 1, playerNext);

            // Skip if this looks like part of a multi-byte value (e.g., 0x00000001 as uint32)
            if (playerPrev == 0 && playerNext == 0) {
                // Could be a standalone bool — check that the NPC also has 0s around it
                uint8_t npcPrev = 0, npcNext = 0;
                Memory::Read(npcCharPtr + off - 1, npcPrev);
                Memory::Read(npcCharPtr + off + 1, npcNext);

                // Both have 0-padded neighbors — likely a bool field
                if (npcPrev == 0 && npcNext == 0) {
                    s_discoveredPlayerControlledOffset = off;
                    GetOffsets().character.isPlayerControlled = off;
                    spdlog::info("game_character: Discovered isPlayerControlled offset = 0x{:X} "
                                 "(player=1, npc=0)", off);
                    return;
                }
            }
        }
    }

    spdlog::debug("game_character: isPlayerControlled probe failed — "
                  "no distinguishing byte found between player 0x{:X} and npc 0x{:X}",
                  playerCharPtr, npcCharPtr);
}

bool WritePlayerControlled(uintptr_t charPtr, bool controlled) {
    int off = GetOffsets().character.isPlayerControlled;
    if (off < 0) return false;
    uint8_t val = controlled ? 1 : 0;
    Memory::Write(charPtr + off, val);
    spdlog::info("game_character: Set isPlayerControlled={} on char 0x{:X} (offset 0x{:X})",
                 controlled, charPtr, off);
    return true;
}

} // namespace kmp::game

// ── Bridge functions to avoid circular Core include ──
// Core sets these via the game functions resolver. game_character.cpp reads them.
static uintptr_t s_resolvedPlayerBase = 0;
static uintptr_t s_resolvedGameWorld = 0;

namespace kmp::game {

uintptr_t GetResolvedPlayerBase() {
    return s_resolvedPlayerBase;
}

void SetResolvedPlayerBase(uintptr_t addr) {
    s_resolvedPlayerBase = addr;
}

uintptr_t GetResolvedGameWorld() {
    return s_resolvedGameWorld;
}

void SetResolvedGameWorld(uintptr_t addr) {
    s_resolvedGameWorld = addr;
    spdlog::info("game_character: GameWorld bridge set to 0x{:X}", addr);
}

} // namespace kmp::game

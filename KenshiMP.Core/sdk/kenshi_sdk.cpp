#include "kenshi_sdk.h"
#include "../game/game_types.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

namespace kmp::sdk {

// ═══════════════════════════════════════════════════════════════════════════
//  ENTITY SNAPSHOT DIFF
// ═══════════════════════════════════════════════════════════════════════════

uint16_t EntitySnapshot::DiffAgainst(const EntitySnapshot& prev) const {
    uint16_t flags = Dirty_None;

    // Position: threshold 0.1 units (same as KMP_POS_CHANGE_THRESHOLD)
    float dx = position.x - prev.position.x;
    float dy = position.y - prev.position.y;
    float dz = position.z - prev.position.z;
    if (dx * dx + dy * dy + dz * dz > 0.01f)
        flags |= Dirty_Position;

    // Rotation: threshold 0.01 (same as KMP_ROT_CHANGE_THRESHOLD)
    float dw = rotation.w - prev.rotation.w;
    float drx = rotation.x - prev.rotation.x;
    float dry = rotation.y - prev.rotation.y;
    float drz = rotation.z - prev.rotation.z;
    if (dw * dw + drx * drx + dry * dry + drz * drz > 0.0001f)
        flags |= Dirty_Rotation;

    // Animation state
    if (animState != prev.animState)
        flags |= Dirty_Animation;

    // Health: any body part changed by > 0.5
    for (int i = 0; i < 7; i++) {
        if (std::abs(health[i] - prev.health[i]) > 0.5f) {
            flags |= Dirty_Health;
            break;
        }
    }

    // Alive state
    if (alive != prev.alive)
        flags |= Dirty_Health;

    return flags;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WORLD SNAPSHOT
// ═══════════════════════════════════════════════════════════════════════════

const EntitySnapshot* WorldSnapshot::FindByPtr(uintptr_t ptr) const {
    for (const auto& e : entities) {
        if (e.gamePtr == ptr) return &e;
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SDK IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

bool KenshiSDK::Initialize() {
    // Verify we have the globals needed for entity enumeration
    uintptr_t playerBase = game::GetResolvedPlayerBase();
    uintptr_t gameWorld = game::GetResolvedGameWorld();

    if (playerBase == 0 && gameWorld == 0) {
        spdlog::warn("KenshiSDK: Cannot initialize — no PlayerBase or GameWorld resolved");
        return false;
    }

    m_initialized = true;
    m_frameNumber = 0;
    spdlog::info("KenshiSDK: Initialized (PlayerBase=0x{:X}, GameWorld=0x{:X})",
                 playerBase, gameWorld);
    return true;
}

void KenshiSDK::Update() {
    if (!m_initialized) return;

    auto start = std::chrono::steady_clock::now();

    // Swap previous/current
    {
        std::lock_guard lock(m_mutex);
        m_previous = std::move(m_current);
    }

    // Poll fresh state
    WorldSnapshot newSnap;
    newSnap.frameNumber = ++m_frameNumber;
    PollEntities(newSnap);

    // Compute diff
    WorldDiff diff = ComputeDiff(m_previous, newSnap);

    // Store results
    {
        std::lock_guard lock(m_mutex);
        m_current = std::move(newSnap);
        m_lastDiff = std::move(diff);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    m_lastPollTimeMs = std::chrono::duration<float, std::milli>(elapsed).count();
}

WorldSnapshot KenshiSDK::GetCurrentSnapshot() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

WorldDiff KenshiSDK::GetLastDiff() const {
    std::lock_guard lock(m_mutex);
    return m_lastDiff;
}

bool KenshiSDK::GetEntityState(uintptr_t gamePtr, EntitySnapshot& out) const {
    std::lock_guard lock(m_mutex);
    const EntitySnapshot* found = m_current.FindByPtr(gamePtr);
    if (!found) return false;
    out = *found;
    return true;
}

std::vector<uintptr_t> KenshiSDK::GetAllEntityPtrs() const {
    std::lock_guard lock(m_mutex);
    std::vector<uintptr_t> ptrs;
    ptrs.reserve(m_current.entities.size());
    for (const auto& e : m_current.entities) {
        ptrs.push_back(e.gamePtr);
    }
    return ptrs;
}

size_t KenshiSDK::GetEntityCount() const {
    std::lock_guard lock(m_mutex);
    return m_current.entities.size();
}

// ── State Write ──

bool KenshiSDK::WritePosition(uintptr_t gamePtr, const Vec3& pos) {
    if (gamePtr == 0) return false;
    game::CharacterAccessor accessor(reinterpret_cast<void*>(gamePtr));
    return accessor.WritePosition(pos);
}

bool KenshiSDK::WriteHealth(uintptr_t gamePtr, BodyPart part, float value) {
    if (gamePtr == 0) return false;

    auto& offsets = game::GetOffsets().character;

    // Use the CE-verified health chain: char+0x2B8 → +0x5F8 → +0x40+(part*8)
    uintptr_t ptr1 = 0;
    if (!Memory::Read(gamePtr + offsets.healthChain1, ptr1) || ptr1 == 0)
        return false;

    uintptr_t ptr2 = 0;
    if (!Memory::Read(ptr1 + offsets.healthChain2, ptr2) || ptr2 == 0)
        return false;

    int partOffset = offsets.healthBase +
        static_cast<int>(part) * offsets.healthStride;
    return Memory::Write(ptr2 + partOffset, value);
}

bool KenshiSDK::WriteName(uintptr_t gamePtr, const std::string& name) {
    if (gamePtr == 0) return false;
    game::CharacterAccessor accessor(reinterpret_cast<void*>(gamePtr));
    return accessor.WriteName(name);
}

// ── Polling ──

// SEH filter — no C++ objects with destructors allowed in __try functions.
// We only wrap the raw pointer read that could fault, not the C++ containers.
static int SEH_ReadCharPtr(uintptr_t listBase, int index, uintptr_t* outPtr) {
    __try {
        uintptr_t elemAddr = listBase + static_cast<uintptr_t>(index) * 8;
        *outPtr = *reinterpret_cast<uintptr_t*>(elemAddr);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outPtr = 0;
        return 1;
    }
}

void KenshiSDK::PollEntities(WorldSnapshot& snapshot) {
    // Use CharacterIterator (C++ friendly, no SEH needed at this level)
    game::CharacterIterator iter;
    int count = iter.Count();
    snapshot.entities.reserve(static_cast<size_t>(count));

    while (iter.HasNext()) {
        game::CharacterAccessor accessor = iter.Next();
        if (!accessor.IsValid()) continue;

        EntitySnapshot entity = ReadEntity(accessor.GetPtr());
        if (entity.gamePtr != 0) {
            snapshot.entities.push_back(std::move(entity));
        }
    }

    // Read world state
    uintptr_t gameWorld = game::GetResolvedGameWorld();
    if (gameWorld != 0) {
        game::GameWorldAccessor world(gameWorld);
        snapshot.timeOfDay = world.GetTimeOfDay();
        snapshot.gameSpeed = world.GetGameSpeed();
    }
}

EntitySnapshot KenshiSDK::ReadEntity(uintptr_t charPtr) const {
    EntitySnapshot snap;
    snap.gamePtr = charPtr;

    game::CharacterAccessor accessor(reinterpret_cast<void*>(charPtr));
    if (!accessor.IsValid()) return snap;

    snap.position = accessor.GetPosition();
    snap.rotation = accessor.GetRotation();
    snap.factionPtr = accessor.GetFactionPtr();
    snap.name = accessor.GetName();
    snap.alive = accessor.IsAlive();
    snap.animState = accessor.GetAnimState();
    snap.moveSpeed = accessor.GetMoveSpeed();

    // Read faction ID from faction pointer
    {
        const int fIdOff = game::GetOffsets().faction.id;
        if (snap.factionPtr != 0 && fIdOff >= 0) {
            Memory::Read(snap.factionPtr + fIdOff, snap.factionId);
        }
    }

    // Read all 7 body part health values
    for (int i = 0; i < 7; i++) {
        snap.health[i] = accessor.GetHealth(static_cast<BodyPart>(i));
    }

    return snap;
}

// ── Diff ──

WorldDiff KenshiSDK::ComputeDiff(const WorldSnapshot& oldSnap,
                                  const WorldSnapshot& newSnap) const {
    WorldDiff diff;

    // Build lookup from old snapshot
    std::unordered_map<uintptr_t, const EntitySnapshot*> oldMap;
    oldMap.reserve(oldSnap.entities.size());
    for (const auto& e : oldSnap.entities) {
        oldMap[e.gamePtr] = &e;
    }

    // Check new entities against old
    std::unordered_map<uintptr_t, bool> seen;
    seen.reserve(newSnap.entities.size());

    for (const auto& e : newSnap.entities) {
        seen[e.gamePtr] = true;

        auto it = oldMap.find(e.gamePtr);
        if (it == oldMap.end()) {
            // New entity
            diff.added.push_back(e.gamePtr);
            EntityDelta delta;
            delta.gamePtr = e.gamePtr;
            delta.dirtyFlags = Dirty_All;
            delta.snapshot = e;
            diff.changed.push_back(std::move(delta));
        } else {
            // Existing entity — check for changes
            uint16_t flags = e.DiffAgainst(*it->second);
            if (flags != Dirty_None) {
                EntityDelta delta;
                delta.gamePtr = e.gamePtr;
                delta.dirtyFlags = flags;
                delta.snapshot = e;
                diff.changed.push_back(std::move(delta));
            }
        }
    }

    // Check for removed entities
    for (const auto& e : oldSnap.entities) {
        if (seen.find(e.gamePtr) == seen.end()) {
            diff.removed.push_back(e.gamePtr);
        }
    }

    return diff;
}

} // namespace kmp::sdk

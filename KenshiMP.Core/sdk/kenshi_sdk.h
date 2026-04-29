#pragma once
#include "kmp/types.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>

namespace kmp::sdk {

// ═══════════════════════════════════════════════════════════════════════════
//  ENTITY SNAPSHOT
//  Complete state of one game entity at a point in time.
//  Read from game memory via polling (no hooks required).
// ═══════════════════════════════════════════════════════════════════════════

struct EntitySnapshot {
    uintptr_t   gamePtr    = 0;        // Raw game object pointer (identity key)
    Vec3        position;
    Quat        rotation;
    float       health[7]  = {};       // Per body part
    uintptr_t   factionPtr = 0;
    uint32_t    factionId  = 0;
    std::string name;
    uint8_t     animState  = 0;
    float       moveSpeed  = 0.f;
    bool        alive      = true;
    bool        playerControlled = false;

    // Dirty comparison: returns DirtyFlags bitmask of what changed
    uint16_t DiffAgainst(const EntitySnapshot& prev) const;
};

// ═══════════════════════════════════════════════════════════════════════════
//  WORLD SNAPSHOT
//  All entities in the game world at a point in time.
// ═══════════════════════════════════════════════════════════════════════════

struct WorldSnapshot {
    std::vector<EntitySnapshot> entities;
    float timeOfDay  = 0.f;
    float gameSpeed  = 1.f;
    uint32_t frameNumber = 0;

    // Find entity by game pointer (returns nullptr if not found)
    const EntitySnapshot* FindByPtr(uintptr_t ptr) const;
};

// ═══════════════════════════════════════════════════════════════════════════
//  STATE DIFF
//  Delta between two world snapshots. Minimal data for network sync.
// ═══════════════════════════════════════════════════════════════════════════

struct EntityDelta {
    uintptr_t gamePtr = 0;
    uint16_t  dirtyFlags = 0;     // Which fields changed (DirtyFlags bitmask)
    EntitySnapshot snapshot;       // Full current state (sender filters by dirtyFlags)
};

struct WorldDiff {
    std::vector<EntityDelta>    changed;    // Entities with state changes
    std::vector<uintptr_t>      added;      // New entities (game pointers)
    std::vector<uintptr_t>      removed;    // Entities that disappeared
};

// ═══════════════════════════════════════════════════════════════════════════
//  KENSHI SDK
//  Clean abstraction over game state. Polls game memory each tick.
//  No hooks required for reading. Wraps CharacterIterator, CharacterAccessor,
//  GameWorldAccessor, and all offset chains behind a simple API.
// ═══════════════════════════════════════════════════════════════════════════

class KenshiSDK {
public:
    KenshiSDK() = default;

    // ── Lifecycle ──
    // Call once after game is loaded and global pointers are resolved.
    bool Initialize();

    // Call once per game tick from OnGameTick.
    // Polls all entities, builds snapshot, computes diff against previous.
    void Update();

    // ── State Access ──

    // Get the latest world snapshot (thread-safe copy).
    WorldSnapshot GetCurrentSnapshot() const;

    // Get the diff since last Update() call.
    WorldDiff GetLastDiff() const;

    // Get snapshot for a specific entity by game pointer.
    bool GetEntityState(uintptr_t gamePtr, EntitySnapshot& out) const;

    // ── State Write ──
    // Apply remote state to a game entity (writes to game memory).

    bool WritePosition(uintptr_t gamePtr, const Vec3& pos);
    bool WriteHealth(uintptr_t gamePtr, BodyPart part, float value);
    bool WriteName(uintptr_t gamePtr, const std::string& name);

    // ── Entity Enumeration ──

    // Get all currently tracked entity game pointers.
    std::vector<uintptr_t> GetAllEntityPtrs() const;

    // Get count of tracked entities.
    size_t GetEntityCount() const;

    // ── Player Faction ──

    // Get the local player's faction pointer (for filtering).
    uintptr_t GetPlayerFactionPtr() const { return m_playerFactionPtr; }
    void SetPlayerFactionPtr(uintptr_t ptr) { m_playerFactionPtr = ptr; }

    // ── Diagnostics ──
    uint32_t GetFrameNumber() const { return m_frameNumber; }
    float GetLastPollTimeMs() const { return m_lastPollTimeMs; }

    // Read one entity's full state from game memory (public for SEH wrapper access).
    EntitySnapshot ReadEntity(uintptr_t charPtr) const;

private:
    // Poll all characters from the game's character list.
    void PollEntities(WorldSnapshot& snapshot);

    // Compute diff between old and new snapshots.
    WorldDiff ComputeDiff(const WorldSnapshot& oldSnap, const WorldSnapshot& newSnap) const;

    mutable std::mutex m_mutex;
    WorldSnapshot m_current;
    WorldSnapshot m_previous;
    WorldDiff     m_lastDiff;

    uintptr_t m_playerFactionPtr = 0;
    uint32_t  m_frameNumber = 0;
    float     m_lastPollTimeMs = 0.f;
    bool      m_initialized = false;
};

} // namespace kmp::sdk

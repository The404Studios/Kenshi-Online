#pragma once
#include "kmp/types.h"
#include "kmp/memory.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>

namespace kmp {

// Forward - the game's character creation function
// RootObjectFactory::process (RVA 0x00581770)
// RCX = this (factory instance), RDX = GameData* (template)
// Returns: void* (Character*)
using FactoryProcessFn = void*(__fastcall*)(void* factory, void* gameData);

// Pending spawn request queued from the network thread
struct SpawnRequest {
    EntityID    netId;
    PlayerID    owner;
    EntityType  type;
    std::string templateName;  // GameData template name (e.g. "Greenlander")
    Vec3        position;
    Quat        rotation;
    uint32_t    templateId;
    uint32_t    factionId;
};

class SpawnManager {
public:
    // Called from entity_hooks when a character is created by the game.
    // Captures the factory pointer and builds the template database.
    void OnGameCharacterCreated(void* factory, void* gameData, void* character);

    // Queue a spawn request (thread-safe, called from network thread)
    void QueueSpawn(const SpawnRequest& request);

    // Process queued spawns (called from game thread only!)
    void ProcessSpawnQueue();

    // Find a GameData template by name
    void* FindTemplate(const std::string& name) const;

    // Get any valid character template (fallback)
    void* GetDefaultTemplate() const;

    // Is the factory ready? (have we captured it yet?)
    bool IsReady() const { return m_factory != nullptr; }

    // Get the number of known templates
    size_t GetTemplateCount() const;

    // Scan the heap for GameData objects (called once after game loads)
    void ScanGameDataHeap();

private:
    // The captured RootObjectFactory instance
    void* m_factory = nullptr;

    // The original factory->process function pointer (trampoline)
    FactoryProcessFn m_origProcess = nullptr;

    // Template database: name -> GameData*
    mutable std::mutex m_templateMutex;
    std::unordered_map<std::string, void*> m_templates;
    void* m_defaultTemplate = nullptr; // First captured template

    // Spawn queue (network thread -> game thread)
    std::mutex m_queueMutex;
    std::queue<SpawnRequest> m_spawnQueue;

    // Callback to register spawned characters
    std::function<void(EntityID netId, void* gameObject)> m_onSpawned;

public:
    void SetOrigProcess(FactoryProcessFn fn) { m_origProcess = fn; }
    void SetOnSpawnedCallback(std::function<void(EntityID, void*)> cb) { m_onSpawned = cb; }

    // Read a Kenshi std::string from memory (handles SSO).
    // Public so entity_hooks and other systems can read template names.
    static std::string ReadKenshiString(uintptr_t addr);
};

} // namespace kmp

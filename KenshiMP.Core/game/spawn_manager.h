#pragma once
#include "kmp/types.h"
#include "kmp/memory.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <atomic>

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
    uint32_t    retryCount = 0; // How many times this request has been re-queued

    // Extended state (synced on connect so server+clients have correct initial state)
    bool        hasExtendedState = false;
    float       health[7] = {100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f};
    bool        alive = true;
};

static constexpr uint32_t MAX_SPAWN_RETRIES = 200; // ~10 seconds at 20 ticks/sec

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

    // Get the factory pointer (needed for createRandomChar fallback)
    void* GetFactory() const { return m_factory; }

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
    void* m_defaultTemplate = nullptr; // Hook parameter template (may be invalid/temporary!)
    void* m_characterSourcedTemplate = nullptr; // Template extracted from character backpointer (validated, persistent)
    uintptr_t m_managerPointer = 0; // GameDataManager* found via character-sourced template

    // Factory-validated templates: these are gameData pointers actually passed to the factory
    // by the game engine. They are GUARANTEED to work with factory->process().
    // Heap-scan templates may include Race/Item/Building types that crash the factory.
    std::unordered_map<std::string, void*> m_factoryInputTemplates; // name -> gameData*
    void* m_lastFactoryInput = nullptr;       // Most recent factory input (any type)
    std::string m_lastFactoryInputName;       // Name of the above

    // CHARACTER templates: subset of factory-validated templates from objects with factions.
    // Objects with factions are actual characters (not buildings, items, or food).
    // These are the ONLY templates suitable for spawning remote player characters.
    std::unordered_map<std::string, void*> m_characterTemplates; // name -> gameData*
    void* m_lastCharacterTemplate = nullptr;
    std::string m_lastCharacterTemplateName;

    // Saved request struct from a successful factory call
    std::vector<uint8_t> m_savedRequestStruct;
    bool m_hasRequestStruct = false;

    // Pre-call request struct for standalone spawning
    uint8_t m_preCallData[1024] = {};
    size_t m_preCallDataSize = 0;
    uintptr_t m_preCallOrigAddr = 0;  // Original stack address (for self-reference fixup)
    bool m_hasPreCallData = false;

    // Mod player templates: GameData* from kenshi-online.mod for each player slot.
    // These are persistent objects in the game's GameDataManager — safe to pass to factory.
    static constexpr int MAX_MOD_TEMPLATES = 16;
    void* m_modPlayerTemplates[MAX_MOD_TEMPLATES] = {};
    std::atomic<int> m_modTemplateCount{0};

    // Mod candidate storage: multiple GameData entries per name.
    // m_templates is a unique map (one entry per name), so when the mod defines
    // both a FACTION and a CHARACTER named "Player 1", one overwrites the other.
    // This vector stores ALL heap-scan entries for mod player names so that
    // FindModTemplates can use the numId heuristic to pick the correct one.
    std::vector<std::pair<std::string, void*>> m_modCandidates;

    // GameData pointer offset in the request struct (detected at runtime).
    // -1 = not detected yet.
    int m_gameDataOffsetInStruct = -1;

    // Position offset in the request struct (detected at runtime by entity_hooks).
    int m_positionOffsetInStruct = -1;

    // Spawn queue (network thread -> game thread)
    mutable std::mutex m_queueMutex;
    std::queue<SpawnRequest> m_spawnQueue;

    // Callback to register spawned characters
    std::function<void(EntityID netId, void* gameObject)> m_onSpawned;

public:
    void SetFactory(void* factory) {
        if (!m_factory && factory) {
            m_factory = factory;
        }
    }
    void SetOrigProcess(FactoryProcessFn fn) { m_origProcess = fn; }
    void SetOnSpawnedCallback(std::function<void(EntityID, void*)> cb) { m_onSpawned = cb; }

    // Save a copy of the factory's request struct from a successful game call.
    // The factory takes (factory*, requestStruct*), NOT (factory*, GameData*).
    // We replicate the request struct for remote spawns.
    void SetSavedRequestStruct(const uint8_t* data, size_t size);
    bool HasRequestStruct() const { return m_hasRequestStruct; }

    // Save the PRE-CALL request struct data with original address for self-reference fixup.
    // This allows calling the factory standalone (from GameFrameUpdate) without
    // needing to be inside Hook_CharacterCreate.
    void SetPreCallData(const uint8_t* data, size_t size, uintptr_t origAddr);
    bool HasPreCallData() const { return m_hasPreCallData; }

    // Spawn a character using the saved pre-call data (standalone, no hook context needed).
    // Constructs a request struct, fixes self-references, and calls the factory.
    // If desiredPosition is non-null, writes it into the struct at offset 0x20 (detected).
    // Returns the created character pointer or nullptr.
    void* SpawnCharacterDirect(const Vec3* desiredPosition = nullptr, int modSlot = 0);

    // Process spawn queue from within the CharacterCreate hook.
    // The hook has ALREADY disabled itself (HookBypass active), so we can call
    // the factory directly. This runs on the game thread in the correct context.
    // Returns the number of characters spawned.
    int ProcessSpawnQueueFromHook(void* factory);

    // Check if there are pending spawn requests
    bool HasPendingSpawns() const {
        std::lock_guard lock(m_queueMutex);
        return !m_spawnQueue.empty();
    }

    // Get number of pending spawn requests
    size_t GetPendingSpawnCount() const {
        std::lock_guard lock(m_queueMutex);
        return m_spawnQueue.size();
    }

    // Clear all pending spawn requests (call on full disconnect)
    void ClearSpawnQueue() {
        std::lock_guard lock(m_queueMutex);
        std::queue<SpawnRequest> empty;
        m_spawnQueue.swap(empty);
    }

    // Remove pending spawn requests for a specific player (call on player leave)
    int ClearSpawnsForOwner(PlayerID owner) {
        std::lock_guard lock(m_queueMutex);
        std::queue<SpawnRequest> filtered;
        int removed = 0;
        while (!m_spawnQueue.empty()) {
            SpawnRequest req = m_spawnQueue.front();
            m_spawnQueue.pop();
            if (req.owner == owner) {
                removed++;
            } else {
                filtered.push(req);
            }
        }
        m_spawnQueue.swap(filtered);
        return removed;
    }

    // Pop the next spawn request from the queue (returns true if one was available)
    bool PopNextSpawn(SpawnRequest& outReq) {
        std::lock_guard lock(m_queueMutex);
        if (m_spawnQueue.empty()) return false;
        outReq = m_spawnQueue.front();
        m_spawnQueue.pop();
        return true;
    }

    // Re-queue a spawn request (for retry)
    void RequeueSpawn(const SpawnRequest& req) {
        std::lock_guard lock(m_queueMutex);
        m_spawnQueue.push(req);
    }

    // Get the validated character-sourced template (preferred for spawning)
    void* GetCharacterSourcedTemplate() const {
        std::lock_guard lock(m_templateMutex);
        return m_characterSourcedTemplate;
    }

    // Get count of factory-validated templates (all types)
    size_t GetFactoryTemplateCount() const {
        std::lock_guard lock(m_templateMutex);
        return m_factoryInputTemplates.size();
    }

    // Get count of CHARACTER templates (objects with factions — actual characters)
    size_t GetCharacterTemplateCount() const {
        std::lock_guard lock(m_templateMutex);
        return m_characterTemplates.size();
    }

    // Get discovered GameDataManager pointer (for heap scan bootstrapping)
    uintptr_t GetManagerPointer() const { return m_managerPointer; }

    // ── Mod Template System ──
    // After heap scan, find GameData entries from kenshi-online.mod for player characters.
    // These are REAL persistent GameData objects that the factory can use directly.
    void FindModTemplates();

    // Get mod template for a player slot (0-based). Returns nullptr if not found.
    void* GetModTemplate(int playerSlot) const;

    // Get number of discovered mod templates
    int GetModTemplateCount() const { return m_modTemplateCount; }

    // ── GameData Offset in Request Struct ──
    // Detected at runtime: offset within the pre-call request struct where
    // the GameData pointer is stored. Allows swapping templates for remote spawns.
    void SetGameDataOffset(int offset);
    int GetGameDataOffset() const { return m_gameDataOffsetInStruct; }
    void SetPositionOffset(int offset);
    int GetPositionOffset() const { return m_positionOffsetInStruct; }

    // ── Improved Spawn with Mod Template ──
    // Clones the pre-call struct, swaps in a mod template's GameData pointer,
    // sets desired position, fixes self-references, and calls factory.
    // This creates a fully-initialized character because the GameData is real.
    void* SpawnWithModTemplate(int playerSlot, const Vec3& position);

    // Read a Kenshi std::string from memory (handles SSO).
    // Public so entity_hooks and other systems can read template names.
    static std::string ReadKenshiString(uintptr_t addr);

    // Verify factory readiness and log detailed status.
    // Called from Core::OnGameLoaded() to confirm spawn system is operational.
    // Returns true if at least one spawn path is available.
    bool VerifyReadiness() const;
};

} // namespace kmp

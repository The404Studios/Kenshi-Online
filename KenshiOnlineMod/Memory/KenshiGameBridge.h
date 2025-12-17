/*
 * Kenshi Game Bridge
 * Clean API for reading/writing Kenshi game state
 * This is the main interface between the multiplayer mod and Kenshi
 */

#pragma once

#include "KenshiMemoryStructures.h"
#include "PatternScanner.h"
#include <vector>
#include <functional>
#include <mutex>

namespace Kenshi
{
    //=========================================================================
    // EXPORTED STATE STRUCTURES (for network sync)
    //=========================================================================

    // Lightweight player state for network synchronization
    struct PlayerState
    {
        uint32_t characterId;
        char name[64];

        // Transform
        Vector3 position;
        Quaternion rotation;
        Vector3 velocity;

        // Health
        float health;
        float maxHealth;
        float bloodLevel;
        float hunger;
        float thirst;

        // Limb health (indexed by LimbType)
        float limbHealth[(int)LimbType::LIMB_COUNT];
        uint8_t limbBleeding[(int)LimbType::LIMB_COUNT];

        // State
        AIState state;
        AnimationType currentAnimation;
        uint8_t isUnconscious;
        uint8_t isDead;
        uint8_t isInCombat;
        uint8_t isSneaking;

        // Equipment (item template IDs)
        uint32_t equippedItems[(int)EquipSlot::SLOT_COUNT];

        // Faction
        int32_t factionId;
        int32_t squadId;

        // Combat
        uint32_t combatTargetId;
        float attackCooldown;

        // Timestamp
        float gameTime;
        uint64_t syncTick;
    };

    // Squad state for network sync
    struct SquadState
    {
        uint32_t squadId;
        char name[64];

        int32_t factionId;
        uint32_t leaderId;

        SquadOrder currentOrder;
        FormationType formation;
        Vector3 orderTarget;

        uint32_t memberIds[32];
        int32_t memberCount;

        uint8_t isInCombat;
        uint8_t isPlayerSquad;
        uint8_t isMoving;
        uint8_t padding;

        uint64_t syncTick;
    };

    // World state snapshot
    struct WorldState
    {
        // Time
        float gameTime;
        int32_t gameDay;
        int32_t gameYear;
        float timeScale;

        // Weather
        WeatherType weather;
        float weatherIntensity;
        float temperature;
        float windSpeed;
        float windDirection;

        // Player info
        int32_t playerMoney;
        int32_t playerFactionId;

        // Stats
        int32_t playerCharacterCount;
        int32_t totalNPCCount;
        int32_t totalBuildingCount;

        uint64_t syncTick;
    };

    // Combat event for sync
    struct CombatEventSync
    {
        uint32_t attackerId;
        uint32_t defenderId;
        AttackType attackType;
        DamageType damageType;
        LimbType targetLimb;
        float damage;
        uint8_t wasBlocked;
        uint8_t wasDodged;
        uint8_t isCritical;
        uint8_t causedKnockdown;
        float timestamp;
    };

    // Inventory change event
    struct InventoryEvent
    {
        uint32_t characterId;
        uint32_t itemId;
        uint32_t itemTemplateId;
        int32_t quantityChange;  // Positive = add, negative = remove
        int32_t slotIndex;
        float timestamp;
    };

    //=========================================================================
    // GAME BRIDGE CLASS
    //=========================================================================

    class GameBridge
    {
    public:
        static GameBridge& Get()
        {
            static GameBridge instance;
            return instance;
        }

        //---------------------------------------------------------------------
        // Initialization
        //---------------------------------------------------------------------

        // Initialize the bridge - must be called first
        bool Initialize();

        // Shutdown and cleanup
        void Shutdown();

        // Check if bridge is ready
        bool IsReady() const { return m_isInitialized; }

        // Get initialization error message
        const char* GetLastError() const { return m_lastError; }

        //---------------------------------------------------------------------
        // Player State (Your "PlayerController")
        //---------------------------------------------------------------------

        // Get all player-controlled characters
        std::vector<PlayerState> GetAllPlayerCharacters();

        // Get a specific player character by ID
        bool GetPlayerState(uint32_t characterId, PlayerState& outState);

        // Set player state (for applying server updates)
        bool SetPlayerState(const PlayerState& state);

        // Get the currently selected character
        bool GetSelectedCharacter(PlayerState& outState);

        // Get player character count
        int32_t GetPlayerCharacterCount();

        // Get raw character pointer (for advanced use)
        Character* GetCharacterPtr(uint32_t characterId);

        //---------------------------------------------------------------------
        // Squad State
        //---------------------------------------------------------------------

        // Get the player's main squad
        bool GetPlayerSquadState(SquadState& outState);

        // Get squad by ID
        bool GetSquadState(uint32_t squadId, SquadState& outState);

        // Get all player squads
        std::vector<SquadState> GetAllPlayerSquads();

        // Issue order to squad
        bool IssueSquadOrder(uint32_t squadId, SquadOrder order, const Vector3& target);

        // Add character to squad
        bool AddToSquad(uint32_t squadId, uint32_t characterId);

        // Remove character from squad
        bool RemoveFromSquad(uint32_t squadId, uint32_t characterId);

        //---------------------------------------------------------------------
        // World State (Your "GameState")
        //---------------------------------------------------------------------

        // Get current world state
        bool GetWorldState(WorldState& outState);

        // Set game time (server authoritative)
        bool SetGameTime(float time);

        // Set weather (server authoritative)
        bool SetWeather(WeatherType weather, float intensity);

        // Get game world pointer
        GameWorld* GetGameWorld();

        //---------------------------------------------------------------------
        // Faction System
        //---------------------------------------------------------------------

        // Get faction relation between two factions
        int32_t GetFactionRelation(int32_t faction1, int32_t faction2);

        // Set faction relation (server authoritative)
        bool SetFactionRelation(int32_t faction1, int32_t faction2, int32_t relation);

        // Get all factions
        std::vector<Faction*> GetAllFactions();

        // Get player faction
        Faction* GetPlayerFaction();

        //---------------------------------------------------------------------
        // Movement and Commands
        //---------------------------------------------------------------------

        // Move character to position
        bool MoveCharacterTo(uint32_t characterId, const Vector3& position);

        // Set character position directly (teleport)
        bool SetCharacterPosition(uint32_t characterId, const Vector3& position);

        // Set character rotation
        bool SetCharacterRotation(uint32_t characterId, const Quaternion& rotation);

        // Set character state
        bool SetCharacterState(uint32_t characterId, AIState state);

        // Issue command to character
        bool IssueCommand(uint32_t characterId, SquadOrder command, const Vector3& target);

        //---------------------------------------------------------------------
        // Combat System
        //---------------------------------------------------------------------

        // Register combat event callback
        using CombatCallback = std::function<void(const CombatEventSync&)>;
        void SetCombatCallback(CombatCallback callback);

        // Apply combat event (from server)
        bool ApplyCombatEvent(const CombatEventSync& event);

        // Set character as combat target
        bool SetCombatTarget(uint32_t attackerId, uint32_t targetId);

        // Apply damage to character
        bool ApplyDamage(uint32_t characterId, LimbType limb, float damage, DamageType type);

        //---------------------------------------------------------------------
        // Inventory System
        //---------------------------------------------------------------------

        // Register inventory change callback
        using InventoryCallback = std::function<void(const InventoryEvent&)>;
        void SetInventoryCallback(InventoryCallback callback);

        // Add item to character inventory
        bool AddItemToInventory(uint32_t characterId, uint32_t itemTemplateId, int32_t quantity);

        // Remove item from inventory
        bool RemoveItemFromInventory(uint32_t characterId, uint32_t itemId, int32_t quantity);

        // Transfer item between characters
        bool TransferItem(uint32_t fromCharId, uint32_t toCharId, uint32_t itemId, int32_t quantity);

        // Set character money
        bool SetCharacterMoney(uint32_t characterId, int32_t amount);

        //---------------------------------------------------------------------
        // Animation System
        //---------------------------------------------------------------------

        // Play animation on character
        bool PlayAnimation(uint32_t characterId, AnimationType anim, bool force = false);

        // Get current animation
        AnimationType GetCurrentAnimation(uint32_t characterId);

        // Sync animation state from server
        bool SyncAnimation(uint32_t characterId, AnimationType anim, float time, float speed);

        //---------------------------------------------------------------------
        // NPC Management
        //---------------------------------------------------------------------

        // Get NPCs in range of position
        std::vector<PlayerState> GetNPCsInRange(const Vector3& position, float range);

        // Get all NPCs
        std::vector<Character*> GetAllNPCs();

        // Get NPC count
        int32_t GetNPCCount();

        //---------------------------------------------------------------------
        // Building System
        //---------------------------------------------------------------------

        // Get all player buildings
        std::vector<Building*> GetPlayerBuildings();

        // Get building by ID
        Building* GetBuilding(uint32_t buildingId);

        // Set building state
        bool SetBuildingState(uint32_t buildingId, BuildingState state, float progress);

        //---------------------------------------------------------------------
        // Utility Functions
        //---------------------------------------------------------------------

        // Convert world position to cell coordinates
        void WorldToCell(const Vector3& worldPos, int32_t& cellX, int32_t& cellY);

        // Convert cell coordinates to world position (center)
        Vector3 CellToWorld(int32_t cellX, int32_t cellY);

        // Get distance between two characters
        float GetDistance(uint32_t char1, uint32_t char2);

        // Check line of sight
        bool HasLineOfSight(const Vector3& from, const Vector3& to);

        // Get current sync tick
        uint64_t GetCurrentTick() const { return m_currentTick; }

        // Increment sync tick
        void IncrementTick() { m_currentTick++; }

    private:
        GameBridge() = default;
        ~GameBridge() = default;
        GameBridge(const GameBridge&) = delete;
        GameBridge& operator=(const GameBridge&) = delete;

        // Internal helpers
        Character* FindCharacter(uint32_t characterId);
        Squad* FindSquad(uint32_t squadId);
        void PopulatePlayerState(Character* character, PlayerState& state);
        void PopulateSquadState(Squad* squad, SquadState& state);

        // Memory read/write helpers
        template<typename T>
        T ReadMemory(uintptr_t address);

        template<typename T>
        bool WriteMemory(uintptr_t address, const T& value);

        bool m_isInitialized = false;
        char m_lastError[256] = { 0 };
        uint64_t m_currentTick = 0;

        std::mutex m_mutex;

        CombatCallback m_combatCallback;
        InventoryCallback m_inventoryCallback;

        // Cached pointers
        GameWorld* m_gameWorld = nullptr;
        Character** m_playerSquadList = nullptr;
        int32_t* m_playerSquadCount = nullptr;
    };

    //=========================================================================
    // HOOK SYSTEM FOR GAME EVENTS
    //=========================================================================

    // Hook manager for intercepting game functions
    class HookManager
    {
    public:
        static HookManager& Get()
        {
            static HookManager instance;
            return instance;
        }

        // Initialize all hooks
        bool Initialize();

        // Remove all hooks
        void Shutdown();

        // Individual hook controls
        bool HookCharacterUpdate(bool enable);
        bool HookCombatSystem(bool enable);
        bool HookInventorySystem(bool enable);
        bool HookAISystem(bool enable);

        // Callbacks
        using CharacterUpdateCallback = std::function<void(Character*)>;
        using CombatEventCallback = std::function<void(const CombatEvent&)>;

        void SetCharacterUpdateCallback(CharacterUpdateCallback cb) { m_charUpdateCallback = cb; }
        void SetCombatEventCallback(CombatEventCallback cb) { m_combatCallback = cb; }

    private:
        HookManager() = default;

        CharacterUpdateCallback m_charUpdateCallback;
        CombatEventCallback m_combatCallback;

        // Hook trampolines (function pointers to original code)
        void* m_originalCharUpdate = nullptr;
        void* m_originalCombatAttack = nullptr;
        void* m_originalInventoryAdd = nullptr;
        void* m_originalAIUpdate = nullptr;
    };

} // namespace Kenshi

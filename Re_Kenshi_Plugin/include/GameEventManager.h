#pragma once

#include "KenshiStructures.h"
#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>

namespace ReKenshi {
namespace Events {

/**
 * Game event types
 */
enum class GameEventType {
    // Character events
    CharacterSpawned,
    CharacterDied,
    CharacterUnconscious,
    CharacterHealed,
    CharacterDamaged,
    CharacterMoved,
    CharacterLevelUp,

    // Combat events
    CombatStarted,
    CombatEnded,
    AttackLanded,
    AttackMissed,
    BlockSuccessful,

    // World events
    DayChanged,
    TimeChanged,
    WeatherChanged,
    WorldLoaded,
    WorldUnloaded,

    // Squad events
    SquadCreated,
    SquadDisbanded,
    CharacterJoinedSquad,
    CharacterLeftSquad,

    // Item events
    ItemPickedUp,
    ItemDropped,
    ItemCrafted,
    ItemSold,
    ItemBought,

    // Faction events
    FactionReputationChanged,
    FactionWarDeclared,
    FactionPeaceDeclared,

    // Player events
    PlayerTeleported,
    PlayerSaved,
    PlayerLoaded,

    // Multiplayer events
    PlayerConnected,
    PlayerDisconnected,
    PlayerMessageReceived,
};

/**
 * Base game event
 */
struct GameEvent {
    GameEventType type;
    uint64_t timestamp;

    GameEvent(GameEventType t) : type(t) {
        timestamp = GetCurrentTimestamp();
    }

    virtual ~GameEvent() = default;

private:
    static uint64_t GetCurrentTimestamp();
};

/**
 * Character event
 */
struct CharacterEvent : public GameEvent {
    uintptr_t characterAddress;
    Kenshi::CharacterData characterData;
    std::string characterName;

    CharacterEvent(GameEventType type, uintptr_t addr)
        : GameEvent(type), characterAddress(addr) {}
};

/**
 * Character damage event
 */
struct CharacterDamageEvent : public CharacterEvent {
    float damageAmount;
    float healthBefore;
    float healthAfter;
    uintptr_t attackerAddress;
    std::string attackerName;

    CharacterDamageEvent(uintptr_t addr, float damage, float before, float after)
        : CharacterEvent(GameEventType::CharacterDamaged, addr)
        , damageAmount(damage)
        , healthBefore(before)
        , healthAfter(after)
        , attackerAddress(0) {}
};

/**
 * Character movement event
 */
struct CharacterMovementEvent : public CharacterEvent {
    Kenshi::Vector3 oldPosition;
    Kenshi::Vector3 newPosition;
    float distance;

    CharacterMovementEvent(uintptr_t addr, const Kenshi::Vector3& oldPos, const Kenshi::Vector3& newPos)
        : CharacterEvent(GameEventType::CharacterMoved, addr)
        , oldPosition(oldPos)
        , newPosition(newPos) {

        // Calculate distance
        float dx = newPos.x - oldPos.x;
        float dy = newPos.y - oldPos.y;
        float dz = newPos.z - oldPos.z;
        distance = sqrtf(dx*dx + dy*dy + dz*dz);
    }
};

/**
 * World event
 */
struct WorldEvent : public GameEvent {
    Kenshi::WorldStateData worldData;

    WorldEvent(GameEventType type) : GameEvent(type) {}
};

/**
 * Day change event
 */
struct DayChangeEvent : public WorldEvent {
    int32_t oldDay;
    int32_t newDay;

    DayChangeEvent(int32_t oldD, int32_t newD)
        : WorldEvent(GameEventType::DayChanged)
        , oldDay(oldD)
        , newDay(newD) {}
};

/**
 * Item event
 */
struct ItemEvent : public GameEvent {
    uintptr_t itemAddress;
    uintptr_t characterAddress;
    Kenshi::ItemData itemData;
    std::string characterName;

    ItemEvent(GameEventType type, uintptr_t itemAddr, uintptr_t charAddr)
        : GameEvent(type)
        , itemAddress(itemAddr)
        , characterAddress(charAddr) {}
};

/**
 * Event listener callback
 */
using EventCallback = std::function<void(const GameEvent&)>;

/**
 * Game Event Manager - detects and dispatches game events
 */
class GameEventManager {
public:
    GameEventManager();
    ~GameEventManager();

    // Initialize with game structure pointers
    void Initialize(uintptr_t gameWorldPtr, uintptr_t characterListPtr, uintptr_t playerPtr);
    void Shutdown();

    // Update - call this every frame to detect events
    void Update(float deltaTime);

    // Event subscription
    void Subscribe(GameEventType type, EventCallback callback);
    void Unsubscribe(GameEventType type);

    // Manual event dispatch
    void DispatchEvent(const GameEvent& event);

    // State queries
    bool IsInitialized() const { return m_initialized; }
    uint64_t GetEventCount(GameEventType type) const;

private:
    // Detection methods
    void DetectCharacterEvents();
    void DetectWorldEvents();
    void DetectCombatEvents();
    void DetectItemEvents();

    // Helper methods
    void CheckCharacterHealth(uintptr_t characterAddr, const Kenshi::CharacterData& currentData);
    void CheckCharacterPosition(uintptr_t characterAddr, const Kenshi::CharacterData& currentData);
    void CheckWorldState(const Kenshi::WorldStateData& currentData);

    // State tracking
    struct CharacterState {
        Kenshi::CharacterData lastData;
        uint64_t lastUpdateTime;
    };

    struct WorldState {
        Kenshi::WorldStateData lastData;
        uint64_t lastUpdateTime;
    };

    bool m_initialized;
    uintptr_t m_gameWorldPtr;
    uintptr_t m_characterListPtr;
    uintptr_t m_playerPtr;

    // Event listeners
    std::unordered_map<GameEventType, std::vector<EventCallback>> m_listeners;

    // State caches
    std::unordered_map<uintptr_t, CharacterState> m_characterStates;
    WorldState m_worldState;

    // Event statistics
    std::unordered_map<GameEventType, uint64_t> m_eventCounts;

    // Update throttling
    float m_updateAccumulator;
    static constexpr float UPDATE_INTERVAL = 0.1f; // Check for events 10 times per second
};

} // namespace Events
} // namespace ReKenshi

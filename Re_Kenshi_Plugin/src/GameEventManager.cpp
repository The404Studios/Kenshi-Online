#include "GameEventManager.h"
#include "MemoryScanner.h"
#include <chrono>
#include <sstream>

namespace ReKenshi {
namespace Events {

uint64_t GameEvent::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ms.count());
}

//=============================================================================
// GameEventManager Implementation
//=============================================================================

GameEventManager::GameEventManager()
    : m_initialized(false)
    , m_gameWorldPtr(0)
    , m_characterListPtr(0)
    , m_playerPtr(0)
    , m_updateAccumulator(0.0f)
{
}

GameEventManager::~GameEventManager() {
    Shutdown();
}

void GameEventManager::Initialize(uintptr_t gameWorldPtr, uintptr_t characterListPtr, uintptr_t playerPtr) {
    if (m_initialized) {
        return;
    }

    m_gameWorldPtr = gameWorldPtr;
    m_characterListPtr = characterListPtr;
    m_playerPtr = playerPtr;

    // Initialize world state
    if (m_gameWorldPtr) {
        Kenshi::GameDataReader::ReadWorldState(m_gameWorldPtr, m_worldState.lastData);
        m_worldState.lastUpdateTime = GameEvent::GetCurrentTimestamp();
    }

    m_initialized = true;
    OutputDebugStringA("[GameEventManager] Initialized\n");
}

void GameEventManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_listeners.clear();
    m_characterStates.clear();
    m_eventCounts.clear();

    m_initialized = false;
    OutputDebugStringA("[GameEventManager] Shutdown\n");
}

void GameEventManager::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    m_updateAccumulator += deltaTime;

    // Throttle updates to reduce CPU usage
    if (m_updateAccumulator < UPDATE_INTERVAL) {
        return;
    }

    m_updateAccumulator = 0.0f;

    // Detect various events
    DetectWorldEvents();
    DetectCharacterEvents();
    DetectCombatEvents();
    DetectItemEvents();
}

void GameEventManager::Subscribe(GameEventType type, EventCallback callback) {
    m_listeners[type].push_back(callback);

    std::ostringstream log;
    log << "[GameEventManager] Subscribed to event type: " << static_cast<int>(type) << "\n";
    OutputDebugStringA(log.str().c_str());
}

void GameEventManager::Unsubscribe(GameEventType type) {
    m_listeners.erase(type);
}

void GameEventManager::DispatchEvent(const GameEvent& event) {
    // Update statistics
    m_eventCounts[event.type]++;

    // Call all listeners for this event type
    auto it = m_listeners.find(event.type);
    if (it != m_listeners.end()) {
        for (const auto& callback : it->second) {
            callback(event);
        }
    }
}

uint64_t GameEventManager::GetEventCount(GameEventType type) const {
    auto it = m_eventCounts.find(type);
    return (it != m_eventCounts.end()) ? it->second : 0;
}

void GameEventManager::DetectWorldEvents() {
    if (!m_gameWorldPtr) {
        return;
    }

    Kenshi::WorldStateData currentWorld;
    if (!Kenshi::GameDataReader::ReadWorldState(m_gameWorldPtr, currentWorld)) {
        return;
    }

    // Check for day change
    if (currentWorld.dayNumber != m_worldState.lastData.dayNumber) {
        DayChangeEvent evt(m_worldState.lastData.dayNumber, currentWorld.dayNumber);
        evt.worldData = currentWorld;
        DispatchEvent(evt);
    }

    // Check for significant time change (more than 1 hour game time)
    float timeDiff = abs(currentWorld.timeOfDay - m_worldState.lastData.timeOfDay);
    if (timeDiff > 0.04f) {  // ~1 hour in game time
        WorldEvent evt(GameEventType::TimeChanged);
        evt.worldData = currentWorld;
        DispatchEvent(evt);
    }

    m_worldState.lastData = currentWorld;
    m_worldState.lastUpdateTime = GameEvent::GetCurrentTimestamp();
}

void GameEventManager::DetectCharacterEvents() {
    if (!m_characterListPtr) {
        return;
    }

    // TODO: Iterate through character list
    // For now, just check the player character
    if (m_playerPtr) {
        uintptr_t playerCharacter = 0;
        if (Memory::MemoryScanner::ReadMemory(m_playerPtr, playerCharacter) && playerCharacter) {
            Kenshi::CharacterData currentData;
            if (Kenshi::GameDataReader::ReadCharacter(playerCharacter, currentData)) {
                CheckCharacterHealth(playerCharacter, currentData);
                CheckCharacterPosition(playerCharacter, currentData);

                // Update cache
                m_characterStates[playerCharacter].lastData = currentData;
                m_characterStates[playerCharacter].lastUpdateTime = GameEvent::GetCurrentTimestamp();
            }
        }
    }
}

void GameEventManager::CheckCharacterHealth(uintptr_t characterAddr, const Kenshi::CharacterData& currentData) {
    auto it = m_characterStates.find(characterAddr);
    if (it == m_characterStates.end()) {
        // First time seeing this character
        return;
    }

    const auto& lastData = it->second.lastData;

    // Check for damage
    if (currentData.health < lastData.health) {
        CharacterDamageEvent evt(
            characterAddr,
            lastData.health - currentData.health,
            lastData.health,
            currentData.health
        );

        // Copy character data
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);

        DispatchEvent(evt);
    }

    // Check for healing
    if (currentData.health > lastData.health) {
        CharacterEvent evt(GameEventType::CharacterHealed, characterAddr);
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);
        DispatchEvent(evt);
    }

    // Check for death
    if (currentData.isAlive && !lastData.isAlive) {
        CharacterEvent evt(GameEventType::CharacterSpawned, characterAddr);
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);
        DispatchEvent(evt);
    }
    else if (!currentData.isAlive && lastData.isAlive) {
        CharacterEvent evt(GameEventType::CharacterDied, characterAddr);
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);
        DispatchEvent(evt);
    }

    // Check for unconscious
    if (currentData.isUnconscious && !lastData.isUnconscious) {
        CharacterEvent evt(GameEventType::CharacterUnconscious, characterAddr);
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);
        DispatchEvent(evt);
    }
}

void GameEventManager::CheckCharacterPosition(uintptr_t characterAddr, const Kenshi::CharacterData& currentData) {
    auto it = m_characterStates.find(characterAddr);
    if (it == m_characterStates.end()) {
        return;
    }

    const auto& lastData = it->second.lastData;

    // Check for significant movement (more than 1 unit)
    float dx = currentData.position.x - lastData.position.x;
    float dy = currentData.position.y - lastData.position.y;
    float dz = currentData.position.z - lastData.position.z;
    float distance = sqrtf(dx*dx + dy*dy + dz*dz);

    if (distance > 1.0f) {
        CharacterMovementEvent evt(characterAddr, lastData.position, currentData.position);
        evt.characterData = currentData;
        evt.characterName = std::string(currentData.name);
        DispatchEvent(evt);
    }
}

void GameEventManager::DetectCombatEvents() {
    // TODO: Implement combat event detection
    // This would require hooking into combat functions
}

void GameEventManager::DetectItemEvents() {
    // TODO: Implement item event detection
    // This would require monitoring inventory changes
}

void GameEventManager::CheckWorldState(const Kenshi::WorldStateData& currentData) {
    // Additional world state checks can go here
}

} // namespace Events
} // namespace ReKenshi

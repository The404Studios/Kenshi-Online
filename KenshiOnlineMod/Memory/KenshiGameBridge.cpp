/*
 * Kenshi Game Bridge Implementation
 * Clean API for reading/writing Kenshi game state
 */

#include "KenshiGameBridge.h"
#include <cstring>
#include <algorithm>

namespace Kenshi
{
    //=========================================================================
    // MEMORY ACCESS HELPERS
    //=========================================================================

    template<typename T>
    T GameBridge::ReadMemory(uintptr_t address)
    {
        __try
        {
            return *reinterpret_cast<T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return T{};
        }
    }

    template<typename T>
    bool GameBridge::WriteMemory(uintptr_t address, const T& value)
    {
        __try
        {
            DWORD oldProtect;
            if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                return false;
            }

            *reinterpret_cast<T*>(address) = value;

            VirtualProtect(reinterpret_cast<void*>(address), sizeof(T), oldProtect, &oldProtect);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    //=========================================================================
    // INITIALIZATION
    //=========================================================================

    bool GameBridge::Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_isInitialized)
        {
            return true;
        }

        // Initialize offset manager (includes pattern scanning)
        if (!OffsetManager::Get().Initialize())
        {
            strcpy_s(m_lastError, "Failed to initialize offset manager");
            return false;
        }

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        if (!offsets.isValid)
        {
            strcpy_s(m_lastError, "Offsets are not valid");
            return false;
        }

        // Cache important pointers
        if (offsets.gameWorld != 0)
        {
            m_gameWorld = ReadMemory<GameWorld*>(offsets.gameWorld);
        }

        if (offsets.playerSquadList != 0)
        {
            m_playerSquadList = reinterpret_cast<Character**>(offsets.playerSquadList);
        }

        if (offsets.playerSquadCount != 0)
        {
            m_playerSquadCount = reinterpret_cast<int32_t*>(offsets.playerSquadCount);
        }

        // Validate we can read basic game state
        if (m_gameWorld == nullptr)
        {
            strcpy_s(m_lastError, "Failed to get GameWorld pointer");
            return false;
        }

        m_isInitialized = true;
        m_lastError[0] = '\0';
        return true;
    }

    void GameBridge::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_isInitialized = false;
        m_gameWorld = nullptr;
        m_playerSquadList = nullptr;
        m_playerSquadCount = nullptr;
        m_combatCallback = nullptr;
        m_inventoryCallback = nullptr;
    }

    //=========================================================================
    // INTERNAL HELPERS
    //=========================================================================

    Character* GameBridge::FindCharacter(uint32_t characterId)
    {
        if (!m_isInitialized)
            return nullptr;

        // First check player squad
        if (m_playerSquadList && m_playerSquadCount)
        {
            int32_t count = ReadMemory<int32_t>(reinterpret_cast<uintptr_t>(m_playerSquadCount));
            if (count > 0 && count < 100)
            {
                Character** list = ReadMemory<Character**>(reinterpret_cast<uintptr_t>(m_playerSquadList));
                if (list)
                {
                    for (int32_t i = 0; i < count; i++)
                    {
                        Character* c = list[i];
                        if (c && c->characterId == characterId)
                        {
                            return c;
                        }
                    }
                }
            }
        }

        // Check all characters
        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.allCharactersList != 0)
        {
            Character** allChars = ReadMemory<Character**>(offsets.allCharactersList);
            int32_t allCount = ReadMemory<int32_t>(offsets.allCharactersCount);

            if (allChars && allCount > 0 && allCount < 10000)
            {
                for (int32_t i = 0; i < allCount; i++)
                {
                    Character* c = allChars[i];
                    if (c && c->characterId == characterId)
                    {
                        return c;
                    }
                }
            }
        }

        return nullptr;
    }

    Squad* GameBridge::FindSquad(uint32_t squadId)
    {
        if (!m_isInitialized || !m_gameWorld)
            return nullptr;

        Squad** squads = m_gameWorld->allSquads;
        int32_t count = m_gameWorld->squadCount;

        if (!squads || count <= 0)
            return nullptr;

        for (int32_t i = 0; i < count && i < 1000; i++)
        {
            if (squads[i] && squads[i]->squadId == squadId)
            {
                return squads[i];
            }
        }

        return nullptr;
    }

    void GameBridge::PopulatePlayerState(Character* character, PlayerState& state)
    {
        if (!character)
            return;

        memset(&state, 0, sizeof(PlayerState));

        state.characterId = character->characterId;

        // Copy name safely
        if (character->name.data)
        {
            strncpy_s(state.name, sizeof(state.name),
                     character->name.data, _TRUNCATE);
        }

        // Transform
        state.position = character->position;
        state.rotation = character->rotation;
        state.velocity = character->velocity;

        // Health
        state.health = character->health;
        state.maxHealth = character->maxHealth;
        state.bloodLevel = character->bloodLevel;
        state.hunger = character->hunger;
        state.thirst = character->thirst;

        // Limb health
        if (character->body)
        {
            for (int i = 0; i < (int)LimbType::LIMB_COUNT; i++)
            {
                state.limbHealth[i] = character->body->parts[i].health;
                state.limbBleeding[i] = character->body->parts[i].isBleeding;
            }
        }

        // State
        state.state = character->currentState;

        if (character->animState)
        {
            state.currentAnimation = character->animState->currentAnim;
        }

        state.isUnconscious = character->isUnconscious;
        state.isDead = character->isDead;
        state.isInCombat = character->isInCombat;
        state.isSneaking = character->isSneaking;

        // Equipment
        if (character->equipment)
        {
            for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            {
                Item* item = character->equipment->slots[i];
                if (item)
                {
                    state.equippedItems[i] = item->templateId;
                }
            }
        }

        // Faction/Squad
        state.factionId = character->factionId;
        if (character->squad)
        {
            state.squadId = character->squad->squadId;
        }

        // Combat
        if (character->combatTarget)
        {
            state.combatTargetId = character->combatTarget->characterId;
        }
        state.attackCooldown = character->attackCooldown;

        // Timestamps
        if (m_gameWorld)
        {
            state.gameTime = m_gameWorld->gameTime;
        }
        state.syncTick = m_currentTick;
    }

    void GameBridge::PopulateSquadState(Squad* squad, SquadState& state)
    {
        if (!squad)
            return;

        memset(&state, 0, sizeof(SquadState));

        state.squadId = squad->squadId;

        if (squad->name.data)
        {
            strncpy_s(state.name, sizeof(state.name),
                     squad->name.data, _TRUNCATE);
        }

        state.factionId = squad->factionId;

        if (squad->leader)
        {
            state.leaderId = squad->leader->characterId;
        }

        state.currentOrder = squad->currentOrder;
        state.formation = squad->formation;
        state.orderTarget = squad->orderTarget;

        // Members
        state.memberCount = std::min(squad->memberCount, 32);
        for (int32_t i = 0; i < state.memberCount; i++)
        {
            if (squad->members[i])
            {
                state.memberIds[i] = squad->members[i]->characterId;
            }
        }

        state.isInCombat = squad->isInCombat;
        state.isPlayerSquad = squad->isPlayerSquad;
        state.isMoving = squad->isMoving;

        state.syncTick = m_currentTick;
    }

    //=========================================================================
    // PLAYER STATE METHODS
    //=========================================================================

    std::vector<PlayerState> GameBridge::GetAllPlayerCharacters()
    {
        std::vector<PlayerState> result;

        if (!m_isInitialized || !m_playerSquadList || !m_playerSquadCount)
            return result;

        std::lock_guard<std::mutex> lock(m_mutex);

        int32_t count = ReadMemory<int32_t>(reinterpret_cast<uintptr_t>(m_playerSquadCount));
        if (count <= 0 || count > 100)
            return result;

        Character** list = ReadMemory<Character**>(reinterpret_cast<uintptr_t>(m_playerSquadList));
        if (!list)
            return result;

        result.reserve(count);

        for (int32_t i = 0; i < count; i++)
        {
            __try
            {
                Character* c = list[i];
                if (c && c->isPlayerControlled)
                {
                    PlayerState state;
                    PopulatePlayerState(c, state);
                    result.push_back(state);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Skip invalid character
            }
        }

        return result;
    }

    bool GameBridge::GetPlayerState(uint32_t characterId, PlayerState& outState)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        PopulatePlayerState(c, outState);
        return true;
    }

    bool GameBridge::SetPlayerState(const PlayerState& state)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(state.characterId);
        if (!c)
            return false;

        // Apply position/rotation
        c->position = state.position;
        c->rotation = state.rotation;
        c->velocity = state.velocity;

        // Apply health (with bounds checking)
        c->health = std::clamp(state.health, 0.0f, state.maxHealth);
        c->bloodLevel = std::clamp(state.bloodLevel, 0.0f, 100.0f);
        c->hunger = std::clamp(state.hunger, 0.0f, c->hungerMax);
        c->thirst = std::clamp(state.thirst, 0.0f, c->thirstMax);

        // Apply limb health
        if (c->body)
        {
            for (int i = 0; i < (int)LimbType::LIMB_COUNT; i++)
            {
                c->body->parts[i].health = state.limbHealth[i];
                c->body->parts[i].isBleeding = state.limbBleeding[i];
            }
        }

        // Apply state
        c->currentState = state.state;
        c->isUnconscious = state.isUnconscious;
        c->isDead = state.isDead;
        c->isInCombat = state.isInCombat;
        c->isSneaking = state.isSneaking;

        return true;
    }

    bool GameBridge::GetSelectedCharacter(PlayerState& outState)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        // Try to get from game world's selected array
        if (m_gameWorld && m_gameWorld->selectedCharacters && m_gameWorld->selectedCount > 0)
        {
            Character* selected = m_gameWorld->selectedCharacters[0];
            if (selected)
            {
                PopulatePlayerState(selected, outState);
                return true;
            }
        }

        return false;
    }

    int32_t GameBridge::GetPlayerCharacterCount()
    {
        if (!m_isInitialized || !m_playerSquadCount)
            return 0;

        return ReadMemory<int32_t>(reinterpret_cast<uintptr_t>(m_playerSquadCount));
    }

    Character* GameBridge::GetCharacterPtr(uint32_t characterId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return FindCharacter(characterId);
    }

    //=========================================================================
    // SQUAD STATE METHODS
    //=========================================================================

    bool GameBridge::GetPlayerSquadState(SquadState& outState)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return false;

        Squad* playerSquad = m_gameWorld->playerSquad;
        if (!playerSquad)
            return false;

        PopulateSquadState(playerSquad, outState);
        return true;
    }

    bool GameBridge::GetSquadState(uint32_t squadId, SquadState& outState)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Squad* squad = FindSquad(squadId);
        if (!squad)
            return false;

        PopulateSquadState(squad, outState);
        return true;
    }

    std::vector<SquadState> GameBridge::GetAllPlayerSquads()
    {
        std::vector<SquadState> result;

        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return result;

        Squad** squads = m_gameWorld->allSquads;
        int32_t count = m_gameWorld->squadCount;

        if (!squads || count <= 0)
            return result;

        for (int32_t i = 0; i < count && i < 100; i++)
        {
            Squad* s = squads[i];
            if (s && s->isPlayerSquad)
            {
                SquadState state;
                PopulateSquadState(s, state);
                result.push_back(state);
            }
        }

        return result;
    }

    bool GameBridge::IssueSquadOrder(uint32_t squadId, SquadOrder order, const Vector3& target)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Squad* squad = FindSquad(squadId);
        if (!squad)
            return false;

        squad->currentOrder = order;
        squad->orderTarget = target;

        return true;
    }

    bool GameBridge::AddToSquad(uint32_t squadId, uint32_t characterId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.fnAddToSquad == 0)
            return false;

        Squad* squad = FindSquad(squadId);
        Character* character = FindCharacter(characterId);

        if (!squad || !character)
            return false;

        // Call game function
        typedef bool(__fastcall* AddToSquadFn)(Squad*, Character*);
        AddToSquadFn addFn = reinterpret_cast<AddToSquadFn>(offsets.fnAddToSquad);

        __try
        {
            return addFn(squad, character);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GameBridge::RemoveFromSquad(uint32_t squadId, uint32_t characterId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Squad* squad = FindSquad(squadId);
        if (!squad)
            return false;

        Character* character = FindCharacter(characterId);
        if (!character)
            return false;

        // Remove from member list
        for (int32_t i = 0; i < squad->memberCount; i++)
        {
            if (squad->members[i] == character)
            {
                // Shift remaining members
                for (int32_t j = i; j < squad->memberCount - 1; j++)
                {
                    squad->members[j] = squad->members[j + 1];
                }
                squad->memberCount--;

                // Clear character's squad reference
                character->squad = nullptr;
                character->squad_leader = nullptr;

                return true;
            }
        }

        return false;
    }

    //=========================================================================
    // WORLD STATE METHODS
    //=========================================================================

    bool GameBridge::GetWorldState(WorldState& outState)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return false;

        memset(&outState, 0, sizeof(WorldState));

        // Time
        outState.gameTime = m_gameWorld->gameTime;
        outState.gameDay = m_gameWorld->gameDay;
        outState.gameYear = m_gameWorld->gameYear;
        outState.timeScale = m_gameWorld->timeScale;

        // Weather
        if (m_gameWorld->weather)
        {
            outState.weather = m_gameWorld->weather->currentWeather;
            outState.weatherIntensity = m_gameWorld->weather->intensity;
            outState.temperature = m_gameWorld->weather->temperature;
            outState.windSpeed = m_gameWorld->weather->windSpeed;
            outState.windDirection = m_gameWorld->weather->windDirection;
        }

        // Player info
        outState.playerMoney = m_gameWorld->playerMoney;
        if (m_gameWorld->playerFaction)
        {
            outState.playerFactionId = m_gameWorld->playerFaction->factionId;
        }

        // Stats
        if (m_playerSquadCount)
        {
            outState.playerCharacterCount = *m_playerSquadCount;
        }
        outState.totalNPCCount = m_gameWorld->characterCount;
        outState.totalBuildingCount = m_gameWorld->buildingCount;

        outState.syncTick = m_currentTick;

        return true;
    }

    bool GameBridge::SetGameTime(float time)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return false;

        m_gameWorld->gameTime = std::fmod(time, 24.0f);
        return true;
    }

    bool GameBridge::SetWeather(WeatherType weather, float intensity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld || !m_gameWorld->weather)
            return false;

        m_gameWorld->weather->currentWeather = weather;
        m_gameWorld->weather->intensity = std::clamp(intensity, 0.0f, 1.0f);
        return true;
    }

    GameWorld* GameBridge::GetGameWorld()
    {
        return m_gameWorld;
    }

    //=========================================================================
    // FACTION METHODS
    //=========================================================================

    int32_t GameBridge::GetFactionRelation(int32_t faction1, int32_t faction2)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized)
            return 0;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.factionList == 0)
            return 0;

        Faction** factions = ReadMemory<Faction**>(offsets.factionList);
        int32_t count = ReadMemory<int32_t>(offsets.factionCount);

        if (!factions || faction1 < 0 || faction1 >= count)
            return 0;

        Faction* f1 = factions[faction1];
        if (!f1 || !f1->relations || faction2 >= f1->relationCount)
            return 0;

        return f1->relations[faction2];
    }

    bool GameBridge::SetFactionRelation(int32_t faction1, int32_t faction2, int32_t relation)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        // Try to use game function if available
        if (offsets.fnSetFactionRelation != 0)
        {
            typedef void(__fastcall* SetRelationFn)(int32_t, int32_t, int32_t);
            SetRelationFn setFn = reinterpret_cast<SetRelationFn>(offsets.fnSetFactionRelation);

            __try
            {
                setFn(faction1, faction2, relation);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Fall through to manual method
            }
        }

        // Manual method
        if (offsets.factionList == 0)
            return false;

        Faction** factions = ReadMemory<Faction**>(offsets.factionList);
        int32_t count = ReadMemory<int32_t>(offsets.factionCount);

        if (!factions || faction1 < 0 || faction1 >= count)
            return false;

        Faction* f1 = factions[faction1];
        if (!f1 || !f1->relations || faction2 >= f1->relationCount)
            return false;

        f1->relations[faction2] = relation;
        return true;
    }

    std::vector<Faction*> GameBridge::GetAllFactions()
    {
        std::vector<Faction*> result;

        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.factionList == 0)
            return result;

        Faction** factions = ReadMemory<Faction**>(offsets.factionList);
        int32_t count = ReadMemory<int32_t>(offsets.factionCount);

        if (!factions || count <= 0)
            return result;

        result.reserve(count);
        for (int32_t i = 0; i < count && i < 1000; i++)
        {
            if (factions[i])
            {
                result.push_back(factions[i]);
            }
        }

        return result;
    }

    Faction* GameBridge::GetPlayerFaction()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return nullptr;

        return m_gameWorld->playerFaction;
    }

    //=========================================================================
    // MOVEMENT AND COMMANDS
    //=========================================================================

    bool GameBridge::MoveCharacterTo(uint32_t characterId, const Vector3& position)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        // Use game's movement command function if available
        if (offsets.fnIssueCommand != 0)
        {
            typedef void(__fastcall* IssueCommandFn)(Character*, int32_t, const Vector3*);
            IssueCommandFn cmdFn = reinterpret_cast<IssueCommandFn>(offsets.fnIssueCommand);

            __try
            {
                cmdFn(c, static_cast<int32_t>(SquadOrder::Move), &position);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Fall through to manual method
            }
        }

        // Manual pathfinding setup
        if (c->aiController)
        {
            c->aiController->moveTarget = position;
            c->aiController->currentState = AIState::Moving;
        }

        c->currentState = AIState::Moving;
        return true;
    }

    bool GameBridge::SetCharacterPosition(uint32_t characterId, const Vector3& position)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        c->position = position;

        // Update scene node if available
        if (c->sceneNode)
        {
            c->sceneNode->localTransform.position = position;
        }

        return true;
    }

    bool GameBridge::SetCharacterRotation(uint32_t characterId, const Quaternion& rotation)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        c->rotation = rotation;
        return true;
    }

    bool GameBridge::SetCharacterState(uint32_t characterId, AIState state)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        // Use game function if available
        if (offsets.fnSetCharacterState != 0)
        {
            typedef void(__fastcall* SetStateFn)(Character*, int32_t);
            SetStateFn setFn = reinterpret_cast<SetStateFn>(offsets.fnSetCharacterState);

            __try
            {
                setFn(c, static_cast<int32_t>(state));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Fall through to manual
            }
        }

        c->currentState = state;
        if (c->aiController)
        {
            c->aiController->currentState = state;
        }

        return true;
    }

    bool GameBridge::IssueCommand(uint32_t characterId, SquadOrder command, const Vector3& target)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        if (offsets.fnIssueCommand != 0)
        {
            typedef void(__fastcall* IssueCommandFn)(Character*, int32_t, const Vector3*);
            IssueCommandFn cmdFn = reinterpret_cast<IssueCommandFn>(offsets.fnIssueCommand);

            __try
            {
                cmdFn(c, static_cast<int32_t>(command), &target);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Manual fallback
            }
        }

        // Manual command application
        if (c->aiController)
        {
            // Set up AI task based on command
            switch (command)
            {
            case SquadOrder::Move:
                c->aiController->moveTarget = target;
                c->aiController->currentState = AIState::Moving;
                break;
            case SquadOrder::Attack:
                c->aiController->currentState = AIState::Fighting;
                break;
            case SquadOrder::Hold:
                c->aiController->currentState = AIState::Idle;
                break;
            case SquadOrder::Follow:
                c->aiController->currentState = AIState::Following;
                break;
            case SquadOrder::Patrol:
                c->aiController->currentState = AIState::Patrolling;
                break;
            case SquadOrder::Guard:
                c->aiController->currentState = AIState::Guarding;
                break;
            default:
                break;
            }
        }

        return true;
    }

    //=========================================================================
    // COMBAT SYSTEM
    //=========================================================================

    void GameBridge::SetCombatCallback(CombatCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_combatCallback = callback;
    }

    bool GameBridge::ApplyCombatEvent(const CombatEventSync& event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* attacker = FindCharacter(event.attackerId);
        Character* defender = FindCharacter(event.defenderId);

        if (!defender)
            return false;

        // Apply damage
        if (!event.wasBlocked && !event.wasDodged)
        {
            ApplyDamage(event.defenderId, event.targetLimb, event.damage, event.damageType);
        }

        // Apply knockdown
        if (event.causedKnockdown && defender->animState)
        {
            defender->animState->currentAnim = AnimationType::KnockDown;
            defender->isUnconscious = 1;
        }

        // Update combat states
        if (attacker)
        {
            attacker->isInCombat = 1;
            attacker->combatTarget = defender;
        }
        defender->isInCombat = 1;

        return true;
    }

    bool GameBridge::SetCombatTarget(uint32_t attackerId, uint32_t targetId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* attacker = FindCharacter(attackerId);
        Character* target = FindCharacter(targetId);

        if (!attacker)
            return false;

        attacker->combatTarget = target;
        attacker->isInCombat = target != nullptr ? 1 : 0;

        if (attacker->aiController)
        {
            if (target)
            {
                attacker->aiController->currentState = AIState::Fighting;
            }
        }

        return true;
    }

    bool GameBridge::ApplyDamage(uint32_t characterId, LimbType limb, float damage, DamageType type)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->body)
            return false;

        int limbIndex = static_cast<int>(limb);
        if (limbIndex < 0 || limbIndex >= static_cast<int>(LimbType::LIMB_COUNT))
            return false;

        BodyPart& part = c->body->parts[limbIndex];

        // Apply damage
        float finalDamage = damage;

        // Check armor
        if (part.equippedArmor)
        {
            float armorReduction = 0.0f;
            switch (type)
            {
            case DamageType::Cut:
                armorReduction = part.equippedArmor->armor.cutResist;
                break;
            case DamageType::Blunt:
                armorReduction = part.equippedArmor->armor.bluntResist;
                break;
            case DamageType::Pierce:
                armorReduction = part.equippedArmor->armor.pierceResist;
                break;
            }
            finalDamage *= (1.0f - armorReduction);
        }

        part.health -= finalDamage;
        part.damage += finalDamage;

        // Start bleeding for cut damage
        if (type == DamageType::Cut && !c->body->parts[limbIndex].isMissing)
        {
            part.isBleeding = 1;
            part.bleedRate = damage * 0.1f;
        }

        // Update overall health
        c->health = 0;
        for (int i = 0; i < static_cast<int>(LimbType::LIMB_COUNT); i++)
        {
            c->health += c->body->parts[i].health;
        }
        c->health /= static_cast<float>(LimbType::LIMB_COUNT);

        // Check for knockout/death
        if (c->health <= 0 || c->body->parts[static_cast<int>(LimbType::Head)].health <= 0)
        {
            c->isDead = 1;
            c->currentState = AIState::Dead;
        }
        else if (c->bloodLevel <= 20.0f)
        {
            c->isUnconscious = 1;
            c->currentState = AIState::Unconscious;
        }

        return true;
    }

    //=========================================================================
    // INVENTORY SYSTEM
    //=========================================================================

    void GameBridge::SetInventoryCallback(InventoryCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inventoryCallback = callback;
    }

    bool GameBridge::AddItemToInventory(uint32_t characterId, uint32_t itemTemplateId, int32_t quantity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c)
            return false;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        if (offsets.fnInventoryAdd != 0)
        {
            typedef bool(__fastcall* AddItemFn)(Inventory*, uint32_t, int32_t);
            AddItemFn addFn = reinterpret_cast<AddItemFn>(offsets.fnInventoryAdd);

            __try
            {
                return addFn(c->inventory, itemTemplateId, quantity);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        return false;
    }

    bool GameBridge::RemoveItemFromInventory(uint32_t characterId, uint32_t itemId, int32_t quantity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->inventory)
            return false;

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();

        if (offsets.fnInventoryRemove != 0)
        {
            typedef bool(__fastcall* RemoveItemFn)(Inventory*, uint32_t, int32_t);
            RemoveItemFn removeFn = reinterpret_cast<RemoveItemFn>(offsets.fnInventoryRemove);

            __try
            {
                return removeFn(c->inventory, itemId, quantity);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        return false;
    }

    bool GameBridge::TransferItem(uint32_t fromCharId, uint32_t toCharId, uint32_t itemId, int32_t quantity)
    {
        if (RemoveItemFromInventory(fromCharId, itemId, quantity))
        {
            return AddItemToInventory(toCharId, itemId, quantity);
        }
        return false;
    }

    bool GameBridge::SetCharacterMoney(uint32_t characterId, int32_t amount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->inventory)
            return false;

        c->inventory->money = amount;
        return true;
    }

    //=========================================================================
    // ANIMATION SYSTEM
    //=========================================================================

    bool GameBridge::PlayAnimation(uint32_t characterId, AnimationType anim, bool force)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->animState)
            return false;

        if (!force && c->animState->isPlaying && !c->animState->canCancel)
        {
            return false;
        }

        c->animState->currentAnim = anim;
        c->animState->animTime = 0.0f;
        c->animState->isPlaying = 1;

        return true;
    }

    AnimationType GameBridge::GetCurrentAnimation(uint32_t characterId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->animState)
            return AnimationType::Idle;

        return c->animState->currentAnim;
    }

    bool GameBridge::SyncAnimation(uint32_t characterId, AnimationType anim, float time, float speed)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c = FindCharacter(characterId);
        if (!c || !c->animState)
            return false;

        c->animState->currentAnim = anim;
        c->animState->animTime = time;
        c->animState->animSpeed = speed;
        c->animState->isPlaying = 1;

        return true;
    }

    //=========================================================================
    // NPC MANAGEMENT
    //=========================================================================

    std::vector<PlayerState> GameBridge::GetNPCsInRange(const Vector3& position, float range)
    {
        std::vector<PlayerState> result;

        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.allCharactersList == 0)
            return result;

        Character** allChars = ReadMemory<Character**>(offsets.allCharactersList);
        int32_t count = ReadMemory<int32_t>(offsets.allCharactersCount);

        if (!allChars || count <= 0)
            return result;

        float rangeSq = range * range;

        for (int32_t i = 0; i < count && i < 10000; i++)
        {
            Character* c = allChars[i];
            if (!c || c->isPlayerControlled)
                continue;

            Vector3 diff = c->position - position;
            float distSq = diff.LengthSquared();

            if (distSq <= rangeSq)
            {
                PlayerState state;
                PopulatePlayerState(c, state);
                result.push_back(state);
            }
        }

        return result;
    }

    std::vector<Character*> GameBridge::GetAllNPCs()
    {
        std::vector<Character*> result;

        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.allCharactersList == 0)
            return result;

        Character** allChars = ReadMemory<Character**>(offsets.allCharactersList);
        int32_t count = ReadMemory<int32_t>(offsets.allCharactersCount);

        if (!allChars || count <= 0)
            return result;

        result.reserve(count);
        for (int32_t i = 0; i < count && i < 10000; i++)
        {
            if (allChars[i] && !allChars[i]->isPlayerControlled)
            {
                result.push_back(allChars[i]);
            }
        }

        return result;
    }

    int32_t GameBridge::GetNPCCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const GameOffsets& offsets = OffsetManager::Get().GetOffsets();
        if (offsets.allCharactersCount == 0)
            return 0;

        return ReadMemory<int32_t>(offsets.allCharactersCount);
    }

    //=========================================================================
    // BUILDING SYSTEM
    //=========================================================================

    std::vector<Building*> GameBridge::GetPlayerBuildings()
    {
        std::vector<Building*> result;

        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return result;

        Building** buildings = m_gameWorld->allBuildings;
        int32_t count = m_gameWorld->buildingCount;

        if (!buildings || count <= 0)
            return result;

        for (int32_t i = 0; i < count && i < 10000; i++)
        {
            if (buildings[i] && buildings[i]->isPlayerOwned)
            {
                result.push_back(buildings[i]);
            }
        }

        return result;
    }

    Building* GameBridge::GetBuilding(uint32_t buildingId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_isInitialized || !m_gameWorld)
            return nullptr;

        Building** buildings = m_gameWorld->allBuildings;
        int32_t count = m_gameWorld->buildingCount;

        if (!buildings || count <= 0)
            return nullptr;

        for (int32_t i = 0; i < count && i < 10000; i++)
        {
            if (buildings[i] && buildings[i]->buildingId == buildingId)
            {
                return buildings[i];
            }
        }

        return nullptr;
    }

    bool GameBridge::SetBuildingState(uint32_t buildingId, BuildingState state, float progress)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Building* b = GetBuilding(buildingId);
        if (!b)
            return false;

        b->state = state;
        b->constructionProgress = std::clamp(progress, 0.0f, 1.0f);

        return true;
    }

    //=========================================================================
    // UTILITY FUNCTIONS
    //=========================================================================

    void GameBridge::WorldToCell(const Vector3& worldPos, int32_t& cellX, int32_t& cellY)
    {
        // Kenshi uses 512 unit cells
        const float cellSize = 512.0f;
        cellX = static_cast<int32_t>(worldPos.x / cellSize);
        cellY = static_cast<int32_t>(worldPos.z / cellSize);  // Note: Z is the "horizontal" axis in Kenshi
    }

    Vector3 GameBridge::CellToWorld(int32_t cellX, int32_t cellY)
    {
        const float cellSize = 512.0f;
        return Vector3{
            cellX * cellSize + cellSize * 0.5f,
            0.0f,  // Y is vertical
            cellY * cellSize + cellSize * 0.5f
        };
    }

    float GameBridge::GetDistance(uint32_t char1, uint32_t char2)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        Character* c1 = FindCharacter(char1);
        Character* c2 = FindCharacter(char2);

        if (!c1 || !c2)
            return -1.0f;

        Vector3 diff = c1->position - c2->position;
        return diff.Length();
    }

    bool GameBridge::HasLineOfSight(const Vector3& from, const Vector3& to)
    {
        // This would require proper physics raycast
        // For now, return true (no obstruction check)
        // TODO: Implement using physics world raycast
        return true;
    }

} // namespace Kenshi

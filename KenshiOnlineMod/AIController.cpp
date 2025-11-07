#include "AIController.h"
#include <iostream>
#include <cmath>

namespace KenshiOnline
{
    AIController::AIController(OgreController* ogreController, AnimationController* animController)
        : m_OgreController(ogreController)
        , m_AnimController(animController)
        , m_Entity(nullptr)
        , m_CurrentState(AIState::Idle)
        , m_PreviousState(AIState::Idle)
        , m_Position(0, 0, 0)
        , m_Velocity(0, 0, 0)
        , m_HomePosition(0, 0, 0)
        , m_Destination(0, 0, 0)
        , m_Target(nullptr)
        , m_LastKnownTargetPosition(0, 0, 0)
        , m_CurrentPatrolIndex(0)
        , m_PatrolWaitTimer(0.0f)
        , m_Health(100.0f)
        , m_MaxHealth(100.0f)
        , m_StateTimer(0.0f)
        , m_AttackCooldown(0.0f)
        , m_SearchTimer(0.0f)
    {
    }

    AIController::~AIController()
    {
        Shutdown();
    }

    bool AIController::Initialize(OgreEntity* entity)
    {
        if (!entity || !m_OgreController || !m_AnimController)
            return false;

        m_Entity = entity;

        if (m_Entity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_Entity->sceneNode;
            m_Position = node->position;
            m_HomePosition = m_Position;
        }

        std::cout << "AI controller initialized\n";
        return true;
    }

    void AIController::Shutdown()
    {
        m_Entity = nullptr;
        m_Target = nullptr;
    }

    void AIController::SetState(AIState newState)
    {
        if (m_CurrentState == newState)
            return;

        m_PreviousState = m_CurrentState;
        m_CurrentState = newState;
        m_StateTimer = 0.0f;

        std::cout << "AI state changed to: " << static_cast<int>(newState) << "\n";
    }

    void AIController::SetBehaviorConfig(const AIBehaviorConfig& config)
    {
        m_Config = config;
    }

    void AIController::SetPatrolPath(const std::vector<PatrolPoint>& path)
    {
        m_PatrolPath = path;
        m_CurrentPatrolIndex = 0;
    }

    void AIController::AddPatrolPoint(const PatrolPoint& point)
    {
        m_PatrolPath.push_back(point);
    }

    void AIController::ClearPatrolPath()
    {
        m_PatrolPath.clear();
        m_CurrentPatrolIndex = 0;
    }

    void AIController::SetTarget(OgreEntity* target)
    {
        m_Target = target;

        if (target && target->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)target->sceneNode;
            m_LastKnownTargetPosition = node->position;
        }
    }

    void AIController::SetPosition(Vector3 position)
    {
        m_Position = position;

        if (m_Entity && m_Entity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_Entity->sceneNode;
            m_OgreController->Write<Vector3>((uintptr_t)&node->position, position);
            node->needsUpdate = 1;
        }
    }

    void AIController::TakeDamage(float damage)
    {
        m_Health -= damage;

        if (m_Health <= 0.0f)
        {
            m_Health = 0.0f;
            SetState(AIState::Dead);
            return;
        }

        // React to damage based on personality
        if (m_CurrentState == AIState::Idle || m_CurrentState == AIState::Patrol)
        {
            switch (m_Config.personality)
            {
                case AIPersonality::Passive:
                case AIPersonality::Coward:
                    SetState(AIState::Flee);
                    break;

                case AIPersonality::Defensive:
                case AIPersonality::Aggressive:
                case AIPersonality::Berserk:
                    SetState(AIState::Defend);
                    if (m_Config.canCallForHelp)
                        CallForHelp();
                    break;
            }
        }

        // Check if should flee due to low health
        float healthPercent = m_Health / m_MaxHealth;
        if (healthPercent <= m_Config.fleeHealthPercent)
        {
            if (m_Config.personality != AIPersonality::Berserk)
            {
                SetState(AIState::Flee);
            }
        }
    }

    void AIController::Update(float deltaTime)
    {
        if (!m_Entity)
            return;

        m_StateTimer += deltaTime;

        if (m_AttackCooldown > 0.0f)
            m_AttackCooldown -= deltaTime;

        // Update based on current state
        switch (m_CurrentState)
        {
            case AIState::Idle:
                UpdateIdle(deltaTime);
                break;
            case AIState::Patrol:
                UpdatePatrol(deltaTime);
                break;
            case AIState::Follow:
                UpdateFollow(deltaTime);
                break;
            case AIState::Attack:
                UpdateAttack(deltaTime);
                break;
            case AIState::Flee:
                UpdateFlee(deltaTime);
                break;
            case AIState::Defend:
                UpdateDefend(deltaTime);
                break;
            case AIState::SearchForEnemy:
                UpdateSearchForEnemy(deltaTime);
                break;
            case AIState::ReturnToPost:
                UpdateReturnToPost(deltaTime);
                break;
            case AIState::Dead:
                // Do nothing when dead
                break;
        }

        UpdateAnimation();
        ApplyMovement();
    }

    // State update implementations

    void AIController::UpdateIdle(float deltaTime)
    {
        // Check for enemies if aggressive
        if (m_Config.personality == AIPersonality::Aggressive ||
            m_Config.personality == AIPersonality::Berserk)
        {
            OgreEntity* enemy = FindNearestEnemy();
            if (enemy)
            {
                SetTarget(enemy);
                SetState(AIState::Attack);
                return;
            }
        }

        // Start patrol if we have a patrol path
        if (!m_PatrolPath.empty() && m_StateTimer > 2.0f)
        {
            SetState(AIState::Patrol);
        }
    }

    void AIController::UpdatePatrol(float deltaTime)
    {
        if (m_PatrolPath.empty())
        {
            SetState(AIState::Idle);
            return;
        }

        // Check for enemies if aggressive
        if (m_Config.personality == AIPersonality::Aggressive ||
            m_Config.personality == AIPersonality::Berserk)
        {
            OgreEntity* enemy = FindNearestEnemy();
            if (enemy)
            {
                SetTarget(enemy);
                SetState(AIState::Attack);
                return;
            }
        }

        // Move to current patrol point
        PatrolPoint& point = m_PatrolPath[m_CurrentPatrolIndex];

        if (IsAtPosition(point.position, 1.0f))
        {
            // Wait at patrol point
            m_PatrolWaitTimer += deltaTime;

            if (m_PatrolWaitTimer >= point.waitTime)
            {
                m_PatrolWaitTimer = 0.0f;
                m_CurrentPatrolIndex = (m_CurrentPatrolIndex + 1) % m_PatrolPath.size();
            }
        }
        else
        {
            MoveTo(point.position, m_Config.patrolSpeed, deltaTime);
        }
    }

    void AIController::UpdateFollow(float deltaTime)
    {
        if (!m_Target || !m_Target->sceneNode)
        {
            SetState(AIState::Idle);
            return;
        }

        OgreSceneNode* targetNode = (OgreSceneNode*)m_Target->sceneNode;
        Vector3 targetPos = targetNode->position;

        float distance = GetDistanceToTarget();

        if (distance > m_Config.followDistance)
        {
            MoveTo(targetPos, m_Config.combatSpeed, deltaTime);
        }
    }

    void AIController::UpdateAttack(float deltaTime)
    {
        if (!m_Target)
        {
            SetState(AIState::SearchForEnemy);
            return;
        }

        if (!CanSeeTarget())
        {
            SetState(AIState::SearchForEnemy);
            return;
        }

        if (IsInAttackRange())
        {
            // Stop and attack
            m_Velocity = Vector3(0, 0, 0);

            if (m_AttackCooldown <= 0.0f)
            {
                PerformAttack();
                m_AttackCooldown = 2.0f; // Attack every 2 seconds
            }
        }
        else
        {
            // Move towards target
            if (m_Target->sceneNode)
            {
                OgreSceneNode* targetNode = (OgreSceneNode*)m_Target->sceneNode;
                MoveTo(targetNode->position, m_Config.combatSpeed, deltaTime);
            }
        }
    }

    void AIController::UpdateFlee(float deltaTime)
    {
        // Run away from target
        if (m_Target && m_Target->sceneNode)
        {
            OgreSceneNode* targetNode = (OgreSceneNode*)m_Target->sceneNode;
            Vector3 targetPos = targetNode->position;

            // Calculate flee direction (opposite of target)
            Vector3 fleeDir;
            fleeDir.x = m_Position.x - targetPos.x;
            fleeDir.z = m_Position.z - targetPos.z;

            float length = sqrtf(fleeDir.x * fleeDir.x + fleeDir.z * fleeDir.z);
            if (length > 0.001f)
            {
                fleeDir.x /= length;
                fleeDir.z /= length;
            }

            Vector3 fleePos;
            fleePos.x = m_Position.x + fleeDir.x * 20.0f;
            fleePos.y = m_Position.y;
            fleePos.z = m_Position.z + fleeDir.z * 20.0f;

            MoveTo(fleePos, m_Config.combatSpeed * 1.2f, deltaTime);
        }

        // Stop fleeing after getting far enough
        if (GetDistanceToTarget() > m_Config.sightRange * 2.0f || m_StateTimer > 10.0f)
        {
            SetState(AIState::ReturnToPost);
        }
    }

    void AIController::UpdateDefend(float deltaTime)
    {
        // Similar to attack but stays near home position
        if (!m_Target)
        {
            SetState(AIState::ReturnToPost);
            return;
        }

        float distanceFromHome = sqrtf(
            (m_Position.x - m_HomePosition.x) * (m_Position.x - m_HomePosition.x) +
            (m_Position.z - m_HomePosition.z) * (m_Position.z - m_HomePosition.z)
        );

        if (distanceFromHome > m_Config.sightRange)
        {
            SetState(AIState::ReturnToPost);
            return;
        }

        UpdateAttack(deltaTime);
    }

    void AIController::UpdateSearchForEnemy(float deltaTime)
    {
        m_SearchTimer += deltaTime;

        // Search for 5 seconds
        if (m_SearchTimer > 5.0f)
        {
            m_SearchTimer = 0.0f;
            SetState(AIState::ReturnToPost);
            return;
        }

        // Try to find enemy again
        OgreEntity* enemy = FindNearestEnemy();
        if (enemy)
        {
            SetTarget(enemy);
            SetState(AIState::Attack);
            return;
        }

        // Move to last known position
        MoveTo(m_LastKnownTargetPosition, m_Config.combatSpeed, deltaTime);
    }

    void AIController::UpdateReturnToPost(float deltaTime)
    {
        if (IsAtPosition(m_HomePosition, 2.0f))
        {
            if (!m_PatrolPath.empty())
                SetState(AIState::Patrol);
            else
                SetState(AIState::Idle);

            return;
        }

        MoveTo(m_HomePosition, m_Config.patrolSpeed, deltaTime);
    }

    // Helper functions

    void AIController::MoveTo(Vector3 destination, float speed, float deltaTime)
    {
        m_Destination = destination;

        Vector3 direction;
        direction.x = destination.x - m_Position.x;
        direction.y = 0;
        direction.z = destination.z - m_Position.z;

        float distance = sqrtf(direction.x * direction.x + direction.z * direction.z);

        if (distance > 0.1f)
        {
            direction.x /= distance;
            direction.z /= distance;

            m_Velocity.x = direction.x * speed;
            m_Velocity.z = direction.z * speed;

            m_Position.x += m_Velocity.x * deltaTime;
            m_Position.z += m_Velocity.z * deltaTime;
        }
    }

    bool AIController::IsAtPosition(Vector3 position, float threshold)
    {
        float dx = position.x - m_Position.x;
        float dz = position.z - m_Position.z;
        float distSq = dx * dx + dz * dz;

        return distSq <= (threshold * threshold);
    }

    OgreEntity* AIController::FindNearestEnemy()
    {
        // In a real implementation, would query nearby entities
        // For now, return nullptr
        return nullptr;
    }

    bool AIController::CanSeeTarget()
    {
        if (!m_Target)
            return false;

        float distance = GetDistanceToTarget();
        return distance <= m_Config.sightRange;
    }

    bool AIController::IsInAttackRange()
    {
        if (!m_Target)
            return false;

        float distance = GetDistanceToTarget();
        return distance <= m_Config.attackRange;
    }

    void AIController::PerformAttack()
    {
        if (!m_Entity || !m_AnimController)
            return;

        std::cout << "AI performing attack!\n";
        m_AnimController->SetCharacterState(m_Entity, AnimationType::Combat_Attack);

        // TODO: Calculate and apply damage
    }

    void AIController::CallForHelp()
    {
        std::cout << "AI calling for help! Squad: " << m_Config.squadID << "\n";
        // TODO: Notify squad members
    }

    float AIController::GetDistanceToTarget()
    {
        if (!m_Target || !m_Target->sceneNode)
            return 9999.0f;

        OgreSceneNode* targetNode = (OgreSceneNode*)m_Target->sceneNode;
        Vector3 targetPos = targetNode->position;

        float dx = targetPos.x - m_Position.x;
        float dz = targetPos.z - m_Position.z;

        return sqrtf(dx * dx + dz * dz);
    }

    void AIController::UpdateAnimation()
    {
        if (!m_AnimController || !m_Entity)
            return;

        if (m_CurrentState == AIState::Dead)
        {
            m_AnimController->SetCharacterState(m_Entity, AnimationType::Death);
            return;
        }

        if (m_CurrentState == AIState::Attack || m_CurrentState == AIState::Defend)
        {
            if (m_AttackCooldown > 1.5f)
            {
                // Attacking
                return; // Already set in PerformAttack
            }
            else
            {
                m_AnimController->SetCharacterState(m_Entity, AnimationType::Combat_Idle);
            }
            return;
        }

        // Movement animations
        float speed = sqrtf(m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z);

        if (speed < 0.1f)
        {
            m_AnimController->SetCharacterState(m_Entity, AnimationType::Idle);
        }
        else if (m_CurrentState == AIState::Flee)
        {
            m_AnimController->SetCharacterState(m_Entity, AnimationType::Sprint);
        }
        else if (speed > 4.0f)
        {
            m_AnimController->SetCharacterState(m_Entity, AnimationType::Run);
        }
        else
        {
            m_AnimController->SetCharacterState(m_Entity, AnimationType::Walk);
        }
    }

    void AIController::ApplyMovement()
    {
        if (!m_Entity || !m_Entity->sceneNode)
            return;

        OgreSceneNode* node = (OgreSceneNode*)m_Entity->sceneNode;
        m_OgreController->Write<Vector3>((uintptr_t)&node->position, m_Position);
        node->needsUpdate = 1;
    }
}

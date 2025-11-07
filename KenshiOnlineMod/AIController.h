#pragma once

#include "OgreController.h"
#include "AnimationController.h"
#include <vector>
#include <unordered_map>

namespace KenshiOnline
{
    // AI states
    enum class AIState
    {
        Idle,
        Patrol,
        Follow,
        Attack,
        Flee,
        Defend,
        SearchForEnemy,
        ReturnToPost,
        Dead
    };

    // AI personality types
    enum class AIPersonality
    {
        Passive,    // Runs away from combat
        Defensive,  // Only fights when attacked
        Aggressive, // Attacks enemies on sight
        Berserk,    // Attacks everything
        Coward      // Flees at low health
    };

    // Patrol point
    struct PatrolPoint
    {
        Vector3 position;
        float waitTime; // How long to wait at this point
    };

    // AI behavior configuration
    struct AIBehaviorConfig
    {
        AIPersonality personality;
        float sightRange;
        float attackRange;
        float fleeHealthPercent;
        float followDistance;
        float patrolSpeed;
        float combatSpeed;
        bool canCallForHelp;
        int squadID;

        AIBehaviorConfig()
            : personality(AIPersonality::Defensive)
            , sightRange(20.0f)
            , attackRange(2.0f)
            , fleeHealthPercent(0.2f)
            , followDistance(2.0f)
            , patrolSpeed(2.0f)
            , combatSpeed(5.0f)
            , canCallForHelp(true)
            , squadID(-1)
        {}
    };

    // AI controller - manages NPC behavior
    class AIController
    {
    public:
        AIController(OgreController* ogreController, AnimationController* animController);
        ~AIController();

        // Initialization
        bool Initialize(OgreEntity* entity);
        void Shutdown();

        // State management
        void SetState(AIState newState);
        AIState GetState() const { return m_CurrentState; }

        // Behavior configuration
        void SetBehaviorConfig(const AIBehaviorConfig& config);
        AIBehaviorConfig GetBehaviorConfig() const { return m_Config; }

        // Patrol
        void SetPatrolPath(const std::vector<PatrolPoint>& path);
        void AddPatrolPoint(const PatrolPoint& point);
        void ClearPatrolPath();

        // Target management
        void SetTarget(OgreEntity* target);
        OgreEntity* GetTarget() const { return m_Target; }

        // Position and movement
        void SetPosition(Vector3 position);
        Vector3 GetPosition() const { return m_Position; }
        void SetHome(Vector3 homePosition) { m_HomePosition = homePosition; }

        // Health
        void SetHealth(float health) { m_Health = health; }
        float GetHealth() const { return m_Health; }
        void TakeDamage(float damage);

        // Squad
        void SetSquad(int squadID) { m_Config.squadID = squadID; }
        int GetSquad() const { return m_Config.squadID; }

        // Update
        void Update(float deltaTime);

    private:
        OgreController* m_OgreController;
        AnimationController* m_AnimController;

        OgreEntity* m_Entity;
        AIState m_CurrentState;
        AIState m_PreviousState;
        AIBehaviorConfig m_Config;

        // Position and movement
        Vector3 m_Position;
        Vector3 m_Velocity;
        Vector3 m_HomePosition;
        Vector3 m_Destination;

        // Target
        OgreEntity* m_Target;
        Vector3 m_LastKnownTargetPosition;

        // Patrol
        std::vector<PatrolPoint> m_PatrolPath;
        int m_CurrentPatrolIndex;
        float m_PatrolWaitTimer;

        // Health
        float m_Health;
        float m_MaxHealth;

        // Timers
        float m_StateTimer;
        float m_AttackCooldown;
        float m_SearchTimer;

        // State update functions
        void UpdateIdle(float deltaTime);
        void UpdatePatrol(float deltaTime);
        void UpdateFollow(float deltaTime);
        void UpdateAttack(float deltaTime);
        void UpdateFlee(float deltaTime);
        void UpdateDefend(float deltaTime);
        void UpdateSearchForEnemy(float deltaTime);
        void UpdateReturnToPost(float deltaTime);

        // Helper functions
        void MoveTo(Vector3 destination, float speed, float deltaTime);
        bool IsAtPosition(Vector3 position, float threshold = 1.0f);
        OgreEntity* FindNearestEnemy();
        bool CanSeeTarget();
        bool IsInAttackRange();
        void PerformAttack();
        void CallForHelp();
        float GetDistanceToTarget();
        void UpdateAnimation();
        void ApplyMovement();
    };
}

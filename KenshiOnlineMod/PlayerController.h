#pragma once

#include "OgreController.h"
#include "AnimationController.h"
#include <Windows.h>

namespace KenshiOnline
{
    // Input state
    struct InputState
    {
        bool moveForward;
        bool moveBackward;
        bool moveLeft;
        bool moveRight;
        bool jump;
        bool crouch;
        bool sprint;
        bool attack;
        bool block;
        bool interact;

        float mouseX;
        float mouseY;
        bool mouseLeftClick;
        bool mouseRightClick;

        InputState()
        {
            memset(this, 0, sizeof(InputState));
        }
    };

    // Player movement state
    enum class MovementState
    {
        Idle,
        Walking,
        Running,
        Sprinting,
        Crouching,
        Jumping,
        Falling
    };

    // Player controller - handles local player input and movement
    class PlayerController
    {
    public:
        PlayerController(OgreController* ogreController, AnimationController* animController);
        ~PlayerController();

        // Initialization
        bool Initialize(OgreEntity* playerEntity, OgreCamera* camera);
        void Shutdown();

        // Input processing
        void ProcessInput(float deltaTime);
        void SetInputState(const InputState& state);
        InputState GetInputState() const { return m_InputState; }

        // Movement
        void UpdateMovement(float deltaTime);
        void SetMovementSpeed(float walk, float run, float sprint);
        void SetPosition(Vector3 position);
        Vector3 GetPosition() const;
        Vector3 GetVelocity() const { return m_Velocity; }

        // Camera control
        void UpdateCamera(float deltaTime);
        void SetCameraMode(int mode); // 0=First person, 1=Third person, 2=Free cam
        int GetCameraMode() const { return m_CameraMode; }
        void SetCameraDistance(float distance);

        // Combat
        void PerformAttack();
        void PerformBlock(bool active);
        bool IsInCombat() const { return m_InCombat; }
        void SetCombatMode(bool combat);

        // Interaction
        void Interact();
        OgreEntity* GetTargetEntity() const { return m_TargetEntity; }

        // State queries
        MovementState GetMovementState() const { return m_MovementState; }
        bool IsGrounded() const { return m_IsGrounded; }

        // Network sync
        struct PlayerSyncData
        {
            Vector3 position;
            Quaternion rotation;
            Vector3 velocity;
            MovementState movementState;
            bool inCombat;
            bool isBlocking;
            std::string currentAnimation;
        };

        PlayerSyncData GetSyncData() const;
        void ApplySyncData(const PlayerSyncData& data);

        // Update
        void Update(float deltaTime);

    private:
        OgreController* m_OgreController;
        AnimationController* m_AnimController;

        OgreEntity* m_PlayerEntity;
        OgreCamera* m_Camera;

        InputState m_InputState;
        Vector3 m_Position;
        Quaternion m_Rotation;
        Vector3 m_Velocity;

        MovementState m_MovementState;
        bool m_IsGrounded;
        bool m_InCombat;
        bool m_IsBlocking;

        // Movement speeds
        float m_WalkSpeed;
        float m_RunSpeed;
        float m_SprintSpeed;
        float m_CurrentSpeed;

        // Camera settings
        int m_CameraMode; // 0=FPS, 1=TPS, 2=Free
        float m_CameraDistance;
        float m_CameraPitch;
        float m_CameraYaw;

        // Interaction
        OgreEntity* m_TargetEntity;
        float m_InteractionRange;

        // Helper functions
        void UpdateRotation(float deltaTime);
        void ApplyMovement(float deltaTime);
        void UpdateAnimation();
        void CheckGround();
        void UpdateTarget();
        Vector3 GetMovementDirection() const;
    };
}

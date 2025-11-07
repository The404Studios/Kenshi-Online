#include "PlayerController.h"
#include <iostream>
#include <cmath>

#define PI 3.14159265358979323846f

namespace KenshiOnline
{
    PlayerController::PlayerController(OgreController* ogreController, AnimationController* animController)
        : m_OgreController(ogreController)
        , m_AnimController(animController)
        , m_PlayerEntity(nullptr)
        , m_Camera(nullptr)
        , m_Position(0, 0, 0)
        , m_Rotation(1, 0, 0, 0)
        , m_Velocity(0, 0, 0)
        , m_MovementState(MovementState::Idle)
        , m_IsGrounded(true)
        , m_InCombat(false)
        , m_IsBlocking(false)
        , m_WalkSpeed(2.5f)
        , m_RunSpeed(5.0f)
        , m_SprintSpeed(8.0f)
        , m_CurrentSpeed(0.0f)
        , m_CameraMode(1) // Third person by default
        , m_CameraDistance(5.0f)
        , m_CameraPitch(20.0f)
        , m_CameraYaw(0.0f)
        , m_TargetEntity(nullptr)
        , m_InteractionRange(3.0f)
    {
    }

    PlayerController::~PlayerController()
    {
        Shutdown();
    }

    bool PlayerController::Initialize(OgreEntity* playerEntity, OgreCamera* camera)
    {
        if (!playerEntity || !m_OgreController || !m_AnimController)
            return false;

        m_PlayerEntity = playerEntity;
        m_Camera = camera;

        if (m_PlayerEntity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_PlayerEntity->sceneNode;
            m_Position = node->position;
            m_Rotation = node->orientation;
        }

        std::cout << "Player controller initialized\n";
        return true;
    }

    void PlayerController::Shutdown()
    {
        m_PlayerEntity = nullptr;
        m_Camera = nullptr;
    }

    void PlayerController::ProcessInput(float deltaTime)
    {
        if (!m_PlayerEntity)
            return;

        // Handle input to update movement state
        bool hasMovementInput = m_InputState.moveForward || m_InputState.moveBackward ||
                                m_InputState.moveLeft || m_InputState.moveRight;

        if (!hasMovementInput)
        {
            if (m_MovementState != MovementState::Idle)
            {
                m_MovementState = MovementState::Idle;
                m_CurrentSpeed = 0.0f;
            }
        }
        else
        {
            // Determine movement speed based on input
            if (m_InputState.sprint)
            {
                m_MovementState = MovementState::Sprinting;
                m_CurrentSpeed = m_SprintSpeed;
            }
            else if (m_InputState.crouch)
            {
                m_MovementState = MovementState::Crouching;
                m_CurrentSpeed = m_WalkSpeed * 0.5f;
            }
            else
            {
                // Default is running in Kenshi
                m_MovementState = MovementState::Running;
                m_CurrentSpeed = m_RunSpeed;
            }
        }

        // Handle jump
        if (m_InputState.jump && m_IsGrounded)
        {
            m_MovementState = MovementState::Jumping;
            m_Velocity.y = 5.0f; // Jump velocity
            m_IsGrounded = false;
        }

        // Handle combat input
        if (m_InputState.attack)
        {
            PerformAttack();
        }

        if (m_InputState.block)
        {
            PerformBlock(true);
        }
        else if (m_IsBlocking)
        {
            PerformBlock(false);
        }

        if (m_InputState.interact)
        {
            Interact();
        }
    }

    void PlayerController::SetInputState(const InputState& state)
    {
        m_InputState = state;
    }

    void PlayerController::UpdateMovement(float deltaTime)
    {
        if (!m_PlayerEntity)
            return;

        // Get movement direction from input
        Vector3 moveDir = GetMovementDirection();

        // Apply velocity
        if (m_CurrentSpeed > 0.0f)
        {
            m_Velocity.x = moveDir.x * m_CurrentSpeed;
            m_Velocity.z = moveDir.z * m_CurrentSpeed;
        }
        else
        {
            // Decelerate
            m_Velocity.x *= 0.9f;
            m_Velocity.z *= 0.9f;
        }

        // Apply gravity
        if (!m_IsGrounded)
        {
            m_Velocity.y -= 9.8f * deltaTime; // Gravity
        }

        // Update position
        m_Position.x += m_Velocity.x * deltaTime;
        m_Position.y += m_Velocity.y * deltaTime;
        m_Position.z += m_Velocity.z * deltaTime;

        // Apply to scene node
        ApplyMovement(deltaTime);

        // Check ground
        CheckGround();

        // Update rotation based on movement
        UpdateRotation(deltaTime);

        // Update animation
        UpdateAnimation();
    }

    void PlayerController::SetMovementSpeed(float walk, float run, float sprint)
    {
        m_WalkSpeed = walk;
        m_RunSpeed = run;
        m_SprintSpeed = sprint;
    }

    void PlayerController::SetPosition(Vector3 position)
    {
        m_Position = position;

        if (m_PlayerEntity && m_PlayerEntity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_PlayerEntity->sceneNode;
            m_OgreController->Write<Vector3>((uintptr_t)&node->position, position);
            node->needsUpdate = 1;
        }
    }

    Vector3 PlayerController::GetPosition() const
    {
        return m_Position;
    }

    void PlayerController::UpdateCamera(float deltaTime)
    {
        if (!m_Camera)
            return;

        // Update camera yaw and pitch from mouse input
        m_CameraYaw += m_InputState.mouseX * 0.1f;
        m_CameraPitch -= m_InputState.mouseY * 0.1f;

        // Clamp pitch
        if (m_CameraPitch > 89.0f) m_CameraPitch = 89.0f;
        if (m_CameraPitch < -89.0f) m_CameraPitch = -89.0f;

        Vector3 cameraPos = m_Position;

        if (m_CameraMode == 0) // First person
        {
            cameraPos.y += 1.7f; // Eye level
        }
        else if (m_CameraMode == 1) // Third person
        {
            // Calculate camera position behind and above player
            float yawRad = m_CameraYaw * PI / 180.0f;
            float pitchRad = m_CameraPitch * PI / 180.0f;

            cameraPos.x -= sinf(yawRad) * cosf(pitchRad) * m_CameraDistance;
            cameraPos.y += sinf(pitchRad) * m_CameraDistance + 1.7f;
            cameraPos.z -= cosf(yawRad) * cosf(pitchRad) * m_CameraDistance;
        }

        m_OgreController->SetCameraPosition(m_Camera, cameraPos);

        // Look at player
        if (m_CameraMode == 1)
        {
            Vector3 lookAt = m_Position;
            lookAt.y += 1.0f; // Look at torso
            m_OgreController->SetCameraTarget(m_Camera, lookAt);
        }
    }

    void PlayerController::SetCameraMode(int mode)
    {
        m_CameraMode = mode;
        std::cout << "Camera mode set to: " << mode << "\n";
    }

    void PlayerController::SetCameraDistance(float distance)
    {
        m_CameraDistance = distance;
    }

    void PlayerController::PerformAttack()
    {
        if (!m_InCombat || !m_AnimController || !m_PlayerEntity)
            return;

        std::cout << "Player attacking!\n";

        // Play attack animation
        m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Combat_Attack);

        // TODO: Send attack command to server
    }

    void PlayerController::PerformBlock(bool active)
    {
        m_IsBlocking = active;

        if (!m_AnimController || !m_PlayerEntity)
            return;

        if (active && !m_InCombat)
        {
            // Entering block puts us in combat mode
            SetCombatMode(true);
        }

        if (m_InCombat)
        {
            if (active)
            {
                m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Combat_Block);
            }
            else
            {
                m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Combat_Idle);
            }
        }
    }

    void PlayerController::SetCombatMode(bool combat)
    {
        if (m_InCombat == combat)
            return;

        m_InCombat = combat;

        if (!m_AnimController || !m_PlayerEntity)
            return;

        if (combat)
        {
            m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Combat_Idle);
        }
        else
        {
            m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Idle);
        }

        std::cout << "Combat mode: " << (combat ? "ON" : "OFF") << "\n";
    }

    void PlayerController::Interact()
    {
        if (!m_PlayerEntity)
            return;

        UpdateTarget();

        if (m_TargetEntity)
        {
            std::cout << "Interacting with: " << m_TargetEntity->name << "\n";

            if (m_AnimController)
            {
                m_AnimController->SetCharacterState(m_PlayerEntity, AnimationType::Interact);
            }

            // TODO: Send interaction to server
        }
    }

    PlayerController::PlayerSyncData PlayerController::GetSyncData() const
    {
        PlayerSyncData data;
        data.position = m_Position;
        data.rotation = m_Rotation;
        data.velocity = m_Velocity;
        data.movementState = m_MovementState;
        data.inCombat = m_InCombat;
        data.isBlocking = m_IsBlocking;

        // Get current animation
        if (m_AnimController && m_PlayerEntity)
        {
            auto animInfo = m_AnimController->GetSyncData(m_PlayerEntity);
            data.currentAnimation = animInfo.animationName;
        }

        return data;
    }

    void PlayerController::ApplySyncData(const PlayerSyncData& data)
    {
        m_Position = data.position;
        m_Rotation = data.rotation;
        m_Velocity = data.velocity;
        m_MovementState = data.movementState;
        m_InCombat = data.inCombat;
        m_IsBlocking = data.isBlocking;

        // Apply to scene node
        if (m_PlayerEntity && m_PlayerEntity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_PlayerEntity->sceneNode;
            m_OgreController->Write<Vector3>((uintptr_t)&node->position, m_Position);
            m_OgreController->Write<Quaternion>((uintptr_t)&node->orientation, m_Rotation);
            node->needsUpdate = 1;
        }

        // Apply animation
        if (m_AnimController && m_PlayerEntity && !data.currentAnimation.empty())
        {
            m_AnimController->PlayAnimation(m_PlayerEntity, data.currentAnimation, true);
        }
    }

    void PlayerController::Update(float deltaTime)
    {
        ProcessInput(deltaTime);
        UpdateMovement(deltaTime);
        UpdateCamera(deltaTime);
    }

    // Private helper functions

    void PlayerController::UpdateRotation(float deltaTime)
    {
        if (m_Velocity.x == 0.0f && m_Velocity.z == 0.0f)
            return;

        // Calculate rotation from velocity direction
        float angle = atan2f(m_Velocity.x, m_Velocity.z);

        // Convert to quaternion (simplified - should use proper quaternion math)
        m_Rotation.w = cosf(angle / 2.0f);
        m_Rotation.y = sinf(angle / 2.0f);

        if (m_PlayerEntity && m_PlayerEntity->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)m_PlayerEntity->sceneNode;
            m_OgreController->Write<Quaternion>((uintptr_t)&node->orientation, m_Rotation);
            node->needsUpdate = 1;
        }
    }

    void PlayerController::ApplyMovement(float deltaTime)
    {
        if (!m_PlayerEntity || !m_PlayerEntity->sceneNode)
            return;

        OgreSceneNode* node = (OgreSceneNode*)m_PlayerEntity->sceneNode;
        m_OgreController->Write<Vector3>((uintptr_t)&node->position, m_Position);
        node->needsUpdate = 1;
    }

    void PlayerController::UpdateAnimation()
    {
        if (!m_AnimController || !m_PlayerEntity)
            return;

        // Don't override combat or special animations
        if (m_InCombat || m_MovementState == MovementState::Jumping)
            return;

        // Set animation based on movement state
        AnimationType animType = AnimationType::Idle;

        switch (m_MovementState)
        {
            case MovementState::Idle:
                animType = AnimationType::Idle;
                break;
            case MovementState::Walking:
                animType = AnimationType::Walk;
                break;
            case MovementState::Running:
                animType = AnimationType::Run;
                break;
            case MovementState::Sprinting:
                animType = AnimationType::Sprint;
                break;
            case MovementState::Crouching:
                animType = AnimationType::Sneak;
                break;
            default:
                break;
        }

        m_AnimController->SetCharacterState(m_PlayerEntity, animType);
    }

    void PlayerController::CheckGround()
    {
        // Simple ground check - in real implementation would raycast down
        if (m_Position.y <= 0.0f)
        {
            m_Position.y = 0.0f;
            m_Velocity.y = 0.0f;
            m_IsGrounded = true;

            if (m_MovementState == MovementState::Falling)
            {
                m_MovementState = MovementState::Idle;
            }
        }
        else if (m_Velocity.y < 0.0f)
        {
            m_MovementState = MovementState::Falling;
            m_IsGrounded = false;
        }
    }

    void PlayerController::UpdateTarget()
    {
        if (!m_OgreController || !m_Camera)
        {
            m_TargetEntity = nullptr;
            return;
        }

        // Raycast from camera to find target
        Vector3 cameraForward(0, 0, -1); // Simplified - should get actual camera direction

        m_TargetEntity = m_OgreController->RaycastFromCamera(m_Camera, cameraForward, m_InteractionRange);
    }

    Vector3 PlayerController::GetMovementDirection() const
    {
        Vector3 dir(0, 0, 0);

        // Calculate movement direction based on input
        if (m_InputState.moveForward)
            dir.z -= 1.0f;
        if (m_InputState.moveBackward)
            dir.z += 1.0f;
        if (m_InputState.moveLeft)
            dir.x -= 1.0f;
        if (m_InputState.moveRight)
            dir.x += 1.0f;

        // Normalize
        float length = sqrtf(dir.x * dir.x + dir.z * dir.z);
        if (length > 0.001f)
        {
            dir.x /= length;
            dir.z /= length;
        }

        // Rotate by camera yaw for camera-relative movement
        float yawRad = m_CameraYaw * PI / 180.0f;
        float cosYaw = cosf(yawRad);
        float sinYaw = sinf(yawRad);

        Vector3 rotated;
        rotated.x = dir.x * cosYaw - dir.z * sinYaw;
        rotated.y = 0;
        rotated.z = dir.x * sinYaw + dir.z * cosYaw;

        return rotated;
    }
}

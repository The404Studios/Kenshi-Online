#include "OgreController.h"
#include <iostream>
#include <cmath>

namespace KenshiOnline
{
    // OGRE memory offsets (reverse engineered)
    namespace OgreOffsets
    {
        constexpr uintptr_t SceneManager = 0x2510A00;
        constexpr uintptr_t RenderSystem = 0x2510B20;
        constexpr uintptr_t Root = 0x2510000;
        constexpr uintptr_t MainCamera = 0x2511200;
    }

    OgreController::OgreController()
        : m_Initialized(false)
        , m_SceneManager(nullptr)
        , m_RenderSystem(nullptr)
        , m_Root(nullptr)
    {
    }

    OgreController::~OgreController()
    {
        Shutdown();
    }

    bool OgreController::Initialize()
    {
        if (m_Initialized)
            return true;

        try
        {
            uintptr_t base = GetKenshiBase();

            // Get OGRE root pointers
            m_Root = Read<void*>(base + OgreOffsets::Root);
            m_SceneManager = Read<void*>(base + OgreOffsets::SceneManager);
            m_RenderSystem = Read<void*>(base + OgreOffsets::RenderSystem);

            if (!m_Root || !m_SceneManager)
            {
                std::cout << "Failed to initialize OGRE controller - pointers null\n";
                return false;
            }

            m_Initialized = true;
            std::cout << "OGRE Controller initialized successfully\n";
            return true;
        }
        catch (...)
        {
            std::cout << "Exception during OGRE controller initialization\n";
            return false;
        }
    }

    void OgreController::Shutdown()
    {
        m_SceneNodes.clear();
        m_Entities.clear();
        m_Cameras.clear();
        m_BlendStates.clear();
        m_Initialized = false;
    }

    OgreSceneNode* OgreController::GetSceneNode(const char* name)
    {
        auto it = m_SceneNodes.find(name);
        if (it != m_SceneNodes.end())
            return it->second;
        return nullptr;
    }

    OgreEntity* OgreController::GetEntity(const char* name)
    {
        auto it = m_Entities.find(name);
        if (it != m_Entities.end())
            return it->second;
        return nullptr;
    }

    OgreSceneNode* OgreController::CreateSceneNode(const char* name, Vector3 position)
    {
        // In real implementation, this would call OGRE's SceneManager::createSceneNode
        // For now, we track the scene node
        OgreSceneNode* node = new OgreSceneNode();
        node->name = _strdup(name);
        node->position = position;
        node->scale = Vector3(1, 1, 1);
        node->needsUpdate = 1;
        node->visible = 1;

        m_SceneNodes[name] = node;
        return node;
    }

    OgreEntity* OgreController::CreateEntity(const char* name, const char* meshName)
    {
        // In real implementation, this would call OGRE's SceneManager::createEntity
        OgreEntity* entity = new OgreEntity();
        entity->name = _strdup(name);
        entity->visible = 1;
        entity->castShadows = 1;

        m_Entities[name] = entity;
        return entity;
    }

    void OgreController::DestroySceneNode(const char* name)
    {
        auto it = m_SceneNodes.find(name);
        if (it != m_SceneNodes.end())
        {
            if (it->second->name)
                free(it->second->name);
            delete it->second;
            m_SceneNodes.erase(it);
        }
    }

    void OgreController::DestroyEntity(const char* name)
    {
        auto it = m_Entities.find(name);
        if (it != m_Entities.end())
        {
            if (it->second->name)
                free(it->second->name);
            delete it->second;
            m_Entities.erase(it);
        }
    }

    OgreAnimationState* OgreController::GetAnimationState(OgreEntity* entity, const char* animName)
    {
        if (!entity || !entity->animationState)
            return nullptr;

        // In real implementation, this would query the animation state set
        OgreAnimationState* animState = (OgreAnimationState*)entity->animationState;

        if (animState->animationName && strcmp(animState->animationName, animName) == 0)
            return animState;

        return nullptr;
    }

    void OgreController::PlayAnimation(OgreEntity* entity, const char* animName, bool loop)
    {
        if (!entity)
            return;

        OgreAnimationState* animState = GetAnimationState(entity, animName);
        if (animState)
        {
            animState->isEnabled = 1;
            animState->isLooping = loop ? 1 : 0;
            animState->timePosition = 0.0f;
            animState->weight = 1.0f;

            std::cout << "Playing animation: " << animName << " on entity: " << entity->name << "\n";
        }
    }

    void OgreController::StopAnimation(OgreEntity* entity, const char* animName)
    {
        if (!entity)
            return;

        OgreAnimationState* animState = GetAnimationState(entity, animName);
        if (animState)
        {
            animState->isEnabled = 0;
            animState->weight = 0.0f;
        }
    }

    void OgreController::BlendAnimation(OgreEntity* entity, const char* fromAnim, const char* toAnim, float blendTime)
    {
        if (!entity)
            return;

        BlendState blend;
        blend.fromAnim = fromAnim;
        blend.toAnim = toAnim;
        blend.blendTime = blendTime;
        blend.currentTime = 0.0f;

        m_BlendStates[entity] = blend;

        std::cout << "Blending animation from " << fromAnim << " to " << toAnim << " over " << blendTime << "s\n";
    }

    void OgreController::UpdateAnimations(float deltaTime)
    {
        // Update animation states
        for (auto& pair : m_Entities)
        {
            OgreEntity* entity = pair.second;
            if (!entity || !entity->animationState)
                continue;

            OgreAnimationState* animState = (OgreAnimationState*)entity->animationState;
            if (animState->isEnabled)
            {
                animState->timePosition += deltaTime;

                // Loop animation if needed
                if (animState->isLooping && animState->timePosition >= animState->length)
                {
                    animState->timePosition = fmodf(animState->timePosition, animState->length);
                }
            }
        }

        // Update blending
        UpdateBlending(deltaTime);
    }

    void OgreController::UpdateBlending(float deltaTime)
    {
        std::vector<OgreEntity*> completedBlends;

        for (auto& pair : m_BlendStates)
        {
            OgreEntity* entity = pair.first;
            BlendState& blend = pair.second;

            blend.currentTime += deltaTime;
            float blendFactor = blend.currentTime / blend.blendTime;

            if (blendFactor >= 1.0f)
            {
                // Blend complete
                StopAnimation(entity, blend.fromAnim.c_str());
                PlayAnimation(entity, blend.toAnim.c_str());
                completedBlends.push_back(entity);
            }
            else
            {
                // Update weights
                OgreAnimationState* fromState = GetAnimationState(entity, blend.fromAnim.c_str());
                OgreAnimationState* toState = GetAnimationState(entity, blend.toAnim.c_str());

                if (fromState)
                    fromState->weight = 1.0f - blendFactor;
                if (toState)
                {
                    toState->weight = blendFactor;
                    toState->isEnabled = 1;
                }
            }
        }

        // Remove completed blends
        for (auto* entity : completedBlends)
        {
            m_BlendStates.erase(entity);
        }
    }

    OgreCamera* OgreController::GetCamera(const char* name)
    {
        auto it = m_Cameras.find(name);
        if (it != m_Cameras.end())
            return it->second;

        // Try to get main camera
        if (strcmp(name, "MainCamera") == 0)
        {
            uintptr_t base = GetKenshiBase();
            OgreCamera* camera = Read<OgreCamera*>(base + OgreOffsets::MainCamera);
            if (camera)
            {
                m_Cameras[name] = camera;
                return camera;
            }
        }

        return nullptr;
    }

    void OgreController::SetCameraPosition(OgreCamera* camera, Vector3 position)
    {
        if (!camera)
            return;

        Write<Vector3>((uintptr_t)&camera->position, position);

        if (camera->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)camera->sceneNode;
            Write<Vector3>((uintptr_t)&node->position, position);
            node->needsUpdate = 1;
        }
    }

    void OgreController::SetCameraOrientation(OgreCamera* camera, Quaternion orientation)
    {
        if (!camera)
            return;

        Write<Quaternion>((uintptr_t)&camera->orientation, orientation);

        if (camera->sceneNode)
        {
            OgreSceneNode* node = (OgreSceneNode*)camera->sceneNode;
            Write<Quaternion>((uintptr_t)&node->orientation, orientation);
            node->needsUpdate = 1;
        }
    }

    void OgreController::SetCameraTarget(OgreCamera* camera, Vector3 target)
    {
        if (!camera)
            return;

        // Calculate look-at orientation
        Vector3 direction;
        direction.x = target.x - camera->position.x;
        direction.y = target.y - camera->position.y;
        direction.z = target.z - camera->position.z;

        // Normalize
        float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length > 0.001f)
        {
            direction.x /= length;
            direction.y /= length;
            direction.z /= length;
        }

        // Create quaternion from direction (simplified)
        // In real implementation, this would be a proper look-at quaternion
        Quaternion orientation;
        orientation.w = 1.0f;
        orientation.x = direction.x * 0.5f;
        orientation.y = direction.y * 0.5f;
        orientation.z = direction.z * 0.5f;

        SetCameraOrientation(camera, orientation);
    }

    std::vector<OgreEntity*> OgreController::GetVisibleEntities()
    {
        std::vector<OgreEntity*> visible;

        for (auto& pair : m_Entities)
        {
            if (pair.second && pair.second->visible)
            {
                visible.push_back(pair.second);
            }
        }

        return visible;
    }

    std::vector<OgreSceneNode*> OgreController::GetNodesInRadius(Vector3 center, float radius)
    {
        std::vector<OgreSceneNode*> nodes;
        float radiusSq = radius * radius;

        for (auto& pair : m_SceneNodes)
        {
            OgreSceneNode* node = pair.second;
            if (!node)
                continue;

            float dx = node->position.x - center.x;
            float dy = node->position.y - center.y;
            float dz = node->position.z - center.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq <= radiusSq)
            {
                nodes.push_back(node);
            }
        }

        return nodes;
    }

    OgreEntity* OgreController::RaycastFromCamera(OgreCamera* camera, Vector3 direction, float maxDistance)
    {
        if (!camera)
            return nullptr;

        // Simplified raycast - in real implementation would use OGRE's raycasting system
        Vector3 rayOrigin = camera->position;

        for (auto& pair : m_Entities)
        {
            OgreEntity* entity = pair.second;
            if (!entity || !entity->visible || !entity->sceneNode)
                continue;

            OgreSceneNode* node = (OgreSceneNode*)entity->sceneNode;

            // Simple sphere-ray intersection
            Vector3 toEntity;
            toEntity.x = node->position.x - rayOrigin.x;
            toEntity.y = node->position.y - rayOrigin.y;
            toEntity.z = node->position.z - rayOrigin.z;

            float distance = sqrtf(toEntity.x * toEntity.x + toEntity.y * toEntity.y + toEntity.z * toEntity.z);

            if (distance <= maxDistance)
            {
                // Check if entity is in ray direction
                float dot = (toEntity.x * direction.x + toEntity.y * direction.y + toEntity.z * direction.z) / distance;
                if (dot > 0.9f) // Within cone
                {
                    return entity;
                }
            }
        }

        return nullptr;
    }

    void OgreController::Update(float deltaTime)
    {
        if (!m_Initialized)
            return;

        UpdateAnimations(deltaTime);

        // Update scene nodes that need updating
        for (auto& pair : m_SceneNodes)
        {
            OgreSceneNode* node = pair.second;
            if (node && node->needsUpdate)
            {
                UpdateSceneNode(node);
                node->needsUpdate = 0;
            }
        }
    }

    void OgreController::UpdateSceneNode(OgreSceneNode* node)
    {
        if (!node)
            return;

        // In real implementation, this would update the scene node's transform
        // and propagate to children
    }

    uintptr_t OgreController::GetKenshiBase()
    {
        return (uintptr_t)GetModuleHandle(NULL);
    }
}

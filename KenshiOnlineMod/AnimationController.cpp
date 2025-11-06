#include "AnimationController.h"
#include <iostream>
#include <algorithm>

namespace KenshiOnline
{
    AnimationController::AnimationController(OgreController* ogreController)
        : m_OgreController(ogreController)
    {
    }

    AnimationController::~AnimationController()
    {
    }

    void AnimationController::PlayAnimation(OgreEntity* entity, const std::string& animName, bool loop, int priority)
    {
        if (!entity || !m_OgreController)
            return;

        // Create or update animation state
        auto& states = m_AnimationStates[entity];

        // Check if animation already exists
        for (auto& state : states)
        {
            if (state.animationName == animName)
            {
                state.isEnabled = true;
                state.isLooping = loop;
                state.priority = priority;
                state.timePosition = 0.0f;
                state.weight = 1.0f;

                m_OgreController->PlayAnimation(entity, animName.c_str(), loop);
                return;
            }
        }

        // Add new animation state
        AnimationStateInfo newState;
        newState.animationName = animName;
        newState.isEnabled = true;
        newState.isLooping = loop;
        newState.priority = priority;
        newState.timePosition = 0.0f;
        newState.weight = 1.0f;
        newState.speed = 1.0f;

        states.push_back(newState);

        m_OgreController->PlayAnimation(entity, animName.c_str(), loop);

        std::cout << "Playing animation: " << animName << " (priority: " << priority << ")\n";
    }

    void AnimationController::StopAnimation(OgreEntity* entity, const std::string& animName)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.animationName == animName)
            {
                state.isEnabled = false;
                state.weight = 0.0f;

                if (m_OgreController)
                    m_OgreController->StopAnimation(entity, animName.c_str());

                break;
            }
        }
    }

    void AnimationController::StopAllAnimations(OgreEntity* entity)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.isEnabled)
            {
                state.isEnabled = false;
                state.weight = 0.0f;

                if (m_OgreController)
                    m_OgreController->StopAnimation(entity, state.animationName.c_str());
            }
        }
    }

    void AnimationController::BlendToAnimation(OgreEntity* entity, const std::string& toAnim, float blendTime, int priority)
    {
        if (!entity)
            return;

        // Find current playing animation
        std::string fromAnim;
        int highestPriority = -1;

        auto& states = m_AnimationStates[entity];
        for (const auto& state : states)
        {
            if (state.isEnabled && state.priority > highestPriority)
            {
                fromAnim = state.animationName;
                highestPriority = state.priority;
            }
        }

        if (fromAnim.empty())
        {
            // No animation playing, just play the new one
            PlayAnimation(entity, toAnim, true, priority);
            return;
        }

        // Queue blend request
        AnimationBlendRequest request;
        request.fromAnim = fromAnim;
        request.toAnim = toAnim;
        request.blendTime = blendTime;
        request.priority = priority;

        m_BlendQueue[entity].push(request);

        std::cout << "Queued blend from " << fromAnim << " to " << toAnim << " over " << blendTime << "s\n";
    }

    void AnimationController::SetAnimationSpeed(OgreEntity* entity, const std::string& animName, float speed)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.animationName == animName)
            {
                state.speed = speed;
                break;
            }
        }
    }

    void AnimationController::SetAnimationWeight(OgreEntity* entity, const std::string& animName, float weight)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.animationName == animName)
            {
                state.weight = weight;
                break;
            }
        }
    }

    bool AnimationController::IsAnimationPlaying(OgreEntity* entity, const std::string& animName)
    {
        if (!entity)
            return false;

        auto& states = m_AnimationStates[entity];

        for (const auto& state : states)
        {
            if (state.animationName == animName && state.isEnabled)
            {
                return true;
            }
        }

        return false;
    }

    float AnimationController::GetAnimationTimePosition(OgreEntity* entity, const std::string& animName)
    {
        if (!entity)
            return 0.0f;

        auto& states = m_AnimationStates[entity];

        for (const auto& state : states)
        {
            if (state.animationName == animName)
            {
                return state.timePosition;
            }
        }

        return 0.0f;
    }

    AnimationStateInfo AnimationController::GetAnimationState(OgreEntity* entity, const std::string& animName)
    {
        if (!entity)
            return AnimationStateInfo();

        auto& states = m_AnimationStates[entity];

        for (const auto& state : states)
        {
            if (state.animationName == animName)
            {
                return state;
            }
        }

        return AnimationStateInfo();
    }

    void AnimationController::SetCharacterState(OgreEntity* entity, AnimationType type)
    {
        if (!entity)
            return;

        AnimationType currentType = GetCharacterState(entity);

        if (currentType == type)
            return; // Already in this state

        std::string animName = GetAnimationNameForType(type);

        if (!animName.empty())
        {
            int priority = static_cast<int>(type); // Use type as priority
            BlendToAnimation(entity, animName, 0.2f, priority);
            m_CurrentType[entity] = type;

            std::cout << "Set character state to: " << static_cast<int>(type) << "\n";
        }
    }

    AnimationType AnimationController::GetCharacterState(OgreEntity* entity)
    {
        if (!entity)
            return AnimationType::Idle;

        auto it = m_CurrentType.find(entity);
        if (it != m_CurrentType.end())
            return it->second;

        return AnimationType::Idle;
    }

    void AnimationController::SyncAnimationFromNetwork(OgreEntity* entity, const AnimationStateInfo& info)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        // Find or create state
        bool found = false;
        for (auto& state : states)
        {
            if (state.animationName == info.animationName)
            {
                state = info;
                found = true;
                break;
            }
        }

        if (!found)
        {
            states.push_back(info);
        }

        // Apply to OGRE
        if (info.isEnabled && m_OgreController)
        {
            m_OgreController->PlayAnimation(entity, info.animationName.c_str(), info.isLooping);
        }
    }

    AnimationStateInfo AnimationController::GetSyncData(OgreEntity* entity)
    {
        if (!entity)
            return AnimationStateInfo();

        // Return highest priority enabled animation
        auto& states = m_AnimationStates[entity];

        AnimationStateInfo* highest = nullptr;
        for (auto& state : states)
        {
            if (state.isEnabled)
            {
                if (!highest || state.priority > highest->priority)
                {
                    highest = &state;
                }
            }
        }

        if (highest)
            return *highest;

        return AnimationStateInfo();
    }

    void AnimationController::Update(float deltaTime)
    {
        // Update all entity animation states
        for (auto& pair : m_AnimationStates)
        {
            OgreEntity* entity = pair.first;
            ProcessBlendQueue(entity, deltaTime);
            UpdateAnimationStates(entity, deltaTime);
        }
    }

    void AnimationController::ProcessBlendQueue(OgreEntity* entity, float deltaTime)
    {
        if (!entity || !m_OgreController)
            return;

        auto it = m_BlendQueue.find(entity);
        if (it == m_BlendQueue.end() || it->second.empty())
            return;

        AnimationBlendRequest& request = it->second.front();

        // Start the blend
        m_OgreController->BlendAnimation(entity, request.fromAnim.c_str(),
                                        request.toAnim.c_str(), request.blendTime);

        // Update states
        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.animationName == request.fromAnim)
            {
                state.weight = 0.0f; // Will be faded out
            }
            else if (state.animationName == request.toAnim)
            {
                state.isEnabled = true;
                state.weight = 1.0f; // Will be faded in
                state.priority = request.priority;
            }
        }

        // Remove processed request
        it->second.pop();
    }

    void AnimationController::UpdateAnimationStates(OgreEntity* entity, float deltaTime)
    {
        if (!entity)
            return;

        auto& states = m_AnimationStates[entity];

        for (auto& state : states)
        {
            if (state.isEnabled)
            {
                state.timePosition += deltaTime * state.speed;

                // Handle looping
                // Note: actual loop length would come from OGRE animation
                // Using a default of 2.0 seconds for now
                float animLength = 2.0f;

                if (state.isLooping && state.timePosition >= animLength)
                {
                    state.timePosition = fmodf(state.timePosition, animLength);
                }
                else if (!state.isLooping && state.timePosition >= animLength)
                {
                    state.isEnabled = false;
                    state.weight = 0.0f;
                }
            }
        }
    }

    std::string AnimationController::GetAnimationNameForType(AnimationType type)
    {
        switch (type)
        {
            case AnimationType::Idle: return "idle";
            case AnimationType::Walk: return "walk";
            case AnimationType::Run: return "run";
            case AnimationType::Sprint: return "sprint";
            case AnimationType::Sneak: return "sneak";
            case AnimationType::Combat_Idle: return "combat_idle";
            case AnimationType::Combat_Block: return "combat_block";
            case AnimationType::Combat_Attack: return "combat_attack";
            case AnimationType::Combat_Dodge: return "combat_dodge";
            case AnimationType::Injured_Walk: return "injured_walk";
            case AnimationType::Crawl: return "crawl";
            case AnimationType::GetUp: return "getup";
            case AnimationType::Knockdown: return "knockdown";
            case AnimationType::Death: return "death";
            case AnimationType::Interact: return "interact";
            case AnimationType::Carry: return "carry";
            case AnimationType::Mining: return "mining";
            case AnimationType::Building: return "building";
            case AnimationType::Eating: return "eating";
            case AnimationType::Sleeping: return "sleeping";
            default: return "idle";
        }
    }

    void AnimationController::SetPrimaryAnimation(OgreEntity* entity, const std::string& animName)
    {
        if (!entity)
            return;

        // Stop all other animations and play this one with highest priority
        StopAllAnimations(entity);
        PlayAnimation(entity, animName, true, 1000);
    }
}

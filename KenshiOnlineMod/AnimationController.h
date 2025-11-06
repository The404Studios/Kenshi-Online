#pragma once

#include "OgreController.h"
#include <string>
#include <unordered_map>
#include <queue>

namespace KenshiOnline
{
    // Animation types in Kenshi
    enum class AnimationType
    {
        Idle = 0,
        Walk = 1,
        Run = 2,
        Sprint = 3,
        Sneak = 4,
        Combat_Idle = 5,
        Combat_Block = 6,
        Combat_Attack = 7,
        Combat_Dodge = 8,
        Injured_Walk = 9,
        Crawl = 10,
        GetUp = 11,
        Knockdown = 12,
        Death = 13,
        Interact = 14,
        Carry = 15,
        Mining = 16,
        Building = 17,
        Eating = 18,
        Sleeping = 19,
        Custom = 99
    };

    // Animation state info for network sync
    struct AnimationStateInfo
    {
        std::string animationName;
        AnimationType type;
        float timePosition;
        float weight;
        float speed;
        bool isEnabled;
        bool isLooping;
        int priority; // Higher priority animations override lower ones

        AnimationStateInfo()
            : type(AnimationType::Idle)
            , timePosition(0)
            , weight(1.0f)
            , speed(1.0f)
            , isEnabled(false)
            , isLooping(true)
            , priority(0)
        {}
    };

    // Animation blend request
    struct AnimationBlendRequest
    {
        std::string fromAnim;
        std::string toAnim;
        float blendTime;
        int priority;
    };

    // Animation controller - manages character animations
    class AnimationController
    {
    public:
        AnimationController(OgreController* ogreController);
        ~AnimationController();

        // Animation playback
        void PlayAnimation(OgreEntity* entity, const std::string& animName, bool loop = true, int priority = 0);
        void StopAnimation(OgreEntity* entity, const std::string& animName);
        void StopAllAnimations(OgreEntity* entity);

        // Animation blending
        void BlendToAnimation(OgreEntity* entity, const std::string& toAnim, float blendTime = 0.3f, int priority = 0);
        void SetAnimationSpeed(OgreEntity* entity, const std::string& animName, float speed);
        void SetAnimationWeight(OgreEntity* entity, const std::string& animName, float weight);

        // Animation state queries
        bool IsAnimationPlaying(OgreEntity* entity, const std::string& animName);
        float GetAnimationTimePosition(OgreEntity* entity, const std::string& animName);
        AnimationStateInfo GetAnimationState(OgreEntity* entity, const std::string& animName);

        // High-level animation control
        void SetCharacterState(OgreEntity* entity, AnimationType type);
        AnimationType GetCharacterState(OgreEntity* entity);

        // Network synchronization
        void SyncAnimationFromNetwork(OgreEntity* entity, const AnimationStateInfo& info);
        AnimationStateInfo GetSyncData(OgreEntity* entity);

        // Update
        void Update(float deltaTime);

    private:
        OgreController* m_OgreController;

        // Current animation states per entity
        std::unordered_map<OgreEntity*, std::vector<AnimationStateInfo>> m_AnimationStates;

        // Current character animation type per entity
        std::unordered_map<OgreEntity*, AnimationType> m_CurrentType;

        // Pending blend requests
        std::unordered_map<OgreEntity*, std::queue<AnimationBlendRequest>> m_BlendQueue;

        // Helper functions
        void ProcessBlendQueue(OgreEntity* entity, float deltaTime);
        void UpdateAnimationStates(OgreEntity* entity, float deltaTime);
        std::string GetAnimationNameForType(AnimationType type);
        void SetPrimaryAnimation(OgreEntity* entity, const std::string& animName);
    };
}

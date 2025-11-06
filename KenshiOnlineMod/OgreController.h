#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace KenshiOnline
{
    // Forward declarations for OGRE types
    struct Vector3
    {
        float x, y, z;
        Vector3() : x(0), y(0), z(0) {}
        Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    };

    struct Quaternion
    {
        float w, x, y, z;
        Quaternion() : w(1), x(0), y(0), z(0) {}
        Quaternion(float _w, float _x, float _y, float _z) : w(_w), x(_x), y(_y), z(_z) {}
    };

    // OGRE Engine structures (reverse engineered from Kenshi)
    struct OgreSceneNode
    {
        void* vtable;
        char* name;
        void* parent;
        void* children;
        int childCount;

        Vector3 position;
        Quaternion orientation;
        Vector3 scale;

        void* attachedObject;
        void* userData;

        uint8_t needsUpdate;
        uint8_t visible;
    };

    struct OgreEntity
    {
        void* vtable;
        char* name;
        void* mesh;
        void* skeleton;
        void* animationState;
        void* sceneNode;

        void* subEntities;
        int subEntityCount;

        uint8_t visible;
        uint8_t castShadows;
    };

    struct OgreSkeleton
    {
        void* vtable;
        char* name;
        void* bones;
        int boneCount;

        void* animations;
        int animationCount;

        void* blendMode;
    };

    struct OgreAnimation
    {
        void* vtable;
        char* name;
        float length;
        void* tracks;
        int trackCount;

        uint8_t isEnabled;
        uint8_t isLooping;
    };

    struct OgreAnimationState
    {
        void* vtable;
        void* animation;
        char* animationName;

        float timePosition;
        float weight;
        float length;

        uint8_t isEnabled;
        uint8_t isLooping;
    };

    struct OgreCamera
    {
        void* vtable;
        char* name;
        void* sceneNode;

        Vector3 position;
        Quaternion orientation;

        float fov;
        float nearClipDistance;
        float farClipDistance;

        void* viewport;
    };

    struct OgreViewport
    {
        void* vtable;
        void* camera;
        void* renderTarget;

        int left, top, width, height;
        float zOrder;

        uint8_t isAutoUpdated;
    };

    // OGRE Controller class - manages OGRE engine integration
    class OgreController
    {
    public:
        OgreController();
        ~OgreController();

        // Initialization
        bool Initialize();
        void Shutdown();

        // Scene management
        OgreSceneNode* GetSceneNode(const char* name);
        OgreEntity* GetEntity(const char* name);
        OgreSceneNode* CreateSceneNode(const char* name, Vector3 position);
        OgreEntity* CreateEntity(const char* name, const char* meshName);
        void DestroySceneNode(const char* name);
        void DestroyEntity(const char* name);

        // Animation management
        OgreAnimationState* GetAnimationState(OgreEntity* entity, const char* animName);
        void PlayAnimation(OgreEntity* entity, const char* animName, bool loop = true);
        void StopAnimation(OgreEntity* entity, const char* animName);
        void BlendAnimation(OgreEntity* entity, const char* fromAnim, const char* toAnim, float blendTime);
        void UpdateAnimations(float deltaTime);

        // Camera management
        OgreCamera* GetCamera(const char* name);
        void SetCameraPosition(OgreCamera* camera, Vector3 position);
        void SetCameraOrientation(OgreCamera* camera, Quaternion orientation);
        void SetCameraTarget(OgreCamera* camera, Vector3 target);

        // Scene queries
        std::vector<OgreEntity*> GetVisibleEntities();
        std::vector<OgreSceneNode*> GetNodesInRadius(Vector3 center, float radius);
        OgreEntity* RaycastFromCamera(OgreCamera* camera, Vector3 direction, float maxDistance);

        // Update
        void Update(float deltaTime);

        // Memory access
        static uintptr_t GetKenshiBase();
        template<typename T>
        static T Read(uintptr_t address);
        template<typename T>
        static void Write(uintptr_t address, T value);

    private:
        bool m_Initialized;
        void* m_SceneManager;
        void* m_RenderSystem;
        void* m_Root;

        std::unordered_map<std::string, OgreSceneNode*> m_SceneNodes;
        std::unordered_map<std::string, OgreEntity*> m_Entities;
        std::unordered_map<std::string, OgreCamera*> m_Cameras;

        // Animation blending state
        struct BlendState
        {
            std::string fromAnim;
            std::string toAnim;
            float blendTime;
            float currentTime;
        };
        std::unordered_map<OgreEntity*, BlendState> m_BlendStates;

        // Helper functions
        void UpdateBlending(float deltaTime);
        void UpdateSceneNode(OgreSceneNode* node);
    };

    // Template implementations
    template<typename T>
    T OgreController::Read(uintptr_t address)
    {
        return *(T*)address;
    }

    template<typename T>
    void OgreController::Write(uintptr_t address, T value)
    {
        DWORD oldProtect;
        VirtualProtect((void*)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
        *(T*)address = value;
        VirtualProtect((void*)address, sizeof(T), oldProtect, &oldProtect);
    }
}

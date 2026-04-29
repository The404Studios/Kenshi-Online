#pragma once
#include "kmp/types.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace kmp::sdk {

// ═══════════════════════════════════════════════════════════════════════════
//  VISUAL PROXY
//  Represents a remote player as a tracked entity with position interpolation.
//
//  Phase 1 (current): State tracking + interpolation only.
//     Manages proxy state (position, rotation, animation) independently
//     of the game's entity system. No hooks, no game entities, no crashes.
//
//  Phase 2 (future): Ogre SceneNode rendering.
//     Once we discover the SceneManager at runtime via VTable scanning,
//     attach Ogre meshes to visualize remote players.
//
//  The key insight: all the STATE MANAGEMENT (interpolation, lifecycle,
//  ownership) works without Ogre headers. We just need Ogre for the final
//  "make it visible" step, which can be added incrementally.
// ═══════════════════════════════════════════════════════════════════════════

struct ProxyState {
    EntityID    netId = INVALID_ENTITY;
    PlayerID    owner = INVALID_PLAYER;
    Vec3        position;
    Vec3        targetPosition;      // Interpolation target
    Quat        rotation;
    Quat        targetRotation;
    float       moveSpeed = 0.f;
    uint8_t     animState = 0;
    std::string name;
    std::string meshName;            // For future Ogre rendering
    bool        visible = true;
    bool        alive   = true;
};

class VisualProxy {
public:
    VisualProxy() = default;
    ~VisualProxy() = default;

    // ── Lifecycle ──

    // Initialize the proxy system. No external dependencies needed for Phase 1.
    bool Initialize();

    // Shutdown: destroy all proxies.
    void Shutdown();

    // ── Per-Frame Update ──
    // Interpolate positions toward targets. deltaTime in seconds.
    void Update(float deltaTime);

    // ── Proxy Management ──

    // Create a proxy for a remote entity.
    bool CreateProxy(EntityID netId, PlayerID owner,
                     const std::string& displayName,
                     const std::string& meshName,
                     const Vec3& position, const Quat& rotation);

    // Destroy a proxy.
    void DestroyProxy(EntityID netId);

    // Destroy all proxies owned by a specific player.
    void DestroyPlayerProxies(PlayerID owner);

    // Destroy all proxies.
    void DestroyAll();

    // ── State Updates (from network) ──

    // Set interpolation target (called when position update arrives).
    void SetTargetPosition(EntityID netId, const Vec3& pos, const Quat& rot,
                           float moveSpeed, uint8_t animState);

    // Snap to position immediately (teleport).
    void SnapPosition(EntityID netId, const Vec3& pos, const Quat& rot);

    // Set visibility.
    void SetVisible(EntityID netId, bool visible);

    // Set alive state.
    void SetAlive(EntityID netId, bool alive);

    // Update display name.
    void SetName(EntityID netId, const std::string& name);

    // ── Queries ──

    bool HasProxy(EntityID netId) const;
    size_t GetProxyCount() const;
    Vec3 GetPosition(EntityID netId) const;
    std::vector<EntityID> GetAllProxyIds() const;

    // Get full proxy state (for debug/diagnostics).
    bool GetProxyState(EntityID netId, ProxyState& out) const;

private:
    struct ProxyData {
        ProxyState state;
        void*      sceneNode  = nullptr; // Future: Ogre::SceneNode*
        void*      ogreEntity = nullptr; // Future: Ogre::Entity*
    };

    void UpdateProxy(ProxyData& proxy, float deltaTime);

    mutable std::mutex m_mutex;
    std::unordered_map<EntityID, ProxyData> m_proxies;
    bool m_initialized = false;

    static constexpr float INTERP_SPEED = 10.0f;
    static constexpr float SNAP_DISTANCE = 50.0f;
};

} // namespace kmp::sdk

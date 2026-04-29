#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Engine Context — Centralized engine state container
// ═══════════════════════════════════════════════════════════════════════════
// Provides validated access to all engine subsystems in one place.
// Every access is conditionalized — returns nullptr/safe-default if the
// subsystem isn't initialized yet.
//
// This replaces the pattern of passing 6+ references through constructors
// and provides a single point of truth for "what's available right now."
//
// Usage:
//   auto& ctx = EngineContext::Get();
//   if (auto* registry = ctx.GetRegistry()) {
//       registry->Register(...);
//   }

#include "engine_pipeline.h"
#include "engine_events.h"
#include "kmp/assert.h"
#include "kmp/types.h"
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>

// Forward declarations — avoid circular includes
namespace kmp {
    class EntityRegistry;
    class Interpolation;
    class NetworkClient;
    class SpawnManager;
    class PlayerController;
    class LoadingOrchestrator;
    class Overlay;
    class NativeHud;
    struct ClientConfig;
    class TaskOrchestrator;
    class SyncOrchestrator;
}

namespace kmp::game {
    struct GameOffsets;
    class CharacterAccessor;
    class GameWorldAccessor;
}

namespace kmp::engine {

// ── Engine lifecycle phases ──
enum class EnginePhase : uint8_t {
    Uninitialized,   // Nothing set up yet
    Scanning,        // Pattern scanner running
    HooksInstalled,  // Hooks installed, waiting for game load
    GameReady,       // Game world loaded, ready to connect
    Connecting,      // Attempting connection to server
    Connected,       // Fully operational — all systems go
    Disconnecting,   // Cleaning up connection
    ShuttingDown,    // Engine shutting down
};

inline const char* EnginePhaseToString(EnginePhase p) {
    switch (p) {
        case EnginePhase::Uninitialized:  return "Uninitialized";
        case EnginePhase::Scanning:       return "Scanning";
        case EnginePhase::HooksInstalled: return "HooksInstalled";
        case EnginePhase::GameReady:      return "GameReady";
        case EnginePhase::Connecting:     return "Connecting";
        case EnginePhase::Connected:      return "Connected";
        case EnginePhase::Disconnecting:  return "Disconnecting";
        case EnginePhase::ShuttingDown:   return "ShuttingDown";
        default:                          return "Unknown";
    }
}

class EngineContext {
public:
    static EngineContext& Get() {
        static EngineContext instance;
        return instance;
    }

    // ── Phase Management ──

    EnginePhase GetPhase() const { return m_phase.load(std::memory_order_acquire); }

    void TransitionTo(EnginePhase newPhase) {
        EnginePhase old = m_phase.load(std::memory_order_acquire);

        // Validate transitions
        bool valid = false;
        switch (newPhase) {
            case EnginePhase::Scanning:       valid = (old == EnginePhase::Uninitialized); break;
            case EnginePhase::HooksInstalled: valid = (old == EnginePhase::Scanning); break;
            case EnginePhase::GameReady:      valid = (old == EnginePhase::HooksInstalled ||
                                                       old == EnginePhase::Disconnecting); break;
            case EnginePhase::Connecting:     valid = (old == EnginePhase::GameReady); break;
            case EnginePhase::Connected:      valid = (old == EnginePhase::Connecting); break;
            case EnginePhase::Disconnecting:  valid = (old == EnginePhase::Connected ||
                                                       old == EnginePhase::Connecting); break;
            case EnginePhase::ShuttingDown:   valid = true; // Always allowed
                break;
            default: break;
        }

        if (!valid) {
            spdlog::warn("EngineContext: Invalid phase transition {} -> {}",
                         EnginePhaseToString(old), EnginePhaseToString(newPhase));
            KMP_ASSERT_MSG(false, "Invalid engine phase transition");
            return;
        }

        spdlog::info("EngineContext: Phase {} -> {}", EnginePhaseToString(old), EnginePhaseToString(newPhase));
        m_phase.store(newPhase, std::memory_order_release);

        // Publish event
        EventBus::Get().Publish(ConnectionStateChangedEvent{
            newPhase == EnginePhase::Connected,
            ""
        });
    }

    // Convenience checks
    bool IsConnected() const { return m_phase.load(std::memory_order_acquire) == EnginePhase::Connected; }
    bool IsGameReady() const {
        auto p = m_phase.load(std::memory_order_acquire);
        return p >= EnginePhase::GameReady && p < EnginePhase::ShuttingDown;
    }
    bool IsOperational() const {
        auto p = m_phase.load(std::memory_order_acquire);
        return p >= EnginePhase::HooksInstalled && p < EnginePhase::ShuttingDown;
    }

    // ── Subsystem Registration ──
    // Called during initialization to wire up subsystems.
    // Each setter validates the pointer.

    void SetEntityRegistry(EntityRegistry* r)     { KMP_ASSERT(r); m_registry = r; }
    void SetInterpolation(Interpolation* i)       { KMP_ASSERT(i); m_interpolation = i; }
    void SetNetworkClient(NetworkClient* c)       { KMP_ASSERT(c); m_client = c; }
    void SetSpawnManager(SpawnManager* s)          { KMP_ASSERT(s); m_spawnManager = s; }
    void SetPlayerController(PlayerController* p) { KMP_ASSERT(p); m_playerController = p; }
    void SetLoadingOrchestrator(LoadingOrchestrator* l) { KMP_ASSERT(l); m_loadingOrch = l; }
    void SetOverlay(Overlay* o)                   { KMP_ASSERT(o); m_overlay = o; }
    void SetNativeHud(NativeHud* h)               { KMP_ASSERT(h); m_nativeHud = h; }
    void SetTaskOrchestrator(TaskOrchestrator* t)  { KMP_ASSERT(t); m_taskOrch = t; }
    void SetSyncOrchestrator(SyncOrchestrator* s)  { m_syncOrch = s; } // Can be null (feature flag)

    // ── Subsystem Access ──
    // All getters return nullable pointers. Caller must check.
    // Use KMP_ENSURE() for convenient checked access.

    EntityRegistry*      GetRegistry()          { return m_registry; }
    Interpolation*       GetInterpolation()     { return m_interpolation; }
    NetworkClient*       GetNetworkClient()     { return m_client; }
    SpawnManager*        GetSpawnManager()       { return m_spawnManager; }
    PlayerController*    GetPlayerController()  { return m_playerController; }
    LoadingOrchestrator* GetLoadingOrchestrator() { return m_loadingOrch; }
    Overlay*             GetOverlay()           { return m_overlay; }
    NativeHud*           GetNativeHud()         { return m_nativeHud; }
    TaskOrchestrator*    GetTaskOrchestrator()  { return m_taskOrch; }
    SyncOrchestrator*    GetSyncOrchestrator()  { return m_syncOrch; }

    // Const versions
    const EntityRegistry*      GetRegistry() const          { return m_registry; }
    const Interpolation*       GetInterpolation() const     { return m_interpolation; }
    const NetworkClient*       GetNetworkClient() const     { return m_client; }
    const SpawnManager*        GetSpawnManager() const       { return m_spawnManager; }
    const PlayerController*    GetPlayerController() const  { return m_playerController; }
    const LoadingOrchestrator* GetLoadingOrchestrator() const { return m_loadingOrch; }

    // ── Pipeline Access ──
    EnginePipeline& GetPipeline() { return m_pipeline; }
    const EnginePipeline& GetPipeline() const { return m_pipeline; }

    // ── Player State ──

    void SetLocalPlayerId(PlayerID id) { m_localPlayerId.store(id, std::memory_order_release); }
    PlayerID GetLocalPlayerId() const { return m_localPlayerId.load(std::memory_order_acquire); }

    void SetIsHost(bool host) { m_isHost = host; }
    bool IsHost() const { return m_isHost; }

    // ── Game World ──

    void SetGameWorldPtr(uintptr_t ptr) { m_gameWorldPtr = ptr; }
    uintptr_t GetGameWorldPtr() const { return m_gameWorldPtr; }

    void SetPlayerBasePtr(uintptr_t ptr) { m_playerBasePtr = ptr; }
    uintptr_t GetPlayerBasePtr() const { return m_playerBasePtr; }

    // ── Shutdown ──

    void Reset() {
        m_phase.store(EnginePhase::Uninitialized, std::memory_order_release);
        m_localPlayerId.store(INVALID_PLAYER, std::memory_order_release);
        m_isHost = false;
        m_gameWorldPtr = 0;
        m_playerBasePtr = 0;
        m_pipeline.Reset();
        // Don't null out subsystem pointers — they may still be valid objects
    }

private:
    EngineContext() = default;

    // Engine phase
    std::atomic<EnginePhase> m_phase{EnginePhase::Uninitialized};

    // Subsystem pointers (non-owning — Core owns the actual objects)
    EntityRegistry*      m_registry          = nullptr;
    Interpolation*       m_interpolation     = nullptr;
    NetworkClient*       m_client            = nullptr;
    SpawnManager*        m_spawnManager       = nullptr;
    PlayerController*    m_playerController  = nullptr;
    LoadingOrchestrator* m_loadingOrch        = nullptr;
    Overlay*             m_overlay           = nullptr;
    NativeHud*           m_nativeHud         = nullptr;
    TaskOrchestrator*    m_taskOrch           = nullptr;
    SyncOrchestrator*    m_syncOrch           = nullptr;

    // Pipeline
    EnginePipeline m_pipeline;

    // Player state
    std::atomic<PlayerID> m_localPlayerId{INVALID_PLAYER};
    bool m_isHost = false;

    // Game world pointers
    uintptr_t m_gameWorldPtr  = 0;
    uintptr_t m_playerBasePtr = 0;
};

// ── Convenience Macros ──
// Safe subsystem access with automatic null checking

#define KMP_CTX()       kmp::engine::EngineContext::Get()
#define KMP_PIPELINE()  KMP_CTX().GetPipeline()
#define KMP_EVENTS()    kmp::engine::EventBus::Get()

// Get a subsystem, returning from the function if null
#define KMP_REQUIRE(subsystem) \
    KMP_ENSURE(KMP_CTX().Get##subsystem())

// Check if engine is in a valid state for the operation
#define KMP_REQUIRE_CONNECTED() \
    KMP_GUARD(KMP_CTX().IsConnected())

#define KMP_REQUIRE_GAME_READY() \
    KMP_GUARD(KMP_CTX().IsGameReady())

#define KMP_REQUIRE_OPERATIONAL() \
    KMP_GUARD(KMP_CTX().IsOperational())

} // namespace kmp::engine

#pragma once
#include "kmp/scanner.h"
#include "kmp/patterns.h"
#include "kmp/hook_manager.h"
#include "kmp/config.h"
#include "net/client.h"
#include "sync/entity_registry.h"
#include "sync/interpolation.h"
#include "game/spawn_manager.h"
#include "ui/overlay.h"
#include <atomic>
#include <thread>
#include <mutex>

namespace kmp {

class Core {
public:
    static Core& Get();

    bool Initialize();
    void Shutdown();

    // Accessors
    PatternScanner&   GetScanner()        { return m_scanner; }
    GameFunctions&    GetGameFunctions()   { return m_gameFuncs; }
    NetworkClient&    GetClient()          { return m_client; }
    EntityRegistry&   GetEntityRegistry()  { return m_entityRegistry; }
    Interpolation&    GetInterpolation()   { return m_interpolation; }
    SpawnManager&     GetSpawnManager()    { return m_spawnManager; }
    Overlay&          GetOverlay()         { return m_overlay; }
    ClientConfig&     GetConfig()          { return m_config; }

    bool IsConnected() const { return m_connected; }
    bool IsHost() const { return m_isHost; }
    PlayerID GetLocalPlayerId() const { return m_localPlayerId; }

    void SetConnected(bool connected) { m_connected = connected; }
    void SetLocalPlayerId(PlayerID id) { m_localPlayerId = id; }
    void SetIsHost(bool host) { m_isHost = host; }

    // Called from game thread hooks
    void OnGameTick(float deltaTime);

    // Set by time_hooks when TimeUpdate hook is active.
    // When true, render_hooks skips its fallback OnGameTick call.
    void SetTimeHookActive(bool active) { m_timeHookActive = active; }
    bool IsTimeHookActive() const { return m_timeHookActive; }

private:
    Core() = default;
    ~Core() = default;

    bool InitScanner();
    bool InitHooks();
    bool InitNetwork();
    bool InitUI();

    // Network thread
    void NetworkThreadFunc();

    PatternScanner  m_scanner;
    GameFunctions   m_gameFuncs;
    NetworkClient   m_client;
    EntityRegistry  m_entityRegistry;
    Interpolation   m_interpolation;
    SpawnManager    m_spawnManager;
    Overlay         m_overlay;
    ClientConfig    m_config;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    bool              m_isHost = false;
    bool              m_timeHookActive = false;
    PlayerID          m_localPlayerId = 0;

    std::thread m_networkThread;
};

} // namespace kmp

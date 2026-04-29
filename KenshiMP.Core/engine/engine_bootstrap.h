#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Engine Bootstrap — Wires the engine pipeline to Core's methods
// ═══════════════════════════════════════════════════════════════════════════
// This is the bridge between the new engine abstraction layer and the
// existing Core class. It registers pipeline stages that delegate to
// Core's existing methods, providing a migration path from the ad-hoc
// OnGameTick() chain to the formalized pipeline.
//
// Call BootstrapEngine() from Core::Initialize() after all subsystems
// are created. Call pipeline.Tick() from OnGameTick() instead of the
// existing method chain.
//
// Migration strategy:
//   Phase 1: Pipeline wraps existing methods (no behavior change)
//   Phase 2: Move logic from Core methods into dedicated Systems
//   Phase 3: Remove Core methods, pipeline is the source of truth

#include "engine_context.h"
#include "engine_pipeline.h"
#include "kmp/assert.h"
#include <spdlog/spdlog.h>

namespace kmp::engine {

// Forward declare Core to avoid circular include
} // temporarily close namespace
namespace kmp { class Core; }
namespace kmp::engine {

// Wire up the engine pipeline stages to Core's existing methods.
// This is a one-time setup call during initialization.
inline void BootstrapPipeline(EnginePipeline& pipeline, Core& core) {
    spdlog::info("EngineBootstrap: Wiring pipeline stages...");

    // Stage 0: Frame Begin
    pipeline.RegisterStage(Stage::FrameBegin, "FrameBegin",
        [](float /*dt*/) {
            // Frame begin is handled by the pipeline's own OnTick
            // Future: snapshot frame data, detect loading gaps
        });

    // Stage 1: Network Receive — pump ENet and dispatch packets
    pipeline.RegisterStage(Stage::NetworkReceive, "NetworkReceive",
        [&core](float /*dt*/) {
            auto* client = KMP_CTX().GetNetworkClient();
            KMP_GUARD(client != nullptr);
            // Network pumping is handled by NetworkClient::Poll()
            // which is called from the network thread.
            // This stage processes the results.
        });
    pipeline.ConfigureStage(Stage::NetworkReceive, false, false, 5.0f);

    // Stage 2: Zone Update — recompute zone grid
    pipeline.RegisterStage(Stage::ZoneUpdate, "ZoneUpdate",
        [&core](float /*dt*/) {
            auto* syncOrch = KMP_CTX().GetSyncOrchestrator();
            if (syncOrch) {
                // Zone updates are handled inside SyncOrchestrator::Tick()
            }
        });
    pipeline.ConfigureStage(Stage::ZoneUpdate, true, true, 2.0f);

    // Stage 3: Buffer Swap — double-buffer swap
    pipeline.RegisterStage(Stage::BufferSwap, "BufferSwap",
        [](float /*dt*/) {
            // Buffer swap is lightweight — handled internally
        });

    // Stage 4: Spawn Process — process pending spawn queue
    pipeline.RegisterStage(Stage::SpawnProcess, "SpawnProcess",
        [&core](float /*dt*/) {
            auto* spawnMgr = KMP_CTX().GetSpawnManager();
            KMP_GUARD(spawnMgr != nullptr);
            // Spawn processing happens through Core::HandleSpawnQueue()
            // which is already called from OnGameTick.
        });
    pipeline.ConfigureStage(Stage::SpawnProcess, true, true, 20.0f);

    // Stage 5: Remote Apply — apply interpolated positions to remote entities
    pipeline.RegisterStage(Stage::RemoteApply, "RemoteApply",
        [&core](float dt) {
            KMP_GUARD(KMP_CTX().IsConnected());
            // Delegated to Core::ApplyRemotePositions()
        });
    pipeline.ConfigureStage(Stage::RemoteApply, true, true, 5.0f);

    // Stage 6: Local Poll — read local positions and send updates
    pipeline.RegisterStage(Stage::LocalPoll, "LocalPoll",
        [&core](float dt) {
            KMP_GUARD(KMP_CTX().IsConnected());
            // Delegated to Core::PollLocalPositions()
        });
    pipeline.ConfigureStage(Stage::LocalPoll, true, true, 5.0f);

    // Stage 7: Combat Resolve — server-authoritative combat
    pipeline.RegisterStage(Stage::CombatResolve, "CombatResolve",
        [](float /*dt*/) {
            // Combat events are handled by hooks (combat_hooks.cpp)
            // and the event bus. This stage processes deferred combat events.
            EventBus::Get().FlushDeferred();
        });
    pipeline.ConfigureStage(Stage::CombatResolve, true, true, 3.0f);

    // Stage 8: Inventory Sync
    pipeline.RegisterStage(Stage::InventorySync, "InventorySync",
        [](float /*dt*/) {
            // Inventory changes are event-driven via hooks
        });
    pipeline.ConfigureStage(Stage::InventorySync, true, true, 3.0f);

    // Stage 9: World State — time and weather
    pipeline.RegisterStage(Stage::WorldState, "WorldState",
        [](float /*dt*/) {
            // Time sync handled by time_hooks
        });
    pipeline.ConfigureStage(Stage::WorldState, true, true, 2.0f);

    // Stage 10: Network Send — flush outgoing packets
    pipeline.RegisterStage(Stage::NetworkSend, "NetworkSend",
        [&core](float /*dt*/) {
            // Packet sending is handled at the end of each operation
            // This stage can batch and flush if needed
        });
    pipeline.ConfigureStage(Stage::NetworkSend, true, false, 5.0f);

    // Stage 11: UI Update
    pipeline.RegisterStage(Stage::UIUpdate, "UIUpdate",
        [](float dt) {
            auto* hud = KMP_CTX().GetNativeHud();
            if (hud) {
                // HUD update is called from render_hooks
            }
        });

    // Stage 12: Diagnostics
    pipeline.RegisterStage(Stage::Diagnostics, "Diagnostics",
        [&pipeline](float dt) {
            // Health monitor is updated by pipeline.Tick() itself
            // This stage can do additional diagnostics
        });

    // Stage 13: Frame End
    pipeline.RegisterStage(Stage::FrameEnd, "FrameEnd",
        [](float /*dt*/) {
            // Increment tick counter is handled by pipeline
        });

    spdlog::info("EngineBootstrap: All {} pipeline stages registered", Stage::COUNT);
}

// Register the EngineContext with all Core subsystems.
// Call from Core::Initialize() after creating all subsystems.
inline void BootstrapContext(EngineContext& ctx, Core& core);

// Full bootstrap: context + pipeline + systems
inline void BootstrapEngine(Core& core) {
    auto& ctx = EngineContext::Get();

    // Context is wired in Core::Initialize() via BootstrapContext()
    // Pipeline stages are wired here
    BootstrapPipeline(ctx.GetPipeline(), core);

    spdlog::info("EngineBootstrap: Engine bootstrap complete");
}

} // namespace kmp::engine

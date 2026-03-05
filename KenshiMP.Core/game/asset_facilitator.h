#pragma once
#include "loading_orchestrator.h"
#include <string>

namespace kmp {

// AssetFacilitator — simplified facade for asset/loading operations.
//
// Instead of hooks and spawn systems needing to interact with
// LoadingOrchestrator directly, they call the Facilitator for
// high-level queries like "can we spawn right now?"
//
// Example:
//   auto& fac = AssetFacilitator::Get();
//   if (fac.CanSpawn()) { /* proceed with spawn */ }
//   fac.PreloadTemplate("Greenlander");
//
// Thin delegation layer — no logic duplication.

class AssetFacilitator {
public:
    static AssetFacilitator& Get();

    // Must be called once after LoadingOrchestrator is constructed
    void Bind(LoadingOrchestrator* orchestrator);
    void Unbind();
    bool IsBound() const { return m_orch != nullptr; }

    // ════════════════════════════════════════════════════════════════
    // Spawn Safety (used by entity_hooks and SyncOrchestrator)
    // ════════════════════════════════════════════════════════════════

    // Can we safely spawn a character right now?
    // Checks: loading phase, pending resources, cooldown timer.
    // Returns true if no orchestrator is bound (graceful degradation).
    bool CanSpawn() const;

    // Request preloading resources for a template (anticipatory)
    void PreloadTemplate(const std::string& templateName);

    // ════════════════════════════════════════════════════════════════
    // Loading Phase (used by entity_hooks burst detection)
    // ════════════════════════════════════════════════════════════════

    bool IsGameLoading() const;
    bool IsZoneLoading() const;
    LoadingPhase GetPhase() const;

    // ════════════════════════════════════════════════════════════════
    // Diagnostics
    // ════════════════════════════════════════════════════════════════

    struct LoadingStats {
        size_t pendingResources = 0;
        size_t loadedResources  = 0;
        LoadingPhase currentPhase = LoadingPhase::Idle;
        bool safeToSpawn = true;
        bool gameLoaded  = false;
        int  burstCount  = 0;
    };

    LoadingStats GetStats() const;

private:
    AssetFacilitator() = default;
    LoadingOrchestrator* m_orch = nullptr;
};

} // namespace kmp

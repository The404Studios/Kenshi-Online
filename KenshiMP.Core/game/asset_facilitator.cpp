#include "asset_facilitator.h"
#include <spdlog/spdlog.h>

namespace kmp {

AssetFacilitator& AssetFacilitator::Get() {
    static AssetFacilitator instance;
    return instance;
}

void AssetFacilitator::Bind(LoadingOrchestrator* orchestrator) {
    m_orch = orchestrator;
    spdlog::info("AssetFacilitator: Bound to LoadingOrchestrator");
}

void AssetFacilitator::Unbind() {
    m_orch = nullptr;
    spdlog::info("AssetFacilitator: Unbound");
}

bool AssetFacilitator::CanSpawn() const {
    // Graceful degradation: if no orchestrator, allow spawns
    if (!m_orch) return true;
    return m_orch->IsSafeToSpawn();
}

void AssetFacilitator::PreloadTemplate(const std::string& templateName) {
    if (m_orch) {
        m_orch->RequestPreload(templateName);
    }
}

bool AssetFacilitator::IsGameLoading() const {
    if (!m_orch) return false;
    return m_orch->GetPhase() == LoadingPhase::InitialLoad;
}

bool AssetFacilitator::IsZoneLoading() const {
    if (!m_orch) return false;
    return m_orch->GetPhase() == LoadingPhase::ZoneTransition;
}

LoadingPhase AssetFacilitator::GetPhase() const {
    if (!m_orch) return LoadingPhase::Idle;
    return m_orch->GetPhase();
}

AssetFacilitator::LoadingStats AssetFacilitator::GetStats() const {
    LoadingStats stats;
    if (!m_orch) return stats;

    stats.pendingResources = m_orch->GetPendingResourceCount();
    stats.loadedResources  = m_orch->GetLoadedResourceCount();
    stats.currentPhase     = m_orch->GetPhase();
    stats.safeToSpawn      = m_orch->IsSafeToSpawn();
    stats.gameLoaded       = m_orch->IsGameLoaded();
    stats.burstCount       = m_orch->GetBurstCount();
    return stats;
}

} // namespace kmp

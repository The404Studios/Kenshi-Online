#include "pipeline_orchestrator.h"
#include "../core.h"
#include "../hooks/entity_hooks.h"
#include "kmp/protocol.h"
#include "kmp/constants.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>

namespace kmp {

// ══════════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::Initialize(PlayerID localId, EntityRegistry& registry,
                                       SpawnManager& spawnMgr, LoadingOrchestrator& loadingOrch,
                                       NetworkClient& client, NativeHud& hud) {
    m_localPlayerId = localId;
    m_registry      = &registry;
    m_spawnMgr      = &spawnMgr;
    m_loadingOrch   = &loadingOrch;
    m_client        = &client;
    m_hud           = &hud;

    auto now = std::chrono::steady_clock::now();
    m_connectionStart   = now;
    m_lastSnapshotTime  = now;
    m_lastAnomalyCheck  = now;
    m_gameLoadedTimeSet = false;
    m_initialized       = true;

    spdlog::info("PipelineOrchestrator: Initialized for player {}", localId);
}

void PipelineOrchestrator::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;

    {
        std::lock_guard lock(m_remoteMutex);
        m_remoteSnapshots.clear();
        m_remoteSnapshotTimes.clear();
    }
    {
        std::lock_guard lock(m_eventMutex);
        m_events.clear();
        m_pendingBroadcast.clear();
    }
    {
        std::lock_guard lock(m_anomalyMutex);
        m_anomalies.clear();
    }

    spdlog::info("PipelineOrchestrator: Shutdown");
}

// ══════════════════════════════════════════════════════════════════════════════
// TIMING
// ══════════════════════════════════════════════════════════════════════════════

uint32_t PipelineOrchestrator::GetTimestampMs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_connectionStart).count());
}

// ══════════════════════════════════════════════════════════════════════════════
// TICK
// ══════════════════════════════════════════════════════════════════════════════

// SEH wrapper — PipelineOrchestrator::TickInner has C++ objects (lock_guard, etc.)
// that prevent __try from being used directly in it. This thin wrapper catches any AV
// from garbage game pointers (common on Steam where singletons don't resolve).
static bool SEH_PipelineOrchestratorTick(PipelineOrchestrator* self) {
    __try {
        self->TickInner();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PipelineOrchestrator::Tick(float /*deltaTime*/) {
    if (!m_initialized) return;

    if (!SEH_PipelineOrchestratorTick(this)) {
        static int s_tickCrashCount = 0;
        if (++s_tickCrashCount <= 5) {
            spdlog::error("PipelineOrchestrator::Tick SEH crash #{}", s_tickCrashCount);
        }
    }
}

void PipelineOrchestrator::TickInner() {
    auto now = std::chrono::steady_clock::now();

    // Track game loaded time for CanSpawn stuck detection
    if (!m_gameLoadedTimeSet && Core::Get().IsGameLoaded()) {
        m_gameLoadedTime = now;
        m_gameLoadedTimeSet = true;
    }

    // 1Hz snapshot collection + broadcast
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastSnapshotTime);
    if (sinceLast.count() >= SNAPSHOT_INTERVAL_MS) {
        PipelineSnapshot snap = CollectSnapshot();
        {
            std::lock_guard lock(m_snapshotMutex);
            m_lastLocalSnapshot = snap;
        }
        if (Core::Get().IsConnected()) {
            BroadcastSnapshot(snap);
        }
        m_lastSnapshotTime = now;
    }

    // Broadcast pending events
    if (Core::Get().IsConnected()) {
        BroadcastPendingEvents();
    }

    // Anomaly detection (every 2s)
    auto sinceAnomaly = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastAnomalyCheck);
    if (sinceAnomaly.count() >= ANOMALY_CHECK_INTERVAL_MS) {
        RunAnomalyDetection();
        m_lastAnomalyCheck = now;
    }

    // Update HUD if visible
    if (m_hudVisible.load(std::memory_order_relaxed)) {
        UpdateHud();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SNAPSHOT COLLECTION
// ══════════════════════════════════════════════════════════════════════════════

PipelineSnapshot PipelineOrchestrator::CollectSnapshot() const {
    PipelineSnapshot snap{};
    auto& core = Core::Get();

    snap.version   = 1;
    snap.playerId  = m_localPlayerId;
    snap.snapshotTimestamp = GetTimestampMs();

    // entity_hooks state
    snap.totalCreates      = static_cast<uint16_t>(entity_hooks::GetTotalCreates() & 0xFFFF);
    snap.totalDestroys     = static_cast<uint16_t>(entity_hooks::GetTotalDestroys() & 0xFFFF);
    snap.burstCount        = 0; // burst count within current window
    snap.inPlaceSpawnCount = static_cast<uint8_t>(
        std::min(entity_hooks::GetInPlaceSpawnCount(), 255));

    snap.hookFlags = 0;
    auto hookDiags = HookManager::Get().GetDiagnostics();
    for (auto& d : hookDiags) {
        if (d.name == "CharacterCreate" && d.enabled) snap.hookFlags |= 0x01;
        if (d.name == "CharacterDestroy" && d.enabled) snap.hookFlags |= 0x02;
    }
    // Burst detection removed — hook is disabled during loading entirely

    // SpawnManager state
    if (m_spawnMgr) {
        snap.pendingSpawnCount = static_cast<uint16_t>(
            std::min(m_spawnMgr->GetPendingSpawnCount(), (size_t)65535));
        snap.templateCount     = static_cast<uint16_t>(
            std::min(m_spawnMgr->GetTemplateCount(), (size_t)65535));
        snap.charTemplateCount = static_cast<uint8_t>(
            std::min(m_spawnMgr->GetCharacterTemplateCount(), (size_t)255));

        snap.factoryFlags = 0;
        if (m_spawnMgr->IsReady())          snap.factoryFlags |= 0x01;
        if (m_spawnMgr->HasPreCallData())   snap.factoryFlags |= 0x02;
        if (m_spawnMgr->HasRequestStruct()) snap.factoryFlags |= 0x04;
    }

    // LoadingOrchestrator state
    if (m_loadingOrch) {
        snap.loadingPhase     = static_cast<uint8_t>(m_loadingOrch->GetPhase());
        snap.pendingResources = static_cast<uint8_t>(
            std::min(m_loadingOrch->GetPendingResourceCount(), (size_t)255));
        snap.orchFlags = 0;
        if (m_loadingOrch->IsGameLoaded())   snap.orchFlags |= 0x01;
        if (m_loadingOrch->IsSafeToSpawn())  snap.orchFlags |= 0x02;
        if (m_loadingOrch->GetBurstCount() > 0) snap.orchFlags |= 0x04;
    }

    // EntityRegistry state
    if (m_registry) {
        size_t total  = m_registry->GetEntityCount();
        size_t remote = m_registry->GetRemoteCount();
        size_t spawned = m_registry->GetSpawnedRemoteCount();
        snap.localEntityCount   = static_cast<uint16_t>(total > remote ? total - remote : 0);
        snap.remoteEntityCount  = static_cast<uint16_t>(remote);
        snap.spawnedRemoteCount = static_cast<uint16_t>(spawned);
        snap.ghostCount         = static_cast<uint16_t>(remote > spawned ? remote - spawned : 0);
    }

    // Core state
    snap.lastCompletedStep = static_cast<uint8_t>(core.GetLastCompletedStep());
    snap.coreFlags = 0;
    if (core.IsHost())       snap.coreFlags |= 0x01;
    if (core.IsLoading())    snap.coreFlags |= 0x02;
    if (core.IsGameLoaded()) snap.coreFlags |= 0x04;
    if (core.IsConnected())  snap.coreFlags |= 0x08;
    snap.ping = static_cast<uint16_t>(
        std::min(core.GetClient().GetPing(), (uint32_t)65535));
    snap.uptimeSeconds = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_connectionStart).count());

    // SyncOrchestrator state
    auto* syncOrch = core.GetSyncOrchestrator();
    if (syncOrch) {
        snap.syncTickCount = static_cast<uint16_t>(syncOrch->GetTickCount() & 0xFFFF);
        snap.syncFlags = 0;
        if (syncOrch->IsActive()) snap.syncFlags |= 0x01;
    }

    return snap;
}

// ══════════════════════════════════════════════════════════════════════════════
// NETWORK: BROADCAST
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::BroadcastSnapshot(const PipelineSnapshot& snap) {
    if (!m_client) return;

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_PipelineSnapshot);
    writer.WriteRaw(&snap, sizeof(PipelineSnapshot));
    m_client->SendReliableUnordered(writer.Data(), writer.Size());
}

void PipelineOrchestrator::BroadcastPendingEvents() {
    if (!m_client) return;

    std::lock_guard lock(m_eventMutex);
    if (m_pendingBroadcast.empty()) return;

    // Batch up to EVENT_BROADCAST_BATCH events per packet
    int count = static_cast<int>(std::min(m_pendingBroadcast.size(),
                                           (size_t)EVENT_BROADCAST_BATCH));

    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_PipelineEvent);
    writer.WriteU8(static_cast<uint8_t>(count));

    for (int i = 0; i < count; i++) {
        auto& evt = m_pendingBroadcast.front();
        writer.WriteU8(static_cast<uint8_t>(evt.type));
        writer.WriteU8(evt.severity);
        writer.WriteU16(0); // padding
        writer.WriteU32(evt.timestamp);
        writer.WriteU32(evt.entityId);
        writer.WriteU32(evt.auxData);
        std::string detail = evt.detail.substr(0, 64);
        writer.WriteString(detail);
        m_pendingBroadcast.pop_front();
    }

    m_client->SendReliableUnordered(writer.Data(), writer.Size());
}

// ══════════════════════════════════════════════════════════════════════════════
// NETWORK: RECEIVE
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::OnRemoteSnapshot(PlayerID sender, const uint8_t* data, size_t size) {
    if (size < sizeof(PipelineSnapshot)) return;

    PipelineSnapshot snap{};
    std::memcpy(&snap, data, sizeof(PipelineSnapshot));

    // Validate version
    if (snap.version == 0 || snap.version > 1) return;

    {
        std::lock_guard lock(m_remoteMutex);
        m_remoteSnapshots[sender] = snap;
        m_remoteSnapshotTimes[sender] = std::chrono::steady_clock::now();
    }
}

void PipelineOrchestrator::OnRemoteEvent(PlayerID sender, const uint8_t* data, size_t size) {
    PacketReader reader(data, size);

    uint8_t count;
    if (!reader.ReadU8(count)) return;
    if (count > EVENT_BROADCAST_BATCH) count = EVENT_BROADCAST_BATCH;

    std::lock_guard lock(m_eventMutex);
    for (int i = 0; i < count; i++) {
        PipelineEvent evt{};
        uint8_t typeRaw, sev;
        uint16_t pad;
        if (!reader.ReadU8(typeRaw)) break;
        if (!reader.ReadU8(sev)) break;
        if (!reader.ReadU16(pad)) break;
        if (!reader.ReadU32(evt.timestamp)) break;
        if (!reader.ReadU32(evt.entityId)) break;
        if (!reader.ReadU32(evt.auxData)) break;
        if (!reader.ReadString(evt.detail, 64)) break;

        evt.type = static_cast<PipelineEventType>(typeRaw);
        evt.severity = sev;

        // Prefix detail with sender info
        evt.detail = "[P" + std::to_string(sender) + "] " + evt.detail;

        m_events.push_back(std::move(evt));
        if (m_events.size() > MAX_LOCAL_EVENTS) {
            m_events.pop_front();
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// EVENT RECORDING
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::RecordEvent(PipelineEventType type, uint8_t severity,
                                        EntityID entityId, uint32_t auxData,
                                        const std::string& detail) {
    if (!m_initialized) return;

    PipelineEvent evt;
    evt.type      = type;
    evt.severity  = severity;
    evt.timestamp = GetTimestampMs();
    evt.entityId  = entityId;
    evt.auxData   = auxData;
    evt.detail    = detail;

    std::lock_guard lock(m_eventMutex);
    m_events.push_back(evt);
    if (m_events.size() > MAX_LOCAL_EVENTS) {
        m_events.pop_front();
    }
    m_pendingBroadcast.push_back(evt);
}

// ══════════════════════════════════════════════════════════════════════════════
// ANOMALY DETECTION
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::RunAnomalyDetection() {
    auto now = std::chrono::steady_clock::now();
    PipelineSnapshot local;
    {
        std::lock_guard lock(m_snapshotMutex);
        local = m_lastLocalSnapshot;
    }

    std::lock_guard remoteLock(m_remoteMutex);

    for (auto& [peerId, remoteSnap] : m_remoteSnapshots) {
        // Spawn queue mismatch
        if (remoteSnap.pendingSpawnCount > 5 && local.pendingSpawnCount == 0) {
            RaiseAnomaly(AnomalyType::SpawnQueueMismatch, peerId,
                "P" + std::to_string(peerId) + " has " +
                std::to_string(remoteSnap.pendingSpawnCount) +
                " pending spawns, local has 0");
        } else {
            ResolveAnomaly(AnomalyType::SpawnQueueMismatch, peerId);
        }

        // Entity ghost mismatch
        if (remoteSnap.spawnedRemoteCount > 0 && local.ghostCount > 3) {
            RaiseAnomaly(AnomalyType::EntityGhostMismatch, m_localPlayerId,
                "Local has " + std::to_string(local.ghostCount) +
                " ghost entities (no game object)");
        } else {
            ResolveAnomaly(AnomalyType::EntityGhostMismatch, m_localPlayerId);
        }

        // Snapshot staleness
        auto& snapTime = m_remoteSnapshotTimes[peerId];
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - snapTime);
        if (elapsed.count() > SNAPSHOT_STALE_THRESHOLD_MS) {
            RaiseAnomaly(AnomalyType::SnapshotStale, peerId,
                "No snapshot from P" + std::to_string(peerId) +
                " for " + std::to_string(elapsed.count() / 1000) + "s");
        } else {
            ResolveAnomaly(AnomalyType::SnapshotStale, peerId);
        }
    }

    // CanSpawn stuck (local)
    if (m_gameLoadedTimeSet) {
        auto sinceLoad = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_gameLoadedTime);
        bool canSpawn = (local.orchFlags & 0x02) != 0;
        if (!canSpawn && sinceLoad.count() > CANSPAWN_STUCK_THRESHOLD_MS) {
            RaiseAnomaly(AnomalyType::CanSpawnStuck, m_localPlayerId,
                "CanSpawn stuck false for " + std::to_string(sinceLoad.count() / 1000) +
                "s (phase=" + std::string(GetPhaseName(local.loadingPhase)) + ")");
        } else if (canSpawn) {
            ResolveAnomaly(AnomalyType::CanSpawnStuck, m_localPlayerId);
        }
    }

    // Hook not firing
    if (local.uptimeSeconds > 30 && local.totalCreates == 0) {
        RaiseAnomaly(AnomalyType::HookNotFiring, m_localPlayerId,
            "CharacterCreate: 0 calls after " + std::to_string(local.uptimeSeconds) + "s");
    } else if (local.totalCreates > 0) {
        ResolveAnomaly(AnomalyType::HookNotFiring, m_localPlayerId);
    }

    // Phase stuck
    if (local.loadingPhase != 0 && local.uptimeSeconds > 60) {
        RaiseAnomaly(AnomalyType::PhaseStuck, m_localPlayerId,
            "Loading phase " + std::string(GetPhaseName(local.loadingPhase)) +
            " stuck for >60s");
    } else if (local.loadingPhase == 0) {
        ResolveAnomaly(AnomalyType::PhaseStuck, m_localPlayerId);
    }
}

void PipelineOrchestrator::RaiseAnomaly(AnomalyType type, PlayerID source,
                                         const std::string& desc) {
    std::lock_guard lock(m_anomalyMutex);

    // Check if already raised (not resolved)
    for (auto& a : m_anomalies) {
        if (a.type == type && a.sourcePlayer == source && !a.resolved) {
            return; // Already active
        }
    }

    PipelineAnomaly anomaly;
    anomaly.type         = type;
    anomaly.sourcePlayer = source;
    anomaly.detectedAt   = GetTimestampMs();
    anomaly.description  = desc;
    anomaly.resolved     = false;

    m_anomalies.push_back(std::move(anomaly));
    if (m_anomalies.size() > MAX_ANOMALIES) {
        m_anomalies.erase(m_anomalies.begin());
    }

    spdlog::warn("PipelineOrchestrator: ANOMALY — {}", desc);
    if (m_hud) {
        m_hud->AddSystemMessage("[Pipeline] " + desc);
    }
}

void PipelineOrchestrator::ResolveAnomaly(AnomalyType type, PlayerID source) {
    std::lock_guard lock(m_anomalyMutex);
    for (auto& a : m_anomalies) {
        if (a.type == type && a.sourcePlayer == source && !a.resolved) {
            a.resolved = true;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// HUD
// ══════════════════════════════════════════════════════════════════════════════

void PipelineOrchestrator::ToggleHud() {
    bool current = m_hudVisible.load(std::memory_order_relaxed);
    m_hudVisible.store(!current, std::memory_order_relaxed);
}

void PipelineOrchestrator::UpdateHud() {
    if (!m_hud) return;

    PipelineSnapshot local;
    {
        std::lock_guard lock(m_snapshotMutex);
        local = m_lastLocalSnapshot;
    }

    char buf[256];

    snprintf(buf, sizeof(buf), "Phase:%s CanSpawn:%s Burst:%s Step:%d",
             GetPhaseName(local.loadingPhase),
             (local.orchFlags & 0x02) ? "Y" : "N",
             (local.hookFlags & 0x04) ? "Y" : "N",
             local.lastCompletedStep);
    m_hud->LogStep("PIPE", buf);

    snprintf(buf, sizeof(buf), "Creates:%d Destroys:%d InPlace:%d Pending:%d",
             local.totalCreates, local.totalDestroys,
             local.inPlaceSpawnCount, local.pendingSpawnCount);
    m_hud->LogStep("PIPE", buf);

    snprintf(buf, sizeof(buf), "Local:%d Remote:%d Ghost:%d Tpl:%d/%d",
             local.localEntityCount, local.remoteEntityCount,
             local.ghostCount, local.charTemplateCount, local.templateCount);
    m_hud->LogStep("PIPE", buf);

    // Remote peer summaries
    {
        std::lock_guard remoteLock(m_remoteMutex);
        for (auto& [peerId, snap] : m_remoteSnapshots) {
            snprintf(buf, sizeof(buf), "P%u: Phase:%s Spawn:%s Ent:%d/%d Ghost:%d Ping:%d",
                     peerId, GetPhaseName(snap.loadingPhase),
                     (snap.orchFlags & 0x02) ? "Y" : "N",
                     snap.localEntityCount, snap.remoteEntityCount,
                     snap.ghostCount, snap.ping);
            m_hud->LogStep("PEER", buf);
        }
    }

    // Active anomalies
    {
        std::lock_guard anomalyLock(m_anomalyMutex);
        for (auto& a : m_anomalies) {
            if (!a.resolved) {
                m_hud->LogStep("WARN", a.description);
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// QUERIES
// ══════════════════════════════════════════════════════════════════════════════

PipelineSnapshot PipelineOrchestrator::GetLocalSnapshot() const {
    std::lock_guard lock(m_snapshotMutex);
    return m_lastLocalSnapshot;
}

std::unordered_map<PlayerID, PipelineSnapshot> PipelineOrchestrator::GetRemoteSnapshots() const {
    std::lock_guard lock(m_remoteMutex);
    return m_remoteSnapshots;
}

std::vector<PipelineEvent> PipelineOrchestrator::GetRecentEvents(int maxCount) const {
    std::lock_guard lock(m_eventMutex);
    std::vector<PipelineEvent> result;
    int start = static_cast<int>(m_events.size()) - maxCount;
    if (start < 0) start = 0;
    for (int i = start; i < static_cast<int>(m_events.size()); i++) {
        result.push_back(m_events[i]);
    }
    return result;
}

std::vector<PipelineAnomaly> PipelineOrchestrator::GetActiveAnomalies() const {
    std::lock_guard lock(m_anomalyMutex);
    std::vector<PipelineAnomaly> result;
    for (auto& a : m_anomalies) {
        if (!a.resolved) result.push_back(a);
    }
    return result;
}

std::vector<PipelineEvent> PipelineOrchestrator::GetEntityHistory(EntityID entityId, int maxCount) const {
    std::lock_guard lock(m_eventMutex);
    std::vector<PipelineEvent> result;
    for (auto it = m_events.rbegin(); it != m_events.rend() && (int)result.size() < maxCount; ++it) {
        if (it->entityId == entityId) {
            result.push_back(*it);
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// TEXT FORMATTING
// ══════════════════════════════════════════════════════════════════════════════

static const char* EventTypeName(PipelineEventType type) {
    switch (type) {
    case PipelineEventType::SpawnQueued:       return "SpawnQueued";
    case PipelineEventType::SpawnSucceeded:    return "SpawnOK";
    case PipelineEventType::SpawnFailed:       return "SpawnFail";
    case PipelineEventType::SpawnRetried:      return "SpawnRetry";
    case PipelineEventType::SpawnCapReached:   return "SpawnCap";
    case PipelineEventType::BurstDetected:     return "BurstStart";
    case PipelineEventType::BurstEnded:        return "BurstEnd";
    case PipelineEventType::LoadingComplete:   return "LoadDone";
    case PipelineEventType::PhaseChanged:      return "PhaseChg";
    case PipelineEventType::EntityRegistered:  return "EntReg";
    case PipelineEventType::EntityLinked:      return "EntLink";
    case PipelineEventType::EntityUnregistered:return "EntUnreg";
    case PipelineEventType::HookInstalled:     return "HookInst";
    case PipelineEventType::HookCrashed:       return "HookCrash";
    case PipelineEventType::ConnectionLost:    return "ConnLost";
    case PipelineEventType::SnapshotReceived:  return "SnapRecv";
    default: return "???";
    }
}

static void FormatSnapshotBlock(const PipelineSnapshot& snap, const char* label,
                                 std::string& out) {
    char buf[512];

    snprintf(buf, sizeof(buf),
        "  --- %s (Player %u) ---\n"
        "  Phase: %s | CanSpawn: %s | GameLoaded: %s | Burst: %s\n"
        "  Creates: %u | Destroys: %u | InPlace: %u\n"
        "  Pending: %u | Templates: %u/%u | Factory: %s PreCall: %s\n"
        "  Entities — Local: %u Remote: %u Spawned: %u Ghost: %u\n"
        "  Core — Host: %s Loading: %s Step: %d Ping: %ums Uptime: %us\n"
        "  Hooks — Create: %s Destroy: %s\n",
        label, snap.playerId,
        GetPhaseName(snap.loadingPhase),
        (snap.orchFlags & 0x02) ? "YES" : "NO",
        (snap.orchFlags & 0x01) ? "YES" : "NO",
        (snap.hookFlags & 0x04) ? "YES" : "NO",
        snap.totalCreates, snap.totalDestroys, snap.inPlaceSpawnCount,
        snap.pendingSpawnCount, snap.charTemplateCount, snap.templateCount,
        (snap.factoryFlags & 0x01) ? "YES" : "NO",
        (snap.factoryFlags & 0x02) ? "YES" : "NO",
        snap.localEntityCount, snap.remoteEntityCount,
        snap.spawnedRemoteCount, snap.ghostCount,
        (snap.coreFlags & 0x01) ? "YES" : "NO",
        (snap.coreFlags & 0x02) ? "YES" : "NO",
        snap.lastCompletedStep, snap.ping, snap.uptimeSeconds,
        (snap.hookFlags & 0x01) ? "OK" : "OFF",
        (snap.hookFlags & 0x02) ? "OK" : "OFF");

    out += buf;
}

std::string PipelineOrchestrator::FormatStatusDump() const {
    std::string result;
    result.reserve(2048);
    result += "=== Pipeline Status ===\n";

    // Local snapshot
    {
        std::lock_guard lock(m_snapshotMutex);
        FormatSnapshotBlock(m_lastLocalSnapshot, "LOCAL", result);
    }

    // Remote snapshots
    {
        std::lock_guard lock(m_remoteMutex);
        for (auto& [peerId, snap] : m_remoteSnapshots) {
            std::string label = "REMOTE P" + std::to_string(peerId);
            FormatSnapshotBlock(snap, label.c_str(), result);
        }
    }

    // Active anomalies
    auto anomalies = GetActiveAnomalies();
    if (!anomalies.empty()) {
        result += "\n  --- Anomalies ---\n";
        for (auto& a : anomalies) {
            result += "  [!] " + a.description + "\n";
        }
    }

    // Recent events (last 10)
    auto events = GetRecentEvents(10);
    if (!events.empty()) {
        result += "\n  --- Recent Events ---\n";
        for (auto& e : events) {
            char buf[256];
            snprintf(buf, sizeof(buf), "  [%ums] %s ent=%u: %s\n",
                     e.timestamp, EventTypeName(e.type), e.entityId, e.detail.c_str());
            result += buf;
        }
    }

    return result;
}

std::string PipelineOrchestrator::FormatEntityTrack(EntityID entityId) const {
    auto events = GetEntityHistory(entityId, 20);
    if (events.empty()) {
        return "No pipeline events for entity " + std::to_string(entityId);
    }

    std::string result = "=== Entity " + std::to_string(entityId) + " Pipeline History ===\n";
    for (auto& e : events) {
        char buf[256];
        snprintf(buf, sizeof(buf), "  [%ums] %s (sev=%d aux=%u): %s\n",
                 e.timestamp, EventTypeName(e.type), e.severity, e.auxData, e.detail.c_str());
        result += buf;
    }
    return result;
}

} // namespace kmp

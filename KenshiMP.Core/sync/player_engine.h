#pragma once
#include "../game/player_controller.h"
#include "kmp/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

namespace kmp {

// Player lifecycle states
enum class PlayerState : uint8_t {
    Connecting   = 0,  // Handshake in progress
    Loading      = 1,  // World snapshot being received
    InGame       = 2,  // Fully loaded, syncing
    AFK          = 3,  // No input for extended period
    Disconnected = 4,  // Cleaned up, will be removed
};

// Extended player session info
struct PlayerSession {
    PlayerID    playerId    = INVALID_PLAYER;
    std::string name;
    PlayerState state       = PlayerState::Connecting;
    ZoneCoord   zone;
    Vec3        position;
    uint32_t    ping        = 0;

    // Session timing
    std::chrono::steady_clock::time_point joinTime;
    std::chrono::steady_clock::time_point lastActivity;
    std::chrono::steady_clock::time_point lastPositionUpdate;

    // Entity summary
    int entityCount = 0;
    bool isHost     = false;
};

class PlayerEngine {
public:
    explicit PlayerEngine(PlayerController& controller);

    // ---- State Machine ----

    void SetState(PlayerID id, PlayerState newState);
    PlayerState GetState(PlayerID id) const;

    // ---- Registration (delegates to PlayerController + adds session tracking) ----

    void OnHandshakeAck(PlayerID localId, const std::string& name);
    void OnRemotePlayerJoined(PlayerID id, const std::string& name);
    void OnRemotePlayerLeft(PlayerID id);
    void OnWorldSnapshotReceived(int entityCount);

    // ---- Activity Tracking ----

    void RecordActivity(PlayerID id);
    void UpdatePlayerPosition(PlayerID id, const Vec3& pos, const ZoneCoord& zone);
    std::vector<PlayerID> CheckAFK(float afkTimeoutSeconds = 300.f);

    // ---- Query API ----

    const PlayerSession* GetSession(PlayerID id) const;
    std::vector<PlayerID> GetByState(PlayerState state) const;
    std::vector<PlayerID> GetByZone(const ZoneCoord& zone) const;
    PlayerID FindByName(const std::string& partialName) const;
    std::vector<PlayerSession> GetAllSessions() const;

    struct StateCount {
        int connecting = 0, loading = 0, inGame = 0, afk = 0, disconnected = 0;
    };
    StateCount GetStateCounts() const;

    // ---- Local Player ----

    PlayerID GetLocalPlayerId() const;
    const std::string& GetLocalPlayerName() const;
    PlayerState GetLocalState() const { return m_localState; }
    ZoneCoord GetLocalZone() const { return m_localZone; }
    void SetLocalZone(const ZoneCoord& zone) { m_localZone = zone; }

    // ---- Passthrough ----

    PlayerController& Controller() { return m_controller; }
    const PlayerController& Controller() const { return m_controller; }

    // ---- Reset ----
    void Reset();

private:
    PlayerController& m_controller;
    mutable std::mutex m_mutex;

    PlayerState m_localState = PlayerState::Connecting;
    ZoneCoord   m_localZone;
    PlayerID    m_localPlayerId = INVALID_PLAYER;
    std::string m_localPlayerName;

    std::unordered_map<PlayerID, PlayerSession> m_sessions;
};

} // namespace kmp

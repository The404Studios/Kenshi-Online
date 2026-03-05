#include "player_engine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>

namespace kmp {

PlayerEngine::PlayerEngine(PlayerController& controller)
    : m_controller(controller) {}

// ---- State Machine ----

void PlayerEngine::SetState(PlayerID id, PlayerState newState) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (id == m_localPlayerId) {
        m_localState = newState;
        spdlog::info("PlayerEngine: Local player state -> {}",
                     static_cast<int>(newState));
        return;
    }

    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        it->second.state = newState;
        spdlog::info("PlayerEngine: Player {} ({}) state -> {}",
                     id, it->second.name, static_cast<int>(newState));
    }
}

PlayerState PlayerEngine::GetState(PlayerID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (id == m_localPlayerId) return m_localState;

    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        return it->second.state;
    }
    return PlayerState::Disconnected;
}

// ---- Registration ----

void PlayerEngine::OnHandshakeAck(PlayerID localId, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localPlayerId = localId;
    m_localPlayerName = name;
    m_localState = PlayerState::Loading;

    spdlog::info("PlayerEngine: Local player initialized id={} name='{}'", localId, name);
}

void PlayerEngine::OnRemotePlayerJoined(PlayerID id, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::steady_clock::now();
    PlayerSession session;
    session.playerId = id;
    session.name = name;
    session.state = PlayerState::InGame;
    session.joinTime = now;
    session.lastActivity = now;
    session.lastPositionUpdate = now;
    m_sessions[id] = std::move(session);

    spdlog::info("PlayerEngine: Remote player joined id={} name='{}'", id, name);
}

void PlayerEngine::OnRemotePlayerLeft(PlayerID id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        it->second.state = PlayerState::Disconnected;
        spdlog::info("PlayerEngine: Remote player left id={} name='{}'", id, it->second.name);
    }
}

void PlayerEngine::OnWorldSnapshotReceived(int entityCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localState = PlayerState::InGame;
    spdlog::info("PlayerEngine: World snapshot received ({} entities), state -> InGame", entityCount);
}

// ---- Activity Tracking ----

void PlayerEngine::RecordActivity(PlayerID id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        it->second.lastActivity = std::chrono::steady_clock::now();

        // If player was AFK, transition back to InGame
        if (it->second.state == PlayerState::AFK) {
            it->second.state = PlayerState::InGame;
            spdlog::info("PlayerEngine: Player {} ({}) is no longer AFK",
                         id, it->second.name);
        }
    }
}

void PlayerEngine::UpdatePlayerPosition(PlayerID id, const Vec3& pos, const ZoneCoord& zone) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        it->second.position = pos;
        it->second.zone = zone;
        it->second.lastPositionUpdate = std::chrono::steady_clock::now();
        it->second.lastActivity = it->second.lastPositionUpdate;
    }
}

std::vector<PlayerID> PlayerEngine::CheckAFK(float afkTimeoutSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerID> newlyAfk;
    auto now = std::chrono::steady_clock::now();

    for (auto& [id, session] : m_sessions) {
        if (session.state != PlayerState::InGame) continue;

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - session.lastActivity).count();
        if (elapsed >= static_cast<long long>(afkTimeoutSeconds)) {
            session.state = PlayerState::AFK;
            newlyAfk.push_back(id);
            spdlog::info("PlayerEngine: Player {} ({}) is now AFK ({}s idle)",
                         id, session.name, elapsed);
        }
    }
    return newlyAfk;
}

// ---- Query API ----

const PlayerSession* PlayerEngine::GetSession(PlayerID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(id);
    if (it != m_sessions.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<PlayerID> PlayerEngine::GetByState(PlayerState state) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerID> result;
    for (const auto& [id, session] : m_sessions) {
        if (session.state == state) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<PlayerID> PlayerEngine::GetByZone(const ZoneCoord& zone) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerID> result;
    for (const auto& [id, session] : m_sessions) {
        if (session.zone == zone) {
            result.push_back(id);
        }
    }
    return result;
}

PlayerID PlayerEngine::FindByName(const std::string& partialName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Case-insensitive partial match
    std::string lowerPartial = partialName;
    std::transform(lowerPartial.begin(), lowerPartial.end(), lowerPartial.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& [id, session] : m_sessions) {
        std::string lowerName = session.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerName.find(lowerPartial) != std::string::npos) {
            return id;
        }
    }
    return INVALID_PLAYER;
}

std::vector<PlayerSession> PlayerEngine::GetAllSessions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlayerSession> result;
    result.reserve(m_sessions.size());
    for (const auto& [id, session] : m_sessions) {
        result.push_back(session);
    }
    return result;
}

PlayerEngine::StateCount PlayerEngine::GetStateCounts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    StateCount counts;
    for (const auto& [id, session] : m_sessions) {
        switch (session.state) {
        case PlayerState::Connecting:   counts.connecting++; break;
        case PlayerState::Loading:      counts.loading++; break;
        case PlayerState::InGame:       counts.inGame++; break;
        case PlayerState::AFK:          counts.afk++; break;
        case PlayerState::Disconnected: counts.disconnected++; break;
        }
    }
    return counts;
}

// ---- Local Player ----

PlayerID PlayerEngine::GetLocalPlayerId() const {
    return m_localPlayerId;
}

const std::string& PlayerEngine::GetLocalPlayerName() const {
    return m_localPlayerName;
}

// ---- Reset ----

void PlayerEngine::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.clear();
    m_localPlayerId = INVALID_PLAYER;
    m_localPlayerName.clear();
    m_localState = PlayerState::Connecting;
    m_localZone = ZoneCoord{0, 0};
    spdlog::info("PlayerEngine: Reset");
}

} // namespace kmp

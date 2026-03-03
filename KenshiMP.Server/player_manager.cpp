#include "player_manager.h"
#include "server.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace kmp {

// ── Name management ──

static std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

PlayerID PlayerManager::FindByName(
    const std::unordered_map<PlayerID, ConnectedPlayer>& players,
    const std::string& name) {

    std::string needle = ToLower(name);

    // Exact match first
    for (auto& [id, player] : players) {
        if (ToLower(player.name) == needle) return id;
    }

    // Partial match (prefix)
    for (auto& [id, player] : players) {
        std::string lower = ToLower(player.name);
        if (lower.find(needle) == 0) return id;
    }

    // Substring match
    for (auto& [id, player] : players) {
        std::string lower = ToLower(player.name);
        if (lower.find(needle) != std::string::npos) return id;
    }

    return 0;
}

PlayerID PlayerManager::FindByExactName(
    const std::unordered_map<PlayerID, ConnectedPlayer>& players,
    const std::string& name) {

    std::string needle = ToLower(name);
    for (auto& [id, player] : players) {
        if (ToLower(player.name) == needle) return id;
    }
    return 0;
}

bool PlayerManager::IsNameTaken(
    const std::unordered_map<PlayerID, ConnectedPlayer>& players,
    const std::string& name) {

    return FindByExactName(players, name) != 0;
}

std::string PlayerManager::MakeUniqueName(
    const std::unordered_map<PlayerID, ConnectedPlayer>& players,
    const std::string& baseName) {

    if (!IsNameTaken(players, baseName)) return baseName;

    for (int suffix = 2; suffix < 100; suffix++) {
        std::string candidate = baseName + "_" + std::to_string(suffix);
        if (!IsNameTaken(players, candidate)) return candidate;
    }
    return baseName + "_" + std::to_string(rand() % 9999);
}

// ── Ban list ──

void PlayerManager::BanIP(const std::string& ip) {
    m_bannedIPs.insert(ip);
    spdlog::info("PlayerManager: Banned IP {}", ip);
}

void PlayerManager::UnbanIP(const std::string& ip) {
    m_bannedIPs.erase(ip);
    spdlog::info("PlayerManager: Unbanned IP {}", ip);
}

bool PlayerManager::IsIPBanned(const std::string& ip) const {
    return m_bannedIPs.count(ip) > 0;
}

std::vector<std::string> PlayerManager::GetBanList() const {
    return std::vector<std::string>(m_bannedIPs.begin(), m_bannedIPs.end());
}

// ── AFK detection ──

std::vector<PlayerID> PlayerManager::GetAFKPlayers(
    const std::unordered_map<PlayerID, ConnectedPlayer>& players,
    float currentTime, float timeoutSeconds) {

    std::vector<PlayerID> afk;
    for (auto& [id, player] : players) {
        if (currentTime - player.lastUpdate > timeoutSeconds) {
            afk.push_back(id);
        }
    }
    return afk;
}

// ── Rate limiting ──

bool PlayerManager::CheckRateLimit(PlayerID id, float currentTime,
                                    float windowSeconds, int maxMessages) {
    auto it = m_rateLimits.find(id);
    if (it == m_rateLimits.end()) return false;

    auto& timestamps = it->second.timestamps;

    // Count messages within the window
    int count = 0;
    for (auto& t : timestamps) {
        if (currentTime - t <= windowSeconds) count++;
    }
    return count >= maxMessages;
}

void PlayerManager::RecordMessage(PlayerID id, float currentTime) {
    m_rateLimits[id].timestamps.push_back(currentTime);
}

void PlayerManager::CleanupRateLimits(float currentTime, float windowSeconds) {
    for (auto& [id, entry] : m_rateLimits) {
        entry.timestamps.erase(
            std::remove_if(entry.timestamps.begin(), entry.timestamps.end(),
                           [&](float t) { return currentTime - t > windowSeconds; }),
            entry.timestamps.end());
    }
}

} // namespace kmp

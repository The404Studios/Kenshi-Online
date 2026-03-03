#pragma once
#include "kmp/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace kmp {

struct ConnectedPlayer; // Forward declaration from server.h

// Player management utilities for the dedicated server.
// Provides name lookup, ban list, AFK detection, and rate limiting.
class PlayerManager {
public:
    // ── Name management ──

    // Find a player by name (case-insensitive partial match).
    // Returns the player ID, or 0 if not found.
    static PlayerID FindByName(const std::unordered_map<PlayerID, ConnectedPlayer>& players,
                                const std::string& name);

    // Find a player by exact name (case-insensitive).
    static PlayerID FindByExactName(const std::unordered_map<PlayerID, ConnectedPlayer>& players,
                                     const std::string& name);

    // Check if a name is already taken (case-insensitive).
    static bool IsNameTaken(const std::unordered_map<PlayerID, ConnectedPlayer>& players,
                            const std::string& name);

    // Generate a unique name if the requested one is taken (appends _2, _3, etc.)
    static std::string MakeUniqueName(const std::unordered_map<PlayerID, ConnectedPlayer>& players,
                                       const std::string& baseName);

    // ── Ban list ──

    void BanIP(const std::string& ip);
    void UnbanIP(const std::string& ip);
    bool IsIPBanned(const std::string& ip) const;
    std::vector<std::string> GetBanList() const;

    // ── AFK detection ──

    // Get players who haven't sent updates in `timeoutSeconds`.
    static std::vector<PlayerID> GetAFKPlayers(
        const std::unordered_map<PlayerID, ConnectedPlayer>& players,
        float currentTime, float timeoutSeconds = 300.f);

    // ── Rate limiting ──

    // Check if a player has exceeded message rate (returns true if should be throttled).
    bool CheckRateLimit(PlayerID id, float currentTime, float windowSeconds = 1.f, int maxMessages = 10);
    void RecordMessage(PlayerID id, float currentTime);
    void CleanupRateLimits(float currentTime, float windowSeconds = 5.f);

private:
    std::unordered_set<std::string> m_bannedIPs;

    struct RateLimitEntry {
        std::vector<float> timestamps;
    };
    std::unordered_map<PlayerID, RateLimitEntry> m_rateLimits;
};

} // namespace kmp

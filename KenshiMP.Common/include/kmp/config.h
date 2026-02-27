#pragma once
#include "constants.h"
#include <string>
#include <cstdint>

namespace kmp {

struct ClientConfig {
    std::string playerName   = "Player";
    std::string lastServer   = "127.0.0.1";
    uint16_t    lastPort     = KMP_DEFAULT_PORT;
    bool        autoConnect  = true;
    float       overlayScale = 1.0f;

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    static std::string GetDefaultPath();
};

struct ServerConfig {
    std::string serverName   = "KenshiMP Server";
    uint16_t    port         = KMP_DEFAULT_PORT;
    int         maxPlayers   = KMP_MAX_PLAYERS;
    std::string password;
    std::string savePath     = "world.kmpsave";
    int         tickRate     = KMP_TICK_RATE;
    bool        pvpEnabled   = true;
    float       gameSpeed    = 1.0f;

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};

} // namespace kmp

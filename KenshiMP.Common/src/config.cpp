#include "kmp/config.h"
#include "kmp/constants.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shlobj.h>
#include <algorithm>

namespace kmp {

using json = nlohmann::json;

// ── Validation helpers ──

template<typename T>
static T Clamp(T value, T lo, T hi) {
    return (std::max)(lo, (std::min)(value, hi));
}

// ── ClientConfig ──

std::string ClientConfig::GetDefaultPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::string dir = std::string(path) + "\\KenshiMP";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\client.json";
    }
    return "client.json";
}

std::string ClientConfig::GetInstancePath() {
    // PID-specific config path so multiple game instances don't collide.
    // Each Kenshi process gets its own config file for saving state.
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::string dir = std::string(path) + "\\KenshiMP";
        CreateDirectoryA(dir.c_str(), nullptr);
        DWORD pid = GetCurrentProcessId();
        return dir + "\\client_" + std::to_string(pid) + ".json";
    }
    return "client.json";
}

bool ClientConfig::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;
        if (j.contains("playerName"))  playerName  = j["playerName"].get<std::string>();
        if (j.contains("lastServer"))  lastServer  = j["lastServer"].get<std::string>();
        if (j.contains("lastPort"))    lastPort    = j["lastPort"].get<uint16_t>();
        if (j.contains("autoConnect")) autoConnect = j["autoConnect"].get<bool>();
        if (j.contains("overlayScale")) overlayScale = j["overlayScale"].get<float>();
        if (j.contains("favoriteServers")) {
            favoriteServers = j["favoriteServers"].get<std::vector<std::string>>();
        }
        if (j.contains("masterServer")) masterServer = j["masterServer"].get<std::string>();
        if (j.contains("masterPort"))   masterPort   = j["masterPort"].get<uint16_t>();
        if (j.contains("useSyncOrchestrator")) useSyncOrchestrator = j["useSyncOrchestrator"].get<bool>();

        // ── Validate loaded values ──
        if (playerName.size() > KMP_MAX_NAME_LENGTH)
            playerName.resize(KMP_MAX_NAME_LENGTH);
        lastPort    = Clamp<uint16_t>(lastPort, 1024, 65535);
        overlayScale = Clamp(overlayScale, 0.1f, 10.0f);
        masterPort  = Clamp<uint16_t>(masterPort, 1024, 65535);

        return true;
    } catch (...) {
        return false;
    }
}

bool ClientConfig::Save(const std::string& path) const {
    json j;
    j["playerName"]  = playerName;
    j["lastServer"]  = lastServer;
    j["lastPort"]    = lastPort;
    j["autoConnect"] = autoConnect;
    j["overlayScale"] = overlayScale;
    j["favoriteServers"] = favoriteServers;
    j["masterServer"] = masterServer;
    j["masterPort"]   = masterPort;
    j["useSyncOrchestrator"] = useSyncOrchestrator;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

// ── ServerConfig ──

bool ServerConfig::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;
        if (j.contains("serverName")) serverName = j["serverName"].get<std::string>();
        if (j.contains("port"))       port       = j["port"].get<uint16_t>();
        if (j.contains("maxPlayers")) maxPlayers = j["maxPlayers"].get<int>();
        if (j.contains("password"))   password   = j["password"].get<std::string>();
        if (j.contains("savePath"))   savePath   = j["savePath"].get<std::string>();
        if (j.contains("tickRate"))   tickRate   = j["tickRate"].get<int>();
        if (j.contains("pvpEnabled")) pvpEnabled = j["pvpEnabled"].get<bool>();
        if (j.contains("gameSpeed"))  gameSpeed  = j["gameSpeed"].get<float>();
        if (j.contains("masterServer")) masterServer = j["masterServer"].get<std::string>();
        if (j.contains("masterPort"))   masterPort   = j["masterPort"].get<uint16_t>();

        // ── Validate loaded values ──
        port        = Clamp<uint16_t>(port, 1024, 65535);
        maxPlayers  = Clamp(maxPlayers, 1, KMP_MAX_PLAYERS);
        tickRate    = Clamp(tickRate, 1, 60);
        gameSpeed   = Clamp(gameSpeed, 0.1f, 10.0f);
        masterPort  = Clamp<uint16_t>(masterPort, 1024, 65535);
        if (serverName.size() > KMP_MAX_NAME_LENGTH)
            serverName.resize(KMP_MAX_NAME_LENGTH);

        return true;
    } catch (...) {
        return false;
    }
}

bool ServerConfig::Save(const std::string& path) const {
    json j;
    j["serverName"] = serverName;
    j["port"]       = port;
    j["maxPlayers"] = maxPlayers;
    j["password"]   = password;
    j["savePath"]   = savePath;
    j["tickRate"]   = tickRate;
    j["pvpEnabled"] = pvpEnabled;
    j["gameSpeed"]  = gameSpeed;
    j["masterServer"] = masterServer;
    j["masterPort"]   = masterPort;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

} // namespace kmp

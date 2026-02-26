#include "kmp/config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <shlobj.h>

namespace kmp {

using json = nlohmann::json;

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

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

} // namespace kmp

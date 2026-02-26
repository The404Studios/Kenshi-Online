#include "injector.h"
#include <Windows.h>
#include <ShlObj.h>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

namespace kmp {

static const char* PLUGIN_LINE = "Plugin=KenshiMP.Core";

bool InstallOgrePlugin(const std::wstring& gamePath) {
    std::wstring cfgPath = gamePath + L"\\Plugins_x64.cfg";

    // Read existing config
    std::ifstream inFile(cfgPath);
    if (!inFile.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    bool alreadyInstalled = false;

    while (std::getline(inFile, line)) {
        // Remove trailing CR if present
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == PLUGIN_LINE) {
            alreadyInstalled = true;
        }
        lines.push_back(line);
    }
    inFile.close();

    if (alreadyInstalled) return true; // Already installed

    // Add our plugin line
    lines.push_back(PLUGIN_LINE);

    // Write back
    std::ofstream outFile(cfgPath);
    if (!outFile.is_open()) return false;

    for (size_t i = 0; i < lines.size(); i++) {
        outFile << lines[i];
        if (i + 1 < lines.size()) outFile << "\n";
    }
    outFile.close();

    return true;
}

bool RemoveOgrePlugin(const std::wstring& gamePath) {
    std::wstring cfgPath = gamePath + L"\\Plugins_x64.cfg";

    std::ifstream inFile(cfgPath);
    if (!inFile.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;

    while (std::getline(inFile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != PLUGIN_LINE) {
            lines.push_back(line);
        }
    }
    inFile.close();

    std::ofstream outFile(cfgPath);
    if (!outFile.is_open()) return false;

    for (size_t i = 0; i < lines.size(); i++) {
        outFile << lines[i];
        if (i + 1 < lines.size()) outFile << "\n";
    }
    outFile.close();

    return true;
}

bool WriteConnectConfig(const char* address, const char* port, const char* playerName) {
    char appData[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return false;
    }

    std::string dir = std::string(appData) + "\\KenshiMP";
    CreateDirectoryA(dir.c_str(), nullptr);

    std::string path = dir + "\\connect.json";
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"address\": \"" << address << "\",\n";
    file << "  \"port\": " << port << ",\n";
    file << "  \"playerName\": \"" << playerName << "\"\n";
    file << "}\n";

    return true;
}

} // namespace kmp

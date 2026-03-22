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
    // Write to client.json — the same file that KenshiMP.Core reads via ClientConfig
    char appData[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return false;
    }

    std::string dir = std::string(appData) + "\\KenshiMP";
    CreateDirectoryA(dir.c_str(), nullptr);

    std::string path = dir + "\\client.json";

    // Parse port as integer
    int portNum = 27800;
    try { portNum = std::stoi(port); } catch (...) {}

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"playerName\": \"" << playerName << "\",\n";
    file << "  \"lastServer\": \"" << address << "\",\n";
    file << "  \"lastPort\": " << portNum << ",\n";
    file << "  \"autoConnect\": true,\n";
    file << "  \"overlayScale\": 1.0\n";
    file << "}\n";

    return true;
}

bool InstallModFile(const std::wstring& gamePath) {
    // Find the kenshi-online.mod source file.
    // Search order: next to injector, one dir up, build output, KenshiMP root.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) exeDir = exeDir.substr(0, lastSlash);

    std::wstring candidates[] = {
        exeDir + L"\\kenshi-online.mod",
        exeDir + L"\\..\\kenshi-online.mod",
        exeDir + L"\\..\\data\\kenshi-online.mod",
        gamePath + L"\\KenshiMP\\kenshi-online.mod",
    };

    std::wstring srcPath;
    for (auto& c : candidates) {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
            srcPath = c;
            break;
        }
    }

    if (srcPath.empty()) {
        // Mod file not found — not fatal, spawn system will fall back
        return false;
    }

    // Copy to data/ directory (always loaded alongside gamedata.base)
    std::wstring dataPath = gamePath + L"\\data\\kenshi-online.mod";
    CopyFileW(srcPath.c_str(), dataPath.c_str(), FALSE);

    // Also copy to mods/kenshi-online/ directory (standard mod location)
    std::wstring modsDir = gamePath + L"\\mods";
    CreateDirectoryW(modsDir.c_str(), nullptr);
    std::wstring modSubDir = modsDir + L"\\kenshi-online";
    CreateDirectoryW(modSubDir.c_str(), nullptr);
    std::wstring modsDstPath = modSubDir + L"\\kenshi-online.mod";
    CopyFileW(srcPath.c_str(), modsDstPath.c_str(), FALSE);

    // Add "kenshi-online" to __mods.list if not already present
    std::wstring modsListPath = gamePath + L"\\data\\__mods.list";
    std::ifstream modsListIn(modsListPath);
    std::vector<std::string> modLines;
    std::string modLine;
    bool alreadyListed = false;
    while (std::getline(modsListIn, modLine)) {
        if (!modLine.empty() && modLine.back() == '\r') modLine.pop_back();
        if (modLine == "kenshi-online") alreadyListed = true;
        modLines.push_back(modLine);
    }
    modsListIn.close();

    if (!alreadyListed) {
        modLines.push_back("kenshi-online");
        std::ofstream modsListOut(modsListPath);
        for (size_t i = 0; i < modLines.size(); i++) {
            modsListOut << modLines[i];
            if (i + 1 < modLines.size()) modsListOut << "\n";
        }
        modsListOut.close();
    }

    return true;
}

} // namespace kmp

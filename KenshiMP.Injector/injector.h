#pragma once
#include <string>

namespace kmp {

// Install "Plugin=KenshiMP.Core" into Plugins_x64.cfg
bool InstallOgrePlugin(const std::wstring& gamePath);

// Remove our plugin line from Plugins_x64.cfg (for clean uninstall)
bool RemoveOgrePlugin(const std::wstring& gamePath);

// Write connection config to %APPDATA%/KenshiMP/connect.json
bool WriteConnectConfig(const char* address, const char* port, const char* playerName);

} // namespace kmp

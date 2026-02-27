#pragma once
#include <string>

namespace kmp {

// Auto-detect Kenshi installation path from Steam registry
std::wstring FindKenshiPath();

// Copy KenshiMP.Core.dll from the injector's directory to the Kenshi game directory
bool CopyPluginDll(const std::wstring& gamePath);

// Launch Kenshi via Steam or direct executable
bool LaunchKenshi(const std::wstring& gamePath);

} // namespace kmp

#pragma once
#include <string>

namespace kmp {

// Auto-detect Kenshi installation path from Steam registry
std::wstring FindKenshiPath();

// Launch Kenshi via Steam or direct executable
bool LaunchKenshi(const std::wstring& gamePath);

} // namespace kmp

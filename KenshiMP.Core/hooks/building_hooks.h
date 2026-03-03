#pragma once

namespace kmp::building_hooks {

bool Install();
void Uninstall();

// Suppress during save load
void SetLoading(bool loading);

} // namespace kmp::building_hooks

#pragma once

namespace kmp::inventory_hooks {

bool Install();
void Uninstall();

// Suppress network sends during loading (items added during save load)
void SetLoading(bool loading);

} // namespace kmp::inventory_hooks

#pragma once
#include <atomic>
namespace kmp::save_hooks {
    bool Install();
    void Uninstall();
    // True while a save is being loaded — other hooks should skip network operations
    bool IsLoading();
}

#pragma once
namespace kmp::time_hooks {
    bool Install();
    void Uninstall();
    void SetServerTime(float timeOfDay, float gameSpeed);
}

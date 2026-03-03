#pragma once
namespace kmp::time_hooks {
    bool Install();
    void Uninstall();
    void SetServerTime(float timeOfDay, float gameSpeed);

    // Read current time from the captured TimeManager (hooked at runtime)
    float GetTimeOfDay();   // 0.0-1.0 (reads timeManager+0x08)
    float GetGameSpeed();   // reads timeManager+0x10
    bool  WriteTimeOfDay(float timeOfDay); // writes timeManager+0x08
    bool  HasTimeManager(); // true if TimeUpdate hook captured the pointer
}

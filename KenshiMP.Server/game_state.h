#pragma once
#include "kmp/types.h"
#include <string>

namespace kmp {

// Game state management utilities for the dedicated server.
// Handles time progression, weather transitions, and world events.
class GameStateManager {
public:
    // ── Time management ──

    void Update(float deltaTime, float gameSpeed);

    float GetTimeOfDay() const { return m_timeOfDay; }
    void  SetTimeOfDay(float time);

    // Get human-readable time string (e.g., "Day 3, 14:30")
    std::string GetTimeString() const;

    // Check if it's currently daytime (0.25 - 0.75)
    bool IsDaytime() const;

    // Get the current game day number
    int GetDayNumber() const { return m_dayNumber; }

    // ── Weather management ──

    int  GetWeatherState() const { return m_weatherState; }
    void SetWeatherState(int state) { m_weatherState = state; }

    // Advance weather randomly (call periodically)
    void UpdateWeather(float deltaTime);

    // ── World events ──

    // Check for day/night transitions (returns true on transition)
    bool CheckDayNightTransition();

    // Get server uptime in seconds
    float GetUptime() const { return m_uptime; }

private:
    float m_timeOfDay = 0.5f;     // 0.0 = midnight, 0.5 = noon
    int   m_dayNumber = 1;
    int   m_weatherState = 0;     // 0=clear, 1=cloudy, 2=dust, 3=rain, 4=acid
    float m_uptime = 0.f;
    float m_weatherTimer = 0.f;
    float m_weatherInterval = 600.f; // Weather change every ~10 minutes
    bool  m_wasDaytime = true;
};

} // namespace kmp

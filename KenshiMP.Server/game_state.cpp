#include "game_state.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace kmp {

void GameStateManager::Update(float deltaTime, float gameSpeed) {
    m_uptime += deltaTime;

    // Advance time of day (24-hour cycle scaled by gameSpeed)
    m_timeOfDay += deltaTime * gameSpeed / 86400.f;
    if (m_timeOfDay >= 1.f) {
        m_timeOfDay -= 1.f;
        m_dayNumber++;
    }

    // Update weather periodically
    UpdateWeather(deltaTime);
}

void GameStateManager::SetTimeOfDay(float time) {
    m_timeOfDay = time - std::floor(time); // Clamp to [0, 1)
    if (m_timeOfDay < 0.f) m_timeOfDay += 1.f;
}

std::string GameStateManager::GetTimeString() const {
    // Convert 0.0-1.0 to hours:minutes
    float hours24 = m_timeOfDay * 24.f;
    int hour = static_cast<int>(hours24) % 24;
    int minute = static_cast<int>((hours24 - std::floor(hours24)) * 60.f);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Day %d, %02d:%02d", m_dayNumber, hour, minute);
    return buf;
}

bool GameStateManager::IsDaytime() const {
    return m_timeOfDay >= 0.25f && m_timeOfDay < 0.75f;
}

void GameStateManager::UpdateWeather(float deltaTime) {
    m_weatherTimer += deltaTime;
    if (m_weatherTimer < m_weatherInterval) return;
    m_weatherTimer = 0.f;

    // Simple random weather transitions
    // Kenshi weather: 0=clear, 1=cloudy, 2=dust storm, 3=rain, 4=acid rain
    // Weighted: clear is most common, acid rain is rare
    int roll = std::rand() % 100;
    if (roll < 40)       m_weatherState = 0; // 40% clear
    else if (roll < 65)  m_weatherState = 1; // 25% cloudy
    else if (roll < 80)  m_weatherState = 2; // 15% dust
    else if (roll < 95)  m_weatherState = 3; // 15% rain
    else                 m_weatherState = 4; // 5% acid rain
}

bool GameStateManager::CheckDayNightTransition() {
    bool isDaytime = IsDaytime();
    if (isDaytime != m_wasDaytime) {
        m_wasDaytime = isDaytime;
        return true; // Transition occurred
    }
    return false;
}

} // namespace kmp

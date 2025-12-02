/*
 * OverlayUI.h - UI Components for Kenshi Online Overlay
 * Renders the actual UI using ImGui
 */

#pragma once

#include <string>
#include <array>

namespace KenshiOnline
{
    class Overlay;

    class OverlayUI
    {
    public:
        explicit OverlayUI(Overlay& overlay);
        ~OverlayUI() = default;

        void Render();

    private:
        // Window rendering functions
        void RenderStatusBar();
        void RenderMainWindow();
        void RenderConnectionPanel();
        void RenderPlayerList();
        void RenderChat();
        void RenderSettings();
        void RenderNotifications();

        // Helper functions
        void RenderHealthBar(float health, float maxHealth, float width);
        const char* GetConnectionStatusText();
        ImVec4 GetConnectionStatusColor();

        Overlay& m_Overlay;

        // UI State
        bool m_ShowMainWindow = true;
        int m_CurrentTab = 0;

        // Connection form
        std::array<char, 64> m_ServerAddressBuffer = {};
        std::array<char, 32> m_UsernameBuffer = {};
        std::array<char, 32> m_PasswordBuffer = {};
        int m_Port = 5555;

        // Chat
        std::array<char, 256> m_ChatInputBuffer = {};
        bool m_ScrollChatToBottom = true;

        // Settings
        float m_OverlayOpacity = 0.94f;
        bool m_ShowFPS = true;
        bool m_ShowPing = true;
        bool m_AlwaysShowStatusBar = true;

        // Animation
        float m_WindowAlpha = 0.0f;
        float m_FadeSpeed = 8.0f;
    };
}

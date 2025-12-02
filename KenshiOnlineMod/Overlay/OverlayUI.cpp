/*
 * OverlayUI.cpp - UI Components Implementation
 */

#include "OverlayUI.h"
#include "Overlay.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace KenshiOnline
{
    OverlayUI::OverlayUI(Overlay& overlay)
        : m_Overlay(overlay)
    {
        // Initialize buffers
        std::strcpy(m_ServerAddressBuffer.data(), "127.0.0.1");
    }

    void OverlayUI::Render()
    {
        // Always render status bar if enabled
        if (m_AlwaysShowStatusBar || m_Overlay.IsVisible())
        {
            RenderStatusBar();
        }

        // Render notifications
        RenderNotifications();

        // Only render main window when overlay is visible
        if (!m_Overlay.IsVisible())
        {
            m_WindowAlpha = std::max(0.0f, m_WindowAlpha - ImGui::GetIO().DeltaTime * m_FadeSpeed);
            return;
        }

        // Fade in
        m_WindowAlpha = std::min(1.0f, m_WindowAlpha + ImGui::GetIO().DeltaTime * m_FadeSpeed);

        // Set global alpha
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_WindowAlpha * m_OverlayOpacity);

        RenderMainWindow();

        ImGui::PopStyleVar();
    }

    void OverlayUI::RenderStatusBar()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                  ImGuiWindowFlags_NoInputs |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing |
                                  ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;

        // Position at top-right
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.6f);

        if (ImGui::Begin("##StatusBar", nullptr, flags))
        {
            // Connection status
            ImVec4 statusColor = GetConnectionStatusColor();
            ImGui::TextColored(statusColor, "[%s]", GetConnectionStatusText());

            // Player count
            if (m_Overlay.GetConnectionStatus() == ConnectionStatus::Connected)
            {
                ImGui::SameLine();
                ImGui::Text("| Players: %d", m_Overlay.GetPlayerCount());
            }

            // Ping
            if (m_ShowPing && m_Overlay.GetConnectionStatus() == ConnectionStatus::Connected)
            {
                ImGui::SameLine();
                int ping = m_Overlay.GetPing();
                ImVec4 pingColor = ping < 50 ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                                   ping < 100 ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
                                   ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(pingColor, "| %dms", ping);
            }

            // FPS
            if (m_ShowFPS)
            {
                ImGui::SameLine();
                ImGui::Text("| %.0f FPS", io.Framerate);
            }

            // Toggle hint
            if (!m_Overlay.IsVisible())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "| [INSERT] Menu");
            }
        }
        ImGui::End();
    }

    void OverlayUI::RenderMainWindow()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Center window
        ImVec2 windowSize(500, 400);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("Kenshi Online", &m_ShowMainWindow, flags))
        {
            // Tab bar
            if (ImGui::BeginTabBar("##MainTabs"))
            {
                if (ImGui::BeginTabItem("Connection"))
                {
                    m_CurrentTab = 0;
                    RenderConnectionPanel();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Players"))
                {
                    m_CurrentTab = 1;
                    RenderPlayerList();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Chat"))
                {
                    m_CurrentTab = 2;
                    RenderChat();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Settings"))
                {
                    m_CurrentTab = 3;
                    RenderSettings();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void OverlayUI::RenderConnectionPanel()
    {
        ConnectionStatus status = m_Overlay.GetConnectionStatus();

        // Status display
        ImGui::TextColored(GetConnectionStatusColor(), "Status: %s", GetConnectionStatusText());
        ImGui::Separator();

        if (status == ConnectionStatus::Connected)
        {
            // Connected view
            ImGui::Text("Server: %s:%d", m_Overlay.GetServerAddress().c_str(), m_Port);
            ImGui::Text("Players Online: %d", m_Overlay.GetPlayerCount());
            ImGui::Text("Ping: %dms", m_Overlay.GetPing());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Disconnect", ImVec2(-1, 30)))
            {
                m_Overlay.OnDisconnect();
            }
        }
        else if (status == ConnectionStatus::Connecting)
        {
            // Connecting view
            ImGui::Text("Connecting to server...");

            // Loading animation
            static float loadingTime = 0.0f;
            loadingTime += ImGui::GetIO().DeltaTime;
            int dots = static_cast<int>(loadingTime * 2) % 4;
            ImGui::Text("Please wait%.*s", dots, "...");

            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(-1, 30)))
            {
                m_Overlay.OnDisconnect();
            }
        }
        else
        {
            // Disconnected view - show connection form
            ImGui::Text("Server Address:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ServerAddress", m_ServerAddressBuffer.data(), m_ServerAddressBuffer.size());

            ImGui::Text("Port:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt("##Port", &m_Port);
            m_Port = std::clamp(m_Port, 1, 65535);

            ImGui::Spacing();

            ImGui::Text("Username:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Username", m_UsernameBuffer.data(), m_UsernameBuffer.size());

            ImGui::Text("Password:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Password", m_PasswordBuffer.data(), m_PasswordBuffer.size(),
                            ImGuiInputTextFlags_Password);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool canConnect = std::strlen(m_ServerAddressBuffer.data()) > 0 &&
                              std::strlen(m_UsernameBuffer.data()) > 0;

            if (!canConnect)
                ImGui::BeginDisabled();

            if (ImGui::Button("Connect", ImVec2(-1, 35)))
            {
                m_Overlay.OnConnect(
                    m_ServerAddressBuffer.data(),
                    m_Port,
                    m_UsernameBuffer.data(),
                    m_PasswordBuffer.data()
                );
            }

            if (!canConnect)
                ImGui::EndDisabled();

            if (status == ConnectionStatus::Error)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    "Connection failed. Check server address and try again.");
            }
        }
    }

    void OverlayUI::RenderPlayerList()
    {
        auto players = m_Overlay.GetPlayers();

        ImGui::Text("Online Players: %d", static_cast<int>(players.size()));
        ImGui::Separator();

        // Player list
        if (ImGui::BeginChild("##PlayerListScroll", ImVec2(0, 0), true))
        {
            if (players.empty())
            {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No players online");
            }
            else
            {
                for (const auto& player : players)
                {
                    ImGui::PushID(player.name.c_str());

                    // Player entry
                    if (ImGui::TreeNode(player.name.c_str()))
                    {
                        // Health bar
                        ImGui::Text("Health:");
                        ImGui::SameLine();
                        RenderHealthBar(player.health, player.maxHealth, 150.0f);
                        ImGui::SameLine();
                        ImGui::Text("%.0f/%.0f", player.health, player.maxHealth);

                        // Position
                        ImGui::Text("Position: (%.1f, %.1f, %.1f)", player.x, player.y, player.z);

                        // Faction
                        ImGui::Text("Faction ID: %d", player.factionId);

                        // Status
                        ImVec4 statusColor = player.isOnline ?
                            ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        ImGui::TextColored(statusColor, player.isOnline ? "Online" : "Offline");

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
    }

    void OverlayUI::RenderChat()
    {
        auto messages = m_Overlay.GetChatMessages();

        // Chat messages area
        float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("##ChatMessages", ImVec2(0, -footerHeight), true))
        {
            for (const auto& msg : messages)
            {
                ImVec4 color;
                std::string prefix;

                switch (msg.type)
                {
                case 0: // Global
                    color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    prefix = "[Global]";
                    break;
                case 1: // Team
                    color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                    prefix = "[Team]";
                    break;
                case 2: // System
                    color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                    prefix = "[System]";
                    break;
                default:
                    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    prefix = "";
                }

                ImGui::TextColored(color, "%s %s: %s", prefix.c_str(), msg.sender.c_str(), msg.message.c_str());
            }

            // Auto-scroll to bottom
            if (m_ScrollChatToBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();

        // Chat input
        ImGui::Separator();

        bool reclaim_focus = false;
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;

        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("##ChatInput", m_ChatInputBuffer.data(), m_ChatInputBuffer.size(), input_flags))
        {
            if (std::strlen(m_ChatInputBuffer.data()) > 0)
            {
                m_Overlay.OnSendChat(m_ChatInputBuffer.data());
                m_ChatInputBuffer[0] = '\0';
                m_ScrollChatToBottom = true;
            }
            reclaim_focus = true;
        }

        ImGui::SameLine();

        if (ImGui::Button("Send", ImVec2(-1, 0)))
        {
            if (std::strlen(m_ChatInputBuffer.data()) > 0)
            {
                m_Overlay.OnSendChat(m_ChatInputBuffer.data());
                m_ChatInputBuffer[0] = '\0';
                m_ScrollChatToBottom = true;
            }
            reclaim_focus = true;
        }

        // Auto-focus on input
        if (reclaim_focus)
        {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    void OverlayUI::RenderSettings()
    {
        ImGui::Text("Overlay Settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Appearance
        if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Opacity", &m_OverlayOpacity, 0.3f, 1.0f, "%.2f");
            ImGui::Checkbox("Always Show Status Bar", &m_AlwaysShowStatusBar);
        }

        // Display
        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Show FPS", &m_ShowFPS);
            ImGui::Checkbox("Show Ping", &m_ShowPing);
        }

        // Controls
        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Toggle Overlay: INSERT");
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Press INSERT to show/hide this menu");
        }

        // About
        if (ImGui::CollapsingHeader("About"))
        {
            ImGui::Text("Kenshi Online Multiplayer Mod");
            ImGui::Text("Version: 1.0.0");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Play Kenshi with your friends!");
        }
    }

    void OverlayUI::RenderNotifications()
    {
        ImGuiIO& io = ImGui::GetIO();
        float deltaTime = io.DeltaTime;

        // Update notification timers and remove expired ones
        for (auto it = m_Notifications.begin(); it != m_Notifications.end();)
        {
            it->timeRemaining -= deltaTime;
            if (it->timeRemaining <= 0)
            {
                it = m_Notifications.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Render notifications in bottom-right corner
        if (m_Notifications.empty())
            return;

        float yOffset = 60.0f;  // Start above status bar
        float padding = 10.0f;
        float notificationWidth = 300.0f;

        for (size_t i = 0; i < m_Notifications.size(); ++i)
        {
            const auto& notif = m_Notifications[i];

            // Calculate alpha based on time remaining
            float alpha = 1.0f;
            if (notif.timeRemaining < NOTIFICATION_FADE_TIME)
            {
                alpha = notif.timeRemaining / NOTIFICATION_FADE_TIME;
            }
            else if (notif.duration - notif.timeRemaining < NOTIFICATION_FADE_TIME)
            {
                alpha = (notif.duration - notif.timeRemaining) / NOTIFICATION_FADE_TIME;
            }

            // Position notification
            ImVec2 pos(io.DisplaySize.x - notificationWidth - padding,
                       io.DisplaySize.y - yOffset - (i * 50.0f));

            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(ImVec2(notificationWidth, 0));
            ImGui::SetNextWindowBgAlpha(0.9f * alpha);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                      ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_AlwaysAutoResize;

            char windowId[32];
            snprintf(windowId, sizeof(windowId), "##Notification%zu", i);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

            // Color the window border based on notification type
            ImVec4 borderColor = GetNotificationColor(notif.type);
            ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

            if (ImGui::Begin(windowId, nullptr, flags))
            {
                // Icon based on type
                const char* icon = "";
                switch (notif.type)
                {
                case NotificationType::Success: icon = "[OK]"; break;
                case NotificationType::Warning: icon = "[!]"; break;
                case NotificationType::Error: icon = "[X]"; break;
                default: icon = "[i]"; break;
                }

                ImGui::TextColored(borderColor, "%s", icon);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", notif.message.c_str());
            }
            ImGui::End();

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor();
        }
    }

    void OverlayUI::ShowNotification(const std::string& message, NotificationType type, float duration)
    {
        Notification notif;
        notif.message = message;
        notif.type = type;
        notif.duration = duration;
        notif.timeRemaining = duration;

        m_Notifications.push_front(notif);

        // Limit number of notifications
        while (m_Notifications.size() > MAX_NOTIFICATIONS)
        {
            m_Notifications.pop_back();
        }
    }

    void OverlayUI::ShowError(const std::string& message, float duration)
    {
        ShowNotification(message, NotificationType::Error, duration);
    }

    void OverlayUI::ShowSuccess(const std::string& message, float duration)
    {
        ShowNotification(message, NotificationType::Success, duration);
    }

    ImVec4 OverlayUI::GetNotificationColor(NotificationType type)
    {
        switch (type)
        {
        case NotificationType::Success: return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        case NotificationType::Warning: return ImVec4(0.9f, 0.7f, 0.1f, 1.0f);
        case NotificationType::Error: return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
        default: return ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
        }
    }

    void OverlayUI::RenderHealthBar(float health, float maxHealth, float width)
    {
        float fraction = maxHealth > 0 ? (health / maxHealth) : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);

        // Color based on health percentage
        ImVec4 color;
        if (fraction > 0.6f)
            color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // Green
        else if (fraction > 0.3f)
            color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);  // Yellow
        else
            color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);  // Red

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(fraction, ImVec2(width, 0));
        ImGui::PopStyleColor();
    }

    const char* OverlayUI::GetConnectionStatusText()
    {
        switch (m_Overlay.GetConnectionStatus())
        {
        case ConnectionStatus::Disconnected: return "Disconnected";
        case ConnectionStatus::Connecting: return "Connecting...";
        case ConnectionStatus::Connected: return "Connected";
        case ConnectionStatus::Error: return "Error";
        default: return "Unknown";
        }
    }

    ImVec4 OverlayUI::GetConnectionStatusColor()
    {
        switch (m_Overlay.GetConnectionStatus())
        {
        case ConnectionStatus::Disconnected: return ImVec4(0.8f, 0.4f, 0.4f, 1.0f);
        case ConnectionStatus::Connecting: return ImVec4(0.8f, 0.8f, 0.4f, 1.0f);
        case ConnectionStatus::Connected: return ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
        case ConnectionStatus::Error: return ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        }
    }
}

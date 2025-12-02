/*
 * OverlayUI.cpp - Complete UI Implementation for Kenshi Online
 * Full-featured overlay with login, server browser, friends, lobbies
 */

#include "OverlayUI.h"
#include "Overlay.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace KenshiOnline
{
    // Color scheme
    namespace Colors
    {
        const ImVec4 Primary = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        const ImVec4 PrimaryDark = ImVec4(0.18f, 0.41f, 0.69f, 1.00f);
        const ImVec4 PrimaryLight = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
        const ImVec4 Secondary = ImVec4(0.96f, 0.73f, 0.18f, 1.00f);
        const ImVec4 Success = ImVec4(0.18f, 0.80f, 0.44f, 1.00f);
        const ImVec4 Danger = ImVec4(0.91f, 0.30f, 0.24f, 1.00f);
        const ImVec4 Warning = ImVec4(0.95f, 0.77f, 0.06f, 1.00f);
        const ImVec4 Background = ImVec4(0.08f, 0.08f, 0.12f, 0.98f);
        const ImVec4 Surface = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        const ImVec4 SurfaceLight = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
        const ImVec4 TextPrimary = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        const ImVec4 TextSecondary = ImVec4(0.70f, 0.70f, 0.75f, 1.00f);
        const ImVec4 TextDisabled = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    }

    OverlayUI::OverlayUI(Overlay& overlay)
        : m_Overlay(overlay)
    {
        m_ScreenTransition.speed = 8.0f;
        m_ScreenTransition.SetImmediate(1.0f);
        m_WindowAlpha.speed = 6.0f;
        m_WindowAlpha.SetImmediate(0.0f);
    }

    void OverlayUI::Update(float deltaTime)
    {
        // Update animations
        m_ScreenTransition.Update(deltaTime);
        m_WindowAlpha.Update(deltaTime);
        m_WindowPosition.Update(deltaTime);

        // Update loading animation
        m_LoadingTime += deltaTime;

        // Update refresh cooldown
        if (m_RefreshCooldown > 0)
            m_RefreshCooldown -= deltaTime;

        // Update notification timers
        for (auto it = m_Notifications.begin(); it != m_Notifications.end();)
        {
            it->timeRemaining -= deltaTime;
            if (it->timeRemaining <= 0)
                it = m_Notifications.erase(it);
            else
                ++it;
        }
    }

    void OverlayUI::Render()
    {
        ImGuiIO& io = ImGui::GetIO();
        float deltaTime = io.DeltaTime;

        Update(deltaTime);

        // Always render status bar and notifications
        if (m_AlwaysShowStatusBar || m_Overlay.IsVisible())
        {
            RenderStatusBar();
        }
        RenderNotifications();

        // Handle overlay visibility with smooth fade
        if (!m_Overlay.IsVisible())
        {
            m_WindowAlpha.target = 0.0f;
            if (m_WindowAlpha.current < 0.01f)
                return;
        }
        else
        {
            m_WindowAlpha.target = 1.0f;
        }

        // Apply global alpha
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_WindowAlpha.current * m_OverlayOpacity);

        // Render current screen
        switch (m_CurrentScreen)
        {
        case UIScreen::Login:
            RenderLoginScreen();
            break;
        case UIScreen::Register:
            RenderRegisterScreen();
            break;
        case UIScreen::MainMenu:
            RenderMainMenu();
            break;
        case UIScreen::ServerBrowser:
            RenderServerBrowser();
            break;
        case UIScreen::Friends:
            RenderFriendsPanel();
            break;
        case UIScreen::Lobby:
            RenderLobbyScreen();
            break;
        case UIScreen::Settings:
            RenderSettingsScreen();
            break;
        case UIScreen::InGame:
            RenderInGameOverlay();
            break;
        }

        // Render modals on top
        RenderPasswordModal();
        RenderCreateLobbyModal();
        RenderInviteFriendsModal();
        RenderConfirmModal();

        ImGui::PopStyleVar();
    }

    void OverlayUI::SetScreen(UIScreen screen)
    {
        if (screen == m_CurrentScreen)
            return;

        m_PreviousScreen = m_CurrentScreen;
        m_CurrentScreen = screen;

        // Trigger transition animation
        m_ScreenTransition.current = 0.0f;
        m_ScreenTransition.target = 1.0f;

        // Determine transition direction
        int currentIndex = static_cast<int>(m_CurrentScreen);
        int previousIndex = static_cast<int>(m_PreviousScreen);
        m_TransitionDirection = currentIndex > previousIndex ? 1.0f : -1.0f;
    }

    // ==================== Login Screen ====================

    void OverlayUI::RenderLoginScreen()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(420, 480);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 30));

        if (ImGui::Begin("##LoginWindow", nullptr, flags))
        {
            // Logo/Title
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);  // Default font, would use larger
            float titleWidth = ImGui::CalcTextSize("KENSHI ONLINE").x;
            ImGui::SetCursorPosX((windowSize.x - titleWidth) * 0.5f - 15);
            ImGui::TextColored(Colors::Primary, "KENSHI ONLINE");
            ImGui::PopFont();

            ImGui::Spacing();
            float subtitleWidth = ImGui::CalcTextSize("Play with your friends").x;
            ImGui::SetCursorPosX((windowSize.x - subtitleWidth) * 0.5f - 15);
            ImGui::TextColored(Colors::TextSecondary, "Play with your friends");

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();

            // Login form
            ImGui::Text("Username");
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::Surface);

            bool enterPressed = false;
            if (ImGui::InputText("##Username", m_UsernameBuffer.data(), m_UsernameBuffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                ImGui::SetKeyboardFocusHere();  // Move to password
            }

            ImGui::Spacing();
            ImGui::Text("Password");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Password", m_PasswordBuffer.data(), m_PasswordBuffer.size(),
                                 ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue))
            {
                enterPressed = true;
            }

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            ImGui::Spacing();

            // Remember me checkbox
            ImGui::Checkbox("Remember me", &m_RememberMe);

            ImGui::Spacing();
            ImGui::Spacing();

            // Error message
            if (!m_LoginError.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Danger);
                ImGui::TextWrapped("%s", m_LoginError.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            // Login button
            bool canLogin = std::strlen(m_UsernameBuffer.data()) > 0 &&
                           std::strlen(m_PasswordBuffer.data()) > 0 &&
                           !m_IsConnecting;

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 14));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

            if (!canLogin || m_IsConnecting)
                ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::PrimaryLight);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Colors::PrimaryDark);

            if ((ImGui::Button(m_IsConnecting ? "Signing in..." : "Sign In", ImVec2(-1, 0)) || enterPressed) && canLogin)
            {
                if (m_OnLogin)
                {
                    m_OnLogin(m_UsernameBuffer.data(), m_PasswordBuffer.data());
                    m_IsConnecting = true;
                    m_LoginError.clear();
                }
            }

            ImGui::PopStyleColor(3);

            if (!canLogin || m_IsConnecting)
                ImGui::EndDisabled();

            ImGui::PopStyleVar(2);

            // Loading spinner
            if (m_IsConnecting)
            {
                ImGui::Spacing();
                RenderLoadingSpinner("##LoginSpinner", 12.0f);
            }

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Register link
            float registerWidth = ImGui::CalcTextSize("Don't have an account? Register").x;
            ImGui::SetCursorPosX((windowSize.x - registerWidth) * 0.5f - 15);
            ImGui::TextColored(Colors::TextSecondary, "Don't have an account?");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary);

            if (ImGui::Button("Register"))
            {
                SetScreen(UIScreen::Register);
            }

            ImGui::PopStyleColor(4);
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ==================== Register Screen ====================

    void OverlayUI::RenderRegisterScreen()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(420, 560);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 30));

        if (ImGui::Begin("##RegisterWindow", nullptr, flags))
        {
            // Back button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::TextSecondary);
            if (ImGui::Button("< Back"))
            {
                SetScreen(UIScreen::Login);
            }
            ImGui::PopStyleColor(2);

            // Title
            ImGui::Spacing();
            float titleWidth = ImGui::CalcTextSize("Create Account").x;
            ImGui::SetCursorPosX((windowSize.x - titleWidth) * 0.5f - 15);
            ImGui::TextColored(Colors::Primary, "Create Account");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::Surface);

            // Username
            ImGui::Text("Username");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##RegUsername", m_UsernameBuffer.data(), m_UsernameBuffer.size());

            ImGui::Spacing();

            // Email
            ImGui::Text("Email");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##RegEmail", m_EmailBuffer.data(), m_EmailBuffer.size());

            ImGui::Spacing();

            // Password
            ImGui::Text("Password");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##RegPassword", m_PasswordBuffer.data(), m_PasswordBuffer.size(),
                            ImGuiInputTextFlags_Password);

            ImGui::Spacing();

            // Confirm Password
            ImGui::Text("Confirm Password");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##RegConfirmPassword", m_ConfirmPasswordBuffer.data(), m_ConfirmPasswordBuffer.size(),
                            ImGuiInputTextFlags_Password);

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);

            ImGui::Spacing();
            ImGui::Spacing();

            // Validation
            bool passwordsMatch = std::strcmp(m_PasswordBuffer.data(), m_ConfirmPasswordBuffer.data()) == 0;
            bool validForm = std::strlen(m_UsernameBuffer.data()) >= 3 &&
                            std::strlen(m_EmailBuffer.data()) > 0 &&
                            std::strlen(m_PasswordBuffer.data()) >= 6 &&
                            passwordsMatch;

            if (std::strlen(m_PasswordBuffer.data()) > 0 && !passwordsMatch)
            {
                ImGui::TextColored(Colors::Danger, "Passwords do not match");
                ImGui::Spacing();
            }

            // Register button
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 14));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

            if (!validForm || m_IsConnecting)
                ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Success);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.85f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.70f, 0.38f, 1.0f));

            if (ImGui::Button(m_IsConnecting ? "Creating account..." : "Create Account", ImVec2(-1, 0)))
            {
                if (m_OnRegister)
                {
                    m_OnRegister(m_UsernameBuffer.data(), m_PasswordBuffer.data(), m_EmailBuffer.data());
                    m_IsConnecting = true;
                }
            }

            ImGui::PopStyleColor(3);

            if (!validForm || m_IsConnecting)
                ImGui::EndDisabled();

            ImGui::PopStyleVar(2);

            if (m_IsConnecting)
            {
                ImGui::Spacing();
                RenderLoadingSpinner("##RegisterSpinner", 12.0f);
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ==================== Main Menu ====================

    void OverlayUI::RenderMainMenu()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(600, 500);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 25));

        if (ImGui::Begin("##MainMenuWindow", nullptr, flags))
        {
            // Header with user info
            ImGui::TextColored(Colors::Primary, "KENSHI ONLINE");
            ImGui::SameLine(windowSize.x - 180);
            ImGui::TextColored(Colors::TextSecondary, "Welcome, %s", m_CurrentUsername.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();

            // Main menu buttons
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 20));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 15));

            float buttonWidth = (windowSize.x - 90) * 0.5f;

            // Play button (large)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::PrimaryLight);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Colors::PrimaryDark);

            if (ImGui::Button("PLAY", ImVec2(-1, 80)))
            {
                SetScreen(UIScreen::ServerBrowser);
            }

            ImGui::PopStyleColor(3);

            ImGui::Spacing();

            // Two column layout
            ImGui::BeginGroup();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::SurfaceLight);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Colors::Surface);

            if (ImGui::Button("Server Browser", ImVec2(buttonWidth, 60)))
            {
                SetScreen(UIScreen::ServerBrowser);
            }

            if (ImGui::Button("Friends", ImVec2(buttonWidth, 60)))
            {
                SetScreen(UIScreen::Friends);
            }

            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();

            if (ImGui::Button("Create Lobby", ImVec2(buttonWidth, 60)))
            {
                m_ShowCreateLobbyModal = true;
            }

            if (ImGui::Button("Settings", ImVec2(buttonWidth, 60)))
            {
                SetScreen(UIScreen::Settings);
            }

            ImGui::PopStyleColor(3);

            ImGui::EndGroup();

            ImGui::PopStyleVar(3);

            // Bottom section - Profile/Stats
            ImGui::SetCursorPosY(windowSize.y - 120);
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginGroup();
            ImGui::TextColored(Colors::TextSecondary, "Level %d", m_UserProfile.level);
            ImGui::Text("Games Played: %d", m_UserProfile.gamesPlayed);
            ImGui::Text("Total Play Time: %s", FormatPlayTime(m_UserProfile.totalPlayTime).c_str());
            ImGui::EndGroup();

            ImGui::SameLine(windowSize.x - 150);

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::Danger);

            if (ImGui::Button("Sign Out", ImVec2(100, 35)))
            {
                if (m_OnLogout)
                    m_OnLogout();
                SetScreen(UIScreen::Login);
                m_IsLoggedIn = false;
            }

            ImGui::PopStyleColor(2);
            ImGui::EndGroup();
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ==================== Server Browser ====================

    void OverlayUI::RenderServerBrowser()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(800, 600);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 15));

        if (ImGui::Begin("Server Browser", nullptr, flags))
        {
            // Back button
            if (ImGui::Button("< Back to Menu"))
            {
                SetScreen(UIScreen::MainMenu);
            }

            ImGui::SameLine(windowSize.x - 180);

            // Refresh button
            bool canRefresh = m_RefreshCooldown <= 0;
            if (!canRefresh)
                ImGui::BeginDisabled();

            if (ImGui::Button("Refresh Servers"))
            {
                if (m_OnRefreshServers)
                {
                    m_OnRefreshServers();
                    m_RefreshCooldown = REFRESH_COOLDOWN_TIME;
                }
            }

            if (!canRefresh)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(Colors::TextDisabled, "(%.0fs)", m_RefreshCooldown);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Filters row
            ImGui::Text("Search:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##ServerSearch", m_ServerSearchBuffer.data(), m_ServerSearchBuffer.size());

            ImGui::SameLine(350);
            ImGui::Checkbox("Show Full", &m_FilterShowFull);
            ImGui::SameLine();
            ImGui::Checkbox("Show Empty", &m_FilterShowEmpty);
            ImGui::SameLine();
            ImGui::Checkbox("Passworded", &m_FilterShowPassworded);

            ImGui::Spacing();

            // Server list
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Surface);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

            if (ImGui::BeginChild("##ServerList", ImVec2(0, -80), true))
            {
                if (m_Servers.empty())
                {
                    ImVec2 center = ImGui::GetWindowSize();
                    ImGui::SetCursorPos(ImVec2(center.x * 0.5f - 80, center.y * 0.5f - 20));
                    ImGui::TextColored(Colors::TextDisabled, "No servers found");
                    ImGui::SetCursorPosX(center.x * 0.5f - 60);
                    ImGui::TextColored(Colors::TextDisabled, "Click Refresh to search");
                }
                else
                {
                    // Table header
                    ImGui::Columns(5, "ServerColumns", true);
                    ImGui::SetColumnWidth(0, 250);
                    ImGui::SetColumnWidth(1, 100);
                    ImGui::SetColumnWidth(2, 100);
                    ImGui::SetColumnWidth(3, 80);
                    ImGui::SetColumnWidth(4, 100);

                    ImGui::TextColored(Colors::TextSecondary, "Server Name");
                    ImGui::NextColumn();
                    ImGui::TextColored(Colors::TextSecondary, "Players");
                    ImGui::NextColumn();
                    ImGui::TextColored(Colors::TextSecondary, "Mode");
                    ImGui::NextColumn();
                    ImGui::TextColored(Colors::TextSecondary, "Ping");
                    ImGui::NextColumn();
                    ImGui::TextColored(Colors::TextSecondary, "Region");
                    ImGui::NextColumn();

                    ImGui::Separator();

                    // Server entries
                    int index = 0;
                    for (const auto& server : m_Servers)
                    {
                        // Apply filters
                        if (!m_FilterShowFull && server.playerCount >= server.maxPlayers)
                            continue;
                        if (!m_FilterShowEmpty && server.playerCount == 0)
                            continue;
                        if (!m_FilterShowPassworded && server.hasPassword)
                            continue;
                        if (std::strlen(m_ServerSearchBuffer.data()) > 0)
                        {
                            std::string search = m_ServerSearchBuffer.data();
                            std::string name = server.name;
                            std::transform(search.begin(), search.end(), search.begin(), ::tolower);
                            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                            if (name.find(search) == std::string::npos)
                                continue;
                        }

                        bool isSelected = (m_SelectedServerIndex == index);
                        ImGui::PushID(index);

                        // Selectable row
                        if (ImGui::Selectable("##ServerRow", isSelected, ImGuiSelectableFlags_SpanAllColumns))
                        {
                            m_SelectedServerIndex = index;
                        }

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                        {
                            // Double-click to join
                            if (server.hasPassword)
                            {
                                m_ShowPasswordModal = true;
                            }
                            else if (m_OnJoinServer)
                            {
                                m_OnJoinServer(server, "");
                            }
                        }

                        ImGui::SameLine();

                        // Server name with icons
                        if (server.isOfficial)
                            ImGui::TextColored(Colors::Secondary, "[Official] ");
                        else
                            ImGui::Text("           ");
                        ImGui::SameLine();
                        if (server.hasPassword)
                            ImGui::Text("[Lock] ");
                        ImGui::SameLine();
                        ImGui::Text("%s", server.name.c_str());

                        ImGui::NextColumn();

                        // Player count with color
                        float fillRatio = static_cast<float>(server.playerCount) / server.maxPlayers;
                        ImVec4 playerColor = fillRatio >= 1.0f ? Colors::Danger :
                                            fillRatio >= 0.8f ? Colors::Warning : Colors::Success;
                        ImGui::TextColored(playerColor, "%d/%d", server.playerCount, server.maxPlayers);

                        ImGui::NextColumn();
                        ImGui::Text("%s", server.gameMode.c_str());
                        ImGui::NextColumn();

                        // Ping with color
                        ImVec4 pingColor = server.ping < 50 ? Colors::Success :
                                          server.ping < 100 ? Colors::Warning : Colors::Danger;
                        ImGui::TextColored(pingColor, "%dms", server.ping);

                        ImGui::NextColumn();
                        ImGui::Text("%s", server.region.c_str());
                        ImGui::NextColumn();

                        ImGui::PopID();
                        index++;
                    }

                    ImGui::Columns(1);
                }
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // Bottom action bar
            bool hasSelection = m_SelectedServerIndex >= 0 && m_SelectedServerIndex < static_cast<int>(m_Servers.size());

            if (!hasSelection)
                ImGui::BeginDisabled();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::PrimaryLight);

            if (ImGui::Button("Join Server", ImVec2(150, 0)))
            {
                if (hasSelection)
                {
                    const auto& server = m_Servers[m_SelectedServerIndex];
                    if (server.hasPassword)
                    {
                        m_ShowPasswordModal = true;
                    }
                    else if (m_OnJoinServer)
                    {
                        m_OnJoinServer(server, "");
                    }
                }
            }

            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors::SurfaceLight);

            if (ImGui::Button("Server Info", ImVec2(120, 0)))
            {
                // Show server info modal
            }

            ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);

            if (!hasSelection)
                ImGui::EndDisabled();

            // Server count
            ImGui::SameLine(windowSize.x - 180);
            ImGui::TextColored(Colors::TextSecondary, "%zu servers found", m_Servers.size());
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ==================== Friends Panel ====================

    void OverlayUI::RenderFriendsPanel()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(450, 550);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 15));

        if (ImGui::Begin("Friends", nullptr, flags))
        {
            // Back button
            if (ImGui::Button("< Back"))
            {
                SetScreen(UIScreen::MainMenu);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Add friend input
            ImGui::Text("Add Friend:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::InputText("##AddFriend", m_FriendSearchBuffer.data(), m_FriendSearchBuffer.size());
            ImGui::PopStyleVar();

            ImGui::SameLine();
            bool canAdd = std::strlen(m_FriendSearchBuffer.data()) > 0;
            if (!canAdd)
                ImGui::BeginDisabled();

            if (ImGui::Button("Add"))
            {
                if (m_OnAddFriend)
                {
                    m_OnAddFriend(m_FriendSearchBuffer.data());
                    m_FriendSearchBuffer[0] = '\0';
                }
            }

            if (!canAdd)
                ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Spacing();

            // Friend requests section
            bool hasIncoming = false;
            for (const auto& f : m_Friends)
            {
                if (f.isIncomingRequest)
                {
                    hasIncoming = true;
                    break;
                }
            }

            if (hasIncoming)
            {
                ImGui::TextColored(Colors::Secondary, "Friend Requests");
                ImGui::Separator();

                for (const auto& f : m_Friends)
                {
                    if (!f.isIncomingRequest)
                        continue;

                    ImGui::PushID(f.id.c_str());

                    ImGui::Text("%s", f.username.c_str());
                    ImGui::SameLine(250);

                    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Success);
                    if (ImGui::SmallButton("Accept"))
                    {
                        if (m_OnAcceptFriend)
                            m_OnAcceptFriend(f.id);
                    }
                    ImGui::PopStyleColor();

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Danger);
                    if (ImGui::SmallButton("Decline"))
                    {
                        if (m_OnRemoveFriend)
                            m_OnRemoveFriend(f.id);
                    }
                    ImGui::PopStyleColor();

                    ImGui::PopID();
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }

            // Friends list
            ImGui::TextColored(Colors::TextSecondary, "Friends (%zu)", m_Friends.size());
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Surface);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

            if (ImGui::BeginChild("##FriendsList", ImVec2(0, 0), true))
            {
                // Sort friends by status (online first)
                std::vector<const FriendInfo*> sortedFriends;
                for (const auto& f : m_Friends)
                {
                    if (!f.isIncomingRequest && !f.isPendingRequest)
                        sortedFriends.push_back(&f);
                }

                std::sort(sortedFriends.begin(), sortedFriends.end(),
                    [](const FriendInfo* a, const FriendInfo* b) {
                        if (a->status != b->status)
                            return static_cast<int>(a->status) < static_cast<int>(b->status);
                        return a->username < b->username;
                    });

                if (sortedFriends.empty())
                {
                    ImGui::TextColored(Colors::TextDisabled, "No friends yet");
                    ImGui::TextColored(Colors::TextDisabled, "Add friends to play together!");
                }
                else
                {
                    int index = 0;
                    for (const auto* friendPtr : sortedFriends)
                    {
                        RenderFriendCard(*friendPtr, index++);
                    }
                }
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void OverlayUI::RenderFriendCard(const FriendInfo& friendInfo, int index)
    {
        ImGui::PushID(index);

        ImVec2 cardSize(ImGui::GetContentRegionAvail().x, 60);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::SurfaceLight);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

        if (ImGui::BeginChild("##FriendCard", cardSize, true))
        {
            // Status indicator
            ImVec4 statusColor = GetStatusColor(friendInfo.status);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            drawList->AddCircleFilled(ImVec2(p.x + 8, p.y + 20), 5, ImGui::ColorConvertFloat4ToU32(statusColor));

            ImGui::SetCursorPosX(25);

            // Name and status
            ImGui::BeginGroup();
            ImGui::Text("%s", friendInfo.username.c_str());
            ImGui::TextColored(statusColor, "%s", GetStatusText(friendInfo.status));
            if (friendInfo.status == FriendStatus::InGame && !friendInfo.currentServer.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(Colors::TextDisabled, "- %s", friendInfo.currentServer.c_str());
            }
            ImGui::EndGroup();

            // Action buttons
            ImGui::SameLine(cardSize.x - 120);
            ImGui::BeginGroup();

            if (friendInfo.status != FriendStatus::Offline)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
                if (ImGui::SmallButton("Invite"))
                {
                    if (m_OnInviteFriend)
                        m_OnInviteFriend(friendInfo.id);
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.2f, 0.2f, 0.8f));
            if (ImGui::SmallButton("Remove"))
            {
                m_ConfirmModalTitle = "Remove Friend";
                m_ConfirmModalMessage = "Are you sure you want to remove " + friendInfo.username + " from your friends?";
                m_ConfirmModalCallback = [this, id = friendInfo.id]() {
                    if (m_OnRemoveFriend)
                        m_OnRemoveFriend(id);
                };
                m_ShowConfirmModal = true;
            }
            ImGui::PopStyleColor();

            ImGui::EndGroup();
        }
        ImGui::EndChild();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PopID();
    }

    // ==================== Lobby Screen ====================

    void OverlayUI::RenderLobbyScreen()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(700, 550);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(25, 20));

        std::string windowTitle = "Lobby - " + m_CurrentLobby.name;
        if (ImGui::Begin(windowTitle.c_str(), nullptr, flags))
        {
            // Leave button
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Danger);
            if (ImGui::Button("Leave Lobby"))
            {
                if (m_OnLeaveLobby)
                    m_OnLeaveLobby();
                SetScreen(UIScreen::MainMenu);
            }
            ImGui::PopStyleColor();

            ImGui::SameLine(windowSize.x - 180);

            // Invite button
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
            if (ImGui::Button("Invite Friends"))
            {
                m_ShowInviteFriendsModal = true;
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Lobby info
            ImGui::TextColored(Colors::TextSecondary, "Host: %s", m_CurrentLobby.hostName.c_str());
            ImGui::SameLine(300);
            ImGui::TextColored(Colors::TextSecondary, "Game Mode: %s", m_CurrentLobby.gameMode.c_str());
            ImGui::SameLine(500);
            ImGui::TextColored(Colors::TextSecondary, "Players: %d/%d",
                              m_CurrentLobby.playerCount, m_CurrentLobby.maxPlayers);

            ImGui::Spacing();
            ImGui::Spacing();

            // Player slots grid
            ImGui::Text("Players:");
            ImGui::Spacing();

            float slotWidth = (windowSize.x - 80) / 2.0f;
            float slotHeight = 80.0f;

            for (int i = 0; i < m_CurrentLobby.maxPlayers; i++)
            {
                if (i > 0 && i % 2 == 0)
                {
                    // New row
                }
                else if (i > 0)
                {
                    ImGui::SameLine();
                }

                const LobbyPlayer* player = nullptr;
                if (i < static_cast<int>(m_CurrentLobby.players.size()))
                {
                    player = &m_CurrentLobby.players[i];
                }

                bool isLocalPlayer = player && player->username == m_CurrentUsername;
                RenderLobbyPlayerSlot(player, i, isLocalPlayer);
            }

            // Bottom action bar
            ImGui::SetCursorPosY(windowSize.y - 90);
            ImGui::Separator();
            ImGui::Spacing();

            // Ready checkbox
            if (ImGui::Checkbox("Ready", &m_IsReady))
            {
                if (m_OnReadyUp)
                    m_OnReadyUp(m_IsReady);
            }

            // Check if all players are ready
            bool allReady = true;
            for (const auto& player : m_CurrentLobby.players)
            {
                if (!player.isReady)
                {
                    allReady = false;
                    break;
                }
            }

            // Start game button (host only)
            bool isHost = false;
            for (const auto& player : m_CurrentLobby.players)
            {
                if (player.username == m_CurrentUsername && player.isHost)
                {
                    isHost = true;
                    break;
                }
            }

            if (isHost)
            {
                ImGui::SameLine(windowSize.x - 200);

                bool canStart = allReady && m_CurrentLobby.players.size() >= 1;
                if (!canStart)
                    ImGui::BeginDisabled();

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(25, 15));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, Colors::Success);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.85f, 0.50f, 1.0f));

                if (ImGui::Button("START GAME"))
                {
                    if (m_OnStartGame)
                        m_OnStartGame();
                }

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);

                if (!canStart)
                    ImGui::EndDisabled();
            }

            if (!allReady)
            {
                ImGui::SameLine(300);
                ImGui::TextColored(Colors::Warning, "Waiting for all players to ready up...");
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    void OverlayUI::RenderLobbyPlayerSlot(const LobbyPlayer* player, int slot, bool isLocalPlayer)
    {
        ImGui::PushID(slot);

        ImVec2 slotSize(280, 70);

        ImVec4 bgColor = player ? (isLocalPlayer ? ImVec4(0.15f, 0.25f, 0.35f, 1.0f) : Colors::Surface)
                                : ImVec4(0.08f, 0.08f, 0.10f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, bgColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);

        if (ImGui::BeginChild("##PlayerSlot", slotSize, true))
        {
            if (player)
            {
                // Ready indicator
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec4 readyColor = player->isReady ? Colors::Success : Colors::TextDisabled;
                drawList->AddCircleFilled(ImVec2(p.x + 10, p.y + 25), 6, ImGui::ColorConvertFloat4ToU32(readyColor));

                ImGui::SetCursorPosX(30);

                // Player info
                ImGui::BeginGroup();

                if (player->isHost)
                {
                    ImGui::TextColored(Colors::Secondary, "[Host] %s", player->username.c_str());
                }
                else
                {
                    ImGui::Text("%s", player->username.c_str());
                }

                ImGui::TextColored(Colors::TextSecondary, "Level %d", player->characterLevel);

                if (!player->selectedCharacter.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(Colors::TextDisabled, "- %s", player->selectedCharacter.c_str());
                }

                ImGui::EndGroup();

                // Ready status
                ImGui::SameLine(slotSize.x - 60);
                if (player->isReady)
                {
                    ImGui::TextColored(Colors::Success, "READY");
                }
                else
                {
                    ImGui::TextColored(Colors::TextDisabled, "...");
                }
            }
            else
            {
                // Empty slot
                ImGui::SetCursorPos(ImVec2(slotSize.x * 0.5f - 40, slotSize.y * 0.5f - 10));
                ImGui::TextColored(Colors::TextDisabled, "Empty Slot");
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::PopID();
    }

    // ==================== Settings Screen ====================

    void OverlayUI::RenderSettingsScreen()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 windowSize(500, 450);
        ImVec2 windowPos((io.DisplaySize.x - windowSize.x) * 0.5f,
                         (io.DisplaySize.y - windowSize.y) * 0.5f);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(25, 20));

        if (ImGui::Begin("Settings", nullptr, flags))
        {
            // Back button
            if (ImGui::Button("< Back"))
            {
                SetScreen(UIScreen::MainMenu);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Appearance
            if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                ImGui::Text("Overlay Opacity");
                ImGui::SliderFloat("##Opacity", &m_OverlayOpacity, 0.5f, 1.0f, "%.0f%%");

                ImGui::Text("UI Scale");
                ImGui::SliderInt("##UIScale", &m_UIScale, 80, 150, "%d%%");

                ImGui::Checkbox("Enable Animations", &m_EnableAnimations);

                ImGui::Unindent();
                ImGui::Spacing();
            }

            // Display
            if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                ImGui::Checkbox("Show FPS Counter", &m_ShowFPS);
                ImGui::Checkbox("Show Ping", &m_ShowPing);
                ImGui::Checkbox("Always Show Status Bar", &m_AlwaysShowStatusBar);

                ImGui::Unindent();
                ImGui::Spacing();
            }

            // Audio
            if (ImGui::CollapsingHeader("Audio"))
            {
                ImGui::Indent();

                ImGui::Checkbox("Enable UI Sounds", &m_EnableSounds);

                ImGui::Unindent();
                ImGui::Spacing();
            }

            // Controls
            if (ImGui::CollapsingHeader("Controls"))
            {
                ImGui::Indent();

                ImGui::TextColored(Colors::TextSecondary, "Toggle Overlay:");
                ImGui::SameLine(200);
                ImGui::Text("INSERT");

                ImGui::Unindent();
                ImGui::Spacing();
            }

            // About
            if (ImGui::CollapsingHeader("About"))
            {
                ImGui::Indent();

                ImGui::Text("Kenshi Online");
                ImGui::TextColored(Colors::TextSecondary, "Version 2.0.0");
                ImGui::Spacing();
                ImGui::TextColored(Colors::TextDisabled, "Play Kenshi with your friends!");
                ImGui::TextColored(Colors::TextDisabled, "github.com/The404Studios/Kenshi-Online");

                ImGui::Unindent();
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ==================== In-Game Overlay ====================

    void OverlayUI::RenderInGameOverlay()
    {
        // Minimal in-game HUD
        ImGuiIO& io = ImGui::GetIO();

        // Player list (side panel)
        ImGui::SetNextWindowPos(ImVec2(10, 100), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200, 300), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.7f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;

        if (ImGui::Begin("##InGamePlayers", nullptr, flags))
        {
            ImGui::TextColored(Colors::Primary, "Players Online");
            ImGui::Separator();

            auto players = m_Overlay.GetPlayers();
            for (const auto& player : players)
            {
                ImVec4 statusColor = player.isOnline ? Colors::Success : Colors::TextDisabled;
                ImGui::TextColored(statusColor, "%s", player.name.c_str());

                // Mini health bar
                float healthPct = player.maxHealth > 0 ? player.health / player.maxHealth : 0;
                ImGui::SameLine(150);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                    healthPct > 0.5f ? Colors::Success :
                    healthPct > 0.25f ? Colors::Warning : Colors::Danger);
                ImGui::ProgressBar(healthPct, ImVec2(40, 8), "");
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();

        // Chat box
        ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - 250), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);

        if (ImGui::Begin("##InGameChat", nullptr, flags))
        {
            // Chat messages
            if (ImGui::BeginChild("##ChatMessages", ImVec2(0, -30), false))
            {
                auto messages = m_Overlay.GetChatMessages();
                for (const auto& msg : messages)
                {
                    ImVec4 color = msg.type == 2 ? Colors::Warning : Colors::TextPrimary;
                    ImGui::TextColored(color, "[%s]: %s", msg.sender.c_str(), msg.message.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            // Chat input
            ImGui::SetNextItemWidth(-50);
            if (ImGui::InputText("##ChatInput", m_ChatInputBuffer.data(), m_ChatInputBuffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (std::strlen(m_ChatInputBuffer.data()) > 0)
                {
                    m_Overlay.OnSendChat(m_ChatInputBuffer.data());
                    m_ChatInputBuffer[0] = '\0';
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Send"))
            {
                if (std::strlen(m_ChatInputBuffer.data()) > 0)
                {
                    m_Overlay.OnSendChat(m_ChatInputBuffer.data());
                    m_ChatInputBuffer[0] = '\0';
                }
            }
        }
        ImGui::End();
    }

    // ==================== UI Components ====================

    void OverlayUI::RenderStatusBar()
    {
        ImGuiIO& io = ImGui::GetIO();

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.7f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));

        if (ImGui::Begin("##StatusBar", nullptr, flags))
        {
            // Connection status
            ConnectionStatus status = m_Overlay.GetConnectionStatus();
            ImVec4 statusColor;
            const char* statusText;

            switch (status)
            {
            case ConnectionStatus::Connected:
                statusColor = Colors::Success;
                statusText = "Online";
                break;
            case ConnectionStatus::Connecting:
                statusColor = Colors::Warning;
                statusText = "Connecting...";
                break;
            case ConnectionStatus::Error:
                statusColor = Colors::Danger;
                statusText = "Error";
                break;
            default:
                statusColor = Colors::TextDisabled;
                statusText = "Offline";
            }

            ImGui::TextColored(statusColor, "[%s]", statusText);

            if (status == ConnectionStatus::Connected)
            {
                ImGui::SameLine();
                ImGui::Text("| %d players", m_Overlay.GetPlayerCount());

                if (m_ShowPing)
                {
                    ImGui::SameLine();
                    int ping = m_Overlay.GetPing();
                    ImVec4 pingColor = ping < 50 ? Colors::Success :
                                       ping < 100 ? Colors::Warning : Colors::Danger;
                    ImGui::TextColored(pingColor, "| %dms", ping);
                }
            }

            if (m_ShowFPS)
            {
                ImGui::SameLine();
                ImGui::Text("| %.0f FPS", io.Framerate);
            }

            if (!m_Overlay.IsVisible())
            {
                ImGui::SameLine();
                ImGui::TextColored(Colors::TextDisabled, "| [INS] Menu");
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
    }

    void OverlayUI::RenderNotifications()
    {
        ImGuiIO& io = ImGui::GetIO();

        if (m_Notifications.empty())
            return;

        float yOffset = 50.0f;
        float padding = 15.0f;
        float notificationWidth = 320.0f;

        for (size_t i = 0; i < m_Notifications.size() && i < MAX_NOTIFICATIONS; ++i)
        {
            const auto& notif = m_Notifications[i];

            // Calculate alpha for fade in/out
            float alpha = 1.0f;
            if (notif.timeRemaining < NOTIFICATION_FADE_TIME)
                alpha = notif.timeRemaining / NOTIFICATION_FADE_TIME;
            else if (notif.duration - notif.timeRemaining < NOTIFICATION_FADE_TIME)
                alpha = (notif.duration - notif.timeRemaining) / NOTIFICATION_FADE_TIME;

            // Slide in animation
            float slideOffset = 0;
            if (notif.duration - notif.timeRemaining < 0.3f)
                slideOffset = (0.3f - (notif.duration - notif.timeRemaining)) * 100.0f;

            ImVec2 pos(io.DisplaySize.x - notificationWidth - padding + slideOffset,
                       io.DisplaySize.y - yOffset - (i * 70.0f));

            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(ImVec2(notificationWidth, 0));
            ImGui::SetNextWindowBgAlpha(0.95f * alpha);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_AlwaysAutoResize;

            char windowId[32];
            snprintf(windowId, sizeof(windowId), "##Notif%zu", i);

            ImVec4 borderColor = GetNotificationColor(notif.type);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 12));
            ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Surface);

            if (ImGui::Begin(windowId, nullptr, flags))
            {
                const char* icon;
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

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
        }
    }

    void OverlayUI::RenderLoadingSpinner(const char* label, float radius)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        pos.x += radius + 5;
        pos.y += radius;

        float time = m_LoadingTime * 3.0f;
        int numSegments = 12;
        float startAngle = time;

        for (int i = 0; i < numSegments; i++)
        {
            float angle = startAngle + (i * 2.0f * (float)M_PI / numSegments);
            float alpha = 1.0f - (i / (float)numSegments);
            ImVec4 color = Colors::Primary;
            color.w = alpha;

            ImVec2 p(pos.x + std::cos(angle) * radius, pos.y + std::sin(angle) * radius);
            drawList->AddCircleFilled(p, 3.0f, ImGui::ColorConvertFloat4ToU32(color));
        }

        ImGui::Dummy(ImVec2(radius * 2 + 10, radius * 2 + 10));
    }

    void OverlayUI::RenderHealthBar(float health, float maxHealth, float width)
    {
        float fraction = maxHealth > 0 ? (health / maxHealth) : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);

        ImVec4 color = fraction > 0.6f ? Colors::Success :
                       fraction > 0.3f ? Colors::Warning : Colors::Danger;

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(fraction, ImVec2(width, 0));
        ImGui::PopStyleColor();
    }

    // ==================== Modal Dialogs ====================

    void OverlayUI::RenderPasswordModal()
    {
        if (!m_ShowPasswordModal)
            return;

        ImGui::OpenPopup("Enter Password##ServerPassword");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(300, 150));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::Background);

        if (ImGui::BeginPopupModal("Enter Password##ServerPassword", &m_ShowPasswordModal,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Server requires a password:");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ServerPwd", m_ServerPasswordBuffer.data(), m_ServerPasswordBuffer.size(),
                            ImGuiInputTextFlags_Password);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Join", ImVec2(120, 0)))
            {
                if (m_SelectedServerIndex >= 0 && m_OnJoinServer)
                {
                    m_OnJoinServer(m_Servers[m_SelectedServerIndex], m_ServerPasswordBuffer.data());
                    m_ServerPasswordBuffer[0] = '\0';
                }
                m_ShowPasswordModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_ServerPasswordBuffer[0] = '\0';
                m_ShowPasswordModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void OverlayUI::RenderCreateLobbyModal()
    {
        if (!m_ShowCreateLobbyModal)
            return;

        ImGui::OpenPopup("Create Lobby##CreateLobby");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(350, 280));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::Background);

        if (ImGui::BeginPopupModal("Create Lobby##CreateLobby", &m_ShowCreateLobbyModal,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Lobby Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##LobbyName", m_LobbyNameBuffer.data(), m_LobbyNameBuffer.size());

            ImGui::Spacing();

            ImGui::Text("Max Players:");
            ImGui::SetNextItemWidth(100);
            ImGui::SliderInt("##MaxPlayers", &m_LobbyMaxPlayers, 2, 8);

            ImGui::Spacing();

            ImGui::Checkbox("Private Lobby", &m_LobbyIsPrivate);

            if (m_LobbyIsPrivate)
            {
                ImGui::Text("Password:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##LobbyPwd", m_LobbyPasswordBuffer.data(), m_LobbyPasswordBuffer.size(),
                                ImGuiInputTextFlags_Password);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool canCreate = std::strlen(m_LobbyNameBuffer.data()) > 0;
            if (!canCreate)
                ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Success);
            if (ImGui::Button("Create", ImVec2(140, 0)))
            {
                if (m_OnCreateLobby)
                {
                    m_OnCreateLobby(m_LobbyNameBuffer.data(), m_LobbyMaxPlayers,
                                    m_LobbyIsPrivate, m_LobbyPasswordBuffer.data());
                }
                m_ShowCreateLobbyModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();

            if (!canCreate)
                ImGui::EndDisabled();

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(140, 0)))
            {
                m_ShowCreateLobbyModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void OverlayUI::RenderInviteFriendsModal()
    {
        if (!m_ShowInviteFriendsModal)
            return;

        ImGui::OpenPopup("Invite Friends##InviteFriends");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(300, 350));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::Background);

        if (ImGui::BeginPopupModal("Invite Friends##InviteFriends", &m_ShowInviteFriendsModal,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::Text("Select friends to invite:");
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Surface);
            if (ImGui::BeginChild("##InviteList", ImVec2(0, -40), true))
            {
                for (const auto& friendInfo : m_Friends)
                {
                    if (friendInfo.status == FriendStatus::Offline ||
                        friendInfo.isPendingRequest || friendInfo.isIncomingRequest)
                        continue;

                    ImGui::PushID(friendInfo.id.c_str());

                    ImVec4 statusColor = GetStatusColor(friendInfo.status);
                    ImGui::TextColored(statusColor, "%s", friendInfo.username.c_str());

                    ImGui::SameLine(200);

                    ImGui::PushStyleColor(ImGuiCol_Button, Colors::Primary);
                    if (ImGui::SmallButton("Invite"))
                    {
                        if (m_OnInviteFriend)
                            m_OnInviteFriend(friendInfo.id);
                        ShowSuccess("Invite sent to " + friendInfo.username);
                    }
                    ImGui::PopStyleColor();

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            if (ImGui::Button("Close", ImVec2(-1, 0)))
            {
                m_ShowInviteFriendsModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void OverlayUI::RenderConfirmModal()
    {
        if (!m_ShowConfirmModal)
            return;

        ImGui::OpenPopup("Confirm##ConfirmModal");

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(320, 150));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::Background);

        if (ImGui::BeginPopupModal("Confirm##ConfirmModal", &m_ShowConfirmModal,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::TextWrapped("%s", m_ConfirmModalMessage.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, Colors::Danger);
            if (ImGui::Button("Confirm", ImVec2(120, 0)))
            {
                if (m_ConfirmModalCallback)
                    m_ConfirmModalCallback();
                m_ShowConfirmModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                m_ShowConfirmModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ==================== Helper Functions ====================

    void OverlayUI::ShowNotification(const std::string& message, NotificationType type, float duration)
    {
        Notification notif;
        notif.message = message;
        notif.type = type;
        notif.duration = duration;
        notif.timeRemaining = duration;

        m_Notifications.push_front(notif);

        while (m_Notifications.size() > MAX_NOTIFICATIONS)
            m_Notifications.pop_back();
    }

    void OverlayUI::ShowError(const std::string& message, float duration)
    {
        ShowNotification(message, NotificationType::Error, duration);
    }

    void OverlayUI::ShowSuccess(const std::string& message, float duration)
    {
        ShowNotification(message, NotificationType::Success, duration);
    }

    void OverlayUI::SetServerList(const std::vector<ServerInfo>& servers)
    {
        m_Servers = servers;
        m_SelectedServerIndex = -1;
    }

    void OverlayUI::SetFriendsList(const std::vector<FriendInfo>& friends)
    {
        m_Friends = friends;
    }

    void OverlayUI::SetCurrentLobby(const LobbyInfo& lobby)
    {
        m_CurrentLobby = lobby;
        m_HasLobby = true;
        SetScreen(UIScreen::Lobby);
    }

    void OverlayUI::SetUserProfile(const UserProfile& profile)
    {
        m_UserProfile = profile;
    }

    void OverlayUI::ClearCurrentLobby()
    {
        m_HasLobby = false;
        m_CurrentLobby = LobbyInfo();
    }

    void OverlayUI::SetLoggedIn(bool loggedIn, const std::string& username)
    {
        m_IsLoggedIn = loggedIn;
        m_IsConnecting = false;
        m_CurrentUsername = username;

        if (loggedIn)
        {
            SetScreen(UIScreen::MainMenu);
            ShowSuccess("Welcome back, " + username + "!");
        }
    }

    void OverlayUI::SetConnecting(bool connecting)
    {
        m_IsConnecting = connecting;
    }

    void OverlayUI::SetLoginError(const std::string& error)
    {
        m_LoginError = error;
        m_IsConnecting = false;
        ShowError(error);
    }

    const char* OverlayUI::GetStatusText(FriendStatus status)
    {
        switch (status)
        {
        case FriendStatus::Online: return "Online";
        case FriendStatus::InGame: return "In Game";
        case FriendStatus::InLobby: return "In Lobby";
        case FriendStatus::Away: return "Away";
        default: return "Offline";
        }
    }

    ImVec4 OverlayUI::GetStatusColor(FriendStatus status)
    {
        switch (status)
        {
        case FriendStatus::Online: return Colors::Success;
        case FriendStatus::InGame: return Colors::Primary;
        case FriendStatus::InLobby: return Colors::Secondary;
        case FriendStatus::Away: return Colors::Warning;
        default: return Colors::TextDisabled;
        }
    }

    ImVec4 OverlayUI::GetNotificationColor(NotificationType type)
    {
        switch (type)
        {
        case NotificationType::Success: return Colors::Success;
        case NotificationType::Warning: return Colors::Warning;
        case NotificationType::Error: return Colors::Danger;
        default: return Colors::Primary;
        }
    }

    std::string OverlayUI::FormatTime(uint64_t timestamp)
    {
        time_t time = static_cast<time_t>(timestamp);
        struct tm* tm = localtime(&time);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%H:%M", tm);
        return std::string(buffer);
    }

    std::string OverlayUI::FormatPlayTime(int minutes)
    {
        if (minutes < 60)
            return std::to_string(minutes) + "m";

        int hours = minutes / 60;
        int mins = minutes % 60;

        if (hours < 24)
            return std::to_string(hours) + "h " + std::to_string(mins) + "m";

        int days = hours / 24;
        hours = hours % 24;
        return std::to_string(days) + "d " + std::to_string(hours) + "h";
    }
}

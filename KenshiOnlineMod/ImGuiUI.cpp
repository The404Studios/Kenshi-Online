#include "ImGuiUI.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

// Note: This implementation assumes ImGui library is linked separately
// You'll need to add ImGui source files or link against ImGui library

namespace KenshiOnline
{
    UIManager::UIManager()
        : m_ShowMainMenu(false) // Start with login screen
        , m_ShowServerBrowser(false)
        , m_ShowFriendsList(false)
        , m_ShowLobbyInvites(false)
        , m_ShowSettings(false)
        , m_ShowLogin(true) // Show login by default
        , m_ShowRegister(false)
        , m_IsLoggedIn(false)
        , m_ConnectedServer(nullptr)
        , m_SelectedServerIndex(-1)
        , m_SelectedFriendIndex(-1)
        , m_DirectConnectPort(5555)
        , m_Hwnd(nullptr)
        , m_Initialized(false)
    {
        memset(m_ServerFilterBuffer, 0, sizeof(m_ServerFilterBuffer));
        memset(m_FriendSearchBuffer, 0, sizeof(m_FriendSearchBuffer));
        memset(m_DirectConnectAddress, 0, sizeof(m_DirectConnectAddress));
        memset(m_LoginUsername, 0, sizeof(m_LoginUsername));
        memset(m_LoginPassword, 0, sizeof(m_LoginPassword));
        memset(m_RegisterUsername, 0, sizeof(m_RegisterUsername));
        memset(m_RegisterEmail, 0, sizeof(m_RegisterEmail));
        memset(m_RegisterPassword, 0, sizeof(m_RegisterPassword));
        memset(m_RegisterPasswordConfirm, 0, sizeof(m_RegisterPasswordConfirm));
        memset(m_StatusMessage, 0, sizeof(m_StatusMessage));
        strcpy_s(m_DirectConnectAddress, "127.0.0.1");
    }

    UIManager::~UIManager()
    {
        Shutdown();
    }

    bool UIManager::Initialize(HWND hwnd)
    {
        if (m_Initialized) return true;

        m_Hwnd = hwnd;

        // Initialize ImGui context
        // ImGui::CreateContext();
        // ImGuiIO& io = ImGui::GetIO();
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Initialize ImGui for Windows + DX11/DX12/OpenGL
        // ImGui_ImplWin32_Init(hwnd);
        // ImGui_ImplDX11_Init(device, deviceContext); // Or your graphics API

        m_Initialized = true;
        return true;
    }

    void UIManager::Shutdown()
    {
        if (!m_Initialized) return;

        // ImGui_ImplDX11_Shutdown();
        // ImGui_ImplWin32_Shutdown();
        // ImGui::DestroyContext();

        m_Initialized = false;
    }

    void UIManager::Render()
    {
        if (!m_Initialized) return;

        // Start ImGui frame
        // ImGui_ImplDX11_NewFrame();
        // ImGui_ImplWin32_NewFrame();
        // ImGui::NewFrame();

        // Show login/register if not logged in
        if (!m_IsLoggedIn)
        {
            if (m_ShowLogin) RenderLogin();
            if (m_ShowRegister) RenderRegister();
        }
        else
        {
            // Render main UI windows when logged in
            if (m_ShowMainMenu) RenderMainMenu();
            if (m_ShowServerBrowser) RenderServerBrowser();
            if (m_ShowFriendsList) RenderFriendsList();
            if (m_ShowLobbyInvites) RenderLobbyInvites();
            if (m_ShowSettings) RenderSettings();

            RenderConnectionStatus();
        }

        // End ImGui frame
        // ImGui::Render();
        // ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void UIManager::RenderMainMenu()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Kenshi Online - Main Menu", &m_ShowMainMenu))
        {
            ImGui::Text("Welcome to Kenshi Online!");
            ImGui::Separator();

            ImVec2 buttonSize(200, 40);
            if (ImGui::Button("Server Browser"))
            {
                m_ShowServerBrowser = true;
            }

            if (ImGui::Button("Friends List"))
            {
                m_ShowFriendsList = true;
            }

            if (ImGui::Button("Lobby Invites"))
            {
                m_ShowLobbyInvites = true;
            }

            if (ImGui::Button("Settings"))
            {
                m_ShowSettings = true;
            }

            ImGui::Separator();

            // Direct connect
            ImGui::Text("Direct Connect:");
            ImGui::InputText("Address", m_DirectConnectAddress, sizeof(m_DirectConnectAddress));

            char portBuffer[16];
            sprintf_s(portBuffer, sizeof(portBuffer), "%d", m_DirectConnectPort);
            if (ImGui::InputText("Port", portBuffer, sizeof(portBuffer)))
            {
                m_DirectConnectPort = atoi(portBuffer);
            }

            if (ImGui::Button("Connect"))
            {
                ServerInfo directServer;
                directServer.name = "Direct Connect";
                directServer.address = m_DirectConnectAddress;
                directServer.port = m_DirectConnectPort;

                if (m_OnJoinServer)
                {
                    m_OnJoinServer(directServer);
                }
            }
        }
        ImGui::End();
    }

    void UIManager::RenderServerBrowser()
    {
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Server Browser", &m_ShowServerBrowser))
        {
            // Filter and refresh
            ImGui::InputText("Filter", m_ServerFilterBuffer, sizeof(m_ServerFilterBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Refresh"))
            {
                if (m_OnRefreshServers)
                {
                    m_OnRefreshServers();
                }
            }

            ImGui::Separator();

            // Server list using columns (compatible with older ImGui)
            ImGui::Columns(7, "ServerColumns", true);
            ImGui::Text("Name"); ImGui::NextColumn();
            ImGui::Text("Players"); ImGui::NextColumn();
            ImGui::Text("Map"); ImGui::NextColumn();
            ImGui::Text("Mode"); ImGui::NextColumn();
            ImGui::Text("Ping"); ImGui::NextColumn();
            ImGui::Text("Version"); ImGui::NextColumn();
            ImGui::Text("Lock"); ImGui::NextColumn();
            ImGui::Separator();

            // Filter servers
            std::string filterStr(m_ServerFilterBuffer);
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            for (int i = 0; i < m_Servers.size(); i++)
            {
                const auto& server = m_Servers[i];

                // Apply filter
                if (!filterStr.empty())
                {
                    std::string serverName = server.name;
                    std::transform(serverName.begin(), serverName.end(), serverName.begin(), ::tolower);
                    if (serverName.find(filterStr) == std::string::npos)
                    {
                        continue;
                    }
                }

                bool selected = (m_SelectedServerIndex == i);

                if (ImGui::Selectable(server.name.c_str(), selected))
                {
                    m_SelectedServerIndex = i;
                }
                ImGui::NextColumn();

                char playerText[32];
                sprintf_s(playerText, sizeof(playerText), "%d/%d", server.playerCount, server.maxPlayers);
                ImGui::Text(playerText);
                ImGui::NextColumn();

                ImGui::Text(server.mapName.c_str());
                ImGui::NextColumn();

                ImGui::Text(server.gameMode.c_str());
                ImGui::NextColumn();

                ImGui::Text("%d ms", server.ping);
                ImGui::NextColumn();

                ImGui::Text(server.version.c_str());
                ImGui::NextColumn();

                ImGui::Text(server.passwordProtected ? "Yes" : "No");
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::Separator();

            // Join button
            if (m_SelectedServerIndex >= 0 && m_SelectedServerIndex < m_Servers.size())
            {
                if (ImGui::Button("Join Server"))
                {
                    if (m_OnJoinServer)
                    {
                        m_OnJoinServer(m_Servers[m_SelectedServerIndex]);
                    }
                }

                ImGui::SameLine();
                ImGui::Text("Selected: %s", m_Servers[m_SelectedServerIndex].name.c_str());
            }
        }
        ImGui::End();
    }

    void UIManager::RenderFriendsList()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Friends List", &m_ShowFriendsList))
        {
            ImGui::InputText("Search", m_FriendSearchBuffer, sizeof(m_FriendSearchBuffer));
            ImGui::Separator();

            // Friends list (use simple child window without size for compatibility)
            ImGui::BeginChild("FriendsListChild");
            {
                std::string searchStr(m_FriendSearchBuffer);
                std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

                for (int i = 0; i < m_Friends.size(); i++)
                {
                    const auto& friendData = m_Friends[i];

                    // Apply search filter
                    if (!searchStr.empty())
                    {
                        std::string username = friendData.username;
                        std::transform(username.begin(), username.end(), username.begin(), ::tolower);
                        if (username.find(searchStr) == std::string::npos)
                        {
                            continue;
                        }
                    }

                    bool selected = (m_SelectedFriendIndex == i);

                    // Friend entry
                    std::string statusIndicator = friendData.isOnline ? "[Online]" : "[Offline]";
                    std::string friendLabel = statusIndicator + " " + friendData.displayName + " (Lvl " + std::to_string(friendData.level) + ")";

                    if (ImGui::Selectable(friendLabel.c_str(), selected))
                    {
                        m_SelectedFriendIndex = i;
                    }

                    if (friendData.isOnline && !friendData.currentServer.empty())
                    {
                        ImGui::SameLine();
                        ImGui::Text("- %s", friendData.currentServer.c_str());
                    }
                    else if (!friendData.isOnline)
                    {
                        ImGui::SameLine();
                        ImGui::Text("- Last seen: %s", friendData.lastSeen.c_str());
                    }
                }
            }
            ImGui::EndChild();

            ImGui::Separator();

            // Friend actions
            if (m_SelectedFriendIndex >= 0 && m_SelectedFriendIndex < m_Friends.size())
            {
                const auto& selectedFriend = m_Friends[m_SelectedFriendIndex];

                if (selectedFriend.isOnline)
                {
                    if (ImGui::Button("Invite to Lobby"))
                    {
                        if (m_OnInviteFriend)
                        {
                            m_OnInviteFriend(selectedFriend);
                        }
                    }

                    ImGui::SameLine();

                    if (!selectedFriend.currentServer.empty())
                    {
                        if (ImGui::Button("Join Server"))
                        {
                            // TODO: Get server info and join
                        }
                    }
                }
                else
                {
                    ImGui::Text("Friend is offline");
                }
            }
        }
        ImGui::End();
    }

    void UIManager::RenderLobbyInvites()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Lobby Invites", &m_ShowLobbyInvites))
        {
            if (m_LobbyInvites.empty())
            {
                ImGui::Text("No pending invites");
            }
            else
            {
                for (const auto& invite : m_LobbyInvites)
                {
                    ImGui::Text("From: %s", invite.fromDisplayName.c_str());
                    ImGui::Text("Lobby: %s (%d players)", invite.lobbyName.c_str(), invite.playerCount);
                    ImGui::Text("Time: %s", invite.timestamp.c_str());

                    if (ImGui::Button(("Accept##" + invite.inviteId).c_str()))
                    {
                        if (m_OnAcceptInvite)
                        {
                            m_OnAcceptInvite(invite);
                        }
                    }

                    ImGui::SameLine();

                    if (ImGui::Button(("Decline##" + invite.inviteId).c_str()))
                    {
                        // Remove invite
                        // TODO: Implement decline
                    }

                    ImGui::Separator();
                }
            }
        }
        ImGui::End();
    }

    void UIManager::RenderSettings()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Settings", &m_ShowSettings))
        {
            ImGui::Text("Network Settings");
            ImGui::Separator();

            // TODO: Add settings controls
            ImGui::Text("Coming soon...");
        }
        ImGui::End();
    }

    void UIManager::RenderConnectionStatus()
    {
        // Small status window in corner
        ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Connection Status", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar))
        {
            if (m_ConnectedServer != nullptr)
            {
                ImGui::Text("Connected to: %s", m_ConnectedServer->name.c_str());
                ImGui::Text("Players: %d/%d", m_ConnectedServer->playerCount, m_ConnectedServer->maxPlayers);
                ImGui::Text("Ping: %d ms", m_ConnectedServer->ping);

                if (ImGui::Button("Disconnect"))
                {
                    if (m_OnDisconnect)
                    {
                        m_OnDisconnect();
                    }
                }
            }
            else
            {
                ImGui::Text("Not connected");
            }
        }
        ImGui::End();
    }

    // Setters
    void UIManager::ShowMainMenu(bool show) { m_ShowMainMenu = show; }
    void UIManager::ShowServerBrowser(bool show) { m_ShowServerBrowser = show; }
    void UIManager::ShowFriendsList(bool show) { m_ShowFriendsList = show; }
    void UIManager::ShowLobbyInvites(bool show) { m_ShowLobbyInvites = show; }
    void UIManager::ShowSettings(bool show) { m_ShowSettings = show; }
    void UIManager::ShowLogin(bool show) { m_ShowLogin = show; m_ShowRegister = !show; }
    void UIManager::ShowRegister(bool show) { m_ShowRegister = show; m_ShowLogin = !show; }

    void UIManager::SetLoggedIn(bool loggedIn, const std::string& username)
    {
        m_IsLoggedIn = loggedIn;
        m_Username = username;

        if (loggedIn)
        {
            // Switch to main menu after login
            m_ShowLogin = false;
            m_ShowRegister = false;
            m_ShowMainMenu = true;
            strcpy_s(m_StatusMessage, "Login successful!");
        }
        else
        {
            // Clear sensitive data on logout
            memset(m_LoginPassword, 0, sizeof(m_LoginPassword));
            memset(m_RegisterPassword, 0, sizeof(m_RegisterPassword));
            memset(m_RegisterPasswordConfirm, 0, sizeof(m_RegisterPasswordConfirm));
            m_ShowMainMenu = false;
            m_ShowLogin = true;
        }
    }

    void UIManager::SetServers(const std::vector<ServerInfo>& servers)
    {
        m_Servers = servers;
    }

    void UIManager::SetFriends(const std::vector<Friend>& friends)
    {
        m_Friends = friends;
    }

    void UIManager::SetLobbyInvites(const std::vector<LobbyInvite>& invites)
    {
        m_LobbyInvites = invites;
    }

    void UIManager::SetConnectedServer(const ServerInfo* server)
    {
        m_ConnectedServer = server;
    }

    // Callbacks
    void UIManager::SetOnJoinServer(std::function<void(const ServerInfo&)> callback)
    {
        m_OnJoinServer = callback;
    }

    void UIManager::SetOnInviteFriend(std::function<void(const Friend&)> callback)
    {
        m_OnInviteFriend = callback;
    }

    void UIManager::SetOnAcceptInvite(std::function<void(const LobbyInvite&)> callback)
    {
        m_OnAcceptInvite = callback;
    }

    void UIManager::SetOnRefreshServers(std::function<void()> callback)
    {
        m_OnRefreshServers = callback;
    }

    void UIManager::SetOnDisconnect(std::function<void()> callback)
    {
        m_OnDisconnect = callback;
    }

    void UIManager::SetOnLogin(std::function<void(const std::string&, const std::string&)> callback)
    {
        m_OnLogin = callback;
    }

    void UIManager::SetOnRegister(std::function<void(const std::string&, const std::string&, const std::string&)> callback)
    {
        m_OnRegister = callback;
    }

    bool UIManager::WantsMouseCapture()
    {
        // return ImGui::GetIO().WantCaptureMouse;
        return false;
    }

    bool UIManager::WantsKeyboardCapture()
    {
        // return ImGui::GetIO().WantCaptureKeyboard;
        return false;
    }

    // Login Screen
    void UIManager::RenderLogin()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(500, 300), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Kenshi Online - Login", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Welcome to Kenshi Online!");
            ImGui::Separator();
            ImGui::Spacing();

            // Username field
            ImGui::Text("Username:");
            ImGui::InputText("##username", m_LoginUsername, sizeof(m_LoginUsername));
            ImGui::Spacing();

            // Password field
            ImGui::Text("Password:");
            ImGui::InputText("##password", m_LoginPassword, sizeof(m_LoginPassword));
            ImGui::Spacing();
            ImGui::Spacing();

            // Login button
            if (ImGui::Button("Login"))
            {
                if (strlen(m_LoginUsername) > 0 && strlen(m_LoginPassword) > 0)
                {
                    if (m_OnLogin)
                    {
                        m_OnLogin(std::string(m_LoginUsername), std::string(m_LoginPassword));
                    }
                }
                else
                {
                    strcpy_s(m_StatusMessage, "Please enter username and password");
                }
            }

            ImGui::SameLine();

            // Register button
            if (ImGui::Button("Register"))
            {
                ShowRegister(true);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Status message
            if (strlen(m_StatusMessage) > 0)
            {
                ImGui::Text("%s", m_StatusMessage);
            }
        }
        ImGui::End();
    }

    // Register Screen
    void UIManager::RenderRegister()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(500, 250), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Kenshi Online - Register", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Create Your Account");
            ImGui::Separator();
            ImGui::Spacing();

            // Username field
            ImGui::Text("Username:");
            ImGui::InputText("##reg_username", m_RegisterUsername, sizeof(m_RegisterUsername));
            ImGui::Spacing();

            // Email field
            ImGui::Text("Email:");
            ImGui::InputText("##reg_email", m_RegisterEmail, sizeof(m_RegisterEmail));
            ImGui::Spacing();

            // Password field
            ImGui::Text("Password:");
            ImGui::InputText("##reg_password", m_RegisterPassword, sizeof(m_RegisterPassword));
            ImGui::Spacing();

            // Confirm password field
            ImGui::Text("Confirm Password:");
            ImGui::InputText("##reg_password_confirm", m_RegisterPasswordConfirm, sizeof(m_RegisterPasswordConfirm));
            ImGui::Spacing();
            ImGui::Spacing();

            // Register button
            if (ImGui::Button("Create Account"))
            {
                if (strlen(m_RegisterUsername) > 0 && strlen(m_RegisterEmail) > 0 &&
                    strlen(m_RegisterPassword) > 0 && strlen(m_RegisterPasswordConfirm) > 0)
                {
                    if (strcmp(m_RegisterPassword, m_RegisterPasswordConfirm) == 0)
                    {
                        if (m_OnRegister)
                        {
                            m_OnRegister(
                                std::string(m_RegisterUsername),
                                std::string(m_RegisterEmail),
                                std::string(m_RegisterPassword)
                            );
                        }
                    }
                    else
                    {
                        strcpy_s(m_StatusMessage, "Passwords do not match!");
                    }
                }
                else
                {
                    strcpy_s(m_StatusMessage, "Please fill in all fields");
                }
            }

            ImGui::SameLine();

            // Back to login button
            if (ImGui::Button("Back to Login"))
            {
                ShowLogin(true);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Status message
            if (strlen(m_StatusMessage) > 0)
            {
                ImGui::Text("%s", m_StatusMessage);
            }
        }
        ImGui::End();
    }
}

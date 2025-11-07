#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

// Include actual ImGui implementation
#include "imgui/imgui.h"

namespace KenshiOnline
{
    // Server information structure
    struct ServerInfo
    {
        std::string name;
        std::string address;
        int port;
        int playerCount;
        int maxPlayers;
        std::string mapName;
        int ping;
        bool passwordProtected;
        std::string version;
        std::string gameMode;
    };

    // Friend structure
    struct Friend
    {
        std::string id;
        std::string username;
        std::string displayName;
        bool isOnline;
        std::string currentServer;
        std::string lastSeen;
        int level;
    };

    // Lobby invitation structure
    struct LobbyInvite
    {
        std::string inviteId;
        std::string fromUsername;
        std::string fromDisplayName;
        std::string lobbyId;
        std::string lobbyName;
        int playerCount;
        std::string timestamp;
    };

    /// <summary>
    /// Main UI Manager for Kenshi Online
    /// Handles all ImGui rendering and UI state
    /// </summary>
    class UIManager
    {
    public:
        UIManager();
        ~UIManager();

        // Initialize/Shutdown
        bool Initialize(HWND hwnd);
        void Shutdown();

        // Main render function (call every frame)
        void Render();

        // UI State
        void ShowMainMenu(bool show = true);
        void ShowServerBrowser(bool show = true);
        void ShowFriendsList(bool show = true);
        void ShowLobbyInvites(bool show = true);
        void ShowSettings(bool show = true);

        // Data management
        void SetServers(const std::vector<ServerInfo>& servers);
        void SetFriends(const std::vector<Friend>& friends);
        void SetLobbyInvites(const std::vector<LobbyInvite>& invites);
        void SetConnectedServer(const ServerInfo* server);

        // Callbacks
        void SetOnJoinServer(std::function<void(const ServerInfo&)> callback);
        void SetOnInviteFriend(std::function<void(const Friend&)> callback);
        void SetOnAcceptInvite(std::function<void(const LobbyInvite&)> callback);
        void SetOnRefreshServers(std::function<void()> callback);
        void SetOnDisconnect(std::function<void()> callback);

        // Input
        bool WantsMouseCapture();
        bool WantsKeyboardCapture();

    private:
        // Render individual UI windows
        void RenderMainMenu();
        void RenderServerBrowser();
        void RenderFriendsList();
        void RenderLobbyInvites();
        void RenderSettings();
        void RenderConnectionStatus();

        // UI State
        bool m_ShowMainMenu;
        bool m_ShowServerBrowser;
        bool m_ShowFriendsList;
        bool m_ShowLobbyInvites;
        bool m_ShowSettings;

        // Data
        std::vector<ServerInfo> m_Servers;
        std::vector<Friend> m_Friends;
        std::vector<LobbyInvite> m_LobbyInvites;
        const ServerInfo* m_ConnectedServer;

        // Selected items
        int m_SelectedServerIndex;
        int m_SelectedFriendIndex;

        // Input buffers
        char m_ServerFilterBuffer[256];
        char m_FriendSearchBuffer[256];
        char m_DirectConnectAddress[256];
        int m_DirectConnectPort;

        // Callbacks
        std::function<void(const ServerInfo&)> m_OnJoinServer;
        std::function<void(const Friend&)> m_OnInviteFriend;
        std::function<void(const LobbyInvite&)> m_OnAcceptInvite;
        std::function<void()> m_OnRefreshServers;
        std::function<void()> m_OnDisconnect;

        // Initialization
        HWND m_Hwnd;
        bool m_Initialized;
    };
}

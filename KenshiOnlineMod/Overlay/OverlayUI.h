/*
 * OverlayUI.h - Complete UI System for Kenshi Online
 * Full-featured overlay with login, server browser, friends, lobbies
 */

#pragma once

#include <string>
#include <array>
#include <deque>
#include <vector>
#include <functional>
#include <chrono>
#include <imgui.h>

namespace KenshiOnline
{
    class Overlay;

    // ==================== Enums ====================

    enum class NotificationType
    {
        Info,
        Success,
        Warning,
        Error
    };

    enum class UIScreen
    {
        Login,
        Register,
        MainMenu,
        ServerBrowser,
        Friends,
        Lobby,
        Settings,
        InGame
    };

    enum class FriendStatus
    {
        Offline,
        Online,
        InGame,
        InLobby,
        Away
    };

    enum class LobbyState
    {
        Waiting,
        Starting,
        InGame
    };

    // ==================== Data Structures ====================

    struct Notification
    {
        std::string message;
        NotificationType type;
        float duration;
        float timeRemaining;
    };

    struct ServerInfo
    {
        std::string id;
        std::string name;
        std::string address;
        int port;
        int playerCount;
        int maxPlayers;
        int ping;
        bool isOfficial;
        bool hasPassword;
        std::string gameMode;
        std::string region;
        std::string version;
    };

    struct FriendInfo
    {
        std::string id;
        std::string username;
        FriendStatus status;
        std::string currentServer;
        std::string currentLobby;
        bool isPendingRequest;
        bool isIncomingRequest;
        uint64_t lastOnline;
    };

    struct LobbyPlayer
    {
        std::string id;
        std::string username;
        bool isReady;
        bool isHost;
        int characterLevel;
        std::string selectedCharacter;
    };

    struct LobbyInfo
    {
        std::string id;
        std::string name;
        std::string hostName;
        int playerCount;
        int maxPlayers;
        bool isPrivate;
        LobbyState state;
        std::string gameMode;
        std::vector<LobbyPlayer> players;
    };

    struct UserProfile
    {
        std::string id;
        std::string username;
        std::string email;
        int level;
        int totalPlayTime;
        int gamesPlayed;
        std::string avatarUrl;
    };

    // ==================== Animation Helpers ====================

    struct AnimatedFloat
    {
        float current = 0.0f;
        float target = 0.0f;
        float speed = 5.0f;

        void Update(float deltaTime)
        {
            float diff = target - current;
            current += diff * std::min(1.0f, speed * deltaTime);
        }

        void SetImmediate(float value)
        {
            current = target = value;
        }
    };

    struct AnimatedVec2
    {
        ImVec2 current = ImVec2(0, 0);
        ImVec2 target = ImVec2(0, 0);
        float speed = 5.0f;

        void Update(float deltaTime)
        {
            float factor = std::min(1.0f, speed * deltaTime);
            current.x += (target.x - current.x) * factor;
            current.y += (target.y - current.y) * factor;
        }
    };

    // ==================== Main UI Class ====================

    class OverlayUI
    {
    public:
        explicit OverlayUI(Overlay& overlay);
        ~OverlayUI() = default;

        void Render();
        void Update(float deltaTime);

        // Screen navigation
        void SetScreen(UIScreen screen);
        UIScreen GetCurrentScreen() const { return m_CurrentScreen; }

        // Notification methods
        void ShowNotification(const std::string& message, NotificationType type = NotificationType::Info, float duration = 3.0f);
        void ShowError(const std::string& message, float duration = 5.0f);
        void ShowSuccess(const std::string& message, float duration = 3.0f);

        // Data setters (called from network callbacks)
        void SetServerList(const std::vector<ServerInfo>& servers);
        void SetFriendsList(const std::vector<FriendInfo>& friends);
        void SetCurrentLobby(const LobbyInfo& lobby);
        void SetUserProfile(const UserProfile& profile);
        void ClearCurrentLobby();

        // Callbacks for network actions
        using LoginCallback = std::function<void(const std::string& username, const std::string& password)>;
        using RegisterCallback = std::function<void(const std::string& username, const std::string& password, const std::string& email)>;
        using RefreshServersCallback = std::function<void()>;
        using JoinServerCallback = std::function<void(const ServerInfo& server, const std::string& password)>;
        using CreateLobbyCallback = std::function<void(const std::string& name, int maxPlayers, bool isPrivate, const std::string& password)>;
        using JoinLobbyCallback = std::function<void(const std::string& lobbyId, const std::string& password)>;
        using LeaveLobbyCallback = std::function<void()>;
        using ReadyUpCallback = std::function<void(bool ready)>;
        using StartGameCallback = std::function<void()>;
        using AddFriendCallback = std::function<void(const std::string& username)>;
        using RemoveFriendCallback = std::function<void(const std::string& friendId)>;
        using InviteFriendCallback = std::function<void(const std::string& friendId)>;
        using AcceptFriendCallback = std::function<void(const std::string& friendId)>;
        using LogoutCallback = std::function<void()>;

        void SetLoginCallback(LoginCallback cb) { m_OnLogin = cb; }
        void SetRegisterCallback(RegisterCallback cb) { m_OnRegister = cb; }
        void SetRefreshServersCallback(RefreshServersCallback cb) { m_OnRefreshServers = cb; }
        void SetJoinServerCallback(JoinServerCallback cb) { m_OnJoinServer = cb; }
        void SetCreateLobbyCallback(CreateLobbyCallback cb) { m_OnCreateLobby = cb; }
        void SetJoinLobbyCallback(JoinLobbyCallback cb) { m_OnJoinLobby = cb; }
        void SetLeaveLobbyCallback(LeaveLobbyCallback cb) { m_OnLeaveLobby = cb; }
        void SetReadyUpCallback(ReadyUpCallback cb) { m_OnReadyUp = cb; }
        void SetStartGameCallback(StartGameCallback cb) { m_OnStartGame = cb; }
        void SetAddFriendCallback(AddFriendCallback cb) { m_OnAddFriend = cb; }
        void SetRemoveFriendCallback(RemoveFriendCallback cb) { m_OnRemoveFriend = cb; }
        void SetInviteFriendCallback(InviteFriendCallback cb) { m_OnInviteFriend = cb; }
        void SetAcceptFriendCallback(AcceptFriendCallback cb) { m_OnAcceptFriend = cb; }
        void SetLogoutCallback(LogoutCallback cb) { m_OnLogout = cb; }

        // State setters
        void SetLoggedIn(bool loggedIn, const std::string& username = "");
        void SetConnecting(bool connecting);
        void SetLoginError(const std::string& error);

    private:
        // Screen rendering functions
        void RenderLoginScreen();
        void RenderRegisterScreen();
        void RenderMainMenu();
        void RenderServerBrowser();
        void RenderFriendsPanel();
        void RenderLobbyScreen();
        void RenderSettingsScreen();
        void RenderInGameOverlay();

        // UI Components
        void RenderStatusBar();
        void RenderNotifications();
        void RenderLoadingSpinner(const char* label, float radius = 20.0f);
        void RenderServerCard(const ServerInfo& server, int index);
        void RenderFriendCard(const FriendInfo& friendInfo, int index);
        void RenderLobbyPlayerSlot(const LobbyPlayer* player, int slot, bool isLocalPlayer);
        void RenderHealthBar(float health, float maxHealth, float width);
        void RenderProgressBar(float progress, const ImVec4& color, float width, float height);

        // Modal dialogs
        void RenderPasswordModal();
        void RenderCreateLobbyModal();
        void RenderInviteFriendsModal();
        void RenderConfirmModal();

        // Helper functions
        const char* GetStatusText(FriendStatus status);
        ImVec4 GetStatusColor(FriendStatus status);
        ImVec4 GetNotificationColor(NotificationType type);
        std::string FormatTime(uint64_t timestamp);
        std::string FormatPlayTime(int minutes);

        // Style helpers
        void PushButtonStyle(const ImVec4& color);
        void PopButtonStyle();
        void RenderGradientBackground(const ImVec2& pos, const ImVec2& size, const ImVec4& colorTop, const ImVec4& colorBottom);

        Overlay& m_Overlay;

        // Current state
        UIScreen m_CurrentScreen = UIScreen::Login;
        UIScreen m_PreviousScreen = UIScreen::Login;
        bool m_IsLoggedIn = false;
        bool m_IsConnecting = false;
        std::string m_CurrentUsername;
        std::string m_LoginError;

        // Animation state
        AnimatedFloat m_ScreenTransition;
        AnimatedFloat m_WindowAlpha;
        AnimatedVec2 m_WindowPosition;
        float m_TransitionDirection = 1.0f;  // 1.0 = forward, -1.0 = back

        // Data
        std::vector<ServerInfo> m_Servers;
        std::vector<FriendInfo> m_Friends;
        LobbyInfo m_CurrentLobby;
        UserProfile m_UserProfile;
        bool m_HasLobby = false;

        // Input buffers
        std::array<char, 64> m_UsernameBuffer = {};
        std::array<char, 64> m_PasswordBuffer = {};
        std::array<char, 64> m_ConfirmPasswordBuffer = {};
        std::array<char, 128> m_EmailBuffer = {};
        std::array<char, 64> m_ServerPasswordBuffer = {};
        std::array<char, 64> m_LobbyNameBuffer = {};
        std::array<char, 64> m_LobbyPasswordBuffer = {};
        std::array<char, 64> m_FriendSearchBuffer = {};
        std::array<char, 256> m_ChatInputBuffer = {};

        // UI state
        int m_SelectedServerIndex = -1;
        int m_SelectedFriendIndex = -1;
        int m_LobbyMaxPlayers = 4;
        bool m_LobbyIsPrivate = false;
        bool m_IsReady = false;
        bool m_RememberMe = false;

        // Modal state
        bool m_ShowPasswordModal = false;
        bool m_ShowCreateLobbyModal = false;
        bool m_ShowInviteFriendsModal = false;
        bool m_ShowConfirmModal = false;
        std::string m_ConfirmModalTitle;
        std::string m_ConfirmModalMessage;
        std::function<void()> m_ConfirmModalCallback;

        // Server browser filters
        std::array<char, 64> m_ServerSearchBuffer = {};
        bool m_FilterShowFull = true;
        bool m_FilterShowEmpty = true;
        bool m_FilterShowPassworded = true;
        int m_FilterRegion = 0;  // 0 = All
        int m_SortColumn = 0;
        bool m_SortAscending = true;

        // Notifications
        std::deque<Notification> m_Notifications;
        static constexpr size_t MAX_NOTIFICATIONS = 5;
        static constexpr float NOTIFICATION_FADE_TIME = 0.3f;

        // Settings
        float m_OverlayOpacity = 0.96f;
        bool m_ShowFPS = true;
        bool m_ShowPing = true;
        bool m_AlwaysShowStatusBar = true;
        bool m_EnableSounds = true;
        bool m_EnableAnimations = true;
        int m_UIScale = 100;

        // Timing
        float m_LoadingTime = 0.0f;
        float m_RefreshCooldown = 0.0f;
        static constexpr float REFRESH_COOLDOWN_TIME = 3.0f;

        // Callbacks
        LoginCallback m_OnLogin;
        RegisterCallback m_OnRegister;
        RefreshServersCallback m_OnRefreshServers;
        JoinServerCallback m_OnJoinServer;
        CreateLobbyCallback m_OnCreateLobby;
        JoinLobbyCallback m_OnJoinLobby;
        LeaveLobbyCallback m_OnLeaveLobby;
        ReadyUpCallback m_OnReadyUp;
        StartGameCallback m_OnStartGame;
        AddFriendCallback m_OnAddFriend;
        RemoveFriendCallback m_OnRemoveFriend;
        InviteFriendCallback m_OnInviteFriend;
        AcceptFriendCallback m_OnAcceptFriend;
        LogoutCallback m_OnLogout;
    };
}

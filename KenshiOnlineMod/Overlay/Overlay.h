/*
 * Overlay.h - Main Overlay Manager for Kenshi Online
 * Manages ImGui context, network callbacks, and coordinates rendering
 */

#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <memory>

namespace KenshiOnline
{
    // Forward declarations
    class OverlayUI;

    // Player info for display
    struct PlayerInfo
    {
        std::string id;
        std::string name;
        float health;
        float maxHealth;
        float x, y, z;
        bool isOnline;
        int factionId;
        std::string status;
    };

    // Chat message
    struct ChatMessage
    {
        std::string sender;
        std::string message;
        uint64_t timestamp;
        int type;  // 0 = global, 1 = team, 2 = system
    };

    // Connection status
    enum class ConnectionStatus
    {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    class Overlay
    {
    public:
        static Overlay& Get();

        bool Initialize();
        void Shutdown();
        void Render();

        // State management
        bool IsVisible() const { return m_Visible; }
        void SetVisible(bool visible) { m_Visible = visible; }
        void Toggle() { m_Visible = !m_Visible; }

        // Connection status
        ConnectionStatus GetConnectionStatus() const { return m_ConnectionStatus; }
        void SetConnectionStatus(ConnectionStatus status);
        void SetServerAddress(const std::string& address) { m_ServerAddress = address; }
        std::string GetServerAddress() const { return m_ServerAddress; }

        // Player management
        void UpdateLocalPlayer(const PlayerInfo& info);
        void UpdatePlayerList(const std::vector<PlayerInfo>& players);
        void AddPlayer(const PlayerInfo& player);
        void RemovePlayer(const std::string& name);
        std::vector<PlayerInfo> GetPlayers() const;
        int GetPlayerCount() const;

        // Chat
        void AddChatMessage(const ChatMessage& msg);
        void AddSystemMessage(const std::string& message);
        std::vector<ChatMessage> GetChatMessages() const;
        void ClearChat();

        // Callbacks for network events (called from mod)
        using ConnectCallback = std::function<void(const std::string& address, int port, const std::string& username, const std::string& password)>;
        using ChatCallback = std::function<void(const std::string& message)>;
        using DisconnectCallback = std::function<void()>;

        void SetConnectCallback(ConnectCallback cb) { m_ConnectCallback = cb; }
        void SetChatCallback(ChatCallback cb) { m_ChatCallback = cb; }
        void SetDisconnectCallback(DisconnectCallback cb) { m_DisconnectCallback = cb; }

        // Called by UI to trigger network actions
        void OnConnect(const std::string& address, int port, const std::string& username, const std::string& password);
        void OnSendChat(const std::string& message);
        void OnDisconnect();

        // Stats
        void SetPing(int ping) { m_Ping = ping; }
        int GetPing() const { return m_Ping; }
        void SetFPS(int fps) { m_FPS = fps; }
        int GetFPS() const { return m_FPS; }

        // Error display
        void ShowError(const std::string& error);
        void ShowNotification(const std::string& message);
        void ShowSuccess(const std::string& message);

        // Get UI for advanced access
        OverlayUI* GetUI() const { return m_UI.get(); }

    private:
        Overlay();
        ~Overlay();
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        bool InitializeImGui();
        void ShutdownImGui();

        std::unique_ptr<OverlayUI> m_UI;
        bool m_Initialized = false;
        bool m_Visible = true;  // Start visible for login screen

        // State
        ConnectionStatus m_ConnectionStatus = ConnectionStatus::Disconnected;
        std::string m_ServerAddress = "127.0.0.1";
        int m_Port = 5555;
        int m_Ping = 0;
        int m_FPS = 0;

        // Player data
        PlayerInfo m_LocalPlayer;
        std::vector<PlayerInfo> m_Players;
        mutable std::mutex m_PlayerMutex;

        // Chat
        std::vector<ChatMessage> m_ChatMessages;
        mutable std::mutex m_ChatMutex;
        static constexpr size_t MAX_CHAT_MESSAGES = 100;

        // Callbacks
        ConnectCallback m_ConnectCallback;
        ChatCallback m_ChatCallback;
        DisconnectCallback m_DisconnectCallback;
    };
}

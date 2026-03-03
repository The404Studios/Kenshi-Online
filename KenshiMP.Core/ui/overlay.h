#pragma once
#include "native_menu.h"
#include "../net/server_query.h"
#include "kmp/types.h"
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <chrono>

namespace kmp {

class Overlay {
public:
    void Update(); // Called every frame from Present hook — handles auto-connect, connection state
    void Shutdown();

    // Chat
    void AddChatMessage(PlayerID sender, const std::string& message);
    void AddSystemMessage(const std::string& message);

    // Player list
    void AddPlayer(const PlayerInfo& player);
    void RemovePlayer(PlayerID id);
    void UpdatePlayerPing(PlayerID id, uint32_t ping);

    // Input capture state
    bool IsInputCapture() const {
        return m_nativeMenu.IsVisible();
    }

    void CloseAll() {
        m_nativeMenu.Hide();
    }

    // NativeMenu access (used by NativeMenu button handlers)
    NativeMenu& GetNativeMenu() { return m_nativeMenu; }

    // ServerQueryClient access (used by NativeMenu server browser)
    ServerQueryClient& GetQueryClient() { return m_queryClient; }

    // Helpers for NativeMenu to drive overlay connection state
    void SetHostingServer(bool hosting) { m_hostingServer = hosting; }
    void SetConnecting(bool connecting) { m_connecting = connecting; }
    void SetAutoConnect(const std::string& ip, uint16_t port);
    void SetConnectionInfo(const std::string& ip, uint16_t port, const std::string& name);
    void SetPlayerName(const std::string& name);

    // Reset overlay state (called on disconnect for clean reconnect)
    void ResetForReconnect();

private:
    // Chat
    struct ChatEntry {
        PlayerID    sender;
        std::string senderName;
        std::string message;
        float       timestamp;
        bool        isSystem;
    };
    std::deque<ChatEntry> m_chatHistory;
    bool m_chatScrollToBottom = false;

    // Player list
    std::vector<PlayerInfo> m_players;

    // Connection
    char m_serverAddress[128] = "127.0.0.1";
    char m_serverPort[8] = "27800";
    char m_playerName[32] = "Player";
    bool m_connecting = false;

    // Config loaded flag
    bool m_firstFrame = true;
    char m_settingsName[32] = "Player";
    bool m_settingsAutoConnect = true;

    // Auto-connect on game load
    bool m_autoConnectPending = false;  // True = connect when game loads
    bool m_autoConnectDone = false;     // True = already attempted

    // Hosting
    bool m_hostingServer = false;       // True if we launched the server process

    // Connection retry (handles UPnP mapping delay on remote host)
    int  m_connectAttempt = 0;
    int  m_maxConnectAttempts = 6;      // ~30 seconds total (5s per attempt)
    bool m_retryPending = false;
    std::chrono::steady_clock::time_point m_retryTime;

    // Game load detection state (members instead of statics so they reset properly)
    std::chrono::steady_clock::time_point m_firstUpdateTime;
    std::chrono::steady_clock::time_point m_lastPlayerBaseCheck;
    bool m_startupDelayPassed = false;
    bool m_playerBaseCheckedOnce = false;
    bool m_firstUpdateTimeSet = false;
    int  m_playerBasePollCount = 0;

    // Auto-connect timer state (members instead of statics)
    std::chrono::steady_clock::time_point m_gameLoadedTime;
    bool m_gameLoadedTimerStarted = false;

    // Native MyGUI menu
    NativeMenu m_nativeMenu;

    // Server query client (separate ENet host for browsing)
    ServerQueryClient m_queryClient;

    // General
    std::mutex m_mutex;
    float m_uptime = 0.f;
    int m_browserFrameCounter = 0;

    static constexpr int MAX_CHAT_HISTORY = 100;
};

} // namespace kmp

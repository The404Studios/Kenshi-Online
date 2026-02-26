#pragma once
#include "kmp/types.h"
#include <string>
#include <vector>
#include <mutex>
#include <deque>

namespace kmp {

class Overlay {
public:
    void Render(); // Called every frame from Present hook
    void Shutdown();

    // Chat
    void AddChatMessage(PlayerID sender, const std::string& message);
    void AddSystemMessage(const std::string& message);

    // Player list
    void AddPlayer(const PlayerInfo& player);
    void RemovePlayer(PlayerID id);
    void UpdatePlayerPing(PlayerID id, uint32_t ping);

    // Input capture state
    bool IsInputCapture() const { return m_chatOpen || m_connectionOpen || m_serverBrowserOpen; }

    // Show/hide
    void ToggleChat() { m_chatOpen = !m_chatOpen; }
    void TogglePlayerList() { m_playerListOpen = !m_playerListOpen; }
    void ToggleConnectionUI() { m_connectionOpen = !m_connectionOpen; }
    void ToggleServerBrowser() { m_serverBrowserOpen = !m_serverBrowserOpen; }
    void CloseAll() {
        m_chatOpen = false;
        m_playerListOpen = false;
        m_connectionOpen = false;
        m_serverBrowserOpen = false;
    }

private:
    void RenderHUD();
    void RenderChat();
    void RenderPlayerList();
    void RenderConnectionUI();
    void RenderServerBrowser();
    void RenderDebugOverlay();

    // Chat
    struct ChatEntry {
        PlayerID    sender;
        std::string senderName;
        std::string message;
        float       timestamp;
        bool        isSystem;
    };
    std::deque<ChatEntry> m_chatHistory;
    char m_chatInput[256] = {};
    bool m_chatOpen = false;
    bool m_chatScrollToBottom = false;

    // Player list
    std::vector<PlayerInfo> m_players;
    bool m_playerListOpen = false;

    // Connection
    char m_serverAddress[128] = "127.0.0.1";
    char m_serverPort[8] = "27800";
    char m_playerName[32] = "Player";
    bool m_connectionOpen = false;
    bool m_connecting = false;

    // Server browser
    struct ServerEntry {
        std::string name;
        std::string address;
        uint16_t    port;
        int         players;
        int         maxPlayers;
        uint32_t    ping;
    };
    std::vector<ServerEntry> m_serverList;
    bool m_serverBrowserOpen = false;
    bool m_refreshing = false;

    // Debug
    bool m_debugOpen = false;

    // General
    std::mutex m_mutex;
    float m_uptime = 0.f;

    static constexpr int MAX_CHAT_HISTORY = 100;
};

} // namespace kmp

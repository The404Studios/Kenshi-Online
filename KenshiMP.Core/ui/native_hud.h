#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <chrono>

namespace kmp {

// Native MyGUI in-game HUD — replaces the Win32 GDI overlay.
// Uses Kenshi_MultiplayerHUD.layout for all rendering.
// Shows: connection status bar, debug/loading log, chat, player list.
// Visible on both main menu and in-game.
class NativeHud {
public:
    bool Init();
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Call every frame from Present hook
    void Update();

    // Show/hide the entire HUD
    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }

    // Toggle panels
    void TogglePlayerList();
    void ToggleLogPanel();
    void ToggleDebugInfo() { m_showDebug = !m_showDebug; }

    // Chat
    void AddChatMessage(const std::string& sender, const std::string& message);
    void AddSystemMessage(const std::string& message);

    // Debug/loading log — thread-safe, called from any thread
    // Replaces the old GDI HudOverlay::LogStep
    void LogStep(const std::string& tag, const std::string& message);

    // Chat input (WndProc-driven)
    bool IsChatInputActive() const { return m_chatInputActive; }
    void OpenChatInput();
    void CloseChatInput();
    void OnChatChar(wchar_t ch);
    void OnChatKeyDown(int vk);

    bool IsPlayerListVisible() const { return m_showPlayerList; }
    bool IsDebugVisible() const { return m_showDebug; }
    bool IsLogPanelVisible() const { return m_showLogPanel; }

    static constexpr int MAX_CHAT_LINES = 10;
    static constexpr int MAX_PLAYER_ROWS = 8;
    static constexpr int MAX_LOG_LINES = 20;

private:
    bool CacheWidgets();
    bool CreateWidgetsFallback();  // Programmatic creation when layout fails
    void UpdateStatusBar();
    void UpdateChatDisplay();
    void UpdatePlayerList();
    void UpdateLogPanel();
    void SendChatMessage();

    // Widget pointers
    void* m_root = nullptr;
    void* m_statusBar = nullptr;
    void* m_statusText = nullptr;
    void* m_chatPanel = nullptr;
    void* m_chatLines[MAX_CHAT_LINES] = {};
    void* m_chatInput = nullptr;
    void* m_playerListPanel = nullptr;
    void* m_playerListTitle = nullptr;
    void* m_playerRows[MAX_PLAYER_ROWS] = {};

    // Log panel widgets
    void* m_logPanel = nullptr;
    void* m_logTitle = nullptr;
    void* m_logLines[MAX_LOG_LINES] = {};
    void* m_logHint = nullptr;

    // State
    bool m_initialized = false;
    bool m_visible = false;
    bool m_showPlayerList = false;
    bool m_showDebug = false;
    bool m_showLogPanel = true;  // Visible by default during loading
    bool m_chatInputActive = false;
    std::string m_chatInputText;
    int m_frameCounter = 0;

    // Chat message history
    struct ChatEntry {
        std::string text;
        std::string timestamp;  // "[HH:MM]" formatted
        bool isSystem;
        std::chrono::steady_clock::time_point time;
    };
    std::deque<ChatEntry> m_chatHistory;
    static constexpr int MAX_CHAT_HISTORY = 50;
    static constexpr int CHAT_FADE_SECONDS = 30;

    // Debug/loading log entries
    struct LogEntry {
        std::string tag;
        std::string message;
        std::chrono::steady_clock::time_point time;
    };
    std::deque<LogEntry> m_logEntries;
    static constexpr int MAX_LOG_ENTRIES = 100;

    std::mutex m_chatMutex;
    std::mutex m_logMutex;

    // Auto-hide log panel after game loads
    bool m_logAutoHideTriggered = false;
    std::chrono::steady_clock::time_point m_gameLoadedTime;
};

} // namespace kmp

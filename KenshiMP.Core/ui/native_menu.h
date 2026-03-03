#pragma once
#include <string>

namespace kmp {

// Manages the native MyGUI multiplayer panel (Kenshi_MultiplayerPanel.layout).
// Looks indistinguishable from native Kenshi UI — same fonts, skins, styling.
class NativeMenu {
public:
    static constexpr int MAX_SERVER_ROWS = 8;

    // Initialize: resolve MyGUI bridge, load layout, cache widget pointers.
    // Call after MyGUI is initialized (i.e., after Kenshi's main menu is up).
    bool Init();
    bool IsInitialized() const { return m_initialized; }

    // Show/hide the entire panel
    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }

    // Called each frame to detect button clicks and manage state
    void Update();

    // Handle a mouse click at screen coordinates (from WndProc, no ImGui needed)
    void OnClick(int screenX, int screenY);

    // Handle keyboard input for EditBox fields (from WndProc WM_CHAR/WM_KEYDOWN)
    void OnChar(wchar_t ch);
    void OnKeyDown(int vk);
    bool HasActiveEditBox() const { return m_activeField != ActiveField::None; }

    // Shutdown: unload layout
    void Shutdown();

    // Read user input from EditBoxes
    std::string GetServerIP();
    std::string GetServerPort();
    std::string GetPlayerName();

    // Set status text (shown in join panel)
    void SetStatus(const std::string& text);

    // Server browser widget accessors (used by Overlay to update display)
    void* GetServerNameWidget(int row) { return (row >= 0 && row < MAX_SERVER_ROWS) ? m_serverNameTexts[row] : nullptr; }
    void* GetServerInfoWidget(int row) { return (row >= 0 && row < MAX_SERVER_ROWS) ? m_serverInfoTexts[row] : nullptr; }

    // Sub-panel visibility
    enum class Panel { MainButtons, Join, Settings, ServerBrowser };
    void ShowPanel(Panel panel);

private:
    // Button action handlers
    void OnHostClicked();
    void OnConnectClicked();
    void OnSettingsSaved();
    void OnServerBrowserClicked();
    void OnRefreshServersClicked();

    // Attempt to resolve all widget pointers after layout is loaded
    bool CacheWidgets();

    // Widget pointers (opaque — these are MyGUI::Widget*)
    void* m_root = nullptr;               // Root fullscreen overlay
    void* m_multiplayerPanel = nullptr;    // The floating panel

    // Main buttons
    void* m_hostButton = nullptr;
    void* m_joinButton = nullptr;
    void* m_settingsButton = nullptr;
    void* m_browserButton = nullptr;
    void* m_backButton = nullptr;

    // Join sub-panel
    void* m_joinPanel = nullptr;
    void* m_serverIPEdit = nullptr;
    void* m_serverPortEdit = nullptr;
    void* m_playerNameEdit = nullptr;
    void* m_connectButton = nullptr;
    void* m_joinBackButton = nullptr;
    void* m_statusText = nullptr;

    // Settings sub-panel
    void* m_settingsPanel = nullptr;
    void* m_settingsNameEdit = nullptr;
    void* m_autoConnectTick = nullptr;
    void* m_settingsBackButton = nullptr;

    // Server Browser sub-panel
    void* m_browserPanel = nullptr;
    void* m_browserTitle = nullptr;
    void* m_browserRefreshButton = nullptr;
    void* m_browserBackButton = nullptr;
    void* m_browserStatusText = nullptr;
    // Server list rows (up to MAX_SERVER_ROWS)
    void* m_serverNameTexts[MAX_SERVER_ROWS] = {};
    void* m_serverInfoTexts[MAX_SERVER_ROWS] = {};

    // Credit
    void* m_creditText = nullptr;

    // Active EditBox tracking for keyboard input
    enum class ActiveField { None, IP, Port, Name, SettingsName };
    ActiveField m_activeField = ActiveField::None;

    // State
    bool m_initialized = false;
    bool m_visible = false;
    Panel m_currentPanel = Panel::MainButtons;
    int m_selectedServerRow = -1;

    // Button click detection via screen-space coordinates
    // These are the position_real values from the layout, relative to the panel
    struct ButtonRect {
        float relX, relY, relW, relH;  // Relative to MultiplayerPanel
    };

    bool IsButtonClicked(void* button, const ButtonRect& rect);

    // Panel position_real in screen space (from Root > MultiplayerPanel)
    static constexpr float PANEL_X = 0.520312f;
    static constexpr float PANEL_Y = 0.138889f;
    static constexpr float PANEL_W = 0.364062f;
    static constexpr float PANEL_H = 0.768519f;

    // Main button rects (relative to panel)
    static constexpr ButtonRect HOST_BTN     = {0.1f, 0.13f, 0.8f, 0.09f};
    static constexpr ButtonRect JOIN_BTN     = {0.1f, 0.25f, 0.8f, 0.09f};
    static constexpr ButtonRect SETTINGS_BTN = {0.1f, 0.37f, 0.8f, 0.09f};
    static constexpr ButtonRect BROWSER_BTN  = {0.1f, 0.49f, 0.8f, 0.09f};
    static constexpr ButtonRect BACK_BTN     = {0.1f, 0.61f, 0.8f, 0.09f};

    // Join panel buttons (relative to join panel which is at 0.05, 0.13, 0.9, 0.75 within panel)
    static constexpr float JOIN_PANEL_X = 0.05f;
    static constexpr float JOIN_PANEL_Y = 0.13f;
    static constexpr float JOIN_PANEL_W = 0.9f;
    static constexpr float JOIN_PANEL_H = 0.75f;

    static constexpr ButtonRect CONNECT_BTN   = {0.05f, 0.5f, 0.42f, 0.14f};
    static constexpr ButtonRect JOIN_BACK_BTN = {0.53f, 0.5f, 0.42f, 0.14f};

    // EditBox rects within join sub-panel (for click-to-focus detection)
    static constexpr ButtonRect IP_EDIT_RECT   = {0.32f, 0.0f, 0.66f, 0.1f};
    static constexpr ButtonRect PORT_EDIT_RECT = {0.32f, 0.14f, 0.3f, 0.1f};
    static constexpr ButtonRect NAME_EDIT_RECT = {0.32f, 0.28f, 0.66f, 0.1f};

    // EditBox rect within settings sub-panel
    static constexpr ButtonRect SETTINGS_NAME_EDIT_RECT = {0.42f, 0.0f, 0.56f, 0.1f};

    // Settings panel buttons (relative to settings panel same origin as join panel)
    static constexpr ButtonRect SETTINGS_BACK_BTN = {0.25f, 0.5f, 0.5f, 0.14f};
    static constexpr ButtonRect AUTO_CONNECT_TICK = {0.42f, 0.15f, 0.06f, 0.08f};

    // Server browser panel (same sub-panel origin as join panel)
    static constexpr ButtonRect BROWSER_REFRESH_BTN = {0.05f, 0.85f, 0.42f, 0.12f};
    static constexpr ButtonRect BROWSER_BACK_BTN    = {0.53f, 0.85f, 0.42f, 0.12f};
    // Server rows (8 rows, each ~0.08 height, starting at Y=0.08)
    static constexpr float SERVER_ROW_Y_START = 0.08f;
    static constexpr float SERVER_ROW_HEIGHT  = 0.085f;

    // Convert sub-panel-relative rect to screen coords and test click
    bool IsSubPanelButtonClicked(const ButtonRect& btnInSubPanel,
                                  float subPanelX, float subPanelY,
                                  float subPanelW, float subPanelH);

    // Auto-connect tick state (MyGUI TickBox doesn't have built-in state)
    bool m_autoConnectChecked = true;

    // Stored values — fallback if GetCaption fails (SSO mismatch, etc.)
    std::string m_storedIP = "127.0.0.1";
    std::string m_storedPort = "27800";
    std::string m_storedName = "Player";
};

} // namespace kmp

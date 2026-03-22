#include "native_menu.h"
#include "mygui_bridge.h"
#include "../core.h"
#include "../hooks/entity_hooks.h"
#include "kmp/constants.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <Windows.h>

namespace kmp {

bool NativeMenu::Init() {
    if (m_initialized) return true;

    // Cooldown: if Init() failed recently, don't retry for 10 seconds.
    // Prevents rapid crash-catch-retry loops that corrupt MyGUI state
    // (e.g., at the logo screen before resources are loaded).
    static auto s_lastFailTime = std::chrono::steady_clock::time_point{};
    static bool s_hasFailedBefore = false;
    if (s_hasFailedBefore) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_lastFailTime);
        if (elapsed.count() < 10) {
            return false; // Still in cooldown
        }
    }

    OutputDebugStringA("KMP: NativeMenu::Init() called\n");

    auto& bridge = MyGuiBridge::Get();
    if (!bridge.Init()) {
        spdlog::warn("NativeMenu: MyGuiBridge not ready yet");
        s_hasFailedBefore = true;
        s_lastFailTime = std::chrono::steady_clock::now();
        return false;
    }
    OutputDebugStringA("KMP: NativeMenu — MyGuiBridge ready\n");

    if (!bridge.LoadLayout("Kenshi_MultiplayerPanel.layout")) {
        spdlog::error("NativeMenu: Failed to load Kenshi_MultiplayerPanel.layout");
        s_hasFailedBefore = true;
        s_lastFailTime = std::chrono::steady_clock::now();
        return false;
    }

    if (!CacheWidgets()) {
        spdlog::error("NativeMenu: Failed to cache widgets");
        bridge.UnloadLayout("Kenshi_MultiplayerPanel.layout");
        s_hasFailedBefore = true;
        s_lastFailTime = std::chrono::steady_clock::now();
        return false;
    }

    // Pre-populate EditBoxes with config values
    auto& config = Core::Get().GetConfig();
    bridge.SetCaption(m_serverIPEdit, config.lastServer);
    bridge.SetCaption(m_serverPortEdit, std::to_string(config.lastPort));
    bridge.SetCaption(m_playerNameEdit, config.playerName);
    bridge.SetCaption(m_settingsNameEdit, config.playerName);
    m_autoConnectChecked = config.autoConnect;

    // Disable MyGUI keyboard focus AND make read-only — we handle all text input
    // manually via WndProc (OnChar/OnKeyDown). NeedKeyFocus=false prevents focus
    // requests, but Kenshi's OIS input can still deliver keystrokes via MyGUI's
    // injectKeyPress. ReadOnly=true prevents the EditBox from processing them
    // internally, eliminating double-typing.
    bridge.SetProperty(m_serverIPEdit, "NeedKeyFocus", "false");
    bridge.SetProperty(m_serverPortEdit, "NeedKeyFocus", "false");
    bridge.SetProperty(m_playerNameEdit, "NeedKeyFocus", "false");
    bridge.SetProperty(m_settingsNameEdit, "NeedKeyFocus", "false");
    bridge.SetProperty(m_serverIPEdit, "ReadOnly", "true");
    bridge.SetProperty(m_serverPortEdit, "ReadOnly", "true");
    bridge.SetProperty(m_playerNameEdit, "ReadOnly", "true");
    bridge.SetProperty(m_settingsNameEdit, "ReadOnly", "true");

    // Store the values we set so we have reliable fallbacks if GetCaption fails
    m_storedIP = config.lastServer;
    m_storedPort = std::to_string(config.lastPort);
    m_storedName = config.playerName;

    m_initialized = true;
    OutputDebugStringA("KMP: NativeMenu — initialized, setting captions...\n");

    // Explicitly set button captions (layout XML captions may not render if font is wrong)
    bridge.SetCaption(m_hostButton, "HOST GAME");
    bridge.SetCaption(m_joinButton, "JOIN GAME");
    bridge.SetCaption(m_settingsButton, "SETTINGS");
    if (m_browserButton) bridge.SetCaption(m_browserButton, "SERVER BROWSER");
    bridge.SetCaption(m_backButton, "BACK");
    bridge.SetCaption(m_connectButton, "CONNECT");
    bridge.SetCaption(m_joinBackButton, "BACK");
    bridge.SetCaption(m_settingsBackButton, "BACK");
    if (m_creditText) bridge.SetCaption(m_creditText, "KenshiMP by fourzerofour");

    // IMPORTANT: Start hidden — the Root widget may be visible by default from the layout.
    // If left visible, it blocks all clicks from reaching the game (splash screen, etc.)
    bridge.SetVisible(m_root, false);
    m_visible = false;

    spdlog::info("NativeMenu: Initialized successfully (hidden by default)");
    return true;
}

bool NativeMenu::CacheWidgets() {
    auto& bridge = MyGuiBridge::Get();

    struct WidgetLookup {
        const char* name;
        void** target;
    };

    WidgetLookup lookups[] = {
        {"MPRoot",              &m_root},
        {"MultiplayerPanel",   &m_multiplayerPanel},
        {"MPHostButton",       &m_hostButton},
        {"MPJoinButton",       &m_joinButton},
        {"MPSettingsButton",   &m_settingsButton},
        {"MPBrowserButton",    &m_browserButton},
        {"MPBackButton",       &m_backButton},
        {"MPJoinPanel",        &m_joinPanel},
        {"MPServerIP",         &m_serverIPEdit},
        {"MPServerPort",       &m_serverPortEdit},
        {"MPPlayerName",       &m_playerNameEdit},
        {"MPConnectButton",    &m_connectButton},
        {"MPJoinBackButton",   &m_joinBackButton},
        {"MPStatusText",       &m_statusText},
        {"MPSettingsPanel",    &m_settingsPanel},
        {"MPSettingsName",     &m_settingsNameEdit},
        {"MPAutoConnectTick",  &m_autoConnectTick},
        {"MPSettingsBackButton", &m_settingsBackButton},
        {"MPBrowserPanel",     &m_browserPanel},
        {"MPBrowserTitle",     &m_browserTitle},
        {"MPBrowserRefresh",   &m_browserRefreshButton},
        {"MPBrowserBack",      &m_browserBackButton},
        {"MPBrowserStatus",    &m_browserStatusText},
        {"MPCreditText",       &m_creditText},
    };

    int found = 0;
    int total = sizeof(lookups) / sizeof(lookups[0]);

    for (auto& lk : lookups) {
        *lk.target = bridge.FindWidget(lk.name);
        if (*lk.target) {
            found++;
            spdlog::info("NativeMenu: Found widget '{}' at 0x{:X}", lk.name, (uintptr_t)*lk.target);
        } else {
            spdlog::warn("NativeMenu: Widget '{}' not found", lk.name);
        }
    }

    // Cache server browser row widgets
    for (int i = 0; i < MAX_SERVER_ROWS; i++) {
        std::string nameKey = "MPServerName" + std::to_string(i);
        std::string infoKey = "MPServerInfo" + std::to_string(i);
        m_serverNameTexts[i] = bridge.FindWidget(nameKey);
        m_serverInfoTexts[i] = bridge.FindWidget(infoKey);
        if (m_serverNameTexts[i]) found++;
        if (m_serverInfoTexts[i]) found++;
    }

    spdlog::info("NativeMenu: Cached {}/{} widgets (+server rows)", found, total);

    // Root and panel are required
    return m_root != nullptr && m_multiplayerPanel != nullptr;
}

void NativeMenu::Show() {
    if (!m_initialized) {
        if (!Init()) return;
    }

    auto& bridge = MyGuiBridge::Get();
    bridge.SetVisible(m_root, true);
    m_visible = true;
    ShowPanel(Panel::MainButtons);
    spdlog::info("NativeMenu: Shown");
}

void NativeMenu::Hide() {
    if (!m_initialized) return;

    auto& bridge = MyGuiBridge::Get();
    bridge.SetVisible(m_root, false);
    m_visible = false;
    m_activeField = ActiveField::None; // Clear focus state so no ghost input
    spdlog::info("NativeMenu: Hidden");
}

void NativeMenu::ShowPanel(Panel panel) {
    if (!m_initialized) return;

    auto& bridge = MyGuiBridge::Get();
    m_currentPanel = panel;
    m_activeField = ActiveField::None;

    // Hide all sub-panels
    if (m_joinPanel)     bridge.SetVisible(m_joinPanel, false);
    if (m_settingsPanel) bridge.SetVisible(m_settingsPanel, false);
    if (m_browserPanel)  bridge.SetVisible(m_browserPanel, false);

    // Show/hide main buttons
    bool showMain = (panel == Panel::MainButtons);
    if (m_hostButton)     bridge.SetVisible(m_hostButton, showMain);
    if (m_joinButton)     bridge.SetVisible(m_joinButton, showMain);
    if (m_settingsButton) bridge.SetVisible(m_settingsButton, showMain);
    if (m_browserButton)  bridge.SetVisible(m_browserButton, showMain);
    if (m_backButton)     bridge.SetVisible(m_backButton, showMain);

    // Show the selected sub-panel
    switch (panel) {
    case Panel::Join:
        if (m_joinPanel) bridge.SetVisible(m_joinPanel, true);
        break;
    case Panel::Settings:
        if (m_settingsPanel) bridge.SetVisible(m_settingsPanel, true);
        break;
    case Panel::ServerBrowser:
        if (m_browserPanel) bridge.SetVisible(m_browserPanel, true);
        break;
    case Panel::MainButtons:
        break;
    }
}

void NativeMenu::Update() {
    // Update() is now a no-op — input is handled by OnClick() and WndProc
}

void NativeMenu::OnClick(int screenX, int screenY) {
    if (!m_initialized || !m_visible) return;

    // Get screen dimensions from the actual window
    RECT clientRect;
    HWND hwnd = GetActiveWindow();
    if (!hwnd || !GetClientRect(hwnd, &clientRect)) return;

    float screenW = static_cast<float>(clientRect.right - clientRect.left);
    float screenH = static_cast<float>(clientRect.bottom - clientRect.top);

    float mx = static_cast<float>(screenX);
    float my = static_cast<float>(screenY);

    // Convert panel-relative button rect to screen coords and test click
    auto testMainButton = [&](const ButtonRect& btn) -> bool {
        float absX = (PANEL_X + btn.relX * PANEL_W) * screenW;
        float absY = (PANEL_Y + btn.relY * PANEL_H) * screenH;
        float absW = btn.relW * PANEL_W * screenW;
        float absH = btn.relH * PANEL_H * screenH;
        return mx >= absX && mx <= absX + absW && my >= absY && my <= absY + absH;
    };

    auto testSubPanelButton = [&](const ButtonRect& btn,
                                   float spX, float spY, float spW, float spH) -> bool {
        // Sub-panel is positioned relative to panel, button is relative to sub-panel
        float absX = (PANEL_X + (spX + btn.relX * spW) * PANEL_W) * screenW;
        float absY = (PANEL_Y + (spY + btn.relY * spH) * PANEL_H) * screenH;
        float absW = btn.relW * spW * PANEL_W * screenW;
        float absH = btn.relH * spH * PANEL_H * screenH;
        return mx >= absX && mx <= absX + absW && my >= absY && my <= absY + absH;
    };

    switch (m_currentPanel) {
    case Panel::MainButtons:
        if (testMainButton(HOST_BTN)) {
            OnHostClicked();
        } else if (testMainButton(JOIN_BTN)) {
            // Load config values into EditBoxes
            auto& config = Core::Get().GetConfig();
            auto& bridge = MyGuiBridge::Get();
            m_storedIP = config.lastServer;
            m_storedPort = std::to_string(config.lastPort);
            m_storedName = config.playerName;
            bridge.SetCaption(m_serverIPEdit, m_storedIP);
            bridge.SetCaption(m_serverPortEdit, m_storedPort);
            bridge.SetCaption(m_playerNameEdit, m_storedName);
            SetStatus("");
            ShowPanel(Panel::Join);
            spdlog::info("NativeMenu: JOIN GAME clicked");
        } else if (testMainButton(SETTINGS_BTN)) {
            auto& config = Core::Get().GetConfig();
            auto& bridge = MyGuiBridge::Get();
            m_storedName = config.playerName;
            bridge.SetCaption(m_settingsNameEdit, m_storedName);
            m_autoConnectChecked = config.autoConnect;
            ShowPanel(Panel::Settings);
            spdlog::info("NativeMenu: SETTINGS clicked");
        } else if (testMainButton(BROWSER_BTN)) {
            OnServerBrowserClicked();
        } else if (testMainButton(BACK_BTN)) {
            Hide();
            spdlog::info("NativeMenu: BACK clicked");
        }
        break;

    case Panel::Join:
        if (testSubPanelButton(CONNECT_BTN, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::None;
            OnConnectClicked();
        } else if (testSubPanelButton(JOIN_BACK_BTN, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::None;
            ShowPanel(Panel::MainButtons);
            spdlog::info("NativeMenu: Join BACK clicked");
        } else if (testSubPanelButton(IP_EDIT_RECT, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::IP;
            spdlog::info("NativeMenu: IP EditBox focused");
        } else if (testSubPanelButton(PORT_EDIT_RECT, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::Port;
            spdlog::info("NativeMenu: Port EditBox focused");
        } else if (testSubPanelButton(NAME_EDIT_RECT, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::Name;
            spdlog::info("NativeMenu: Name EditBox focused");
        } else {
            m_activeField = ActiveField::None; // Click outside any field
        }
        break;

    case Panel::Settings:
        if (testSubPanelButton(AUTO_CONNECT_TICK, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_autoConnectChecked = !m_autoConnectChecked;
            spdlog::info("NativeMenu: Auto-connect toggled to {}", m_autoConnectChecked);
        } else if (testSubPanelButton(SETTINGS_BACK_BTN, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::None;
            OnSettingsSaved();
            ShowPanel(Panel::MainButtons);
            spdlog::info("NativeMenu: Settings BACK clicked (saved)");
        } else if (testSubPanelButton(SETTINGS_NAME_EDIT_RECT, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            m_activeField = ActiveField::SettingsName;
            spdlog::info("NativeMenu: Settings Name EditBox focused");
        } else {
            m_activeField = ActiveField::None;
        }
        break;

    case Panel::ServerBrowser:
        if (testSubPanelButton(BROWSER_REFRESH_BTN, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            OnRefreshServersClicked();
        } else if (testSubPanelButton(BROWSER_BACK_BTN, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
            ShowPanel(Panel::MainButtons);
            spdlog::info("NativeMenu: Browser BACK clicked");
        } else {
            // Check if a server row was clicked
            for (int i = 0; i < MAX_SERVER_ROWS; i++) {
                ButtonRect rowRect = {0.0f, SERVER_ROW_Y_START + i * SERVER_ROW_HEIGHT, 1.0f, SERVER_ROW_HEIGHT};
                if (testSubPanelButton(rowRect, JOIN_PANEL_X, JOIN_PANEL_Y, JOIN_PANEL_W, JOIN_PANEL_H)) {
                    m_selectedServerRow = i;
                    // If this row has a valid server, fill in IP/Port
                    auto& config = Core::Get().GetConfig();
                    if (i < static_cast<int>(config.favoriteServers.size())) {
                        std::string addr = config.favoriteServers[i];
                        size_t colon = addr.find(':');
                        if (colon != std::string::npos) {
                            m_storedIP = addr.substr(0, colon);
                            m_storedPort = addr.substr(colon + 1);
                        } else {
                            m_storedIP = addr;
                            m_storedPort = "27800";
                        }
                        auto& bridge = MyGuiBridge::Get();
                        bridge.SetCaption(m_serverIPEdit, m_storedIP);
                        bridge.SetCaption(m_serverPortEdit, m_storedPort);

                        uint16_t port = static_cast<uint16_t>(std::atoi(m_storedPort.c_str()));
                        auto& overlay = Core::Get().GetOverlay();

                        if (Core::Get().IsGameLoaded()) {
                            // Game loaded — connect immediately
                            ShowPanel(Panel::Join);
                            spdlog::info("NativeMenu: Server row {} selected -> {}:{} (game loaded, showing Join)",
                                         i, m_storedIP, m_storedPort);
                        } else {
                            // Game not loaded — queue auto-connect for after save loads
                            overlay.SetAutoConnect(m_storedIP, port);
                            // Save as lastServer so it persists
                            config.lastServer = m_storedIP;
                            config.lastPort = port;
                            Hide();
                            Core::Get().GetNativeHud().AddSystemMessage(
                                "Server selected: " + m_storedIP + ":" + m_storedPort +
                                " — load your save to connect.");
                            spdlog::info("NativeMenu: Server row {} selected -> {}:{} (queued auto-connect, load save to join)",
                                         i, m_storedIP, m_storedPort);
                        }
                    }
                    break;
                }
            }
        }
        break;
    }
}

void NativeMenu::Shutdown() {
    if (!m_initialized) return;
    Hide();
    MyGuiBridge::Get().UnloadLayout("Kenshi_MultiplayerPanel.layout");
    m_initialized = false;
}

std::string NativeMenu::GetServerIP() {
    // Try to read from widget first (handles case where user clicked CONNECT without typing)
    if (m_serverIPEdit) {
        std::string widgetText = MyGuiBridge::Get().GetCaption(m_serverIPEdit);
        if (!widgetText.empty()) m_storedIP = widgetText;
    }
    return m_storedIP;
}

std::string NativeMenu::GetServerPort() {
    if (m_serverPortEdit) {
        std::string widgetText = MyGuiBridge::Get().GetCaption(m_serverPortEdit);
        if (!widgetText.empty()) m_storedPort = widgetText;
    }
    return m_storedPort;
}

std::string NativeMenu::GetPlayerName() {
    if (m_playerNameEdit) {
        std::string widgetText = MyGuiBridge::Get().GetCaption(m_playerNameEdit);
        if (!widgetText.empty()) m_storedName = widgetText;
    }
    return m_storedName;
}

void NativeMenu::SetStatus(const std::string& text) {
    if (m_statusText) {
        MyGuiBridge::Get().SetCaption(m_statusText, text);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Button action handlers (wired to existing Core logic)
// ═══════════════════════════════════════════════════════════════

void NativeMenu::OnHostClicked() {
    spdlog::info("NativeMenu: HOST GAME clicked");

    // Launch KenshiMP.Server.exe
    static const char s_anchor = 0;
    char dllPath[MAX_PATH] = {};
    HMODULE hSelf = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       &s_anchor,
                       &hSelf);
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);

    std::string dllDir(dllPath);
    size_t lastSlash = dllDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) dllDir = dllDir.substr(0, lastSlash);

    std::string serverPaths[] = {
        dllDir + "\\KenshiMP.Server.exe",
        dllDir + "\\KenshiMP\\build\\bin\\Release\\KenshiMP.Server.exe",
    };

    std::string serverExe;
    for (auto& path : serverPaths) {
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            serverExe = path;
            break;
        }
    }

    if (!serverExe.empty()) {
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        if (CreateProcessA(serverExe.c_str(), nullptr, nullptr, nullptr,
                           FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            auto& overlay = Core::Get().GetOverlay();
            overlay.SetHostingServer(true);
            overlay.SetAutoConnect("127.0.0.1", KMP_DEFAULT_PORT);

            spdlog::info("NativeMenu: Launched server: {}", serverExe);

            // HIDE the panel so user can click Kenshi's New Game button
            Hide();
        } else {
            SetStatus("Failed to launch server.");
            spdlog::error("NativeMenu: CreateProcess failed: {}", GetLastError());
        }
    } else {
        SetStatus("Server exe not found.");
        spdlog::error("NativeMenu: Server exe not found in any expected path");
    }
}

void NativeMenu::OnConnectClicked() {
    spdlog::info("NativeMenu: CONNECT clicked");

    auto& core = Core::Get();

    // Gate on game load — don't allow connection from main menu
    if (!core.IsGameLoaded()) {
        SetStatus("Load a save first, then connect.");
        core.GetNativeHud().AddSystemMessage("Please load a save game first, then connect.");
        spdlog::info("NativeMenu: Connect blocked — game not loaded yet");
        return;
    }

    std::string ip = GetServerIP();
    std::string portStr = GetServerPort();
    std::string name = GetPlayerName();

    if (ip.empty()) ip = "127.0.0.1";
    if (portStr.empty()) portStr = std::to_string(KMP_DEFAULT_PORT);
    if (name.empty()) name = "Player";

    uint16_t port = static_cast<uint16_t>(std::atoi(portStr.c_str()));

    spdlog::info("NativeMenu: Connecting to {}:{} as '{}'", ip, port, name);

    auto& overlay = core.GetOverlay();

    // Update overlay's connection state so it can handle the async result
    overlay.SetConnectionInfo(ip, port, name);

    // Log to NativeHud (visible immediately)
    core.GetNativeHud().LogStep("NET", "Connecting to " + ip + ":" + portStr + "...");
    core.GetNativeHud().AddSystemMessage("Connecting to " + ip + ":" + portStr + " as '" + name + "'...");

    // Game is loaded (early return above guarantees this).
    // Do NOT call ResumeForNetwork() here — it will be called from
    // HandleHandshakeAck() after the handshake completes.
    auto& client = core.GetClient();

    // Reset stale connection state
    if (client.IsConnected() || client.IsConnecting()) {
        client.Disconnect();
        core.SetConnected(false);
    }

    if (client.ConnectAsync(ip.c_str(), port)) {
        overlay.SetConnecting(true);
        core.TransitionTo(ClientPhase::Connecting);
        spdlog::info("NativeMenu: ConnectAsync started (game loaded)");
        core.GetNativeHud().LogStep("NET", "Connection started");
        Hide();
    } else {
        SetStatus("Failed to connect. Check server address.");
        core.GetNativeHud().LogStep("ERR", "ConnectAsync failed!");
    }
}

void NativeMenu::OnSettingsSaved() {
    std::string name = m_storedName;
    if (name.empty()) name = "Player";

    auto& config = Core::Get().GetConfig();
    config.playerName = name;
    config.autoConnect = m_autoConnectChecked;
    config.Save(ClientConfig::GetInstancePath());

    Core::Get().GetOverlay().SetPlayerName(name);

    spdlog::info("NativeMenu: Settings saved (name='{}', autoConnect={})", name, m_autoConnectChecked);
}

void NativeMenu::OnServerBrowserClicked() {
    spdlog::info("NativeMenu: SERVER BROWSER clicked");
    ShowPanel(Panel::ServerBrowser);

    // Populate server list from favorites
    auto& bridge = MyGuiBridge::Get();
    auto& config = Core::Get().GetConfig();
    for (int i = 0; i < MAX_SERVER_ROWS; i++) {
        if (i < static_cast<int>(config.favoriteServers.size())) {
            if (m_serverNameTexts[i]) bridge.SetCaption(m_serverNameTexts[i], config.favoriteServers[i]);
            if (m_serverInfoTexts[i]) bridge.SetCaption(m_serverInfoTexts[i], "...");
        } else {
            if (m_serverNameTexts[i]) bridge.SetCaption(m_serverNameTexts[i], "");
            if (m_serverInfoTexts[i]) bridge.SetCaption(m_serverInfoTexts[i], "");
        }
    }

    if (m_browserStatusText) bridge.SetCaption(m_browserStatusText, "Click REFRESH to query servers");
}

void NativeMenu::OnRefreshServersClicked() {
    spdlog::info("NativeMenu: REFRESH clicked");

    auto& config = Core::Get().GetConfig();
    auto& queryClient = Core::Get().GetOverlay().GetQueryClient();

    if (!queryClient.IsQueryActive()) {
        queryClient.Clear();
    }

    // Query the master server for the global server list
    if (!config.masterServer.empty()) {
        queryClient.QueryMasterServer(config.masterServer, config.masterPort);
        spdlog::info("NativeMenu: Querying master server at {}:{}",
                     config.masterServer, config.masterPort);
    }

    // Also query all favorites directly (for servers not on master)
    for (const auto& addr : config.favoriteServers) {
        size_t colon = addr.find(':');
        std::string ip = addr;
        uint16_t port = 27800;
        if (colon != std::string::npos) {
            ip = addr.substr(0, colon);
            port = static_cast<uint16_t>(std::atoi(addr.substr(colon + 1).c_str()));
        }
        queryClient.QueryServer(ip, port);
    }

    int totalQueries = static_cast<int>(config.favoriteServers.size()) +
                       (config.masterServer.empty() ? 0 : 1);
    if (m_browserStatusText) {
        MyGuiBridge::Get().SetCaption(m_browserStatusText,
            "Querying " + std::to_string(totalQueries) + " sources...");
    }
}

// ═══════════════════════════════════════════════════════════════
//  Keyboard input for EditBox fields (WndProc-driven)
// ═══════════════════════════════════════════════════════════════

void NativeMenu::OnChar(wchar_t ch) {
    if (!m_initialized || !m_visible) return;
    if (m_activeField == ActiveField::None) return;

    // Get reference to the active stored string
    std::string* target = nullptr;
    void* editWidget = nullptr;
    size_t maxLen = 64;

    switch (m_activeField) {
    case ActiveField::IP:
        target = &m_storedIP;
        editWidget = m_serverIPEdit;
        maxLen = 45; // IPv4/IPv6
        break;
    case ActiveField::Port:
        target = &m_storedPort;
        editWidget = m_serverPortEdit;
        maxLen = 5;
        break;
    case ActiveField::Name:
        target = &m_storedName;
        editWidget = m_playerNameEdit;
        maxLen = 31;
        break;
    case ActiveField::SettingsName:
        target = &m_storedName;
        editWidget = m_settingsNameEdit;
        maxLen = 31;
        break;
    default:
        return;
    }

    // Filter: only printable ASCII
    if (ch >= 32 && ch < 127 && target->size() < maxLen) {
        *target += static_cast<char>(ch);
        MyGuiBridge::Get().SetCaption(editWidget, *target);
    }
}

void NativeMenu::OnKeyDown(int vk) {
    if (!m_initialized || !m_visible) return;
    if (m_activeField == ActiveField::None) return;

    std::string* target = nullptr;
    void* editWidget = nullptr;

    switch (m_activeField) {
    case ActiveField::IP:
        target = &m_storedIP;
        editWidget = m_serverIPEdit;
        break;
    case ActiveField::Port:
        target = &m_storedPort;
        editWidget = m_serverPortEdit;
        break;
    case ActiveField::Name:
        target = &m_storedName;
        editWidget = m_playerNameEdit;
        break;
    case ActiveField::SettingsName:
        target = &m_storedName;
        editWidget = m_settingsNameEdit;
        break;
    default:
        return;
    }

    if (vk == VK_BACK) {
        // Backspace: remove last character
        if (!target->empty()) {
            target->pop_back();
            MyGuiBridge::Get().SetCaption(editWidget, *target);
        }
    } else if (vk == VK_RETURN) {
        // Enter: deactivate field, trigger connect if in Join panel
        m_activeField = ActiveField::None;
        if (m_currentPanel == Panel::Join) {
            OnConnectClicked();
        }
    } else if (vk == VK_TAB) {
        // Tab: cycle to next field
        if (m_currentPanel == Panel::Join) {
            if (m_activeField == ActiveField::IP) m_activeField = ActiveField::Port;
            else if (m_activeField == ActiveField::Port) m_activeField = ActiveField::Name;
            else if (m_activeField == ActiveField::Name) m_activeField = ActiveField::IP;
        }
    } else if (vk == VK_ESCAPE) {
        // Escape: deactivate field
        m_activeField = ActiveField::None;
    }
}

} // namespace kmp

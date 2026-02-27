#include "overlay.h"
#include "../core.h"
#include "../game/game_types.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>

namespace kmp {

// ── Color Palette ──
static const ImVec4 COL_ACCENT     = ImVec4(0.90f, 0.55f, 0.10f, 1.0f); // Kenshi orange
static const ImVec4 COL_ACCENT_DIM = ImVec4(0.70f, 0.40f, 0.08f, 1.0f);
static const ImVec4 COL_GREEN      = ImVec4(0.30f, 1.00f, 0.30f, 1.0f);
static const ImVec4 COL_RED        = ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
static const ImVec4 COL_YELLOW     = ImVec4(1.00f, 0.80f, 0.20f, 1.0f);
static const ImVec4 COL_BLUE       = ImVec4(0.40f, 0.80f, 1.00f, 1.0f);
static const ImVec4 COL_DIM        = ImVec4(0.60f, 0.60f, 0.60f, 1.0f);

// Helper: large centered button
static bool BigButton(const char* label, float width = -1.f, float height = 40.f) {
    if (width < 0.f) width = ImGui::GetContentRegionAvail().x;
    return ImGui::Button(label, ImVec2(width, height));
}

void Overlay::Render() {
    m_uptime += ImGui::GetIO().DeltaTime;

    // Load config into UI fields on first frame
    if (m_firstFrame) {
        m_firstFrame = false;
        auto& config = Core::Get().GetConfig();
        strncpy(m_playerName, config.playerName.c_str(), sizeof(m_playerName) - 1);
        strncpy(m_settingsName, config.playerName.c_str(), sizeof(m_settingsName) - 1);
        strncpy(m_serverAddress, config.lastServer.c_str(), sizeof(m_serverAddress) - 1);
        snprintf(m_serverPort, sizeof(m_serverPort), "%d", config.lastPort);
        m_settingsAutoConnect = config.autoConnect;

        // Auto-connect if configured (one-shot: clear the flag after attempting)
        if (config.autoConnect && !config.lastServer.empty()) {
            auto& core = Core::Get();
            if (core.GetClient().ConnectAsync(config.lastServer.c_str(), config.lastPort)) {
                m_connecting = true;
                snprintf(m_statusMessage, sizeof(m_statusMessage),
                         "Auto-connecting to %s:%d...", config.lastServer.c_str(), config.lastPort);
            }
            // Clear auto-connect so future launches don't retry without the injector
            config.autoConnect = false;
            m_settingsAutoConnect = false;
            config.Save(ClientConfig::GetDefaultPath());
        }
    }

    // Handle keybinds — F1 and Escape ALWAYS work (even with text fields focused)
    auto& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        if (m_mainMenuOpen) {
            // Close menu (only if connected, otherwise keep it open)
            if (Core::Get().IsConnected() || Core::Get().GetClient().IsConnected()) {
                m_mainMenuOpen = false;
            }
        } else {
            m_mainMenuOpen = true;
            m_menuPage = MenuPage::Main;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (m_mainMenuOpen && m_menuPage != MenuPage::Main) {
            m_menuPage = MenuPage::Main;
        } else if (m_mainMenuOpen) {
            if (Core::Get().IsConnected() || Core::Get().GetClient().IsConnected()) {
                m_mainMenuOpen = false;
            }
        } else {
            CloseAll();
        }
    }

    // Other keybinds only when no text field is focused
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) TogglePlayerList();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !m_chatOpen) ToggleChat();
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) m_debugOpen = !m_debugOpen;
    }

    // Check async connect result (works from both main menu and in-game)
    if (m_connecting) {
        auto& core = Core::Get();
        if (core.GetClient().IsConnected()) {
            // Connection established — send handshake
            m_connecting = false;
            spdlog::info("Overlay: Connection established, sending handshake as '{}'", m_playerName);
            MsgHandshake hs{};
            hs.protocolVersion = KMP_PROTOCOL_VERSION;
            strncpy(hs.playerName, m_playerName, KMP_MAX_NAME_LENGTH);

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_Handshake);
            writer.WriteRaw(&hs, sizeof(hs));
            core.GetClient().SendReliable(writer.Data(), writer.Size());

            core.GetConfig().playerName = m_playerName;
            core.GetConfig().lastServer = m_serverAddress;
            core.GetConfig().lastPort = static_cast<uint16_t>(std::atoi(m_serverPort));

            m_mainMenuOpen = false;
            m_connectionOpen = false;
            m_statusMessage[0] = '\0';
        } else if (!core.GetClient().IsConnecting()) {
            // Connection failed — reopen main menu so user can retry
            m_connecting = false;
            snprintf(m_statusMessage, sizeof(m_statusMessage), "Connection failed. Is the server running?");
            AddSystemMessage("Connection failed");
            m_mainMenuOpen = true;
            m_menuPage = MenuPage::Main;
        }
    }

    // Main menu (full-screen)
    if (m_mainMenuOpen) {
        RenderMainMenu();
    }

    // In-game HUD and panels (only when not in main menu)
    if (!m_mainMenuOpen) {
        RenderHUD();
        if (m_chatOpen) RenderChat();
        if (m_playerListOpen) RenderPlayerList();
        if (m_connectionOpen) RenderConnectionUI();
        if (m_debugOpen) RenderDebugOverlay();
    }
}

void Overlay::Shutdown() {
    // Save config on shutdown
    auto& core = Core::Get();
    core.GetConfig().playerName = m_playerName;
    core.GetConfig().autoConnect = m_settingsAutoConnect;
}

// ═══════════════════════════════════════════════════════════════
//  MAIN MENU (Full-screen overlay)
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderMainMenu() {
    auto* viewport = ImGui::GetMainViewport();
    ImVec2 vpPos = viewport->WorkPos;
    ImVec2 vpSize = viewport->WorkSize;

    // Full-screen dark background
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                               ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowPos(vpPos);
    ImGui::SetNextWindowSize(vpSize);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    if (ImGui::Begin("##MainMenuBG", nullptr, bgFlags)) {
        // Nothing here — just the dark background
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Center panel
    float panelW = 460.f;
    float panelH = 520.f;
    ImVec2 panelPos(vpPos.x + (vpSize.x - panelW) * 0.5f,
                    vpPos.y + (vpSize.y - panelH) * 0.5f);

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 20));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.98f));

    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;

    if (ImGui::Begin("##MainMenuPanel", nullptr, panelFlags)) {
        switch (m_menuPage) {
        case MenuPage::Main:        RenderMainPage(); break;
        case MenuPage::Connect:     RenderConnectPage(); break;
        case MenuPage::ServerBrowser: RenderBrowserPage(); break;
        case MenuPage::Settings:    RenderSettingsPage(); break;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Version info (bottom-right)
    ImGui::SetNextWindowPos(ImVec2(vpPos.x + vpSize.x - 10, vpPos.y + vpSize.y - 10),
                           ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.f);
    if (ImGui::Begin("##Version", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav)) {
        ImGui::TextColored(COL_DIM, "Kenshi-Online v0.1.0");
    }
    ImGui::End();
}

void Overlay::RenderMainPage() {
    // Title
    ImGui::Dummy(ImVec2(0, 15));

    // Centered title text
    const char* title = "KENSHI-ONLINE";
    float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();

    const char* subtitle = "Multiplayer Mod";
    float subW = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - subW) * 0.5f);
    ImGui::TextColored(COL_DIM, "%s", subtitle);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // Player name
    ImGui::Text("Player Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##MenuPlayerName", m_playerName, sizeof(m_playerName));

    ImGui::Dummy(ImVec2(0, 10));

    // Status message (connection progress, errors)
    if (m_connecting) {
        ImGui::TextColored(COL_YELLOW, "Connecting to %s:%s...", m_serverAddress, m_serverPort);
        ImGui::Spacing();
        if (BigButton("Cancel")) {
            Core::Get().GetClient().Disconnect();
            m_connecting = false;
            m_statusMessage[0] = '\0';
        }
        return;
    }

    if (m_statusMessage[0] != '\0') {
        ImGui::TextColored(COL_RED, "%s", m_statusMessage);
        ImGui::Spacing();
    }

    // Buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.45f, 0.08f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.55f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.35f, 0.05f, 1.0f));

    if (BigButton("Quick Connect", -1.f, 45.f)) {
        m_menuPage = MenuPage::Connect;
    }

    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    if (BigButton("Server Browser", -1.f, 40.f)) {
        m_menuPage = MenuPage::ServerBrowser;
        // Refresh server list
        m_serverList.clear();
        m_serverList.push_back({"Local Server", "127.0.0.1", KMP_DEFAULT_PORT, 0, 16, 0});
        auto& config = Core::Get().GetConfig();
        if (!config.lastServer.empty() && config.lastServer != "127.0.0.1") {
            m_serverList.push_back({"Last Server", config.lastServer, config.lastPort, 0, 16, 0});
        }
    }

    ImGui::Spacing();

    if (BigButton("Settings", -1.f, 40.f)) {
        m_menuPage = MenuPage::Settings;
        strncpy(m_settingsName, m_playerName, sizeof(m_settingsName) - 1);
        m_settingsAutoConnect = Core::Get().GetConfig().autoConnect;
    }

    ImGui::Spacing();

    if (Core::Get().IsConnected()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.50f, 0.15f, 0.90f));
        if (BigButton("Resume Game", -1.f, 40.f)) {
            m_mainMenuOpen = false;
        }
        ImGui::PopStyleColor();
    }

    // Bottom info
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    auto& funcs = Core::Get().GetGameFunctions();
    int resolved = funcs.CountResolved();
    ImGui::TextColored(COL_DIM, "Engine: %d functions resolved", resolved);

    if (Core::Get().IsConnected()) {
        ImGui::TextColored(COL_GREEN, "Status: Connected (%dms)",
                          Core::Get().GetClient().GetPing());
    } else {
        ImGui::TextColored(COL_DIM, "Status: Not connected");
    }

    ImGui::TextColored(COL_DIM, "Press F1 to toggle this menu");
    ImGui::TextColored(COL_DIM, "Load a Kenshi save, then connect to play");
}

void Overlay::RenderConnectPage() {
    ImGui::Dummy(ImVec2(0, 10));

    const char* title = "Quick Connect";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
    ImGui::TextColored(COL_ACCENT, "%s", title);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Text("Server Address:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##ConnAddr", m_serverAddress, sizeof(m_serverAddress));

    ImGui::Spacing();
    ImGui::Text("Port:");
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("##ConnPort", m_serverPort, sizeof(m_serverPort));

    ImGui::Dummy(ImVec2(0, 15));

    // Connect button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.45f, 0.08f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.55f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.35f, 0.05f, 1.0f));

    if (BigButton("Connect", -1.f, 45.f)) {
        auto& core = Core::Get();
        auto& client = core.GetClient();

        // Reset stale connection state so reconnect always works
        if (client.IsConnected() || client.IsConnecting()) {
            spdlog::info("Overlay: Resetting previous connection before reconnect");
            client.Disconnect();
            core.SetConnected(false);
        }

        uint16_t port = static_cast<uint16_t>(std::atoi(m_serverPort));
        spdlog::info("Overlay: Connect clicked -> {}:{}", m_serverAddress, port);

        if (client.ConnectAsync(m_serverAddress, port)) {
            m_connecting = true;
            m_menuPage = MenuPage::Main;
            snprintf(m_statusMessage, sizeof(m_statusMessage),
                     "Connecting to %s:%s...", m_serverAddress, m_serverPort);
        } else {
            spdlog::error("Overlay: ConnectAsync failed for {}:{}", m_serverAddress, port);
            snprintf(m_statusMessage, sizeof(m_statusMessage),
                     "Failed to connect. Check server address.");
            m_menuPage = MenuPage::Main;
        }
    }

    ImGui::PopStyleColor(3);

    ImGui::Dummy(ImVec2(0, 10));

    if (BigButton("Back", -1.f, 35.f)) {
        m_menuPage = MenuPage::Main;
    }
}

void Overlay::RenderBrowserPage() {
    ImGui::Dummy(ImVec2(0, 10));

    const char* title = "Server Browser";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
    ImGui::TextColored(COL_ACCENT, "%s", title);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Refresh")) {
        m_serverList.clear();
        m_serverList.push_back({"Local Server", "127.0.0.1", KMP_DEFAULT_PORT, 0, 16, 0});
        auto& config = Core::Get().GetConfig();
        if (!config.lastServer.empty() && config.lastServer != "127.0.0.1") {
            m_serverList.push_back({"Last Server", config.lastServer, config.lastPort, 0, 16, 0});
        }
    }
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "%d servers", (int)m_serverList.size());

    ImGui::Spacing();

    // Server table
    float tableH = 200.f;
    if (ImGui::BeginTable("##MenuServerTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY,
                          ImVec2(0, tableH))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < m_serverList.size(); i++) {
            auto& srv = m_serverList[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%s", srv.name.c_str());

            ImGui::TableNextColumn();
            ImGui::TextColored(COL_DIM, "%s:%d", srv.address.c_str(), srv.port);

            ImGui::TableNextColumn();
            ImGui::Text("%d/%d", srv.players, srv.maxPlayers);

            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("Join")) {
                strncpy(m_serverAddress, srv.address.c_str(), sizeof(m_serverAddress) - 1);
                snprintf(m_serverPort, sizeof(m_serverPort), "%d", srv.port);
                m_menuPage = MenuPage::Connect;
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Direct connect inline
    ImGui::Text("Direct IP:");
    ImGui::SameLine();
    static char directIP[128] = "";
    ImGui::SetNextItemWidth(170);
    bool enterPressed = ImGui::InputText("##DirectIP", directIP, sizeof(directIP),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (enterPressed || ImGui::Button("Go")) {
        std::string addr(directIP);
        auto colon = addr.find(':');
        if (colon != std::string::npos) {
            strncpy(m_serverAddress, addr.substr(0, colon).c_str(), sizeof(m_serverAddress) - 1);
            snprintf(m_serverPort, sizeof(m_serverPort), "%s", addr.substr(colon + 1).c_str());
        } else {
            strncpy(m_serverAddress, directIP, sizeof(m_serverAddress) - 1);
        }
        m_menuPage = MenuPage::Connect;
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (BigButton("Back", -1.f, 35.f)) {
        m_menuPage = MenuPage::Main;
    }
}

void Overlay::RenderSettingsPage() {
    ImGui::Dummy(ImVec2(0, 10));

    const char* title = "Settings";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
    ImGui::TextColored(COL_ACCENT, "%s", title);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    // Player name
    ImGui::Text("Player Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##SettingsName", m_settingsName, sizeof(m_settingsName));

    ImGui::Dummy(ImVec2(0, 5));

    // Default server
    ImGui::Text("Default Server:");
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("##SettingsAddr", m_serverAddress, sizeof(m_serverAddress));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##SettingsPort", m_serverPort, sizeof(m_serverPort));

    ImGui::Dummy(ImVec2(0, 5));

    // Auto-connect
    ImGui::Checkbox("Auto-connect on startup", &m_settingsAutoConnect);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically connect to the default server when Kenshi starts");
    }

    ImGui::Dummy(ImVec2(0, 10));

    // Keybinds reference
    ImGui::TextColored(COL_ACCENT_DIM, "Keybinds:");
    ImGui::TextColored(COL_DIM, "  F1     - Main Menu / Connect");
    ImGui::TextColored(COL_DIM, "  Tab    - Player List");
    ImGui::TextColored(COL_DIM, "  Enter  - Chat");
    ImGui::TextColored(COL_DIM, "  `      - Debug Overlay");
    ImGui::TextColored(COL_DIM, "  Escape - Close / Back");

    ImGui::Dummy(ImVec2(0, 15));

    // Save & Back
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.45f, 0.08f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.55f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.35f, 0.05f, 1.0f));

    if (BigButton("Save Settings", -1.f, 40.f)) {
        // Apply
        strncpy(m_playerName, m_settingsName, sizeof(m_playerName) - 1);
        auto& config = Core::Get().GetConfig();
        config.playerName = m_settingsName;
        config.lastServer = m_serverAddress;
        config.lastPort = static_cast<uint16_t>(std::atoi(m_serverPort));
        config.autoConnect = m_settingsAutoConnect;
        config.Save(ClientConfig::GetDefaultPath());

        snprintf(m_statusMessage, sizeof(m_statusMessage), "Settings saved.");
        m_menuPage = MenuPage::Main;
    }

    ImGui::PopStyleColor(3);

    ImGui::Spacing();

    if (BigButton("Back", -1.f, 35.f)) {
        m_menuPage = MenuPage::Main;
    }
}

// ═══════════════════════════════════════════════════════════════
//  IN-GAME HUD (shown when main menu is closed)
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderHUD() {
    auto& core = Core::Get();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10, viewport->WorkPos.y + 10),
                           ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);

    if (ImGui::Begin("##KenshiOnlineHUD", nullptr, flags)) {
        if (core.IsConnected()) {
            ImGui::TextColored(COL_GREEN, "KENSHI-ONLINE");
            ImGui::SameLine();
            ImGui::Text("| %d/%d | %dms",
                       (int)m_players.size(), KMP_MAX_PLAYERS,
                       core.GetClient().GetPing());
        } else {
            ImGui::TextColored(COL_ACCENT, "KENSHI-ONLINE");
            ImGui::SameLine();
            ImGui::Text("| Offline | F1: Menu");
        }
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════
//  CHAT WINDOW
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderChat() {
    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetMainViewport()->WorkSize.y - 260),
                           ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Chat", &m_chatOpen, ImGuiWindowFlags_NoCollapse)) {
        float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("ChatScroll", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            std::lock_guard lock(m_mutex);
            for (auto& entry : m_chatHistory) {
                if (entry.isSystem) {
                    ImGui::TextColored(COL_YELLOW, "[System] %s", entry.message.c_str());
                } else {
                    ImGui::TextColored(COL_BLUE, "[%s]", entry.senderName.c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", entry.message.c_str());
                }
            }
            if (m_chatScrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                m_chatScrollToBottom = false;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        bool sendMsg = false;
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("##ChatInput", m_chatInput, sizeof(m_chatInput),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            sendMsg = true;
        }
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Send", ImVec2(50, 0))) sendMsg = true;

        if (sendMsg && m_chatInput[0] != '\0') {
            auto& core = Core::Get();
            if (core.IsConnected()) {
                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_ChatMessage);
                writer.WriteU32(core.GetLocalPlayerId());
                writer.WriteString(std::string(m_chatInput));
                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }
            AddChatMessage(Core::Get().GetLocalPlayerId(), m_chatInput);
            m_chatInput[0] = '\0';
        }
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════
//  PLAYER LIST
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderPlayerList() {
    ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Players", &m_playerListOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Players (%d/%d)", (int)m_players.size(), KMP_MAX_PLAYERS);
        ImGui::Separator();

        if (ImGui::BeginTable("PlayerTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            std::lock_guard lock(m_mutex);
            for (auto& player : m_players) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                if (player.id == Core::Get().GetLocalPlayerId()) {
                    ImGui::TextColored(COL_GREEN, "%s", player.name.c_str());
                } else {
                    ImGui::Text("%s", player.name.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::Text("%dms", player.ping);

                ImGui::TableNextColumn();
                if (player.isHost) {
                    ImGui::TextColored(COL_YELLOW, "Host");
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════
//  CONNECTION UI (in-game, for disconnect)
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderConnectionUI() {
    ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_FirstUseEver);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Connection", &m_connectionOpen, ImGuiWindowFlags_NoCollapse)) {
        auto& core = Core::Get();

        if (core.IsConnected()) {
            ImGui::TextColored(COL_GREEN, "Connected to %s:%s", m_serverAddress, m_serverPort);
            ImGui::Text("Player: %s (ID: %d)", m_playerName, core.GetLocalPlayerId());
            ImGui::Text("Ping: %dms", core.GetClient().GetPing());
            ImGui::Separator();
            if (ImGui::Button("Disconnect", ImVec2(-1, 30))) {
                core.GetClient().Disconnect();
                core.SetConnected(false);
                m_connectionOpen = false;
                m_mainMenuOpen = true;
                m_menuPage = MenuPage::Main;
            }
        } else {
            ImGui::Text("Not connected.");
            if (ImGui::Button("Open Menu", ImVec2(-1, 30))) {
                m_connectionOpen = false;
                m_mainMenuOpen = true;
                m_menuPage = MenuPage::Main;
            }
        }
    }
    ImGui::End();
}

// Kept for compatibility but no longer primary — main menu replaces it
void Overlay::RenderServerBrowser() {
    // Redirect to main menu browser page
    m_serverBrowserOpen = false;
    m_mainMenuOpen = true;
    m_menuPage = MenuPage::ServerBrowser;
}

// ═══════════════════════════════════════════════════════════════
//  DEBUG OVERLAY
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderDebugOverlay() {
    ImGui::SetNextWindowSize(ImVec2(350, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &m_debugOpen)) {
        auto& core = Core::Get();
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::Text("Entities: %zu (remote: %zu)",
                    core.GetEntityRegistry().GetEntityCount(),
                    core.GetEntityRegistry().GetRemoteCount());
        ImGui::Text("Hooks: %zu", HookManager::Get().GetHookCount());
        ImGui::Text("Connected: %s", core.IsConnected() ? "Yes" : "No");
        if (core.IsConnected()) {
            ImGui::Text("Ping: %dms", core.GetClient().GetPing());
            ImGui::Text("Player ID: %d", core.GetLocalPlayerId());
        }
        ImGui::Separator();
        ImGui::Text("Scanner base: 0x%llX", core.GetScanner().GetBase());
        ImGui::Text("PlayerBase: 0x%llX", core.GetGameFunctions().PlayerBase);
        ImGui::Text("GameWorld: 0x%llX", core.GetGameFunctions().GameWorldSingleton);

        auto& funcs = core.GetGameFunctions();
        ImGui::Text("Resolved functions: %d", funcs.CountResolved());

        auto& offsets = game::GetOffsets().character;
        if (offsets.animClassOffset >= 0) {
            ImGui::Text("animClassOffset: 0x%X", offsets.animClassOffset);
        } else {
            ImGui::Text("animClassOffset: not found");
        }
        ImGui::Text("TimeHook: %s", core.IsTimeHookActive() ? "active" : "render fallback");
        ImGui::Text("SetPosition: %s", funcs.CharacterSetPosition ? "resolved" : "unavailable");
        ImGui::Text("MoveTo: %s", funcs.CharacterMoveTo ? "resolved" : "unavailable");
        ImGui::Text("SpawnManager: %s",
                    core.GetSpawnManager().IsReady() ? "ready" : "waiting");
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════
//  DATA MANAGEMENT
// ═══════════════════════════════════════════════════════════════

void Overlay::AddChatMessage(PlayerID sender, const std::string& message) {
    std::lock_guard lock(m_mutex);
    ChatEntry entry;
    entry.sender = sender;
    entry.message = message;
    entry.isSystem = false;
    entry.timestamp = m_uptime;

    for (auto& p : m_players) {
        if (p.id == sender) { entry.senderName = p.name; break; }
    }
    if (entry.senderName.empty()) entry.senderName = "Player " + std::to_string(sender);

    m_chatHistory.push_back(entry);
    while (m_chatHistory.size() > MAX_CHAT_HISTORY) m_chatHistory.pop_front();
    m_chatScrollToBottom = true;
}

void Overlay::AddSystemMessage(const std::string& message) {
    std::lock_guard lock(m_mutex);
    ChatEntry entry;
    entry.sender = 0;
    entry.senderName = "System";
    entry.message = message;
    entry.isSystem = true;
    entry.timestamp = m_uptime;

    m_chatHistory.push_back(entry);
    while (m_chatHistory.size() > MAX_CHAT_HISTORY) m_chatHistory.pop_front();
    m_chatScrollToBottom = true;
}

void Overlay::AddPlayer(const PlayerInfo& player) {
    std::lock_guard lock(m_mutex);
    for (auto& p : m_players) {
        if (p.id == player.id) { p = player; return; }
    }
    m_players.push_back(player);
}

void Overlay::RemovePlayer(PlayerID id) {
    std::lock_guard lock(m_mutex);
    m_players.erase(std::remove_if(m_players.begin(), m_players.end(),
        [id](const PlayerInfo& p) { return p.id == id; }), m_players.end());
}

void Overlay::UpdatePlayerPing(PlayerID id, uint32_t ping) {
    std::lock_guard lock(m_mutex);
    for (auto& p : m_players) {
        if (p.id == id) { p.ping = ping; return; }
    }
}

} // namespace kmp

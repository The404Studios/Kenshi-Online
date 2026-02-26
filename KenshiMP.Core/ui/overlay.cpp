#include "overlay.h"
#include "../core.h"
#include "kmp/protocol.h"
#include "kmp/constants.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace kmp {

void Overlay::Render() {
    m_uptime += ImGui::GetIO().DeltaTime;

    // Handle keybinds (process even when no panels are open)
    auto& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) TogglePlayerList();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !m_chatOpen) ToggleChat();
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) ToggleConnectionUI();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) ToggleServerBrowser();
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) m_debugOpen = !m_debugOpen;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) CloseAll();
    }

    RenderHUD();

    if (m_chatOpen) RenderChat();
    if (m_playerListOpen) RenderPlayerList();
    if (m_connectionOpen) RenderConnectionUI();
    if (m_serverBrowserOpen) RenderServerBrowser();
    if (m_debugOpen) RenderDebugOverlay();
}

void Overlay::Shutdown() {
    // Nothing to clean up
}

// ── HUD Bar ──
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
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "KENSHI-ONLINE");
            ImGui::SameLine();
            ImGui::Text("| %d/%d | %dms | %s:%d",
                       (int)m_players.size(), KMP_MAX_PLAYERS,
                       core.GetClient().GetPing(),
                       core.GetClient().GetServerAddress().c_str(),
                       core.GetClient().GetServerPort());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "KENSHI-ONLINE");
            ImGui::SameLine();
            ImGui::Text("| Offline | F1: Connect | F2: Server Browser");
        }
    }
    ImGui::End();
}

// ── Chat Window ──
void Overlay::RenderChat() {
    ImGui::SetNextWindowSize(ImVec2(500, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetMainViewport()->WorkSize.y - 260),
                           ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Chat", &m_chatOpen, ImGuiWindowFlags_NoCollapse)) {
        // Chat history
        float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("ChatScroll", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            std::lock_guard lock(m_mutex);
            for (auto& entry : m_chatHistory) {
                if (entry.isSystem) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[System] %s",
                                      entry.message.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[%s]",
                                      entry.senderName.c_str());
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

        // Input
        ImGui::Separator();
        bool sendMsg = false;
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("##ChatInput", m_chatInput, sizeof(m_chatInput),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            sendMsg = true;
        }
        // Auto-focus chat input when opened
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Send", ImVec2(50, 0))) sendMsg = true;

        if (sendMsg && m_chatInput[0] != '\0') {
            // Send chat message
            auto& core = Core::Get();
            if (core.IsConnected()) {
                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_ChatMessage);
                writer.WriteU32(core.GetLocalPlayerId());
                writer.WriteString(std::string(m_chatInput));
                core.GetClient().SendReliable(writer.Data(), writer.Size());
            }
            // Also show locally
            AddChatMessage(core.GetLocalPlayerId(), m_chatInput);
            m_chatInput[0] = '\0';
        }
    }
    ImGui::End();
}

// ── Player List ──
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

                // Highlight local player
                if (player.id == Core::Get().GetLocalPlayerId()) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", player.name.c_str());
                } else {
                    ImGui::Text("%s", player.name.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::Text("%dms", player.ping);

                ImGui::TableNextColumn();
                if (player.isHost) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Host");
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// ── Connection UI ──
void Overlay::RenderConnectionUI() {
    ImGui::SetNextWindowSize(ImVec2(350, 220), ImGuiCond_FirstUseEver);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Kenshi-Online Connect", &m_connectionOpen, ImGuiWindowFlags_NoCollapse)) {
        auto& core = Core::Get();

        if (core.IsConnected()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Connected to %s:%s",
                              m_serverAddress, m_serverPort);
            ImGui::Text("Player: %s (ID: %d)", m_playerName, core.GetLocalPlayerId());
            ImGui::Text("Ping: %dms", core.GetClient().GetPing());
            ImGui::Separator();
            if (ImGui::Button("Disconnect", ImVec2(-1, 30))) {
                core.GetClient().Disconnect();
                core.SetConnected(false);
            }
        } else {
            ImGui::Text("Player Name:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Name", m_playerName, sizeof(m_playerName));

            ImGui::Spacing();
            ImGui::Text("Server Address:");
            ImGui::SetNextItemWidth(-80);
            ImGui::InputText("##Address", m_serverAddress, sizeof(m_serverAddress));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Port", m_serverPort, sizeof(m_serverPort));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (m_connecting) {
                ImGui::Text("Connecting...");
            } else {
                if (ImGui::Button("Connect", ImVec2(-1, 35))) {
                    m_connecting = true;
                    uint16_t port = static_cast<uint16_t>(std::atoi(m_serverPort));
                    if (core.GetClient().Connect(m_serverAddress, port)) {
                        // Send handshake
                        MsgHandshake hs{};
                        hs.protocolVersion = KMP_PROTOCOL_VERSION;
                        strncpy(hs.playerName, m_playerName, KMP_MAX_NAME_LENGTH);

                        PacketWriter writer;
                        writer.WriteHeader(MessageType::C2S_Handshake);
                        writer.WriteRaw(&hs, sizeof(hs));
                        core.GetClient().SendReliable(writer.Data(), writer.Size());

                        // Save config
                        core.GetConfig().playerName = m_playerName;
                        core.GetConfig().lastServer = m_serverAddress;
                        core.GetConfig().lastPort = port;
                    }
                    m_connecting = false;
                }
            }
        }
    }
    ImGui::End();
}

// ── Server Browser ──
void Overlay::RenderServerBrowser() {
    ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Server Browser", &m_serverBrowserOpen, ImGuiWindowFlags_NoCollapse)) {
        // Toolbar
        if (ImGui::Button(m_refreshing ? "Refreshing..." : "Refresh")) {
            m_refreshing = true;
            // TODO: Query master server or LAN broadcast for servers
            // For now, add a placeholder
            m_serverList.clear();
            m_serverList.push_back({"Local Server", "127.0.0.1", KMP_DEFAULT_PORT, 1, 16, 5});
            m_serverList.push_back({"LAN Game", "192.168.1.100", KMP_DEFAULT_PORT, 3, 16, 12});
            m_refreshing = false;
        }
        ImGui::SameLine();
        ImGui::Text("Servers: %d", (int)m_serverList.size());

        ImGui::Separator();

        // Server list
        if (ImGui::BeginTable("ServerTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Server Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_serverList.size(); i++) {
                auto& server = m_serverList[i];
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", server.name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s:%d", server.address.c_str(), server.port);

                ImGui::TableNextColumn();
                if (server.players >= server.maxPlayers) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%d/%d",
                                      server.players, server.maxPlayers);
                } else {
                    ImGui::Text("%d/%d", server.players, server.maxPlayers);
                }

                ImGui::TableNextColumn();
                ImGui::Text("%dms", server.ping);

                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                if (server.players < server.maxPlayers) {
                    if (ImGui::SmallButton("Join")) {
                        strncpy(m_serverAddress, server.address.c_str(), sizeof(m_serverAddress) - 1);
                        snprintf(m_serverPort, sizeof(m_serverPort), "%d", server.port);
                        m_serverBrowserOpen = false;
                        m_connectionOpen = true;
                    }
                } else {
                    ImGui::TextDisabled("Full");
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Separator();

        // Direct connect
        ImGui::Text("Direct Connect:");
        ImGui::SameLine();
        static char directAddr[128] = "";
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##DirectAddr", directAddr, sizeof(directAddr));
        ImGui::SameLine();
        if (ImGui::Button("Connect##Direct")) {
            // Parse address:port
            std::string addr(directAddr);
            auto colonPos = addr.find(':');
            if (colonPos != std::string::npos) {
                strncpy(m_serverAddress, addr.substr(0, colonPos).c_str(), sizeof(m_serverAddress) - 1);
                snprintf(m_serverPort, sizeof(m_serverPort), "%s", addr.substr(colonPos + 1).c_str());
            } else {
                strncpy(m_serverAddress, directAddr, sizeof(m_serverAddress) - 1);
            }
            m_serverBrowserOpen = false;
            m_connectionOpen = true;
        }
    }
    ImGui::End();
}

// ── Debug Overlay ──
void Overlay::RenderDebugOverlay() {
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", &m_debugOpen)) {
        auto& core = Core::Get();
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Entities: %zu (remote: %zu)",
                    core.GetEntityRegistry().GetEntityCount(),
                    core.GetEntityRegistry().GetRemoteCount());
        ImGui::Text("Hooks: %zu", HookManager::Get().GetHookCount());
        ImGui::Text("Connected: %s", core.IsConnected() ? "Yes" : "No");
        if (core.IsConnected()) {
            ImGui::Text("Ping: %dms", core.GetClient().GetPing());
            ImGui::Text("Player ID: %d", core.GetLocalPlayerId());
        }
        ImGui::Text("Scanner base: 0x%llX", core.GetScanner().GetBase());
        ImGui::Text("PlayerBase: 0x%llX", core.GetGameFunctions().PlayerBase);
    }
    ImGui::End();
}

// ── Chat Management ──
void Overlay::AddChatMessage(PlayerID sender, const std::string& message) {
    std::lock_guard lock(m_mutex);
    ChatEntry entry;
    entry.sender = sender;
    entry.message = message;
    entry.isSystem = false;
    entry.timestamp = m_uptime;

    // Find sender name
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
    // Update or add
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

#include "native_hud.h"
#include "mygui_bridge.h"
#include "../core.h"
#include "../sys/command_registry.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <Windows.h>

namespace kmp {

bool NativeHud::Init() {
    if (m_initialized) return true;

    auto& bridge = MyGuiBridge::Get();
    if (!bridge.IsReady()) return false;

    bool layoutOk = bridge.LoadLayout("Kenshi_MultiplayerHUD.layout");

    if (layoutOk) {
        if (!CacheWidgets()) {
            spdlog::error("NativeHud: Layout loaded but failed to cache widgets");
            bridge.UnloadLayout("Kenshi_MultiplayerHUD.layout");
            layoutOk = false;
        }
    }

    if (!layoutOk) {
        spdlog::warn("NativeHud: Layout loading failed, trying programmatic widget creation");
        if (!CreateWidgetsFallback()) {
            spdlog::error("NativeHud: Programmatic creation also failed");
            return false;
        }
    }

    m_initialized = true;
    spdlog::info("NativeHud: Initialized with native MyGUI (layout={})", layoutOk ? "file" : "programmatic");
    return true;
}

bool NativeHud::CacheWidgets() {
    auto& bridge = MyGuiBridge::Get();

    m_root = bridge.FindWidget("HUDRoot");
    m_statusBar = bridge.FindWidget("HUDStatusBar");
    m_statusText = bridge.FindWidget("HUDStatusText");
    m_chatPanel = bridge.FindWidget("HUDChatPanel");
    m_chatInput = bridge.FindWidget("HUDChatInput");
    m_playerListPanel = bridge.FindWidget("HUDPlayerList");
    m_playerListTitle = bridge.FindWidget("HUDPlayerListTitle");

    // Log panel
    m_logPanel = bridge.FindWidget("HUDLogPanel");
    m_logTitle = bridge.FindWidget("HUDLogTitle");
    m_logHint = bridge.FindWidget("HUDLogHint");

    for (int i = 0; i < MAX_CHAT_LINES; i++) {
        m_chatLines[i] = bridge.FindWidget("HUDChat" + std::to_string(i));
    }
    for (int i = 0; i < MAX_PLAYER_ROWS; i++) {
        m_playerRows[i] = bridge.FindWidget("HUDPlayer" + std::to_string(i));
    }
    for (int i = 0; i < MAX_LOG_LINES; i++) {
        m_logLines[i] = bridge.FindWidget("HUDLog" + std::to_string(i));
    }

    int found = (m_root ? 1 : 0) + (m_statusBar ? 1 : 0) + (m_statusText ? 1 : 0)
              + (m_chatPanel ? 1 : 0) + (m_chatInput ? 1 : 0) + (m_playerListPanel ? 1 : 0) + (m_logPanel ? 1 : 0);
    for (int i = 0; i < MAX_CHAT_LINES; i++) if (m_chatLines[i]) found++;
    for (int i = 0; i < MAX_PLAYER_ROWS; i++) if (m_playerRows[i]) found++;
    for (int i = 0; i < MAX_LOG_LINES; i++) if (m_logLines[i]) found++;

    spdlog::info("NativeHud: Cached {} widgets (log={}, status={}, chatPanel={}, chat={})",
                 found, m_logPanel != nullptr, m_statusText != nullptr,
                 m_chatPanel != nullptr, m_chatLines[0] != nullptr);
    return m_root != nullptr;
}

bool NativeHud::CreateWidgetsFallback() {
    auto& bridge = MyGuiBridge::Get();

    // MyGUI Align values
    constexpr int ALIGN_DEFAULT     = 0;
    constexpr int ALIGN_HCENTER_TOP = 5;  // HCenter|Top
    constexpr int ALIGN_LEFT_BOTTOM = 24; // Left|Bottom
    constexpr int ALIGN_RIGHT_TOP   = 36; // Right|Top
    constexpr int ALIGN_HSTRETCH    = 48; // Left|Right (HStretch)
    constexpr int ALIGN_STRETCH     = 60; // HStretch|VStretch

    // Root — fullscreen non-interactive container
    m_root = bridge.CreateRootWidget("Widget", "PanelEmpty",
                                      0.f, 0.f, 1.f, 1.f, ALIGN_STRETCH, "Main", "HUDRoot");
    if (!m_root) {
        spdlog::error("NativeHud: Failed to create root widget");
        return false;
    }
    bridge.SetProperty(m_root, "NeedMouse", "false");
    bridge.SetProperty(m_root, "Visible", "false");

    // Status bar — top center
    m_statusBar = bridge.CreateChildWidget(m_root, "Widget", "PanelEmpty",
                                            0.25f, 0.003f, 0.5f, 0.035f, ALIGN_HCENTER_TOP, "HUDStatusBar");
    if (m_statusBar) {
        bridge.SetProperty(m_statusBar, "NeedMouse", "false");
    }

    m_statusText = bridge.CreateChildWidget(m_statusBar ? m_statusBar : m_root,
                                             "TextBox", "TextBox",
                                             0.02f, 0.05f, 0.96f, 0.9f, ALIGN_STRETCH, "HUDStatusText");
    if (m_statusText) {
        bridge.SetCaption(m_statusText, "KENSHI ONLINE  |  Not Connected  |  F1 = Menu");
        bridge.SetProperty(m_statusText, "TextAlign", "Center VCenter");
        bridge.SetProperty(m_statusText, "NeedMouse", "false");
    }

    // Log panel — right side
    m_logPanel = bridge.CreateChildWidget(m_root, "Widget", "PanelEmpty",
                                           0.6f, 0.05f, 0.39f, 0.55f, ALIGN_RIGHT_TOP, "HUDLogPanel");
    if (m_logPanel) {
        bridge.SetProperty(m_logPanel, "NeedMouse", "false");
    }

    m_logTitle = bridge.CreateChildWidget(m_logPanel ? m_logPanel : m_root,
                                           "TextBox", "TextBox",
                                           0.02f, 0.01f, 0.96f, 0.06f, ALIGN_HSTRETCH, "HUDLogTitle");
    if (m_logTitle) {
        bridge.SetCaption(m_logTitle, "KENSHI ONLINE");
        bridge.SetProperty(m_logTitle, "TextAlign", "Center");
        bridge.SetProperty(m_logTitle, "NeedMouse", "false");
    }

    // Log lines (20 lines)
    if (m_logPanel) {
        for (int i = 0; i < MAX_LOG_LINES; i++) {
            float y = 0.08f + i * 0.04f;
            std::string name = "HUDLog" + std::to_string(i);
            m_logLines[i] = bridge.CreateChildWidget(m_logPanel, "TextBox", "TextBox",
                                                      0.02f, y, 0.96f, 0.04f, ALIGN_HSTRETCH, name);
            if (m_logLines[i]) {
                bridge.SetProperty(m_logLines[i], "TextAlign", "Left VCenter");
                bridge.SetProperty(m_logLines[i], "NeedMouse", "false");
            }
        }
    }

    m_logHint = bridge.CreateChildWidget(m_logPanel ? m_logPanel : m_root,
                                          "TextBox", "TextBox",
                                          0.02f, 0.92f, 0.96f, 0.06f, ALIGN_HSTRETCH, "HUDLogHint");
    if (m_logHint) {
        bridge.SetCaption(m_logHint, "Insert = toggle  |  F1 = menu  |  Enter = chat");
        bridge.SetProperty(m_logHint, "TextAlign", "Center");
        bridge.SetProperty(m_logHint, "NeedMouse", "false");
    }

    // Chat panel — bottom left (semi-transparent background)
    m_chatPanel = bridge.CreateChildWidget(m_root, "Widget", "PanelEmpty",
                                            0.0f, 0.75f, 0.41f, 0.25f, ALIGN_LEFT_BOTTOM, "HUDChatPanel");
    if (m_chatPanel) {
        bridge.SetProperty(m_chatPanel, "NeedMouse", "false");
        bridge.SetProperty(m_chatPanel, "Visible", "false");
    }

    // Chat lines — inside chat panel
    void* chatParent = m_chatPanel ? m_chatPanel : m_root;
    for (int i = 0; i < MAX_CHAT_LINES; i++) {
        float y = 0.04f + i * 0.088f;
        std::string name = "HUDChat" + std::to_string(i);
        m_chatLines[i] = bridge.CreateChildWidget(chatParent, "TextBox", "TextBox",
                                                   0.012f, y, 0.976f, 0.088f, ALIGN_HSTRETCH, name);
        if (m_chatLines[i]) {
            bridge.SetProperty(m_chatLines[i], "TextAlign", "Left VCenter");
            bridge.SetProperty(m_chatLines[i], "NeedMouse", "false");
        }
    }

    // Chat input — inside chat panel
    m_chatInput = bridge.CreateChildWidget(chatParent, "TextBox", "TextBox",
                                            0.012f, 0.912f, 0.976f, 0.088f, ALIGN_HSTRETCH, "HUDChatInput");
    if (m_chatInput) {
        bridge.SetProperty(m_chatInput, "TextAlign", "Left VCenter");
        bridge.SetProperty(m_chatInput, "Visible", "false");
        bridge.SetProperty(m_chatInput, "NeedMouse", "false");
    }

    // Player list — top right
    m_playerListPanel = bridge.CreateChildWidget(m_root, "Widget", "PanelEmpty",
                                                  0.75f, 0.05f, 0.245f, 0.35f, ALIGN_RIGHT_TOP, "HUDPlayerList");
    if (m_playerListPanel) {
        bridge.SetProperty(m_playerListPanel, "Visible", "false");
        bridge.SetProperty(m_playerListPanel, "NeedMouse", "false");
    }

    m_playerListTitle = bridge.CreateChildWidget(m_playerListPanel ? m_playerListPanel : m_root,
                                                  "TextBox", "TextBox",
                                                  0.0f, 0.0f, 1.0f, 0.12f, ALIGN_HSTRETCH, "HUDPlayerListTitle");
    if (m_playerListTitle) {
        bridge.SetCaption(m_playerListTitle, "PLAYERS ONLINE");
        bridge.SetProperty(m_playerListTitle, "TextAlign", "Center");
        bridge.SetProperty(m_playerListTitle, "NeedMouse", "false");
    }

    for (int i = 0; i < MAX_PLAYER_ROWS; i++) {
        float y = 0.14f + i * 0.1f;
        std::string name = "HUDPlayer" + std::to_string(i);
        m_playerRows[i] = bridge.CreateChildWidget(m_playerListPanel ? m_playerListPanel : m_root,
                                                    "TextBox", "TextBox",
                                                    0.02f, y, 0.96f, 0.1f, ALIGN_HSTRETCH, name);
        if (m_playerRows[i]) {
            bridge.SetProperty(m_playerRows[i], "TextAlign", "Left VCenter");
            bridge.SetProperty(m_playerRows[i], "NeedMouse", "false");
        }
    }

    int found = (m_root ? 1 : 0) + (m_statusText ? 1 : 0) + (m_logPanel ? 1 : 0);
    for (int i = 0; i < MAX_CHAT_LINES; i++) if (m_chatLines[i]) found++;
    for (int i = 0; i < MAX_LOG_LINES; i++) if (m_logLines[i]) found++;
    spdlog::info("NativeHud: Programmatic fallback created {} widgets", found);
    return m_root != nullptr;
}

void NativeHud::Shutdown() {
    if (!m_initialized) return;
    Hide();
    MyGuiBridge::Get().UnloadLayout("Kenshi_MultiplayerHUD.layout");
    m_initialized = false;
}

void NativeHud::Show() {
    if (!m_initialized) return;
    MyGuiBridge::Get().SetVisible(m_root, true);
    m_visible = true;
}

void NativeHud::Hide() {
    if (!m_initialized) return;
    MyGuiBridge::Get().SetVisible(m_root, false);
    m_visible = false;
}

void NativeHud::Update() {
    auto& core = Core::Get();

    // Try to init as soon as MyGUI bridge is ready (works on main menu too)
    if (!m_initialized) {
        static int s_initAttempts = 0;
        static auto s_lastAttempt = std::chrono::steady_clock::time_point{};

        // Throttle retries: every 1s for first 10 attempts, every 10s after that
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastAttempt);
        int retryIntervalMs = (s_initAttempts < 10) ? 1000 : 10000;
        if (s_initAttempts > 0 && elapsed.count() < retryIntervalMs) return;

        auto& bridge = MyGuiBridge::Get();
        if (bridge.IsReady()) {
            s_lastAttempt = now;
            s_initAttempts++;
            if (!Init()) {
                if (s_initAttempts == 10) {
                    spdlog::warn("NativeHud: 10 init attempts failed — retrying every 10s");
                }
                return;
            }
            Show();
            // Show log panel by default
            if (m_logPanel) {
                MyGuiBridge::Get().SetVisible(m_logPanel, m_showLogPanel);
            }
        } else {
            return;
        }
    }

    if (!m_initialized || !m_visible) return;

    m_frameCounter++;

    // Update status bar every 15 frames (~4 times per second)
    if (m_frameCounter % 15 == 0) {
        UpdateStatusBar();
    }

    // Update other panels every 30 frames (~2 times per second)
    if (m_frameCounter % 30 == 0) {
        UpdateChatDisplay();
        UpdatePlayerList();
        UpdateLogPanel();
    }

    // Auto-hide log panel 15 seconds after game loads
    if (m_showLogPanel && !m_logAutoHideTriggered && core.IsGameLoaded()) {
        if (m_gameLoadedTime.time_since_epoch().count() == 0) {
            m_gameLoadedTime = std::chrono::steady_clock::now();
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_gameLoadedTime);
        if (elapsed.count() >= 15 && !core.IsConnected()) {
            // Only auto-hide if not connected (keep visible when things are happening)
            m_showLogPanel = false;
            m_logAutoHideTriggered = true;
            if (m_logPanel) {
                MyGuiBridge::Get().SetVisible(m_logPanel, false);
            }
        }
    }
}

void NativeHud::UpdateStatusBar() {
    if (!m_statusText) return;

    auto& core = Core::Get();
    auto& bridge = MyGuiBridge::Get();

    std::string status = "KENSHI ONLINE";

    if (core.IsConnected()) {
        auto& pc = core.GetPlayerController();
        auto remotePlayers = pc.GetAllRemotePlayers();

        status += "  |  CONNECTED as " + pc.GetLocalPlayerName();
        status += "  |  " + std::to_string(1 + remotePlayers.size()) + " player"
                + (remotePlayers.size() > 0 ? "s" : "") + " online";

        auto& er = core.GetEntityRegistry();
        size_t remoteCount = er.GetRemoteCount();
        if (remoteCount > 0) {
            status += "  |  " + std::to_string(remoteCount) + " remote";
        }

        // Append ping
        uint32_t ping = core.GetClient().GetPing();
        status += "  |  " + std::to_string(ping) + "ms";

        bridge.SetCaption(m_statusText, status);
    } else if (core.IsGameLoaded()) {
        status += "  |  NOT CONNECTED  |  F1 = Menu  |  Enter = Chat";
        bridge.SetCaption(m_statusText, status);
    } else {
        status += "  |  Loading...  |  F1 = Menu";
        bridge.SetCaption(m_statusText, status);
    }
}

void NativeHud::UpdateChatDisplay() {
    auto& bridge = MyGuiBridge::Get();
    std::lock_guard lock(m_chatMutex);

    // Prune old messages
    auto now = std::chrono::steady_clock::now();
    while (!m_chatHistory.empty()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_chatHistory.front().time).count();
        if (age > CHAT_FADE_SECONDS) {
            m_chatHistory.pop_front();
        } else {
            break;
        }
    }

    // Show/hide chat panel based on whether there are active messages or input is open
    bool hasContent = !m_chatHistory.empty() || m_chatInputActive;
    if (m_chatPanel) {
        bridge.SetVisible(m_chatPanel, hasContent);
    }

    // Fill chat lines (most recent at bottom)
    int msgCount = static_cast<int>(m_chatHistory.size());
    int startIdx = (msgCount > MAX_CHAT_LINES) ? (msgCount - MAX_CHAT_LINES) : 0;

    for (int i = 0; i < MAX_CHAT_LINES; i++) {
        if (!m_chatLines[i]) continue;
        int msgIdx = startIdx + i;
        if (msgIdx < msgCount) {
            auto& entry = m_chatHistory[msgIdx];
            std::string display = entry.timestamp + " " + entry.text;
            bridge.SetCaption(m_chatLines[i], display);
            // Color system messages amber, player chat white
            if (entry.isSystem) {
                bridge.SetProperty(m_chatLines[i], "TextColour", "0.9 0.55 0.1");
            } else {
                bridge.SetProperty(m_chatLines[i], "TextColour", "0.9 0.9 0.9");
            }
        } else {
            bridge.SetCaption(m_chatLines[i], "");
        }
    }
}

void NativeHud::UpdatePlayerList() {
    if (!m_playerListPanel) return;

    auto& bridge = MyGuiBridge::Get();
    auto& core = Core::Get();

    bridge.SetVisible(m_playerListPanel, m_showPlayerList);
    if (!m_showPlayerList) return;

    int row = 0;

    if (core.IsConnected()) {
        auto& pc = core.GetPlayerController();
        if (row < MAX_PLAYER_ROWS && m_playerRows[row]) {
            std::string localLabel = pc.GetLocalPlayerName() + " (You)";
            if (core.IsHost()) {
                localLabel += "  [HOST]";
            }
            bridge.SetCaption(m_playerRows[row], localLabel);
            row++;
        }

        auto remotePlayers = pc.GetAllRemotePlayers();
        for (auto& rp : remotePlayers) {
            if (row >= MAX_PLAYER_ROWS) break;
            if (!m_playerRows[row]) continue;
            std::string info = rp.playerName;
            if (rp.hasSpawnedCharacter) {
                info += "  [IN WORLD]";
            } else {
                info += "  [loading]";
            }
            bridge.SetCaption(m_playerRows[row], info);
            row++;
        }
    }

    for (int i = row; i < MAX_PLAYER_ROWS; i++) {
        if (m_playerRows[i]) bridge.SetCaption(m_playerRows[i], "");
    }
}

void NativeHud::UpdateLogPanel() {
    if (!m_logPanel) return;

    auto& bridge = MyGuiBridge::Get();
    bridge.SetVisible(m_logPanel, m_showLogPanel);
    if (!m_showLogPanel) return;

    std::lock_guard lock(m_logMutex);

    // Show the most recent MAX_LOG_LINES entries
    int entryCount = static_cast<int>(m_logEntries.size());
    int startIdx = (entryCount > MAX_LOG_LINES) ? (entryCount - MAX_LOG_LINES) : 0;

    for (int i = 0; i < MAX_LOG_LINES; i++) {
        if (!m_logLines[i]) continue;
        int entryIdx = startIdx + i;
        if (entryIdx < entryCount) {
            auto& entry = m_logEntries[entryIdx];
            std::string line = "[" + entry.tag + "] " + entry.message;
            bridge.SetCaption(m_logLines[i], line);
        } else {
            bridge.SetCaption(m_logLines[i], "");
        }
    }
}

void NativeHud::LogStep(const std::string& tag, const std::string& message) {
    std::lock_guard lock(m_logMutex);
    LogEntry entry;
    entry.tag = tag;
    entry.message = message;
    entry.time = std::chrono::steady_clock::now();
    m_logEntries.push_back(entry);
    if (m_logEntries.size() > MAX_LOG_ENTRIES) {
        m_logEntries.pop_front();
    }
    // NativeHud is now the sole logging pipeline — log directly here
    std::string logLine = "[" + tag + "] " + message;
    spdlog::info("NativeHud: {}", logLine);
    OutputDebugStringA(("KMP: " + logLine + "\n").c_str());
}

void NativeHud::TogglePlayerList() {
    m_showPlayerList = !m_showPlayerList;
    spdlog::info("NativeHud: Player list {}", m_showPlayerList ? "ON" : "OFF");
}

void NativeHud::ToggleLogPanel() {
    m_showLogPanel = !m_showLogPanel;
    if (m_logPanel) {
        MyGuiBridge::Get().SetVisible(m_logPanel, m_showLogPanel);
    }
    spdlog::info("NativeHud: Log panel {}", m_showLogPanel ? "ON" : "OFF");
}

static std::string MakeTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm local;
    localtime_s(&local, &tt);
    char buf[8];
    snprintf(buf, sizeof(buf), "[%02d:%02d]", local.tm_hour, local.tm_min);
    return buf;
}

void NativeHud::AddChatMessage(const std::string& sender, const std::string& message) {
    std::lock_guard lock(m_chatMutex);
    ChatEntry entry;
    entry.text = sender + ": " + message;
    entry.timestamp = MakeTimestamp();
    entry.isSystem = false;
    entry.time = std::chrono::steady_clock::now();
    m_chatHistory.push_back(entry);
    if (m_chatHistory.size() > MAX_CHAT_HISTORY) {
        m_chatHistory.pop_front();
    }
    spdlog::info("NativeHud: Chat: {}", entry.text);
}

void NativeHud::AddSystemMessage(const std::string& message) {
    std::lock_guard lock(m_chatMutex);
    ChatEntry entry;
    entry.text = "[System] " + message;
    entry.timestamp = MakeTimestamp();
    entry.isSystem = true;
    entry.time = std::chrono::steady_clock::now();
    m_chatHistory.push_back(entry);
    if (m_chatHistory.size() > MAX_CHAT_HISTORY) {
        m_chatHistory.pop_front();
    }
    spdlog::info("NativeHud: System: {}", message);
}

void NativeHud::OpenChatInput() {
    m_chatInputActive = true;
    m_chatInputText.clear();
    if (m_chatInput) {
        auto& bridge = MyGuiBridge::Get();
        bridge.SetVisible(m_chatInput, true);
        bridge.SetCaption(m_chatInput, "> _");
    }
}

void NativeHud::CloseChatInput() {
    m_chatInputActive = false;
    m_chatInputText.clear();
    if (m_chatInput) {
        auto& bridge = MyGuiBridge::Get();
        bridge.SetVisible(m_chatInput, false);
        bridge.SetCaption(m_chatInput, "");
    }
}

void NativeHud::OnChatChar(wchar_t ch) {
    if (!m_chatInputActive) return;
    if (ch >= 32 && ch < 127 && m_chatInputText.size() < 200) {
        m_chatInputText += static_cast<char>(ch);
        if (m_chatInput) {
            MyGuiBridge::Get().SetCaption(m_chatInput, "> " + m_chatInputText + "_");
        }
    }
}

void NativeHud::OnChatKeyDown(int vk) {
    if (!m_chatInputActive) return;

    if (vk == VK_BACK) {
        if (!m_chatInputText.empty()) {
            m_chatInputText.pop_back();
            if (m_chatInput) {
                MyGuiBridge::Get().SetCaption(m_chatInput, "> " + m_chatInputText + "_");
            }
        }
    } else if (vk == VK_RETURN) {
        if (!m_chatInputText.empty()) {
            SendChatMessage();
        }
        CloseChatInput();
    } else if (vk == VK_ESCAPE) {
        CloseChatInput();
    }
}

void NativeHud::SendChatMessage() {
    if (m_chatInputText.empty()) return;

    auto& core = Core::Get();

    // ── Handle slash commands via CommandRegistry ──
    if (m_chatInputText[0] == '/') {
        std::string result = CommandRegistry::Get().Execute(m_chatInputText);
        if (!result.empty()) {
            // Split multi-line results into individual system messages
            std::istringstream stream(result);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty()) {
                    AddSystemMessage(line);
                }
            }
        }
        return;
    }

    if (!core.IsConnected()) {
        AddSystemMessage("Not connected to a server.");
        return;
    }

    // Send chat to server (protocol: header + U32 senderId + U16 len + raw string)
    PacketWriter writer;
    writer.WriteHeader(MessageType::C2S_ChatMessage);
    writer.WriteU32(core.GetLocalPlayerId());
    writer.WriteString(m_chatInputText);
    core.GetClient().SendReliable(writer.Data(), writer.Size());

    // Show locally
    std::string localName = core.GetPlayerController().GetLocalPlayerName();
    AddChatMessage(localName, m_chatInputText);
}

} // namespace kmp

#include "UIRenderer.h"
#include "OgreOverlay.h"
#include "IPCClient.h"
#include <windows.h>

// NOTE: This is a stub implementation
// To fully implement this, you need a proper UI rendering system
// Options: ImGui, custom OGRE GUI, or HTML/CSS via embedded browser

namespace ReKenshi {

UIRenderer::UIRenderer()
    : m_overlay(nullptr)
    , m_ipcClient(nullptr)
    , m_currentScreen(UIScreen::NONE)
    , m_mouseX(0)
    , m_mouseY(0)
    , m_mouseLeftDown(false)
    , m_usernameFocused(false)
    , m_passwordFocused(false)
    , m_selectedServerIndex(-1)
    , m_autoConnect(false)
    , m_maxFPS(60)
{
}

UIRenderer::~UIRenderer() {
    Shutdown();
}

bool UIRenderer::Initialize(OgreOverlay* overlay, IPC::IPCClient* ipcClient) {
    m_overlay = overlay;
    m_ipcClient = ipcClient;

    // Set up IPC message callback
    if (m_ipcClient) {
        m_ipcClient->SetMessageCallback([this](const IPC::Message& msg) {
            // Handle messages from backend
            switch (msg.GetType()) {
            case IPC::MessageType::AUTH_RESPONSE: {
                auto response = IPC::MessageParser::ParseAuthResponse(msg);
                OnAuthResponse(response.success, response.token, response.error);
                break;
            }
            case IPC::MessageType::SERVER_LIST_RESPONSE: {
                auto response = IPC::MessageParser::ParseServerListResponse(msg);
                std::vector<ServerInfo> servers;
                for (const auto& s : response.servers) {
                    servers.push_back({s.id, s.name, s.playerCount, s.maxPlayers, s.ping, false});
                }
                OnServerListReceived(servers);
                break;
            }
            case IPC::MessageType::CONNECTION_STATUS: {
                auto status = IPC::MessageParser::ParseConnectionStatus(msg);
                OnConnectionStatus((int)status, msg.GetPayloadAsString());
                break;
            }
            }
        });
    }

    return true;
}

void UIRenderer::Shutdown() {
    m_overlay = nullptr;
    m_ipcClient = nullptr;
}

void UIRenderer::Render(float deltaTime) {
    switch (m_currentScreen) {
    case UIScreen::MAIN_MENU:
        RenderMainMenu(deltaTime);
        break;
    case UIScreen::SIGN_IN:
        RenderSignIn(deltaTime);
        break;
    case UIScreen::SERVER_BROWSER:
        RenderServerBrowser(deltaTime);
        break;
    case UIScreen::SETTINGS:
        RenderSettings(deltaTime);
        break;
    default:
        break;
    }
}

void UIRenderer::ShowScreen(UIScreen screen) {
    m_currentScreen = screen;

    // Request data for certain screens
    if (screen == UIScreen::SERVER_BROWSER && m_ipcClient) {
        auto msg = IPC::MessageBuilder::CreateServerListRequest();
        m_ipcClient->SendAsync(std::move(msg));
    }
}

void UIRenderer::HideScreen() {
    m_currentScreen = UIScreen::NONE;
}

void UIRenderer::OnMouseMove(int x, int y) {
    m_mouseX = x;
    m_mouseY = y;
}

void UIRenderer::OnMouseClick(int x, int y, bool leftButton) {
    m_mouseX = x;
    m_mouseY = y;
    m_mouseLeftDown = leftButton;
}

void UIRenderer::OnKeyPress(int vkCode) {
    // TODO: Handle keyboard input for text boxes
}

void UIRenderer::OnTextInput(char c) {
    // TODO: Add character to focused text box
}

void UIRenderer::OnAuthResponse(bool success, const std::string& token, const std::string& error) {
    if (success) {
        m_authError.clear();
        ShowScreen(UIScreen::MAIN_MENU);
        OutputDebugStringA("[ReKenshi] Authentication successful\n");
    } else {
        m_authError = error;
        OutputDebugStringA(("[ReKenshi] Authentication failed: " + error + "\n").c_str());
    }
}

void UIRenderer::OnServerListReceived(const std::vector<ServerInfo>& servers) {
    m_servers = servers;
    OutputDebugStringA(("[ReKenshi] Received " + std::to_string(servers.size()) + " servers\n").c_str());
}

void UIRenderer::OnConnectionStatus(int status, const std::string& message) {
    m_connectionStatus = message;
    OutputDebugStringA(("[ReKenshi] Connection status: " + message + "\n").c_str());
}

// Stub rendering implementations
void UIRenderer::RenderMainMenu(float deltaTime) {
    // TODO: Render main menu UI
    // For now, just output debug string once
    static bool once = false;
    if (!once) {
        OutputDebugStringA("[ReKenshi] Rendering main menu\n");
        once = true;
    }
}

void UIRenderer::RenderSignIn(float deltaTime) {
    // TODO: Render sign-in screen
    OutputDebugStringA("[ReKenshi] Rendering sign-in screen\n");
}

void UIRenderer::RenderServerBrowser(float deltaTime) {
    // TODO: Render server browser
    OutputDebugStringA("[ReKenshi] Rendering server browser\n");
}

void UIRenderer::RenderSettings(float deltaTime) {
    // TODO: Render settings screen
    OutputDebugStringA("[ReKenshi] Rendering settings screen\n");
}

// UI Element helpers (stubs)
bool UIRenderer::Button(const std::string& text, int x, int y, int width, int height) {
    // TODO: Render button and check if clicked
    return false;
}

void UIRenderer::TextBox(std::string& text, int x, int y, int width, int height, bool& focused) {
    // TODO: Render text box and handle input
}

void UIRenderer::Label(const std::string& text, int x, int y) {
    // TODO: Render text label
}

void UIRenderer::Panel(int x, int y, int width, int height) {
    // TODO: Render background panel
}

bool UIRenderer::IsMouseOver(int x, int y, int width, int height) const {
    return m_mouseX >= x && m_mouseX <= x + width &&
           m_mouseY >= y && m_mouseY <= y + height;
}

void UIRenderer::DrawRect(int x, int y, int width, int height, uint32_t color) {
    // TODO: Draw colored rectangle using OGRE
}

void UIRenderer::DrawText(const std::string& text, int x, int y, uint32_t color) {
    // TODO: Draw text using OGRE font
}

} // namespace ReKenshi

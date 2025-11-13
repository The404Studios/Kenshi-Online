#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace ReKenshi {

// Forward declarations
class OgreOverlay;
namespace IPC { class IPCClient; }

/**
 * UI Screen types
 */
enum class UIScreen {
    NONE,
    MAIN_MENU,
    SIGN_IN,
    SERVER_BROWSER,
    SETTINGS,
};

/**
 * Server info for browser
 */
struct ServerInfo {
    std::string id;
    std::string name;
    int playerCount;
    int maxPlayers;
    int ping;
    bool selected;
};

/**
 * UI Renderer - renders UI screens on top of OGRE overlay
 */
class UIRenderer {
public:
    UIRenderer();
    ~UIRenderer();

    // Initialization
    bool Initialize(OgreOverlay* overlay, IPC::IPCClient* ipcClient);
    void Shutdown();

    // Rendering
    void Render(float deltaTime);

    // Screen management
    void ShowScreen(UIScreen screen);
    void HideScreen();
    UIScreen GetCurrentScreen() const { return m_currentScreen; }

    // Input handling
    void OnMouseMove(int x, int y);
    void OnMouseClick(int x, int y, bool leftButton);
    void OnKeyPress(int vkCode);
    void OnTextInput(char c);

    // State updates from IPC
    void OnAuthResponse(bool success, const std::string& token, const std::string& error);
    void OnServerListReceived(const std::vector<ServerInfo>& servers);
    void OnConnectionStatus(int status, const std::string& message);

private:
    // Screen rendering functions
    void RenderMainMenu(float deltaTime);
    void RenderSignIn(float deltaTime);
    void RenderServerBrowser(float deltaTime);
    void RenderSettings(float deltaTime);

    // UI Elements (simple immediate-mode UI)
    bool Button(const std::string& text, int x, int y, int width, int height);
    void TextBox(std::string& text, int x, int y, int width, int height, bool& focused);
    void Label(const std::string& text, int x, int y);
    void Panel(int x, int y, int width, int height);

    // Helper functions
    bool IsMouseOver(int x, int y, int width, int height) const;
    void DrawRect(int x, int y, int width, int height, uint32_t color);
    void DrawText(const std::string& text, int x, int y, uint32_t color);

    // State
    OgreOverlay* m_overlay;
    IPC::IPCClient* m_ipcClient;
    UIScreen m_currentScreen;

    // Mouse state
    int m_mouseX;
    int m_mouseY;
    bool m_mouseLeftDown;

    // Sign-in screen state
    std::string m_username;
    std::string m_password;
    bool m_usernameFocused;
    bool m_passwordFocused;
    std::string m_authError;

    // Server browser state
    std::vector<ServerInfo> m_servers;
    int m_selectedServerIndex;
    std::string m_connectionStatus;

    // Settings state
    bool m_autoConnect;
    int m_maxFPS;
};

} // namespace ReKenshi

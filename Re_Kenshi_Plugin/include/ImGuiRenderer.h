#pragma once

#include <d3d11.h>
#include <memory>
#include <functional>

// Forward declare ImGui types
struct ImGuiContext;

namespace ReKenshi {
namespace UI {

// Forward declarations
class D3D11Hook;

/**
 * ImGui renderer for DirectX 11
 * Provides a clean interface for rendering UI using ImGui
 */
class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();

    // Initialization
    bool Initialize(HWND window, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // Frame management
    void BeginFrame();
    void EndFrame();
    void Render();

    // Input processing
    void ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // State
    bool IsInitialized() const { return m_initialized; }
    bool IsMouseCaptured() const;
    bool IsKeyboardCaptured() const;

private:
    bool m_initialized;
    ImGuiContext* m_imguiContext;

    // D3D11 resources
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    HWND m_window;
};

/**
 * UI Screen Manager - handles different UI screens
 */
enum class UIScreenType {
    None,
    MainMenu,
    SignIn,
    ServerBrowser,
    Settings,
    PlayerList,
    Chat,
};

class UIScreenManager {
public:
    UIScreenManager();
    ~UIScreenManager();

    void Initialize();
    void Shutdown();

    // Screen management
    void ShowScreen(UIScreenType screen);
    void HideScreen();
    UIScreenType GetCurrentScreen() const { return m_currentScreen; }

    // Rendering
    void Render();

    // Input
    void HandleInput();

private:
    // Screen rendering functions
    void RenderMainMenu();
    void RenderSignIn();
    void RenderServerBrowser();
    void RenderSettings();
    void RenderPlayerList();
    void RenderChat();

    // Helper UI elements
    void DrawHeader(const char* title);
    void DrawFooter();
    bool DrawButton(const char* label, float width = 200.0f);
    void DrawInputText(const char* label, char* buffer, size_t bufferSize);
    void DrawInputPassword(const char* label, char* buffer, size_t bufferSize);

    UIScreenType m_currentScreen;
    bool m_showOverlay;

    // UI State
    struct UIState {
        // Sign-in
        char username[64];
        char password[64];
        char authError[256];

        // Server browser
        int selectedServerIndex;
        char searchFilter[64];

        // Settings
        bool autoConnect;
        int targetFPS;
        float uiScale;

        // Chat
        char chatInput[256];
        bool chatFocused;

        UIState() {
            memset(username, 0, sizeof(username));
            memset(password, 0, sizeof(password));
            memset(authError, 0, sizeof(authError));
            memset(searchFilter, 0, sizeof(searchFilter));
            memset(chatInput, 0, sizeof(chatInput));

            selectedServerIndex = -1;
            autoConnect = false;
            targetFPS = 60;
            uiScale = 1.0f;
            chatFocused = false;
        }
    };

    UIState m_state;
};

} // namespace UI
} // namespace ReKenshi

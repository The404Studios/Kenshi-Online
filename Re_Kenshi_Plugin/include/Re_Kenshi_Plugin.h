#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <memory>

// Version information
#define REKENSHI_VERSION_MAJOR 1
#define REKENSHI_VERSION_MINOR 0
#define REKENSHI_VERSION_PATCH 0

namespace ReKenshi {

// Forward declarations
class OgreOverlay;
class InputHandler;
class UIRenderer;

namespace IPC {
    class IPCClient;
}

namespace Rendering {
    class D3D11Hook;
}

namespace UI {
    class ImGuiRenderer;
    class UIScreenManager;
}

/**
 * Main plugin class - manages the lifecycle of the Re_Kenshi plugin
 */
class Plugin {
public:
    static Plugin& GetInstance();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    void Update(float deltaTime);

    // Component access
    OgreOverlay* GetOverlay() { return m_overlay.get(); }
    InputHandler* GetInputHandler() { return m_inputHandler.get(); }
    IPC::IPCClient* GetIPCClient() { return m_ipcClient.get(); }
    UIRenderer* GetUIRenderer() { return m_uiRenderer.get(); }
    Rendering::D3D11Hook* GetD3D11Hook() { return m_d3d11Hook.get(); }
    UI::ImGuiRenderer* GetImGuiRenderer() { return m_imguiRenderer.get(); }
    UI::UIScreenManager* GetUIScreenManager() { return m_uiScreenManager.get(); }

    // State
    bool IsInitialized() const { return m_initialized; }
    bool IsOverlayVisible() const { return m_overlayVisible; }
    void SetOverlayVisible(bool visible);

private:
    Plugin() = default;
    ~Plugin() = default;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    // Helper functions
    bool InitializeGameStructures();
    void UpdateGameState(float deltaTime);
    void PrintDiagnostics();

    // State
    bool m_initialized = false;
    bool m_overlayVisible = false;

    // Components
    std::unique_ptr<OgreOverlay> m_overlay;
    std::unique_ptr<InputHandler> m_inputHandler;
    std::unique_ptr<IPC::IPCClient> m_ipcClient;
    std::unique_ptr<UIRenderer> m_uiRenderer;
    std::unique_ptr<Rendering::D3D11Hook> m_d3d11Hook;
    std::unique_ptr<UI::ImGuiRenderer> m_imguiRenderer;
    std::unique_ptr<UI::UIScreenManager> m_uiScreenManager;

    // Game structure pointers (found via pattern scanning)
    uintptr_t m_gameWorldPtr = 0;
    uintptr_t m_characterListPtr = 0;
    uintptr_t m_playerControllerPtr = 0;
};

} // namespace ReKenshi

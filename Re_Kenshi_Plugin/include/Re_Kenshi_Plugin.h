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
class IPCClient;
class UIRenderer;

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
    IPCClient* GetIPCClient() { return m_ipcClient.get(); }
    UIRenderer* GetUIRenderer() { return m_uiRenderer.get(); }

    // State
    bool IsInitialized() const { return m_initialized; }
    bool IsOverlayVisible() const { return m_overlayVisible; }
    void SetOverlayVisible(bool visible);

private:
    Plugin() = default;
    ~Plugin() = default;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    bool m_initialized = false;
    bool m_overlayVisible = false;

    std::unique_ptr<OgreOverlay> m_overlay;
    std::unique_ptr<InputHandler> m_inputHandler;
    std::unique_ptr<IPCClient> m_ipcClient;
    std::unique_ptr<UIRenderer> m_uiRenderer;
};

} // namespace ReKenshi

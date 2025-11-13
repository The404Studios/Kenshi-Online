#include "Re_Kenshi_Plugin.h"
#include <windows.h>
#include <thread>
#include <chrono>

using namespace ReKenshi;

// Global plugin instance
static std::thread g_updateThread;
static bool g_running = false;

/**
 * Main update loop for the plugin
 */
void PluginUpdateLoop() {
    auto& plugin = Plugin::GetInstance();

    const auto targetFrameTime = std::chrono::milliseconds(16); // ~60 FPS
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Update plugin
        plugin.Update(deltaTime);

        // Sleep to maintain frame rate
        std::this_thread::sleep_for(targetFrameTime);
    }
}

/**
 * DLL Entry Point
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        // Disable thread library calls for performance
        DisableThreadLibraryCalls(hModule);

        // Initialize plugin
        auto& plugin = Plugin::GetInstance();
        if (!plugin.Initialize()) {
            MessageBoxA(nullptr, "Failed to initialize Re_Kenshi Plugin", "Error", MB_OK | MB_ICONERROR);
            return FALSE;
        }

        // Start update thread
        g_running = true;
        g_updateThread = std::thread(PluginUpdateLoop);

        MessageBoxA(nullptr, "Re_Kenshi Plugin loaded successfully!\nPress F1 to open menu.",
                   "Re_Kenshi", MB_OK | MB_ICONINFORMATION);
        break;
    }

    case DLL_PROCESS_DETACH: {
        // Stop update thread
        g_running = false;
        if (g_updateThread.joinable()) {
            g_updateThread.join();
        }

        // Shutdown plugin
        auto& plugin = Plugin::GetInstance();
        plugin.Shutdown();
        break;
    }

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

namespace ReKenshi {

Plugin& Plugin::GetInstance() {
    static Plugin instance;
    return instance;
}

bool Plugin::Initialize() {
    if (m_initialized) {
        return true;
    }

    // Initialize IPC client first
    m_ipcClient = std::make_unique<IPCClient>();
    if (!m_ipcClient->Connect()) {
        MessageBoxA(nullptr, "Failed to connect to Kenshi Online backend.\nMake sure the service is running.",
                   "IPC Error", MB_OK | MB_ICONWARNING);
        // Continue anyway - we can retry connection later
    }

    // Initialize input handler
    m_inputHandler = std::make_unique<InputHandler>();
    HWND gameWindow = FindWindowA("OgreD3D11Wnd", nullptr); // Kenshi's window class
    if (!gameWindow) {
        gameWindow = GetForegroundWindow(); // Fallback to current window
    }

    if (!m_inputHandler->Initialize(gameWindow)) {
        return false;
    }

    // Set F1 callback to toggle overlay
    m_inputHandler->SetF1Callback([this]() {
        SetOverlayVisible(!m_overlayVisible);
    });

    // Initialize OGRE overlay
    m_overlay = std::make_unique<OgreOverlay>();
    if (!m_overlay->Initialize()) {
        MessageBoxA(nullptr, "Failed to initialize OGRE overlay.\nUI features may not work.",
                   "OGRE Error", MB_OK | MB_ICONWARNING);
        // Continue anyway - core features might still work
    }

    // Initialize UI renderer
    m_uiRenderer = std::make_unique<UIRenderer>();
    if (!m_uiRenderer->Initialize(m_overlay.get(), m_ipcClient.get())) {
        return false;
    }

    m_initialized = true;
    return true;
}

void Plugin::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_uiRenderer.reset();
    m_overlay.reset();
    m_inputHandler.reset();
    m_ipcClient.reset();

    m_initialized = false;
}

void Plugin::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update IPC client (process messages)
    if (m_ipcClient) {
        m_ipcClient->Update();
    }

    // Update input
    if (m_inputHandler) {
        m_inputHandler->Update();
    }

    // Update and render UI if visible
    if (m_overlayVisible && m_overlay && m_uiRenderer) {
        m_overlay->Render(deltaTime);
        m_uiRenderer->Render(deltaTime);
    }
}

void Plugin::SetOverlayVisible(bool visible) {
    m_overlayVisible = visible;

    if (m_overlay) {
        if (visible) {
            m_overlay->Show();
        } else {
            m_overlay->Hide();
        }
    }

    if (m_inputHandler) {
        m_inputHandler->SetCaptureInput(visible);
    }

    if (visible && m_uiRenderer) {
        m_uiRenderer->ShowScreen(UIScreen::MAIN_MENU);
    } else if (m_uiRenderer) {
        m_uiRenderer->HideScreen();
    }
}

} // namespace ReKenshi

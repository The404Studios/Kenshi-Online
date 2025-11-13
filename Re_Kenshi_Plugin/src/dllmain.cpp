#include "Re_Kenshi_Plugin.h"
#include "D3D11Hook.h"
#include "ImGuiRenderer.h"
#include "MemoryScanner.h"
#include "KenshiStructures.h"
#include "GameEventManager.h"
#include "MultiplayerSyncManager.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <sstream>

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

    OutputDebugStringA("[Re_Kenshi] Starting initialization...\n");

    // Phase 1: Memory scanning to find game structures
    OutputDebugStringA("[Re_Kenshi] Phase 1: Scanning for game structures...\n");
    if (!InitializeGameStructures()) {
        OutputDebugStringA("[Re_Kenshi] Warning: Failed to find some game structures\n");
        // Continue anyway - not critical for basic functionality
    }

    // Phase 2: Initialize IPC client
    OutputDebugStringA("[Re_Kenshi] Phase 2: Initializing IPC client...\n");
    m_ipcClient = std::make_unique<IPC::IPCClient>();
    if (!m_ipcClient->Connect()) {
        MessageBoxA(nullptr, "Failed to connect to Kenshi Online backend.\nMake sure the service is running.",
                   "IPC Error", MB_OK | MB_ICONWARNING);
        // Continue anyway - we can retry connection later
    }

    // Phase 3: Initialize input handler
    OutputDebugStringA("[Re_Kenshi] Phase 3: Initializing input handler...\n");
    m_inputHandler = std::make_unique<InputHandler>();
    HWND gameWindow = FindWindowA("OgreD3D11Wnd", nullptr); // Kenshi's window class
    if (!gameWindow) {
        gameWindow = GetForegroundWindow(); // Fallback to current window
    }

    if (!m_inputHandler->Initialize(gameWindow)) {
        OutputDebugStringA("[Re_Kenshi] ERROR: Failed to initialize input handler\n");
        return false;
    }

    // Set F1 callback to toggle overlay
    m_inputHandler->SetF1Callback([this]() {
        SetOverlayVisible(!m_overlayVisible);
    });

    // Phase 4: Initialize D3D11 hook for rendering
    OutputDebugStringA("[Re_Kenshi] Phase 4: Initializing D3D11 hook...\n");
    m_d3d11Hook = std::make_unique<Rendering::D3D11Hook>();
    if (!m_d3d11Hook->Initialize()) {
        OutputDebugStringA("[Re_Kenshi] Warning: Failed to initialize D3D11 hook\n");
        // Try OGRE fallback
        m_overlay = std::make_unique<OgreOverlay>();
        if (!m_overlay->Initialize()) {
            MessageBoxA(nullptr, "Failed to initialize rendering system.\nUI features disabled.",
                       "Rendering Error", MB_OK | MB_ICONWARNING);
        }
    } else {
        // Initialize ImGui renderer
        OutputDebugStringA("[Re_Kenshi] Initializing ImGui renderer...\n");
        m_imguiRenderer = std::make_unique<UI::ImGuiRenderer>();
        if (m_imguiRenderer->Initialize(
            m_d3d11Hook->GetWindowHandle(),
            m_d3d11Hook->GetDevice(),
            m_d3d11Hook->GetContext())) {

            // Set Present callback for rendering
            m_d3d11Hook->SetPresentCallback([this](IDXGISwapChain*, ID3D11Device*, ID3D11DeviceContext*) {
                if (m_overlayVisible && m_imguiRenderer) {
                    m_imguiRenderer->BeginFrame();
                    if (m_uiScreenManager) {
                        m_uiScreenManager->Render();
                    }
                    m_imguiRenderer->EndFrame();
                    m_imguiRenderer->Render();
                }
            });

            // Initialize UI screen manager
            m_uiScreenManager = std::make_unique<UI::UIScreenManager>();
            m_uiScreenManager->Initialize();
        }
    }

    // Phase 5: Initialize UI renderer (fallback if ImGui not available)
    if (!m_uiScreenManager) {
        OutputDebugStringA("[Re_Kenshi] Using legacy UI renderer...\n");
        m_uiRenderer = std::make_unique<UIRenderer>();
        if (!m_uiRenderer->Initialize(m_overlay.get(), m_ipcClient.get())) {
            OutputDebugStringA("[Re_Kenshi] Warning: UI renderer initialization failed\n");
        }
    }

    // Phase 6: Initialize game event manager
    OutputDebugStringA("[Re_Kenshi] Phase 6: Initializing game event manager...\n");
    m_eventManager = std::make_unique<Events::GameEventManager>();
    m_eventManager->Initialize(m_gameWorldPtr, m_characterListPtr, m_playerControllerPtr);

    // Phase 7: Initialize multiplayer sync manager
    OutputDebugStringA("[Re_Kenshi] Phase 7: Initializing multiplayer sync manager...\n");
    m_syncManager = std::make_unique<Multiplayer::MultiplayerSyncManager>();
    m_syncManager->Initialize(m_ipcClient.get(), m_eventManager.get(), m_playerControllerPtr);

    m_initialized = true;
    OutputDebugStringA("[Re_Kenshi] Initialization complete!\n");

    // Print diagnostic info
    PrintDiagnostics();

    return true;
}

bool Plugin::InitializeGameStructures() {
    // Scan for important game patterns
    std::ostringstream log;

    // Find game world
    auto pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::GAME_WORLD);
    uintptr_t gameWorldAddr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);
    if (gameWorldAddr) {
        m_gameWorldPtr = Memory::MemoryScanner::ResolveRelativeAddress(gameWorldAddr, 7);
        log << "[Re_Kenshi] Found Game World at: 0x" << std::hex << m_gameWorldPtr << std::endl;
    }

    // Find character list
    pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::CHARACTER_LIST);
    uintptr_t charListAddr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);
    if (charListAddr) {
        m_characterListPtr = Memory::MemoryScanner::ResolveRelativeAddress(charListAddr, 7);
        log << "[Re_Kenshi] Found Character List at: 0x" << std::hex << m_characterListPtr << std::endl;
    }

    // Find player controller
    pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::PLAYER_CONTROLLER);
    uintptr_t playerCtrlAddr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);
    if (playerCtrlAddr) {
        m_playerControllerPtr = Memory::MemoryScanner::ResolveRelativeAddress(playerCtrlAddr, 7);
        log << "[Re_Kenshi] Found Player Controller at: 0x" << std::hex << m_playerControllerPtr << std::endl;
    }

    std::string logStr = log.str();
    OutputDebugStringA(logStr.c_str());

    return (m_gameWorldPtr != 0) || (m_characterListPtr != 0);
}

void Plugin::PrintDiagnostics() {
    std::ostringstream log;
    log << "\n========== Re_Kenshi Diagnostics ==========\n";
    log << "IPC Client: " << (m_ipcClient && m_ipcClient->IsConnected() ? "Connected" : "Disconnected") << "\n";
    log << "Input Handler: " << (m_inputHandler ? "Initialized" : "Not initialized") << "\n";
    log << "D3D11 Hook: " << (m_d3d11Hook && m_d3d11Hook->IsInitialized() ? "Active" : "Inactive") << "\n";
    log << "ImGui Renderer: " << (m_imguiRenderer && m_imguiRenderer->IsInitialized() ? "Active" : "Inactive") << "\n";
    log << "OGRE Overlay: " << (m_overlay && m_overlay->IsVisible() ? "Active" : "Inactive") << "\n";
    log << "Event Manager: " << (m_eventManager && m_eventManager->IsInitialized() ? "Active" : "Inactive") << "\n";
    log << "Sync Manager: " << (m_syncManager && m_syncManager->IsInitialized() ? "Active" : "Inactive") << "\n";
    log << "Game World Ptr: 0x" << std::hex << m_gameWorldPtr << "\n";
    log << "Character List Ptr: 0x" << std::hex << m_characterListPtr << "\n";
    log << "Player Controller Ptr: 0x" << std::hex << m_playerControllerPtr << "\n";
    log << "==========================================\n\n";

    OutputDebugStringA(log.str().c_str());
}

void Plugin::Shutdown() {
    if (!m_initialized) {
        return;
    }

    OutputDebugStringA("[Re_Kenshi] Shutting down...\n");

    m_syncManager.reset();
    m_eventManager.reset();
    m_uiScreenManager.reset();
    m_uiRenderer.reset();
    m_imguiRenderer.reset();
    m_d3d11Hook.reset();
    m_overlay.reset();
    m_inputHandler.reset();
    m_ipcClient.reset();

    m_initialized = false;
    OutputDebugStringA("[Re_Kenshi] Shutdown complete\n");
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

    // Update game event manager (detects game events)
    if (m_eventManager) {
        m_eventManager->Update(deltaTime);
    }

    // Update multiplayer sync manager (syncs game state)
    if (m_syncManager) {
        m_syncManager->Update(deltaTime);
    }

    // Update game state reading (if needed for multiplayer)
    UpdateGameState(deltaTime);

    // Update and render UI if visible (legacy renderer)
    if (m_overlayVisible && m_overlay && m_uiRenderer) {
        m_overlay->Render(deltaTime);
        m_uiRenderer->Render(deltaTime);
    }

    // Note: ImGui rendering happens in D3D11 Present callback
}

void Plugin::UpdateGameState(float deltaTime) {
    // Read game state for multiplayer synchronization
    // This is called every frame, so keep it lightweight

    if (!m_ipcClient || !m_ipcClient->IsConnected()) {
        return;
    }

    // TODO: Read player position, health, etc. and send updates via IPC
    // Example:
    /*
    if (m_playerControllerPtr) {
        uintptr_t playerPtr = 0;
        if (Memory::MemoryScanner::ReadMemory(m_playerControllerPtr, playerPtr) && playerPtr) {
            Kenshi::CharacterData character;
            if (Kenshi::GameDataReader::ReadCharacter(playerPtr, character)) {
                // Send player update via IPC
                // m_ipcClient->SendAsync(...);
            }
        }
    }
    */
}

void Plugin::SetOverlayVisible(bool visible) {
    m_overlayVisible = visible;

    OutputDebugStringA(visible ? "[Re_Kenshi] Overlay shown\n" : "[Re_Kenshi] Overlay hidden\n");

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

    // Show/hide UI
    if (m_uiScreenManager) {
        if (visible) {
            m_uiScreenManager->ShowScreen(UI::UIScreenType::MainMenu);
        } else {
            m_uiScreenManager->HideScreen();
        }
    } else if (m_uiRenderer) {
        if (visible) {
            m_uiRenderer->ShowScreen(UIScreen::MAIN_MENU);
        } else {
            m_uiRenderer->HideScreen();
        }
    }
}

} // namespace ReKenshi

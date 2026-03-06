#include "render_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <d3d11.h>
#include <dxgi.h>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace kmp::render_hooks {

// Custom message for spawn queue processing — DEPRECATED.
// ProcessSpawnQueue() consumed the queue before the in-place replay (entity_hooks)
// could use it. The in-place replay is the ONLY safe spawn mechanism.
static constexpr UINT WM_KMP_SPAWN = WM_USER + 100;

// ── State ──
static HWND                  s_hwnd = nullptr;
static WNDPROC               s_originalWndProc = nullptr;

// ── Types ──
using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
static PresentFn s_originalPresent = nullptr;

// ── SEH wrapper for spawn queue processing from WndProc ──
// DISABLED: ProcessSpawnQueue() consumed requests before the in-place replay
// (entity_hooks) could use them. In-place replay is the only safe spawn mechanism.

// ── SEH wrapper for OnGameTick ──
static void SEH_OnGameTick(float dt) {
    __try {
        Core::Get().OnGameTick(dt);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        s_crashCount++;
        int lastStep = Core::Get().GetLastCompletedStep();
        DWORD code = GetExceptionCode();
        if (s_crashCount <= 10 || s_crashCount % 100 == 0) {
            char buf[256];
            sprintf_s(buf, "KMP: OnGameTick SEH crash #%d — exception 0x%08lX at step %d "
                           "(dt=%.4f)\n", s_crashCount, code, lastStep, dt);
            OutputDebugStringA(buf);
            spdlog::error("render_hooks: OnGameTick SEH crash #{} — exception 0x{:08X} "
                          "at step {} (dt={:.4f})", s_crashCount, code, lastStep, dt);
        }
    }
}

// ── MULTIPLAYER button bounds (from Kenshi_MainMenu.layout position_real) ──
static constexpr float MP_BTN_X = 0.260417f;
static constexpr float MP_BTN_Y = 0.582407f;  // Must match Kenshi_MainMenu.layout MultiplayerButton position
static constexpr float MP_BTN_W = 0.15625f;
static constexpr float MP_BTN_H = 0.0638889f;

// ── Startup timestamp: don't allow native menu until main menu is likely loaded ──
static auto s_firstPresentTime = std::chrono::steady_clock::time_point{};
static bool s_firstPresentRecorded = false;

static bool IsMainMenuReady() {
    // Don't allow native menu for the first 15 seconds after first Present.
    // The logo/splash screen runs during this time — MyGUI resources aren't loaded yet.
    if (!s_firstPresentRecorded) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_firstPresentTime);
    return elapsed.count() >= 15;
}

// ── WndProc Hook (pure Win32 input — no ImGui) ──
// Inner function does the actual work — called from SEH wrapper.
static LRESULT WndProcInner(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // WM_KMP_SPAWN: no longer used — spawn queue is handled by in-place replay only
    if (uMsg == WM_KMP_SPAWN) {
        return 0;
    }

    // F1 key: toggle native menu (ignore auto-repeat: bit 30 of lParam = previous key state)
    if (uMsg == WM_KEYDOWN && wParam == VK_F1 && !(lParam & 0x40000000)) {
        auto& overlay = Core::Get().GetOverlay();
        auto& nativeMenu = overlay.GetNativeMenu();
        if (nativeMenu.IsVisible()) {
            nativeMenu.Hide();
        } else if (!Core::Get().IsGameLoaded() && !IsMainMenuReady()) {
            OutputDebugStringA("KMP: F1 pressed too early (logo/splash) — ignoring\n");
        } else {
            // Works on main menu AND in-game
            nativeMenu.Show();
        }
        return 0; // consume the key
    }

    // Tab key: toggle player list on native HUD (ignore auto-repeat)
    if (uMsg == WM_KEYDOWN && wParam == VK_TAB && !(lParam & 0x40000000)) {
        if (Core::Get().IsGameLoaded() && Core::Get().IsConnected()) {
            Core::Get().GetNativeHud().TogglePlayerList();
            return 0; // consume — prevent Kenshi Tab action (inventory switch)
        }
    }

    // Insert key: toggle loading/debug log panel (native MyGUI)
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT && !(lParam & 0x40000000)) {
        Core::Get().GetNativeHud().ToggleLogPanel();
        return 0;
    }

    // Backtick key: toggle debug info on native HUD (ignore auto-repeat)
    if (uMsg == WM_KEYDOWN && wParam == VK_OEM_3 && !(lParam & 0x40000000)) {
        if (Core::Get().IsGameLoaded() && Core::Get().IsConnected()) {
            Core::Get().GetNativeHud().ToggleDebugInfo();
            return 0; // consume — prevent Kenshi console/debug action
        }
    }

    // Escape key: close chat input or native menu (ignore auto-repeat)
    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE && !(lParam & 0x40000000)) {
        auto& nativeHud = Core::Get().GetNativeHud();
        if (nativeHud.IsChatInputActive()) {
            nativeHud.CloseChatInput();
            return 0;
        }
        auto& nativeMenu = Core::Get().GetOverlay().GetNativeMenu();
        if (nativeMenu.IsVisible()) {
            nativeMenu.OnKeyDown(VK_ESCAPE);
            nativeMenu.Hide();
            return 0;
        }
    }

    // Enter key: toggle chat input (when game loaded, no menu open)
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN && !(lParam & 0x40000000)) {
        auto& nativeMenu = Core::Get().GetOverlay().GetNativeMenu();
        if (!nativeMenu.IsVisible() && Core::Get().IsGameLoaded()) {
            auto& nativeHud = Core::Get().GetNativeHud();
            if (nativeHud.IsChatInputActive()) {
                nativeHud.OnChatKeyDown(VK_RETURN);
            } else {
                nativeHud.OpenChatInput();
            }
            return 0;
        }
    }

    // ── Modal input gates ──
    // When chat or menu is active, consume ALL keyboard input to prevent
    // the game (OIS/MyGUI/DirectInput) from also processing keystrokes.
    // This fixes double-typing and prevents game actions while UI is open.
    bool chatActive = Core::Get().GetNativeHud().IsChatInputActive();
    bool menuVisible = Core::Get().GetOverlay().GetNativeMenu().IsVisible();

    // WM_CHAR: forward printable characters to active UI, always consume when modal
    if (uMsg == WM_CHAR) {
        if (chatActive) {
            Core::Get().GetNativeHud().OnChatChar(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (menuVisible) {
            auto& nativeMenu = Core::Get().GetOverlay().GetNativeMenu();
            if (nativeMenu.HasActiveEditBox()) {
                nativeMenu.OnChar(static_cast<wchar_t>(wParam));
            }
            return 0; // always consume when menu is visible (modal)
        }
    }

    // WM_KEYDOWN: forward control keys to active UI, always consume when modal
    if (uMsg == WM_KEYDOWN && wParam != VK_F1 && wParam != VK_ESCAPE) {
        if (chatActive) {
            if (wParam == VK_BACK || wParam == VK_RETURN) {
                Core::Get().GetNativeHud().OnChatKeyDown(static_cast<int>(wParam));
            }
            return 0; // consume ALL keydowns when chat is active
        }
        if (menuVisible) {
            auto& nativeMenu = Core::Get().GetOverlay().GetNativeMenu();
            if (wParam == VK_BACK || wParam == VK_RETURN || wParam == VK_TAB) {
                nativeMenu.OnKeyDown(static_cast<int>(wParam));
            }
            return 0; // consume ALL keydowns when menu is visible (modal)
        }
    }

    // WM_KEYUP: consume when chat or menu is active to prevent unpaired key-up
    // events reaching OIS (which would desync its internal key state tracking)
    if (uMsg == WM_KEYUP) {
        if (chatActive || menuVisible) return 0;
    }

    // Mouse click handling
    if (uMsg == WM_LBUTTONDOWN) {
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);

        auto& nativeMenu = Core::Get().GetOverlay().GetNativeMenu();

        if (nativeMenu.IsVisible()) {
            // Native panel is open — forward click to its handler
            nativeMenu.OnClick(mx, my);
        } else if (!Core::Get().IsGameLoaded() && IsMainMenuReady()) {
            // On main menu — check if click hit our MULTIPLAYER button
            RECT clientRect;
            if (GetClientRect(hWnd, &clientRect)) {
                float screenW = static_cast<float>(clientRect.right - clientRect.left);
                float screenH = static_cast<float>(clientRect.bottom - clientRect.top);

                if (screenW > 0 && screenH > 0) {
                    float nx = static_cast<float>(mx) / screenW;
                    float ny = static_cast<float>(my) / screenH;

                    if (nx >= MP_BTN_X && nx <= (MP_BTN_X + MP_BTN_W) &&
                        ny >= MP_BTN_Y && ny <= (MP_BTN_Y + MP_BTN_H)) {
                        spdlog::info("render_hooks: MULTIPLAYER button clicked ({}, {})", mx, my);
                        nativeMenu.Show();
                        return 0; // consume the click
                    }
                }
            }
        }
    }

    return CallWindowProcA(s_originalWndProc, hWnd, uMsg, wParam, lParam);
}

// SEH wrapper — a crash in our WndProc must not kill the game
static LRESULT CALLBACK HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    __try {
        return WndProcInner(hWnd, uMsg, wParam, lParam);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH CRASH in WndProc (msg=0x%X, wp=0x%llX)\n",
                      uMsg, (unsigned long long)wParam);
            OutputDebugStringA(buf);
        }
        return CallWindowProcA(s_originalWndProc, hWnd, uMsg, wParam, lParam);
    }
}

// ── Present Hook ──
// Passthrough: HWND discovery + WndProc hook + OnGameTick fallback.
// NO ImGui rendering — Ogre3D/DX11 conflict causes crash.
static std::chrono::steady_clock::time_point s_lastFrameTime{};
static bool s_hasLastFrameTime = false;

// ── SEH wrappers for per-frame calls ──
static void SEH_OverlayUpdate() {
    __try {
        Core::Get().GetOverlay().Update();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 5) OutputDebugStringA("KMP: SEH CRASH in Overlay::Update()\n");
    }
}

// All rendering via native MyGUI NativeHud — no GDI overlay.

static void SEH_NativeHudUpdate() {
    __try {
        Core::Get().GetNativeHud().Update();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 5) OutputDebugStringA("KMP: SEH CRASH in NativeHud::Update()\n");
    }
}

// No GDI overlay — NativeHud handles all display.

static HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    // Record first Present time for startup guard (logo/splash delay)
    if (!s_firstPresentRecorded) {
        s_firstPresentTime = std::chrono::steady_clock::now();
        s_firstPresentRecorded = true;
        OutputDebugStringA("KMP: First Present — recording startup time\n");
    }

    static int s_presentCount = 0;
    s_presentCount++;
    if (s_presentCount <= 3) {
        char buf[128];
        sprintf_s(buf, "KMP: HookPresent #%d\n", s_presentCount);
        OutputDebugStringA(buf);
    }

    // ── Periodic diagnostic: log loading state every ~2 seconds (300 frames) ──
    if (s_presentCount % 300 == 1) {
        auto& diag = Core::Get();
        spdlog::info("render_hooks: frame={} gameLoaded={} connected={} loading={}",
                     s_presentCount, diag.IsGameLoaded(), diag.IsConnected(), diag.IsLoading());
    }

    // One-time: grab HWND from the swap chain for WndProc hook
    if (!s_hwnd) {
        DXGI_SWAP_CHAIN_DESC desc;
        if (SUCCEEDED(swapChain->GetDesc(&desc))) {
            s_hwnd = desc.OutputWindow;
            OutputDebugStringA("KMP: Got HWND from swap chain\n");

            // Install WndProc hook for input (F1, mouse clicks, etc.)
            s_originalWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
            OutputDebugStringA("KMP: WndProc hook installed\n");

            // GDI overlay removed — native MyGUI HUD (NativeHud) handles all rendering
        }
    }

    // ── Per-frame overlay update (auto-connect, connection state, disconnect detect) ──
    // Each call is SEH-protected so a crash in one doesn't kill the game.
    {
        static bool s_overlayUpdateStarted = false;
        if (!s_overlayUpdateStarted) {
            OutputDebugStringA("KMP: HookPresent — starting per-frame Update() calls\n");
            s_overlayUpdateStarted = true;
        }
        SEH_OverlayUpdate();
        // NativeHud handles all display
        SEH_NativeHudUpdate();
    }

    // ── OnGameTick driver ──
    // Always drive OnGameTick from Present hook when connected.
    auto& core = Core::Get();
    if (core.IsConnected()) {
        static int s_connectedFrames = 0;
        s_connectedFrames++;

        // Log first 20 connected frames, then every 100th
        if (s_connectedFrames <= 20 || s_connectedFrames % 100 == 0) {
            spdlog::debug("render_hooks: Connected frame #{} (present #{})",
                          s_connectedFrames, s_presentCount);
        }

        auto now = std::chrono::steady_clock::now();
        if (s_hasLastFrameTime) {
            float dt = std::chrono::duration<float>(now - s_lastFrameTime).count();
            if (dt > 0.0f && dt < 0.5f) {
                SEH_OnGameTick(dt);
            }
        } else {
            OutputDebugStringA("KMP: First OnGameTick from Present hook\n");
            spdlog::info("render_hooks: First OnGameTick from Present hook (connected frame #{})",
                         s_connectedFrames);
        }
        s_lastFrameTime = now;
        s_hasLastFrameTime = true;
    }

    return s_originalPresent(swapChain, syncInterval, flags);
}

// ── Get DXGI VTable ──
// Create a temporary D3D11 device + swap chain to read the vtable
static bool GetDXGIVTable(void**& vtable) {
    WNDCLASSEXA wc = {sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0,
                     GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr,
                     "KMP_TEMP", nullptr};
    RegisterClassExA(&wc);
    HWND tempHwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW,
                                 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = tempHwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* tempDevice = nullptr;
    IDXGISwapChain* tempSwapChain = nullptr;
    ID3D11DeviceContext* tempContext = nullptr;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &scd, &tempSwapChain, &tempDevice, &featureLevel, &tempContext);

    if (FAILED(hr)) {
        DestroyWindow(tempHwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    vtable = *reinterpret_cast<void***>(tempSwapChain);

    tempSwapChain->Release();
    tempContext->Release();
    tempDevice->Release();
    DestroyWindow(tempHwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return true;
}

// ── Install/Uninstall ──

bool Install() {
    void** vtable = nullptr;
    if (!GetDXGIVTable(vtable)) {
        spdlog::error("render_hooks: Failed to get DXGI vtable");
        return false;
    }

    auto& hookMgr = HookManager::Get();

    // Present is vtable index 8
    if (!hookMgr.InstallAt("DXGI_Present",
                           reinterpret_cast<uintptr_t>(vtable[8]),
                           &HookPresent, &s_originalPresent)) {
        spdlog::error("render_hooks: Failed to hook Present");
        return false;
    }

    spdlog::info("render_hooks: Installed successfully (passthrough + WndProc)");
    return true;
}

void Uninstall() {
    if (s_originalWndProc && s_hwnd) {
        SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
    }

    HookManager::Get().Remove("DXGI_Present");
}

void PostSpawnTrigger() {
    if (s_hwnd) {
        PostMessageA(s_hwnd, WM_KMP_SPAWN, 0, 0);
    }
}

} // namespace kmp::render_hooks

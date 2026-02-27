#include "render_hooks.h"
#include "../core.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mutex>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace kmp::render_hooks {

// ── State ──
static bool                  s_initialized = false;
static ID3D11Device*         s_device = nullptr;
static ID3D11DeviceContext*  s_context = nullptr;
static ID3D11RenderTargetView* s_rtv = nullptr;
static HWND                  s_hwnd = nullptr;
static WNDPROC               s_originalWndProc = nullptr;
static std::mutex            s_renderMutex;

// ── Types ──
using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static PresentFn        s_originalPresent = nullptr;
static ResizeBuffersFn  s_originalResizeBuffers = nullptr;

// ── WndProc Hook ──
static LRESULT CALLBACK HookWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Always let ImGui see input events for its internal state
    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

    // Only block game input when our overlay is actively capturing
    if (ImGui::GetCurrentContext() && Core::Get().GetOverlay().IsInputCapture()) {
        // Block keyboard and mouse input from reaching the game
        if ((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) ||
            (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
            return true;
        }
    }

    return CallWindowProcA(s_originalWndProc, hWnd, uMsg, wParam, lParam);
}

// ── Initialize ImGui ──
static void InitImGui(IDXGISwapChain* swapChain) {
    if (s_initialized) return;

    HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_device));
    if (FAILED(hr)) {
        spdlog::error("render_hooks: Failed to get D3D11 device from swap chain");
        return;
    }

    s_device->GetImmediateContext(&s_context);

    // Get the back buffer for rendering
    ID3D11Texture2D* backBuffer = nullptr;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (SUCCEEDED(hr)) {
        s_device->CreateRenderTargetView(backBuffer, nullptr, &s_rtv);
        backBuffer->Release();
    }

    // Get HWND from swap chain
    DXGI_SWAP_CHAIN_DESC desc;
    swapChain->GetDesc(&desc);
    s_hwnd = desc.OutputWindow;

    // Hook WndProc for input
    s_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Don't save layout

    // Style
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.Alpha = 0.92f;

    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(s_device, s_context);

    s_initialized = true;
    spdlog::info("render_hooks: ImGui initialized successfully");
}

// ── Present Hook ──
// Used as a guaranteed OnGameTick fallback when time_hooks doesn't install.
static std::chrono::steady_clock::time_point s_lastFrameTime{};
static bool s_hasLastFrameTime = false;

static HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    std::lock_guard lock(s_renderMutex);

    if (!s_initialized) {
        InitImGui(swapChain);
    }

    if (s_initialized && s_rtv) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render our overlay
        Core::Get().GetOverlay().Render();

        ImGui::EndFrame();
        ImGui::Render();

        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // ── Guaranteed OnGameTick fallback ──
    // time_hooks calls OnGameTick when installed, but TIME_UPDATE pattern may be
    // null so the time hook may never install. Drive OnGameTick from the Present
    // hook (which always installs) so position sync/spawning/interpolation work.
    // Skip if time_hooks is active to avoid double-calling.
    auto& core = Core::Get();
    if (core.IsConnected() && !core.IsTimeHookActive()) {
        auto now = std::chrono::steady_clock::now();
        if (s_hasLastFrameTime) {
            float dt = std::chrono::duration<float>(now - s_lastFrameTime).count();
            // Clamp delta to avoid huge jumps (e.g. after alt-tab)
            if (dt > 0.0f && dt < 0.5f) {
                core.OnGameTick(dt);
            }
        }
        s_lastFrameTime = now;
        s_hasLastFrameTime = true;
    }

    return s_originalPresent(swapChain, syncInterval, flags);
}

// ── ResizeBuffers Hook ──
static HRESULT __stdcall HookResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount,
                                           UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    std::lock_guard lock(s_renderMutex);

    // Release render target before resize
    if (s_rtv) {
        s_rtv->Release();
        s_rtv = nullptr;
    }

    HRESULT hr = s_originalResizeBuffers(swapChain, bufferCount, width, height, format, flags);

    // Recreate render target
    if (SUCCEEDED(hr) && s_device) {
        ID3D11Texture2D* backBuffer = nullptr;
        hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        if (SUCCEEDED(hr)) {
            s_device->CreateRenderTargetView(backBuffer, nullptr, &s_rtv);
            backBuffer->Release();
        }
    }

    return hr;
}

// ── Get DXGI VTable ──
// Create a temporary D3D11 device + swap chain to read the vtable
static bool GetDXGIVTable(void**& vtable) {
    // Create a temporary hidden window
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

    // Read vtable from swap chain
    vtable = *reinterpret_cast<void***>(tempSwapChain);

    // Clean up temporary objects (vtable addresses remain valid as long as d3d11.dll is loaded)
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

    // ResizeBuffers is vtable index 13
    if (!hookMgr.InstallAt("DXGI_ResizeBuffers",
                           reinterpret_cast<uintptr_t>(vtable[13]),
                           &HookResizeBuffers, &s_originalResizeBuffers)) {
        spdlog::warn("render_hooks: Failed to hook ResizeBuffers (overlay may break on resize)");
    }

    spdlog::info("render_hooks: Installed successfully");
    return true;
}

void Uninstall() {
    std::lock_guard lock(s_renderMutex);

    if (s_initialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
        if (s_context) { s_context->Release(); s_context = nullptr; }
        if (s_device) { s_device->Release(); s_device = nullptr; }

        if (s_originalWndProc && s_hwnd) {
            SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
        }

        s_initialized = false;
    }

    HookManager::Get().Remove("DXGI_Present");
    HookManager::Get().Remove("DXGI_ResizeBuffers");
}

} // namespace kmp::render_hooks

#define _CRT_SECURE_NO_WARNINGS
#include "DirectX11Hook.h"
#include "ImGuiUI.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/backends/imgui_impl_win32.h"
#include <iostream>

namespace KenshiOnline
{
    // Static members
    DirectX11Hook::PresentFn DirectX11Hook::s_OriginalPresent = nullptr;

    // External reference to UI Manager
    extern UIManager* g_UIManager;

    DirectX11Hook::DirectX11Hook()
        : m_Device(nullptr)
        , m_Context(nullptr)
        , m_SwapChain(nullptr)
        , m_RenderTargetView(nullptr)
        , m_Initialized(false)
        , m_OriginalPresent(nullptr)
    {
    }

    DirectX11Hook::~DirectX11Hook()
    {
        Shutdown();
    }

    DirectX11Hook& DirectX11Hook::Get()
    {
        static DirectX11Hook instance;
        return instance;
    }

    bool DirectX11Hook::Initialize()
    {
        if (m_Initialized)
            return true;

        std::cout << "[DirectX11Hook] Initializing DirectX 11 hooks...\n";

        // Find the swap chain
        if (!FindSwapChain())
        {
            std::cout << "[DirectX11Hook] Failed to find swap chain!\n";
            return false;
        }

        std::cout << "[DirectX11Hook] Found swap chain!\n";

        // Get device and context from swap chain
        HRESULT hr = m_SwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&m_Device);
        if (FAILED(hr) || !m_Device)
        {
            std::cout << "[DirectX11Hook] Failed to get device!\n";
            return false;
        }

        m_Device->GetImmediateContext(&m_Context);
        if (!m_Context)
        {
            std::cout << "[DirectX11Hook] Failed to get context!\n";
            return false;
        }

        std::cout << "[DirectX11Hook] Got D3D11 device and context!\n";

        // Create render target view
        ID3D11Texture2D* backBuffer = nullptr;
        hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr))
        {
            std::cout << "[DirectX11Hook] Failed to get back buffer!\n";
            return false;
        }

        hr = m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);
        backBuffer->Release();

        if (FAILED(hr))
        {
            std::cout << "[DirectX11Hook] Failed to create render target view!\n";
            return false;
        }

        // Hook Present function
        if (!HookPresent())
        {
            std::cout << "[DirectX11Hook] Failed to hook Present!\n";
            return false;
        }

        std::cout << "[DirectX11Hook] Present hooked successfully!\n";

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        // Initialize ImGui backends
        HWND hwnd = FindWindowA(nullptr, "Kenshi");
        if (!hwnd)
        {
            std::cout << "[DirectX11Hook] Warning: Could not find Kenshi window\n";
        }

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(m_Device, m_Context);

        std::cout << "[DirectX11Hook] ImGui initialized!\n";

        m_Initialized = true;
        return true;
    }

    void DirectX11Hook::Shutdown()
    {
        if (!m_Initialized)
            return;

        UnhookPresent();

        // Shutdown ImGui
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (m_RenderTargetView)
        {
            m_RenderTargetView->Release();
            m_RenderTargetView = nullptr;
        }

        if (m_Context)
        {
            m_Context->Release();
            m_Context = nullptr;
        }

        if (m_Device)
        {
            m_Device->Release();
            m_Device = nullptr;
        }

        m_SwapChain = nullptr; // Don't release, we don't own it
        m_Initialized = false;
    }

    bool DirectX11Hook::FindSwapChain()
    {
        // Create a temporary D3D11 device to get vtable
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = GetForegroundWindow();
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* tempDevice = nullptr;
        IDXGISwapChain* tempSwapChain = nullptr;
        ID3D11DeviceContext* tempContext = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            &featureLevel,
            1,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &tempSwapChain,
            &tempDevice,
            nullptr,
            &tempContext
        );

        if (FAILED(hr))
        {
            std::cout << "[DirectX11Hook] Failed to create temp device\n";
            return false;
        }

        // Store the swap chain pointer (this is a placeholder - in real scenario we'd scan for Kenshi's)
        m_SwapChain = tempSwapChain;

        // Clean up temp objects
        if (tempContext) tempContext->Release();
        // Don't release tempDevice and tempSwapChain, we're using them

        return true;
    }

    bool DirectX11Hook::HookPresent()
    {
        if (!m_SwapChain)
            return false;

        // Get vtable
        void** vtable = *(void***)m_SwapChain;

        // Present is at index 8 in IDXGISwapChain vtable
        s_OriginalPresent = (PresentFn)vtable[8];

        // Hook it (simple vtable swap - not thread safe, use VEH/Detours in production)
        DWORD oldProtect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        vtable[8] = &PresentHook;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

        m_OriginalPresent = (void*)s_OriginalPresent;
        return true;
    }

    void DirectX11Hook::UnhookPresent()
    {
        if (!m_SwapChain || !s_OriginalPresent)
            return;

        // Restore original Present
        void** vtable = *(void***)m_SwapChain;
        DWORD oldProtect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        vtable[8] = s_OriginalPresent;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

        s_OriginalPresent = nullptr;
    }

    HRESULT STDMETHODCALLTYPE DirectX11Hook::PresentHook(
        IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags)
    {
        // Render ImGui
        if (g_UIManager)
        {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Render our UI
            g_UIManager->Render();

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        // Call original Present
        return s_OriginalPresent(swapChain, syncInterval, flags);
    }
}

// Note: ImGui backend implementations are now provided by official ImGui files
// imgui_impl_dx11.cpp and imgui_impl_win32.cpp
// These are downloaded and included via setup_imgui.bat

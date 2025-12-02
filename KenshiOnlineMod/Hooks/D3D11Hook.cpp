/*
 * D3D11Hook.cpp - DirectX 11 Hooking Implementation
 */

#include "D3D11Hook.h"
#include <MinHook.h>
#include <iostream>

namespace KenshiOnline
{
    // Static member initialization
    D3D11Hook::PresentFn D3D11Hook::s_OriginalPresent = nullptr;
    D3D11Hook::ResizeBuffersFn D3D11Hook::s_OriginalResizeBuffers = nullptr;

    D3D11Hook& D3D11Hook::Get()
    {
        static D3D11Hook instance;
        return instance;
    }

    bool D3D11Hook::Initialize()
    {
        if (m_Initialized)
            return true;

        std::cout << "[D3D11Hook] Initializing DirectX 11 hooks...\n";

        // Initialize MinHook
        if (MH_Initialize() != MH_OK)
        {
            std::cout << "[D3D11Hook] ERROR: Failed to initialize MinHook\n";
            return false;
        }

        // Create a temporary D3D11 device to get vtable addresses
        D3D_FEATURE_LEVEL featureLevel;
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferDesc.Width = 100;
        swapChainDesc.BufferDesc.Height = 100;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = GetDesktopWindow();
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* tempSwapChain = nullptr;
        ID3D11Device* tempDevice = nullptr;
        ID3D11DeviceContext* tempContext = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &tempSwapChain,
            &tempDevice,
            &featureLevel,
            &tempContext
        );

        if (FAILED(hr))
        {
            std::cout << "[D3D11Hook] ERROR: Failed to create temp D3D11 device (0x" << std::hex << hr << ")\n";
            return false;
        }

        // Get vtable addresses
        void** swapChainVTable = *reinterpret_cast<void***>(tempSwapChain);
        void* presentAddr = swapChainVTable[8];   // Present is at index 8
        void* resizeAddr = swapChainVTable[13];   // ResizeBuffers is at index 13

        std::cout << "[D3D11Hook] Present address: 0x" << std::hex << (uintptr_t)presentAddr << "\n";
        std::cout << "[D3D11Hook] ResizeBuffers address: 0x" << std::hex << (uintptr_t)resizeAddr << "\n";

        // Clean up temp objects
        tempSwapChain->Release();
        tempDevice->Release();
        tempContext->Release();

        // Hook Present
        if (MH_CreateHook(presentAddr, &HookedPresent, reinterpret_cast<void**>(&s_OriginalPresent)) != MH_OK)
        {
            std::cout << "[D3D11Hook] ERROR: Failed to create Present hook\n";
            return false;
        }

        if (MH_EnableHook(presentAddr) != MH_OK)
        {
            std::cout << "[D3D11Hook] ERROR: Failed to enable Present hook\n";
            return false;
        }

        // Hook ResizeBuffers
        if (MH_CreateHook(resizeAddr, &HookedResizeBuffers, reinterpret_cast<void**>(&s_OriginalResizeBuffers)) != MH_OK)
        {
            std::cout << "[D3D11Hook] ERROR: Failed to create ResizeBuffers hook\n";
            return false;
        }

        if (MH_EnableHook(resizeAddr) != MH_OK)
        {
            std::cout << "[D3D11Hook] ERROR: Failed to enable ResizeBuffers hook\n";
            return false;
        }

        m_Initialized = true;
        std::cout << "[D3D11Hook] DirectX 11 hooks installed successfully!\n";
        return true;
    }

    void D3D11Hook::Shutdown()
    {
        if (!m_Initialized)
            return;

        std::cout << "[D3D11Hook] Shutting down...\n";

        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        CleanupRenderTarget();

        m_Device = nullptr;
        m_Context = nullptr;
        m_SwapChain = nullptr;
        m_Initialized = false;
    }

    void D3D11Hook::CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);
            backBuffer->Release();
        }
    }

    void D3D11Hook::CleanupRenderTarget()
    {
        if (m_RenderTargetView)
        {
            m_RenderTargetView->Release();
            m_RenderTargetView = nullptr;
        }
    }

    HRESULT WINAPI D3D11Hook::HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        auto& hook = Get();

        if (hook.m_FirstFrame)
        {
            // First frame - initialize everything
            hook.m_SwapChain = pSwapChain;
            pSwapChain->GetDevice(IID_PPV_ARGS(&hook.m_Device));
            hook.m_Device->GetImmediateContext(&hook.m_Context);

            // Get game window
            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            hook.m_GameWindow = desc.OutputWindow;

            hook.CreateRenderTarget();
            hook.m_FirstFrame = false;

            std::cout << "[D3D11Hook] First frame initialized - Window: 0x" << std::hex << (uintptr_t)hook.m_GameWindow << "\n";
        }

        // Call render callback
        if (hook.m_RenderCallback)
        {
            hook.m_Context->OMSetRenderTargets(1, &hook.m_RenderTargetView, nullptr);
            hook.m_RenderCallback();
        }

        return s_OriginalPresent(pSwapChain, SyncInterval, Flags);
    }

    HRESULT WINAPI D3D11Hook::HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
        UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
    {
        auto& hook = Get();

        // Clean up render target before resize
        hook.CleanupRenderTarget();

        // Call original
        HRESULT hr = s_OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        // Recreate render target
        hook.CreateRenderTarget();

        // Notify callback
        if (hook.m_ResizeCallback)
        {
            hook.m_ResizeCallback(Width, Height);
        }

        return hr;
    }
}

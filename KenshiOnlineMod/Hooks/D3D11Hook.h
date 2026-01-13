/*
 * D3D11Hook.h - DirectX 11 Hooking for Kenshi Online Overlay
 * Hooks the Present function to render ImGui overlay
 */

#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <functional>

namespace KenshiOnline
{
    class D3D11Hook
    {
    public:
        using RenderCallback = std::function<void()>;
        using ResizeCallback = std::function<void(UINT, UINT)>;

        static D3D11Hook& Get();

        bool Initialize();
        void Shutdown();

        void SetRenderCallback(RenderCallback callback) { m_RenderCallback = callback; }
        void SetResizeCallback(ResizeCallback callback) { m_ResizeCallback = callback; }

        ID3D11Device* GetDevice() const { return m_Device; }
        ID3D11DeviceContext* GetContext() const { return m_Context; }
        IDXGISwapChain* GetSwapChain() const { return m_SwapChain; }
        ID3D11RenderTargetView* GetRenderTarget() const { return m_RenderTargetView; }

        bool IsInitialized() const { return m_Initialized; }
        HWND GetGameWindow() const { return m_GameWindow; }

    private:
        D3D11Hook() = default;
        ~D3D11Hook() = default;
        D3D11Hook(const D3D11Hook&) = delete;
        D3D11Hook& operator=(const D3D11Hook&) = delete;

        bool HookPresent();
        bool HookResizeBuffers();
        void CreateRenderTarget();
        void CleanupRenderTarget();

        // Hook trampolines
        static HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT WINAPI HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
            UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

        // Function pointers
        using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

        static PresentFn s_OriginalPresent;
        static ResizeBuffersFn s_OriginalResizeBuffers;

        ID3D11Device* m_Device = nullptr;
        ID3D11DeviceContext* m_Context = nullptr;
        IDXGISwapChain* m_SwapChain = nullptr;
        ID3D11RenderTargetView* m_RenderTargetView = nullptr;
        HWND m_GameWindow = nullptr;

        RenderCallback m_RenderCallback;
        ResizeCallback m_ResizeCallback;

        bool m_Initialized = false;
        bool m_FirstFrame = true;
    };
}

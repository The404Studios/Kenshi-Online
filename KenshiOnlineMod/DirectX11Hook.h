#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>

namespace KenshiOnline
{
    class DirectX11Hook
    {
    public:
        static DirectX11Hook& Get();

        bool Initialize();
        void Shutdown();

        // Get D3D11 device and context
        ID3D11Device* GetDevice() const { return m_Device; }
        ID3D11DeviceContext* GetContext() const { return m_Context; }
        IDXGISwapChain* GetSwapChain() const { return m_SwapChain; }

        // Hook status
        bool IsInitialized() const { return m_Initialized; }

    private:
        DirectX11Hook();
        ~DirectX11Hook();

        // Prevent copying
        DirectX11Hook(const DirectX11Hook&) = delete;
        DirectX11Hook& operator=(const DirectX11Hook&) = delete;

        bool FindSwapChain();
        bool HookPresent();
        void UnhookPresent();

        // D3D11 objects
        ID3D11Device* m_Device;
        ID3D11DeviceContext* m_Context;
        IDXGISwapChain* m_SwapChain;
        ID3D11RenderTargetView* m_RenderTargetView;

        // Hook state
        bool m_Initialized;
        void* m_OriginalPresent;

        // Present hook
        static HRESULT STDMETHODCALLTYPE PresentHook(
            IDXGISwapChain* swapChain,
            UINT syncInterval,
            UINT flags
        );

        // Original Present function pointer
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        static PresentFn s_OriginalPresent;
    };
}

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <functional>
#include <memory>

namespace ReKenshi {
namespace Rendering {

/**
 * Direct3D 11 hook for rendering overlays
 * Hooks into the Present function to render ImGui
 */
class D3D11Hook {
public:
    using PresentCallback = std::function<void(IDXGISwapChain*, ID3D11Device*, ID3D11DeviceContext*)>;

    D3D11Hook();
    ~D3D11Hook();

    // Initialize and install hooks
    bool Initialize();
    void Shutdown();

    // Callbacks
    void SetPresentCallback(PresentCallback callback) { m_presentCallback = callback; }

    // Accessors
    IDXGISwapChain* GetSwapChain() const { return m_swapChain; }
    ID3D11Device* GetDevice() const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }
    HWND GetWindowHandle() const { return m_windowHandle; }

    bool IsInitialized() const { return m_initialized; }

private:
    // Hook methods
    bool FindD3D11Device();
    bool HookPresent();
    void UnhookPresent();

    // Static hook function
    static HRESULT STDMETHODCALLTYPE PresentHook(
        IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags
    );

    // D3D11 objects
    IDXGISwapChain* m_swapChain;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    ID3D11RenderTargetView* m_renderTargetView;
    HWND m_windowHandle;

    // Hook state
    bool m_initialized;
    void* m_originalPresent;
    PresentCallback m_presentCallback;

    // Singleton for hook callback
    static D3D11Hook* s_instance;
};

} // namespace Rendering
} // namespace ReKenshi

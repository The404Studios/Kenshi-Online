#include "D3D11Hook.h"
#include "MemoryScanner.h"
#include <iostream>

// MinHook library for function hooking (you'll need to add this dependency)
// For now, we'll use manual detour method
namespace ReKenshi {
namespace Rendering {

D3D11Hook* D3D11Hook::s_instance = nullptr;

D3D11Hook::D3D11Hook()
    : m_swapChain(nullptr)
    , m_device(nullptr)
    , m_context(nullptr)
    , m_renderTargetView(nullptr)
    , m_windowHandle(nullptr)
    , m_initialized(false)
    , m_originalPresent(nullptr)
{
    s_instance = this;
}

D3D11Hook::~D3D11Hook() {
    Shutdown();
    s_instance = nullptr;
}

bool D3D11Hook::Initialize() {
    if (m_initialized) {
        return true;
    }

    OutputDebugStringA("[D3D11Hook] Initializing...\n");

    // Find D3D11 device
    if (!FindD3D11Device()) {
        OutputDebugStringA("[D3D11Hook] Failed to find D3D11 device\n");
        return false;
    }

    // Hook Present function
    if (!HookPresent()) {
        OutputDebugStringA("[D3D11Hook] Failed to hook Present\n");
        return false;
    }

    m_initialized = true;
    OutputDebugStringA("[D3D11Hook] Initialized successfully\n");
    return true;
}

void D3D11Hook::Shutdown() {
    if (!m_initialized) {
        return;
    }

    UnhookPresent();

    if (m_renderTargetView) {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }

    // Don't release device/context/swapchain - they're owned by the game

    m_initialized = false;
}

bool D3D11Hook::FindD3D11Device() {
    // Method 1: Pattern scan for D3D11 device pointer
    auto pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::D3D11_DEVICE);
    uintptr_t devicePatternAddr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);

    if (devicePatternAddr) {
        // Resolve RIP-relative address
        uintptr_t devicePtrAddr = Memory::MemoryScanner::ResolveRelativeAddress(devicePatternAddr + 2, 7);

        if (devicePtrAddr) {
            Memory::MemoryScanner::ReadMemory(devicePtrAddr, m_device);
        }
    }

    // Method 2: If pattern scan fails, try to find swap chain and get device from it
    if (!m_device) {
        pattern = Memory::MemoryScanner::ParsePattern(Memory::KenshiPatterns::DXGI_SWAPCHAIN);
        uintptr_t swapChainPatternAddr = Memory::MemoryScanner::FindPattern("kenshi_x64.exe", pattern);

        if (swapChainPatternAddr) {
            uintptr_t swapChainPtrAddr = Memory::MemoryScanner::ResolveRelativeAddress(swapChainPatternAddr, 7);

            if (swapChainPtrAddr) {
                Memory::MemoryScanner::ReadMemory(swapChainPtrAddr, m_swapChain);

                if (m_swapChain) {
                    m_swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&m_device);
                }
            }
        }
    }

    // Method 3: Brute force search in memory (last resort)
    if (!m_device) {
        OutputDebugStringA("[D3D11Hook] Warning: Using brute force device search\n");

        // Create temporary D3D11 device to get vtable
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        IDXGISwapChain* tempSwapChain = nullptr;
        ID3D11Device* tempDevice = nullptr;
        ID3D11DeviceContext* tempContext = nullptr;

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = GetForegroundWindow();
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(
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
            &tempContext))) {

            // Get vtable addresses
            void** swapChainVTable = *reinterpret_cast<void***>(tempSwapChain);

            tempContext->Release();
            tempDevice->Release();
            tempSwapChain->Release();

            // Now scan for these vtable pointers in memory
            // This would require more complex scanning logic
        }
    }

    if (!m_device) {
        return false;
    }

    // Get context
    m_device->GetImmediateContext(&m_context);

    // Find window handle
    DXGI_SWAP_CHAIN_DESC swapDesc;
    if (m_swapChain && SUCCEEDED(m_swapChain->GetDesc(&swapDesc))) {
        m_windowHandle = swapDesc.OutputWindow;
    } else {
        m_windowHandle = FindWindowA("OgreD3D11Wnd", nullptr);
    }

    return true;
}

bool D3D11Hook::HookPresent() {
    if (!m_swapChain) {
        return false;
    }

    // Get Present function address from vtable
    void** vTable = *reinterpret_cast<void***>(m_swapChain);
    void* presentFunc = vTable[8];  // Present is at index 8 in IDXGISwapChain vtable

    // Install detour hook
    // NOTE: This requires MinHook or similar hooking library
    // For now, this is a placeholder

    /*
    if (MH_CreateHook(presentFunc, &PresentHook, &m_originalPresent) != MH_OK) {
        return false;
    }

    if (MH_EnableHook(presentFunc) != MH_OK) {
        return false;
    }
    */

    m_originalPresent = presentFunc;

    // Manual VTable hooking (more dangerous but doesn't require external library)
    DWORD oldProtect;
    VirtualProtect(&vTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    vTable[8] = &PresentHook;
    VirtualProtect(&vTable[8], sizeof(void*), oldProtect, &oldProtect);

    return true;
}

void D3D11Hook::UnhookPresent() {
    if (!m_swapChain || !m_originalPresent) {
        return;
    }

    // Restore original function
    void** vTable = *reinterpret_cast<void***>(m_swapChain);

    DWORD oldProtect;
    VirtualProtect(&vTable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    vTable[8] = m_originalPresent;
    VirtualProtect(&vTable[8], sizeof(void*), oldProtect, &oldProtect);

    m_originalPresent = nullptr;
}

HRESULT STDMETHODCALLTYPE D3D11Hook::PresentHook(
    IDXGISwapChain* swapChain,
    UINT syncInterval,
    UINT flags)
{
    // Call user callback for rendering
    if (s_instance && s_instance->m_presentCallback) {
        s_instance->m_presentCallback(
            swapChain,
            s_instance->m_device,
            s_instance->m_context
        );
    }

    // Call original Present
    typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
    PresentFn originalPresent = reinterpret_cast<PresentFn>(s_instance->m_originalPresent);

    return originalPresent(swapChain, syncInterval, flags);
}

} // namespace Rendering
} // namespace ReKenshi

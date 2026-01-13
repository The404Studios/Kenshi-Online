/*
 * InputHook.cpp - Input Hooking Implementation
 */

#include "InputHook.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <iostream>

// Forward declare ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace KenshiOnline
{
    // Static instance storage for WndProc callback
    static InputHook* s_Instance = nullptr;
    static WNDPROC s_OriginalWndProc = nullptr;

    InputHook& InputHook::Get()
    {
        static InputHook instance;
        return instance;
    }

    bool InputHook::Initialize(HWND gameWindow)
    {
        if (m_Initialized)
            return true;

        if (!gameWindow || !IsWindow(gameWindow))
        {
            std::cout << "[InputHook] ERROR: Invalid game window\n";
            return false;
        }

        m_GameWindow = gameWindow;
        s_Instance = this;

        // Hook the window procedure
        s_OriginalWndProc = (WNDPROC)SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        m_OriginalWndProc = s_OriginalWndProc;

        if (!m_OriginalWndProc)
        {
            std::cout << "[InputHook] ERROR: Failed to hook WndProc\n";
            return false;
        }

        m_Initialized = true;
        std::cout << "[InputHook] Input hook installed - Toggle key: INSERT\n";
        return true;
    }

    void InputHook::Shutdown()
    {
        if (!m_Initialized)
            return;

        // Restore original WndProc
        if (m_GameWindow && m_OriginalWndProc)
        {
            SetWindowLongPtrW(m_GameWindow, GWLP_WNDPROC, (LONG_PTR)m_OriginalWndProc);
        }

        m_OriginalWndProc = nullptr;
        s_OriginalWndProc = nullptr;
        s_Instance = nullptr;
        m_GameWindow = nullptr;
        m_Initialized = false;

        std::cout << "[InputHook] Input hook removed\n";
    }

    LRESULT CALLBACK InputHook::HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Check for toggle key
        if (msg == WM_KEYDOWN && s_Instance)
        {
            if (wParam == (WPARAM)s_Instance->m_ToggleKey)
            {
                s_Instance->m_CaptureInput = !s_Instance->m_CaptureInput;
                std::cout << "[InputHook] Overlay " << (s_Instance->m_CaptureInput ? "OPENED" : "CLOSED") << "\n";

                // Show/hide cursor
                if (s_Instance->m_CaptureInput)
                {
                    while (ShowCursor(TRUE) < 0);
                }
                else
                {
                    while (ShowCursor(FALSE) >= 0);
                }

                return 0;
            }
        }

        // If overlay is capturing input, send to ImGui
        if (s_Instance && s_Instance->m_CaptureInput)
        {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
                return 0;

            // Block game input when overlay is active
            switch (msg)
            {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_MOUSEMOVE:
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
                // Check if ImGui wants to capture these
                if (ImGui::GetCurrentContext())
                {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.WantCaptureMouse || io.WantCaptureKeyboard)
                        return 0;
                }
                break;
            }
        }

        // Call original WndProc
        return CallWindowProcW(s_OriginalWndProc, hWnd, msg, wParam, lParam);
    }
}

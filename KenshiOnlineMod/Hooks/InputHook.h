/*
 * InputHook.h - Input Hooking for Kenshi Online Overlay
 * Hooks WndProc to intercept input when overlay is active
 */

#pragma once

#include <Windows.h>

namespace KenshiOnline
{
    class InputHook
    {
    public:
        static InputHook& Get();

        bool Initialize(HWND gameWindow);
        void Shutdown();

        // Check if input should go to overlay
        bool IsCapturingInput() const { return m_CaptureInput; }
        void SetCaptureInput(bool capture) { m_CaptureInput = capture; }

        // Toggle overlay visibility with key
        void SetToggleKey(int vKey) { m_ToggleKey = vKey; }
        int GetToggleKey() const { return m_ToggleKey; }

        HWND GetGameWindow() const { return m_GameWindow; }

    private:
        InputHook() = default;
        ~InputHook() = default;
        InputHook(const InputHook&) = delete;
        InputHook& operator=(const InputHook&) = delete;

        static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND m_GameWindow = nullptr;
        WNDPROC m_OriginalWndProc = nullptr;
        bool m_CaptureInput = false;
        int m_ToggleKey = VK_INSERT;  // Default toggle key
        bool m_Initialized = false;
    };
}

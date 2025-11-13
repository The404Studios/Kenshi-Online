#pragma once

#include <windows.h>
#include <functional>
#include <unordered_map>

namespace ReKenshi {

/**
 * Input Handler - captures keyboard and mouse input
 * Intercepts F1 key and UI interactions when overlay is visible
 */
class InputHandler {
public:
    using KeyCallback = std::function<void()>;
    using MouseCallback = std::function<void(int x, int y, bool leftButton, bool rightButton)>;

    InputHandler();
    ~InputHandler();

    // Initialization
    bool Initialize(HWND gameWindow);
    void Shutdown();

    // Input processing
    void Update();
    bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Callbacks
    void SetF1Callback(KeyCallback callback) { m_f1Callback = callback; }
    void SetMouseCallback(MouseCallback callback) { m_mouseCallback = callback; }

    // State
    bool IsKeyDown(int vkCode) const;
    void GetMousePosition(int& x, int& y) const;

    // Input capture mode (when UI is active)
    void SetCaptureInput(bool capture) { m_captureInput = capture; }
    bool IsCaptureInput() const { return m_captureInput; }

private:
    // Windows message hook
    static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static InputHandler* s_instance;

    // Window procedure hook
    static LRESULT CALLBACK WndProcHook(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    WNDPROC m_originalWndProc;

    HWND m_gameWindow;
    HHOOK m_keyboardHook;
    HHOOK m_mouseHook;

    // Input state
    bool m_keyState[256];
    int m_mouseX;
    int m_mouseY;
    bool m_leftButtonDown;
    bool m_rightButtonDown;
    bool m_captureInput;

    // Callbacks
    KeyCallback m_f1Callback;
    MouseCallback m_mouseCallback;
};

} // namespace ReKenshi

#include "InputHandler.h"
#include <iostream>

namespace ReKenshi {

InputHandler* InputHandler::s_instance = nullptr;

InputHandler::InputHandler()
    : m_gameWindow(nullptr)
    , m_originalWndProc(nullptr)
    , m_keyboardHook(nullptr)
    , m_mouseHook(nullptr)
    , m_mouseX(0)
    , m_mouseY(0)
    , m_leftButtonDown(false)
    , m_rightButtonDown(false)
    , m_captureInput(false)
{
    s_instance = this;
    std::memset(m_keyState, 0, sizeof(m_keyState));
}

InputHandler::~InputHandler() {
    Shutdown();
    s_instance = nullptr;
}

bool InputHandler::Initialize(HWND gameWindow) {
    if (!gameWindow) {
        return false;
    }

    m_gameWindow = gameWindow;

    // Install keyboard hook
    m_keyboardHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        KeyboardHookProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!m_keyboardHook) {
        return false;
    }

    // Install mouse hook
    m_mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        MouseHookProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!m_mouseHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
        return false;
    }

    return true;
}

void InputHandler::Shutdown() {
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }

    if (m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }

    if (m_originalWndProc && m_gameWindow) {
        SetWindowLongPtr(m_gameWindow, GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
        m_originalWndProc = nullptr;
    }
}

void InputHandler::Update() {
    // Update mouse position
    POINT pt;
    if (GetCursorPos(&pt) && ScreenToClient(m_gameWindow, &pt)) {
        m_mouseX = pt.x;
        m_mouseY = pt.y;
    }

    // Update button states
    m_leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m_rightButtonDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

bool InputHandler::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    // Return true to block message from game, false to pass through

    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP: {
        int vkCode = (int)wParam;
        bool isDown = (msg == WM_KEYDOWN);

        m_keyState[vkCode] = isDown;

        // Check for F1 key
        if (vkCode == VK_F1 && isDown) {
            if (m_f1Callback) {
                m_f1Callback();
            }
            return true; // Block F1 from game
        }

        // If capturing input, block all keys
        if (m_captureInput) {
            return true;
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE: {
        if (m_captureInput) {
            // Process mouse for UI
            if (m_mouseCallback) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                bool leftButton = (wParam & MK_LBUTTON) != 0;
                bool rightButton = (wParam & MK_RBUTTON) != 0;

                m_mouseCallback(x, y, leftButton, rightButton);
            }
            return true; // Block mouse from game
        }
        break;
    }
    }

    return false; // Pass through to game
}

bool InputHandler::IsKeyDown(int vkCode) const {
    if (vkCode < 0 || vkCode >= 256) {
        return false;
    }
    return m_keyState[vkCode];
}

void InputHandler::GetMousePosition(int& x, int& y) const {
    x = m_mouseX;
    y = m_mouseY;
}

LRESULT CALLBACK InputHandler::KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance) {
        KBDLLHOOKSTRUCT* kbd = (KBDLLHOOKSTRUCT*)lParam;

        if (s_instance->ProcessMessage(
            wParam == WM_KEYDOWN ? WM_KEYDOWN : WM_KEYUP,
            kbd->vkCode,
            0)) {
            return 1; // Block message
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputHandler::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance && s_instance->m_captureInput) {
        // Block mouse input when UI is active
        return 1;
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InputHandler::WndProcHook(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (s_instance && s_instance->ProcessMessage(msg, wParam, lParam)) {
        return 0; // Message handled
    }

    // Call original window procedure
    if (s_instance && s_instance->m_originalWndProc) {
        return CallWindowProc(s_instance->m_originalWndProc, hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace ReKenshi

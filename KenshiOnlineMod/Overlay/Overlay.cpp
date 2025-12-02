/*
 * Overlay.cpp - Main Overlay Manager Implementation
 */

#include "Overlay.h"
#include "OverlayUI.h"
#include "../Hooks/D3D11Hook.h"
#include "../Hooks/InputHook.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <iostream>
#include <chrono>

namespace KenshiOnline
{
    Overlay::Overlay()
    {
    }

    Overlay::~Overlay()
    {
        Shutdown();
    }

    Overlay& Overlay::Get()
    {
        static Overlay instance;
        return instance;
    }

    bool Overlay::Initialize()
    {
        if (m_Initialized)
            return true;

        std::cout << "[Overlay] Initializing overlay system...\n";

        // Initialize D3D11 hooks
        if (!D3D11Hook::Get().Initialize())
        {
            std::cout << "[Overlay] ERROR: Failed to initialize D3D11 hooks\n";
            return false;
        }

        // Set up render callback
        D3D11Hook::Get().SetRenderCallback([this]() {
            this->Render();
        });

        // Wait for first frame to get device
        std::cout << "[Overlay] Waiting for first frame...\n";
        int waitCount = 0;
        while (!D3D11Hook::Get().GetDevice() && waitCount < 100)
        {
            Sleep(100);
            waitCount++;
        }

        if (!D3D11Hook::Get().GetDevice())
        {
            std::cout << "[Overlay] ERROR: Timed out waiting for D3D11 device\n";
            return false;
        }

        // Initialize ImGui
        if (!InitializeImGui())
        {
            std::cout << "[Overlay] ERROR: Failed to initialize ImGui\n";
            return false;
        }

        // Initialize input hook
        HWND gameWindow = D3D11Hook::Get().GetGameWindow();
        if (gameWindow && !InputHook::Get().Initialize(gameWindow))
        {
            std::cout << "[Overlay] WARNING: Failed to initialize input hook\n";
        }

        // Create UI after everything is initialized
        m_UI = std::make_unique<OverlayUI>(*this);

        m_Initialized = true;
        m_Visible = true;  // Start visible for login screen

        std::cout << "[Overlay] Overlay system initialized successfully!\n";
        std::cout << "[Overlay] Press INSERT to toggle the overlay\n";

        return true;
    }

    void Overlay::Shutdown()
    {
        if (!m_Initialized)
            return;

        std::cout << "[Overlay] Shutting down overlay...\n";

        m_UI.reset();
        InputHook::Get().Shutdown();
        ShutdownImGui();
        D3D11Hook::Get().Shutdown();

        m_Initialized = false;
    }

    bool Overlay::InitializeImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.IniFilename = nullptr;  // Don't save settings

        // Set up style - Modern dark theme
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 6.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.WindowPadding = ImVec2(12, 12);
        style.FramePadding = ImVec2(8, 6);
        style.ItemSpacing = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(6, 4);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        // Custom dark blue color scheme
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.98f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.28f, 0.80f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.32f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.06f, 0.06f, 0.10f, 0.90f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.60f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.90f);
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.45f, 0.75f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.36f, 0.60f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.39f, 0.71f, 1.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.26f, 0.59f, 0.98f, 0.90f);
        colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);

        // Initialize Win32 backend
        if (!ImGui_ImplWin32_Init(D3D11Hook::Get().GetGameWindow()))
        {
            std::cout << "[Overlay] ERROR: ImGui_ImplWin32_Init failed\n";
            return false;
        }

        // Initialize DX11 backend
        if (!ImGui_ImplDX11_Init(D3D11Hook::Get().GetDevice(), D3D11Hook::Get().GetContext()))
        {
            std::cout << "[Overlay] ERROR: ImGui_ImplDX11_Init failed\n";
            ImGui_ImplWin32_Shutdown();
            return false;
        }

        std::cout << "[Overlay] ImGui initialized successfully\n";
        return true;
    }

    void Overlay::ShutdownImGui()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void Overlay::Render()
    {
        if (!m_Initialized)
            return;

        // Start new frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Update visibility based on input hook
        m_Visible = InputHook::Get().IsCapturingInput();

        // Render UI
        if (m_UI)
        {
            m_UI->Render();
        }

        // End frame
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void Overlay::SetConnectionStatus(ConnectionStatus status)
    {
        m_ConnectionStatus = status;

        // Update UI based on status
        if (m_UI)
        {
            switch (status)
            {
            case ConnectionStatus::Connected:
                m_UI->ShowSuccess("Connected to server!");
                break;
            case ConnectionStatus::Error:
                m_UI->ShowError("Connection failed");
                break;
            default:
                break;
            }
        }
    }

    void Overlay::UpdateLocalPlayer(const PlayerInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);
        m_LocalPlayer = info;
    }

    void Overlay::UpdatePlayerList(const std::vector<PlayerInfo>& players)
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);
        m_Players = players;
    }

    void Overlay::AddPlayer(const PlayerInfo& player)
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);

        // Check if player already exists
        for (auto& p : m_Players)
        {
            if (p.id == player.id || p.name == player.name)
            {
                p = player;
                return;
            }
        }

        m_Players.push_back(player);

        if (m_UI)
        {
            m_UI->ShowNotification(player.name + " joined the game");
        }
    }

    void Overlay::RemovePlayer(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);

        for (auto it = m_Players.begin(); it != m_Players.end(); ++it)
        {
            if (it->name == name)
            {
                if (m_UI)
                {
                    m_UI->ShowNotification(name + " left the game");
                }
                m_Players.erase(it);
                return;
            }
        }
    }

    std::vector<PlayerInfo> Overlay::GetPlayers() const
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);
        return m_Players;
    }

    int Overlay::GetPlayerCount() const
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);
        return static_cast<int>(m_Players.size());
    }

    void Overlay::AddChatMessage(const ChatMessage& msg)
    {
        std::lock_guard<std::mutex> lock(m_ChatMutex);

        m_ChatMessages.push_back(msg);

        // Limit chat history
        while (m_ChatMessages.size() > MAX_CHAT_MESSAGES)
        {
            m_ChatMessages.erase(m_ChatMessages.begin());
        }
    }

    void Overlay::AddSystemMessage(const std::string& message)
    {
        ChatMessage msg;
        msg.sender = "System";
        msg.message = message;
        msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        msg.type = 2;  // System message

        AddChatMessage(msg);
    }

    std::vector<ChatMessage> Overlay::GetChatMessages() const
    {
        std::lock_guard<std::mutex> lock(m_ChatMutex);
        return m_ChatMessages;
    }

    void Overlay::ClearChat()
    {
        std::lock_guard<std::mutex> lock(m_ChatMutex);
        m_ChatMessages.clear();
    }

    void Overlay::OnConnect(const std::string& address, int port, const std::string& username, const std::string& password)
    {
        m_ServerAddress = address;
        m_Port = port;
        m_ConnectionStatus = ConnectionStatus::Connecting;

        if (m_ConnectCallback)
        {
            m_ConnectCallback(address, port, username, password);
        }
    }

    void Overlay::OnSendChat(const std::string& message)
    {
        if (m_ChatCallback)
        {
            m_ChatCallback(message);
        }
    }

    void Overlay::OnDisconnect()
    {
        m_ConnectionStatus = ConnectionStatus::Disconnected;

        if (m_DisconnectCallback)
        {
            m_DisconnectCallback();
        }

        // Clear player list on disconnect
        {
            std::lock_guard<std::mutex> lock(m_PlayerMutex);
            m_Players.clear();
        }

        AddSystemMessage("Disconnected from server");
    }

    void Overlay::ShowError(const std::string& error)
    {
        if (m_UI)
        {
            m_UI->ShowError(error);
        }
        std::cout << "[Overlay] ERROR: " << error << "\n";
    }

    void Overlay::ShowNotification(const std::string& message)
    {
        if (m_UI)
        {
            m_UI->ShowNotification(message);
        }
        std::cout << "[Overlay] NOTIFY: " << message << "\n";
    }

    void Overlay::ShowSuccess(const std::string& message)
    {
        if (m_UI)
        {
            m_UI->ShowSuccess(message);
        }
        std::cout << "[Overlay] SUCCESS: " << message << "\n";
    }
}

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
        m_UI = std::make_unique<OverlayUI>(*this);
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

        m_Initialized = true;
        std::cout << "[Overlay] Overlay system initialized successfully!\n";
        std::cout << "[Overlay] Press INSERT to toggle the overlay\n";

        return true;
    }

    void Overlay::Shutdown()
    {
        if (!m_Initialized)
            return;

        std::cout << "[Overlay] Shutting down overlay...\n";

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

        // Set up style
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.FrameRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;

        // Custom colors - dark blue theme
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.12f, 0.94f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.24f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.20f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.35f, 0.40f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.45f, 0.67f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.40f, 0.40f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.30f, 0.55f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.35f, 0.60f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.40f, 0.31f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.30f, 0.55f, 0.80f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.35f, 0.60f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.24f, 0.86f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.30f, 0.55f, 0.80f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.24f, 0.45f, 1.00f);

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
            if (p.name == player.name)
            {
                p = player;
                return;
            }
        }

        m_Players.push_back(player);
    }

    void Overlay::RemovePlayer(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_PlayerMutex);
        m_Players.erase(
            std::remove_if(m_Players.begin(), m_Players.end(),
                [&name](const PlayerInfo& p) { return p.name == name; }),
            m_Players.end()
        );
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
        if (m_ChatMessages.size() > MAX_CHAT_MESSAGES)
        {
            m_ChatMessages.erase(m_ChatMessages.begin());
        }
    }

    void Overlay::AddSystemMessage(const std::string& message)
    {
        ChatMessage msg;
        msg.sender = "SYSTEM";
        msg.message = message;
        msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
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
        if (m_DisconnectCallback)
        {
            m_DisconnectCallback();
        }
    }

    void Overlay::ShowError(const std::string& error)
    {
        m_CurrentError = error;
        m_ErrorTimer = 5.0f;
        std::cout << "[Overlay] ERROR: " << error << "\n";
    }

    void Overlay::ShowNotification(const std::string& message)
    {
        m_CurrentNotification = message;
        m_NotificationTimer = 3.0f;
        std::cout << "[Overlay] NOTIFY: " << message << "\n";
    }
}

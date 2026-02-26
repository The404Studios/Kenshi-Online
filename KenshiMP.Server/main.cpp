#include "server.h"
#include "kmp/config.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <csignal>
#include <iostream>
#include <atomic>

static std::atomic<bool> g_running{true};

void SignalHandler(int signal) {
    spdlog::info("Received signal {}, shutting down...", signal);
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Setup logging
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("KenshiOnline_Server.log", true);
    auto logger = std::make_shared<spdlog::logger>("server",
        spdlog::sinks_init_list{console_sink, file_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    spdlog::info("╔══════════════════════════════════════╗");
    spdlog::info("║    Kenshi-Online Dedicated Server     ║");
    spdlog::info("║         v0.1.0 - Up to 16 Players    ║");
    spdlog::info("╚══════════════════════════════════════╝");

    // Load server config
    kmp::ServerConfig config;
    std::string configPath = "server.json";
    if (argc > 1) configPath = argv[1];

    if (config.Load(configPath)) {
        spdlog::info("Loaded config from: {}", configPath);
    } else {
        spdlog::info("No config found at '{}', using defaults", configPath);
        config.Save(configPath);
        spdlog::info("Default config saved to: {}", configPath);
    }

    // Signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Create and start server
    kmp::GameServer server;
    if (!server.Start(config)) {
        spdlog::error("Failed to start server!");
        return 1;
    }

    // Try to load saved world state
    {
        std::string savePath = config.savePath.empty() ? "kenshi_mp_world.json" : config.savePath;
        // LoadWorldFromFile is called internally if the file exists
        // The server will populate entities from the save file
        spdlog::info("Looking for saved world at: {}", savePath);
    }

    spdlog::info("Server '{}' started on port {} (max {} players)",
                 config.serverName, config.port, config.maxPlayers);
    spdlog::info("PvP: {} | Game Speed: {}x | Tick Rate: {} Hz",
                 config.pvpEnabled ? "Enabled" : "Disabled",
                 config.gameSpeed, config.tickRate);
    spdlog::info("Type 'help' for commands, 'stop' to shutdown");

    // Console command thread
    std::thread consoleThread([&server]() {
        std::string line;
        while (g_running && std::getline(std::cin, line)) {
            if (line == "stop" || line == "quit" || line == "exit") {
                g_running = false;
                break;
            } else if (line == "help") {
                std::cout << "Commands:" << std::endl;
                std::cout << "  status  - Show server status" << std::endl;
                std::cout << "  players - List connected players" << std::endl;
                std::cout << "  kick <id> - Kick a player" << std::endl;
                std::cout << "  say <msg> - Broadcast system message" << std::endl;
                std::cout << "  save    - Save world state" << std::endl;
                std::cout << "  stop    - Shutdown server" << std::endl;
            } else if (line == "status") {
                server.PrintStatus();
            } else if (line == "players") {
                server.PrintPlayers();
            } else if (line == "save") {
                server.SaveWorld();
            } else if (line.substr(0, 4) == "say ") {
                server.BroadcastSystemMessage(line.substr(4));
            } else if (line.substr(0, 5) == "kick ") {
                try {
                    uint32_t id = std::stoul(line.substr(5));
                    server.KickPlayer(id, "Kicked by admin");
                } catch (...) {
                    std::cout << "Usage: kick <player_id>" << std::endl;
                }
            } else if (!line.empty()) {
                std::cout << "Unknown command. Type 'help' for commands." << std::endl;
            }
        }
    });

    // Main server loop
    auto lastTick = std::chrono::steady_clock::now();
    int tickIntervalMs = 1000 / config.tickRate;

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick);

        if (elapsed.count() >= tickIntervalMs) {
            float deltaTime = elapsed.count() / 1000.f;
            lastTick = now;

            server.Update(deltaTime);
        }

        // Sleep a bit to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spdlog::info("Shutting down...");
    server.SaveWorld();
    server.Stop();

    if (consoleThread.joinable()) {
        consoleThread.detach(); // Detach since stdin::getline blocks
    }

    spdlog::info("Server stopped.");
    return 0;
}

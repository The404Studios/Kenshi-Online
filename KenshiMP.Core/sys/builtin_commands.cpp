#include "command_registry.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "../hooks/time_hooks.h"
#include "../hooks/entity_hooks.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include "kmp/hook_manager.h"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace kmp {

void CommandRegistry::RegisterBuiltins() {
    // /help — List all registered commands
    Register("help", "List all available commands", [](const CommandArgs&) -> std::string {
        auto cmds = CommandRegistry::Get().GetAll();
        std::string result = "--- Commands ---";
        for (auto* cmd : cmds) {
            result += "\n/" + cmd->name + " - " + cmd->description;
        }
        return result;
    });

    // /tp [player] — Teleport to nearest (or named) remote player
    Register("tp", "Teleport to player (/tp or /tp name)", [](const CommandArgs& args) -> std::string {
        auto& core = Core::Get();
        if (!core.IsConnected()) return "Not connected to a server.";

        // If a player name is given, find their entity and teleport to it
        if (!args.args.empty()) {
            std::string targetName = args.args[0];
            // Join all args in case name has spaces
            for (size_t i = 1; i < args.args.size(); i++)
                targetName += " " + args.args[i];

            // Search remote players for a name match (case-insensitive partial)
            auto remotePlayers = core.GetPlayerController().GetAllRemotePlayers();
            PlayerID foundId = 0;
            std::string foundName;

            // First pass: exact match (case-insensitive)
            for (auto& rp : remotePlayers) {
                std::string rpLower = rp.playerName;
                std::string tgtLower = targetName;
                std::transform(rpLower.begin(), rpLower.end(), rpLower.begin(), ::tolower);
                std::transform(tgtLower.begin(), tgtLower.end(), tgtLower.begin(), ::tolower);
                if (rpLower == tgtLower) { foundId = rp.playerId; foundName = rp.playerName; break; }
            }
            // Second pass: prefix match
            if (foundId == 0) {
                for (auto& rp : remotePlayers) {
                    std::string rpLower = rp.playerName;
                    std::string tgtLower = targetName;
                    std::transform(rpLower.begin(), rpLower.end(), rpLower.begin(), ::tolower);
                    std::transform(tgtLower.begin(), tgtLower.end(), tgtLower.begin(), ::tolower);
                    if (rpLower.find(tgtLower) == 0) { foundId = rp.playerId; foundName = rp.playerName; break; }
                }
            }

            if (foundId == 0) return "Player '" + targetName + "' not found.";

            // Find an entity owned by this player
            auto remoteEntities = core.GetEntityRegistry().GetRemoteEntities();
            Vec3 targetPos(0, 0, 0);
            bool foundPos = false;
            for (EntityID eid : remoteEntities) {
                auto* info = core.GetEntityRegistry().GetInfo(eid);
                if (info && info->ownerPlayerId == foundId) {
                    Vec3 pos = info->lastPosition;
                    if (pos.x != 0.f || pos.y != 0.f || pos.z != 0.f) {
                        targetPos = pos;
                        foundPos = true;
                        break;
                    }
                }
            }
            if (!foundPos) return "Player '" + foundName + "' has no visible entities.";

            // Teleport local squad to target
            auto localEntities = core.GetEntityRegistry().GetPlayerEntities(core.GetLocalPlayerId());
            int teleported = 0;
            for (EntityID netId : localEntities) {
                void* gameObj = core.GetEntityRegistry().GetGameObject(netId);
                if (!gameObj) continue;
                game::CharacterAccessor accessor(gameObj);
                if (!accessor.IsValid()) continue;
                Vec3 tpPos = targetPos;
                tpPos.x += static_cast<float>(teleported % 4) * 3.0f;
                tpPos.z += static_cast<float>(teleported / 4) * 3.0f;
                if (accessor.WritePosition(tpPos)) {
                    core.GetEntityRegistry().UpdatePosition(netId, tpPos);
                    teleported++;
                }
            }
            if (teleported > 0) return "Teleported to " + foundName + "!";
            return "Teleport failed — no valid local characters.";
        }

        // No name given — teleport to nearest
        if (core.TeleportToNearestRemotePlayer()) {
            return ""; // TeleportToNearestRemotePlayer already shows messages
        }
        return ""; // Error messages already shown by the method
    });

    // /teleport alias — forward args to /tp
    Register("teleport", "Teleport to player (/teleport or /teleport name)", [](const CommandArgs& args) -> std::string {
        std::string cmd = "/tp";
        for (auto& a : args.args) cmd += " " + a;
        return CommandRegistry::Get().Execute(cmd);
    });

    // /pos — Show current position
    Register("pos", "Show your current position", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto localEntities = core.GetEntityRegistry().GetPlayerEntities(core.GetLocalPlayerId());
        if (!localEntities.empty()) {
            void* obj = core.GetEntityRegistry().GetGameObject(localEntities[0]);
            if (obj) {
                game::CharacterAccessor accessor(obj);
                Vec3 pos = accessor.GetPosition();
                char buf[128];
                snprintf(buf, sizeof(buf), "Position: (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
                return buf;
            }
        }
        return "No local character found.";
    });

    // /position alias
    Register("position", "Show your current position", [](const CommandArgs&) -> std::string {
        return CommandRegistry::Get().Execute("/pos");
    });

    // /players — List connected players with IDs
    Register("players", "List connected players", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& pc = core.GetPlayerController();
        auto remotePlayers = pc.GetAllRemotePlayers();

        std::string result = "--- Players ---";
        result += "\nYou: " + pc.GetLocalPlayerName();
        for (auto& rp : remotePlayers) {
            result += "\n  " + rp.playerName + " (ID " + std::to_string(rp.playerId) + ")";
        }
        result += "\nTotal: " + std::to_string(1 + remotePlayers.size());
        return result;
    });

    // /who alias
    Register("who", "List connected players", [](const CommandArgs&) -> std::string {
        return CommandRegistry::Get().Execute("/players");
    });

    // /status — Connection, entity, spawn stats
    Register("status", "Show connection and entity status", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& sm = core.GetSpawnManager();
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Connected: %s | Entities: %d | Remote: %d | PendingSpawns: %d | Templates: %d",
                 core.IsConnected() ? "yes" : "no",
                 (int)core.GetEntityRegistry().GetEntityCount(),
                 (int)core.GetEntityRegistry().GetRemoteCount(),
                 (int)sm.GetPendingSpawnCount(),
                 (int)sm.GetTemplateCount());
        return buf;
    });

    // /connect ip [port] — Connect to a server
    Register("connect", "Connect to a server (ip [port])", [](const CommandArgs& args) -> std::string {
        auto& core = Core::Get();
        if (core.IsConnected()) return "Already connected. Use /disconnect first.";
        if (args.args.empty()) return "Usage: /connect <ip> [port]";

        std::string ip = args.args[0];
        uint16_t port = 7777; // Default port
        if (args.args.size() >= 2) {
            try {
                port = static_cast<uint16_t>(std::stoi(args.args[1]));
            } catch (...) {
                return "Invalid port number.";
            }
        }

        if (core.GetClient().ConnectAsync(ip, port)) {
            return "Connecting to " + ip + ":" + std::to_string(port) + "...";
        }
        return "Connection failed to start.";
    });

    // /disconnect — Disconnect from server
    Register("disconnect", "Disconnect from server", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        if (!core.IsConnected()) return "Not connected.";

        // Teleport remote entities underground before clearing registry
        // (SetConnected(false) clears the registry but doesn't hide the game objects)
        auto& registry = core.GetEntityRegistry();
        auto remoteEntities = registry.GetRemoteEntities();
        int cleaned = 0;
        for (EntityID eid : remoteEntities) {
            void* gameObj = registry.GetGameObject(eid);
            if (gameObj) {
                game::CharacterAccessor accessor(gameObj);
                Vec3 underground(0.f, -10000.f, 0.f);
                accessor.WritePosition(underground);
                cleaned++;
            }
        }

        core.GetClient().Disconnect();
        core.SetConnected(false);

        std::string msg = "Disconnected from server.";
        if (cleaned > 0)
            msg += " Cleaned up " + std::to_string(cleaned) + " remote entities.";
        return msg;
    });

    // /time [value] — Show or set time of day (0.0=midnight, 0.5=noon)
    Register("time", "Show/set time (/time or /time 0.5)", [](const CommandArgs& args) -> std::string {
        if (!time_hooks::HasTimeManager())
            return "Time manager not captured yet (TimeUpdate hook hasn't fired).";

        // If argument given, try to set time
        if (!args.args.empty()) {
            try {
                float newTime = std::stof(args.args[0]);
                if (newTime < 0.f || newTime >= 1.f) return "Time must be between 0.0 and 1.0 (0=midnight, 0.5=noon).";
                if (time_hooks::WriteTimeOfDay(newTime)) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Time set to %.2f", newTime);
                    return buf;
                }
                return "Failed to write time.";
            } catch (...) {
                return "Invalid time value. Use 0.0-1.0 (0=midnight, 0.5=noon).";
            }
        }

        // Show current time (read from captured TimeManager)
        float tod = time_hooks::GetTimeOfDay();
        float speed = time_hooks::GetGameSpeed();

        float hours24 = tod * 24.f;
        int hour = static_cast<int>(hours24) % 24;
        int minute = static_cast<int>((hours24 - std::floor(hours24)) * 60.f);

        char buf[128];
        snprintf(buf, sizeof(buf), "Time: %02d:%02d (%.2f) | Speed: %.1fx",
                 hour, minute, tod, speed);
        return buf;
    });

    // /debug — Toggle debug info overlay
    Register("debug", "Toggle debug info overlay", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        core.GetNativeHud().ToggleLogPanel();
        return "Debug overlay toggled.";
    });

    // /entities — List all tracked entities by type
    Register("entities", "List all tracked entities", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& er = core.GetEntityRegistry();

        size_t total = er.GetEntityCount();
        size_t remote = er.GetRemoteCount();
        size_t spawned = er.GetSpawnedRemoteCount();

        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Entities: %d total | %d local | %d remote (%d spawned in world)",
                 (int)total, (int)(total - remote), (int)remote, (int)spawned);
        return buf;
    });

    // /ping — Show current ping to server
    Register("ping", "Show current ping to server", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        if (!core.IsConnected()) return "Not connected.";

        uint32_t ping = core.GetClient().GetPing();
        return "Ping: " + std::to_string(ping) + " ms";
    });

    // /kick <player> [reason] — Kick a player (host only)
    Register("kick", "Kick a player (host only)", [](const CommandArgs& args) -> std::string {
        auto& core = Core::Get();
        if (!core.IsConnected()) return "Not connected.";
        if (!core.IsHost()) return "Only the host can kick players.";
        if (args.args.empty()) return "Usage: /kick <name> [reason]";

        std::string targetName = args.args[0];
        std::string reason;
        for (size_t i = 1; i < args.args.size(); i++)
            reason += (i > 1 ? " " : "") + args.args[i];

        auto remotePlayers = core.GetPlayerController().GetAllRemotePlayers();
        PlayerID targetId = 0;
        for (auto& rp : remotePlayers) {
            std::string rpLower = rp.playerName;
            std::string tgtLower = targetName;
            std::transform(rpLower.begin(), rpLower.end(), rpLower.begin(), ::tolower);
            std::transform(tgtLower.begin(), tgtLower.end(), tgtLower.begin(), ::tolower);
            if (rpLower.find(tgtLower) == 0) { targetId = rp.playerId; break; }
        }
        if (targetId == 0) return "Player '" + targetName + "' not found.";

        MsgAdminCommand msg{};
        msg.commandType = 0; // kick
        msg.targetPlayerId = targetId;
        if (!reason.empty()) strncpy(msg.textParam, reason.c_str(), sizeof(msg.textParam) - 1);

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_AdminCommand);
        writer.WriteRaw(&msg, sizeof(msg));
        core.GetClient().SendReliable(writer.Data(), writer.Size());
        return "Kick request sent.";
    });

    // /announce <message> — Broadcast system message (host only)
    Register("announce", "Broadcast system message (host only)", [](const CommandArgs& args) -> std::string {
        auto& core = Core::Get();
        if (!core.IsConnected()) return "Not connected.";
        if (!core.IsHost()) return "Only the host can announce.";
        if (args.args.empty()) return "Usage: /announce <message>";

        std::string message;
        for (size_t i = 0; i < args.args.size(); i++)
            message += (i > 0 ? " " : "") + args.args[i];

        MsgAdminCommand msg{};
        msg.commandType = 4; // announce
        strncpy(msg.textParam, message.c_str(), sizeof(msg.textParam) - 1);

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_AdminCommand);
        writer.WriteRaw(&msg, sizeof(msg));
        core.GetClient().SendReliable(writer.Data(), writer.Size());
        return "Announcement sent.";
    });

    // /hooks — Hook status dashboard (debug tool)
    Register("hooks", "Show all hook status and prologue bytes", [](const CommandArgs&) -> std::string {
        auto diags = HookManager::Get().GetDiagnostics();
        if (diags.empty()) return "No hooks installed.";

        std::string result = "--- Hook Status ---";
        int active = 0, movRaxCount = 0;

        for (auto& d : diags) {
            char line[256];
            char prologueStr[32];
            snprintf(prologueStr, sizeof(prologueStr),
                     "%02X %02X %02X %02X %02X %02X %02X %02X",
                     d.prologue[0], d.prologue[1], d.prologue[2], d.prologue[3],
                     d.prologue[4], d.prologue[5], d.prologue[6], d.prologue[7]);

            const char* mode = "trampoline";
            if (d.isVtable) mode = "vtable";
            // Check if prologue starts with mov rax, rsp (48 8B C4)
            bool isMovRax = (d.prologue[0] == 0x48 && d.prologue[1] == 0x8B && d.prologue[2] == 0xC4);
            if (isMovRax) { mode = "tramp+movrax"; movRaxCount++; }

            snprintf(line, sizeof(line), "\n%-20s 0x%012llX  %s  [%s]  %s  calls:%d crash:%d",
                     d.name.c_str(),
                     static_cast<unsigned long long>(d.targetAddr),
                     d.enabled ? "ON " : "OFF",
                     prologueStr,
                     mode,
                     d.callCount,
                     d.crashCount);
            result += line;
            if (d.enabled) active++;
        }

        char summary[128];
        snprintf(summary, sizeof(summary),
                 "\n--- %d total | %d active | %d mov-rax-rsp ---",
                 (int)diags.size(), active, movRaxCount);
        result += summary;
        return result;
    });

    spdlog::info("CommandRegistry: {} built-in commands registered", GetAll().size());
}

} // namespace kmp

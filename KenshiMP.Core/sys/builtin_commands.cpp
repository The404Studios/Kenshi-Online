#include "command_registry.h"
#include "../core.h"
#include "../game/game_types.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "../game/asset_facilitator.h"
#include "../game/loading_orchestrator.h"
#include "../hooks/time_hooks.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/ai_hooks.h"
#include "../hooks/char_tracker_hooks.h"
#include "../game/shared_save_sync.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
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
        uint16_t port = KMP_DEFAULT_PORT; // 27800
        if (args.args.size() >= 2) {
            try {
                port = static_cast<uint16_t>(std::stoi(args.args[1]));
            } catch (...) {
                return "Invalid port number.";
            }
        }

        if (core.GetClient().ConnectAsync(ip, port)) {
            core.TransitionTo(ClientPhase::Connecting);
            core.GetOverlay().SetConnecting(true);
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

    // ═══════════════════════════════════════════════════════════════════
    // DEBUG / REVERSE ENGINEERING TOOLS
    // ═══════════════════════════════════════════════════════════════════

    // /offsets — Dump all known offsets with verification status
    Register("offsets", "Dump all game offsets and their status", [](const CommandArgs&) -> std::string {
        auto& co = game::GetOffsets().character;
        auto& wo = game::GetOffsets().world;

        auto fmtOff = [](const char* name, int val) -> std::string {
            char buf[64];
            if (val >= 0)
                snprintf(buf, sizeof(buf), "\n  %-22s 0x%03X  OK", name, val);
            else
                snprintf(buf, sizeof(buf), "\n  %-22s  -1    UNKNOWN", name);
            return buf;
        };

        std::string r = "--- Character Offsets ---";
        r += fmtOff("name", co.name);
        r += fmtOff("faction", co.faction);
        r += fmtOff("position (read)", co.position);
        r += fmtOff("rotation", co.rotation);
        r += fmtOff("gameDataPtr", co.gameDataPtr);
        r += fmtOff("inventory", co.inventory);
        r += fmtOff("stats", co.stats);
        r += fmtOff("animClassOffset", co.animClassOffset);
        r += fmtOff("charMovementOffset", co.charMovementOffset);
        r += fmtOff("writablePosOffset", co.writablePosOffset);
        r += fmtOff("writablePosVecOff", co.writablePosVecOffset);
        r += fmtOff("squad", co.squad);
        r += fmtOff("equipment", co.equipment);
        r += fmtOff("isPlayerControlled", co.isPlayerControlled);
        r += fmtOff("health (direct)", co.health);
        r += fmtOff("healthChain1", co.healthChain1);
        r += fmtOff("healthChain2", co.healthChain2);
        r += fmtOff("healthBase", co.healthBase);
        r += fmtOff("moneyChain1", co.moneyChain1);
        r += fmtOff("moneyChain2", co.moneyChain2);
        r += fmtOff("moneyBase", co.moneyBase);
        r += fmtOff("sceneNode", co.sceneNode);
        r += fmtOff("aiPackage", co.aiPackage);
        r += "\n--- World Offsets ---";
        r += fmtOff("gameSpeed", wo.gameSpeed);
        r += fmtOff("characterList", wo.characterList);
        r += fmtOff("zoneManager", wo.zoneManager);
        r += fmtOff("timeOfDay", wo.timeOfDay);

        int known = 0, unknown = 0;
        auto count = [&](int v) { if (v >= 0) known++; else unknown++; };
        count(co.name); count(co.faction); count(co.position); count(co.rotation);
        count(co.animClassOffset); count(co.squad); count(co.equipment);
        count(co.isPlayerControlled); count(co.health); count(co.sceneNode);
        count(co.aiPackage);

        char summary[128];
        snprintf(summary, sizeof(summary), "\n--- %d known | %d unknown ---", known, unknown);
        r += summary;
        return r;
    });

    // /dump <hex_addr> [lines] — Hex dump memory at address
    Register("dump", "Hex dump memory (/dump <addr> [lines])", [](const CommandArgs& args) -> std::string {
        if (args.args.empty()) return "Usage: /dump <hex_address> [lines=4]";

        uintptr_t addr = 0;
        try {
            addr = std::stoull(args.args[0], nullptr, 16);
        } catch (...) {
            return "Invalid hex address.";
        }

        int lines = 4;
        if (args.args.size() >= 2) {
            try { lines = std::stoi(args.args[1]); } catch (...) {}
        }
        if (lines < 1) lines = 1;
        if (lines > 32) lines = 32;

        std::string r = "--- Memory dump at 0x" + args.args[0] + " ---";
        for (int line = 0; line < lines; line++) {
            uintptr_t lineAddr = addr + line * 16;
            char hexPart[80] = {};
            char asciiPart[20] = {};
            bool anyFail = false;

            for (int b = 0; b < 16; b++) {
                uint8_t byte = 0;
                if (Memory::Read(lineAddr + b, byte)) {
                    sprintf_s(hexPart + b * 3, 4, "%02X ", byte);
                    asciiPart[b] = (byte >= 0x20 && byte <= 0x7E) ? (char)byte : '.';
                } else {
                    sprintf_s(hexPart + b * 3, 4, "?? ");
                    asciiPart[b] = '?';
                    anyFail = true;
                }
            }
            asciiPart[16] = '\0';

            char lineBuf[160];
            snprintf(lineBuf, sizeof(lineBuf), "\n  %012llX  %s |%s|",
                     (unsigned long long)lineAddr, hexPart, asciiPart);
            r += lineBuf;

            if (anyFail) { r += " [READ FAIL]"; break; }
        }
        return r;
    });

    // /probe — Read all known fields of the primary character
    Register("probe", "Probe primary character's memory fields", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
        if (!primaryChar) return "No primary character found.";

        uintptr_t ptr = reinterpret_cast<uintptr_t>(primaryChar);
        game::CharacterAccessor accessor(primaryChar);
        auto& co = game::GetOffsets().character;

        char buf[128];
        std::string r = "--- Primary Character Probe ---";
        snprintf(buf, sizeof(buf), "\n  Address:  0x%012llX", (unsigned long long)ptr);
        r += buf;

        // Name
        std::string name = accessor.GetName();
        r += "\n  Name:     " + (name.empty() ? "(empty)" : name);

        // Position
        Vec3 pos = accessor.GetPosition();
        snprintf(buf, sizeof(buf), "\n  Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        r += buf;

        // Rotation
        Quat rot = accessor.GetRotation();
        snprintf(buf, sizeof(buf), "\n  Rotation: (%.2f, %.2f, %.2f, %.2f)", rot.w, rot.x, rot.y, rot.z);
        r += buf;

        // Faction
        uintptr_t factionPtr = accessor.GetFactionPtr();
        if (factionPtr) {
            game::FactionAccessor faction(reinterpret_cast<void*>(factionPtr));
            std::string factionName = faction.GetName();
            snprintf(buf, sizeof(buf), "\n  Faction:  0x%llX '%s'",
                     (unsigned long long)factionPtr, factionName.c_str());
        } else {
            snprintf(buf, sizeof(buf), "\n  Faction:  (null)");
        }
        r += buf;

        // GameData
        uintptr_t gdPtr = accessor.GetGameDataPtr();
        if (gdPtr) {
            std::string gdName = SpawnManager::ReadKenshiString(gdPtr + 0x28);
            snprintf(buf, sizeof(buf), "\n  GameData: 0x%llX '%s'",
                     (unsigned long long)gdPtr, gdName.c_str());
        } else {
            snprintf(buf, sizeof(buf), "\n  GameData: (null)");
        }
        r += buf;

        // Squad
        uintptr_t squadPtr = accessor.GetSquadPtr();
        snprintf(buf, sizeof(buf), "\n  Squad:    0x%llX", (unsigned long long)squadPtr);
        r += buf;

        // Health chain
        float hp = accessor.GetHealth(BodyPart::Head);
        snprintf(buf, sizeof(buf), "\n  Health:   %.1f (head)", hp);
        r += buf;

        // Money
        int money = accessor.GetMoney();
        snprintf(buf, sizeof(buf), "\n  Money:    %d cats", money);
        r += buf;

        // AnimClass offset
        snprintf(buf, sizeof(buf), "\n  AnimClass offset: %s",
                 co.animClassOffset >= 0 ? std::to_string(co.animClassOffset).c_str() : "UNKNOWN");
        r += buf;

        // isPlayerControlled offset
        snprintf(buf, sizeof(buf), "\n  PlayerControlled offset: %s",
                 co.isPlayerControlled >= 0 ? std::to_string(co.isPlayerControlled).c_str() : "UNKNOWN");
        r += buf;

        // Write-position chain test
        if (co.animClassOffset >= 0) {
            uintptr_t animClass = 0;
            Memory::Read(ptr + co.animClassOffset, animClass);
            uintptr_t charMov = 0;
            if (animClass) Memory::Read(animClass + co.charMovementOffset, charMov);
            snprintf(buf, sizeof(buf), "\n  WritePos chain: animClass=0x%llX charMov=0x%llX",
                     (unsigned long long)animClass, (unsigned long long)charMov);
            r += buf;
            if (charMov) {
                uintptr_t posAddr = charMov + co.writablePosOffset + co.writablePosVecOffset;
                float wx = 0, wy = 0, wz = 0;
                Memory::Read(posAddr, wx);
                Memory::Read(posAddr + 4, wy);
                Memory::Read(posAddr + 8, wz);
                snprintf(buf, sizeof(buf), "\n  WritablePos:    (%.1f, %.1f, %.1f)", wx, wy, wz);
                r += buf;
            }
        }

        return r;
    });

    // /chars — List all characters visible to the mod
    Register("chars", "List all known characters", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        std::string r = "--- Registry Entities ---";

        auto localEntities = registry.GetPlayerEntities(core.GetLocalPlayerId());
        auto remoteEntities = registry.GetRemoteEntities();

        char buf[256];
        snprintf(buf, sizeof(buf), "\n  Local: %d  Remote: %d",
                 (int)localEntities.size(), (int)remoteEntities.size());
        r += buf;

        // Local entities
        for (EntityID eid : localEntities) {
            void* obj = registry.GetGameObject(eid);
            auto* info = registry.GetInfo(eid);
            if (obj) {
                game::CharacterAccessor accessor(obj);
                Vec3 pos = accessor.GetPosition();
                std::string name = accessor.GetName();
                snprintf(buf, sizeof(buf), "\n  [L] #%u 0x%llX '%s' (%.0f,%.0f,%.0f)",
                         eid, (unsigned long long)obj, name.c_str(), pos.x, pos.y, pos.z);
            } else {
                snprintf(buf, sizeof(buf), "\n  [L] #%u (no game object)", eid);
            }
            r += buf;
        }

        // Remote entities
        for (EntityID eid : remoteEntities) {
            void* obj = registry.GetGameObject(eid);
            auto* info = registry.GetInfo(eid);
            PlayerID owner = info ? info->ownerPlayerId : 0;
            if (obj) {
                game::CharacterAccessor accessor(obj);
                Vec3 pos = accessor.GetPosition();
                std::string name = accessor.GetName();
                snprintf(buf, sizeof(buf), "\n  [R] #%u owner=%u 0x%llX '%s' (%.0f,%.0f,%.0f)",
                         eid, owner, (unsigned long long)obj, name.c_str(), pos.x, pos.y, pos.z);
            } else {
                snprintf(buf, sizeof(buf), "\n  [R] #%u owner=%u (no game object — pending spawn)",
                         eid, owner);
            }
            r += buf;
        }

        // CharacterIterator count
        game::CharacterIterator iter;
        snprintf(buf, sizeof(buf), "\n--- CharacterIterator: %d characters in world ---", iter.Count());
        r += buf;

        // Loading cache removed — CharacterIterator is the sole discovery path

        return r;
    });

    // /spawn — SpawnManager readiness and state
    Register("spawn", "Show spawn system status", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& sm = core.GetSpawnManager();

        char buf[256];
        std::string r = "--- Spawn System Status ---";

        snprintf(buf, sizeof(buf), "\n  Factory ready:     %s", sm.IsReady() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Pre-call data:     %s", sm.HasPreCallData() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Request struct:    %s", sm.HasRequestStruct() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Pending spawns:    %d", (int)sm.GetPendingSpawnCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Total templates:   %d", (int)sm.GetTemplateCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Factory templates: %d", (int)sm.GetFactoryTemplateCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Char templates:    %d", (int)sm.GetCharacterTemplateCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  GDM pointer:       0x%llX",
                 (unsigned long long)sm.GetManagerPointer());
        r += buf;

        // Spawn path readiness
        bool inPlace = sm.IsReady() && sm.HasPreCallData();
        bool direct = sm.HasPreCallData();
        snprintf(buf, sizeof(buf), "\n  --- Spawn Paths ---");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  In-place replay:   %s", inPlace ? "READY" : "NOT READY");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Direct spawn:      %s", direct ? "READY" : "NOT READY");
        r += buf;

        // In-place spawn stats
        int inPlaceCount = entity_hooks::GetInPlaceSpawnCount();
        bool recentSpawn = entity_hooks::HasRecentInPlaceSpawn(30);
        snprintf(buf, sizeof(buf), "\n  In-place spawns:   %d (recent: %s)",
                 inPlaceCount, recentSpawn ? "yes" : "no");
        r += buf;

        // Game loaded state
        snprintf(buf, sizeof(buf), "\n  Game loaded:       %s", core.IsGameLoaded() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Connected:         %s", core.IsConnected() ? "YES" : "NO");
        r += buf;

        // Loading orchestrator state (spawn gating)
        auto& orch = core.GetLoadingOrch();
        const char* phaseName = "?";
        switch (orch.GetPhase()) {
            case LoadingPhase::Idle: phaseName = "Idle"; break;
            case LoadingPhase::InitialLoad: phaseName = "InitialLoad"; break;
            case LoadingPhase::ZoneTransition: phaseName = "ZoneTransition"; break;
            case LoadingPhase::SpawnLoad: phaseName = "SpawnLoad"; break;
        }
        r += "\n  --- Spawn Gate ---";
        snprintf(buf, sizeof(buf), "\n  Phase:             %s", phaseName);
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Orch game loaded:  %s", orch.IsGameLoaded() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  In burst:          %s", orch.IsInBurst() ? "YES" : "NO");
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Burst count:       %d", orch.GetBurstCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Can spawn now:     %s", AssetFacilitator::Get().CanSpawn() ? "YES" : "NO");
        r += buf;
        std::string blockReason = orch.GetSpawnBlockReason();
        snprintf(buf, sizeof(buf), "\n  Block reason:      %s", blockReason.c_str());
        r += buf;

        return r;
    });

    // /verify — Cross-verify offsets by reading a live character
    Register("verify", "Verify offsets against live character data", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
        if (!primaryChar) return "No primary character — load a game first.";

        uintptr_t ptr = reinterpret_cast<uintptr_t>(primaryChar);
        auto& co = game::GetOffsets().character;
        std::string r = "--- Offset Verification ---";
        char buf[256];
        int pass = 0, fail = 0, skip = 0;

        auto check = [&](const char* name, int offset, auto validator) {
            if (offset < 0) { skip++; r += "\n  SKIP " + std::string(name); return; }
            if (validator(ptr + offset)) {
                pass++;
                snprintf(buf, sizeof(buf), "\n  PASS %-20s +0x%03X", name, offset);
            } else {
                fail++;
                snprintf(buf, sizeof(buf), "\n  FAIL %-20s +0x%03X", name, offset);
            }
            r += buf;
        };

        // Position: should be non-zero
        check("position", co.position, [](uintptr_t addr) {
            float x = 0, y = 0, z = 0;
            Memory::Read(addr, x); Memory::Read(addr + 4, y); Memory::Read(addr + 8, z);
            return (x != 0.f || y != 0.f || z != 0.f);
        });

        // Rotation: w should be near 1.0 for identity, and magnitude ~1
        check("rotation", co.rotation, [](uintptr_t addr) {
            float w = 0, x = 0, y = 0, z = 0;
            Memory::Read(addr, w); Memory::Read(addr + 4, x);
            Memory::Read(addr + 8, y); Memory::Read(addr + 12, z);
            float mag = w * w + x * x + y * y + z * z;
            return (mag > 0.5f && mag < 1.5f);
        });

        // Faction: should be a valid pointer
        check("faction", co.faction, [](uintptr_t addr) {
            uintptr_t val = 0;
            Memory::Read(addr, val);
            return (val > 0x10000 && val < 0x00007FFFFFFFFFFF);
        });

        // Name: should be a readable string (check SSO layout)
        check("name", co.name, [](uintptr_t addr) {
            uint64_t length = 0, capacity = 0;
            Memory::Read(addr + 0x10, length);
            Memory::Read(addr + 0x18, capacity);
            return (length > 0 && length < 200 && capacity >= length);
        });

        // GameData: should be a valid pointer
        check("gameDataPtr", co.gameDataPtr, [](uintptr_t addr) {
            uintptr_t val = 0;
            Memory::Read(addr, val);
            return (val > 0x10000 && val < 0x00007FFFFFFFFFFF);
        });

        // Inventory: should be a valid pointer
        check("inventory", co.inventory, [](uintptr_t addr) {
            uintptr_t val = 0;
            Memory::Read(addr, val);
            return (val > 0x10000 && val < 0x00007FFFFFFFFFFF);
        });

        // Stats: pointer or inline — should be non-zero region
        check("stats", co.stats, [](uintptr_t addr) {
            uintptr_t val = 0;
            Memory::Read(addr, val);
            return (val != 0);
        });

        // Health chain: follow pointer chain
        {
            r += "\n  --- Health Chain ---";
            uintptr_t step1 = 0, step2 = 0;
            float hp = 0;
            bool ok = false;
            Memory::Read(ptr + co.healthChain1, step1);
            if (step1 > 0x10000 && step1 < 0x00007FFFFFFFFFFF) {
                Memory::Read(step1 + co.healthChain2, step2);
                if (step2 > 0x10000 && step2 < 0x00007FFFFFFFFFFF) {
                    Memory::Read(step2 + co.healthBase, hp);
                    ok = (hp >= -100.f && hp <= 200.f);
                }
            }
            snprintf(buf, sizeof(buf), "\n  %s health chain: +%X -> 0x%llX -> +%X -> 0x%llX -> +%X -> %.1f",
                     ok ? "PASS" : "FAIL",
                     co.healthChain1, (unsigned long long)step1,
                     co.healthChain2, (unsigned long long)step2,
                     co.healthBase, hp);
            r += buf;
            if (ok) pass++; else fail++;
        }

        // AnimClass chain
        if (co.animClassOffset >= 0) {
            uintptr_t animClass = 0, charMov = 0;
            Memory::Read(ptr + co.animClassOffset, animClass);
            bool ok = false;
            if (animClass > 0x10000 && animClass < 0x00007FFFFFFFFFFF) {
                Memory::Read(animClass + co.charMovementOffset, charMov);
                if (charMov > 0x10000 && charMov < 0x00007FFFFFFFFFFF) {
                    float wx = 0;
                    Memory::Read(charMov + co.writablePosOffset + co.writablePosVecOffset, wx);
                    ok = (wx != 0.f); // writable position should match cached
                }
            }
            snprintf(buf, sizeof(buf), "\n  %s writePos chain: anim=0x%llX charMov=0x%llX",
                     ok ? "PASS" : "FAIL",
                     (unsigned long long)animClass, (unsigned long long)charMov);
            r += buf;
            if (ok) pass++; else fail++;
        } else {
            r += "\n  SKIP writePos chain (animClassOffset unknown)";
            skip++;
        }

        snprintf(buf, sizeof(buf), "\n--- %d PASS | %d FAIL | %d SKIP ---", pass, fail, skip);
        r += buf;
        return r;
    });

    // /scan <charptr> [start] [end] — Scan character memory for pointers/values
    Register("scan", "Scan char struct for pointers (/scan <addr> [start] [end])", [](const CommandArgs& args) -> std::string {
        if (args.args.empty()) return "Usage: /scan <hex_addr> [start_offset=0] [end_offset=0x200]";

        uintptr_t addr = 0;
        try { addr = std::stoull(args.args[0], nullptr, 16); }
        catch (...) { return "Invalid hex address."; }

        int startOff = 0, endOff = 0x200;
        if (args.args.size() >= 2)
            try { startOff = std::stoi(args.args[1], nullptr, 16); } catch (...) {}
        if (args.args.size() >= 3)
            try { endOff = std::stoi(args.args[2], nullptr, 16); } catch (...) {}

        if (endOff > 0x1000) endOff = 0x1000;
        if (endOff - startOff > 0x400) endOff = startOff + 0x400; // Max 64 lines

        std::string r = "--- Pointer scan 0x" + args.args[0] + " ---";
        char buf[256];

        for (int off = startOff; off < endOff; off += 8) {
            uintptr_t val = 0;
            if (!Memory::Read(addr + off, val)) {
                snprintf(buf, sizeof(buf), "\n  +0x%03X: READ FAIL", off);
                r += buf;
                break;
            }

            // Classify the value
            const char* tag = "";
            if (val == 0) {
                tag = "(null)";
            } else if (val > 0x10000 && val < 0x00007FFFFFFFFFFF) {
                // Looks like a pointer — try to read a string at val+0x28 (GameData name)
                std::string name = SpawnManager::ReadKenshiString(val + 0x28);
                if (!name.empty() && name.length() > 1 && name.length() < 100) {
                    snprintf(buf, sizeof(buf), "\n  +0x%03X: 0x%012llX  PTR -> name='%s'",
                             off, (unsigned long long)val, name.c_str());
                    r += buf;
                    continue;
                }
                // Try reading name at val+0x10 (Kenshi std::string at different layout)
                name = SpawnManager::ReadKenshiString(val + 0x10);
                if (!name.empty() && name.length() > 1 && name.length() < 100) {
                    snprintf(buf, sizeof(buf), "\n  +0x%03X: 0x%012llX  PTR -> +10='%s'",
                             off, (unsigned long long)val, name.c_str());
                    r += buf;
                    continue;
                }
                tag = "PTR";
            } else {
                // Try interpreting as float pair
                float f1 = 0, f2 = 0;
                memcpy(&f1, &val, 4);
                memcpy(&f2, reinterpret_cast<const char*>(&val) + 4, 4);
                if (std::abs(f1) > 0.001f && std::abs(f1) < 1e6f &&
                    std::abs(f2) > 0.001f && std::abs(f2) < 1e6f) {
                    snprintf(buf, sizeof(buf), "\n  +0x%03X: 0x%016llX  float(%.2f, %.2f)",
                             off, (unsigned long long)val, f1, f2);
                    r += buf;
                    continue;
                }
                tag = "";
            }

            snprintf(buf, sizeof(buf), "\n  +0x%03X: 0x%016llX  %s",
                     off, (unsigned long long)val, tag);
            r += buf;
        }
        return r;
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

    // ── Pipeline debugger ──
    Register("pipeline", "Pipeline debugger (/pipeline [status|entity <id>])",
        [](const CommandArgs& args) -> std::string {
            auto& pipe = Core::Get().GetPipelineOrch();

            if (args.args.empty()) {
                pipe.ToggleHud();
                return pipe.IsHudVisible() ? "Pipeline HUD enabled." : "Pipeline HUD disabled.";
            }

            if (args.args[0] == "status") {
                return pipe.FormatStatusDump();
            }

            if (args.args[0] == "entity" && args.args.size() >= 2) {
                try {
                    EntityID eid = static_cast<EntityID>(std::stoul(args.args[1]));
                    return pipe.FormatEntityTrack(eid);
                } catch (...) {
                    return "Invalid entity ID. Usage: /pipeline entity <id>";
                }
            }

            return "Usage: /pipeline [status|entity <id>]";
        });

    // /discover — Runtime offset discovery using anchor-based scanning
    // Scans the character struct to FIND where fields actually live,
    // instead of assuming hardcoded GOG offsets are correct.
    Register("discover", "Discover character offsets by scanning struct memory", [](const CommandArgs& args) -> std::string {
        auto& core = Core::Get();
        void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
        if (!primaryChar) return "No primary character — load a game first.";

        uintptr_t charPtr = reinterpret_cast<uintptr_t>(primaryChar);
        auto& co = game::GetOffsets().character;
        char buf[512];
        std::string r = "--- Offset Discovery ---";
        snprintf(buf, sizeof(buf), "\n  Character at 0x%012llX", (unsigned long long)charPtr);
        r += buf;

        // ══════════════════════════════════════════════════════════════
        //  STEP 0: Hex dump first 0x500 bytes to LOG FILE for analysis
        // ══════════════════════════════════════════════════════════════
        spdlog::info("=== CHARACTER STRUCT HEX DUMP (0x500 bytes) at 0x{:012X} ===", charPtr);
        for (int row = 0; row < 0x500; row += 16) {
            char hexLine[128] = {};
            char asciiLine[20] = {};
            int pos = 0;
            for (int b = 0; b < 16; b++) {
                uint8_t byte = 0;
                Memory::Read(charPtr + row + b, byte);
                pos += sprintf_s(hexLine + pos, sizeof(hexLine) - pos, "%02X ", byte);
                asciiLine[b] = (byte >= 0x20 && byte <= 0x7E) ? (char)byte : '.';
            }
            asciiLine[16] = '\0';
            spdlog::info("  +{:03X}: {} |{}|", row, hexLine, asciiLine);
        }
        spdlog::info("=== END HEX DUMP ===");
        r += "\n  Hex dump (0x500 bytes) written to log file.";

        // Helper: check if value looks like a valid heap pointer
        uintptr_t modBase = Memory::GetModuleBase();
        auto isHeapPtr = [modBase](uintptr_t val) -> bool {
            if (val < 0x10000 || val >= 0x00007FFFFFFFFFFF) return false;
            if ((val & 0x7) != 0) return false;
            if (val >= modBase && val < modBase + 0x4000000) return false;
            return true;
        };

        // Read known anchor: position from the existing accessor
        game::CharacterAccessor accessor(primaryChar);
        Vec3 knownPos = accessor.GetPosition();
        std::string knownName = accessor.GetName();

        snprintf(buf, sizeof(buf), "\n  Known position: (%.1f, %.1f, %.1f)", knownPos.x, knownPos.y, knownPos.z);
        r += buf;
        r += "\n  Known name: " + knownName;

        int matches = 0, mismatches = 0;

        // ══════════════════════════════════════════════════════════════
        //  STEP 1: Scan for POSITION (3 consecutive floats)
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Position Scan ---";
        bool posNonZero = (knownPos.x != 0.f || knownPos.y != 0.f || knownPos.z != 0.f);
        int discoveredPosOffset = -1;
        if (posNonZero) {
            for (int off = 0; off < 0x400; off += 4) {
                float fx = 0, fy = 0, fz = 0;
                Memory::Read(charPtr + off, fx);
                Memory::Read(charPtr + off + 4, fy);
                Memory::Read(charPtr + off + 8, fz);
                if (std::abs(fx - knownPos.x) < 0.5f &&
                    std::abs(fy - knownPos.y) < 0.5f &&
                    std::abs(fz - knownPos.z) < 0.5f) {
                    snprintf(buf, sizeof(buf), "\n  FOUND position at +0x%03X (%.1f, %.1f, %.1f)", off, fx, fy, fz);
                    r += buf;
                    spdlog::info("DISCOVER: Position at +0x{:03X} = ({:.1f}, {:.1f}, {:.1f})", off, fx, fy, fz);
                    if (discoveredPosOffset == -1) discoveredPosOffset = off;
                }
            }
            if (discoveredPosOffset == co.position) {
                r += "\n  OK: Matches hardcoded +0x" + std::to_string(co.position);
                matches++;
            } else if (discoveredPosOffset >= 0) {
                snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.position, discoveredPosOffset);
                r += buf;
                mismatches++;
            } else {
                r += "\n  NOT FOUND";
                mismatches++;
            }
        } else {
            r += "\n  SKIP (position is zero — character may not be loaded)";
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 2: Scan for NAME (MSVC std::string pattern)
        //  Look for size/capacity pair where size matches known name length
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Name Scan ---";
        int discoveredNameOffset = -1;
        if (!knownName.empty() && knownName != "Unknown") {
            uint64_t expectedSize = knownName.size();
            for (int off = 0; off < 0x400; off += 8) {
                uint64_t size = 0, capacity = 0;
                Memory::Read(charPtr + off + 0x10, size);
                Memory::Read(charPtr + off + 0x18, capacity);
                if (size != expectedSize || capacity < size || capacity > 256) continue;

                // Try reading the actual string to verify
                char testBuf[257] = {};
                bool readable = true;
                if (capacity <= 15) {
                    // SSO: inline
                    for (size_t i = 0; i < size && i < 256; i++) {
                        if (!Memory::Read(charPtr + off + i, testBuf[i])) { readable = false; break; }
                    }
                } else {
                    // Heap: pointer at +0x00
                    uintptr_t dataPtr = 0;
                    Memory::Read(charPtr + off, dataPtr);
                    if (dataPtr < 0x10000 || dataPtr >= 0x00007FFFFFFFFFFF) continue;
                    for (size_t i = 0; i < size && i < 256; i++) {
                        if (!Memory::Read(dataPtr + i, testBuf[i])) { readable = false; break; }
                    }
                }
                if (!readable) continue;
                std::string foundName(testBuf, (size_t)size);
                if (foundName == knownName) {
                    snprintf(buf, sizeof(buf), "\n  FOUND name at +0x%03X = '%s'", off, foundName.c_str());
                    r += buf;
                    spdlog::info("DISCOVER: Name at +0x{:03X} = '{}'", off, foundName);
                    if (discoveredNameOffset == -1) discoveredNameOffset = off;
                }
            }
            if (discoveredNameOffset == co.name) {
                matches++;
                r += "\n  OK: Matches hardcoded";
            } else if (discoveredNameOffset >= 0) {
                snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.name, discoveredNameOffset);
                r += buf;
                mismatches++;
            } else {
                r += "\n  NOT FOUND";
                mismatches++;
            }
        } else {
            r += "\n  SKIP (name unknown)";
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 3: Scan for FACTION (valid heap pointer with name at +0x10)
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Faction Pointer Scan ---";
        int discoveredFactionOffset = -1;
        for (int off = 0; off < 0x100; off += 8) {
            uintptr_t candidate = 0;
            Memory::Read(charPtr + off, candidate);
            if (!isHeapPtr(candidate)) continue;

            // A faction should have a readable name at +0x10
            uint64_t fNameSize = 0, fNameCap = 0;
            Memory::Read(candidate + 0x10 + 0x10, fNameSize);
            Memory::Read(candidate + 0x10 + 0x18, fNameCap);
            if (fNameSize < 1 || fNameSize > 100 || fNameCap < fNameSize || fNameCap > 256) continue;

            // Read the faction name
            char fNameBuf[101] = {};
            bool fReadable = true;
            if (fNameCap <= 15) {
                for (size_t i = 0; i < fNameSize && i < 100; i++)
                    if (!Memory::Read(candidate + 0x10 + i, fNameBuf[i])) { fReadable = false; break; }
            } else {
                uintptr_t fDataPtr = 0;
                Memory::Read(candidate + 0x10, fDataPtr);
                if (fDataPtr < 0x10000) continue;
                for (size_t i = 0; i < fNameSize && i < 100; i++)
                    if (!Memory::Read(fDataPtr + i, fNameBuf[i])) { fReadable = false; break; }
            }
            if (!fReadable) continue;

            // Validate: name should be printable ASCII
            bool allAscii = true;
            for (size_t i = 0; i < fNameSize; i++)
                if (fNameBuf[i] < 0x20 || fNameBuf[i] > 0x7E) { allAscii = false; break; }
            if (!allAscii) continue;

            snprintf(buf, sizeof(buf), "\n  FOUND faction ptr at +0x%03X -> 0x%llX name='%s'",
                     off, (unsigned long long)candidate, fNameBuf);
            r += buf;
            spdlog::info("DISCOVER: Faction at +0x{:03X} -> 0x{:X} name='{}'", off, candidate, fNameBuf);

            // Also try reading faction ID at candidate+0x08
            uint32_t factionId = 0;
            Memory::Read(candidate + game::GetOffsets().faction.id, factionId);
            snprintf(buf, sizeof(buf), " (id=%u)", factionId);
            r += buf;

            if (discoveredFactionOffset == -1) discoveredFactionOffset = off;
        }
        if (discoveredFactionOffset == co.faction) {
            matches++;
            r += "\n  OK: Matches hardcoded +0x" + std::to_string(co.faction);
        } else if (discoveredFactionOffset >= 0) {
            snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.faction, discoveredFactionOffset);
            r += buf;
            mismatches++;
        } else {
            r += "\n  NOT FOUND (no pointer with readable name at +0x10)";
            mismatches++;
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 4: Scan for GAMEDATA (pointer to object with name at +0x28)
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- GameData Pointer Scan ---";
        int discoveredGDOffset = -1;
        for (int off = 0; off < 0x200; off += 8) {
            uintptr_t candidate = 0;
            Memory::Read(charPtr + off, candidate);
            if (!isHeapPtr(candidate)) continue;

            std::string gdName = SpawnManager::ReadKenshiString(candidate + 0x28);
            if (gdName.empty() || gdName.size() < 2 || gdName.size() > 64) continue;

            // Validate: printable ASCII
            bool ok = true;
            for (char c : gdName) if (c < 0x20 || c > 0x7E) { ok = false; break; }
            if (!ok) continue;

            // Check for GameDataManager* at candidate+0x10 (should be consistent)
            uintptr_t gdmPtr = 0;
            Memory::Read(candidate + 0x10, gdmPtr);

            snprintf(buf, sizeof(buf), "\n  FOUND GameData at +0x%03X -> 0x%llX name='%s' mgr=0x%llX",
                     off, (unsigned long long)candidate, gdName.c_str(), (unsigned long long)gdmPtr);
            r += buf;
            spdlog::info("DISCOVER: GameData at +0x{:03X} -> 0x{:X} name='{}' mgr=0x{:X}", off, candidate, gdName, gdmPtr);
            if (discoveredGDOffset == -1) discoveredGDOffset = off;
        }
        if (discoveredGDOffset == co.gameDataPtr) {
            matches++;
            r += "\n  OK: Matches hardcoded";
        } else if (discoveredGDOffset >= 0) {
            snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.gameDataPtr, discoveredGDOffset);
            r += buf;
            mismatches++;
        } else {
            r += "\n  NOT FOUND";
            mismatches++;
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 5: Scan for HEALTH CHAIN
        //  Follow every pointer in the struct, then follow sub-pointers,
        //  looking for arrays of floats near 100.0
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Health Chain Discovery ---";
        int discoveredHC1 = -1, discoveredHC2 = -1, discoveredHBase = -1;
        spdlog::info("DISCOVER: Starting health chain scan...");

        for (int off1 = 0x100; off1 < 0x400; off1 += 8) {
            uintptr_t ptr1 = 0;
            Memory::Read(charPtr + off1, ptr1);
            if (!isHeapPtr(ptr1)) continue;

            // Follow sub-pointers from ptr1
            for (int off2 = 0; off2 < 0x800; off2 += 8) {
                uintptr_t ptr2 = 0;
                if (!Memory::Read(ptr1 + off2, ptr2)) continue;
                if (!isHeapPtr(ptr2)) continue;

                // Look for 7 consecutive floats near 100.0 (full health character)
                for (int base = 0; base < 0x200; base += 4) {
                    int goodCount = 0;
                    for (int part = 0; part < 7; part++) {
                        float hp = 0;
                        if (!Memory::Read(ptr2 + base + part * 8, hp)) break;
                        // Health values for a loaded char are typically 50-200 range
                        if (hp > 10.f && hp < 300.f) goodCount++;
                    }
                    if (goodCount >= 5) {
                        // Found a candidate! Read all 7 values
                        float healthVals[7] = {};
                        for (int p = 0; p < 7; p++)
                            Memory::Read(ptr2 + base + p * 8, healthVals[p]);

                        snprintf(buf, sizeof(buf),
                                 "\n  FOUND health chain: +0x%03X -> +0x%03X -> +0x%03X"
                                 "\n    [%.0f, %.0f, %.0f, %.0f, %.0f, %.0f, %.0f]",
                                 off1, off2, base,
                                 healthVals[0], healthVals[1], healthVals[2], healthVals[3],
                                 healthVals[4], healthVals[5], healthVals[6]);
                        r += buf;
                        spdlog::info("DISCOVER: Health chain +0x{:03X} -> +0x{:03X} -> +0x{:03X} = "
                                     "[{:.0f}, {:.0f}, {:.0f}, {:.0f}, {:.0f}, {:.0f}, {:.0f}]",
                                     off1, off2, base,
                                     healthVals[0], healthVals[1], healthVals[2], healthVals[3],
                                     healthVals[4], healthVals[5], healthVals[6]);

                        if (discoveredHC1 == -1) {
                            discoveredHC1 = off1;
                            discoveredHC2 = off2;
                            discoveredHBase = base;
                        }
                    }
                }
            }
        }

        if (discoveredHC1 == co.healthChain1 && discoveredHC2 == co.healthChain2 && discoveredHBase == co.healthBase) {
            matches++;
            r += "\n  OK: Matches hardcoded chain (0x2B8->0x5F8->0x40)";
        } else if (discoveredHC1 >= 0) {
            snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=(0x%X->0x%X->0x%X), Found=(0x%X->0x%X->0x%X)",
                     co.healthChain1, co.healthChain2, co.healthBase,
                     discoveredHC1, discoveredHC2, discoveredHBase);
            r += buf;
            mismatches++;
        } else {
            r += "\n  NOT FOUND (no 7-float health array discovered)";
            mismatches++;
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 6: Scan for INVENTORY pointer
        //  Valid heap pointer, with owner backpointer and item count
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Inventory Pointer Scan ---";
        int discoveredInvOffset = -1;
        for (int off = 0x200; off < 0x400; off += 8) {
            uintptr_t candidate = 0;
            Memory::Read(charPtr + off, candidate);
            if (!isHeapPtr(candidate)) continue;

            // Inventory should have: items ptr at +0x10, itemCount at +0x18
            // and owner backpointer at +0x28 should point back to our character
            uintptr_t ownerPtr = 0;
            Memory::Read(candidate + 0x28, ownerPtr);
            if (ownerPtr != charPtr) continue;  // Owner must be THIS character

            int itemCount = 0;
            Memory::Read(candidate + 0x18, itemCount);
            if (itemCount < 0 || itemCount > 10000) continue;

            snprintf(buf, sizeof(buf), "\n  FOUND inventory at +0x%03X -> 0x%llX (owner=self, items=%d)",
                     off, (unsigned long long)candidate, itemCount);
            r += buf;
            spdlog::info("DISCOVER: Inventory at +0x{:03X} -> 0x{:X} (items={})", off, candidate, itemCount);
            if (discoveredInvOffset == -1) discoveredInvOffset = off;
        }
        if (discoveredInvOffset == co.inventory) {
            matches++;
            r += "\n  OK: Matches hardcoded";
        } else if (discoveredInvOffset >= 0) {
            snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.inventory, discoveredInvOffset);
            r += buf;
            mismatches++;
        } else {
            // Try without owner check (owner offset might be different)
            r += "\n  NOT FOUND with owner check. Scanning without...";
            for (int off = 0x200; off < 0x400; off += 8) {
                uintptr_t candidate = 0;
                Memory::Read(charPtr + off, candidate);
                if (!isHeapPtr(candidate)) continue;

                // Look for reasonable item count at any nearby offset
                for (int countOff = 0x10; countOff <= 0x30; countOff += 8) {
                    int itemCount = 0;
                    Memory::Read(candidate + countOff, itemCount);
                    if (itemCount >= 0 && itemCount < 500) {
                        // Check if items pointer looks valid
                        uintptr_t itemsPtr = 0;
                        Memory::Read(candidate + countOff - 8, itemsPtr);
                        if (itemCount == 0 || isHeapPtr(itemsPtr)) {
                            snprintf(buf, sizeof(buf), "\n  CANDIDATE inv at +0x%03X -> 0x%llX (count=%d at +0x%02X)",
                                     off, (unsigned long long)candidate, itemCount, countOff);
                            r += buf;
                            spdlog::info("DISCOVER: Inventory candidate at +0x{:03X} -> 0x{:X} (count={} at +0x{:02X})",
                                         off, candidate, itemCount, countOff);
                            if (discoveredInvOffset == -1) discoveredInvOffset = off;
                            break;
                        }
                    }
                }
            }
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 7: Scan for STATS (inline block of floats in skill range)
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Stats Block Scan ---";
        int discoveredStatsOffset = -1;
        for (int off = 0x300; off < 0x500; off += 4) {
            // Check 10 consecutive floats in reasonable stat range (1-100)
            int statCount = 0;
            for (int s = 0; s < 10; s++) {
                float val = 0;
                Memory::Read(charPtr + off + s * 4, val);
                if (val >= 1.f && val <= 100.f) statCount++;
            }
            if (statCount >= 7) {
                // Read first 5 stats for display
                float s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
                Memory::Read(charPtr + off, s0);
                Memory::Read(charPtr + off + 4, s1);
                Memory::Read(charPtr + off + 8, s2);
                Memory::Read(charPtr + off + 12, s3);
                Memory::Read(charPtr + off + 16, s4);
                snprintf(buf, sizeof(buf), "\n  FOUND stats block at +0x%03X [%.1f, %.1f, %.1f, %.1f, %.1f, ...]",
                         off, s0, s1, s2, s3, s4);
                r += buf;
                spdlog::info("DISCOVER: Stats block at +0x{:03X} [{:.1f}, {:.1f}, {:.1f}, {:.1f}, {:.1f}]",
                             off, s0, s1, s2, s3, s4);
                if (discoveredStatsOffset == -1) discoveredStatsOffset = off;
            }
        }
        if (discoveredStatsOffset == co.stats) {
            matches++;
            r += "\n  OK: Matches hardcoded +0x450";
        } else if (discoveredStatsOffset >= 0) {
            snprintf(buf, sizeof(buf), "\n  MISMATCH: Hardcoded=+0x%03X, Found=+0x%03X", co.stats, discoveredStatsOffset);
            r += buf;
            mismatches++;
        } else {
            r += "\n  NOT FOUND";
        }

        // ══════════════════════════════════════════════════════════════
        //  STEP 8: Scan for MONEY CHAIN
        // ══════════════════════════════════════════════════════════════
        r += "\n\n  --- Money Chain Scan ---";
        int knownMoney = accessor.GetMoney();
        snprintf(buf, sizeof(buf), "\n  Current money (via hardcoded chain): %d cats", knownMoney);
        r += buf;

        // Also dump raw pointers at the hardcoded offsets for debugging
        {
            uintptr_t mc1 = 0, mc2 = 0;
            int moneyVal = 0;
            Memory::Read(charPtr + co.moneyChain1, mc1);
            if (mc1) Memory::Read(mc1 + co.moneyChain2, mc2);
            if (mc2) Memory::Read(mc2 + co.moneyBase, moneyVal);
            snprintf(buf, sizeof(buf), "\n  Chain check: +0x%X->0x%llX, +0x%X->0x%llX, +0x%X->%d",
                     co.moneyChain1, (unsigned long long)mc1,
                     co.moneyChain2, (unsigned long long)mc2,
                     co.moneyBase, moneyVal);
            r += buf;
        }

        // ══════════════════════════════════════════════════════════════
        //  SUMMARY
        // ══════════════════════════════════════════════════════════════
        snprintf(buf, sizeof(buf), "\n\n--- SUMMARY: %d matches | %d mismatches ---", matches, mismatches);
        r += buf;

        if (mismatches > 0) {
            r += "\n  WARNING: Some offsets do not match hardcoded values!";
            r += "\n  Check log file for hex dump and details.";
            r += "\n  Update offsets in game_types.h if mismatches are confirmed.";
        } else if (matches > 0) {
            r += "\n  All verified offsets match hardcoded values.";
        }

        spdlog::info("DISCOVER: Complete — {} matches, {} mismatches", matches, mismatches);
        return r;
    });

    // /discover update — Apply discovered offsets to the live offset table
    Register("discover_apply", "Apply discovered offsets (run /discover first)", [](const CommandArgs& args) -> std::string {
        // This is a placeholder — after /discover confirms correct offsets,
        // this command would update GetOffsets() with the discovered values.
        // For now, users should manually update game_types.h defaults.
        return "Not yet implemented. Run /discover first, check log, then update game_types.h.";
    });

    // /forcespawn — Bypass all gates and force-spawn pending remote characters
    Register("forcespawn", "Force-spawn pending remote characters (bypass gates)", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& sm = core.GetSpawnManager();

        if (!core.IsConnected()) return "Not connected to a server.";
        if (!core.IsGameLoaded()) return "Game not loaded yet.";

        size_t pending = sm.GetPendingSpawnCount();

        // If no pending spawns, check for stuck remote entities and re-queue them
        if (pending == 0) {
            auto remoteEntities = core.GetEntityRegistry().GetRemoteEntities();
            int requeued = 0;
            for (auto eid : remoteEntities) {
                if (!core.GetEntityRegistry().GetGameObject(eid)) {
                    auto infoCopy = core.GetEntityRegistry().GetInfoCopy(eid);
                    if (infoCopy) {
                        SpawnRequest req;
                        req.netId = eid;
                        req.owner = infoCopy->ownerPlayerId;
                        req.type = infoCopy->type;
                        req.position = infoCopy->lastPosition;
                        sm.QueueSpawn(req);
                        requeued++;
                    }
                }
            }
            if (requeued > 0) {
                pending = requeued;
            } else {
                return "No pending spawns and no stuck remote entities.";
            }
        }

        if (!sm.IsReady()) {
            // Try to use ForceSpawnRemotePlayers which sets the bypass flag
            core.ForceSpawnRemotePlayers();
            return "SpawnManager not fully ready — forcing bypass for next tick.";
        }

        int spawned = 0, failed = 0;
        for (size_t i = 0; i < pending && i < 16; i++) {
            SpawnRequest req;
            if (!sm.PopNextSpawn(req)) break;

            // Map owner to mod template slot
            int templateCount = sm.GetModTemplateCount();
            int modSlot = 0;
            if (templateCount > 0 && req.owner > 0) {
                modSlot = (static_cast<int>(req.owner) - 1) % templateCount;
            }
            if (modSlot < 0 || modSlot >= templateCount) modSlot = 0;

            // Try mod template first, then createRandomChar fallback
            void* newChar = nullptr;
            if (templateCount > 0) {
                newChar = sm.SpawnCharacterDirect(&req.position, modSlot);
            }
            if (!newChar) {
                newChar = entity_hooks::CallFactoryCreateRandom(sm.GetFactory());
            }

            uintptr_t addr = reinterpret_cast<uintptr_t>(newChar);
            if (newChar && addr > 0x10000 && addr < 0x00007FFFFFFFFFFF && (addr & 0x7) == 0) {
                core.GetEntityRegistry().SetGameObject(req.netId, newChar);
                core.GetEntityRegistry().UpdatePosition(req.netId, req.position);

                // Full post-spawn setup (position, rename, AI suppress, faction fix)
                game::CharacterAccessor accessor(newChar);
                if (req.position.x != 0.f || req.position.y != 0.f || req.position.z != 0.f) {
                    accessor.WritePosition(req.position);
                }

                // Fix faction pointer to prevent crash on faction+0x250 access
                uintptr_t localFaction = entity_hooks::GetEarlyPlayerFaction();
                if (localFaction == 0) localFaction = entity_hooks::GetFallbackFaction();
                if (localFaction != 0) {
                    accessor.WriteFaction(localFaction);
                }

                core.GetPlayerController().OnRemoteCharacterSpawned(req.netId, newChar, req.owner);
                ai_hooks::MarkRemoteControlled(newChar);
                game::ScheduleDeferredAnimClassProbe(addr);

                spdlog::info("forcespawn: Spawned entity {} at 0x{:X}", req.netId, addr);
                spawned++;
            } else {
                spdlog::warn("forcespawn: All spawn methods returned null for entity {}", req.netId);
                failed++;
            }
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "Force-spawned %d characters (%d failed, %d remaining)",
                 spawned, failed, (int)sm.GetPendingSpawnCount());
        return buf;
    });

    // /fulldiag — Comprehensive diagnostic dump (all systems)
    Register("fulldiag", "Full diagnostic dump of all systems", [](const CommandArgs&) -> std::string {
        auto& core = Core::Get();
        auto& sm = core.GetSpawnManager();
        auto& reg = core.GetEntityRegistry();
        auto& orch = core.GetLoadingOrch();
        char buf[256];
        std::string r;

        // ── Connection ──
        r += "=== CONNECTION ===";
        snprintf(buf, sizeof(buf), "\n  Connected: %s  |  Player ID: %u  |  Game loaded: %s",
                 core.IsConnected() ? "YES" : "NO",
                 core.GetLocalPlayerId(),
                 core.IsGameLoaded() ? "YES" : "NO");
        r += buf;

        // ── Entity Registry ──
        r += "\n=== ENTITIES ===";
        size_t total = reg.GetEntityCount();
        size_t remote = reg.GetRemoteCount();
        size_t spawned = reg.GetSpawnedRemoteCount();
        snprintf(buf, sizeof(buf), "\n  Total: %d  |  Remote: %d  |  Remote spawned: %d",
                 (int)total, (int)remote, (int)spawned);
        r += buf;

        // ── Spawn System ──
        r += "\n=== SPAWN SYSTEM ===";
        snprintf(buf, sizeof(buf), "\n  Factory: %s  |  PreCall: %s  |  Pending: %d",
                 sm.IsReady() ? "YES" : "NO",
                 sm.HasPreCallData() ? "YES" : "NO",
                 (int)sm.GetPendingSpawnCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  Templates: %d total, %d char, %d factory",
                 (int)sm.GetTemplateCount(),
                 (int)sm.GetCharacterTemplateCount(),
                 (int)sm.GetFactoryTemplateCount());
        r += buf;
        snprintf(buf, sizeof(buf), "\n  In-place spawns: %d", entity_hooks::GetInPlaceSpawnCount());
        r += buf;

        // ── Spawn Gate ──
        r += "\n=== SPAWN GATE ===";
        const char* phaseName = "?";
        switch (orch.GetPhase()) {
            case LoadingPhase::Idle: phaseName = "Idle"; break;
            case LoadingPhase::InitialLoad: phaseName = "InitialLoad"; break;
            case LoadingPhase::ZoneTransition: phaseName = "ZoneTransition"; break;
            case LoadingPhase::SpawnLoad: phaseName = "SpawnLoad"; break;
        }
        snprintf(buf, sizeof(buf), "\n  Phase: %s  |  GameLoaded: %s  |  Burst: %s  |  CanSpawn: %s",
                 phaseName,
                 orch.IsGameLoaded() ? "Y" : "N",
                 orch.IsInBurst() ? "Y" : "N",
                 AssetFacilitator::Get().CanSpawn() ? "Y" : "N");
        r += buf;
        std::string blockReason = orch.GetSpawnBlockReason();
        snprintf(buf, sizeof(buf), "\n  Block reason: %s", blockReason.c_str());
        r += buf;

        // ── Hooks ──
        r += "\n=== HOOKS ===";
        auto diags = HookManager::Get().GetDiagnostics();
        int active = 0, movrax = 0;
        for (auto& d : diags) {
            if (d.enabled) active++;
            if (d.prologue[0] == 0x48 && d.prologue[1] == 0x8B && d.prologue[2] == 0xC4) movrax++;
        }
        snprintf(buf, sizeof(buf), "\n  Total: %d  |  Active: %d  |  MovRaxRsp: %d",
                 (int)diags.size(), active, movrax);
        r += buf;

        // Show any crashed hooks
        for (auto& d : diags) {
            if (d.crashCount > 0) {
                snprintf(buf, sizeof(buf), "\n  CRASHED: %s (crashes: %d)", d.name.c_str(), d.crashCount);
                r += buf;
            }
        }

        // ── Primary Character ──
        r += "\n=== PRIMARY CHAR ===";
        void* primary = core.GetPlayerController().GetPrimaryCharacter();
        if (primary) {
            uintptr_t p = reinterpret_cast<uintptr_t>(primary);
            float px = 0, py = 0, pz = 0;
            uintptr_t fac = 0;
            Memory::Read(p + 0x48, px); Memory::Read(p + 0x4C, py); Memory::Read(p + 0x50, pz);
            Memory::Read(p + 0x10, fac);
            snprintf(buf, sizeof(buf), "\n  Addr: 0x%llX  Pos: (%.0f, %.0f, %.0f)  Faction: 0x%llX",
                     (unsigned long long)p, px, py, pz, (unsigned long long)fac);
            r += buf;
        } else {
            r += "\n  (none)";
        }

        r += "\n--- Use /forcespawn to bypass gates ---";
        return r;
    });

    // /instance — Launch a second Kenshi instance for testing multiplayer
    Register("instance", "Launch a second Kenshi instance (bypasses Steam single-instance lock)", [](const CommandArgs&) -> std::string {
        // Find kenshi_x64.exe relative to our DLL
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, modulePath, MAX_PATH);

        // Launch with -nosteam flag to bypass Steam's single-instance check
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        std::string cmdLine = std::string("\"") + modulePath + "\" -nosteam";
        if (CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                           0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return "Launched second Kenshi instance (PID: " + std::to_string(pi.dwProcessId) + ")";
        }

        // Fallback: try without -nosteam
        cmdLine = std::string("\"") + modulePath + "\"";
        if (CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                           0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return "Launched second Kenshi instance (PID: " + std::to_string(pi.dwProcessId) + ")";
        }

        return "Failed to launch Kenshi: " + std::to_string(GetLastError());
    });

    // /syncstatus — Show shared-save sync status
    Register("syncstatus", "Show shared-save sync status", [](const CommandArgs&) -> std::string {
        std::string r = "=== SHARED-SAVE SYNC ===";
        r += "\nOwn character: " + shared_save_sync::GetOwnCharacterName();
        r += " — " + std::string(shared_save_sync::IsOwnCharacterFound() ? "FOUND" : "searching...");
        r += "\nOther character: " + shared_save_sync::GetOtherCharacterName();
        r += " — " + std::string(shared_save_sync::IsOtherCharacterFound() ? "FOUND" : "searching...");
        r += "\nTracked characters: " + std::to_string(char_tracker_hooks::GetTrackedCount());
        return r;
    });

    spdlog::info("CommandRegistry: {} built-in commands registered", GetAll().size());
}

} // namespace kmp

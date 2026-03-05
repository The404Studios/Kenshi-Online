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

        // Loading cache
        auto& cache = entity_hooks::GetLoadingCharacters();
        snprintf(buf, sizeof(buf), "\n--- Loading cache: %d characters ---", (int)cache.size());
        r += buf;

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

    spdlog::info("CommandRegistry: {} built-in commands registered", GetAll().size());
}

} // namespace kmp

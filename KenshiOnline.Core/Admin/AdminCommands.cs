using System;
using System.Collections.Generic;
using System.Linq;
using KenshiOnline.Core.Entities;
using KenshiOnline.Core.Synchronization;
using KenshiOnline.Core.Session;

namespace KenshiOnline.Core.Admin
{
    /// <summary>
    /// Command result
    /// </summary>
    public class CommandResult
    {
        public bool Success { get; set; }
        public string Message { get; set; }
        public Dictionary<string, object> Data { get; set; }

        public CommandResult()
        {
            Data = new Dictionary<string, object>();
        }

        public static CommandResult Ok(string message = "Command executed successfully")
        {
            return new CommandResult { Success = true, Message = message };
        }

        public static CommandResult Error(string message)
        {
            return new CommandResult { Success = false, Message = message };
        }
    }

    /// <summary>
    /// Admin command handler
    /// </summary>
    public class AdminCommands
    {
        private readonly EntityManager _entityManager;
        private readonly SessionManager _sessionManager;
        private readonly WorldStateManager _worldStateManager;
        private readonly CombatSync _combatSync;
        private readonly InventorySync _inventorySync;

        private readonly Dictionary<string, Func<string[], string, CommandResult>> _commands;

        public AdminCommands(
            EntityManager entityManager,
            SessionManager sessionManager,
            WorldStateManager worldStateManager,
            CombatSync combatSync,
            InventorySync inventorySync)
        {
            _entityManager = entityManager;
            _sessionManager = sessionManager;
            _worldStateManager = worldStateManager;
            _combatSync = combatSync;
            _inventorySync = inventorySync;

            _commands = new Dictionary<string, Func<string[], string, CommandResult>>();
            RegisterCommands();
        }

        #region Command Registration

        private void RegisterCommands()
        {
            // Player management
            _commands["kick"] = CmdKick;
            _commands["ban"] = CmdBan;
            _commands["setadmin"] = CmdSetAdmin;
            _commands["teleport"] = CmdTeleport;
            _commands["heal"] = CmdHeal;
            _commands["kill"] = CmdKill;

            // World management
            _commands["settime"] = CmdSetTime;
            _commands["setspeed"] = CmdSetSpeed;
            _commands["pause"] = CmdPause;
            _commands["unpause"] = CmdUnpause;
            _commands["setweather"] = CmdSetWeather;
            _commands["nextday"] = CmdNextDay;

            // Entity spawning
            _commands["spawnitem"] = CmdSpawnItem;
            _commands["spawnnpc"] = CmdSpawnNPC;

            // Server info
            _commands["stats"] = CmdStats;
            _commands["list"] = CmdList;
            _commands["info"] = CmdInfo;
            _commands["help"] = CmdHelp;

            // Debug
            _commands["debug"] = CmdDebug;
            _commands["clear"] = CmdClear;
        }

        #endregion

        #region Command Execution

        /// <summary>
        /// Execute command
        /// </summary>
        public CommandResult ExecuteCommand(string command, string executorPlayerId)
        {
            // Check if executor is admin
            if (!_sessionManager.IsAdmin(executorPlayerId))
            {
                return CommandResult.Error("You do not have admin permissions");
            }

            // Parse command
            var parts = command.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
            {
                return CommandResult.Error("No command specified");
            }

            var cmdName = parts[0].ToLower();
            var args = parts.Skip(1).ToArray();

            // Execute command
            if (_commands.TryGetValue(cmdName, out var cmdFunc))
            {
                try
                {
                    return cmdFunc(args, executorPlayerId);
                }
                catch (Exception ex)
                {
                    return CommandResult.Error($"Command error: {ex.Message}");
                }
            }

            return CommandResult.Error($"Unknown command: {cmdName}");
        }

        #endregion

        #region Player Management Commands

        private CommandResult CmdKick(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: kick <playerId> [reason]");

            var playerId = args[0];
            var reason = args.Length > 1 ? string.Join(" ", args.Skip(1)) : "Kicked by admin";

            if (_sessionManager.KickPlayer(playerId, reason))
            {
                return CommandResult.Ok($"Player {playerId} has been kicked");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        private CommandResult CmdBan(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: ban <playerId> [reason]");

            // TODO: Implement actual ban system with persistence
            var playerId = args[0];
            var reason = args.Length > 1 ? string.Join(" ", args.Skip(1)) : "Banned by admin";

            if (_sessionManager.KickPlayer(playerId, reason))
            {
                return CommandResult.Ok($"Player {playerId} has been banned");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        private CommandResult CmdSetAdmin(string[] args, string executorId)
        {
            if (args.Length < 2)
                return CommandResult.Error("Usage: setadmin <playerId> <true|false>");

            var playerId = args[0];
            var isAdmin = bool.Parse(args[1]);

            if (_sessionManager.SetAdmin(playerId, isAdmin))
            {
                return CommandResult.Ok($"Player {playerId} admin status set to {isAdmin}");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        private CommandResult CmdTeleport(string[] args, string executorId)
        {
            if (args.Length < 4)
                return CommandResult.Error("Usage: teleport <playerId> <x> <y> <z>");

            var playerId = args[0];
            var x = float.Parse(args[1]);
            var y = float.Parse(args[2]);
            var z = float.Parse(args[3]);

            var player = _entityManager.GetPlayerByPlayerId(playerId);
            if (player != null)
            {
                player.Position = new Vector3(x, y, z);
                player.MarkDirty();
                return CommandResult.Ok($"Teleported {playerId} to ({x}, {y}, {z})");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        private CommandResult CmdHeal(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: heal <playerId>");

            var playerId = args[0];
            var player = _entityManager.GetPlayerByPlayerId(playerId);

            if (player != null)
            {
                player.Heal(player.MaxHealth);
                return CommandResult.Ok($"Healed player {playerId}");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        private CommandResult CmdKill(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: kill <playerId>");

            var playerId = args[0];
            var player = _entityManager.GetPlayerByPlayerId(playerId);

            if (player != null)
            {
                player.TakeDamage(player.Health);
                return CommandResult.Ok($"Killed player {playerId}");
            }

            return CommandResult.Error($"Player {playerId} not found");
        }

        #endregion

        #region World Management Commands

        private CommandResult CmdSetTime(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: settime <hour> (0-24)");

            var hour = float.Parse(args[0]);
            _worldStateManager.SetGameTime(hour);

            return CommandResult.Ok($"Game time set to {hour:F1}");
        }

        private CommandResult CmdSetSpeed(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: setspeed <multiplier> (0.1-10)");

            var speed = float.Parse(args[0]);
            _worldStateManager.SetGameSpeed(speed);

            return CommandResult.Ok($"Game speed set to {speed}x");
        }

        private CommandResult CmdPause(string[] args, string executorId)
        {
            _worldStateManager.SetPaused(true);
            return CommandResult.Ok("Game paused");
        }

        private CommandResult CmdUnpause(string[] args, string executorId)
        {
            _worldStateManager.SetPaused(false);
            return CommandResult.Ok("Game unpaused");
        }

        private CommandResult CmdSetWeather(string[] args, string executorId)
        {
            if (args.Length < 1)
                return CommandResult.Error("Usage: setweather <Clear|Cloudy|Foggy|Rainy|Sandstorm|Windy>");

            var weather = args[0];
            _worldStateManager.SetWeather(weather);

            return CommandResult.Ok($"Weather set to {weather}");
        }

        private CommandResult CmdNextDay(string[] args, string executorId)
        {
            _worldStateManager.AdvanceDay();
            return CommandResult.Ok($"Advanced to day {_worldStateManager.State.GameDay}");
        }

        #endregion

        #region Entity Spawning Commands

        private CommandResult CmdSpawnItem(string[] args, string executorId)
        {
            if (args.Length < 5)
                return CommandResult.Error("Usage: spawnitem <name> <type> <x> <y> <z>");

            var name = args[0];
            var type = args[1];
            var x = float.Parse(args[2]);
            var y = float.Parse(args[3]);
            var z = float.Parse(args[4]);

            var item = _entityManager.CreateItem(name, type, new Vector3(x, y, z));
            if (item != null)
            {
                return CommandResult.Ok($"Spawned item {name} at ({x}, {y}, {z})");
            }

            return CommandResult.Error("Failed to spawn item");
        }

        private CommandResult CmdSpawnNPC(string[] args, string executorId)
        {
            if (args.Length < 5)
                return CommandResult.Error("Usage: spawnnpc <name> <type> <x> <y> <z>");

            var name = args[0];
            var type = args[1];
            var x = float.Parse(args[2]);
            var y = float.Parse(args[3]);
            var z = float.Parse(args[4]);

            var npc = _entityManager.CreateNPC(name, type, new Vector3(x, y, z));
            if (npc != null)
            {
                return CommandResult.Ok($"Spawned NPC {name} at ({x}, {y}, {z})");
            }

            return CommandResult.Error("Failed to spawn NPC");
        }

        #endregion

        #region Info Commands

        private CommandResult CmdStats(string[] args, string executorId)
        {
            var stats = _sessionManager.GetStatistics();
            stats["totalEntities"] = _entityManager.TotalEntities;
            stats["playerEntities"] = _entityManager.PlayerCount;
            stats["npcEntities"] = _entityManager.NPCCount;
            stats["itemEntities"] = _entityManager.ItemCount;
            stats["combatEvents"] = _combatSync.TotalCombatEvents;
            stats["inventoryActions"] = _inventorySync.TotalInventoryActions;

            var result = CommandResult.Ok("Server statistics");
            result.Data = stats;
            return result;
        }

        private CommandResult CmdList(string[] args, string executorId)
        {
            var players = _sessionManager.GetPlayerList();
            var result = CommandResult.Ok($"Players online: {players.Count}");
            result.Data["players"] = players;
            return result;
        }

        private CommandResult CmdInfo(string[] args, string executorId)
        {
            var serverInfo = _sessionManager.GetServerInfo();
            var worldState = _worldStateManager.State;

            var result = CommandResult.Ok("Server info");
            result.Data["server"] = serverInfo.Serialize();
            result.Data["world"] = worldState.Serialize();
            result.Data["time"] = _worldStateManager.GetTimeOfDayString();
            result.Data["date"] = _worldStateManager.GetDateString();

            return result;
        }

        private CommandResult CmdHelp(string[] args, string executorId)
        {
            var commands = new List<string>
            {
                "Player Management:",
                "  kick <playerId> [reason] - Kick a player",
                "  ban <playerId> [reason] - Ban a player",
                "  setadmin <playerId> <true|false> - Set admin status",
                "  teleport <playerId> <x> <y> <z> - Teleport player",
                "  heal <playerId> - Heal player to full",
                "  kill <playerId> - Kill player",
                "",
                "World Management:",
                "  settime <hour> - Set game time (0-24)",
                "  setspeed <multiplier> - Set game speed (0.1-10)",
                "  pause - Pause game",
                "  unpause - Unpause game",
                "  setweather <type> - Set weather",
                "  nextday - Advance to next day",
                "",
                "Entity Spawning:",
                "  spawnitem <name> <type> <x> <y> <z> - Spawn item",
                "  spawnnpc <name> <type> <x> <y> <z> - Spawn NPC",
                "",
                "Server Info:",
                "  stats - Show server statistics",
                "  list - List online players",
                "  info - Show server info",
                "  help - Show this help"
            };

            var result = CommandResult.Ok("Available commands");
            result.Data["commands"] = commands;
            return result;
        }

        #endregion

        #region Debug Commands

        private CommandResult CmdDebug(string[] args, string executorId)
        {
            var debug = new Dictionary<string, object>
            {
                ["sessions"] = _sessionManager.TotalSessions,
                ["entities"] = _entityManager.TotalEntities,
                ["players"] = _entityManager.PlayerCount,
                ["npcs"] = _entityManager.NPCCount,
                ["items"] = _entityManager.ItemCount,
                ["worldTime"] = _worldStateManager.State.GameTime,
                ["worldDay"] = _worldStateManager.State.GameDay,
                ["gameSpeed"] = _worldStateManager.State.GameSpeedMultiplier,
                ["paused"] = _worldStateManager.State.PauseGame
            };

            var result = CommandResult.Ok("Debug info");
            result.Data = debug;
            return result;
        }

        private CommandResult CmdClear(string[] args, string executorId)
        {
            // Clear all entities (except players)
            var toRemove = _entityManager.GetAllEntities()
                .Where(e => e.Type != EntityType.Player)
                .Select(e => e.Id)
                .ToList();

            foreach (var id in toRemove)
            {
                _entityManager.UnregisterEntity(id);
            }

            return CommandResult.Ok($"Cleared {toRemove.Count} entities");
        }

        #endregion
    }
}

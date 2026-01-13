using System;
using System.Collections.Generic;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Networking.Authority;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Extensions to EnhancedServer for game state management.
    /// Integrates GameStateManager with server-owned saves via WorldSaveLoader.
    /// </summary>
    public static class ServerExtensions
    {
        private static GameStateManager gameStateManager;

        /// <summary>
        /// Set the game state manager for this server with save system integration.
        /// </summary>
        /// <param name="server">The server instance</param>
        /// <param name="manager">The game state manager</param>
        /// <param name="serverContext">Optional server context for save system integration</param>
        /// <param name="worldId">World ID for save files</param>
        public static void SetGameStateManager(this EnhancedServer server, GameStateManager manager,
            ServerContext serverContext = null, string worldId = "default")
        {
            gameStateManager = manager;

            // Initialize save system if server context provided
            if (serverContext != null && gameStateManager != null)
            {
                gameStateManager.InitializeWithSaveSystem(serverContext, worldId);
                Console.WriteLine($"[ServerExtensions] Save system initialized for world: {worldId}");
            }

            // Subscribe to game state events
            if (gameStateManager != null)
            {
                gameStateManager.OnPlayerJoined += (playerId, playerData) =>
                {
                    Console.WriteLine($"Player {playerId} joined the game");
                    // Broadcast to all clients
                    BroadcastPlayerJoined(server, playerId, playerData);
                };

                gameStateManager.OnPlayerLeft += (playerId) =>
                {
                    Console.WriteLine($"Player {playerId} left the game");
                    BroadcastPlayerLeft(server, playerId);
                };

                gameStateManager.OnPlayerStateChanged += (playerId, playerData) =>
                {
                    // Broadcast state updates
                    BroadcastPlayerState(server, playerId, playerData);
                };

                gameStateManager.OnPlayerSpawnedBroadcast += (playerId, position) =>
                {
                    Console.WriteLine($"Player {playerId} spawned at {position.X}, {position.Y}, {position.Z}");
                    BroadcastPlayerSpawned(server, playerId, position);
                };

                gameStateManager.OnGroupSpawnCompletedBroadcast += (groupId, playerIds) =>
                {
                    Console.WriteLine($"Group spawn {groupId} completed for {playerIds.Count} players");
                    BroadcastGroupSpawnCompleted(server, groupId, playerIds);
                };
            }
        }

        /// <summary>
        /// Get the game state manager
        /// </summary>
        public static GameStateManager GetGameStateManager(this EnhancedServer server)
        {
            return gameStateManager;
        }

        /// <summary>
        /// Handle spawn request from client
        /// </summary>
        public static async void HandleSpawnRequest(this EnhancedServer server, GameMessage message)
        {
            try
            {
                if (gameStateManager == null)
                {
                    Console.WriteLine("ERROR: Game state manager not initialized!");
                    return;
                }

                string playerId = message.PlayerId;
                string location = message.Data.ContainsKey("location") ? message.Data["location"].ToString() : "Hub";

                Console.WriteLine($"Spawn request from {playerId} at {location}");

                // Create player data
                var playerData = new Data.PlayerData
                {
                    PlayerId = playerId,
                    DisplayName = playerId,
                    Health = 100,
                    MaxHealth = 100,
                    CurrentState = PlayerState.Idle
                };

                // Add player to game
                bool success = await gameStateManager.AddPlayer(playerId, playerData, location);

                if (success)
                {
                    Console.WriteLine($"Successfully spawned player {playerId}");
                }
                else
                {
                    Console.WriteLine($"Failed to spawn player {playerId}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR handling spawn request: {ex.Message}");
            }
        }

        /// <summary>
        /// Handle group spawn request from client
        /// </summary>
        public static void HandleGroupSpawnRequest(this EnhancedServer server, GameMessage message)
        {
            try
            {
                if (gameStateManager == null)
                {
                    Console.WriteLine("ERROR: Game state manager not initialized!");
                    return;
                }

                string playerId = message.PlayerId;
                var playerIds = message.Data.ContainsKey("playerIds") ?
                    (List<string>)message.Data["playerIds"] : new List<string> { playerId };
                string location = message.Data.ContainsKey("location") ? message.Data["location"].ToString() : "Hub";

                Console.WriteLine($"Group spawn request from {playerId} for {playerIds.Count} players at {location}");

                string groupId = gameStateManager.RequestGroupSpawn(playerIds, location);

                if (!string.IsNullOrEmpty(groupId))
                {
                    // Send group ID back to clients
                    var response = new GameMessage
                    {
                        Type = MessageType.GroupSpawnCreated,
                        Data = new Dictionary<string, object>
                        {
                            { "groupId", groupId },
                            { "playerIds", playerIds },
                            { "location", location }
                        }
                    };

                    BroadcastMessage(server, response);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR handling group spawn request: {ex.Message}");
            }
        }

        /// <summary>
        /// Handle player ready for group spawn
        /// </summary>
        public static void HandleGroupSpawnReady(this EnhancedServer server, GameMessage message)
        {
            try
            {
                if (gameStateManager == null)
                    return;

                string playerId = message.PlayerId;
                string groupId = message.Data.ContainsKey("groupId") ? message.Data["groupId"].ToString() : "";

                if (!string.IsNullOrEmpty(groupId))
                {
                    Console.WriteLine($"Player {playerId} ready for group spawn {groupId}");
                    gameStateManager.PlayerReadyForGroupSpawn(groupId, playerId);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR handling group spawn ready: {ex.Message}");
            }
        }

        /// <summary>
        /// Stop the server
        /// </summary>
        public static void Stop(this EnhancedServer server)
        {
            Console.WriteLine("Stopping server...");
            gameStateManager?.Stop();
        }

        #region Broadcasting

        private static void BroadcastPlayerJoined(EnhancedServer server, string playerId, Data.PlayerData playerData)
        {
            var message = new GameMessage
            {
                Type = MessageType.PlayerJoined,
                PlayerId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "displayName", playerData.DisplayName },
                    { "health", playerData.Health },
                    { "maxHealth", playerData.MaxHealth }
                }
            };

            BroadcastMessage(server, message);
        }

        private static void BroadcastPlayerLeft(EnhancedServer server, string playerId)
        {
            var message = new GameMessage
            {
                Type = MessageType.PlayerLeft,
                PlayerId = playerId
            };

            BroadcastMessage(server, message);
        }

        private static void BroadcastPlayerState(EnhancedServer server, string playerId, Data.PlayerData playerData)
        {
            var message = new GameMessage
            {
                Type = MessageType.PlayerStateUpdate,
                PlayerId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "health", playerData.Health },
                    { "state", playerData.CurrentState.ToString() }
                }
            };

            if (playerData.Position != null)
            {
                message.Data["x"] = playerData.Position.X;
                message.Data["y"] = playerData.Position.Y;
                message.Data["z"] = playerData.Position.Z;
            }

            BroadcastMessage(server, message);
        }

        private static void BroadcastPlayerSpawned(EnhancedServer server, string playerId, Position position)
        {
            var message = new GameMessage
            {
                Type = MessageType.PlayerSpawned,
                PlayerId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "x", position.X },
                    { "y", position.Y },
                    { "z", position.Z }
                }
            };

            BroadcastMessage(server, message);
        }

        private static void BroadcastGroupSpawnCompleted(EnhancedServer server, string groupId, List<string> playerIds)
        {
            var message = new GameMessage
            {
                Type = MessageType.GroupSpawnCompleted,
                Data = new Dictionary<string, object>
                {
                    { "groupId", groupId },
                    { "playerIds", playerIds }
                }
            };

            BroadcastMessage(server, message);
        }

        private static void BroadcastMessage(EnhancedServer server, GameMessage message)
        {
            try
            {
                // Use public BroadcastToAll method
                server.BroadcastToAll(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR broadcasting message: {ex.Message}");
            }
        }

        #endregion
    }
}

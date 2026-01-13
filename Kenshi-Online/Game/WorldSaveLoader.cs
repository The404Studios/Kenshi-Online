using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Networking.Authority;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// WorldSaveLoader bridges server-owned saves with KenshiGameBridge memory injection.
    ///
    /// Architecture:
    /// - Server loads world save from disk
    /// - KenshiGameBridge reads current Kenshi memory state
    /// - WorldSaveLoader synchronizes: Server save <-> Game memory
    ///
    /// The server save is AUTHORITATIVE. Game memory is synchronized TO the save,
    /// not the other way around (except for initial state capture).
    /// </summary>
    public class WorldSaveLoader : IDisposable
    {
        private const string LOG_PREFIX = "[WorldSaveLoader] ";

        private readonly ServerContext serverContext;
        private readonly KenshiGameBridge gameBridge;
        private readonly string worldId;

        private WorldSaveData currentWorldSave;
        private bool isLoaded = false;

        public event Action<WorldSaveData> OnWorldLoaded;
        public event Action<string> OnLoadError;
        public event Action OnSaveComplete;

        /// <summary>
        /// Current world save data (read-only access)
        /// </summary>
        public WorldSaveData WorldSave => currentWorldSave;

        /// <summary>
        /// Whether the world has been loaded
        /// </summary>
        public bool IsLoaded => isLoaded;

        public WorldSaveLoader(ServerContext serverContext, KenshiGameBridge gameBridge, string worldId = "default")
        {
            this.serverContext = serverContext ?? throw new ArgumentNullException(nameof(serverContext));
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.worldId = worldId;

            Logger.Log(LOG_PREFIX + $"WorldSaveLoader initialized for world: {worldId}");
        }

        #region World Loading

        /// <summary>
        /// Load world save and synchronize with game state.
        /// Called once on server startup.
        /// </summary>
        public async Task<bool> LoadWorldAsync()
        {
            try
            {
                Logger.Log(LOG_PREFIX + "Loading world save...");

                // 1. Load world save from disk
                currentWorldSave = await serverContext.SaveManager.LoadWorldSave(worldId);

                if (currentWorldSave == null)
                {
                    Logger.Log(LOG_PREFIX + "Creating new world save");
                    currentWorldSave = CreateDefaultWorldSave();
                    await serverContext.SaveManager.SaveWorldData(worldId, currentWorldSave);
                }

                Logger.Log(LOG_PREFIX + $"World save loaded (v{currentWorldSave.SaveVersion})");

                // 2. Connect to Kenshi if not connected
                if (!gameBridge.IsConnected)
                {
                    Logger.Log(LOG_PREFIX + "Connecting to Kenshi...");
                    if (!gameBridge.ConnectToKenshi())
                    {
                        Logger.Log(LOG_PREFIX + "WARNING: Could not connect to Kenshi. Running in headless mode.");
                        // Can still run server without game connection (headless server)
                    }
                }

                // 3. If connected to game, capture initial state
                if (gameBridge.IsConnected && currentWorldSave.NPCStates.Count == 0)
                {
                    Logger.Log(LOG_PREFIX + "Capturing initial world state from game...");
                    await CaptureInitialWorldState();
                }

                isLoaded = true;
                OnWorldLoaded?.Invoke(currentWorldSave);

                Logger.Log(LOG_PREFIX + "World load complete");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR loading world: {ex.Message}");
                OnLoadError?.Invoke(ex.Message);
                return false;
            }
        }

        /// <summary>
        /// Capture initial world state from the running Kenshi game.
        /// Only used when starting a fresh world save.
        /// </summary>
        private async Task CaptureInitialWorldState()
        {
            if (!gameBridge.IsConnected)
                return;

            // For now, we start with an empty world save.
            // NPCs and buildings will be populated as they're encountered.
            // This is intentional - we don't want to dump the entire game state.

            Logger.Log(LOG_PREFIX + "Initial world state captured (empty - will populate on demand)");
            await Task.CompletedTask;
        }

        #endregion

        #region Player State Management

        /// <summary>
        /// Load a player into the world.
        /// Called when a player connects to the server.
        /// </summary>
        public async Task<PlayerSpawnResult> LoadPlayerAsync(string playerId, string username)
        {
            var result = new PlayerSpawnResult { Success = false };

            try
            {
                Logger.Log(LOG_PREFIX + $"Loading player {playerId} ({username})...");

                // 1. Register player with server context (loads their save)
                bool registered = await serverContext.RegisterPlayer(playerId, username);
                if (!registered)
                {
                    result.ErrorMessage = "Failed to register player";
                    return result;
                }

                // 2. Get player's save data
                var player = serverContext.GetPlayer(playerId);
                if (player?.SaveData == null)
                {
                    result.ErrorMessage = "Player save data not found";
                    return result;
                }

                // 3. Determine spawn position
                Position spawnPosition;
                if (player.SaveData.Position != null)
                {
                    // Use saved position
                    spawnPosition = new Position(
                        player.SaveData.Position.X,
                        player.SaveData.Position.Y,
                        player.SaveData.Position.Z,
                        player.SaveData.Position.Rotation, 0, 0);
                }
                else
                {
                    // Default spawn position
                    spawnPosition = GetDefaultSpawnPosition();
                }

                // 4. If connected to game, spawn player in Kenshi
                if (gameBridge.IsConnected)
                {
                    var playerData = new PlayerData
                    {
                        PlayerId = playerId,
                        DisplayName = username,
                        Health = player.SaveData.Health,
                        MaxHealth = player.SaveData.MaxHealth,
                        FactionId = "player" // Player faction
                    };

                    bool spawned = gameBridge.SpawnPlayer(playerId, playerData, spawnPosition);
                    if (!spawned)
                    {
                        Logger.Log(LOG_PREFIX + $"WARNING: Could not spawn player {playerId} in game");
                        // Continue anyway - player can still participate in network
                    }
                }

                result.Success = true;
                result.PlayerId = playerId;
                result.SpawnPosition = spawnPosition;
                result.SaveData = player.SaveData;

                Logger.Log(LOG_PREFIX + $"Player {playerId} loaded at ({spawnPosition.X}, {spawnPosition.Y}, {spawnPosition.Z})");
                return result;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR loading player {playerId}: {ex.Message}");
                result.ErrorMessage = ex.Message;
                return result;
            }
        }

        /// <summary>
        /// Unload a player from the world.
        /// Called when a player disconnects.
        /// </summary>
        public async Task UnloadPlayerAsync(string playerId)
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Unloading player {playerId}...");

                // 1. Get current position from game and save it
                if (gameBridge.IsConnected)
                {
                    var currentPos = gameBridge.GetPlayerPosition(playerId);
                    if (currentPos != null)
                    {
                        var player = serverContext.GetPlayer(playerId);
                        if (player?.SaveData != null)
                        {
                            player.SaveData.Position = new SavedPosition
                            {
                                X = currentPos.X,
                                Y = currentPos.Y,
                                Z = currentPos.Z,
                                Rotation = currentPos.RotY
                            };
                            player.SaveData.IsDirty = true;
                        }
                    }

                    // 2. Despawn from game
                    gameBridge.DespawnPlayer(playerId);
                }

                // 3. Unregister from server context (saves data)
                await serverContext.UnregisterPlayer(playerId);

                Logger.Log(LOG_PREFIX + $"Player {playerId} unloaded and saved");
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR unloading player {playerId}: {ex.Message}");
            }
        }

        /// <summary>
        /// Sync player position from network to game memory
        /// </summary>
        public bool SyncPlayerPositionToGame(string playerId, Position position)
        {
            if (!gameBridge.IsConnected)
                return false;

            return gameBridge.UpdatePlayerPosition(playerId, position);
        }

        /// <summary>
        /// Get player position from game memory
        /// </summary>
        public Position GetPlayerPositionFromGame(string playerId)
        {
            if (!gameBridge.IsConnected)
                return null;

            return gameBridge.GetPlayerPosition(playerId);
        }

        #endregion

        #region World State Sync

        /// <summary>
        /// Save all dirty world state to disk
        /// </summary>
        public async Task SaveWorldStateAsync()
        {
            if (currentWorldSave == null)
                return;

            try
            {
                // Sync connected players' positions
                foreach (var player in serverContext.GetAllPlayers())
                {
                    if (gameBridge.IsConnected)
                    {
                        var pos = gameBridge.GetPlayerPosition(player.PlayerId);
                        if (pos != null && player.SaveData != null)
                        {
                            player.SaveData.Position = new SavedPosition
                            {
                                X = pos.X,
                                Y = pos.Y,
                                Z = pos.Z,
                                Rotation = pos.RotY
                            };
                            player.SaveData.IsDirty = true;
                        }
                    }
                }

                // Save world data
                currentWorldSave.IsDirty = true;
                await serverContext.SaveManager.SaveWorldData(worldId, currentWorldSave);

                // Save all player data
                await serverContext.ForceSaveAll();

                OnSaveComplete?.Invoke();
                Logger.Log(LOG_PREFIX + "World state saved");
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR saving world state: {ex.Message}");
            }
        }

        /// <summary>
        /// Update NPC state in world save
        /// </summary>
        public void UpdateNPCState(string npcId, Position position, int health, string state)
        {
            if (currentWorldSave == null)
                return;

            if (!currentWorldSave.NPCStates.TryGetValue(npcId, out var npcSave))
            {
                npcSave = new NPCSaveData { NPCId = npcId };
                currentWorldSave.NPCStates[npcId] = npcSave;
            }

            npcSave.Position = new SavedPosition
            {
                X = position.X,
                Y = position.Y,
                Z = position.Z,
                Rotation = position.RotY
            };
            npcSave.Health = health;
            npcSave.State = state;

            currentWorldSave.IsDirty = true;
        }

        /// <summary>
        /// Add building to world save
        /// </summary>
        public void AddBuilding(BuildingSaveData building)
        {
            if (currentWorldSave == null)
                return;

            currentWorldSave.Buildings.Add(building);
            currentWorldSave.IsDirty = true;
        }

        /// <summary>
        /// Record world event
        /// </summary>
        public void RecordWorldEvent(string eventType, Dictionary<string, object> data)
        {
            if (currentWorldSave == null)
                return;

            currentWorldSave.WorldEvents.Add(new WorldEventSaveData
            {
                EventId = Guid.NewGuid().ToString(),
                EventType = eventType,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Data = data
            });

            currentWorldSave.IsDirty = true;
        }

        #endregion

        #region Game Commands

        /// <summary>
        /// Send a validated command to the game
        /// </summary>
        public bool SendGameCommand(string playerId, string command, params object[] args)
        {
            if (!gameBridge.IsConnected)
            {
                Logger.Log(LOG_PREFIX + $"Cannot send command - game not connected");
                return false;
            }

            return gameBridge.SendGameCommand(playerId, command, args);
        }

        #endregion

        #region Helpers

        private Position GetDefaultSpawnPosition()
        {
            // Default spawn near Hub (Kenshi starting area)
            return new Position(-46960, 0, 44580, 0, 0, 0);
        }

        private WorldSaveData CreateDefaultWorldSave()
        {
            return new WorldSaveData
            {
                WorldId = worldId,
                CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Buildings = new List<BuildingSaveData>(),
                NPCStates = new Dictionary<string, NPCSaveData>(),
                WorldEvents = new List<WorldEventSaveData>()
            };
        }

        #endregion

        public void Dispose()
        {
            // Final save before shutdown
            try
            {
                SaveWorldStateAsync().GetAwaiter().GetResult();
                Logger.Log(LOG_PREFIX + "Final save completed successfully");
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Error during dispose save: {ex.Message}");
            }
            Logger.Log(LOG_PREFIX + "WorldSaveLoader disposed");
        }
    }

    /// <summary>
    /// Result of player spawn attempt
    /// </summary>
    public class PlayerSpawnResult
    {
        public bool Success { get; set; }
        public string PlayerId { get; set; }
        public Position SpawnPosition { get; set; }
        public PlayerSaveData SaveData { get; set; }
        public string ErrorMessage { get; set; }
    }
}

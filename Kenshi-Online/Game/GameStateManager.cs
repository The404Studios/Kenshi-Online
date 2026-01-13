using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Networking.Authority;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Central manager for game state synchronization between server and clients.
    ///
    /// Architecture:
    /// - WorldSaveLoader: Handles persistence (server-owned saves)
    /// - KenshiGameBridge: Handles memory injection (game state)
    /// - GameStateManager: Coordinates between saves, game, and network
    ///
    /// Flow: Network Request -> ServerContext validates -> GameStateManager executes ->
    ///       KenshiGameBridge applies to game -> WorldSaveLoader persists to save
    /// </summary>
    public class GameStateManager : IDisposable
    {
        private readonly KenshiGameBridge gameBridge;
        private readonly PlayerController playerController;
        private readonly SpawnManager spawnManager;
        private readonly StateSynchronizer stateSynchronizer;
        private WorldSaveLoader worldSaveLoader;
        private ServerContext serverContext;
        private const string LOG_PREFIX = "[GameStateManager] ";

        private Timer updateTimer;
        private Timer autoSaveTimer;
        private bool isRunning;
        private readonly object lockObject = new object();

        // Game state
        private readonly Dictionary<string, PlayerData> activePlayers = new Dictionary<string, PlayerData>();
        private readonly Dictionary<string, DateTime> lastUpdateTimes = new Dictionary<string, DateTime>();

        // Configuration
        private const int UPDATE_RATE_MS = 50; // 20 Hz
        private const int POSITION_UPDATE_THRESHOLD_MS = 100; // Send position updates every 100ms
        private const float POSITION_CHANGE_THRESHOLD = 0.5f; // 0.5 meters
        private const int AUTO_SAVE_INTERVAL_MS = 60000; // Auto-save every minute

        public bool IsRunning => isRunning;
        public int ActivePlayerCount => activePlayers.Count;
        public WorldSaveLoader WorldSave => worldSaveLoader;
        public ServerContext ServerContext => serverContext;

        public GameStateManager(KenshiGameBridge gameBridge, StateSynchronizer stateSynchronizer = null)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.stateSynchronizer = stateSynchronizer; // Optional - for compatibility

            this.playerController = new PlayerController(gameBridge);
            this.spawnManager = new SpawnManager(gameBridge, playerController);

            // Subscribe to events
            RegisterEventHandlers();

            Logger.Log(LOG_PREFIX + "GameStateManager initialized");
        }

        /// <summary>
        /// Initialize with world save system for full persistence support
        /// </summary>
        public void InitializeWithSaveSystem(ServerContext serverContext, string worldId = "default")
        {
            this.serverContext = serverContext ?? throw new ArgumentNullException(nameof(serverContext));
            this.worldSaveLoader = new WorldSaveLoader(serverContext, gameBridge, worldId);

            // Subscribe to save events
            worldSaveLoader.OnWorldLoaded += (worldSave) =>
            {
                Logger.Log(LOG_PREFIX + $"World loaded: {worldSave.WorldId} (v{worldSave.SaveVersion})");
            };

            worldSaveLoader.OnLoadError += (error) =>
            {
                Logger.Log(LOG_PREFIX + $"World load error: {error}");
            };

            worldSaveLoader.OnSaveComplete += () =>
            {
                Logger.Log(LOG_PREFIX + "World save completed");
            };

            Logger.Log(LOG_PREFIX + "Save system initialized");
        }

        #region Initialization

        /// <summary>
        /// Start the game state manager
        /// </summary>
        public bool Start()
        {
            try
            {
                if (isRunning)
                {
                    Logger.Log(LOG_PREFIX + "GameStateManager already running");
                    return true;
                }

                Logger.Log(LOG_PREFIX + "Starting GameStateManager...");

                // Connect to Kenshi
                if (!gameBridge.IsConnected)
                {
                    Logger.Log(LOG_PREFIX + "Connecting to Kenshi...");
                    if (!gameBridge.ConnectToKenshi())
                    {
                        Logger.Log(LOG_PREFIX + "WARNING: Failed to connect to Kenshi. Running in headless mode.");
                        // Continue anyway - can still run server without game connection
                    }
                }

                // Load world save if save system is initialized
                if (worldSaveLoader != null)
                {
                    Logger.Log(LOG_PREFIX + "Loading world save...");
                    var loadTask = worldSaveLoader.LoadWorldAsync();
                    loadTask.Wait();

                    if (!loadTask.Result)
                    {
                        Logger.Log(LOG_PREFIX + "WARNING: World save load failed, using defaults");
                    }
                }

                isRunning = true;

                // Start update loop
                updateTimer = new Timer(UpdateGameState, null, 0, UPDATE_RATE_MS);

                // Start auto-save timer if save system is initialized
                if (worldSaveLoader != null)
                {
                    autoSaveTimer = new Timer(AutoSaveCallback, null, AUTO_SAVE_INTERVAL_MS, AUTO_SAVE_INTERVAL_MS);
                }

                Logger.Log(LOG_PREFIX + "GameStateManager started successfully!");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR starting GameStateManager: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Auto-save callback
        /// </summary>
        private async void AutoSaveCallback(object state)
        {
            if (!isRunning || worldSaveLoader == null)
                return;

            try
            {
                await worldSaveLoader.SaveWorldStateAsync();
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Auto-save error: {ex.Message}");
            }
        }

        /// <summary>
        /// Stop the game state manager
        /// </summary>
        public void Stop()
        {
            try
            {
                if (!isRunning)
                    return;

                Logger.Log(LOG_PREFIX + "Stopping GameStateManager...");

                isRunning = false;
                updateTimer?.Dispose();
                updateTimer = null;
                autoSaveTimer?.Dispose();
                autoSaveTimer = null;

                // Save world state before stopping
                if (worldSaveLoader != null)
                {
                    Logger.Log(LOG_PREFIX + "Saving world state...");
                    worldSaveLoader.SaveWorldStateAsync().Wait();
                }

                // Despawn all players
                foreach (var playerId in activePlayers.Keys.ToList())
                {
                    DespawnPlayer(playerId);
                }

                Logger.Log(LOG_PREFIX + "GameStateManager stopped");
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR stopping GameStateManager: {ex.Message}");
            }
        }

        private void RegisterEventHandlers()
        {
            // Game bridge events
            gameBridge.OnPlayerPositionChanged += OnPlayerPositionChanged;

            // Spawn manager events
            spawnManager.OnPlayerSpawned += OnPlayerSpawned;
            spawnManager.OnPlayerDespawned += OnPlayerDespawned;
            spawnManager.OnGroupSpawnCompleted += OnGroupSpawnCompleted;

            // Player controller events
            playerController.OnPlayerPositionChanged += OnPlayerPositionChanged;
            playerController.OnPlayerDied += OnPlayerDied;
        }

        #endregion

        #region Player Management

        /// <summary>
        /// Add a player to the game.
        /// If save system is initialized, loads player from server-owned save.
        /// </summary>
        public async Task<bool> AddPlayer(string playerId, PlayerData playerData, string spawnLocation = "Default")
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Adding player {playerId} ({playerData.DisplayName})");

                // Use save system if available
                if (worldSaveLoader != null)
                {
                    var spawnResult = await worldSaveLoader.LoadPlayerAsync(playerId, playerData.DisplayName);

                    if (spawnResult.Success)
                    {
                        // Update playerData with loaded save data
                        if (spawnResult.SaveData != null)
                        {
                            playerData.Health = spawnResult.SaveData.Health;
                            playerData.MaxHealth = spawnResult.SaveData.MaxHealth;

                            if (spawnResult.SpawnPosition != null)
                            {
                                playerData.Position = spawnResult.SpawnPosition;
                            }
                        }

                        lock (lockObject)
                        {
                            activePlayers[playerId] = playerData;
                            lastUpdateTimes[playerId] = DateTime.UtcNow;
                        }

                        Logger.Log(LOG_PREFIX + $"Player {playerId} loaded from save and spawned");
                        BroadcastPlayerJoined(playerId, playerData);
                        return true;
                    }
                    else
                    {
                        Logger.Log(LOG_PREFIX + $"Failed to load player from save: {spawnResult.ErrorMessage}");
                        // Fall through to legacy spawn
                    }
                }

                // Legacy path - no save system
                lock (lockObject)
                {
                    if (activePlayers.ContainsKey(playerId))
                    {
                        Logger.Log(LOG_PREFIX + $"Player {playerId} already active, updating...");
                        activePlayers[playerId] = playerData;
                        return true;
                    }

                    activePlayers[playerId] = playerData;
                    lastUpdateTimes[playerId] = DateTime.UtcNow;
                }

                // Spawn outside lock
                bool spawned = await spawnManager.SpawnPlayer(playerId, playerData, spawnLocation);

                if (spawned)
                {
                    Logger.Log(LOG_PREFIX + $"Player {playerId} added and spawned successfully");
                    BroadcastPlayerJoined(playerId, playerData);
                }

                return spawned;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR adding player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Remove a player from the game.
        /// If save system is initialized, saves player state before removing.
        /// </summary>
        public bool RemovePlayer(string playerId)
        {
            try
            {
                if (!activePlayers.ContainsKey(playerId))
                {
                    Logger.Log(LOG_PREFIX + $"Player {playerId} not found");
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"Removing player {playerId}");

                // Use save system if available - save and unload player
                if (worldSaveLoader != null)
                {
                    worldSaveLoader.UnloadPlayerAsync(playerId).Wait();
                }
                else
                {
                    // Legacy path - just despawn
                    spawnManager.DespawnPlayer(playerId);
                }

                lock (lockObject)
                {
                    activePlayers.Remove(playerId);
                    lastUpdateTimes.Remove(playerId);
                }

                Logger.Log(LOG_PREFIX + $"Player {playerId} removed and saved");
                BroadcastPlayerLeft(playerId);

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR removing player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Get player data
        /// </summary>
        public PlayerData GetPlayerData(string playerId)
        {
            lock (lockObject)
            {
                return activePlayers.TryGetValue(playerId, out var data) ? data : null;
            }
        }

        /// <summary>
        /// Get all active players
        /// </summary>
        public List<PlayerData> GetAllPlayers()
        {
            lock (lockObject)
            {
                return activePlayers.Values.ToList();
            }
        }

        #endregion

        #region Spawning

        /// <summary>
        /// Spawn a player at a location
        /// </summary>
        public async Task<bool> SpawnPlayer(string playerId, string locationName = "Default")
        {
            var playerData = GetPlayerData(playerId);
            if (playerData == null)
            {
                Logger.Log(LOG_PREFIX + $"ERROR: Player {playerId} not found");
                return false;
            }

            return await spawnManager.SpawnPlayer(playerId, playerData, locationName);
        }

        /// <summary>
        /// Spawn multiple players together (friends)
        /// </summary>
        public string RequestGroupSpawn(List<string> playerIds, string locationName = "Default")
        {
            Logger.Log(LOG_PREFIX + $"Requesting group spawn for {playerIds.Count} players");
            return spawnManager.RequestGroupSpawn(playerIds, locationName);
        }

        /// <summary>
        /// Player signals ready for group spawn
        /// </summary>
        public bool PlayerReadyForGroupSpawn(string groupId, string playerId)
        {
            return spawnManager.PlayerReadyToSpawn(groupId, playerId);
        }

        /// <summary>
        /// Despawn a player
        /// </summary>
        public bool DespawnPlayer(string playerId)
        {
            return spawnManager.DespawnPlayer(playerId);
        }

        /// <summary>
        /// Get available spawn locations
        /// </summary>
        public List<string> GetSpawnLocations()
        {
            return spawnManager.GetAvailableSpawnLocations();
        }

        #endregion

        #region Player Control

        /// <summary>
        /// Move a player to a position
        /// </summary>
        public bool MovePlayer(string playerId, Position targetPosition)
        {
            return playerController.MovePlayer(playerId, targetPosition);
        }

        /// <summary>
        /// Make player follow another player
        /// </summary>
        public bool FollowPlayer(string playerId, string targetPlayerId)
        {
            return playerController.FollowPlayer(playerId, targetPlayerId);
        }

        /// <summary>
        /// Make player attack a target
        /// </summary>
        public bool AttackTarget(string playerId, string targetId)
        {
            return playerController.AttackTarget(playerId, targetId);
        }

        /// <summary>
        /// Make player pickup an item
        /// </summary>
        public bool PickupItem(string playerId, string itemId)
        {
            return playerController.PickupItem(playerId, itemId);
        }

        /// <summary>
        /// Create a squad with players
        /// </summary>
        public string CreateSquad(List<string> playerIds)
        {
            return playerController.CreateSquad(playerIds);
        }

        #endregion

        #region State Updates

        private void UpdateGameState(object state)
        {
            if (!isRunning)
                return;

            try
            {
                lock (lockObject)
                {
                    var now = DateTime.UtcNow;

                    foreach (var playerId in activePlayers.Keys.ToList())
                    {
                        // Update player state from game
                        var updatedData = playerController.UpdatePlayerState(playerId);

                        if (updatedData != null)
                        {
                            activePlayers[playerId] = updatedData;

                            // Check if we should send an update
                            if (ShouldSendUpdate(playerId, now))
                            {
                                SendPlayerUpdate(playerId, updatedData);
                                lastUpdateTimes[playerId] = now;
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR in update loop: {ex.Message}");
            }
        }

        private bool ShouldSendUpdate(string playerId, DateTime now)
        {
            if (!lastUpdateTimes.TryGetValue(playerId, out var lastUpdate))
                return true;

            return (now - lastUpdate).TotalMilliseconds >= POSITION_UPDATE_THRESHOLD_MS;
        }

        private void SendPlayerUpdate(string playerId, PlayerData playerData)
        {
            try
            {
                // Create state update
                var stateUpdate = new StateUpdate
                {
                    PlayerId = playerId,
                    Position = playerData.Position,
                    Health = playerData.Health,
                    CurrentState = playerData.CurrentState,
                    Timestamp = DateTime.UtcNow
                };

                // Send via state synchronizer if available
                stateSynchronizer?.QueueStateUpdate(stateUpdate);

                // Broadcast to other players
                OnPlayerStateChanged?.Invoke(playerId, playerData);
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR sending player update: {ex.Message}");
            }
        }

        #endregion

        #region Event Handlers

        private void OnPlayerPositionChanged(string playerId, Position newPosition)
        {
            lock (lockObject)
            {
                if (activePlayers.TryGetValue(playerId, out var playerData))
                {
                    // Check if position changed significantly
                    if (playerData.Position == null ||
                        newPosition.DistanceTo(playerData.Position) > POSITION_CHANGE_THRESHOLD)
                    {
                        playerData.Position = newPosition;
                        SendPlayerUpdate(playerId, playerData);
                    }
                }
            }
        }

        private void OnPlayerSpawned(string playerId, Position spawnPosition)
        {
            Logger.Log(LOG_PREFIX + $"Player {playerId} spawned at {spawnPosition.X}, {spawnPosition.Y}, {spawnPosition.Z}");
            BroadcastPlayerSpawned(playerId, spawnPosition);
        }

        private void OnPlayerDespawned(string playerId)
        {
            Logger.Log(LOG_PREFIX + $"Player {playerId} despawned");
        }

        private void OnGroupSpawnCompleted(string groupId, List<string> playerIds)
        {
            Logger.Log(LOG_PREFIX + $"Group spawn {groupId} completed for {playerIds.Count} players");
            BroadcastGroupSpawnCompleted(groupId, playerIds);
        }

        private void OnPlayerDied(string playerId)
        {
            Logger.Log(LOG_PREFIX + $"Player {playerId} died");
            BroadcastPlayerDied(playerId);
        }

        #endregion

        #region Broadcasting

        private void BroadcastPlayerJoined(string playerId, PlayerData playerData)
        {
            OnPlayerJoined?.Invoke(playerId, playerData);
        }

        private void BroadcastPlayerLeft(string playerId)
        {
            OnPlayerLeft?.Invoke(playerId);
        }

        private void BroadcastPlayerSpawned(string playerId, Position position)
        {
            OnPlayerSpawnedBroadcast?.Invoke(playerId, position);
        }

        private void BroadcastGroupSpawnCompleted(string groupId, List<string> playerIds)
        {
            OnGroupSpawnCompletedBroadcast?.Invoke(groupId, playerIds);
        }

        private void BroadcastPlayerDied(string playerId)
        {
            OnPlayerDiedBroadcast?.Invoke(playerId);
        }

        #endregion

        #region Events

        public event Action<string, PlayerData> OnPlayerJoined;
        public event Action<string> OnPlayerLeft;
        public event Action<string, PlayerData> OnPlayerStateChanged;
        public event Action<string, Position> OnPlayerSpawnedBroadcast;
        public event Action<string, List<string>> OnGroupSpawnCompletedBroadcast;
        public event Action<string> OnPlayerDiedBroadcast;

        #endregion

        #region Disposal

        public void Dispose()
        {
            Stop();
            worldSaveLoader?.Dispose();
            gameBridge?.Dispose();
            Logger.Log(LOG_PREFIX + "GameStateManager disposed");
        }

        #endregion

        #region Save System Integration

        /// <summary>
        /// Force save all player and world state
        /// </summary>
        public async Task ForceSaveAsync()
        {
            if (worldSaveLoader != null)
            {
                await worldSaveLoader.SaveWorldStateAsync();
            }
        }

        /// <summary>
        /// Sync player position to game (from network)
        /// </summary>
        public bool SyncPlayerPositionToGame(string playerId, Position position)
        {
            if (worldSaveLoader != null)
            {
                return worldSaveLoader.SyncPlayerPositionToGame(playerId, position);
            }

            return gameBridge.UpdatePlayerPosition(playerId, position);
        }

        /// <summary>
        /// Get player position from game memory
        /// </summary>
        public Position GetPlayerPositionFromGame(string playerId)
        {
            if (worldSaveLoader != null)
            {
                return worldSaveLoader.GetPlayerPositionFromGame(playerId);
            }

            return gameBridge.GetPlayerPosition(playerId);
        }

        /// <summary>
        /// Record a world event for persistence
        /// </summary>
        public void RecordWorldEvent(string eventType, Dictionary<string, object> data)
        {
            worldSaveLoader?.RecordWorldEvent(eventType, data);
        }

        #endregion
    }
}

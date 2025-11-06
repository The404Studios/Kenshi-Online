using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Kenshi_Online.Data;
using Kenshi_Online.Networking;
using Kenshi_Online.Utility;

namespace Kenshi_Online.Game
{
    /// <summary>
    /// Central manager for game state synchronization between server and clients
    /// </summary>
    public class GameStateManager : IDisposable
    {
        private readonly KenshiGameBridge gameBridge;
        private readonly PlayerController playerController;
        private readonly SpawnManager spawnManager;
        private readonly StateSynchronizer stateSynchronizer;
        private readonly Logger logger = new Logger("GameStateManager");

        private Timer updateTimer;
        private bool isRunning;
        private readonly object lockObject = new object();

        // Game state
        private readonly Dictionary<string, PlayerData> activePlayers = new Dictionary<string, PlayerData>();
        private readonly Dictionary<string, DateTime> lastUpdateTimes = new Dictionary<string, DateTime>();

        // Configuration
        private const int UPDATE_RATE_MS = 50; // 20 Hz
        private const int POSITION_UPDATE_THRESHOLD_MS = 100; // Send position updates every 100ms
        private const float POSITION_CHANGE_THRESHOLD = 0.5f; // 0.5 meters

        public bool IsRunning => isRunning;
        public int ActivePlayerCount => activePlayers.Count;

        public GameStateManager(KenshiGameBridge gameBridge, StateSynchronizer stateSynchronizer)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.stateSynchronizer = stateSynchronizer ?? throw new ArgumentNullException(nameof(stateSynchronizer));

            this.playerController = new PlayerController(gameBridge);
            this.spawnManager = new SpawnManager(gameBridge, playerController);

            // Subscribe to events
            RegisterEventHandlers();

            logger.Log("GameStateManager initialized");
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
                    logger.Log("GameStateManager already running");
                    return true;
                }

                logger.Log("Starting GameStateManager...");

                // Connect to Kenshi
                if (!gameBridge.IsConnected)
                {
                    logger.Log("Connecting to Kenshi...");
                    if (!gameBridge.ConnectToKenshi())
                    {
                        logger.Log("ERROR: Failed to connect to Kenshi. Make sure the game is running!");
                        return false;
                    }
                }

                isRunning = true;

                // Start update loop
                updateTimer = new Timer(UpdateGameState, null, 0, UPDATE_RATE_MS);

                logger.Log("GameStateManager started successfully!");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR starting GameStateManager: {ex.Message}");
                return false;
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

                logger.Log("Stopping GameStateManager...");

                isRunning = false;
                updateTimer?.Dispose();
                updateTimer = null;

                // Despawn all players
                foreach (var playerId in activePlayers.Keys.ToList())
                {
                    DespawnPlayer(playerId);
                }

                logger.Log("GameStateManager stopped");
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR stopping GameStateManager: {ex.Message}");
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
        /// Add a player to the game
        /// </summary>
        public async Task<bool> AddPlayer(string playerId, PlayerData playerData, string spawnLocation = "Default")
        {
            lock (lockObject)
            {
                try
                {
                    logger.Log($"Adding player {playerId} ({playerData.DisplayName})");

                    if (activePlayers.ContainsKey(playerId))
                    {
                        logger.Log($"Player {playerId} already active, updating...");
                        activePlayers[playerId] = playerData;
                        return true;
                    }

                    activePlayers[playerId] = playerData;
                    lastUpdateTimes[playerId] = DateTime.UtcNow;

                    return true;
                }
                catch (Exception ex)
                {
                    logger.Log($"ERROR adding player: {ex.Message}");
                    return false;
                }
            }

            // Spawn outside lock
            bool spawned = await spawnManager.SpawnPlayer(playerId, playerData, spawnLocation);

            if (spawned)
            {
                logger.Log($"Player {playerId} added and spawned successfully");
                BroadcastPlayerJoined(playerId, playerData);
            }

            return spawned;
        }

        /// <summary>
        /// Remove a player from the game
        /// </summary>
        public bool RemovePlayer(string playerId)
        {
            lock (lockObject)
            {
                try
                {
                    if (!activePlayers.ContainsKey(playerId))
                    {
                        logger.Log($"Player {playerId} not found");
                        return false;
                    }

                    logger.Log($"Removing player {playerId}");

                    activePlayers.Remove(playerId);
                    lastUpdateTimes.Remove(playerId);

                    // Despawn
                    spawnManager.DespawnPlayer(playerId);

                    logger.Log($"Player {playerId} removed successfully");
                    BroadcastPlayerLeft(playerId);

                    return true;
                }
                catch (Exception ex)
                {
                    logger.Log($"ERROR removing player: {ex.Message}");
                    return false;
                }
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
                logger.Log($"ERROR: Player {playerId} not found");
                return false;
            }

            return await spawnManager.SpawnPlayer(playerId, playerData, locationName);
        }

        /// <summary>
        /// Spawn multiple players together (friends)
        /// </summary>
        public string RequestGroupSpawn(List<string> playerIds, string locationName = "Default")
        {
            logger.Log($"Requesting group spawn for {playerIds.Count} players");
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
                logger.Log($"ERROR in update loop: {ex.Message}");
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

                // Send via state synchronizer
                stateSynchronizer.QueueStateUpdate(stateUpdate);

                // Broadcast to other players
                OnPlayerStateChanged?.Invoke(playerId, playerData);
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR sending player update: {ex.Message}");
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
            logger.Log($"Player {playerId} spawned at {spawnPosition.X}, {spawnPosition.Y}, {spawnPosition.Z}");
            BroadcastPlayerSpawned(playerId, spawnPosition);
        }

        private void OnPlayerDespawned(string playerId)
        {
            logger.Log($"Player {playerId} despawned");
        }

        private void OnGroupSpawnCompleted(string groupId, List<string> playerIds)
        {
            logger.Log($"Group spawn {groupId} completed for {playerIds.Count} players");
            BroadcastGroupSpawnCompleted(groupId, playerIds);
        }

        private void OnPlayerDied(string playerId)
        {
            logger.Log($"Player {playerId} died");
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
            gameBridge?.Dispose();
            logger.Log("GameStateManager disposed");
        }

        #endregion

        /// <summary>
        /// State update for synchronization
        /// </summary>
        public class StateUpdate
        {
            public string PlayerId { get; set; }
            public Position Position { get; set; }
            public float Health { get; set; }
            public PlayerState CurrentState { get; set; }
            public DateTime Timestamp { get; set; }
        }
    }
}

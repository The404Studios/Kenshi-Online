using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Client-side multiplayer synchronization manager.
    /// Coordinates between:
    /// - KenshiGameBridge (reads/writes game memory)
    /// - EnhancedClient (network communication)
    /// - Other player rendering/tracking
    ///
    /// Flow:
    /// 1. Read local player position from Kenshi memory
    /// 2. Send position updates to server
    /// 3. Receive other players' positions from server
    /// 4. Update other players in game memory
    /// </summary>
    public class MultiplayerSync : IDisposable
    {
        private const string LOG_PREFIX = "[MultiplayerSync] ";
        private const int POSITION_UPDATE_RATE_MS = 50;  // 20 Hz position updates
        private const float POSITION_THRESHOLD = 0.5f;   // Min distance to trigger update

        private readonly KenshiGameBridge gameBridge;
        private readonly EnhancedClient networkClient;

        private Timer syncTimer;
        private bool isRunning;
        private readonly object lockObject = new object();

        // Local player tracking
        private string localPlayerId;
        private Position lastSentPosition;
        private DateTime lastPositionUpdate;

        // Other players tracking
        private readonly Dictionary<string, OtherPlayer> otherPlayers = new Dictionary<string, OtherPlayer>();

        // Events
        public event Action<string> OnPlayerJoined;
        public event Action<string> OnPlayerLeft;
        public event Action<string, Position> OnPlayerMoved;
        public event Action<string> OnConnectionLost;
        public event Action OnSyncStarted;
        public event Action OnSyncStopped;

        public bool IsRunning => isRunning;
        public string LocalPlayerId => localPlayerId;
        public int OtherPlayerCount => otherPlayers.Count;

        public MultiplayerSync(KenshiGameBridge gameBridge, EnhancedClient networkClient)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.networkClient = networkClient ?? throw new ArgumentNullException(nameof(networkClient));

            // Subscribe to network events
            networkClient.MessageReceived += OnNetworkMessageReceived;

            Logger.Log(LOG_PREFIX + "MultiplayerSync initialized");
        }

        #region Sync Control

        /// <summary>
        /// Start multiplayer synchronization after successful login and spawn
        /// </summary>
        public bool Start(string playerId)
        {
            if (isRunning)
            {
                Logger.Log(LOG_PREFIX + "Sync already running");
                return true;
            }

            if (!gameBridge.IsConnected)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Game bridge not connected");
                return false;
            }

            if (!networkClient.IsConnected)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Network client not connected");
                return false;
            }

            localPlayerId = playerId;
            lastSentPosition = null;
            lastPositionUpdate = DateTime.MinValue;

            Logger.Log(LOG_PREFIX + $"Starting multiplayer sync for player {playerId}...");

            isRunning = true;

            // Start sync timer
            syncTimer = new Timer(SyncTick, null, 0, POSITION_UPDATE_RATE_MS);

            OnSyncStarted?.Invoke();
            Logger.Log(LOG_PREFIX + "Multiplayer sync started successfully!");

            return true;
        }

        /// <summary>
        /// Stop multiplayer synchronization
        /// </summary>
        public void Stop()
        {
            if (!isRunning)
                return;

            Logger.Log(LOG_PREFIX + "Stopping multiplayer sync...");

            isRunning = false;
            syncTimer?.Dispose();
            syncTimer = null;

            // Clean up other players
            lock (lockObject)
            {
                foreach (var player in otherPlayers.Values)
                {
                    gameBridge.DespawnPlayer(player.PlayerId);
                }
                otherPlayers.Clear();
            }

            OnSyncStopped?.Invoke();
            Logger.Log(LOG_PREFIX + "Multiplayer sync stopped");
        }

        #endregion

        #region Sync Loop

        private void SyncTick(object state)
        {
            if (!isRunning)
                return;

            try
            {
                // 1. Read local player position from game
                SyncLocalPlayerPosition();

                // 2. Update interpolation for other players
                UpdateOtherPlayers();
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR in sync tick: {ex.Message}");
            }
        }

        /// <summary>
        /// Read local player position and send to server if changed
        /// </summary>
        private void SyncLocalPlayerPosition()
        {
            if (string.IsNullOrEmpty(localPlayerId))
                return;

            // Read position from game memory (use local player method for actual Kenshi player)
            Position currentPosition = gameBridge.GetLocalPlayerPosition();

            if (currentPosition == null)
                return;

            // Check if position changed significantly
            bool shouldSend = false;

            if (lastSentPosition == null)
            {
                shouldSend = true;
            }
            else
            {
                float distance = CalculateDistance(currentPosition, lastSentPosition);
                if (distance >= POSITION_THRESHOLD)
                {
                    shouldSend = true;
                }
            }

            // Rate limit: at most every 50ms
            if (shouldSend && (DateTime.UtcNow - lastPositionUpdate).TotalMilliseconds >= POSITION_UPDATE_RATE_MS)
            {
                // Send position to server
                networkClient.SendPositionUpdate(
                    currentPosition.X,
                    currentPosition.Y,
                    currentPosition.Z,
                    currentPosition.RotY);

                lastSentPosition = currentPosition;
                lastPositionUpdate = DateTime.UtcNow;
            }
        }

        /// <summary>
        /// Interpolate other player positions for smooth movement
        /// </summary>
        private void UpdateOtherPlayers()
        {
            lock (lockObject)
            {
                foreach (var player in otherPlayers.Values)
                {
                    try
                    {
                        // Interpolate between current and target position
                        if (player.TargetPosition != null && player.CurrentPosition != null)
                        {
                            float t = Math.Min(1.0f, (float)(DateTime.UtcNow - player.LastUpdateTime).TotalMilliseconds / 100f);

                            Position interpolated = new Position(
                                Lerp(player.CurrentPosition.X, player.TargetPosition.X, t),
                                Lerp(player.CurrentPosition.Y, player.TargetPosition.Y, t),
                                Lerp(player.CurrentPosition.Z, player.TargetPosition.Z, t),
                                Lerp(player.CurrentPosition.RotX, player.TargetPosition.RotX, t),
                                Lerp(player.CurrentPosition.RotY, player.TargetPosition.RotY, t),
                                Lerp(player.CurrentPosition.RotZ, player.TargetPosition.RotZ, t)
                            );

                            // Update in game
                            gameBridge.UpdatePlayerPosition(player.PlayerId, interpolated);
                            player.CurrentPosition = interpolated;
                        }
                    }
                    catch (Exception ex)
                    {
                        Logger.Log(LOG_PREFIX + $"Error updating player {player.PlayerId}: {ex.Message}");
                    }
                }
            }
        }

        #endregion

        #region Network Message Handling

        private void OnNetworkMessageReceived(object sender, GameMessage message)
        {
            try
            {
                switch (message.Type)
                {
                    case MessageType.Position:
                        HandlePositionUpdate(message);
                        break;

                    case MessageType.PlayerJoined:
                        HandlePlayerJoined(message);
                        break;

                    case MessageType.PlayerLeft:
                        HandlePlayerLeft(message);
                        break;

                    case MessageType.PlayerSpawned:
                        HandlePlayerSpawned(message);
                        break;

                    case MessageType.PlayerStateUpdate:
                        HandlePlayerStateUpdate(message);
                        break;
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR handling network message: {ex.Message}");
            }
        }

        private void HandlePositionUpdate(GameMessage message)
        {
            string playerId = message.PlayerId;

            // Ignore our own position updates
            if (playerId == localPlayerId)
                return;

            if (!message.Data.TryGetValue("x", out var xObj) ||
                !message.Data.TryGetValue("y", out var yObj) ||
                !message.Data.TryGetValue("z", out var zObj))
            {
                return;
            }

            float x = Convert.ToSingle(xObj);
            float y = Convert.ToSingle(yObj);
            float z = Convert.ToSingle(zObj);
            float rotY = message.Data.TryGetValue("rotY", out var rotYObj) ? Convert.ToSingle(rotYObj) : 0;

            Position newPosition = new Position(x, y, z, 0, rotY, 0);

            lock (lockObject)
            {
                if (otherPlayers.TryGetValue(playerId, out var player))
                {
                    // Set target for interpolation
                    player.TargetPosition = newPosition;
                    player.LastUpdateTime = DateTime.UtcNow;

                    OnPlayerMoved?.Invoke(playerId, newPosition);
                }
            }
        }

        private void HandlePlayerJoined(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == localPlayerId)
                return;

            string displayName = message.Data.TryGetValue("displayName", out var nameObj)
                ? nameObj?.ToString() ?? playerId
                : playerId;

            Logger.Log(LOG_PREFIX + $"Player joined: {playerId} ({displayName})");

            lock (lockObject)
            {
                if (!otherPlayers.ContainsKey(playerId))
                {
                    otherPlayers[playerId] = new OtherPlayer
                    {
                        PlayerId = playerId,
                        DisplayName = displayName
                    };
                }
            }

            OnPlayerJoined?.Invoke(playerId);
        }

        private void HandlePlayerLeft(GameMessage message)
        {
            string playerId = message.PlayerId;

            Logger.Log(LOG_PREFIX + $"Player left: {playerId}");

            lock (lockObject)
            {
                if (otherPlayers.TryGetValue(playerId, out var player))
                {
                    // Despawn from game
                    gameBridge.DespawnPlayer(playerId);
                    otherPlayers.Remove(playerId);
                }
            }

            OnPlayerLeft?.Invoke(playerId);
        }

        private void HandlePlayerSpawned(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == localPlayerId)
                return;

            if (!message.Data.TryGetValue("x", out var xObj) ||
                !message.Data.TryGetValue("y", out var yObj) ||
                !message.Data.TryGetValue("z", out var zObj))
            {
                return;
            }

            float x = Convert.ToSingle(xObj);
            float y = Convert.ToSingle(yObj);
            float z = Convert.ToSingle(zObj);

            Position spawnPosition = new Position(x, y, z);

            Logger.Log(LOG_PREFIX + $"Player {playerId} spawned at ({x}, {y}, {z})");

            lock (lockObject)
            {
                if (!otherPlayers.TryGetValue(playerId, out var player))
                {
                    player = new OtherPlayer { PlayerId = playerId };
                    otherPlayers[playerId] = player;
                }

                player.CurrentPosition = spawnPosition;
                player.TargetPosition = spawnPosition;
                player.LastUpdateTime = DateTime.UtcNow;
                player.IsSpawned = true;

                // Spawn in game
                var playerData = new PlayerData
                {
                    PlayerId = playerId,
                    DisplayName = player.DisplayName ?? playerId,
                    Health = 100,
                    MaxHealth = 100,
                    FactionId = "player"
                };

                gameBridge.SpawnPlayer(playerId, playerData, spawnPosition);
            }
        }

        private void HandlePlayerStateUpdate(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == localPlayerId)
                return;

            lock (lockObject)
            {
                if (otherPlayers.TryGetValue(playerId, out var player))
                {
                    // Update position if included
                    if (message.Data.TryGetValue("x", out var xObj) &&
                        message.Data.TryGetValue("y", out var yObj) &&
                        message.Data.TryGetValue("z", out var zObj))
                    {
                        float x = Convert.ToSingle(xObj);
                        float y = Convert.ToSingle(yObj);
                        float z = Convert.ToSingle(zObj);

                        player.TargetPosition = new Position(x, y, z);
                        player.LastUpdateTime = DateTime.UtcNow;
                    }

                    // Update health if included
                    if (message.Data.TryGetValue("health", out var healthObj))
                    {
                        player.Health = Convert.ToSingle(healthObj);
                    }

                    // Update state if included
                    if (message.Data.TryGetValue("state", out var stateObj))
                    {
                        if (Enum.TryParse<PlayerState>(stateObj?.ToString(), out var state))
                        {
                            player.State = state;
                        }
                    }
                }
            }
        }

        #endregion

        #region Helpers

        private float CalculateDistance(Position a, Position b)
        {
            float dx = a.X - b.X;
            float dy = a.Y - b.Y;
            float dz = a.Z - b.Z;
            return MathF.Sqrt(dx * dx + dy * dy + dz * dz);
        }

        private float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        /// <summary>
        /// Get information about all other players
        /// </summary>
        public List<OtherPlayerInfo> GetOtherPlayers()
        {
            var result = new List<OtherPlayerInfo>();

            lock (lockObject)
            {
                foreach (var player in otherPlayers.Values)
                {
                    result.Add(new OtherPlayerInfo
                    {
                        PlayerId = player.PlayerId,
                        DisplayName = player.DisplayName,
                        Position = player.CurrentPosition,
                        Health = player.Health,
                        State = player.State,
                        IsSpawned = player.IsSpawned
                    });
                }
            }

            return result;
        }

        #endregion

        #region Disposal

        public void Dispose()
        {
            Stop();
            networkClient.MessageReceived -= OnNetworkMessageReceived;
            Logger.Log(LOG_PREFIX + "MultiplayerSync disposed");
        }

        #endregion

        #region Internal Classes

        private class OtherPlayer
        {
            public string PlayerId { get; set; }
            public string DisplayName { get; set; }
            public Position CurrentPosition { get; set; }
            public Position TargetPosition { get; set; }
            public DateTime LastUpdateTime { get; set; }
            public float Health { get; set; } = 100;
            public PlayerState State { get; set; } = PlayerState.Idle;
            public bool IsSpawned { get; set; }
        }

        #endregion
    }

    /// <summary>
    /// Public info about other players (for UI display)
    /// </summary>
    public class OtherPlayerInfo
    {
        public string PlayerId { get; set; }
        public string DisplayName { get; set; }
        public Position Position { get; set; }
        public float Health { get; set; }
        public PlayerState State { get; set; }
        public bool IsSpawned { get; set; }
    }
}

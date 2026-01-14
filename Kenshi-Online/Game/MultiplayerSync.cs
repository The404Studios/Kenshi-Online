using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiOnline.Coordinates;
using KenshiOnline.Coordinates.Integration;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// [DEPRECATED] Client-side multiplayer synchronization manager.
    ///
    /// This class is now a wrapper around CoordinatedMultiplayerSync.
    /// Use CoordinatedMultiplayerSync directly for new code.
    ///
    /// The old ad-hoc approach has been replaced with ring-based architecture:
    /// - Ring1 (Container): Entity ontology
    /// - Ring2 (Info): Observations/proposals
    /// - Ring3 (Authority): Committed truth
    /// - Ring4 (Attribute): Presentation/interpolation
    /// - DataBus: Bidirectional pipeline with staleness budgets
    /// </summary>
    [Obsolete("Use CoordinatedMultiplayerSync instead. This class wraps the new coordinated system.")]
    public class MultiplayerSync : IDisposable
    {
        private const string LOG_PREFIX = "[MultiplayerSync] ";

        // New coordinated sync (does the actual work)
        private readonly CoordinatedMultiplayerSync _coordinatedSync;

        // Legacy references (kept for backward compatibility)
        private readonly KenshiGameBridge gameBridge;
        private readonly EnhancedClient networkClient;

        // Events (forwarded from coordinated sync)
        public event Action<string> OnPlayerJoined;
        public event Action<string> OnPlayerLeft;
        public event Action<string, Position> OnPlayerMoved;
        public event Action<string> OnConnectionLost;
        public event Action OnSyncStarted;
        public event Action OnSyncStopped;

        public bool IsRunning => _coordinatedSync?.IsRunning ?? false;
        public string LocalPlayerId => _coordinatedSync?.LocalPlayerId;
        public int OtherPlayerCount => _coordinatedSync?.GetOtherPlayers().Count ?? 0;

        /// <summary>
        /// Get the underlying coordinated sync for advanced usage.
        /// </summary>
        public CoordinatedMultiplayerSync CoordinatedSync => _coordinatedSync;

        /// <summary>
        /// Get the ring coordinator for direct access to the authority system.
        /// </summary>
        public RingCoordinator Coordinator => _coordinatedSync?.Coordinator;

        public MultiplayerSync(KenshiGameBridge gameBridge, EnhancedClient networkClient)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.networkClient = networkClient ?? throw new ArgumentNullException(nameof(networkClient));

            // Create coordinated sync (uses the new ring-based architecture)
            _coordinatedSync = new CoordinatedMultiplayerSync(gameBridge);

            // Forward events
            _coordinatedSync.OnPlayerJoined += id => OnPlayerJoined?.Invoke(id);
            _coordinatedSync.OnPlayerLeft += id => OnPlayerLeft?.Invoke(id);
            _coordinatedSync.OnPlayerMoved += (id, pos) => OnPlayerMoved?.Invoke(id, pos);
            _coordinatedSync.OnSyncStarted += () => OnSyncStarted?.Invoke();
            _coordinatedSync.OnSyncStopped += () => OnSyncStopped?.Invoke();

            // Subscribe to network events and forward to coordinated sync
            networkClient.MessageReceived += OnNetworkMessageReceived;

            Logger.Log(LOG_PREFIX + "MultiplayerSync initialized (using CoordinatedMultiplayerSync)");
        }

        #region Sync Control

        /// <summary>
        /// Start multiplayer synchronization after successful login and spawn
        /// </summary>
        public bool Start(string playerId)
        {
            Logger.Log(LOG_PREFIX + $"Starting sync for player {playerId} (delegating to CoordinatedMultiplayerSync)");
            return _coordinatedSync.Start(playerId);
        }

        /// <summary>
        /// Stop multiplayer synchronization
        /// </summary>
        public void Stop()
        {
            Logger.Log(LOG_PREFIX + "Stopping sync");
            _coordinatedSync.Stop();
        }

        #endregion

        #region Network Message Handling (Forward to CoordinatedSync)

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

            if (playerId == LocalPlayerId)
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

            // Forward to coordinated sync
            _coordinatedSync.HandlePositionUpdate(playerId, newPosition);
        }

        private void HandlePlayerJoined(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == LocalPlayerId)
                return;

            string displayName = message.Data.TryGetValue("displayName", out var nameObj)
                ? nameObj?.ToString() ?? playerId
                : playerId;

            // Forward to coordinated sync
            _coordinatedSync.HandlePlayerJoined(playerId, displayName);
        }

        private void HandlePlayerLeft(GameMessage message)
        {
            string playerId = message.PlayerId;
            _coordinatedSync.HandlePlayerLeft(playerId);
        }

        private void HandlePlayerSpawned(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == LocalPlayerId)
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

            var playerData = new PlayerData
            {
                PlayerId = playerId,
                DisplayName = playerId,
                Health = 100,
                MaxHealth = 100,
                FactionId = "player"
            };

            // Forward to coordinated sync
            _coordinatedSync.HandlePlayerSpawned(playerId, spawnPosition, playerData);
        }

        private void HandlePlayerStateUpdate(GameMessage message)
        {
            string playerId = message.PlayerId;

            if (playerId == LocalPlayerId)
                return;

            // Update position if included
            if (message.Data.TryGetValue("x", out var xObj) &&
                message.Data.TryGetValue("y", out var yObj) &&
                message.Data.TryGetValue("z", out var zObj))
            {
                float x = Convert.ToSingle(xObj);
                float y = Convert.ToSingle(yObj);
                float z = Convert.ToSingle(zObj);

                _coordinatedSync.HandlePositionUpdate(playerId, new Position(x, y, z));
            }

            // Update health if included
            if (message.Data.TryGetValue("health", out var healthObj))
            {
                float health = Convert.ToSingle(healthObj);
                float maxHealth = message.Data.TryGetValue("maxHealth", out var maxObj)
                    ? Convert.ToSingle(maxObj)
                    : 100f;

                _coordinatedSync.HandleHealthUpdate(playerId, health, maxHealth);
            }
        }

        #endregion

        #region Public API

        /// <summary>
        /// Get information about all other players
        /// </summary>
        public List<OtherPlayerInfo> GetOtherPlayers()
        {
            return _coordinatedSync.GetOtherPlayers();
        }

        #endregion

        #region Disposal

        public void Dispose()
        {
            Stop();
            networkClient.MessageReceived -= OnNetworkMessageReceived;
            _coordinatedSync?.Dispose();
            Logger.Log(LOG_PREFIX + "MultiplayerSync disposed");
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

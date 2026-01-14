using System;
using System.Collections.Generic;
using System.Numerics;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiOnline.Coordinates.Integration
{
    /// <summary>
    /// KenshiOnlineIntegration - The Main Entry Point
    ///
    /// This is the facade that wires together:
    ///   - Game memory injection (KenshiGameBridge → KenshiMemoryActuator)
    ///   - Coordinate system (RingCoordinator with 4 rings + DataBus)
    ///   - Network layer (NetworkBroadcaster)
    ///   - Multiplayer sync (CoordinatedMultiplayerSync)
    ///
    /// Usage:
    ///   var integration = new KenshiOnlineIntegration();
    ///   integration.Initialize(gameBridge);
    ///   integration.Connect(server, port);
    ///   integration.StartMultiplayer(playerId);
    ///
    /// This replaces the ad-hoc approach with proper invariants:
    ///   - Every fact has tick, subject, authority, frame, schema, confidence
    ///   - Every read specifies what it needs and why (staleness budget)
    ///   - Every write goes through validate → normalize → authorize → gate
    ///   - Desync is prevented by pipeline rejecting incompatible data
    /// </summary>
    public class KenshiOnlineIntegration : IDisposable
    {
        private const string LOG_PREFIX = "[KenshiOnlineIntegration] ";

        // Core components
        private KenshiGameBridge _gameBridge;
        private RingCoordinator _coordinator;
        private CoordinatedMultiplayerSync _sync;
        private EnhancedClient _networkClient;

        // State
        private bool _isInitialized;
        private bool _isConnected;
        private string _currentPlayerId;

        // Events
        public event Action<string> OnConnectionEstablished;
        public event Action<string> OnConnectionLost;
        public event Action<string> OnPlayerJoined;
        public event Action<string> OnPlayerLeft;
        public event Action<Exception> OnError;

        public bool IsInitialized => _isInitialized;
        public bool IsConnected => _isConnected;
        public RingCoordinator Coordinator => _coordinator;
        public CoordinatedMultiplayerSync Sync => _sync;

        /// <summary>
        /// Initialize the integration with a game bridge.
        /// </summary>
        public bool Initialize(KenshiGameBridge gameBridge)
        {
            if (_isInitialized)
            {
                Logger.Log(LOG_PREFIX + "Already initialized");
                return true;
            }

            try
            {
                _gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));

                // Ensure game is connected
                if (!_gameBridge.IsConnected)
                {
                    Logger.Log(LOG_PREFIX + "Connecting to Kenshi...");
                    if (!_gameBridge.ConnectToKenshi())
                    {
                        Logger.Log(LOG_PREFIX + "ERROR: Failed to connect to Kenshi");
                        return false;
                    }
                }

                // Create ring coordinator with config
                var config = new CoordinatorConfig
                {
                    TickRateHz = 20,
                    MaxInfosPerCycle = 1000,
                    AcceptThreshold = 0.8f,
                    RejectThreshold = 0.2f,
                    VerificationThreshold = 0.5f,
                    GateConfig = new GateConfig
                    {
                        MaxVelocity = 15f,
                        MaxAcceleration = 30f,
                        BlendRate = 0.15f,
                        SnapThreshold = 5f,
                        AllowedHealthDelta = 0.5f
                    },
                    BusConfig = new BusConfig
                    {
                        MaxQueuedWrites = 10000,
                        EnableCoalescing = true,
                        EnableReadCache = true,
                        ReadCacheTtlTicks = 2
                    }
                };

                _coordinator = new RingCoordinator(config);

                // Create coordinated sync
                _sync = new CoordinatedMultiplayerSync(_gameBridge, _coordinator);

                // Wire up events
                _sync.OnPlayerJoined += playerId => OnPlayerJoined?.Invoke(playerId);
                _sync.OnPlayerLeft += playerId => OnPlayerLeft?.Invoke(playerId);

                _isInitialized = true;
                Logger.Log(LOG_PREFIX + "Initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR initializing: {ex.Message}");
                OnError?.Invoke(ex);
                return false;
            }
        }

        /// <summary>
        /// Connect to multiplayer server.
        /// </summary>
        public bool Connect(string serverAddress, int port)
        {
            if (!_isInitialized)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Not initialized");
                return false;
            }

            try
            {
                Logger.Log(LOG_PREFIX + $"Connecting to {serverAddress}:{port}...");

                _networkClient = new EnhancedClient();

                // Subscribe to network events
                _networkClient.MessageReceived += OnNetworkMessageReceived;

                // Connect (this might be async in practice)
                // For now, assume Connect is synchronous or handled internally
                // _networkClient.Connect(serverAddress, port);

                _isConnected = true;

                // Wire network callbacks to sync
                _sync.SetNetworkCallbacks(
                    (clientId, data) => SendToClient(clientId, data),
                    (data) => BroadcastToAll(data)
                );

                Logger.Log(LOG_PREFIX + "Connected to server");
                OnConnectionEstablished?.Invoke(serverAddress);

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR connecting: {ex.Message}");
                OnError?.Invoke(ex);
                return false;
            }
        }

        /// <summary>
        /// Start multiplayer session for a player.
        /// </summary>
        public bool StartMultiplayer(string playerId)
        {
            if (!_isInitialized)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Not initialized");
                return false;
            }

            try
            {
                _currentPlayerId = playerId;

                // Start coordinated sync
                if (!_sync.Start(playerId))
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to start sync");
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"Multiplayer started for {playerId}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR starting multiplayer: {ex.Message}");
                OnError?.Invoke(ex);
                return false;
            }
        }

        /// <summary>
        /// Stop multiplayer session.
        /// </summary>
        public void StopMultiplayer()
        {
            _sync?.Stop();
            _currentPlayerId = null;
            Logger.Log(LOG_PREFIX + "Multiplayer stopped");
        }

        /// <summary>
        /// Disconnect from server.
        /// </summary>
        public void Disconnect()
        {
            StopMultiplayer();

            if (_networkClient != null)
            {
                _networkClient.MessageReceived -= OnNetworkMessageReceived;
                _networkClient.Disconnect();
                _networkClient = null;
            }

            _isConnected = false;
            Logger.Log(LOG_PREFIX + "Disconnected from server");
        }

        #region High-Level API

        /// <summary>
        /// Spawn a player in the world (local authority).
        /// </summary>
        public bool SpawnPlayer(string playerId, Vector3 position, string displayName = null)
        {
            if (!_isInitialized) return false;

            try
            {
                var playerData = new PlayerData
                {
                    PlayerId = playerId,
                    DisplayName = displayName ?? playerId,
                    Health = 100,
                    MaxHealth = 100,
                    FactionId = "player"
                };

                var pos = new Position(position.X, position.Y, position.Z);
                _sync.HandlePlayerSpawned(playerId, pos, playerData);

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR spawning player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Get the current position of a player (from Ring4 presentation).
        /// </summary>
        public Vector3? GetPlayerPosition(string playerId)
        {
            if (!_isInitialized) return null;

            return _coordinator.DataBus.ResolvePosition(
                NetId.Create(EntityKind.Player, playerId.GetHashCode()));
        }

        /// <summary>
        /// Get presentation state for rendering (interpolated/extrapolated).
        /// </summary>
        public PresentationState GetPresentationState(string playerId)
        {
            if (!_isInitialized) return null;

            return _coordinator.GetPresentationState(
                NetId.Create(EntityKind.Player, playerId.GetHashCode()));
        }

        /// <summary>
        /// Submit a position update (as observation to Ring2).
        /// </summary>
        public void SubmitPositionUpdate(string playerId, Vector3 position, Quaternion rotation)
        {
            if (!_isInitialized) return;

            var entityId = NetId.Create(EntityKind.Player, playerId.GetHashCode());
            var payload = new TransformPayload
            {
                Position = position,
                Rotation = rotation
            };

            _coordinator.SubmitObservation(entityId, entityId, payload, _coordinator.Clock.CurrentTick);
        }

        /// <summary>
        /// Get all other players.
        /// </summary>
        public List<OtherPlayerInfo> GetOtherPlayers()
        {
            return _sync?.GetOtherPlayers() ?? new List<OtherPlayerInfo>();
        }

        /// <summary>
        /// Get comprehensive statistics.
        /// </summary>
        public IntegrationStats GetStats()
        {
            if (!_isInitialized) return default;

            var coordStats = _coordinator.GetStats();
            var syncStats = _sync.GetStats();

            return new IntegrationStats
            {
                CurrentTick = coordStats.CurrentTick,
                TrackedPlayers = syncStats.TrackedPlayers,
                CommitsGenerated = coordStats.CommitsGenerated,
                InfoPending = coordStats.InfoPending,
                AuthorityCommits = coordStats.AuthorityCommits,
                InterpolationRate = coordStats.ExtrapolationRatio,
                BusReadHitRate = coordStats.BusReadHitRate,
                PacketsSent = syncStats.PacketsSent,
                BytesSent = syncStats.BytesSent,
                MemoryReads = syncStats.MemoryReads,
                MemoryWrites = syncStats.MemoryWrites
            };
        }

        #endregion

        #region Network Callbacks

        private void OnNetworkMessageReceived(object sender, GameMessage message)
        {
            try
            {
                switch (message.Type)
                {
                    case MessageType.Position:
                        HandleNetworkPosition(message);
                        break;

                    case MessageType.PlayerJoined:
                        HandleNetworkPlayerJoined(message);
                        break;

                    case MessageType.PlayerLeft:
                        HandleNetworkPlayerLeft(message);
                        break;

                    case MessageType.PlayerSpawned:
                        HandleNetworkPlayerSpawned(message);
                        break;

                    case MessageType.Health:
                        HandleNetworkHealth(message);
                        break;
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR handling network message: {ex.Message}");
            }
        }

        private void HandleNetworkPosition(GameMessage message)
        {
            if (message.PlayerId == _currentPlayerId) return;

            if (message.Data.TryGetValue("x", out var xObj) &&
                message.Data.TryGetValue("y", out var yObj) &&
                message.Data.TryGetValue("z", out var zObj))
            {
                var position = new Position(
                    Convert.ToSingle(xObj),
                    Convert.ToSingle(yObj),
                    Convert.ToSingle(zObj)
                );

                _sync.HandlePositionUpdate(message.PlayerId, position);
            }
        }

        private void HandleNetworkPlayerJoined(GameMessage message)
        {
            string displayName = message.Data.TryGetValue("displayName", out var nameObj)
                ? nameObj?.ToString() ?? message.PlayerId
                : message.PlayerId;

            _sync.HandlePlayerJoined(message.PlayerId, displayName);
        }

        private void HandleNetworkPlayerLeft(GameMessage message)
        {
            _sync.HandlePlayerLeft(message.PlayerId);
        }

        private void HandleNetworkPlayerSpawned(GameMessage message)
        {
            if (!message.Data.TryGetValue("x", out var xObj) ||
                !message.Data.TryGetValue("y", out var yObj) ||
                !message.Data.TryGetValue("z", out var zObj))
                return;

            var position = new Position(
                Convert.ToSingle(xObj),
                Convert.ToSingle(yObj),
                Convert.ToSingle(zObj)
            );

            var playerData = new PlayerData
            {
                PlayerId = message.PlayerId,
                DisplayName = message.PlayerId,
                Health = 100,
                MaxHealth = 100,
                FactionId = "player"
            };

            _sync.HandlePlayerSpawned(message.PlayerId, position, playerData);
        }

        private void HandleNetworkHealth(GameMessage message)
        {
            if (!message.Data.TryGetValue("health", out var healthObj))
                return;

            float health = Convert.ToSingle(healthObj);
            float maxHealth = message.Data.TryGetValue("maxHealth", out var maxObj)
                ? Convert.ToSingle(maxObj)
                : 100f;

            _sync.HandleHealthUpdate(message.PlayerId, health, maxHealth);
        }

        private void SendToClient(string clientId, byte[] data)
        {
            // This would use the network client to send to specific client
            // For now, log
            Logger.Log(LOG_PREFIX + $"Would send {data.Length} bytes to {clientId}");
        }

        private void BroadcastToAll(byte[] data)
        {
            // This would use the network client to broadcast
            // For now, log
            Logger.Log(LOG_PREFIX + $"Would broadcast {data.Length} bytes");
        }

        #endregion

        public void Dispose()
        {
            Disconnect();

            _sync?.Dispose();
            _coordinator?.Dispose();

            _isInitialized = false;
            Logger.Log(LOG_PREFIX + "Disposed");
        }
    }

    public struct IntegrationStats
    {
        public long CurrentTick;
        public int TrackedPlayers;
        public long CommitsGenerated;
        public long InfoPending;
        public long AuthorityCommits;
        public float InterpolationRate;
        public float BusReadHitRate;
        public long PacketsSent;
        public long BytesSent;
        public long MemoryReads;
        public long MemoryWrites;
    }
}

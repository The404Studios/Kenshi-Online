using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiOnline.Coordinates.Integration
{
    /// <summary>
    /// CoordinatedMultiplayerSync - Ring-based replacement for MultiplayerSync.
    ///
    /// Instead of ad-hoc polling and interpolation:
    ///   1. Local player observations → Ring2 (InfoRing)
    ///   2. Ring3 commits → Network broadcast
    ///   3. Remote updates → Ring2 observations → Ring3 → Ring4
    ///   4. Ring4 presentation → Game memory via DataBus
    ///
    /// This provides proper authority semantics, staleness budgets, and
    /// batched writes instead of unbounded races.
    /// </summary>
    public class CoordinatedMultiplayerSync : IDisposable
    {
        private const string LOG_PREFIX = "[CoordinatedSync] ";
        private const int TICK_RATE_HZ = 20;  // 20 Hz = 50ms per tick

        // Core systems
        private readonly RingCoordinator _coordinator;
        private readonly KenshiMemoryActuator _memoryActuator;
        private readonly NetworkBroadcaster _broadcaster;
        private readonly KenshiGameBridge _gameBridge;

        // Entity tracking
        private readonly Dictionary<string, NetId> _playerNetIds = new();
        private readonly Dictionary<NetId, IntPtr> _entityHandles = new();
        private NetId _localPlayerId;
        private string _localPlayerStringId;

        // Sync state
        private Timer _syncTimer;
        private bool _isRunning;
        private readonly object _lock = new();

        // Events
        public event Action<string> OnPlayerJoined;
        public event Action<string> OnPlayerLeft;
        public event Action<string, Position> OnPlayerMoved;
        public event Action OnSyncStarted;
        public event Action OnSyncStopped;

        public bool IsRunning => _isRunning;
        public string LocalPlayerId => _localPlayerStringId;
        public RingCoordinator Coordinator => _coordinator;

        public CoordinatedMultiplayerSync(
            KenshiGameBridge gameBridge,
            RingCoordinator coordinator = null)
        {
            _gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));

            // Create memory actuator
            _memoryActuator = new KenshiMemoryActuator(gameBridge);

            // Create or use provided coordinator
            _coordinator = coordinator ?? new RingCoordinator();

            // Set memory actuator on coordinator
            _coordinator.SetMemoryActuator(_memoryActuator);

            // Create broadcaster
            _broadcaster = new NetworkBroadcaster(_coordinator);

            Logger.Log(LOG_PREFIX + "CoordinatedMultiplayerSync initialized");
        }

        #region Lifecycle

        /// <summary>
        /// Start coordinated sync after successful login.
        /// </summary>
        public bool Start(string playerId)
        {
            if (_isRunning)
            {
                Logger.Log(LOG_PREFIX + "Already running");
                return true;
            }

            if (!_gameBridge.IsConnected)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Game bridge not connected");
                return false;
            }

            lock (_lock)
            {
                _localPlayerStringId = playerId;

                // Create NetId for local player
                _localPlayerId = NetId.Create(EntityKind.Player, playerId.GetHashCode());
                _playerNetIds[playerId] = _localPlayerId;

                // Register local player in ContainerRing
                // We don't have a memory handle yet - it will be set when we read from game
                _coordinator.RegisterEntity(_localPlayerId, EntityKind.Player, IntPtr.Zero, FrameType.World);

                // Start the coordinator
                _coordinator.Start();

                // Start broadcaster
                _broadcaster.Start();

                // Start sync timer
                _syncTimer = new Timer(SyncTick, null, 0, 1000 / TICK_RATE_HZ);

                _isRunning = true;
            }

            OnSyncStarted?.Invoke();
            Logger.Log(LOG_PREFIX + $"Started for player {playerId}");

            return true;
        }

        /// <summary>
        /// Stop coordinated sync.
        /// </summary>
        public void Stop()
        {
            if (!_isRunning)
                return;

            lock (_lock)
            {
                _isRunning = false;

                _syncTimer?.Dispose();
                _syncTimer = null;

                _broadcaster.Stop();
                _coordinator.Stop();

                // Clean up tracked players
                foreach (var kvp in _playerNetIds)
                {
                    if (kvp.Value != _localPlayerId)
                    {
                        _gameBridge.DespawnPlayer(kvp.Key);
                    }
                }

                _playerNetIds.Clear();
                _entityHandles.Clear();
            }

            OnSyncStopped?.Invoke();
            Logger.Log(LOG_PREFIX + "Stopped");
        }

        #endregion

        #region Sync Tick

        private void SyncTick(object state)
        {
            if (!_isRunning)
                return;

            try
            {
                // 1. Read local player from game memory → Ring2 observation
                ObserveLocalPlayer();

                // 2. Process coordinator cycle (Ring2 → Ring3 → Ring4)
                var result = _coordinator.ProcessCycle();

                // 3. Broadcast authority commits to network
                BroadcastAuthorityCommits(result);

                // 4. Update game memory from Ring4 presentation states
                UpdatePresentationToMemory();
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR in sync tick: {ex.Message}");
            }
        }

        /// <summary>
        /// Read local player position from game memory and submit as observation.
        /// </summary>
        private void ObserveLocalPlayer()
        {
            var position = _gameBridge.GetLocalPlayerPosition();
            if (position == null)
                return;

            var transform = new TransformPayload
            {
                Position = new Vector3(position.X, position.Y, position.Z),
                Rotation = Quaternion.CreateFromYawPitchRoll(
                    position.RotY * MathF.PI / 180f,
                    position.RotX * MathF.PI / 180f,
                    position.RotZ * MathF.PI / 180f)
            };

            // Submit to Ring2 as observation
            _coordinator.SubmitObservation(
                _localPlayerId,
                _localPlayerId,  // Source is self
                transform,
                _coordinator.Clock.CurrentTick);
        }

        /// <summary>
        /// Broadcast authority commits to network.
        /// </summary>
        private void BroadcastAuthorityCommits(CycleResult result)
        {
            if (result.Committed <= 0)
                return;

            // Get recent commits from authority ring and broadcast
            // For now, we broadcast position of local player
            var state = _coordinator.AuthorityRing.GetEntityState(_localPlayerId);
            if (state?.Transform != null)
            {
                _broadcaster.BroadcastPosition(
                    _localPlayerId,
                    state.Transform.Position,
                    state.Transform.Rotation,
                    _coordinator.Clock.CurrentTick);
            }
        }

        /// <summary>
        /// Update game memory from Ring4 presentation states.
        /// Uses preconditioning through DataBus.
        /// </summary>
        private void UpdatePresentationToMemory()
        {
            lock (_lock)
            {
                // Get all entities that need presentation updates
                var entitiesToRender = new List<NetId>(_playerNetIds.Values);

                // Precondition render transforms (never blocks)
                var renderStates = _coordinator.DataBus.PreconditionRender(entitiesToRender);

                // Apply to game memory
                foreach (var kvp in renderStates)
                {
                    var entityId = kvp.Key;
                    var response = kvp.Value;

                    // Skip local player - game controls their position
                    if (entityId.Equals(_localPlayerId))
                        continue;

                    // Skip if no valid data
                    if (!response.IsValid)
                        continue;

                    // Get memory handle for entity
                    if (!_entityHandles.TryGetValue(entityId, out var handle) || handle == IntPtr.Zero)
                        continue;

                    // Write to memory
                    if (response.Value is TransformPayload tp)
                    {
                        _memoryActuator.WriteTransform(handle, tp.Position, tp.Rotation);
                    }
                    else if (response.Value is PresentationState ps)
                    {
                        _memoryActuator.WriteTransform(handle, ps.Position, ps.Rotation);
                    }
                }
            }
        }

        #endregion

        #region Player Management

        /// <summary>
        /// Handle remote player joining.
        /// </summary>
        public void HandlePlayerJoined(string playerId, string displayName)
        {
            if (playerId == _localPlayerStringId)
                return;

            lock (_lock)
            {
                if (_playerNetIds.ContainsKey(playerId))
                    return;

                // Create NetId for remote player
                var netId = NetId.Create(EntityKind.Player, playerId.GetHashCode());
                _playerNetIds[playerId] = netId;

                // Register in ContainerRing
                _coordinator.RegisterEntity(netId, EntityKind.Player, IntPtr.Zero, FrameType.World);

                Logger.Log(LOG_PREFIX + $"Player joined: {playerId} ({displayName})");
            }

            OnPlayerJoined?.Invoke(playerId);
        }

        /// <summary>
        /// Handle remote player spawning at a position.
        /// </summary>
        public void HandlePlayerSpawned(string playerId, Position position, PlayerData playerData)
        {
            if (playerId == _localPlayerStringId)
                return;

            lock (_lock)
            {
                NetId netId;
                if (!_playerNetIds.TryGetValue(playerId, out netId))
                {
                    netId = NetId.Create(EntityKind.Player, playerId.GetHashCode());
                    _playerNetIds[playerId] = netId;
                    _coordinator.RegisterEntity(netId, EntityKind.Player, IntPtr.Zero, FrameType.World);
                }

                // Spawn in game
                if (_gameBridge.SpawnPlayer(playerId, playerData, position))
                {
                    // Get the memory handle from game bridge's spawned characters
                    // This requires exposing the handle from KenshiGameBridge
                    // For now, track without handle
                    _entityHandles[netId] = IntPtr.Zero;  // Would be set by game bridge
                }

                // Submit initial position as observation
                var transform = new TransformPayload
                {
                    Position = new Vector3(position.X, position.Y, position.Z),
                    Rotation = Quaternion.Identity
                };

                _coordinator.SubmitObservation(netId, netId, transform, _coordinator.Clock.CurrentTick);

                Logger.Log(LOG_PREFIX + $"Player {playerId} spawned at ({position.X}, {position.Y}, {position.Z})");
            }
        }

        /// <summary>
        /// Handle remote player leaving.
        /// </summary>
        public void HandlePlayerLeft(string playerId)
        {
            lock (_lock)
            {
                if (_playerNetIds.TryGetValue(playerId, out var netId))
                {
                    // Unregister from coordinator
                    _coordinator.UnregisterEntity(netId);

                    // Despawn from game
                    _gameBridge.DespawnPlayer(playerId);

                    _playerNetIds.Remove(playerId);
                    _entityHandles.Remove(netId);

                    Logger.Log(LOG_PREFIX + $"Player left: {playerId}");
                }
            }

            OnPlayerLeft?.Invoke(playerId);
        }

        /// <summary>
        /// Handle position update from network.
        /// </summary>
        public void HandlePositionUpdate(string playerId, Position position)
        {
            if (playerId == _localPlayerStringId)
                return;

            lock (_lock)
            {
                if (!_playerNetIds.TryGetValue(playerId, out var netId))
                    return;

                // Submit as observation to Ring2
                var transform = new TransformPayload
                {
                    Position = new Vector3(position.X, position.Y, position.Z),
                    Rotation = Quaternion.CreateFromYawPitchRoll(
                        position.RotY * MathF.PI / 180f,
                        position.RotX * MathF.PI / 180f,
                        position.RotZ * MathF.PI / 180f)
                };

                // Source is the remote player (we trust them for their own position)
                _coordinator.SubmitObservation(netId, netId, transform, _coordinator.Clock.CurrentTick);
            }

            OnPlayerMoved?.Invoke(playerId, position);
        }

        /// <summary>
        /// Handle health update from network.
        /// </summary>
        public void HandleHealthUpdate(string playerId, float health, float maxHealth)
        {
            lock (_lock)
            {
                if (!_playerNetIds.TryGetValue(playerId, out var netId))
                    return;

                var healthPayload = new HealthPayload
                {
                    Current = health,
                    Maximum = maxHealth
                };

                _coordinator.SubmitObservation(netId, netId, healthPayload, _coordinator.Clock.CurrentTick);
            }
        }

        #endregion

        #region Network Integration

        /// <summary>
        /// Set network callbacks for broadcasting.
        /// </summary>
        public void SetNetworkCallbacks(
            Action<string, byte[]> sendToClient,
            Action<byte[]> broadcastToAll)
        {
            _broadcaster.SetNetworkCallbacks(sendToClient, broadcastToAll);
        }

        /// <summary>
        /// Process inbound network frame.
        /// </summary>
        public void ProcessInboundFrame(byte[] frameData, string sourceClientId)
        {
            _broadcaster.ProcessInboundFrame(frameData, sourceClientId);
        }

        #endregion

        #region Info Accessors

        /// <summary>
        /// Get other player information.
        /// </summary>
        public List<OtherPlayerInfo> GetOtherPlayers()
        {
            var result = new List<OtherPlayerInfo>();

            lock (_lock)
            {
                foreach (var kvp in _playerNetIds)
                {
                    if (kvp.Key == _localPlayerStringId)
                        continue;

                    var state = _coordinator.AuthorityRing.GetEntityState(kvp.Value);
                    var presentation = _coordinator.AttributeRing.GetPresentationState(
                        kvp.Value, _coordinator.Clock.Now.ContinuousTime);

                    Vector3 pos = presentation?.Position ?? state?.Transform?.Position ?? Vector3.Zero;
                    float health = state?.Health?.Current ?? 100f;

                    result.Add(new OtherPlayerInfo
                    {
                        PlayerId = kvp.Key,
                        DisplayName = kvp.Key,
                        Position = new Position(pos.X, pos.Y, pos.Z),
                        Health = health,
                        IsSpawned = state != null
                    });
                }
            }

            return result;
        }

        /// <summary>
        /// Get current statistics.
        /// </summary>
        public CoordinatedSyncStats GetStats()
        {
            var coordStats = _coordinator.GetStats();
            var actuatorStats = _memoryActuator.GetStats();
            var broadcasterStats = _broadcaster.GetStats();

            return new CoordinatedSyncStats
            {
                CurrentTick = coordStats.CurrentTick,
                TrackedPlayers = _playerNetIds.Count,
                CommitsGenerated = coordStats.CommitsGenerated,
                PacketsSent = broadcasterStats.PacketsSent,
                BytesSent = broadcasterStats.BytesSent,
                MemoryReads = actuatorStats.TotalReads,
                MemoryWrites = actuatorStats.TotalWrites
            };
        }

        #endregion

        public void Dispose()
        {
            Stop();
            _broadcaster?.Dispose();
            _coordinator?.Dispose();
            Logger.Log(LOG_PREFIX + "Disposed");
        }
    }

    public struct CoordinatedSyncStats
    {
        public long CurrentTick;
        public int TrackedPlayers;
        public long CommitsGenerated;
        public long PacketsSent;
        public long BytesSent;
        public long MemoryReads;
        public long MemoryWrites;
    }
}

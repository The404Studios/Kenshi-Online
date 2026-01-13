using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Diagnostics;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Deterministic server tick system.
    ///
    /// CONTRACT:
    /// - Server defines the simulation clock
    /// - Clients interpolate only - they never advance "truth time"
    /// - All state changes are tagged with ServerTickId
    /// - Drift beyond threshold triggers correction
    /// </summary>
    public class ServerTickSystem : IDisposable
    {
        private const string LOG_PREFIX = "[ServerTick] ";

        // Tick configuration
        public const int TICK_RATE_HZ = 20;                    // 20 Hz simulation
        public const int TICK_INTERVAL_MS = 1000 / TICK_RATE_HZ; // 50ms per tick
        public const int COMBAT_TICK_RATE_HZ = 30;             // 30 Hz for combat
        public const int COMBAT_TICK_INTERVAL_MS = 1000 / COMBAT_TICK_RATE_HZ;

        // Drift thresholds
        public const int MAX_CLIENT_DRIFT_TICKS = 5;           // Max ticks client can be ahead/behind
        public const int FORCE_RESYNC_DRIFT_TICKS = 10;        // Force full resync at this drift

        // Current state
        private long currentTickId = 0;
        private long currentCombatTickId = 0;
        private readonly Stopwatch tickTimer = new();
        private readonly object tickLock = new object();

        // Tick history for reconciliation
        private readonly ConcurrentDictionary<long, TickSnapshot> tickHistory = new();
        private const int MAX_TICK_HISTORY = 100;

        // Client drift tracking
        private readonly ConcurrentDictionary<string, ClientTickState> clientStates = new();

        // Tick callbacks
        private readonly List<Action<long, float>> tickCallbacks = new();
        private readonly List<Action<long, float>> combatTickCallbacks = new();

        // Threading
        private Timer mainTickTimer;
        private Timer combatTickTimer;
        private bool isRunning;

        // Events
        public event Action<string, int> OnClientDriftDetected;      // playerId, driftTicks
        public event Action<string> OnClientRequiresResync;          // playerId
        public event Action<long, TickSnapshot> OnTickCompleted;     // tickId, snapshot

        public long CurrentTickId => currentTickId;
        public long CurrentCombatTickId => currentCombatTickId;
        public bool IsRunning => isRunning;

        /// <summary>
        /// Start the tick system
        /// </summary>
        public void Start()
        {
            if (isRunning) return;

            isRunning = true;
            tickTimer.Start();

            // Main simulation tick (20 Hz)
            mainTickTimer = new Timer(
                ExecuteMainTick,
                null,
                0,
                TICK_INTERVAL_MS
            );

            // Combat tick (30 Hz)
            combatTickTimer = new Timer(
                ExecuteCombatTick,
                null,
                0,
                COMBAT_TICK_INTERVAL_MS
            );

            Logger.Log(LOG_PREFIX + $"Started - Main: {TICK_RATE_HZ}Hz, Combat: {COMBAT_TICK_RATE_HZ}Hz");
        }

        /// <summary>
        /// Stop the tick system
        /// </summary>
        public void Stop()
        {
            if (!isRunning) return;

            isRunning = false;
            mainTickTimer?.Dispose();
            combatTickTimer?.Dispose();
            tickTimer.Stop();

            Logger.Log(LOG_PREFIX + "Stopped");
        }

        /// <summary>
        /// Register a callback to be called every main tick
        /// </summary>
        public void OnTick(Action<long, float> callback)
        {
            lock (tickLock)
            {
                tickCallbacks.Add(callback);
            }
        }

        /// <summary>
        /// Register a callback for combat ticks
        /// </summary>
        public void OnCombatTick(Action<long, float> callback)
        {
            lock (tickLock)
            {
                combatTickCallbacks.Add(callback);
            }
        }

        /// <summary>
        /// Register a client for drift tracking
        /// </summary>
        public void RegisterClient(string playerId)
        {
            clientStates[playerId] = new ClientTickState
            {
                PlayerId = playerId,
                LastAcknowledgedTick = currentTickId,
                LastReceivedTick = currentTickId,
                DriftHistory = new Queue<int>()
            };
            Logger.Log(LOG_PREFIX + $"Client {playerId} registered at tick {currentTickId}");
        }

        /// <summary>
        /// Unregister a client
        /// </summary>
        public void UnregisterClient(string playerId)
        {
            clientStates.TryRemove(playerId, out _);
        }

        /// <summary>
        /// Process a client's tick acknowledgment
        /// </summary>
        public TickValidationResult ProcessClientTick(string playerId, long clientTickId)
        {
            var result = new TickValidationResult { IsValid = true };

            if (!clientStates.TryGetValue(playerId, out var clientState))
            {
                result.IsValid = false;
                result.Reason = "Client not registered";
                return result;
            }

            // Calculate drift
            long serverTick = currentTickId;
            int drift = (int)(clientTickId - serverTick);

            // Track drift history
            clientState.DriftHistory.Enqueue(drift);
            if (clientState.DriftHistory.Count > 10)
                clientState.DriftHistory.Dequeue();

            clientState.LastReceivedTick = clientTickId;
            clientState.CurrentDrift = drift;

            // Check for excessive drift
            if (Math.Abs(drift) > FORCE_RESYNC_DRIFT_TICKS)
            {
                result.IsValid = false;
                result.Reason = $"Excessive drift: {drift} ticks";
                result.RequiresResync = true;
                result.ServerTickId = serverTick;
                OnClientRequiresResync?.Invoke(playerId);
                Logger.Log(LOG_PREFIX + $"Client {playerId} requires resync - drift: {drift}");
            }
            else if (Math.Abs(drift) > MAX_CLIENT_DRIFT_TICKS)
            {
                result.DriftWarning = true;
                result.DriftAmount = drift;
                result.ServerTickId = serverTick;
                OnClientDriftDetected?.Invoke(playerId, drift);
                Logger.Log(LOG_PREFIX + $"Client {playerId} drift warning: {drift} ticks");
            }

            result.ServerTickId = serverTick;
            return result;
        }

        /// <summary>
        /// Acknowledge that client received a tick
        /// </summary>
        public void AcknowledgeClientTick(string playerId, long tickId)
        {
            if (clientStates.TryGetValue(playerId, out var state))
            {
                if (tickId > state.LastAcknowledgedTick)
                {
                    state.LastAcknowledgedTick = tickId;
                }
            }
        }

        /// <summary>
        /// Get tick snapshot for a specific tick (for reconciliation)
        /// </summary>
        public TickSnapshot GetTickSnapshot(long tickId)
        {
            tickHistory.TryGetValue(tickId, out var snapshot);
            return snapshot;
        }

        /// <summary>
        /// Get current server time info for clients
        /// </summary>
        public ServerTimeInfo GetServerTimeInfo()
        {
            return new ServerTimeInfo
            {
                CurrentTickId = currentTickId,
                CurrentCombatTickId = currentCombatTickId,
                TickRateHz = TICK_RATE_HZ,
                CombatTickRateHz = COMBAT_TICK_RATE_HZ,
                ServerTimeMs = tickTimer.ElapsedMilliseconds,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Get drift info for a client
        /// </summary>
        public ClientDriftInfo GetClientDrift(string playerId)
        {
            if (!clientStates.TryGetValue(playerId, out var state))
                return null;

            return new ClientDriftInfo
            {
                PlayerId = playerId,
                CurrentDrift = state.CurrentDrift,
                LastAcknowledgedTick = state.LastAcknowledgedTick,
                LastReceivedTick = state.LastReceivedTick,
                AverageDrift = CalculateAverageDrift(state)
            };
        }

        private void ExecuteMainTick(object state)
        {
            if (!isRunning) return;

            long tickId;
            float deltaTime = TICK_INTERVAL_MS / 1000f;

            lock (tickLock)
            {
                tickId = ++currentTickId;
            }

            try
            {
                // Execute all tick callbacks
                foreach (var callback in tickCallbacks)
                {
                    try
                    {
                        callback(tickId, deltaTime);
                    }
                    catch (Exception ex)
                    {
                        Logger.Log(LOG_PREFIX + $"Tick callback error: {ex.Message}");
                    }
                }

                // Create snapshot
                var snapshot = new TickSnapshot
                {
                    TickId = tickId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    DeltaTime = deltaTime
                };

                // Store in history
                tickHistory[tickId] = snapshot;

                // Clean old history
                CleanTickHistory();

                OnTickCompleted?.Invoke(tickId, snapshot);
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Main tick error: {ex.Message}");
            }
        }

        private void ExecuteCombatTick(object state)
        {
            if (!isRunning) return;

            long tickId;
            float deltaTime = COMBAT_TICK_INTERVAL_MS / 1000f;

            lock (tickLock)
            {
                tickId = ++currentCombatTickId;
            }

            try
            {
                foreach (var callback in combatTickCallbacks)
                {
                    try
                    {
                        callback(tickId, deltaTime);
                    }
                    catch (Exception ex)
                    {
                        Logger.Log(LOG_PREFIX + $"Combat tick callback error: {ex.Message}");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Combat tick error: {ex.Message}");
            }
        }

        private void CleanTickHistory()
        {
            if (tickHistory.Count > MAX_TICK_HISTORY)
            {
                long threshold = currentTickId - MAX_TICK_HISTORY;
                foreach (var key in tickHistory.Keys)
                {
                    if (key < threshold)
                    {
                        tickHistory.TryRemove(key, out _);
                    }
                }
            }
        }

        private float CalculateAverageDrift(ClientTickState state)
        {
            if (state.DriftHistory.Count == 0) return 0;
            float sum = 0;
            foreach (var d in state.DriftHistory)
                sum += d;
            return sum / state.DriftHistory.Count;
        }

        public void Dispose()
        {
            Stop();
        }
    }

    /// <summary>
    /// Result of validating a client's tick
    /// </summary>
    public class TickValidationResult
    {
        public bool IsValid { get; set; }
        public string Reason { get; set; }
        public bool DriftWarning { get; set; }
        public int DriftAmount { get; set; }
        public bool RequiresResync { get; set; }
        public long ServerTickId { get; set; }
    }

    /// <summary>
    /// Snapshot of server state at a specific tick
    /// </summary>
    public class TickSnapshot
    {
        public long TickId { get; set; }
        public long Timestamp { get; set; }
        public float DeltaTime { get; set; }
        public Dictionary<string, object> StateData { get; set; } = new();
    }

    /// <summary>
    /// Server time information for clients
    /// </summary>
    public class ServerTimeInfo
    {
        public long CurrentTickId { get; set; }
        public long CurrentCombatTickId { get; set; }
        public int TickRateHz { get; set; }
        public int CombatTickRateHz { get; set; }
        public long ServerTimeMs { get; set; }
        public long Timestamp { get; set; }
    }

    /// <summary>
    /// Client drift information
    /// </summary>
    public class ClientDriftInfo
    {
        public string PlayerId { get; set; }
        public int CurrentDrift { get; set; }
        public long LastAcknowledgedTick { get; set; }
        public long LastReceivedTick { get; set; }
        public float AverageDrift { get; set; }
    }

    /// <summary>
    /// Internal client tick tracking state
    /// </summary>
    internal class ClientTickState
    {
        public string PlayerId { get; set; }
        public long LastAcknowledgedTick { get; set; }
        public long LastReceivedTick { get; set; }
        public int CurrentDrift { get; set; }
        public Queue<int> DriftHistory { get; set; }
    }
}

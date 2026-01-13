using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Text.Json;
using System.Threading;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Structured Logging and Diagnostics System
    ///
    /// PRINCIPLE: Debugging multiplayer without logs is self-harm
    ///
    /// Features:
    /// - Per-tick server logs (ring buffer)
    /// - Network event IDs
    /// - Deterministic event ordering
    /// - Session replay from logs
    /// - "Desync diff" capability
    /// </summary>
    public class DiagnosticsLogger : IDisposable
    {
        private const string LOG_PREFIX = "[Diagnostics] ";

        // Configuration
        public const int RING_BUFFER_SIZE = 10000;        // Keep last 10k events
        public const int FLUSH_INTERVAL_MS = 5000;        // Flush every 5 seconds
        public const int MAX_LOG_FILE_SIZE_MB = 100;      // Max log file size

        // Ring buffer for events
        private readonly NetworkEvent[] eventBuffer;
        private int bufferHead = 0;
        private int bufferCount = 0;
        private readonly object bufferLock = new object();

        // Event ID generation
        private long eventIdCounter = 0;

        // File logging
        private readonly string logDirectory;
        private StreamWriter currentLogWriter;
        private string currentLogFile;
        private Timer flushTimer;

        // Session tracking
        private readonly ConcurrentDictionary<string, SessionDiagnostics> sessionDiagnostics = new();

        // State snapshots for diff
        private readonly ConcurrentDictionary<long, StateSnapshot> stateSnapshots = new();
        private const int MAX_SNAPSHOTS = 100;

        public DiagnosticsLogger(string logPath)
        {
            logDirectory = logPath;
            Directory.CreateDirectory(logDirectory);

            eventBuffer = new NetworkEvent[RING_BUFFER_SIZE];

            // Start new log file
            RotateLogFile();

            // Start flush timer
            flushTimer = new Timer(FlushToFile, null, FLUSH_INTERVAL_MS, FLUSH_INTERVAL_MS);

            Logger.Log(LOG_PREFIX + $"Diagnostics logger initialized at {logDirectory}");
        }

        #region Event Logging

        /// <summary>
        /// Log a network event
        /// </summary>
        public long LogEvent(NetworkEventType type, string playerId, string details,
            Dictionary<string, object> data = null)
        {
            var evt = new NetworkEvent
            {
                EventId = Interlocked.Increment(ref eventIdCounter),
                Type = type,
                PlayerId = playerId,
                Details = details,
                Data = data ?? new Dictionary<string, object>(),
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                ServerTick = GetCurrentTick()
            };

            AddToBuffer(evt);
            UpdateSessionDiagnostics(playerId, evt);

            return evt.EventId;
        }

        /// <summary>
        /// Log a tick event
        /// </summary>
        public void LogTick(long tickId, int playerCount, int entityCount, float deltaTime)
        {
            LogEvent(NetworkEventType.Tick, "SERVER", $"Tick {tickId}",
                new Dictionary<string, object>
                {
                    { "tickId", tickId },
                    { "playerCount", playerCount },
                    { "entityCount", entityCount },
                    { "deltaTime", deltaTime }
                });
        }

        /// <summary>
        /// Log a position update
        /// </summary>
        public void LogPosition(string playerId, float x, float y, float z, bool wasValidated, bool wasCorrected)
        {
            LogEvent(NetworkEventType.PositionUpdate, playerId, $"Pos: ({x:F1}, {y:F1}, {z:F1})",
                new Dictionary<string, object>
                {
                    { "x", x },
                    { "y", y },
                    { "z", z },
                    { "validated", wasValidated },
                    { "corrected", wasCorrected }
                });
        }

        /// <summary>
        /// Log a combat event
        /// </summary>
        public void LogCombat(string attackerId, string targetId, string action, int damage, bool wasValidated)
        {
            LogEvent(NetworkEventType.CombatAction, attackerId, $"Combat: {action} -> {targetId}",
                new Dictionary<string, object>
                {
                    { "targetId", targetId },
                    { "action", action },
                    { "damage", damage },
                    { "validated", wasValidated }
                });
        }

        /// <summary>
        /// Log a connection event
        /// </summary>
        public void LogConnection(string playerId, bool connected, string reason = null)
        {
            var type = connected ? NetworkEventType.PlayerConnected : NetworkEventType.PlayerDisconnected;
            LogEvent(type, playerId, connected ? "Connected" : $"Disconnected: {reason}",
                new Dictionary<string, object>
                {
                    { "connected", connected },
                    { "reason", reason }
                });
        }

        /// <summary>
        /// Log a violation
        /// </summary>
        public void LogViolation(string playerId, string violationType, string details)
        {
            LogEvent(NetworkEventType.Violation, playerId, $"Violation: {violationType}",
                new Dictionary<string, object>
                {
                    { "violationType", violationType },
                    { "details", details }
                });
        }

        /// <summary>
        /// Log a desync event
        /// </summary>
        public void LogDesync(string playerId, string component, object clientValue, object serverValue)
        {
            LogEvent(NetworkEventType.Desync, playerId, $"Desync in {component}",
                new Dictionary<string, object>
                {
                    { "component", component },
                    { "clientValue", clientValue },
                    { "serverValue", serverValue }
                });
        }

        #endregion

        #region State Snapshots

        /// <summary>
        /// Take a state snapshot for diff comparison
        /// </summary>
        public void TakeSnapshot(long tickId, Dictionary<string, PlayerStateSnapshot> playerStates)
        {
            var snapshot = new StateSnapshot
            {
                TickId = tickId,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                PlayerStates = playerStates
            };

            stateSnapshots[tickId] = snapshot;

            // Clean old snapshots
            if (stateSnapshots.Count > MAX_SNAPSHOTS)
            {
                long threshold = tickId - MAX_SNAPSHOTS;
                foreach (var key in stateSnapshots.Keys)
                {
                    if (key < threshold)
                        stateSnapshots.TryRemove(key, out _);
                }
            }
        }

        /// <summary>
        /// Compare two snapshots and return differences
        /// </summary>
        public DesyncDiff CompareSnapshots(long tick1, long tick2)
        {
            var diff = new DesyncDiff { Tick1 = tick1, Tick2 = tick2 };

            if (!stateSnapshots.TryGetValue(tick1, out var snap1) ||
                !stateSnapshots.TryGetValue(tick2, out var snap2))
            {
                diff.Error = "Snapshot(s) not found";
                return diff;
            }

            // Compare player states
            foreach (var kvp in snap1.PlayerStates)
            {
                string playerId = kvp.Key;
                var state1 = kvp.Value;

                if (snap2.PlayerStates.TryGetValue(playerId, out var state2))
                {
                    // Compare positions
                    float posDiff = MathF.Sqrt(
                        MathF.Pow(state1.X - state2.X, 2) +
                        MathF.Pow(state1.Y - state2.Y, 2) +
                        MathF.Pow(state1.Z - state2.Z, 2));

                    if (posDiff > 0.01f)
                    {
                        diff.PositionDiffs[playerId] = new PositionDiff
                        {
                            State1 = $"({state1.X:F2}, {state1.Y:F2}, {state1.Z:F2})",
                            State2 = $"({state2.X:F2}, {state2.Y:F2}, {state2.Z:F2})",
                            Distance = posDiff
                        };
                    }

                    // Compare health
                    if (Math.Abs(state1.Health - state2.Health) > 0.1f)
                    {
                        diff.HealthDiffs[playerId] = new ValueDiff
                        {
                            Value1 = state1.Health,
                            Value2 = state2.Health
                        };
                    }
                }
                else
                {
                    diff.MissingInSnap2.Add(playerId);
                }
            }

            // Check for players in snap2 not in snap1
            foreach (var playerId in snap2.PlayerStates.Keys)
            {
                if (!snap1.PlayerStates.ContainsKey(playerId))
                {
                    diff.MissingInSnap1.Add(playerId);
                }
            }

            return diff;
        }

        #endregion

        #region Session Diagnostics

        private void UpdateSessionDiagnostics(string playerId, NetworkEvent evt)
        {
            if (string.IsNullOrEmpty(playerId) || playerId == "SERVER")
                return;

            var diag = sessionDiagnostics.GetOrAdd(playerId, _ => new SessionDiagnostics { PlayerId = playerId });

            diag.TotalEvents++;
            diag.LastEventTime = evt.Timestamp;

            switch (evt.Type)
            {
                case NetworkEventType.PositionUpdate:
                    diag.PositionUpdates++;
                    break;
                case NetworkEventType.CombatAction:
                    diag.CombatActions++;
                    break;
                case NetworkEventType.Violation:
                    diag.Violations++;
                    break;
                case NetworkEventType.Desync:
                    diag.Desyncs++;
                    break;
            }
        }

        /// <summary>
        /// Get diagnostics for a player session
        /// </summary>
        public SessionDiagnostics GetSessionDiagnostics(string playerId)
        {
            sessionDiagnostics.TryGetValue(playerId, out var diag);
            return diag;
        }

        /// <summary>
        /// Get all session diagnostics
        /// </summary>
        public Dictionary<string, SessionDiagnostics> GetAllSessionDiagnostics()
        {
            return new Dictionary<string, SessionDiagnostics>(sessionDiagnostics);
        }

        #endregion

        #region Query Events

        /// <summary>
        /// Get recent events from buffer
        /// </summary>
        public List<NetworkEvent> GetRecentEvents(int count = 100)
        {
            var events = new List<NetworkEvent>();

            lock (bufferLock)
            {
                int start = (bufferHead - Math.Min(count, bufferCount) + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;

                for (int i = 0; i < Math.Min(count, bufferCount); i++)
                {
                    int idx = (start + i) % RING_BUFFER_SIZE;
                    if (eventBuffer[idx] != null)
                        events.Add(eventBuffer[idx]);
                }
            }

            return events;
        }

        /// <summary>
        /// Get events for a specific player
        /// </summary>
        public List<NetworkEvent> GetPlayerEvents(string playerId, int count = 100)
        {
            var events = new List<NetworkEvent>();

            lock (bufferLock)
            {
                int scanned = 0;
                int idx = (bufferHead - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;

                while (scanned < bufferCount && events.Count < count)
                {
                    var evt = eventBuffer[idx];
                    if (evt != null && evt.PlayerId == playerId)
                    {
                        events.Add(evt);
                    }

                    idx = (idx - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
                    scanned++;
                }
            }

            events.Reverse();
            return events;
        }

        /// <summary>
        /// Get events by type
        /// </summary>
        public List<NetworkEvent> GetEventsByType(NetworkEventType type, int count = 100)
        {
            var events = new List<NetworkEvent>();

            lock (bufferLock)
            {
                int scanned = 0;
                int idx = (bufferHead - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;

                while (scanned < bufferCount && events.Count < count)
                {
                    var evt = eventBuffer[idx];
                    if (evt != null && evt.Type == type)
                    {
                        events.Add(evt);
                    }

                    idx = (idx - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
                    scanned++;
                }
            }

            events.Reverse();
            return events;
        }

        #endregion

        #region File Operations

        private void AddToBuffer(NetworkEvent evt)
        {
            lock (bufferLock)
            {
                eventBuffer[bufferHead] = evt;
                bufferHead = (bufferHead + 1) % RING_BUFFER_SIZE;
                if (bufferCount < RING_BUFFER_SIZE)
                    bufferCount++;
            }
        }

        private void FlushToFile(object state)
        {
            try
            {
                lock (bufferLock)
                {
                    if (currentLogWriter == null)
                        return;

                    // Check file size
                    var fileInfo = new FileInfo(currentLogFile);
                    if (fileInfo.Exists && fileInfo.Length > MAX_LOG_FILE_SIZE_MB * 1024 * 1024)
                    {
                        RotateLogFile();
                    }
                }

                // Flush any buffered writes
                currentLogWriter?.Flush();
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Flush error: {ex.Message}");
            }
        }

        private void RotateLogFile()
        {
            currentLogWriter?.Close();

            string timestamp = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss");
            currentLogFile = Path.Combine(logDirectory, $"network_{timestamp}.jsonl");
            currentLogWriter = new StreamWriter(currentLogFile, append: true);

            Logger.Log(LOG_PREFIX + $"Rotated to new log file: {currentLogFile}");
        }

        /// <summary>
        /// Write event to log file
        /// </summary>
        private void WriteToLog(NetworkEvent evt)
        {
            try
            {
                string json = JsonSerializer.Serialize(evt);
                currentLogWriter?.WriteLine(json);
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Write error: {ex.Message}");
            }
        }

        /// <summary>
        /// Export events to file for replay
        /// </summary>
        public string ExportForReplay(long fromTick, long toTick)
        {
            string exportFile = Path.Combine(logDirectory, $"replay_{fromTick}_{toTick}.jsonl");

            lock (bufferLock)
            {
                using var writer = new StreamWriter(exportFile);

                for (int i = 0; i < bufferCount; i++)
                {
                    int idx = (bufferHead - bufferCount + i + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
                    var evt = eventBuffer[idx];

                    if (evt != null && evt.ServerTick >= fromTick && evt.ServerTick <= toTick)
                    {
                        string json = JsonSerializer.Serialize(evt);
                        writer.WriteLine(json);
                    }
                }
            }

            Logger.Log(LOG_PREFIX + $"Exported replay: {exportFile}");
            return exportFile;
        }

        #endregion

        private long GetCurrentTick()
        {
            // This would be set by the tick system
            return 0;
        }

        public void Dispose()
        {
            flushTimer?.Dispose();
            currentLogWriter?.Flush();
            currentLogWriter?.Close();
        }
    }

    #region Data Types

    public enum NetworkEventType
    {
        Tick,
        PlayerConnected,
        PlayerDisconnected,
        PositionUpdate,
        CombatAction,
        InventoryChange,
        ChatMessage,
        Violation,
        Desync,
        Resync,
        Heartbeat,
        Error
    }

    public class NetworkEvent
    {
        public long EventId { get; set; }
        public NetworkEventType Type { get; set; }
        public string PlayerId { get; set; }
        public string Details { get; set; }
        public Dictionary<string, object> Data { get; set; }
        public long Timestamp { get; set; }
        public long ServerTick { get; set; }
    }

    public class SessionDiagnostics
    {
        public string PlayerId { get; set; }
        public long TotalEvents { get; set; }
        public long PositionUpdates { get; set; }
        public long CombatActions { get; set; }
        public long Violations { get; set; }
        public long Desyncs { get; set; }
        public long LastEventTime { get; set; }
    }

    public class StateSnapshot
    {
        public long TickId { get; set; }
        public long Timestamp { get; set; }
        public Dictionary<string, PlayerStateSnapshot> PlayerStates { get; set; }
    }

    public class PlayerStateSnapshot
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }
        public string State { get; set; }
    }

    public class DesyncDiff
    {
        public long Tick1 { get; set; }
        public long Tick2 { get; set; }
        public string Error { get; set; }
        public Dictionary<string, PositionDiff> PositionDiffs { get; set; } = new();
        public Dictionary<string, ValueDiff> HealthDiffs { get; set; } = new();
        public List<string> MissingInSnap1 { get; set; } = new();
        public List<string> MissingInSnap2 { get; set; } = new();
    }

    public class PositionDiff
    {
        public string State1 { get; set; }
        public string State2 { get; set; }
        public float Distance { get; set; }
    }

    public class ValueDiff
    {
        public float Value1 { get; set; }
        public float Value2 { get; set; }
    }

    #endregion
}

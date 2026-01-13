using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Session Recovery and Graceful Degradation System
    ///
    /// RECOVERY FLOW:
    /// 1. Client disconnects
    /// 2. Server saves session state
    /// 3. AI takes control of player character
    /// 4. Client reconnects
    /// 5. Server sends snapshot
    /// 6. Client discards local state
    /// 7. Control restored
    ///
    /// DEGRADATION POLICIES:
    /// - Latency spikes: Increase interpolation buffer
    /// - Packet loss: Request retransmission, fallback to snapshot
    /// - Player drops mid-combat: AI takes over, character invulnerable briefly
    /// </summary>
    public class SessionRecoveryManager
    {
        private const string LOG_PREFIX = "[SessionRecovery] ";

        // Configuration
        public const int SESSION_PRESERVE_DURATION_MS = 300000;  // 5 minutes to reconnect
        public const int HEARTBEAT_INTERVAL_MS = 5000;           // 5 second heartbeat
        public const int HEARTBEAT_TIMEOUT_MS = 15000;           // 15 second timeout
        public const int AI_TAKEOVER_DELAY_MS = 3000;            // 3 seconds before AI takes over
        public const int INVULNERABILITY_DURATION_MS = 5000;     // 5 second invuln on disconnect

        // Preserved sessions (for reconnect)
        private readonly ConcurrentDictionary<string, PreservedSession> preservedSessions = new();

        // Active heartbeat tracking
        private readonly ConcurrentDictionary<string, HeartbeatState> heartbeatStates = new();

        // AI control tracking
        private readonly ConcurrentDictionary<string, AIControlState> aiControlledPlayers = new();

        // Cleanup timer
        private Timer cleanupTimer;

        // Events
        public event Action<string> OnHeartbeatTimeout;                    // playerId
        public event Action<string, PreservedSession> OnSessionPreserved;  // playerId, session
        public event Action<string> OnAITakeover;                          // playerId
        public event Action<string> OnPlayerReconnected;                   // playerId
        public event Func<string, ResyncPacket> OnResyncRequested;         // playerId -> packet

        public SessionRecoveryManager()
        {
            // Start cleanup timer (runs every 30 seconds)
            cleanupTimer = new Timer(CleanupExpiredSessions, null, 30000, 30000);
        }

        #region Heartbeat Management

        /// <summary>
        /// Register a client for heartbeat monitoring
        /// </summary>
        public void RegisterHeartbeat(string playerId)
        {
            heartbeatStates[playerId] = new HeartbeatState
            {
                PlayerId = playerId,
                LastHeartbeat = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                IsHealthy = true
            };
            Logger.Log(LOG_PREFIX + $"Heartbeat registered: {playerId}");
        }

        /// <summary>
        /// Process a heartbeat from client
        /// </summary>
        public HeartbeatResponse ProcessHeartbeat(string playerId, long clientTime)
        {
            var response = new HeartbeatResponse
            {
                ServerTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                NextHeartbeatMs = HEARTBEAT_INTERVAL_MS
            };

            if (heartbeatStates.TryGetValue(playerId, out var state))
            {
                state.LastHeartbeat = response.ServerTime;
                state.IsHealthy = true;
                state.ConsecutiveMisses = 0;

                // Calculate latency
                response.EstimatedLatencyMs = (int)(response.ServerTime - clientTime) / 2;
                state.LatencyMs = response.EstimatedLatencyMs;
            }

            return response;
        }

        /// <summary>
        /// Check for timed out clients (call periodically)
        /// </summary>
        public List<string> CheckHeartbeatTimeouts()
        {
            var timedOut = new List<string>();
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            foreach (var kvp in heartbeatStates)
            {
                var state = kvp.Value;
                long elapsed = now - state.LastHeartbeat;

                if (elapsed > HEARTBEAT_TIMEOUT_MS)
                {
                    state.ConsecutiveMisses++;
                    state.IsHealthy = false;

                    if (!state.TimeoutTriggered)
                    {
                        state.TimeoutTriggered = true;
                        timedOut.Add(kvp.Key);
                        OnHeartbeatTimeout?.Invoke(kvp.Key);
                        Logger.Log(LOG_PREFIX + $"Heartbeat timeout: {kvp.Key}");
                    }
                }
            }

            return timedOut;
        }

        /// <summary>
        /// Get latency for a client
        /// </summary>
        public int GetClientLatency(string playerId)
        {
            if (heartbeatStates.TryGetValue(playerId, out var state))
                return state.LatencyMs;
            return -1;
        }

        #endregion

        #region Session Preservation

        /// <summary>
        /// Preserve a session when client disconnects
        /// </summary>
        public void PreserveSession(string playerId, PlayerSaveData saveData, object worldState)
        {
            var session = new PreservedSession
            {
                PlayerId = playerId,
                SaveData = saveData,
                WorldState = worldState,
                DisconnectTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                ExpiresAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + SESSION_PRESERVE_DURATION_MS
            };

            preservedSessions[playerId] = session;
            OnSessionPreserved?.Invoke(playerId, session);

            Logger.Log(LOG_PREFIX + $"Session preserved: {playerId} (expires in {SESSION_PRESERVE_DURATION_MS / 1000}s)");

            // Schedule AI takeover
            Task.Delay(AI_TAKEOVER_DELAY_MS).ContinueWith(_ => TriggerAITakeover(playerId));
        }

        /// <summary>
        /// Check if a preserved session exists
        /// </summary>
        public bool HasPreservedSession(string playerId)
        {
            if (preservedSessions.TryGetValue(playerId, out var session))
            {
                return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() < session.ExpiresAt;
            }
            return false;
        }

        /// <summary>
        /// Restore a session on reconnect
        /// </summary>
        public SessionRestoreResult RestoreSession(string playerId)
        {
            var result = new SessionRestoreResult { Success = false };

            if (!preservedSessions.TryRemove(playerId, out var session))
            {
                result.Reason = "No preserved session found";
                return result;
            }

            // Check if expired
            if (DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() > session.ExpiresAt)
            {
                result.Reason = "Session expired";
                return result;
            }

            result.Success = true;
            result.SaveData = session.SaveData;
            result.WorldState = session.WorldState;
            result.DisconnectDurationMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - session.DisconnectTime;

            // Remove AI control
            RemoveAIControl(playerId);

            // Re-register heartbeat
            RegisterHeartbeat(playerId);

            OnPlayerReconnected?.Invoke(playerId);
            Logger.Log(LOG_PREFIX + $"Session restored: {playerId} (was disconnected {result.DisconnectDurationMs}ms)");

            return result;
        }

        #endregion

        #region AI Takeover

        private void TriggerAITakeover(string playerId)
        {
            // Only take over if still disconnected
            if (!preservedSessions.ContainsKey(playerId))
                return;

            if (aiControlledPlayers.ContainsKey(playerId))
                return;

            var aiState = new AIControlState
            {
                PlayerId = playerId,
                TakeoverTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                IsInvulnerable = true,
                InvulnerabilityEndsAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + INVULNERABILITY_DURATION_MS,
                Behavior = AIBehavior.Defensive
            };

            aiControlledPlayers[playerId] = aiState;
            OnAITakeover?.Invoke(playerId);

            Logger.Log(LOG_PREFIX + $"AI takeover: {playerId} (invulnerable for {INVULNERABILITY_DURATION_MS}ms)");
        }

        private void RemoveAIControl(string playerId)
        {
            if (aiControlledPlayers.TryRemove(playerId, out _))
            {
                Logger.Log(LOG_PREFIX + $"AI control removed: {playerId}");
            }
        }

        /// <summary>
        /// Check if player is AI controlled
        /// </summary>
        public bool IsAIControlled(string playerId)
        {
            return aiControlledPlayers.ContainsKey(playerId);
        }

        /// <summary>
        /// Check if player is invulnerable (disconnect protection)
        /// </summary>
        public bool IsInvulnerable(string playerId)
        {
            if (aiControlledPlayers.TryGetValue(playerId, out var state))
            {
                if (state.IsInvulnerable)
                {
                    if (DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() > state.InvulnerabilityEndsAt)
                    {
                        state.IsInvulnerable = false;
                        return false;
                    }
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// Get AI behavior for a player
        /// </summary>
        public AIBehavior GetAIBehavior(string playerId)
        {
            if (aiControlledPlayers.TryGetValue(playerId, out var state))
                return state.Behavior;
            return AIBehavior.None;
        }

        #endregion

        #region State Resync

        /// <summary>
        /// Create a resync packet for a client
        /// </summary>
        public ResyncPacket CreateResyncPacket(string playerId, PlayerSaveData saveData,
            long currentTick, Dictionary<string, object> worldState)
        {
            return new ResyncPacket
            {
                PlayerId = playerId,
                SaveData = saveData,
                CurrentServerTick = currentTick,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                WorldState = worldState,
                ResyncReason = "Reconnection"
            };
        }

        /// <summary>
        /// Request resync from external handler
        /// </summary>
        public ResyncPacket RequestResync(string playerId)
        {
            return OnResyncRequested?.Invoke(playerId);
        }

        #endregion

        #region Graceful Degradation

        /// <summary>
        /// Get degradation policy based on current conditions
        /// </summary>
        public DegradationPolicy GetDegradationPolicy(string playerId)
        {
            var policy = new DegradationPolicy();

            if (heartbeatStates.TryGetValue(playerId, out var state))
            {
                // High latency: increase interpolation
                if (state.LatencyMs > 200)
                {
                    policy.InterpolationBufferMs = Math.Min(state.LatencyMs * 2, 500);
                    policy.ReduceUpdateRate = true;
                }

                // Very high latency: reduce quality
                if (state.LatencyMs > 500)
                {
                    policy.ReduceSyncScope = true;
                    policy.DisableNonEssentialSync = true;
                }

                // Unhealthy connection
                if (!state.IsHealthy)
                {
                    policy.PrepareForDisconnect = true;
                }
            }

            return policy;
        }

        #endregion

        #region Cleanup

        private void CleanupExpiredSessions(object state)
        {
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            foreach (var kvp in preservedSessions)
            {
                if (now > kvp.Value.ExpiresAt)
                {
                    if (preservedSessions.TryRemove(kvp.Key, out _))
                    {
                        aiControlledPlayers.TryRemove(kvp.Key, out _);
                        Logger.Log(LOG_PREFIX + $"Expired session cleaned up: {kvp.Key}");
                    }
                }
            }
        }

        public void Dispose()
        {
            cleanupTimer?.Dispose();
        }

        #endregion
    }

    #region Data Classes

    public class PreservedSession
    {
        public string PlayerId { get; set; }
        public PlayerSaveData SaveData { get; set; }
        public object WorldState { get; set; }
        public long DisconnectTime { get; set; }
        public long ExpiresAt { get; set; }
    }

    public class HeartbeatState
    {
        public string PlayerId { get; set; }
        public long LastHeartbeat { get; set; }
        public bool IsHealthy { get; set; }
        public int ConsecutiveMisses { get; set; }
        public int LatencyMs { get; set; }
        public bool TimeoutTriggered { get; set; }
    }

    public class HeartbeatResponse
    {
        public long ServerTime { get; set; }
        public int NextHeartbeatMs { get; set; }
        public int EstimatedLatencyMs { get; set; }
    }

    public class SessionRestoreResult
    {
        public bool Success { get; set; }
        public string Reason { get; set; }
        public PlayerSaveData SaveData { get; set; }
        public object WorldState { get; set; }
        public long DisconnectDurationMs { get; set; }
    }

    public class AIControlState
    {
        public string PlayerId { get; set; }
        public long TakeoverTime { get; set; }
        public bool IsInvulnerable { get; set; }
        public long InvulnerabilityEndsAt { get; set; }
        public AIBehavior Behavior { get; set; }
    }

    public enum AIBehavior
    {
        None,
        Idle,           // Stand still
        Defensive,      // Block, avoid combat
        Flee,           // Run to safety
        Aggressive      // Continue fighting (only if was in combat)
    }

    public class ResyncPacket
    {
        public string PlayerId { get; set; }
        public PlayerSaveData SaveData { get; set; }
        public long CurrentServerTick { get; set; }
        public long Timestamp { get; set; }
        public Dictionary<string, object> WorldState { get; set; }
        public string ResyncReason { get; set; }
    }

    public class DegradationPolicy
    {
        public int InterpolationBufferMs { get; set; } = 100;
        public bool ReduceUpdateRate { get; set; }
        public bool ReduceSyncScope { get; set; }
        public bool DisableNonEssentialSync { get; set; }
        public bool PrepareForDisconnect { get; set; }
    }

    #endregion
}

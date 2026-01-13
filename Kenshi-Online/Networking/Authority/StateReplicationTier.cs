using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// State replication tiers define how different types of data are synchronized.
    /// Each tier has its own replication rate, reconciliation rules, and persistence behavior.
    /// </summary>
    public enum ReplicationTier
    {
        /// <summary>
        /// Tier 0: Transient data - high frequency, no persistence
        /// Examples: Position, rotation, velocity, animation state
        /// </summary>
        Transient = 0,

        /// <summary>
        /// Tier 1: Event data - on-demand, short-term tracking
        /// Examples: Attack actions, hit events, item pickups, ability activations
        /// </summary>
        Event = 1,

        /// <summary>
        /// Tier 2: Persistent data - low frequency, full persistence
        /// Examples: Inventory, stats, faction relations, quest progress
        /// </summary>
        Persistent = 2
    }

    /// <summary>
    /// Configuration for each replication tier
    /// </summary>
    public class TierConfiguration
    {
        public ReplicationTier Tier { get; init; }
        public int ReplicationRateHz { get; init; }
        public int ReconciliationWindowMs { get; init; }
        public bool PersistToSave { get; init; }
        public int RetryCount { get; init; }
        public bool RequiresAcknowledgment { get; init; }
        public ConflictResolution ConflictStrategy { get; init; }
    }

    public enum ConflictResolution
    {
        /// <summary>Server state always wins</summary>
        ServerWins,
        /// <summary>Most recent timestamp wins</summary>
        LastWriteWins,
        /// <summary>Merge changes if possible</summary>
        Merge,
        /// <summary>Reject conflicting changes</summary>
        Reject
    }

    /// <summary>
    /// Static configuration for all replication tiers
    /// </summary>
    public static class TierConfig
    {
        public static readonly Dictionary<ReplicationTier, TierConfiguration> Configurations = new()
        {
            {
                ReplicationTier.Transient, new TierConfiguration
                {
                    Tier = ReplicationTier.Transient,
                    ReplicationRateHz = 20,              // 20 Hz (50ms) - fast for smooth movement
                    ReconciliationWindowMs = 200,        // 200ms window for corrections
                    PersistToSave = false,               // Not saved
                    RetryCount = 0,                      // No retry - just use latest
                    RequiresAcknowledgment = false,      // Fire and forget
                    ConflictStrategy = ConflictResolution.ServerWins
                }
            },
            {
                ReplicationTier.Event, new TierConfiguration
                {
                    Tier = ReplicationTier.Event,
                    ReplicationRateHz = 30,              // 30 Hz for combat precision
                    ReconciliationWindowMs = 500,        // 500ms for event ordering
                    PersistToSave = false,               // Events not saved directly
                    RetryCount = 3,                      // Retry important events
                    RequiresAcknowledgment = true,       // Must confirm receipt
                    ConflictStrategy = ConflictResolution.Reject  // Reject conflicting events
                }
            },
            {
                ReplicationTier.Persistent, new TierConfiguration
                {
                    Tier = ReplicationTier.Persistent,
                    ReplicationRateHz = 1,               // 1 Hz - changes are rare
                    ReconciliationWindowMs = 5000,       // 5s window for persistence sync
                    PersistToSave = true,                // Always save
                    RetryCount = 5,                      // Critical data - retry often
                    RequiresAcknowledgment = true,       // Must confirm
                    ConflictStrategy = ConflictResolution.ServerWins
                }
            }
        };

        public static TierConfiguration GetConfig(ReplicationTier tier)
        {
            return Configurations[tier];
        }
    }

    /// <summary>
    /// Represents a piece of replicated state with tier information
    /// </summary>
    public class ReplicatedState
    {
        public string EntityId { get; set; }
        public string PropertyPath { get; set; }
        public ReplicationTier Tier { get; set; }
        public object Value { get; set; }
        public long Timestamp { get; set; }
        public long Version { get; set; }
        public string SourceId { get; set; }  // Who made the change
        public bool IsDirty { get; set; }
        public bool RequiresSync { get; set; }
    }

    /// <summary>
    /// Manages state replication across different tiers
    /// </summary>
    public class StateReplicator
    {
        // State storage by tier
        private readonly ConcurrentDictionary<string, ReplicatedState> transientState = new();
        private readonly ConcurrentQueue<ReplicatedEvent> eventQueue = new();
        private readonly ConcurrentDictionary<string, ReplicatedState> persistentState = new();

        // Pending acknowledgments
        private readonly ConcurrentDictionary<string, PendingReplication> pendingAcks = new();

        // Version tracking
        private long globalVersion = 0;
        private readonly object versionLock = new object();

        // Dirty tracking for persistent tier
        private readonly HashSet<string> dirtyPersistentKeys = new();
        private readonly object dirtyLock = new object();

        /// <summary>
        /// Update transient state (Tier 0) - position, rotation, velocity
        /// </summary>
        public void UpdateTransient(string entityId, string property, object value, string sourceId)
        {
            string key = $"{entityId}:{property}";
            var state = new ReplicatedState
            {
                EntityId = entityId,
                PropertyPath = property,
                Tier = ReplicationTier.Transient,
                Value = value,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Version = GetNextVersion(),
                SourceId = sourceId,
                IsDirty = true,
                RequiresSync = true
            };

            transientState[key] = state;
        }

        /// <summary>
        /// Queue an event (Tier 1) - combat actions, item pickups, etc.
        /// </summary>
        public string QueueEvent(ReplicatedEvent evt)
        {
            evt.EventId = Guid.NewGuid().ToString();
            evt.Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            evt.Version = GetNextVersion();
            evt.Status = EventStatus.Pending;

            eventQueue.Enqueue(evt);

            // Track for acknowledgment
            var pending = new PendingReplication
            {
                Id = evt.EventId,
                Tier = ReplicationTier.Event,
                CreatedAt = evt.Timestamp,
                RetryCount = 0,
                MaxRetries = TierConfig.GetConfig(ReplicationTier.Event).RetryCount
            };
            pendingAcks[evt.EventId] = pending;

            return evt.EventId;
        }

        /// <summary>
        /// Update persistent state (Tier 2) - inventory, stats, relations
        /// </summary>
        public void UpdatePersistent(string entityId, string property, object value, string sourceId)
        {
            string key = $"{entityId}:{property}";

            var existing = persistentState.GetValueOrDefault(key);
            var config = TierConfig.GetConfig(ReplicationTier.Persistent);

            // Apply conflict resolution
            if (existing != null)
            {
                var resolution = ResolvePersistentConflict(existing, value, sourceId);
                if (!resolution.ShouldApply)
                    return;
                value = resolution.ResolvedValue;
            }

            var state = new ReplicatedState
            {
                EntityId = entityId,
                PropertyPath = property,
                Tier = ReplicationTier.Persistent,
                Value = value,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Version = GetNextVersion(),
                SourceId = sourceId,
                IsDirty = true,
                RequiresSync = true
            };

            persistentState[key] = state;

            // Mark as dirty for save
            lock (dirtyLock)
            {
                dirtyPersistentKeys.Add(key);
            }

            // Track for acknowledgment
            var pending = new PendingReplication
            {
                Id = key,
                Tier = ReplicationTier.Persistent,
                CreatedAt = state.Timestamp,
                RetryCount = 0,
                MaxRetries = config.RetryCount
            };
            pendingAcks[key] = pending;
        }

        /// <summary>
        /// Get transient state for an entity
        /// </summary>
        public T GetTransient<T>(string entityId, string property)
        {
            string key = $"{entityId}:{property}";
            if (transientState.TryGetValue(key, out var state) && state.Value is T typed)
                return typed;
            return default;
        }

        /// <summary>
        /// Get persistent state for an entity
        /// </summary>
        public T GetPersistent<T>(string entityId, string property)
        {
            string key = $"{entityId}:{property}";
            if (persistentState.TryGetValue(key, out var state) && state.Value is T typed)
                return typed;
            return default;
        }

        /// <summary>
        /// Process event acknowledgment
        /// </summary>
        public void AcknowledgeEvent(string eventId)
        {
            if (pendingAcks.TryRemove(eventId, out var pending))
            {
                // Event successfully acknowledged
            }
        }

        /// <summary>
        /// Get all dirty transient states that need syncing
        /// </summary>
        public IEnumerable<ReplicatedState> GetDirtyTransient()
        {
            return transientState.Values.Where(s => s.RequiresSync);
        }

        /// <summary>
        /// Get pending events to send
        /// </summary>
        public IEnumerable<ReplicatedEvent> GetPendingEvents(int maxCount = 50)
        {
            var events = new List<ReplicatedEvent>();
            while (events.Count < maxCount && eventQueue.TryDequeue(out var evt))
            {
                events.Add(evt);
            }
            return events;
        }

        /// <summary>
        /// Get dirty persistent states for saving
        /// </summary>
        public Dictionary<string, ReplicatedState> GetDirtyPersistent()
        {
            lock (dirtyLock)
            {
                var dirty = new Dictionary<string, ReplicatedState>();
                foreach (var key in dirtyPersistentKeys)
                {
                    if (persistentState.TryGetValue(key, out var state))
                    {
                        dirty[key] = state;
                    }
                }
                return dirty;
            }
        }

        /// <summary>
        /// Mark persistent state as saved
        /// </summary>
        public void MarkPersistentSaved(IEnumerable<string> keys)
        {
            lock (dirtyLock)
            {
                foreach (var key in keys)
                {
                    dirtyPersistentKeys.Remove(key);
                    if (persistentState.TryGetValue(key, out var state))
                    {
                        state.IsDirty = false;
                    }
                }
            }
        }

        /// <summary>
        /// Clear sync flags for transient states that have been sent
        /// </summary>
        public void ClearTransientSyncFlags(IEnumerable<string> keys)
        {
            foreach (var key in keys)
            {
                if (transientState.TryGetValue(key, out var state))
                {
                    state.RequiresSync = false;
                }
            }
        }

        /// <summary>
        /// Get pending replications that need retry
        /// </summary>
        public IEnumerable<PendingReplication> GetPendingRetries()
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            var retryThreshold = 1000; // 1 second retry interval

            return pendingAcks.Values.Where(p =>
                p.RetryCount < p.MaxRetries &&
                (now - p.LastAttemptAt) > retryThreshold);
        }

        /// <summary>
        /// Increment retry count for a pending replication
        /// </summary>
        public void IncrementRetry(string id)
        {
            if (pendingAcks.TryGetValue(id, out var pending))
            {
                pending.RetryCount++;
                pending.LastAttemptAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
        }

        /// <summary>
        /// Remove failed replications that exceeded retries
        /// </summary>
        public IEnumerable<PendingReplication> RemoveFailedReplications()
        {
            var failed = pendingAcks.Values.Where(p => p.RetryCount >= p.MaxRetries).ToList();
            foreach (var f in failed)
            {
                pendingAcks.TryRemove(f.Id, out _);
            }
            return failed;
        }

        /// <summary>
        /// Apply server state correction
        /// </summary>
        public void ApplyServerCorrection(string entityId, string property, object serverValue, long serverVersion)
        {
            string key = $"{entityId}:{property}";

            if (transientState.TryGetValue(key, out var state))
            {
                // Server state always wins for transient data
                state.Value = serverValue;
                state.Version = serverVersion;
                state.IsDirty = false;
                state.RequiresSync = false;
            }
        }

        private long GetNextVersion()
        {
            lock (versionLock)
            {
                return ++globalVersion;
            }
        }

        private ConflictResolutionResult ResolvePersistentConflict(ReplicatedState existing, object newValue, string sourceId)
        {
            var config = TierConfig.GetConfig(ReplicationTier.Persistent);
            var result = new ConflictResolutionResult { ShouldApply = true, ResolvedValue = newValue };

            switch (config.ConflictStrategy)
            {
                case ConflictResolution.ServerWins:
                    // If existing was from server and new is from client, reject
                    if (existing.SourceId == "SERVER" && sourceId != "SERVER")
                    {
                        result.ShouldApply = false;
                    }
                    break;

                case ConflictResolution.LastWriteWins:
                    // Always apply newer changes
                    result.ShouldApply = true;
                    break;

                case ConflictResolution.Reject:
                    // Reject if version mismatch
                    result.ShouldApply = false;
                    break;

                case ConflictResolution.Merge:
                    // Attempt merge (implement based on data type)
                    result.ResolvedValue = MergeValues(existing.Value, newValue);
                    break;
            }

            return result;
        }

        private object MergeValues(object existing, object newValue)
        {
            // Simple merge: use new value
            // Can be extended for dictionary merging, etc.
            return newValue;
        }
    }

    /// <summary>
    /// Represents an event for Tier 1 replication
    /// </summary>
    public class ReplicatedEvent
    {
        public string EventId { get; set; }
        public string EventType { get; set; }
        public string EntityId { get; set; }
        public string SourcePlayerId { get; set; }
        public string TargetEntityId { get; set; }
        public Dictionary<string, object> Data { get; set; } = new();
        public long Timestamp { get; set; }
        public long Version { get; set; }
        public EventStatus Status { get; set; }
    }

    public enum EventStatus
    {
        Pending,
        Sent,
        Acknowledged,
        Failed
    }

    /// <summary>
    /// Tracks pending replications awaiting acknowledgment
    /// </summary>
    public class PendingReplication
    {
        public string Id { get; set; }
        public ReplicationTier Tier { get; set; }
        public long CreatedAt { get; set; }
        public long LastAttemptAt { get; set; }
        public int RetryCount { get; set; }
        public int MaxRetries { get; set; }
    }

    public class ConflictResolutionResult
    {
        public bool ShouldApply { get; set; }
        public object ResolvedValue { get; set; }
    }

    /// <summary>
    /// Defines which data belongs to which replication tier
    /// </summary>
    public static class ReplicationTierMapping
    {
        private static readonly Dictionary<string, ReplicationTier> PropertyTiers = new()
        {
            // Tier 0: Transient
            { "Position", ReplicationTier.Transient },
            { "Rotation", ReplicationTier.Transient },
            { "Velocity", ReplicationTier.Transient },
            { "Animation", ReplicationTier.Transient },
            { "MovementState", ReplicationTier.Transient },

            // Tier 1: Event
            { "CombatAction", ReplicationTier.Event },
            { "DamageEvent", ReplicationTier.Event },
            { "ItemPickup", ReplicationTier.Event },
            { "ItemDrop", ReplicationTier.Event },
            { "AbilityUse", ReplicationTier.Event },
            { "Interaction", ReplicationTier.Event },
            { "StatusEffect", ReplicationTier.Event },

            // Tier 2: Persistent
            { "Inventory", ReplicationTier.Persistent },
            { "Equipment", ReplicationTier.Persistent },
            { "Health", ReplicationTier.Persistent },
            { "Stats", ReplicationTier.Persistent },
            { "Skills", ReplicationTier.Persistent },
            { "FactionRelations", ReplicationTier.Persistent },
            { "QuestProgress", ReplicationTier.Persistent },
            { "Experience", ReplicationTier.Persistent },
            { "Level", ReplicationTier.Persistent },
            { "Buildings", ReplicationTier.Persistent },
            { "Money", ReplicationTier.Persistent }
        };

        public static ReplicationTier GetTier(string property)
        {
            if (PropertyTiers.TryGetValue(property, out var tier))
                return tier;

            // Default to Transient for unknown properties
            return ReplicationTier.Transient;
        }

        public static bool IsPersistent(string property)
        {
            return GetTier(property) == ReplicationTier.Persistent;
        }

        public static bool IsEvent(string property)
        {
            return GetTier(property) == ReplicationTier.Event;
        }
    }
}

using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using KenshiMultiplayer.Core;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Conflict resolution strategy.
    /// </summary>
    public enum ConflictStrategy
    {
        /// <summary>First request to reach server wins</summary>
        FirstWins,

        /// <summary>Server timestamp determines winner</summary>
        ServerTimestamp,

        /// <summary>Lower player ID wins (deterministic tiebreaker)</summary>
        LowerIdWins,

        /// <summary>Server rejects both requests</summary>
        RejectBoth
    }

    /// <summary>
    /// Types of conflicts that can occur.
    /// </summary>
    public enum ConflictType
    {
        /// <summary>Two players grabbing same item</summary>
        ItemPickup,

        /// <summary>Two players recruiting same NPC</summary>
        NPCRecruit,

        /// <summary>Two players trading with same shop</summary>
        ShopInteraction,

        /// <summary>Two players attacking same target</summary>
        CombatTarget,

        /// <summary>Two players placing building in same spot</summary>
        BuildingPlacement,

        /// <summary>Two players modifying same entity</summary>
        EntityModification
    }

    /// <summary>
    /// A pending action that may conflict with others.
    /// </summary>
    public class PendingAction
    {
        public string ActionId { get; set; }
        public string PlayerId { get; set; }
        public string TargetEntityId { get; set; }
        public ConflictType Type { get; set; }
        public long ReceivedAt { get; set; }
        public ulong Tick { get; set; }
        public int Sequence { get; set; }
        public Dictionary<string, object> Data { get; set; } = new();

        public static PendingAction Create(string playerId, string targetId, ConflictType type, ulong tick)
        {
            return new PendingAction
            {
                ActionId = Guid.NewGuid().ToString(),
                PlayerId = playerId,
                TargetEntityId = targetId,
                Type = type,
                ReceivedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Tick = tick
            };
        }
    }

    /// <summary>
    /// Result of conflict resolution.
    /// </summary>
    public class ConflictResult
    {
        public bool HasConflict { get; set; }
        public string WinnerId { get; set; }
        public List<string> LoserIds { get; set; } = new();
        public string Reason { get; set; }
        public ConflictStrategy StrategyUsed { get; set; }
    }

    /// <summary>
    /// ConflictResolver handles simultaneous actions on the same entity.
    ///
    /// Core principles:
    /// 1. Server is ALWAYS authoritative
    /// 2. First-to-server wins (latency advantage)
    /// 3. Deterministic tiebreaker for same-tick conflicts
    /// 4. No negotiation, no merge, no rollback
    /// </summary>
    public class ConflictResolver
    {
        // Pending actions indexed by target entity
        private readonly ConcurrentDictionary<string, List<PendingAction>> _pendingByTarget = new();

        // Resolved conflicts for logging/debugging
        private readonly ConcurrentQueue<ConflictResult> _recentConflicts = new();

        // Locks for entity access (prevents race conditions)
        private readonly ConcurrentDictionary<string, EntityLock> _entityLocks = new();

        // Strategy per conflict type
        private readonly Dictionary<ConflictType, ConflictStrategy> _strategies = new()
        {
            { ConflictType.ItemPickup, ConflictStrategy.FirstWins },
            { ConflictType.NPCRecruit, ConflictStrategy.FirstWins },
            { ConflictType.ShopInteraction, ConflictStrategy.FirstWins },
            { ConflictType.CombatTarget, ConflictStrategy.ServerTimestamp }, // Multiple allowed
            { ConflictType.BuildingPlacement, ConflictStrategy.FirstWins },
            { ConflictType.EntityModification, ConflictStrategy.FirstWins }
        };

        /// <summary>
        /// Maximum pending actions per entity before auto-resolve.
        /// </summary>
        private const int MAX_PENDING_ACTIONS = 10;

        /// <summary>
        /// How long to wait for conflicting actions (ms).
        /// </summary>
        private const int CONFLICT_WINDOW_MS = 100;

        /// <summary>
        /// Submit an action that may conflict.
        /// Returns immediately with whether the action is accepted.
        /// </summary>
        public ActionSubmitResult SubmitAction(PendingAction action)
        {
            // Try to acquire lock on target entity
            var lockResult = TryAcquireLock(action.TargetEntityId, action.PlayerId, action.Type);

            if (!lockResult.Acquired)
            {
                return new ActionSubmitResult
                {
                    Accepted = false,
                    Reason = lockResult.Reason,
                    ConflictingPlayerId = lockResult.HolderId
                };
            }

            // Add to pending actions
            var pending = _pendingByTarget.GetOrAdd(action.TargetEntityId, _ => new List<PendingAction>());

            lock (pending)
            {
                pending.Add(action);
                action.Sequence = pending.Count;

                // If too many pending, resolve immediately
                if (pending.Count >= MAX_PENDING_ACTIONS)
                {
                    var result = ResolveConflicts(action.TargetEntityId);
                    return new ActionSubmitResult
                    {
                        Accepted = result.WinnerId == action.PlayerId,
                        Reason = result.WinnerId == action.PlayerId ? null : "Lost to another player",
                        ConflictingPlayerId = result.WinnerId == action.PlayerId ? null : result.WinnerId
                    };
                }
            }

            return new ActionSubmitResult
            {
                Accepted = true,
                ActionId = action.ActionId
            };
        }

        /// <summary>
        /// Resolve all pending actions for an entity.
        /// Call this at the end of a tick or when needed.
        /// </summary>
        public ConflictResult ResolveConflicts(string targetEntityId)
        {
            if (!_pendingByTarget.TryRemove(targetEntityId, out var pending) || pending.Count == 0)
            {
                return new ConflictResult { HasConflict = false };
            }

            List<PendingAction> actions;
            lock (pending)
            {
                actions = new List<PendingAction>(pending);
                pending.Clear();
            }

            // Release lock
            ReleaseLock(targetEntityId);

            if (actions.Count == 1)
            {
                return new ConflictResult
                {
                    HasConflict = false,
                    WinnerId = actions[0].PlayerId
                };
            }

            // Multiple actions = conflict
            var conflictType = actions[0].Type;
            var strategy = _strategies.GetValueOrDefault(conflictType, ConflictStrategy.FirstWins);

            var result = ApplyStrategy(actions, strategy);

            // Log conflict
            _recentConflicts.Enqueue(result);
            while (_recentConflicts.Count > 100)
            {
                _recentConflicts.TryDequeue(out _);
            }

            return result;
        }

        /// <summary>
        /// Apply conflict resolution strategy.
        /// </summary>
        private ConflictResult ApplyStrategy(List<PendingAction> actions, ConflictStrategy strategy)
        {
            var result = new ConflictResult
            {
                HasConflict = true,
                StrategyUsed = strategy
            };

            PendingAction winner;

            switch (strategy)
            {
                case ConflictStrategy.FirstWins:
                    // Sort by received timestamp, then sequence
                    actions.Sort((a, b) =>
                    {
                        var timeCompare = a.ReceivedAt.CompareTo(b.ReceivedAt);
                        return timeCompare != 0 ? timeCompare : a.Sequence.CompareTo(b.Sequence);
                    });
                    winner = actions[0];
                    result.Reason = "First request wins";
                    break;

                case ConflictStrategy.ServerTimestamp:
                    // Sort by server tick
                    actions.Sort((a, b) => a.Tick.CompareTo(b.Tick));
                    winner = actions[0];
                    result.Reason = "Server timestamp wins";
                    break;

                case ConflictStrategy.LowerIdWins:
                    // Deterministic tiebreaker using player ID
                    actions.Sort((a, b) => string.Compare(a.PlayerId, b.PlayerId, StringComparison.Ordinal));
                    winner = actions[0];
                    result.Reason = "Deterministic tiebreaker (lower ID)";
                    break;

                case ConflictStrategy.RejectBoth:
                    result.WinnerId = null;
                    foreach (var action in actions)
                    {
                        result.LoserIds.Add(action.PlayerId);
                    }
                    result.Reason = "Conflicting actions rejected";
                    return result;

                default:
                    // Default to first wins
                    winner = actions[0];
                    result.Reason = "Default resolution";
                    break;
            }

            result.WinnerId = winner.PlayerId;
            foreach (var action in actions)
            {
                if (action.PlayerId != winner.PlayerId)
                {
                    result.LoserIds.Add(action.PlayerId);
                }
            }

            return result;
        }

        /// <summary>
        /// Try to acquire exclusive lock on an entity.
        /// </summary>
        private LockResult TryAcquireLock(string entityId, string playerId, ConflictType type)
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            var existingLock = _entityLocks.GetOrAdd(entityId, _ => new EntityLock
            {
                EntityId = entityId,
                HolderId = playerId,
                AcquiredAt = now,
                Type = type
            });

            lock (existingLock)
            {
                // Check if lock is stale (older than conflict window)
                if (now - existingLock.AcquiredAt > CONFLICT_WINDOW_MS)
                {
                    // Stale lock, take over
                    existingLock.HolderId = playerId;
                    existingLock.AcquiredAt = now;
                    existingLock.Type = type;
                    return new LockResult { Acquired = true };
                }

                // Lock is fresh
                if (existingLock.HolderId == playerId)
                {
                    // Same player, extend lock
                    existingLock.AcquiredAt = now;
                    return new LockResult { Acquired = true };
                }

                // Different player, conflict
                return new LockResult
                {
                    Acquired = false,
                    HolderId = existingLock.HolderId,
                    Reason = $"Entity locked by another player"
                };
            }
        }

        /// <summary>
        /// Release lock on an entity.
        /// </summary>
        private void ReleaseLock(string entityId)
        {
            _entityLocks.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Clean up stale locks (call periodically).
        /// </summary>
        public void CleanupStaleLocks()
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            var staleThreshold = CONFLICT_WINDOW_MS * 10; // 1 second

            var staleKeys = new List<string>();

            foreach (var kvp in _entityLocks)
            {
                if (now - kvp.Value.AcquiredAt > staleThreshold)
                {
                    staleKeys.Add(kvp.Key);
                }
            }

            foreach (var key in staleKeys)
            {
                _entityLocks.TryRemove(key, out _);
            }
        }

        /// <summary>
        /// Get recent conflicts for debugging.
        /// </summary>
        public IEnumerable<ConflictResult> GetRecentConflicts()
        {
            return _recentConflicts.ToArray();
        }

        /// <summary>
        /// Check if an entity is currently locked.
        /// </summary>
        public bool IsEntityLocked(string entityId, out string holderId)
        {
            if (_entityLocks.TryGetValue(entityId, out var entityLock))
            {
                holderId = entityLock.HolderId;
                return true;
            }
            holderId = null;
            return false;
        }
    }

    /// <summary>
    /// Entity lock for preventing race conditions.
    /// </summary>
    public class EntityLock
    {
        public string EntityId { get; set; }
        public string HolderId { get; set; }
        public long AcquiredAt { get; set; }
        public ConflictType Type { get; set; }
    }

    /// <summary>
    /// Result of trying to acquire a lock.
    /// </summary>
    public class LockResult
    {
        public bool Acquired { get; set; }
        public string HolderId { get; set; }
        public string Reason { get; set; }
    }

    /// <summary>
    /// Result of submitting an action.
    /// </summary>
    public class ActionSubmitResult
    {
        public bool Accepted { get; set; }
        public string ActionId { get; set; }
        public string Reason { get; set; }
        public string ConflictingPlayerId { get; set; }
    }
}

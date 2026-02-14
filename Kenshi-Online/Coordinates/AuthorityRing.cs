using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Ring 3: Authority Ring (Time + Identity + Meaning + Authority Epoch)
    ///
    /// Entry includes:
    ///   - commit_id (monotonic)
    ///   - tick
    ///   - subject NetId
    ///   - op (set/patch/spawn/despawn)
    ///   - normalized payload
    ///   - authority_epoch
    ///   - result + reason (if rejected)
    ///
    /// This ring is "truth log" - the Write-Ahead Log of reality.
    ///
    /// Invariant: every commit has monotonically increasing commit_id.
    /// This is what prevents desync becoming "normal."
    ///
    /// The authority ring should aggressively reduce:
    ///   - collapse multiple position updates into the latest per tick
    ///   - coalesce repeated inputs
    ///   - dedupe identical events
    ///   - reject out-of-order or stale proposals
    ///   - enforce constraints ("cannot move 50m in one tick unless teleported")
    /// </summary>
    public class AuthorityRing
    {
        private readonly Commit[] _buffer;
        private readonly int _capacity;
        private long _commitId;
        private long _head;
        private readonly object _commitLock = new();

        // Per-entity state tracking
        private readonly ConcurrentDictionary<NetId, EntityTruthState> _entityStates = new();

        // Snapshot management
        private readonly List<Snapshot> _snapshots = new();
        private readonly int _snapshotInterval;
        private long _lastSnapshotCommitId;

        // Validation constraints
        private readonly List<ICommitConstraint> _constraints = new();

        // Statistics
        private long _totalCommits;
        private long _totalRejected;
        private long _totalCoalesced;

        public AuthorityRing(int capacity = 32768, int snapshotInterval = 1000)
        {
            _capacity = capacity;
            _snapshotInterval = snapshotInterval;
            _buffer = new Commit[capacity];

            // Add default constraints
            _constraints.Add(new TeleportConstraint(50f)); // Max 50 units per tick
            _constraints.Add(new HealthRangeConstraint());
        }

        /// <summary>
        /// Commit a change to the truth log.
        /// Returns the commit if successful, null if rejected.
        /// </summary>
        public Commit? Commit(
            NetId subjectId,
            CommitOp operation,
            SchemaPayload payload,
            long tick,
            uint authorityEpoch,
            NetId sourceId,
            string? reason = null)
        {
            lock (_commitLock)
            {
                // Normalize payload
                if (payload is TransformPayload tp)
                    payload = PayloadNormalizer.NormalizeTransform(tp);

                // Get or create entity state
                var entityState = _entityStates.GetOrAdd(subjectId, _ => new EntityTruthState(subjectId));

                // Check if this would be a duplicate/coalesce
                if (ShouldCoalesce(entityState, operation, payload, tick))
                {
                    Interlocked.Increment(ref _totalCoalesced);
                    return null; // Coalesced into existing commit
                }

                // Apply constraints
                foreach (var constraint in _constraints)
                {
                    var (valid, rejectReason) = constraint.Validate(entityState, operation, payload, tick);
                    if (!valid)
                    {
                        Interlocked.Increment(ref _totalRejected);
                        return CreateRejection(subjectId, operation, payload, tick, authorityEpoch, sourceId, rejectReason);
                    }
                }

                // Create the commit
                var commit = new Commit
                {
                    CommitId = Interlocked.Increment(ref _commitId),
                    SubjectId = subjectId,
                    Operation = operation,
                    Payload = payload,
                    Tick = tick,
                    AuthorityEpoch = authorityEpoch,
                    SourceId = sourceId,
                    Result = CommitResult.Accepted,
                    Reason = reason ?? "",
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    PayloadHash = payload.ComputeHash()
                };

                // Apply to entity state
                entityState.ApplyCommit(commit);

                // Store in ring buffer
                int index = (int)(_head % _capacity);
                _buffer[index] = commit;
                _head++;
                Interlocked.Increment(ref _totalCommits);

                // Check if we need a snapshot
                if (_commitId - _lastSnapshotCommitId >= _snapshotInterval)
                {
                    CreateSnapshot();
                }

                return commit;
            }
        }

        private bool ShouldCoalesce(EntityTruthState entityState, CommitOp operation, SchemaPayload payload, long tick)
        {
            // Only coalesce position updates in the same tick
            if (operation != CommitOp.Set) return false;
            if (payload is not TransformPayload) return false;

            var lastCommit = entityState.LastCommit;
            if (lastCommit == null) return false;
            if (lastCommit.Value.Tick != tick) return false;
            if (lastCommit.Value.Operation != CommitOp.Set) return false;
            if (lastCommit.Value.Payload is not TransformPayload) return false;

            // Same tick, same type - coalesce by updating in place
            entityState.UpdateLastCommit(payload);
            return true;
        }

        private Commit CreateRejection(
            NetId subjectId,
            CommitOp operation,
            SchemaPayload payload,
            long tick,
            uint authorityEpoch,
            NetId sourceId,
            string reason)
        {
            return new Commit
            {
                CommitId = -1, // Rejections don't get commit IDs
                SubjectId = subjectId,
                Operation = operation,
                Payload = payload,
                Tick = tick,
                AuthorityEpoch = authorityEpoch,
                SourceId = sourceId,
                Result = CommitResult.Rejected,
                Reason = reason,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                PayloadHash = payload.ComputeHash()
            };
        }

        /// <summary>
        /// Get commits since a specific commit ID.
        /// </summary>
        public IEnumerable<Commit> GetCommitsSince(long fromCommitId)
        {
            lock (_commitLock)
            {
                long earliest = Math.Max(0, _head - _capacity);
                long start = Math.Max(earliest, fromCommitId);

                for (long i = start; i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    var commit = _buffer[index];
                    if (commit.CommitId > fromCommitId)
                        yield return commit;
                }
            }
        }

        /// <summary>
        /// Get commits for a specific entity.
        /// </summary>
        public IEnumerable<Commit> GetCommitsForEntity(NetId entityId, long? fromTick = null)
        {
            lock (_commitLock)
            {
                for (long i = Math.Max(0, _head - _capacity); i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    var commit = _buffer[index];
                    if (commit.SubjectId != entityId) continue;
                    if (fromTick.HasValue && commit.Tick < fromTick.Value) continue;
                    yield return commit;
                }
            }
        }

        /// <summary>
        /// Get commits in a tick range.
        /// </summary>
        public IEnumerable<Commit> GetCommitsInTickRange(long fromTick, long toTick)
        {
            lock (_commitLock)
            {
                for (long i = Math.Max(0, _head - _capacity); i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    var commit = _buffer[index];
                    if (commit.Tick >= fromTick && commit.Tick <= toTick)
                        yield return commit;
                }
            }
        }

        /// <summary>
        /// Get the current state of an entity.
        /// </summary>
        public EntityTruthState? GetEntityState(NetId entityId)
        {
            return _entityStates.TryGetValue(entityId, out var state) ? state : null;
        }

        /// <summary>
        /// Get all entity states.
        /// </summary>
        public IEnumerable<EntityTruthState> GetAllEntityStates()
        {
            return _entityStates.Values;
        }

        /// <summary>
        /// Remove entity state (on despawn).
        /// </summary>
        public void RemoveEntityState(NetId entityId)
        {
            _entityStates.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Current commit ID (head of the log).
        /// </summary>
        public long CurrentCommitId => Interlocked.Read(ref _commitId);

        /// <summary>
        /// Get statistics.
        /// </summary>
        public (long commits, long rejected, long coalesced) GetStats()
        {
            return (_totalCommits, _totalRejected, _totalCoalesced);
        }

        #region Snapshots

        private void CreateSnapshot()
        {
            var snapshot = new Snapshot
            {
                CommitId = _commitId,
                Tick = GetLatestTick(),
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                EntityStates = _entityStates.ToDictionary(
                    kvp => kvp.Key,
                    kvp => kvp.Value.Clone())
            };

            lock (_snapshots)
            {
                _snapshots.Add(snapshot);
                _lastSnapshotCommitId = _commitId;

                // Keep only last N snapshots
                while (_snapshots.Count > 10)
                    _snapshots.RemoveAt(0);
            }
        }

        /// <summary>
        /// Get the latest snapshot.
        /// </summary>
        public Snapshot? GetLatestSnapshot()
        {
            lock (_snapshots)
            {
                return _snapshots.Count > 0 ? _snapshots[^1] : null;
            }
        }

        /// <summary>
        /// Get a snapshot at or before a specific commit.
        /// </summary>
        public Snapshot? GetSnapshotBefore(long commitId)
        {
            lock (_snapshots)
            {
                for (int i = _snapshots.Count - 1; i >= 0; i--)
                {
                    if (_snapshots[i].CommitId <= commitId)
                        return _snapshots[i];
                }
                return null;
            }
        }

        /// <summary>
        /// Reconstruct state at a specific commit by replaying from snapshot.
        /// </summary>
        public Dictionary<NetId, EntityTruthState>? ReconstructStateAt(long commitId)
        {
            var snapshot = GetSnapshotBefore(commitId);
            if (snapshot == null) return null;

            var states = snapshot.EntityStates.ToDictionary(
                kvp => kvp.Key,
                kvp => kvp.Value.Clone());

            // Replay commits from snapshot to target
            foreach (var commit in GetCommitsSince(snapshot.CommitId))
            {
                if (commit.CommitId > commitId) break;
                if (commit.Result != CommitResult.Accepted) continue;

                if (!states.TryGetValue(commit.SubjectId, out var state))
                {
                    state = new EntityTruthState(commit.SubjectId);
                    states[commit.SubjectId] = state;
                }
                state.ApplyCommit(commit);
            }

            return states;
        }

        #endregion

        #region Constraints

        /// <summary>
        /// Add a validation constraint.
        /// </summary>
        public void AddConstraint(ICommitConstraint constraint)
        {
            _constraints.Add(constraint);
        }

        #endregion

        private long GetLatestTick()
        {
            lock (_commitLock)
            {
                if (_head == 0) return 0;
                int index = (int)((_head - 1) % _capacity);
                return _buffer[index].Tick;
            }
        }
    }

    /// <summary>
    /// Operations that can be committed.
    /// </summary>
    public enum CommitOp : byte
    {
        /// <summary>Set/replace a value.</summary>
        Set = 0,

        /// <summary>Patch/delta update.</summary>
        Patch = 1,

        /// <summary>Entity spawn.</summary>
        Spawn = 2,

        /// <summary>Entity despawn.</summary>
        Despawn = 3,

        /// <summary>Authority transfer.</summary>
        AuthorityChange = 4,

        /// <summary>Event occurred.</summary>
        Event = 5
    }

    /// <summary>
    /// Result of a commit attempt.
    /// </summary>
    public enum CommitResult : byte
    {
        Accepted,
        Rejected,
        Deferred,
        Coalesced
    }

    /// <summary>
    /// A commit in the authority ring (truth log entry).
    /// </summary>
    public struct Commit
    {
        /// <summary>Monotonically increasing commit ID.</summary>
        public long CommitId;

        /// <summary>The entity this commit affects.</summary>
        public NetId SubjectId;

        /// <summary>The operation being performed.</summary>
        public CommitOp Operation;

        /// <summary>The normalized payload.</summary>
        public SchemaPayload Payload;

        /// <summary>The tick when this truth applies.</summary>
        public long Tick;

        /// <summary>The authority epoch that authorized this.</summary>
        public uint AuthorityEpoch;

        /// <summary>Who submitted this commit.</summary>
        public NetId SourceId;

        /// <summary>Was this accepted or rejected?</summary>
        public CommitResult Result;

        /// <summary>Reason for rejection or additional context.</summary>
        public string Reason;

        /// <summary>Wall-clock timestamp.</summary>
        public long Timestamp;

        /// <summary>Hash of the payload for verification.</summary>
        public int PayloadHash;

        /// <summary>Answer: When is this true? (tick / commit_id)</summary>
        public string WhenIsThisTrue => $"Tick {Tick}, Commit {CommitId}";

        /// <summary>Answer: Who decided it? (authority_owner/epoch)</summary>
        public string WhoDecidedIt => $"Source {SourceId.ToShortString()} @ Epoch {AuthorityEpoch}";

        /// <summary>Answer: What does it mean? (schema + op)</summary>
        public string WhatDoesItMean => $"{Operation} {Payload.SchemaId}";

        public override string ToString()
        {
            return $"Commit({CommitId}: {Operation} {SubjectId.ToShortString()} {Payload.SchemaId.Kind}@T{Tick} {Result})";
        }
    }

    /// <summary>
    /// The current truth state of an entity as derived from commits.
    /// </summary>
    public class EntityTruthState
    {
        public NetId EntityId { get; }
        public long LastTick { get; private set; }
        public long LastCommitId { get; private set; }
        public Commit? LastCommit { get; private set; }

        // Current authoritative values
        public TransformPayload? Transform { get; private set; }
        public HealthPayload? Health { get; private set; }
        public InventoryPayload? Inventory { get; private set; }
        public AIStatePayload? AIState { get; private set; }

        // History for rollback/verification
        private readonly List<Commit> _recentCommits = new();
        private const int MaxRecentCommits = 100;

        public EntityTruthState(NetId entityId)
        {
            EntityId = entityId;
        }

        public void ApplyCommit(Commit commit)
        {
            LastTick = commit.Tick;
            LastCommitId = commit.CommitId;
            LastCommit = commit;

            // Update state based on payload type
            switch (commit.Payload)
            {
                case TransformPayload tp:
                    Transform = tp;
                    break;
                case HealthPayload hp:
                    Health = hp;
                    break;
                case InventoryPayload ip:
                    Inventory = ip;
                    break;
                case AIStatePayload ai:
                    AIState = ai;
                    break;
            }

            // Track recent commits
            _recentCommits.Add(commit);
            while (_recentCommits.Count > MaxRecentCommits)
                _recentCommits.RemoveAt(0);
        }

        public void UpdateLastCommit(SchemaPayload newPayload)
        {
            // For coalescing - update the payload of the last commit
            switch (newPayload)
            {
                case TransformPayload tp:
                    Transform = tp;
                    break;
            }
        }

        public EntityTruthState Clone()
        {
            var clone = new EntityTruthState(EntityId)
            {
                LastTick = LastTick,
                LastCommitId = LastCommitId,
                LastCommit = LastCommit,
                Transform = Transform,
                Health = Health,
                Inventory = Inventory,
                AIState = AIState
            };
            return clone;
        }

        public IReadOnlyList<Commit> GetRecentCommits() => _recentCommits;
    }

    /// <summary>
    /// A snapshot of the authority ring state.
    /// </summary>
    public class Snapshot
    {
        public long CommitId { get; set; }
        public long Tick { get; set; }
        public long Timestamp { get; set; }
        public Dictionary<NetId, EntityTruthState> EntityStates { get; set; } = new();
    }

    /// <summary>
    /// Interface for commit validation constraints.
    /// </summary>
    public interface ICommitConstraint
    {
        (bool valid, string reason) Validate(EntityTruthState entityState, CommitOp operation, SchemaPayload payload, long tick);
    }

    /// <summary>
    /// Constraint: entities cannot teleport more than X units per tick.
    /// </summary>
    public class TeleportConstraint : ICommitConstraint
    {
        private readonly float _maxDistancePerTick;

        public TeleportConstraint(float maxDistancePerTick)
        {
            _maxDistancePerTick = maxDistancePerTick;
        }

        public (bool valid, string reason) Validate(EntityTruthState entityState, CommitOp operation, SchemaPayload payload, long tick)
        {
            if (operation != CommitOp.Set) return (true, "");
            if (payload is not TransformPayload newTransform) return (true, "");
            if (entityState.Transform == null) return (true, ""); // First position, allow

            float distance = System.Numerics.Vector3.Distance(
                entityState.Transform.Position,
                newTransform.Position);

            long tickDelta = Math.Max(1, tick - entityState.LastTick);
            float maxAllowed = _maxDistancePerTick * tickDelta;

            if (distance > maxAllowed)
            {
                return (false, $"Teleport detected: {distance:F2} units in {tickDelta} ticks (max {maxAllowed:F2})");
            }

            return (true, "");
        }
    }

    /// <summary>
    /// Constraint: health must be within valid range.
    /// </summary>
    public class HealthRangeConstraint : ICommitConstraint
    {
        public (bool valid, string reason) Validate(EntityTruthState entityState, CommitOp operation, SchemaPayload payload, long tick)
        {
            if (payload is not HealthPayload hp) return (true, "");

            if (hp.Current < 0)
                return (false, $"Health cannot be negative: {hp.Current}");

            if (hp.Current > hp.Maximum * 1.1f) // Allow 10% overheal
                return (false, $"Health exceeds maximum: {hp.Current} > {hp.Maximum}");

            return (true, "");
        }
    }
}

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Attribute Ring - The Presentation/Actuator Gateway
    ///
    /// This ring exists in a domain that is GATED to the actual game.
    /// It extends dimensions of the other rings by:
    ///
    ///   1. Processing undecided data (pending authority)
    ///   2. Gating correct time, position, orientation to game memory
    ///   3. Interpolation between known authoritative states
    ///   4. Extrapolation when authority is late/missing
    ///
    /// The key insight: Ring3 (Authority) contains discrete tick-based truth,
    /// but the game renders at arbitrary frame times. The Attribute Ring
    /// converts between these two temporal domains.
    ///
    /// Truth (Ring3) → Attribute Ring → Game Memory
    ///                     ↓
    ///            [interpolate/extrapolate]
    ///                     ↓
    ///              Presentation State
    /// </summary>
    public class AttributeRing
    {
        // Per-entity attribute state
        private readonly ConcurrentDictionary<NetId, EntityAttributeState> _entityStates = new();

        // Reference to authority ring (source of truth)
        private readonly AuthorityRing _authorityRing;

        // Reference to clock for timing
        private readonly TickClock _clock;

        // Gating configuration
        private readonly GateConfig _gateConfig;

        // Pending writes that haven't been gated yet
        private readonly ConcurrentQueue<PendingWrite> _pendingWrites = new();

        // Statistics
        private long _interpolations;
        private long _extrapolations;
        private long _gatedWrites;
        private long _blockedWrites;

        public AttributeRing(AuthorityRing authorityRing, TickClock clock, GateConfig? config = null)
        {
            _authorityRing = authorityRing;
            _clock = clock;
            _gateConfig = config ?? new GateConfig();
        }

        /// <summary>
        /// Get the presentation state for an entity at a specific render time.
        /// This is what the game should display - interpolated/extrapolated from authority.
        /// </summary>
        public PresentationState? GetPresentationState(NetId entityId, double renderTime)
        {
            var entityState = GetOrCreateEntityState(entityId);
            var authorityState = _authorityRing.GetEntityState(entityId);

            if (authorityState == null)
                return null;

            // Get the tick boundaries for this render time
            long baseTick = (long)renderTime;
            float fraction = (float)(renderTime - baseTick);

            // Get transform attribute
            var transform = entityState.GetAttribute<TransformAttribute>(AttributeKind.Transform);
            if (transform == null && authorityState.Transform != null)
            {
                // Initialize from authority
                transform = new TransformAttribute();
                transform.PushSample(authorityState.LastTick, authorityState.Transform.ToFramedTransform());
                entityState.SetAttribute(AttributeKind.Transform, transform);
            }

            if (transform == null)
                return null;

            // Determine if we interpolate or extrapolate
            var (position, rotation, velocity, mode) = transform.SampleAt(renderTime, _gateConfig);

            if (mode == SampleMode.Extrapolate)
                System.Threading.Interlocked.Increment(ref _extrapolations);
            else
                System.Threading.Interlocked.Increment(ref _interpolations);

            return new PresentationState
            {
                EntityId = entityId,
                RenderTime = renderTime,
                Position = position,
                Rotation = rotation,
                Velocity = velocity,
                Mode = mode,
                Confidence = mode == SampleMode.Interpolate ? 1.0f : CalculateExtrapolationConfidence(transform, renderTime),
                SourceTick = authorityState.LastTick,
                Stale = renderTime - authorityState.LastTick > _gateConfig.MaxStaleTicks
            };
        }

        /// <summary>
        /// Push authoritative state from Ring3 into the attribute ring.
        /// Called when new commits arrive.
        /// </summary>
        public void PushAuthority(NetId entityId, long tick, SchemaPayload payload)
        {
            var entityState = GetOrCreateEntityState(entityId);

            if (payload is TransformPayload tp)
            {
                var transform = entityState.GetOrCreateAttribute<TransformAttribute>(AttributeKind.Transform);
                transform.PushSample(tick, tp.ToFramedTransform());
            }
            else if (payload is HealthPayload hp)
            {
                var health = entityState.GetOrCreateAttribute<ScalarAttribute>(AttributeKind.Health);
                health.PushSample(tick, hp.Current);
            }
        }

        /// <summary>
        /// Gate a write to game memory.
        /// Returns true if the write should proceed, false if blocked.
        /// </summary>
        public GateDecision GateWrite(NetId entityId, AttributeKind kind, object value, double renderTime)
        {
            var entityState = GetOrCreateEntityState(entityId);
            var authorityState = _authorityRing.GetEntityState(entityId);

            // Check: do we have authority for this entity?
            if (authorityState == null)
            {
                System.Threading.Interlocked.Increment(ref _blockedWrites);
                return GateDecision.Block("No authority state");
            }

            // Check: is this write stale?
            long currentTick = _clock.CurrentTick;
            double staleness = currentTick - authorityState.LastTick;

            if (staleness > _gateConfig.MaxStaleTicks)
            {
                // Authority is too old - extrapolate but flag as uncertain
                return GateDecision.AllowWithWarning($"Stale authority: {staleness} ticks old");
            }

            // Check: does this write conflict with recent authority?
            if (kind == AttributeKind.Transform && value is FramedTransform ft)
            {
                var transform = entityState.GetAttribute<TransformAttribute>(AttributeKind.Transform);
                if (transform != null)
                {
                    var (authPos, _, _, _) = transform.SampleAt(renderTime, _gateConfig);
                    float distance = Vector3.Distance(ft.Position, authPos);

                    if (distance > _gateConfig.MaxPositionDivergence)
                    {
                        // Too far from authority - need correction
                        System.Threading.Interlocked.Increment(ref _blockedWrites);
                        return GateDecision.Correct(authPos, $"Position diverged {distance:F2} units");
                    }
                }
            }

            System.Threading.Interlocked.Increment(ref _gatedWrites);
            return GateDecision.Allow();
        }

        /// <summary>
        /// Process pending/undecided data and update presentation state.
        /// Called each frame to keep presentation current.
        /// </summary>
        public void ProcessFrame(double renderTime)
        {
            // Process any pending writes
            while (_pendingWrites.TryDequeue(out var pending))
            {
                var decision = GateWrite(pending.EntityId, pending.Kind, pending.Value, renderTime);
                pending.Callback?.Invoke(decision);
            }

            // Update all entity states with latest authority
            foreach (var kvp in _entityStates)
            {
                var entityId = kvp.Key;
                var entityState = kvp.Value;

                var authorityState = _authorityRing.GetEntityState(entityId);
                if (authorityState == null) continue;

                // Sync any new authority data
                SyncFromAuthority(entityState, authorityState);
            }
        }

        /// <summary>
        /// Queue a write for gating (async).
        /// </summary>
        public void QueueWrite(NetId entityId, AttributeKind kind, object value, Action<GateDecision>? callback = null)
        {
            _pendingWrites.Enqueue(new PendingWrite
            {
                EntityId = entityId,
                Kind = kind,
                Value = value,
                Callback = callback
            });
        }

        /// <summary>
        /// Remove entity state.
        /// </summary>
        public void RemoveEntity(NetId entityId)
        {
            _entityStates.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Get interpolated position for smooth rendering.
        /// </summary>
        public Vector3? GetInterpolatedPosition(NetId entityId, double renderTime)
        {
            var state = GetPresentationState(entityId, renderTime);
            return state?.Position;
        }

        /// <summary>
        /// Get interpolated rotation for smooth rendering.
        /// </summary>
        public Quaternion? GetInterpolatedRotation(NetId entityId, double renderTime)
        {
            var state = GetPresentationState(entityId, renderTime);
            return state?.Rotation;
        }

        #region Private Methods

        private EntityAttributeState GetOrCreateEntityState(NetId entityId)
        {
            return _entityStates.GetOrAdd(entityId, id => new EntityAttributeState(id));
        }

        private void SyncFromAuthority(EntityAttributeState entityState, EntityTruthState authorityState)
        {
            // Check if we have newer authority data
            if (authorityState.Transform != null)
            {
                var transform = entityState.GetOrCreateAttribute<TransformAttribute>(AttributeKind.Transform);
                if (authorityState.LastTick > transform.LatestTick)
                {
                    transform.PushSample(authorityState.LastTick, authorityState.Transform.ToFramedTransform());
                }
            }
        }

        private float CalculateExtrapolationConfidence(TransformAttribute transform, double renderTime)
        {
            double ticksBeyond = renderTime - transform.LatestTick;
            // Confidence decays exponentially with extrapolation distance
            return MathF.Exp(-0.2f * (float)ticksBeyond);
        }

        #endregion

        #region Statistics

        public AttributeRingStats GetStats()
        {
            return new AttributeRingStats
            {
                EntityCount = _entityStates.Count,
                Interpolations = _interpolations,
                Extrapolations = _extrapolations,
                GatedWrites = _gatedWrites,
                BlockedWrites = _blockedWrites,
                PendingWrites = _pendingWrites.Count
            };
        }

        #endregion
    }

    /// <summary>
    /// Kinds of attributes tracked.
    /// </summary>
    public enum AttributeKind : byte
    {
        Transform = 0,
        Health = 1,
        Velocity = 2,
        Animation = 3,
        AIState = 4,
        Combat = 5,
        Inventory = 6
    }

    /// <summary>
    /// How a sample was obtained.
    /// </summary>
    public enum SampleMode : byte
    {
        /// <summary>Exact match at requested time.</summary>
        Exact,

        /// <summary>Interpolated between two known samples.</summary>
        Interpolate,

        /// <summary>Extrapolated beyond known samples.</summary>
        Extrapolate,

        /// <summary>No data available.</summary>
        None
    }

    /// <summary>
    /// Gating configuration.
    /// </summary>
    public class GateConfig
    {
        /// <summary>Maximum ticks authority can be stale before flagging.</summary>
        public double MaxStaleTicks { get; set; } = 10;

        /// <summary>Maximum position divergence before correction (units).</summary>
        public float MaxPositionDivergence { get; set; } = 2.0f;

        /// <summary>Maximum rotation divergence before correction (degrees).</summary>
        public float MaxRotationDivergence { get; set; } = 30f;

        /// <summary>How many ticks of history to keep for interpolation.</summary>
        public int HistorySize { get; set; } = 32;

        /// <summary>Maximum ticks to extrapolate beyond known data.</summary>
        public double MaxExtrapolateTicks { get; set; } = 5;

        /// <summary>Interpolation delay (render behind authority for smoothness).</summary>
        public double InterpolationDelayTicks { get; set; } = 2;

        /// <summary>Blend rate for corrections (0-1, higher = snappier).</summary>
        public float CorrectionBlendRate { get; set; } = 0.3f;
    }

    /// <summary>
    /// Gate decision for a write operation.
    /// </summary>
    public readonly struct GateDecision
    {
        public readonly GateAction Action;
        public readonly string Reason;
        public readonly Vector3? CorrectionPosition;
        public readonly Quaternion? CorrectionRotation;

        private GateDecision(GateAction action, string reason, Vector3? corrPos = null, Quaternion? corrRot = null)
        {
            Action = action;
            Reason = reason;
            CorrectionPosition = corrPos;
            CorrectionRotation = corrRot;
        }

        public static GateDecision Allow() => new GateDecision(GateAction.Allow, "");
        public static GateDecision AllowWithWarning(string warning) => new GateDecision(GateAction.AllowWithWarning, warning);
        public static GateDecision Block(string reason) => new GateDecision(GateAction.Block, reason);
        public static GateDecision Correct(Vector3 position, string reason) =>
            new GateDecision(GateAction.Correct, reason, position);
        public static GateDecision Correct(Vector3 position, Quaternion rotation, string reason) =>
            new GateDecision(GateAction.Correct, reason, position, rotation);

        public bool IsAllowed => Action == GateAction.Allow || Action == GateAction.AllowWithWarning;
        public bool NeedsCorrection => Action == GateAction.Correct;
    }

    public enum GateAction : byte
    {
        Allow,
        AllowWithWarning,
        Block,
        Correct
    }

    /// <summary>
    /// Presentation state for rendering.
    /// This is what the game sees - interpolated/extrapolated from authority.
    /// </summary>
    public class PresentationState
    {
        public NetId EntityId { get; set; }
        public double RenderTime { get; set; }
        public Vector3 Position { get; set; }
        public Quaternion Rotation { get; set; }
        public Vector3 Velocity { get; set; }
        public SampleMode Mode { get; set; }
        public float Confidence { get; set; }
        public long SourceTick { get; set; }
        public bool Stale { get; set; }

        /// <summary>
        /// Get yaw angle in degrees.
        /// </summary>
        public float YawDegrees
        {
            get
            {
                var forward = Vector3.Transform(Vector3.UnitZ, Rotation);
                return MathF.Atan2(forward.X, forward.Z) * 180f / MathF.PI;
            }
        }
    }

    /// <summary>
    /// Per-entity attribute tracking state.
    /// </summary>
    public class EntityAttributeState
    {
        public NetId EntityId { get; }
        private readonly Dictionary<AttributeKind, IAttribute> _attributes = new();
        private readonly object _lock = new();

        public EntityAttributeState(NetId entityId)
        {
            EntityId = entityId;
        }

        public T? GetAttribute<T>(AttributeKind kind) where T : class, IAttribute
        {
            lock (_lock)
            {
                return _attributes.TryGetValue(kind, out var attr) ? attr as T : null;
            }
        }

        public T GetOrCreateAttribute<T>(AttributeKind kind) where T : class, IAttribute, new()
        {
            lock (_lock)
            {
                if (!_attributes.TryGetValue(kind, out var attr))
                {
                    attr = new T();
                    _attributes[kind] = attr;
                }
                return (T)attr;
            }
        }

        public void SetAttribute(AttributeKind kind, IAttribute attribute)
        {
            lock (_lock)
            {
                _attributes[kind] = attribute;
            }
        }
    }

    /// <summary>
    /// Base interface for attributes.
    /// </summary>
    public interface IAttribute
    {
        long LatestTick { get; }
        int SampleCount { get; }
    }

    /// <summary>
    /// Transform attribute with interpolation/extrapolation.
    /// </summary>
    public class TransformAttribute : IAttribute
    {
        private readonly List<TransformSample> _samples = new();
        private const int MaxSamples = 32;

        public long LatestTick => _samples.Count > 0 ? _samples[^1].Tick : 0;
        public int SampleCount => _samples.Count;

        public void PushSample(long tick, FramedTransform transform)
        {
            // Insert in sorted order
            var sample = new TransformSample
            {
                Tick = tick,
                Position = transform.Position,
                Rotation = transform.Rotation,
                Velocity = transform.Velocity
            };

            int insertIndex = _samples.Count;
            for (int i = _samples.Count - 1; i >= 0; i--)
            {
                if (_samples[i].Tick <= tick)
                {
                    insertIndex = i + 1;
                    break;
                }
                if (i == 0) insertIndex = 0;
            }

            _samples.Insert(insertIndex, sample);

            // Trim old samples
            while (_samples.Count > MaxSamples)
                _samples.RemoveAt(0);
        }

        public (Vector3 position, Quaternion rotation, Vector3 velocity, SampleMode mode) SampleAt(double time, GateConfig config)
        {
            if (_samples.Count == 0)
                return (Vector3.Zero, Quaternion.Identity, Vector3.Zero, SampleMode.None);

            // Apply interpolation delay (render behind for smoothness)
            double targetTime = time - config.InterpolationDelayTicks;

            // Find surrounding samples
            TransformSample? before = null;
            TransformSample? after = null;

            for (int i = 0; i < _samples.Count; i++)
            {
                if (_samples[i].Tick <= targetTime)
                    before = _samples[i];
                if (_samples[i].Tick >= targetTime && after == null)
                    after = _samples[i];
            }

            // Exact match
            if (before.HasValue && before.Value.Tick == targetTime)
                return (before.Value.Position, before.Value.Rotation, before.Value.Velocity, SampleMode.Exact);

            // Interpolate between two samples
            if (before.HasValue && after.HasValue && before.Value.Tick != after.Value.Tick)
            {
                float t = (float)((targetTime - before.Value.Tick) / (after.Value.Tick - before.Value.Tick));
                t = Math.Clamp(t, 0f, 1f);

                return (
                    Vector3.Lerp(before.Value.Position, after.Value.Position, t),
                    Quaternion.Slerp(before.Value.Rotation, after.Value.Rotation, t),
                    Vector3.Lerp(before.Value.Velocity, after.Value.Velocity, t),
                    SampleMode.Interpolate
                );
            }

            // Extrapolate beyond known data
            if (before.HasValue)
            {
                double ticksBeyond = targetTime - before.Value.Tick;

                // Clamp extrapolation
                if (ticksBeyond > config.MaxExtrapolateTicks)
                    ticksBeyond = config.MaxExtrapolateTicks;

                // Simple linear extrapolation using velocity
                var extrapolatedPos = before.Value.Position + before.Value.Velocity * (float)ticksBeyond;

                return (
                    extrapolatedPos,
                    before.Value.Rotation, // Don't extrapolate rotation
                    before.Value.Velocity,
                    SampleMode.Extrapolate
                );
            }

            // Only have future data - use first sample
            if (after.HasValue)
            {
                return (after.Value.Position, after.Value.Rotation, after.Value.Velocity, SampleMode.Extrapolate);
            }

            return (Vector3.Zero, Quaternion.Identity, Vector3.Zero, SampleMode.None);
        }

        public TransformSample? GetLatestSample()
        {
            return _samples.Count > 0 ? _samples[^1] : null;
        }
    }

    /// <summary>
    /// A transform sample at a specific tick.
    /// </summary>
    public struct TransformSample
    {
        public long Tick;
        public Vector3 Position;
        public Quaternion Rotation;
        public Vector3 Velocity;
    }

    /// <summary>
    /// Scalar attribute (health, energy, etc.) with interpolation.
    /// </summary>
    public class ScalarAttribute : IAttribute
    {
        private readonly List<ScalarSample> _samples = new();
        private const int MaxSamples = 16;

        public long LatestTick => _samples.Count > 0 ? _samples[^1].Tick : 0;
        public int SampleCount => _samples.Count;

        public void PushSample(long tick, float value)
        {
            _samples.Add(new ScalarSample { Tick = tick, Value = value });

            while (_samples.Count > MaxSamples)
                _samples.RemoveAt(0);
        }

        public (float value, SampleMode mode) SampleAt(double time)
        {
            if (_samples.Count == 0)
                return (0, SampleMode.None);

            // Find surrounding samples
            ScalarSample? before = null;
            ScalarSample? after = null;

            for (int i = 0; i < _samples.Count; i++)
            {
                if (_samples[i].Tick <= time)
                    before = _samples[i];
                if (_samples[i].Tick >= time && after == null)
                    after = _samples[i];
            }

            if (before.HasValue && after.HasValue && before.Value.Tick != after.Value.Tick)
            {
                float t = (float)((time - before.Value.Tick) / (after.Value.Tick - before.Value.Tick));
                return (before.Value.Value + (after.Value.Value - before.Value.Value) * t, SampleMode.Interpolate);
            }

            if (before.HasValue)
                return (before.Value.Value, SampleMode.Exact);

            if (after.HasValue)
                return (after.Value.Value, SampleMode.Extrapolate);

            return (0, SampleMode.None);
        }
    }

    public struct ScalarSample
    {
        public long Tick;
        public float Value;
    }

    /// <summary>
    /// Pending write queued for gating.
    /// </summary>
    internal struct PendingWrite
    {
        public NetId EntityId;
        public AttributeKind Kind;
        public object Value;
        public Action<GateDecision>? Callback;
    }

    /// <summary>
    /// Attribute ring statistics.
    /// </summary>
    public class AttributeRingStats
    {
        public int EntityCount { get; set; }
        public long Interpolations { get; set; }
        public long Extrapolations { get; set; }
        public long GatedWrites { get; set; }
        public long BlockedWrites { get; set; }
        public int PendingWrites { get; set; }

        public float ExtrapolationRatio =>
            (Interpolations + Extrapolations) > 0
                ? Extrapolations / (float)(Interpolations + Extrapolations)
                : 0;
    }
}

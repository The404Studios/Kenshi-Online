using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;
using System.Threading;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// DataBus - The Bidirectional Pipeline
    ///
    /// All information flows through the bus:
    ///   OUTBOUND (Write): Authority → Gate → Bus → Memory
    ///   INBOUND (Read):   Memory → Bus → Resolver → Response
    ///
    /// The game's memory isn't passive - it requests data constantly.
    /// We intercept those requests, resolve against authoritative state,
    /// and respond with correct data. The game never reads stale memory directly.
    ///
    /// Like DNA/RNA: information flows one direction through the machinery.
    /// No shortcuts, no bypasses, everything is logged.
    /// </summary>
    public class DataBus
    {
        // Write pipeline (outbound)
        private readonly ConcurrentQueue<BusWrite> _writeQueue = new();
        private readonly List<IPipelineStage> _writePipeline = new();

        // Read pipeline (inbound)
        private readonly ConcurrentQueue<BusRead> _readQueue = new();
        private readonly IRequestResolver _resolver;

        // Authority source
        private readonly AuthorityRing _authorityRing;
        private readonly AttributeRing _attributeRing;
        private readonly TickClock _clock;

        // Configuration
        private readonly BusConfig _config;

        // Statistics
        private long _writesEnqueued;
        private long _writesFlushed;
        private long _writesRejected;
        private long _readsResolved;
        private long _readsMissed;
        private long _readsCached;

        // Write cache for coalescing
        private readonly ConcurrentDictionary<(NetId, AttributeKind), BusWrite> _pendingWrites = new();

        public DataBus(
            AuthorityRing authorityRing,
            AttributeRing attributeRing,
            TickClock clock,
            BusConfig? config = null)
        {
            _authorityRing = authorityRing;
            _attributeRing = attributeRing;
            _clock = clock;
            _config = config ?? new BusConfig();
            _resolver = new AuthoritativeResolver(authorityRing, attributeRing, clock);

            // Initialize default pipeline stages
            InitializeDefaultPipeline();
        }

        private void InitializeDefaultPipeline()
        {
            _writePipeline.Add(new ValidationStage());
            _writePipeline.Add(new NormalizationStage());
            _writePipeline.Add(new AuthorizationStage(_authorityRing));
            _writePipeline.Add(new GatingStage(_attributeRing, _clock));
        }

        #region Write Path (Outbound)

        /// <summary>
        /// Enqueue a write to the bus. Goes through pipeline before reaching memory.
        /// </summary>
        public BusWriteResult EnqueueWrite(
            NetId entityId,
            AttributeKind kind,
            object value,
            NetId sourceId,
            WritePriority priority = WritePriority.Normal)
        {
            var write = new BusWrite
            {
                Id = Interlocked.Increment(ref _writesEnqueued),
                EntityId = entityId,
                Kind = kind,
                Value = value,
                SourceId = sourceId,
                Priority = priority,
                EnqueueTick = _clock.CurrentTick,
                Status = WriteStatus.Pending
            };

            // Run through pipeline stages
            foreach (var stage in _writePipeline)
            {
                var result = stage.Process(write);
                if (!result.Continue)
                {
                    write.Status = WriteStatus.Rejected;
                    write.RejectReason = result.Reason;
                    Interlocked.Increment(ref _writesRejected);
                    return new BusWriteResult(false, write.Id, result.Reason);
                }
                write = result.Write;
            }

            // Coalesce with existing pending write for same entity/attribute
            var key = (entityId, kind);
            _pendingWrites.AddOrUpdate(key, write, (k, existing) =>
            {
                // Keep newer write, but preserve ID for tracking
                write.CoalescedFrom = existing.Id;
                return write;
            });

            _writeQueue.Enqueue(write);
            return new BusWriteResult(true, write.Id);
        }

        /// <summary>
        /// Flush all pending writes to memory.
        /// Called at tick boundary for atomic batch writes.
        /// </summary>
        public FlushResult Flush(IMemoryActuator actuator)
        {
            var result = new FlushResult { FlushTick = _clock.CurrentTick };

            // Collect writes by priority
            var writes = new List<BusWrite>();
            while (_writeQueue.TryDequeue(out var write))
            {
                writes.Add(write);
            }

            // Sort by priority (Critical first)
            writes.Sort((a, b) => b.Priority.CompareTo(a.Priority));

            // Apply writes
            foreach (var write in writes)
            {
                try
                {
                    ApplyWrite(actuator, write);
                    write.Status = WriteStatus.Applied;
                    result.Applied++;
                    Interlocked.Increment(ref _writesFlushed);
                }
                catch (Exception ex)
                {
                    write.Status = WriteStatus.Failed;
                    write.RejectReason = ex.Message;
                    result.Failed++;
                }
            }

            // Clear coalesce cache
            _pendingWrites.Clear();

            return result;
        }

        private void ApplyWrite(IMemoryActuator actuator, BusWrite write)
        {
            // Get container for memory handle
            // This would be integrated with ContainerRing in practice

            switch (write.Kind)
            {
                case AttributeKind.Transform when write.Value is TransformPayload tp:
                    // Apply through actuator
                    actuator.WriteTransform(write.MemoryHandle, tp.Position, tp.Rotation);
                    break;

                case AttributeKind.Transform when write.Value is FramedTransform ft:
                    actuator.WriteTransform(write.MemoryHandle, ft.Position, ft.Rotation);
                    break;

                case AttributeKind.Health when write.Value is float health:
                    actuator.WriteHealth(write.MemoryHandle, health, 100f); // TODO: get max from state
                    break;

                case AttributeKind.Health when write.Value is HealthPayload hp:
                    actuator.WriteHealth(write.MemoryHandle, hp.Current, hp.Maximum);
                    break;
            }
        }

        #endregion

        #region Read Path (Inbound)

        /// <summary>
        /// Resolve a read request from memory.
        /// Returns authoritative data instead of potentially stale memory.
        /// </summary>
        public BusReadResult ResolveRead(NetId entityId, AttributeKind kind)
        {
            var read = new BusRead
            {
                EntityId = entityId,
                Kind = kind,
                RequestTick = _clock.CurrentTick
            };

            // Try to resolve from authority
            var resolution = _resolver.Resolve(entityId, kind);

            if (resolution.Found)
            {
                Interlocked.Increment(ref _readsResolved);
                return new BusReadResult
                {
                    Found = true,
                    Value = resolution.Value,
                    SourceTick = resolution.SourceTick,
                    Confidence = resolution.Confidence,
                    Mode = resolution.Mode
                };
            }

            Interlocked.Increment(ref _readsMissed);
            return new BusReadResult { Found = false };
        }

        /// <summary>
        /// Resolve position read request.
        /// </summary>
        public Vector3? ResolvePosition(NetId entityId)
        {
            var result = ResolveRead(entityId, AttributeKind.Transform);
            if (!result.Found) return null;

            return result.Value switch
            {
                TransformPayload tp => tp.Position,
                FramedTransform ft => ft.Position,
                PresentationState ps => ps.Position,
                Vector3 v => v,
                _ => null
            };
        }

        /// <summary>
        /// Resolve rotation read request.
        /// </summary>
        public Quaternion? ResolveRotation(NetId entityId)
        {
            var result = ResolveRead(entityId, AttributeKind.Transform);
            if (!result.Found) return null;

            return result.Value switch
            {
                TransformPayload tp => tp.Rotation,
                FramedTransform ft => ft.Rotation,
                PresentationState ps => ps.Rotation,
                Quaternion q => q,
                _ => null
            };
        }

        /// <summary>
        /// Resolve health read request.
        /// </summary>
        public float? ResolveHealth(NetId entityId)
        {
            var result = ResolveRead(entityId, AttributeKind.Health);
            if (!result.Found) return null;

            return result.Value switch
            {
                HealthPayload hp => hp.Current,
                float f => f,
                _ => null
            };
        }

        /// <summary>
        /// Batch resolve multiple entities.
        /// More efficient than individual calls.
        /// </summary>
        public Dictionary<NetId, BusReadResult> ResolveBatch(IEnumerable<(NetId entityId, AttributeKind kind)> requests)
        {
            var results = new Dictionary<NetId, BusReadResult>();
            foreach (var (entityId, kind) in requests)
            {
                results[entityId] = ResolveRead(entityId, kind);
            }
            return results;
        }

        #endregion

        #region Pipeline Management

        /// <summary>
        /// Add a custom pipeline stage.
        /// </summary>
        public void AddPipelineStage(IPipelineStage stage, int? index = null)
        {
            if (index.HasValue)
                _writePipeline.Insert(index.Value, stage);
            else
                _writePipeline.Add(stage);
        }

        /// <summary>
        /// Remove a pipeline stage by type.
        /// </summary>
        public bool RemovePipelineStage<T>() where T : IPipelineStage
        {
            var stage = _writePipeline.Find(s => s is T);
            if (stage != null)
            {
                _writePipeline.Remove(stage);
                return true;
            }
            return false;
        }

        #endregion

        #region Statistics

        public BusStats GetStats()
        {
            return new BusStats
            {
                WritesEnqueued = _writesEnqueued,
                WritesFlushed = _writesFlushed,
                WritesRejected = _writesRejected,
                ReadsResolved = _readsResolved,
                ReadsMissed = _readsMissed,
                ReadsCached = _readsCached,
                PendingWrites = _writeQueue.Count,
                PipelineStages = _writePipeline.Count
            };
        }

        #endregion
    }

    #region Write Types

    public enum WritePriority : byte
    {
        Low = 0,
        Normal = 1,
        High = 2,
        Critical = 3  // Corrections, snaps
    }

    public enum WriteStatus : byte
    {
        Pending,
        Applied,
        Rejected,
        Failed,
        Coalesced
    }

    public class BusWrite
    {
        public long Id { get; set; }
        public NetId EntityId { get; set; }
        public AttributeKind Kind { get; set; }
        public object Value { get; set; } = null!;
        public NetId SourceId { get; set; }
        public WritePriority Priority { get; set; }
        public long EnqueueTick { get; set; }
        public WriteStatus Status { get; set; }
        public string? RejectReason { get; set; }
        public long? CoalescedFrom { get; set; }
        public IntPtr MemoryHandle { get; set; }
    }

    public readonly struct BusWriteResult
    {
        public readonly bool Success;
        public readonly long WriteId;
        public readonly string? Reason;

        public BusWriteResult(bool success, long writeId, string? reason = null)
        {
            Success = success;
            WriteId = writeId;
            Reason = reason;
        }
    }

    public class FlushResult
    {
        public long FlushTick { get; set; }
        public int Applied { get; set; }
        public int Failed { get; set; }
        public int Coalesced { get; set; }
    }

    #endregion

    #region Read Types

    public class BusRead
    {
        public NetId EntityId { get; set; }
        public AttributeKind Kind { get; set; }
        public long RequestTick { get; set; }
    }

    public class BusReadResult
    {
        public bool Found { get; set; }
        public object? Value { get; set; }
        public long SourceTick { get; set; }
        public float Confidence { get; set; }
        public SampleMode Mode { get; set; }
    }

    #endregion

    #region Pipeline Stages

    /// <summary>
    /// A stage in the write pipeline.
    /// Each stage can validate, transform, or reject a write.
    /// </summary>
    public interface IPipelineStage
    {
        string Name { get; }
        PipelineResult Process(BusWrite write);
    }

    public readonly struct PipelineResult
    {
        public readonly bool Continue;
        public readonly BusWrite Write;
        public readonly string? Reason;

        public PipelineResult(bool cont, BusWrite write, string? reason = null)
        {
            Continue = cont;
            Write = write;
            Reason = reason;
        }

        public static PipelineResult Pass(BusWrite write) => new PipelineResult(true, write);
        public static PipelineResult Reject(BusWrite write, string reason) => new PipelineResult(false, write, reason);
    }

    /// <summary>
    /// Stage 1: Validate the write is well-formed.
    /// </summary>
    public class ValidationStage : IPipelineStage
    {
        public string Name => "Validation";

        public PipelineResult Process(BusWrite write)
        {
            if (!write.EntityId.IsValid)
                return PipelineResult.Reject(write, "Invalid entity ID");

            if (write.Value == null)
                return PipelineResult.Reject(write, "Null value");

            return PipelineResult.Pass(write);
        }
    }

    /// <summary>
    /// Stage 2: Normalize the value to canonical form.
    /// </summary>
    public class NormalizationStage : IPipelineStage
    {
        public string Name => "Normalization";

        public PipelineResult Process(BusWrite write)
        {
            // Normalize transforms
            if (write.Value is TransformPayload tp)
            {
                write.Value = PayloadNormalizer.NormalizeTransform(tp);
            }

            return PipelineResult.Pass(write);
        }
    }

    /// <summary>
    /// Stage 3: Check authorization to write.
    /// </summary>
    public class AuthorizationStage : IPipelineStage
    {
        private readonly AuthorityRing _authorityRing;

        public AuthorizationStage(AuthorityRing authorityRing)
        {
            _authorityRing = authorityRing;
        }

        public string Name => "Authorization";

        public PipelineResult Process(BusWrite write)
        {
            // Check if source has authority to write this attribute
            var state = _authorityRing.GetEntityState(write.EntityId);
            if (state == null)
            {
                // Entity not in authority ring - allow if it's a spawn
                return PipelineResult.Pass(write);
            }

            // For now, allow all writes that have entity state
            // In practice, would check authority ownership here
            return PipelineResult.Pass(write);
        }
    }

    /// <summary>
    /// Stage 4: Gate through attribute ring for final check.
    /// </summary>
    public class GatingStage : IPipelineStage
    {
        private readonly AttributeRing _attributeRing;
        private readonly TickClock _clock;

        public GatingStage(AttributeRing attributeRing, TickClock clock)
        {
            _attributeRing = attributeRing;
            _clock = clock;
        }

        public string Name => "Gating";

        public PipelineResult Process(BusWrite write)
        {
            var decision = _attributeRing.GateWrite(
                write.EntityId,
                write.Kind,
                write.Value,
                _clock.Now.ContinuousTime);

            if (decision.Action == GateAction.Block)
                return PipelineResult.Reject(write, decision.Reason);

            // Apply correction if needed
            if (decision.NeedsCorrection && decision.CorrectionPosition.HasValue)
            {
                if (write.Value is TransformPayload tp)
                {
                    tp.Position = decision.CorrectionPosition.Value;
                    if (decision.CorrectionRotation.HasValue)
                        tp.Rotation = decision.CorrectionRotation.Value;
                    write.Value = tp;
                }
            }

            return PipelineResult.Pass(write);
        }
    }

    #endregion

    #region Request Resolver

    /// <summary>
    /// Resolves read requests against authoritative state.
    /// </summary>
    public interface IRequestResolver
    {
        Resolution Resolve(NetId entityId, AttributeKind kind);
    }

    public readonly struct Resolution
    {
        public readonly bool Found;
        public readonly object? Value;
        public readonly long SourceTick;
        public readonly float Confidence;
        public readonly SampleMode Mode;

        public Resolution(bool found, object? value = null, long sourceTick = 0, float confidence = 0, SampleMode mode = SampleMode.None)
        {
            Found = found;
            Value = value;
            SourceTick = sourceTick;
            Confidence = confidence;
            Mode = mode;
        }

        public static Resolution NotFound => new Resolution(false);
    }

    /// <summary>
    /// Resolves requests from Ring3 (authority) and Ring4 (presentation).
    /// Finds the closest match to what's being requested.
    /// </summary>
    public class AuthoritativeResolver : IRequestResolver
    {
        private readonly AuthorityRing _authorityRing;
        private readonly AttributeRing _attributeRing;
        private readonly TickClock _clock;

        public AuthoritativeResolver(AuthorityRing authorityRing, AttributeRing attributeRing, TickClock clock)
        {
            _authorityRing = authorityRing;
            _attributeRing = attributeRing;
            _clock = clock;
        }

        public Resolution Resolve(NetId entityId, AttributeKind kind)
        {
            // First try presentation state (interpolated, most up-to-date for rendering)
            var presentation = _attributeRing.GetPresentationState(entityId, _clock.Now.ContinuousTime);
            if (presentation != null)
            {
                return kind switch
                {
                    AttributeKind.Transform => new Resolution(
                        true,
                        presentation,
                        presentation.SourceTick,
                        presentation.Confidence,
                        presentation.Mode),

                    _ => TryAuthorityFallback(entityId, kind)
                };
            }

            // Fallback to authority ring (raw committed state)
            return TryAuthorityFallback(entityId, kind);
        }

        private Resolution TryAuthorityFallback(NetId entityId, AttributeKind kind)
        {
            var state = _authorityRing.GetEntityState(entityId);
            if (state == null)
                return Resolution.NotFound;

            return kind switch
            {
                AttributeKind.Transform when state.Transform != null =>
                    new Resolution(true, state.Transform, state.LastTick, 1.0f, SampleMode.Exact),

                AttributeKind.Health when state.Health != null =>
                    new Resolution(true, state.Health, state.LastTick, 1.0f, SampleMode.Exact),

                AttributeKind.Inventory when state.Inventory != null =>
                    new Resolution(true, state.Inventory, state.LastTick, 1.0f, SampleMode.Exact),

                AttributeKind.AIState when state.AIState != null =>
                    new Resolution(true, state.AIState, state.LastTick, 1.0f, SampleMode.Exact),

                _ => Resolution.NotFound
            };
        }
    }

    #endregion

    #region Configuration

    public class BusConfig
    {
        /// <summary>Maximum writes to queue before dropping.</summary>
        public int MaxQueuedWrites { get; set; } = 10000;

        /// <summary>Enable write coalescing (merge multiple writes to same target).</summary>
        public bool EnableCoalescing { get; set; } = true;

        /// <summary>Enable read caching.</summary>
        public bool EnableReadCache { get; set; } = true;

        /// <summary>Read cache TTL in ticks.</summary>
        public int ReadCacheTtlTicks { get; set; } = 2;
    }

    public class BusStats
    {
        public long WritesEnqueued { get; set; }
        public long WritesFlushed { get; set; }
        public long WritesRejected { get; set; }
        public long ReadsResolved { get; set; }
        public long ReadsMissed { get; set; }
        public long ReadsCached { get; set; }
        public int PendingWrites { get; set; }
        public int PipelineStages { get; set; }

        public float WriteSuccessRate =>
            WritesEnqueued > 0 ? WritesFlushed / (float)WritesEnqueued : 0;

        public float ReadHitRate =>
            (ReadsResolved + ReadsMissed) > 0 ? ReadsResolved / (float)(ReadsResolved + ReadsMissed) : 0;
    }

    #endregion
}

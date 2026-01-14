using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// RingCoordinator - The Metabolism
    ///
    /// The authority cycle:
    ///   1. Observe memory reality (Ring2 candidates)
    ///   2. Normalize into canonical operations (Ring3)
    ///   3. Apply back into memory (authoritative patch)
    ///   4. Verify by re-observing (feedback loop)
    ///   5. Seal commit once stable
    ///
    /// This is literally control theory: measure → decide → actuate → re-measure.
    ///
    /// If you skip the verification step, you'll "commit lies" that don't actually
    /// take in the game.
    ///
    /// The coordinator ties the three rings together:
    ///   - Ring1 (Container): What exists
    ///   - Ring2 (Info): What was observed/proposed
    ///   - Ring3 (Authority): What is true
    /// </summary>
    public class RingCoordinator : IDisposable
    {
        // The three rings
        public ContainerRing ContainerRing { get; }
        public InfoRing InfoRing { get; }
        public AuthorityRing AuthorityRing { get; }

        // Core systems
        public NetIdRegistry NetIdRegistry { get; }
        public AuthorityTracker AuthorityTracker { get; }
        public TickClock Clock { get; }

        // Frame resolution
        private readonly SimpleFrameResolver _frameResolver;

        // Memory actuator interface
        private readonly IMemoryActuator? _memoryActuator;

        // Processing
        private readonly ConcurrentQueue<ProcessingResult> _pendingVerifications = new();
        private readonly CancellationTokenSource _cts = new();
        private Task? _processingTask;

        // Configuration
        private readonly CoordinatorConfig _config;

        // Statistics
        private long _cyclesProcessed;
        private long _observationsProcessed;
        private long _commitsGenerated;
        private long _correctionsApplied;

        public RingCoordinator(CoordinatorConfig? config = null, IMemoryActuator? memoryActuator = null)
        {
            _config = config ?? new CoordinatorConfig();
            _memoryActuator = memoryActuator;

            // Initialize core systems
            NetIdRegistry = new NetIdRegistry();
            AuthorityTracker = new AuthorityTracker();
            Clock = new TickClock(_config.TickRateHz);

            // Initialize rings
            ContainerRing = new ContainerRing(NetIdRegistry, AuthorityTracker, _config.ContainerEventCapacity);
            InfoRing = new InfoRing(_config.InfoRingCapacity);
            AuthorityRing = new AuthorityRing(_config.AuthorityRingCapacity, _config.SnapshotInterval);

            // Frame resolver - gets parent transforms from Container/Authority rings
            _frameResolver = new SimpleFrameResolver((parentId, boneIndex) =>
            {
                var state = AuthorityRing.GetEntityState(parentId);
                return state?.Transform?.ToFramedTransform();
            });
        }

        /// <summary>
        /// Start the processing loop.
        /// </summary>
        public void Start()
        {
            if (_processingTask != null) return;
            _processingTask = Task.Run(ProcessingLoop, _cts.Token);
        }

        /// <summary>
        /// Stop the processing loop.
        /// </summary>
        public void Stop()
        {
            _cts.Cancel();
            _processingTask?.Wait(1000);
        }

        /// <summary>
        /// Run a single processing cycle (for manual control).
        /// </summary>
        public CycleResult ProcessCycle()
        {
            var sw = Stopwatch.StartNew();
            var result = new CycleResult { Tick = Clock.CurrentTick };

            try
            {
                // 1. Advance tick
                var tickTime = Clock.Advance();
                result.Tick = tickTime.Tick;

                // 2. Drain info ring and process observations
                var infos = InfoRing.DrainPending(_config.MaxInfosPerCycle);
                result.ObservationsProcessed = infos.Count;

                foreach (var info in infos)
                {
                    ProcessInfo(info, result);
                }

                // 3. Verify pending corrections
                ProcessPendingVerifications(result);

                Interlocked.Increment(ref _cyclesProcessed);
            }
            catch (Exception ex)
            {
                result.Error = ex.Message;
            }

            result.ProcessingTimeMs = sw.Elapsed.TotalMilliseconds;
            return result;
        }

        private async Task ProcessingLoop()
        {
            var targetMs = 1000.0 / _config.TickRateHz;

            while (!_cts.Token.IsCancellationRequested)
            {
                var sw = Stopwatch.StartNew();

                ProcessCycle();

                // Sleep to maintain tick rate
                var elapsed = sw.Elapsed.TotalMilliseconds;
                var sleepMs = targetMs - elapsed;
                if (sleepMs > 0)
                {
                    await Task.Delay((int)sleepMs, _cts.Token).ConfigureAwait(false);
                }
            }
        }

        private void ProcessInfo(InfoEntry info, CycleResult result)
        {
            Interlocked.Increment(ref _observationsProcessed);

            // Evaluate confidence and decide
            var decision = info.Confidence.Decide(
                _config.AcceptThreshold,
                _config.RejectThreshold);

            switch (decision)
            {
                case ConfidenceDecision.Accept:
                    CommitInfo(info, result);
                    break;

                case ConfidenceDecision.Reject:
                    InfoRing.MarkProcessed(info.Id, InfoStatus.Rejected);
                    result.Rejected++;
                    break;

                case ConfidenceDecision.RequestMoreSamples:
                    InfoRing.MarkProcessed(info.Id, InfoStatus.Deferred);
                    result.Deferred++;
                    break;

                case ConfidenceDecision.Defer:
                    InfoRing.MarkProcessed(info.Id, InfoStatus.Deferred);
                    result.Deferred++;
                    break;

                case ConfidenceDecision.OverrideSnap:
                    CommitWithSnap(info, result);
                    break;
            }
        }

        private void CommitInfo(InfoEntry info, CycleResult result)
        {
            // Get entity's authority
            var container = ContainerRing.Get(info.SubjectId);
            if (container == null)
            {
                InfoRing.MarkProcessed(info.Id, InfoStatus.Rejected);
                result.Rejected++;
                return;
            }

            // Determine operation type
            var operation = info.Kind switch
            {
                InfoKind.Input => CommitOp.Set,
                InfoKind.Observation => CommitOp.Set,
                InfoKind.Event => CommitOp.Event,
                InfoKind.Proposal => CommitOp.Set,
                InfoKind.Correction => CommitOp.Set,
                _ => CommitOp.Set
            };

            // Commit to authority ring
            var commit = AuthorityRing.Commit(
                info.SubjectId,
                operation,
                info.Payload,
                info.ObservationTick,
                container.Authority.Epoch,
                info.SourceId);

            if (commit.HasValue && commit.Value.Result == CommitResult.Accepted)
            {
                InfoRing.MarkProcessed(info.Id, InfoStatus.Accepted);
                result.Committed++;
                Interlocked.Increment(ref _commitsGenerated);

                // Apply to memory if we have an actuator
                if (_memoryActuator != null && container.MemoryHandle != IntPtr.Zero)
                {
                    ApplyToMemory(commit.Value, container);
                }

                // Provide positive feedback
                InfoRing.ProvideFeedback(info.SourceId, true);
            }
            else
            {
                InfoRing.MarkProcessed(info.Id, InfoStatus.Rejected);
                result.Rejected++;

                // Provide negative feedback
                InfoRing.ProvideFeedback(info.SourceId, false);
            }
        }

        private void CommitWithSnap(InfoEntry info, CycleResult result)
        {
            // Force commit (override normal validation) for authoritative corrections
            var container = ContainerRing.Get(info.SubjectId);
            if (container == null)
            {
                InfoRing.MarkProcessed(info.Id, InfoStatus.Rejected);
                return;
            }

            // Direct commit with snap flag
            var commit = AuthorityRing.Commit(
                info.SubjectId,
                CommitOp.Set,
                info.Payload,
                info.ObservationTick,
                container.Authority.Epoch,
                info.SourceId,
                "Snap correction");

            if (commit.HasValue)
            {
                InfoRing.MarkProcessed(info.Id, InfoStatus.Accepted);
                result.Committed++;
                result.Snaps++;

                // Apply hard correction to memory
                if (_memoryActuator != null && container.MemoryHandle != IntPtr.Zero)
                {
                    ApplySnapToMemory(commit.Value, container);
                }
            }
        }

        private void ApplyToMemory(Commit commit, ContainerEntry container)
        {
            if (_memoryActuator == null) return;

            try
            {
                // Convert payload to memory write
                if (commit.Payload is TransformPayload tp)
                {
                    // Resolve to world space if needed
                    var worldTransform = _frameResolver.ToWorld(tp.ToFramedTransform());

                    _memoryActuator.WriteTransform(
                        container.MemoryHandle,
                        worldTransform.Position,
                        worldTransform.Rotation);

                    // Queue verification
                    _pendingVerifications.Enqueue(new ProcessingResult
                    {
                        EntityId = commit.SubjectId,
                        CommitId = commit.CommitId,
                        ExpectedPayload = commit.Payload,
                        VerifyAtTick = Clock.CurrentTick + 1
                    });
                }
                else if (commit.Payload is HealthPayload hp)
                {
                    _memoryActuator.WriteHealth(container.MemoryHandle, hp.Current, hp.Maximum);
                }
            }
            catch (Exception)
            {
                // Memory write failed - this is expected in some cases
            }
        }

        private void ApplySnapToMemory(Commit commit, ContainerEntry container)
        {
            if (_memoryActuator == null) return;

            try
            {
                if (commit.Payload is TransformPayload tp)
                {
                    var worldTransform = _frameResolver.ToWorld(tp.ToFramedTransform());

                    // Snap applies immediately without interpolation
                    _memoryActuator.WriteTransformImmediate(
                        container.MemoryHandle,
                        worldTransform.Position,
                        worldTransform.Rotation);

                    Interlocked.Increment(ref _correctionsApplied);
                }
            }
            catch (Exception)
            {
                // Snap failed
            }
        }

        private void ProcessPendingVerifications(CycleResult result)
        {
            if (_memoryActuator == null) return;

            var currentTick = Clock.CurrentTick;
            var toRequeue = new List<ProcessingResult>();

            while (_pendingVerifications.TryDequeue(out var pending))
            {
                if (pending.VerifyAtTick > currentTick)
                {
                    // Not time yet
                    toRequeue.Add(pending);
                    continue;
                }

                // Re-read from memory and verify
                var container = ContainerRing.Get(pending.EntityId);
                if (container == null) continue;

                if (pending.ExpectedPayload is TransformPayload expectedTransform)
                {
                    var actualTransform = _memoryActuator.ReadTransform(container.MemoryHandle);
                    if (actualTransform == null) continue;

                    // Check if correction took
                    float distance = Vector3.Distance(
                        expectedTransform.Position,
                        actualTransform.Value.position);

                    if (distance > _config.VerificationThreshold)
                    {
                        // Correction didn't take - may need harder snap
                        result.VerificationsFailed++;

                        // Provide negative feedback
                        InfoRing.ProvideFeedback(pending.EntityId, false);
                    }
                    else
                    {
                        result.VerificationsSucceeded++;
                    }
                }
            }

            // Requeue ones not yet due
            foreach (var pending in toRequeue)
            {
                _pendingVerifications.Enqueue(pending);
            }
        }

        #region Public API

        /// <summary>
        /// Submit an observation from memory reading.
        /// </summary>
        public bool SubmitObservation(
            NetId subjectId,
            SchemaPayload payload,
            NetId sourceId,
            float confidence = 0.8f)
        {
            var currentTime = Clock.Now;
            return InfoRing.Enqueue(
                subjectId,
                sourceId,
                InfoKind.Observation,
                payload,
                currentTime,
                currentTime,
                confidence);
        }

        /// <summary>
        /// Submit player input.
        /// </summary>
        public bool SubmitInput(
            NetId playerId,
            InputPayload input,
            TickTime inputTime)
        {
            return InfoRing.Enqueue(
                playerId,
                playerId,
                InfoKind.Input,
                input,
                inputTime,
                Clock.Now,
                0.95f); // High confidence for direct input
        }

        /// <summary>
        /// Submit a proposal from a remote client.
        /// </summary>
        public bool SubmitProposal(
            NetId subjectId,
            SchemaPayload payload,
            NetId sourceId,
            TickTime observationTime,
            float confidence)
        {
            return InfoRing.Enqueue(
                subjectId,
                sourceId,
                InfoKind.Proposal,
                payload,
                observationTime,
                Clock.Now,
                confidence);
        }

        /// <summary>
        /// Register a new entity.
        /// </summary>
        public NetId RegisterEntity(
            EntityKind kind,
            IntPtr memoryHandle,
            AuthorityOwner owner,
            NetId? ownerId = null,
            string? templateId = null)
        {
            var currentTick = Clock.CurrentTick;
            var epoch = AuthorityTracker.NextEpoch();

            var authority = owner switch
            {
                AuthorityOwner.Server => AuthorityCoordinate.Server(AuthorityScope.All, epoch, currentTick),
                AuthorityOwner.Client => AuthorityCoordinate.Client(ownerId ?? NetId.Invalid, AuthorityScope.All, epoch, currentTick),
                _ => AuthorityCoordinate.Server(AuthorityScope.All, epoch, currentTick)
            };

            return ContainerRing.Register(
                kind,
                memoryHandle,
                SpaceFrame.World,
                authority,
                currentTick,
                templateId);
        }

        /// <summary>
        /// Unregister an entity.
        /// </summary>
        public bool UnregisterEntity(NetId entityId, string reason = "")
        {
            // Commit despawn to authority ring
            AuthorityRing.Commit(
                entityId,
                CommitOp.Despawn,
                new DespawnPayload { EntityId = entityId, Reason = reason },
                Clock.CurrentTick,
                0,
                NetId.Invalid);

            AuthorityRing.RemoveEntityState(entityId);
            return ContainerRing.Unregister(entityId, Clock.CurrentTick, reason);
        }

        /// <summary>
        /// Get the current authoritative state of an entity.
        /// </summary>
        public EntityTruthState? GetEntityState(NetId entityId)
        {
            return AuthorityRing.GetEntityState(entityId);
        }

        /// <summary>
        /// Get commits for replication to clients.
        /// </summary>
        public IEnumerable<Commit> GetCommitsSince(long fromCommitId)
        {
            return AuthorityRing.GetCommitsSince(fromCommitId);
        }

        /// <summary>
        /// Answer the sanity test questions for an entity.
        /// </summary>
        public SanityAnswer? GetSanityAnswer(NetId entityId)
        {
            var container = ContainerRing.Get(entityId);
            if (container == null) return null;

            var state = AuthorityRing.GetEntityState(entityId);

            return new SanityAnswer
            {
                // Who is it? (NetId)
                WhoIsIt = entityId.ToString(),

                // When is this true? (tick / commit_id)
                WhenIsThisTrue = state != null
                    ? $"Tick {state.LastTick}, Commit {state.LastCommitId}"
                    : "Unknown",

                // Who decided it? (authority_owner/epoch)
                WhoDecidedIt = $"{container.Authority.Owner} @ Epoch {container.Authority.Epoch}",

                // What does it mean? (schema + op)
                WhatDoesItMean = state?.LastCommit != null
                    ? $"{state.LastCommit.Value.Operation} {state.LastCommit.Value.Payload.SchemaId}"
                    : "Unknown",

                // In what frame? (space reference)
                InWhatFrame = container.Frame.ToString(),

                // How sure are we? (confidence)
                HowSureAreWe = "Committed (100%)" // In Ring3, we're certain
            };
        }

        #endregion

        #region Statistics

        public CoordinatorStats GetStats()
        {
            var (infoEnqueued, infoDropped, infoProcessed) = InfoRing.GetStats();
            var (commits, rejected, coalesced) = AuthorityRing.GetStats();

            return new CoordinatorStats
            {
                CurrentTick = Clock.CurrentTick,
                CyclesProcessed = _cyclesProcessed,
                ObservationsProcessed = _observationsProcessed,
                CommitsGenerated = _commitsGenerated,
                CorrectionsApplied = _correctionsApplied,
                ContainerCount = ContainerRing.Count,
                InfoPending = InfoRing.PendingCount,
                InfoEnqueued = infoEnqueued,
                InfoDropped = infoDropped,
                AuthorityCommits = commits,
                AuthorityRejected = rejected,
                AuthorityCoalesced = coalesced
            };
        }

        #endregion

        public void Dispose()
        {
            Stop();
            _cts.Dispose();
        }
    }

    /// <summary>
    /// Configuration for the ring coordinator.
    /// </summary>
    public class CoordinatorConfig
    {
        public double TickRateHz { get; set; } = 20.0;
        public int ContainerEventCapacity { get; set; } = 4096;
        public int InfoRingCapacity { get; set; } = 16384;
        public int AuthorityRingCapacity { get; set; } = 32768;
        public int SnapshotInterval { get; set; } = 1000;
        public int MaxInfosPerCycle { get; set; } = 1000;
        public float AcceptThreshold { get; set; } = 0.8f;
        public float RejectThreshold { get; set; } = 0.2f;
        public float VerificationThreshold { get; set; } = 0.1f; // Max distance for verification pass
    }

    /// <summary>
    /// Result of a processing cycle.
    /// </summary>
    public class CycleResult
    {
        public long Tick { get; set; }
        public int ObservationsProcessed { get; set; }
        public int Committed { get; set; }
        public int Rejected { get; set; }
        public int Deferred { get; set; }
        public int Snaps { get; set; }
        public int VerificationsSucceeded { get; set; }
        public int VerificationsFailed { get; set; }
        public double ProcessingTimeMs { get; set; }
        public string? Error { get; set; }
    }

    /// <summary>
    /// Coordinator statistics.
    /// </summary>
    public class CoordinatorStats
    {
        public long CurrentTick { get; set; }
        public long CyclesProcessed { get; set; }
        public long ObservationsProcessed { get; set; }
        public long CommitsGenerated { get; set; }
        public long CorrectionsApplied { get; set; }
        public int ContainerCount { get; set; }
        public long InfoPending { get; set; }
        public long InfoEnqueued { get; set; }
        public long InfoDropped { get; set; }
        public long AuthorityCommits { get; set; }
        public long AuthorityRejected { get; set; }
        public long AuthorityCoalesced { get; set; }
    }

    /// <summary>
    /// Sanity test answers for an entity.
    /// If you can answer these at runtime, you have the needed dimensions.
    /// </summary>
    public class SanityAnswer
    {
        public string WhoIsIt { get; set; } = "";
        public string WhenIsThisTrue { get; set; } = "";
        public string WhoDecidedIt { get; set; } = "";
        public string WhatDoesItMean { get; set; } = "";
        public string InWhatFrame { get; set; } = "";
        public string HowSureAreWe { get; set; } = "";

        public override string ToString()
        {
            return $@"Sanity Check:
  Who is it? {WhoIsIt}
  When is this true? {WhenIsThisTrue}
  Who decided it? {WhoDecidedIt}
  What does it mean? {WhatDoesItMean}
  In what frame? {InWhatFrame}
  How sure are we? {HowSureAreWe}";
        }
    }

    /// <summary>
    /// Pending verification result.
    /// </summary>
    internal class ProcessingResult
    {
        public NetId EntityId { get; set; }
        public long CommitId { get; set; }
        public SchemaPayload? ExpectedPayload { get; set; }
        public long VerifyAtTick { get; set; }
    }

    /// <summary>
    /// Interface for memory reading/writing.
    /// Implement this to connect to actual game memory.
    /// </summary>
    public interface IMemoryActuator
    {
        /// <summary>
        /// Read transform from memory.
        /// </summary>
        (Vector3 position, Quaternion rotation)? ReadTransform(IntPtr handle);

        /// <summary>
        /// Write transform to memory (soft - may be interpolated).
        /// </summary>
        void WriteTransform(IntPtr handle, Vector3 position, Quaternion rotation);

        /// <summary>
        /// Write transform immediately (hard snap).
        /// </summary>
        void WriteTransformImmediate(IntPtr handle, Vector3 position, Quaternion rotation);

        /// <summary>
        /// Read health from memory.
        /// </summary>
        (float current, float max)? ReadHealth(IntPtr handle);

        /// <summary>
        /// Write health to memory.
        /// </summary>
        void WriteHealth(IntPtr handle, float current, float max);
    }
}

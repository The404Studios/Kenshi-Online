using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Ring 2: Info Ring (Time + Meaning + Confidence)
    ///
    /// Entry includes:
    ///   - tick
    ///   - source
    ///   - kind (input / observation / event)
    ///   - payload schema_id
    ///   - confidence
    ///   - optional hash/proof
    ///
    /// This ring is "the world speaking," but not yet trusted.
    ///
    /// Invariant: Ring2 entries must be interpretable even if memory layout changes.
    /// This means we carry extractable meaning, not raw bytes.
    /// </summary>
    public class InfoRing
    {
        private readonly InfoEntry[] _buffer;
        private readonly int _capacity;
        private long _head;
        private long _tail;
        private readonly object _lock = new();

        // Per-entity queues for ordered processing
        private readonly ConcurrentDictionary<NetId, ConcurrentQueue<InfoEntry>> _entityQueues = new();

        // Confidence evaluator
        private readonly ConfidenceEvaluator _confidenceEvaluator = new();

        // Statistics
        private long _totalEnqueued;
        private long _totalDropped;
        private long _totalProcessed;

        public InfoRing(int capacity = 16384)
        {
            _capacity = capacity;
            _buffer = new InfoEntry[capacity];
        }

        /// <summary>
        /// Enqueue an observation/proposal.
        /// </summary>
        public bool Enqueue(
            NetId subjectId,
            NetId sourceId,
            InfoKind kind,
            SchemaPayload payload,
            TickTime observationTime,
            TickTime currentTime,
            float baseConfidence = 0.8f)
        {
            // Evaluate confidence
            var confidence = _confidenceEvaluator.Evaluate(
                sourceId,
                observationTime.Tick,
                currentTime.Tick,
                baseConfidence);

            var entry = new InfoEntry
            {
                Id = Interlocked.Increment(ref _totalEnqueued),
                SubjectId = subjectId,
                SourceId = sourceId,
                Kind = kind,
                SchemaId = payload.SchemaId,
                Payload = payload,
                ObservationTick = observationTime.Tick,
                ReceiveTick = currentTime.Tick,
                Confidence = confidence,
                PayloadHash = payload.ComputeHash(),
                Status = InfoStatus.Pending
            };

            return EnqueueEntry(entry);
        }

        /// <summary>
        /// Enqueue a raw entry (for internal use or deserialization).
        /// </summary>
        public bool EnqueueEntry(InfoEntry entry)
        {
            lock (_lock)
            {
                // Check if buffer is full
                if (_head - _tail >= _capacity)
                {
                    // Drop oldest
                    Interlocked.Increment(ref _totalDropped);
                    _tail++;
                }

                int index = (int)(_head % _capacity);
                _buffer[index] = entry;
                _head++;
            }

            // Also queue by entity for ordered processing
            var entityQueue = _entityQueues.GetOrAdd(entry.SubjectId, _ => new ConcurrentQueue<InfoEntry>());
            entityQueue.Enqueue(entry);

            return true;
        }

        /// <summary>
        /// Dequeue the next entry for processing.
        /// </summary>
        public InfoEntry? Dequeue()
        {
            lock (_lock)
            {
                if (_head <= _tail)
                    return null;

                int index = (int)(_tail % _capacity);
                var entry = _buffer[index];
                _tail++;
                Interlocked.Increment(ref _totalProcessed);
                return entry;
            }
        }

        /// <summary>
        /// Peek at the next entry without removing it.
        /// </summary>
        public InfoEntry? Peek()
        {
            lock (_lock)
            {
                if (_head <= _tail)
                    return null;

                int index = (int)(_tail % _capacity);
                return _buffer[index];
            }
        }

        /// <summary>
        /// Get all pending entries for a specific entity.
        /// </summary>
        public IEnumerable<InfoEntry> GetPendingForEntity(NetId entityId)
        {
            if (_entityQueues.TryGetValue(entityId, out var queue))
            {
                foreach (var entry in queue)
                {
                    if (entry.Status == InfoStatus.Pending)
                        yield return entry;
                }
            }
        }

        /// <summary>
        /// Get all pending entries of a specific kind.
        /// </summary>
        public IEnumerable<InfoEntry> GetPendingByKind(InfoKind kind)
        {
            lock (_lock)
            {
                for (long i = _tail; i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    var entry = _buffer[index];
                    if (entry.Kind == kind && entry.Status == InfoStatus.Pending)
                        yield return entry;
                }
            }
        }

        /// <summary>
        /// Get entries within a tick range.
        /// </summary>
        public IEnumerable<InfoEntry> GetInTickRange(long fromTick, long toTick)
        {
            lock (_lock)
            {
                for (long i = _tail; i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    var entry = _buffer[index];
                    if (entry.ObservationTick >= fromTick && entry.ObservationTick <= toTick)
                        yield return entry;
                }
            }
        }

        /// <summary>
        /// Mark an entry as processed with a specific status.
        /// </summary>
        public void MarkProcessed(long entryId, InfoStatus newStatus)
        {
            lock (_lock)
            {
                for (long i = _tail; i < _head; i++)
                {
                    int index = (int)(i % _capacity);
                    if (_buffer[index].Id == entryId)
                    {
                        _buffer[index].Status = newStatus;
                        break;
                    }
                }
            }
        }

        /// <summary>
        /// Provide feedback for confidence learning.
        /// </summary>
        public void ProvideFeedback(NetId sourceId, bool wasAccurate)
        {
            _confidenceEvaluator.ProvideFeedback(sourceId, wasAccurate);
        }

        /// <summary>
        /// Drain all pending entries up to a max count.
        /// </summary>
        public List<InfoEntry> DrainPending(int maxCount = int.MaxValue)
        {
            var result = new List<InfoEntry>();
            int count = 0;

            while (count < maxCount)
            {
                var entry = Dequeue();
                if (!entry.HasValue) break;
                result.Add(entry.Value);
                count++;
            }

            return result;
        }

        /// <summary>
        /// Clear entity queue after processing.
        /// </summary>
        public void ClearEntityQueue(NetId entityId)
        {
            if (_entityQueues.TryGetValue(entityId, out var queue))
            {
                while (queue.TryDequeue(out _)) { }
            }
        }

        /// <summary>
        /// Number of pending entries.
        /// </summary>
        public long PendingCount => _head - _tail;

        /// <summary>
        /// Get statistics.
        /// </summary>
        public (long enqueued, long dropped, long processed) GetStats()
        {
            return (_totalEnqueued, _totalDropped, _totalProcessed);
        }

        /// <summary>
        /// Get the confidence evaluator for source reliability management.
        /// </summary>
        public ConfidenceEvaluator ConfidenceEvaluator => _confidenceEvaluator;
    }

    /// <summary>
    /// The kind of information in Ring2.
    /// </summary>
    public enum InfoKind : byte
    {
        /// <summary>Player input (keyboard, mouse).</summary>
        Input = 0,

        /// <summary>Observation of current state (memory read).</summary>
        Observation = 1,

        /// <summary>Event that occurred (combat hit, item pickup).</summary>
        Event = 2,

        /// <summary>Proposal for state change.</summary>
        Proposal = 3,

        /// <summary>Prediction of future state.</summary>
        Prediction = 4,

        /// <summary>Request for more information.</summary>
        Query = 5,

        /// <summary>Correction from authority.</summary>
        Correction = 6
    }

    /// <summary>
    /// Processing status of an info entry.
    /// </summary>
    public enum InfoStatus : byte
    {
        /// <summary>Not yet processed.</summary>
        Pending = 0,

        /// <summary>Accepted into Ring3.</summary>
        Accepted = 1,

        /// <summary>Rejected by authority.</summary>
        Rejected = 2,

        /// <summary>Deferred for later processing.</summary>
        Deferred = 3,

        /// <summary>Superseded by newer information.</summary>
        Superseded = 4,

        /// <summary>Expired (too old to process).</summary>
        Expired = 5
    }

    /// <summary>
    /// An entry in the Info Ring.
    /// Represents an observation/proposal that has not yet been committed to truth.
    /// </summary>
    public struct InfoEntry
    {
        /// <summary>
        /// Unique ID for this entry.
        /// </summary>
        public long Id;

        /// <summary>
        /// What entity this information is about.
        /// </summary>
        public NetId SubjectId;

        /// <summary>
        /// Where this information came from.
        /// </summary>
        public NetId SourceId;

        /// <summary>
        /// What kind of information this is.
        /// </summary>
        public InfoKind Kind;

        /// <summary>
        /// The schema type of the payload.
        /// </summary>
        public SchemaId SchemaId;

        /// <summary>
        /// The typed payload (must be interpretable without memory context).
        /// </summary>
        public SchemaPayload Payload;

        /// <summary>
        /// When this was observed (tick).
        /// </summary>
        public long ObservationTick;

        /// <summary>
        /// When this was received/enqueued (tick).
        /// </summary>
        public long ReceiveTick;

        /// <summary>
        /// Confidence score for this observation.
        /// </summary>
        public Confidence Confidence;

        /// <summary>
        /// Hash of the payload for deduplication.
        /// </summary>
        public int PayloadHash;

        /// <summary>
        /// Current processing status.
        /// </summary>
        public InfoStatus Status;

        /// <summary>
        /// Latency from observation to receipt (in ticks).
        /// </summary>
        public long Latency => ReceiveTick - ObservationTick;

        /// <summary>
        /// Is this entry still pending?
        /// </summary>
        public bool IsPending => Status == InfoStatus.Pending;

        /// <summary>
        /// Answer: When is this true?
        /// </summary>
        public string WhenIsThisTrue => $"Tick {ObservationTick}";

        /// <summary>
        /// Answer: How sure are we?
        /// </summary>
        public string HowSureAreWe => Confidence.ToString();

        public override string ToString()
        {
            return $"Info({Id}: {Kind} {SubjectId.ToShortString()} {SchemaId.Kind}@T{ObservationTick} {Status} {Confidence.Effective:F2})";
        }
    }

    /// <summary>
    /// Filter for querying info entries.
    /// </summary>
    public class InfoFilter
    {
        public NetId? SubjectId { get; set; }
        public NetId? SourceId { get; set; }
        public InfoKind? Kind { get; set; }
        public SchemaKind? SchemaKind { get; set; }
        public long? MinTick { get; set; }
        public long? MaxTick { get; set; }
        public InfoStatus? Status { get; set; }
        public float? MinConfidence { get; set; }

        public bool Matches(InfoEntry entry)
        {
            if (SubjectId.HasValue && entry.SubjectId != SubjectId.Value) return false;
            if (SourceId.HasValue && entry.SourceId != SourceId.Value) return false;
            if (Kind.HasValue && entry.Kind != Kind.Value) return false;
            if (SchemaKind.HasValue && entry.SchemaId.Kind != SchemaKind.Value) return false;
            if (MinTick.HasValue && entry.ObservationTick < MinTick.Value) return false;
            if (MaxTick.HasValue && entry.ObservationTick > MaxTick.Value) return false;
            if (Status.HasValue && entry.Status != Status.Value) return false;
            if (MinConfidence.HasValue && entry.Confidence.Effective < MinConfidence.Value) return false;
            return true;
        }
    }
}

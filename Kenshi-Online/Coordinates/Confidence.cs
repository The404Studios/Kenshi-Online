using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Confidence - The Truth vs Suspicion Dimension
    ///
    /// This is the secret sauce for memory-based systems.
    ///
    /// Add a score per observation/proposal:
    ///   - confidence (0..1 or z-score-ish)
    ///   - source reliability
    ///   - staleness
    ///
    /// Then Ring3 uses it to decide:
    ///   - accept
    ///   - reject
    ///   - defer
    ///   - request more samples
    ///   - override with snap
    ///
    /// This is how you avoid committing noisy hallucinations.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct Confidence : IEquatable<Confidence>, IComparable<Confidence>
    {
        /// <summary>
        /// Core confidence value [0.0, 1.0].
        /// 0 = no confidence (reject/ignore)
        /// 0.5 = uncertain (defer/request more samples)
        /// 1.0 = full confidence (accept immediately)
        /// </summary>
        public readonly float Value;

        /// <summary>
        /// Source reliability score [0.0, 1.0].
        /// Based on historical accuracy of this source.
        /// </summary>
        public readonly float SourceReliability;

        /// <summary>
        /// Staleness factor [0.0, 1.0].
        /// 1.0 = fresh, decays toward 0 as data ages.
        /// </summary>
        public readonly float Freshness;

        /// <summary>
        /// Number of corroborating samples.
        /// Multiple samples agreeing increases confidence.
        /// </summary>
        public readonly byte SampleCount;

        /// <summary>
        /// Additional flags for special conditions.
        /// </summary>
        public readonly ConfidenceFlags Flags;

        /// <summary>
        /// High confidence - accept immediately.
        /// </summary>
        public static readonly Confidence High = new Confidence(0.95f, 1.0f, 1.0f, 1);

        /// <summary>
        /// Medium confidence - may need verification.
        /// </summary>
        public static readonly Confidence Medium = new Confidence(0.7f, 0.8f, 1.0f, 1);

        /// <summary>
        /// Low confidence - should defer or request more samples.
        /// </summary>
        public static readonly Confidence Low = new Confidence(0.3f, 0.5f, 1.0f, 1);

        /// <summary>
        /// Zero confidence - reject.
        /// </summary>
        public static readonly Confidence Zero = new Confidence(0f, 0f, 0f, 0);

        public Confidence(float value, float sourceReliability = 1f, float freshness = 1f, byte sampleCount = 1, ConfidenceFlags flags = ConfidenceFlags.None)
        {
            Value = Math.Clamp(value, 0f, 1f);
            SourceReliability = Math.Clamp(sourceReliability, 0f, 1f);
            Freshness = Math.Clamp(freshness, 0f, 1f);
            SampleCount = sampleCount;
            Flags = flags;
        }

        /// <summary>
        /// Compute effective confidence by combining all factors.
        /// </summary>
        public float Effective => Value * SourceReliability * Freshness * (SampleCount > 0 ? 1f : 0f);

        /// <summary>
        /// Should this observation be accepted?
        /// </summary>
        public ConfidenceDecision Decide(float acceptThreshold = 0.8f, float rejectThreshold = 0.2f)
        {
            float eff = Effective;

            if (Flags.HasFlag(ConfidenceFlags.ForcedAccept))
                return ConfidenceDecision.Accept;
            if (Flags.HasFlag(ConfidenceFlags.ForcedReject))
                return ConfidenceDecision.Reject;

            if (eff >= acceptThreshold)
                return ConfidenceDecision.Accept;
            if (eff <= rejectThreshold)
                return ConfidenceDecision.Reject;
            if (SampleCount < 3)
                return ConfidenceDecision.RequestMoreSamples;

            return ConfidenceDecision.Defer;
        }

        /// <summary>
        /// Create a new confidence with decayed freshness.
        /// </summary>
        public Confidence WithAge(int ticksOld, float halfLifeTicks = 20f)
        {
            float decay = MathF.Exp(-0.693f * ticksOld / halfLifeTicks); // ln(2) / halfLife
            return new Confidence(Value, SourceReliability, Freshness * decay, SampleCount, Flags);
        }

        /// <summary>
        /// Combine with another observation (e.g., corroborating sample).
        /// </summary>
        public Confidence Combine(Confidence other)
        {
            // Weighted average of confidence values
            float combinedValue = (Value * SampleCount + other.Value * other.SampleCount) / (SampleCount + other.SampleCount);
            float combinedReliability = (SourceReliability + other.SourceReliability) / 2f;
            float combinedFreshness = Math.Max(Freshness, other.Freshness);
            byte combinedSamples = (byte)Math.Min(255, SampleCount + other.SampleCount);

            return new Confidence(combinedValue, combinedReliability, combinedFreshness, combinedSamples);
        }

        /// <summary>
        /// Adjust confidence based on observed accuracy after the fact.
        /// </summary>
        public Confidence WithFeedback(bool wasAccurate, float learningRate = 0.1f)
        {
            float adjustment = wasAccurate ? learningRate : -learningRate;
            return new Confidence(
                Math.Clamp(Value + adjustment, 0f, 1f),
                Math.Clamp(SourceReliability + adjustment, 0f, 1f),
                Freshness,
                SampleCount,
                Flags);
        }

        public bool Equals(Confidence other) =>
            Math.Abs(Value - other.Value) < 0.001f &&
            Math.Abs(SourceReliability - other.SourceReliability) < 0.001f;

        public override bool Equals(object? obj) => obj is Confidence other && Equals(other);
        public override int GetHashCode() => HashCode.Combine((int)(Value * 1000), (int)(SourceReliability * 1000));

        public int CompareTo(Confidence other) => Effective.CompareTo(other.Effective);

        public static bool operator ==(Confidence left, Confidence right) => left.Equals(right);
        public static bool operator !=(Confidence left, Confidence right) => !left.Equals(right);
        public static bool operator <(Confidence left, Confidence right) => left.Effective < right.Effective;
        public static bool operator >(Confidence left, Confidence right) => left.Effective > right.Effective;

        public override string ToString()
        {
            return $"Conf({Effective:F2} = {Value:F2}*{SourceReliability:F2}*{Freshness:F2} n={SampleCount})";
        }
    }

    [Flags]
    public enum ConfidenceFlags : byte
    {
        None = 0,

        /// <summary>Override normal decision logic and accept.</summary>
        ForcedAccept = 1 << 0,

        /// <summary>Override normal decision logic and reject.</summary>
        ForcedReject = 1 << 1,

        /// <summary>This observation is from a trusted/verified source.</summary>
        Verified = 1 << 2,

        /// <summary>This observation contradicts previous data.</summary>
        Contradictory = 1 << 3,

        /// <summary>This observation is flagged as suspicious.</summary>
        Suspicious = 1 << 4,

        /// <summary>This observation was extrapolated/predicted, not directly observed.</summary>
        Predicted = 1 << 5
    }

    public enum ConfidenceDecision
    {
        /// <summary>Accept this observation as truth.</summary>
        Accept,

        /// <summary>Reject this observation.</summary>
        Reject,

        /// <summary>Defer decision until more information available.</summary>
        Defer,

        /// <summary>Request additional samples to increase confidence.</summary>
        RequestMoreSamples,

        /// <summary>Override with authoritative snap (hard correction).</summary>
        OverrideSnap
    }

    /// <summary>
    /// Tracks source reliability over time.
    /// Learns from historical accuracy of each source.
    /// </summary>
    public class SourceReliabilityTracker
    {
        private readonly ConcurrentDictionary<NetId, SourceStats> _sourceStats = new();

        private class SourceStats
        {
            public int TotalObservations;
            public int AccurateObservations;
            public float ReliabilityScore;
            public long LastObservationTick;

            public SourceStats()
            {
                ReliabilityScore = 0.5f; // Start neutral
            }
        }

        /// <summary>
        /// Get the reliability score for a source.
        /// </summary>
        public float GetReliability(NetId sourceId)
        {
            if (_sourceStats.TryGetValue(sourceId, out var stats))
                return stats.ReliabilityScore;
            return 0.5f; // Unknown source starts neutral
        }

        /// <summary>
        /// Record an observation from a source.
        /// </summary>
        public void RecordObservation(NetId sourceId, long tick)
        {
            var stats = _sourceStats.GetOrAdd(sourceId, _ => new SourceStats());
            stats.TotalObservations++;
            stats.LastObservationTick = tick;
        }

        /// <summary>
        /// Record feedback about whether an observation was accurate.
        /// </summary>
        public void RecordFeedback(NetId sourceId, bool wasAccurate, float learningRate = 0.05f)
        {
            var stats = _sourceStats.GetOrAdd(sourceId, _ => new SourceStats());

            if (wasAccurate)
                stats.AccurateObservations++;

            // Exponential moving average of accuracy
            float accuracy = wasAccurate ? 1f : 0f;
            stats.ReliabilityScore = stats.ReliabilityScore * (1 - learningRate) + accuracy * learningRate;
            stats.ReliabilityScore = Math.Clamp(stats.ReliabilityScore, 0.01f, 0.99f);
        }

        /// <summary>
        /// Get statistics for a source.
        /// </summary>
        public (int total, int accurate, float reliability) GetStats(NetId sourceId)
        {
            if (_sourceStats.TryGetValue(sourceId, out var stats))
                return (stats.TotalObservations, stats.AccurateObservations, stats.ReliabilityScore);
            return (0, 0, 0.5f);
        }

        /// <summary>
        /// Decay reliability scores for inactive sources.
        /// </summary>
        public void DecayInactiveSources(long currentTick, long inactivityThreshold = 1000)
        {
            foreach (var kvp in _sourceStats)
            {
                if (currentTick - kvp.Value.LastObservationTick > inactivityThreshold)
                {
                    // Slowly decay toward neutral
                    kvp.Value.ReliabilityScore = kvp.Value.ReliabilityScore * 0.99f + 0.5f * 0.01f;
                }
            }
        }
    }

    /// <summary>
    /// Evaluates confidence for incoming observations.
    /// </summary>
    public class ConfidenceEvaluator
    {
        private readonly SourceReliabilityTracker _reliabilityTracker = new();

        /// <summary>
        /// Evaluate confidence for an observation.
        /// </summary>
        public Confidence Evaluate(
            NetId sourceId,
            long observationTick,
            long currentTick,
            float baseConfidence = 0.8f,
            ConfidenceFlags flags = ConfidenceFlags.None)
        {
            float sourceReliability = _reliabilityTracker.GetReliability(sourceId);

            // Calculate freshness based on age
            long age = currentTick - observationTick;
            float freshness = MathF.Exp(-0.1f * age); // Decay over ~10 ticks

            // Record this observation
            _reliabilityTracker.RecordObservation(sourceId, currentTick);

            return new Confidence(baseConfidence, sourceReliability, freshness, 1, flags);
        }

        /// <summary>
        /// Provide feedback after observation is verified.
        /// </summary>
        public void ProvideFeedback(NetId sourceId, bool wasAccurate)
        {
            _reliabilityTracker.RecordFeedback(sourceId, wasAccurate);
        }

        /// <summary>
        /// Get the reliability tracker for direct access.
        /// </summary>
        public SourceReliabilityTracker ReliabilityTracker => _reliabilityTracker;
    }

    /// <summary>
    /// Combines multiple observations into a consensus decision.
    /// </summary>
    public class ConsensusBuilder
    {
        private readonly List<(SchemaPayload payload, Confidence confidence, NetId source)> _observations = new();

        public void AddObservation(SchemaPayload payload, Confidence confidence, NetId source)
        {
            _observations.Add((payload, confidence, source));
        }

        public void Clear() => _observations.Clear();

        public int Count => _observations.Count;

        /// <summary>
        /// Build consensus from collected observations.
        /// Returns the best payload and combined confidence.
        /// </summary>
        public (SchemaPayload? payload, Confidence confidence, ConfidenceDecision decision) BuildConsensus()
        {
            if (_observations.Count == 0)
                return (null, Confidence.Zero, ConfidenceDecision.Reject);

            if (_observations.Count == 1)
            {
                var single = _observations[0];
                return (single.payload, single.confidence, single.confidence.Decide());
            }

            // Group by payload hash to find agreeing observations
            var groups = new Dictionary<int, List<(SchemaPayload payload, Confidence confidence)>>();
            foreach (var obs in _observations)
            {
                int hash = obs.payload.ComputeHash();
                if (!groups.ContainsKey(hash))
                    groups[hash] = new List<(SchemaPayload, Confidence)>();
                groups[hash].Add((obs.payload, obs.confidence));
            }

            // Find the group with highest combined confidence
            SchemaPayload? bestPayload = null;
            Confidence bestConfidence = Confidence.Zero;

            foreach (var group in groups.Values)
            {
                // Combine confidences in this group
                Confidence combined = group[0].confidence;
                for (int i = 1; i < group.Count; i++)
                {
                    combined = combined.Combine(group[i].confidence);
                }

                if (combined.Effective > bestConfidence.Effective)
                {
                    bestConfidence = combined;
                    bestPayload = group[0].payload;
                }
            }

            return (bestPayload, bestConfidence, bestConfidence.Decide());
        }

        /// <summary>
        /// Check if observations are contradictory.
        /// </summary>
        public bool HasContradiction()
        {
            if (_observations.Count < 2) return false;

            var hashes = new HashSet<int>();
            foreach (var obs in _observations)
            {
                hashes.Add(obs.payload.ComputeHash());
            }
            return hashes.Count > 1;
        }
    }
}

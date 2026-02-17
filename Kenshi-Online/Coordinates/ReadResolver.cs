using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Read Virtualization - The Semantic Choke Point
    ///
    /// The truth: CPU will ALWAYS read memory. What we can control is WHERE
    /// the read happens and WHAT gets returned. We don't intercept omnisciently;
    /// we precondition subsystem inputs before they act.
    ///
    /// This is Read Virtualization at semantic choke points:
    ///   - Physics calls ResolvePhysicsTransforms() before stepping
    ///   - Renderer calls ResolveRenderTransforms() before drawing
    ///   - AI calls ResolveTargetSets() before deciding
    ///   - Animation calls ResolveAnimStates() before blending
    ///
    /// Each category has its own staleness budget and confidence threshold.
    /// "Closest match" without proper constraints is a silent corruption generator.
    /// </summary>

    #region Request/Response Tuples

    /// <summary>
    /// Request tuple - the minimum dimensions for any read request.
    /// Every request must specify WHAT it wants and WHY.
    /// </summary>
    public readonly struct ReadRequest
    {
        /// <summary>When do you need this data relative to?</summary>
        public readonly long Tick;

        /// <summary>What entity (or use QueryKey for batch queries)?</summary>
        public readonly NetId Subject;

        /// <summary>What data schema are you requesting?</summary>
        public readonly SchemaId SchemaId;

        /// <summary>What reference frame do you need?</summary>
        public readonly FrameType Frame;

        /// <summary>How urgent is this request?</summary>
        public readonly RequestUrgency Urgency;

        /// <summary>Why are you requesting this? (Determines staleness budget)</summary>
        public readonly RequestReason Reason;

        public ReadRequest(
            long tick,
            NetId subject,
            SchemaId schemaId,
            FrameType frame = FrameType.World,
            RequestUrgency urgency = RequestUrgency.CanLag,
            RequestReason reason = RequestReason.Generic)
        {
            Tick = tick;
            Subject = subject;
            SchemaId = schemaId;
            Frame = frame;
            Urgency = urgency;
            Reason = reason;
        }

        /// <summary>Create a physics request (tiny staleness, must converge).</summary>
        public static ReadRequest ForPhysics(long tick, NetId subject, SchemaId schemaId)
            => new ReadRequest(tick, subject, schemaId, FrameType.World, RequestUrgency.MustNow, RequestReason.PhysicsStep);

        /// <summary>Create a render request (moderate staleness, never block).</summary>
        public static ReadRequest ForRender(long tick, NetId subject, SchemaId schemaId)
            => new ReadRequest(tick, subject, schemaId, FrameType.World, RequestUrgency.CanLag, RequestReason.Render);

        /// <summary>Create an AI request (small staleness, strict confidence).</summary>
        public static ReadRequest ForAI(long tick, NetId subject, SchemaId schemaId)
            => new ReadRequest(tick, subject, schemaId, FrameType.World, RequestUrgency.CanLag, RequestReason.AIDecision);

        /// <summary>Create an animation request (moderate staleness unless gameplay-linked).</summary>
        public static ReadRequest ForAnimation(long tick, NetId subject, SchemaId schemaId, bool gameplayLinked = false)
            => new ReadRequest(tick, subject, schemaId, FrameType.Local,
                gameplayLinked ? RequestUrgency.MustNow : RequestUrgency.CanLag,
                RequestReason.Animation);
    }

    /// <summary>
    /// Response tuple - what we return for every resolved request.
    /// Includes provenance so the consumer knows what they're getting.
    /// </summary>
    public readonly struct ReadResponse
    {
        /// <summary>Where did this data come from?</summary>
        public readonly ResponseSource Source;

        /// <summary>The actual value (or null if not found).</summary>
        public readonly object? Value;

        /// <summary>How confident are we in this value?</summary>
        public readonly float Confidence;

        /// <summary>How many ticks until this data is considered stale?</summary>
        public readonly int TtlTicks;

        /// <summary>The resolver's decision on this request.</summary>
        public readonly ResolveDecision Decision;

        /// <summary>If substituted, what was the original value?</summary>
        public readonly object? OriginalValue;

        /// <summary>Tick when this data was sourced.</summary>
        public readonly long SourceTick;

        /// <summary>Why was this decision made?</summary>
        public readonly string? Reason;

        public ReadResponse(
            ResponseSource source,
            object? value,
            float confidence,
            int ttlTicks,
            ResolveDecision decision,
            long sourceTick = 0,
            object? originalValue = null,
            string? reason = null)
        {
            Source = source;
            Value = value;
            Confidence = confidence;
            TtlTicks = ttlTicks;
            Decision = decision;
            SourceTick = sourceTick;
            OriginalValue = originalValue;
            Reason = reason;
        }

        public bool IsValid => Decision != ResolveDecision.Block && Value != null;

        public static ReadResponse NotFound(string reason = "Entity not found")
            => new ReadResponse(ResponseSource.None, null, 0f, 0, ResolveDecision.Block, reason: reason);

        public static ReadResponse Blocked(string reason)
            => new ReadResponse(ResponseSource.None, null, 0f, 0, ResolveDecision.Block, reason: reason);
    }

    public enum RequestUrgency : byte
    {
        /// <summary>Can tolerate some delay, interpolation OK.</summary>
        CanLag,
        /// <summary>Needs data now, will accept extrapolation.</summary>
        MustNow
    }

    public enum RequestReason : byte
    {
        Generic,
        PhysicsStep,    // Tiny staleness budget, soft converge
        Render,         // Moderate staleness, interpolate/extrapolate, never block
        AIDecision,     // Small staleness, strict confidence threshold
        Animation,      // Moderate unless gameplay-linked
        NetworkSync,    // Authority comparison
        Debug           // Diagnostic/logging
    }

    public enum ResponseSource : byte
    {
        None,
        /// <summary>Directly from Ring3 authority commit.</summary>
        AuthorityCommit,
        /// <summary>Interpolated/extrapolated from Ring4.</summary>
        Predicted,
        /// <summary>From resolved cache (still valid TTL).</summary>
        Cached,
        /// <summary>Default/fallback value (entity exists but no data).</summary>
        Default,
        /// <summary>Last known good value (stale but better than nothing).</summary>
        LastKnownGood
    }

    public enum ResolveDecision : byte
    {
        /// <summary>Return the value as-is.</summary>
        Allow,
        /// <summary>Return a corrected/clamped value.</summary>
        Substitute,
        /// <summary>Do not return a value (consumer must handle).</summary>
        Block
    }

    #endregion

    #region Staleness Budgets

    /// <summary>
    /// Staleness budget per request category.
    /// These are the maximum acceptable ages for data.
    /// </summary>
    public static class StalenessBudgets
    {
        /// <summary>
        /// Physics: tiny staleness budget (1-2 ticks).
        /// Physics must converge with authority or desync occurs.
        /// </summary>
        public static readonly StalenessBudget Physics = new StalenessBudget
        {
            MaxStaleTicks = 2,
            MinConfidence = 0.9f,
            AllowExtrapolation = true,
            MaxExtrapolationTicks = 3,
            RequireAuthority = true,
            OnStale = StaleBehavior.SoftConverge
        };

        /// <summary>
        /// Render: moderate staleness budget (5-10 ticks).
        /// Renderer should never block - interpolate, extrapolate, never block.
        /// </summary>
        public static readonly StalenessBudget Render = new StalenessBudget
        {
            MaxStaleTicks = 10,
            MinConfidence = 0.5f,
            AllowExtrapolation = true,
            MaxExtrapolationTicks = 20,
            RequireAuthority = false,
            OnStale = StaleBehavior.Extrapolate
        };

        /// <summary>
        /// AI: small staleness budget with strict confidence.
        /// AI making decisions on uncertain data causes visible mistakes.
        /// </summary>
        public static readonly StalenessBudget AI = new StalenessBudget
        {
            MaxStaleTicks = 5,
            MinConfidence = 0.8f,  // Strict!
            AllowExtrapolation = false,  // Don't guess for AI
            MaxExtrapolationTicks = 0,
            RequireAuthority = true,
            OnStale = StaleBehavior.ReturnNone  // Let AI handle "I don't know"
        };

        /// <summary>
        /// Animation: moderate staleness unless gameplay-linked.
        /// Cosmetic animations can lag; gameplay animations need accuracy.
        /// </summary>
        public static readonly StalenessBudget Animation = new StalenessBudget
        {
            MaxStaleTicks = 8,
            MinConfidence = 0.6f,
            AllowExtrapolation = true,
            MaxExtrapolationTicks = 15,
            RequireAuthority = false,
            OnStale = StaleBehavior.Extrapolate
        };

        /// <summary>
        /// Animation (gameplay-linked): stricter than cosmetic.
        /// </summary>
        public static readonly StalenessBudget AnimationGameplay = new StalenessBudget
        {
            MaxStaleTicks = 3,
            MinConfidence = 0.85f,
            AllowExtrapolation = true,
            MaxExtrapolationTicks = 5,
            RequireAuthority = true,
            OnStale = StaleBehavior.SoftConverge
        };

        public static StalenessBudget ForReason(RequestReason reason, bool gameplayLinked = false)
        {
            return reason switch
            {
                RequestReason.PhysicsStep => Physics,
                RequestReason.Render => Render,
                RequestReason.AIDecision => AI,
                RequestReason.Animation => gameplayLinked ? AnimationGameplay : Animation,
                RequestReason.NetworkSync => Physics,  // Network sync is like physics
                RequestReason.Debug => Render,  // Debug can be lenient
                _ => Render  // Default to render budget (most permissive)
            };
        }
    }

    public readonly struct StalenessBudget
    {
        public readonly int MaxStaleTicks;
        public readonly float MinConfidence;
        public readonly bool AllowExtrapolation;
        public readonly int MaxExtrapolationTicks;
        public readonly bool RequireAuthority;
        public readonly StaleBehavior OnStale;

        public StalenessBudget(
            int maxStaleTicks,
            float minConfidence,
            bool allowExtrapolation,
            int maxExtrapolationTicks,
            bool requireAuthority,
            StaleBehavior onStale)
        {
            MaxStaleTicks = maxStaleTicks;
            MinConfidence = minConfidence;
            AllowExtrapolation = allowExtrapolation;
            MaxExtrapolationTicks = maxExtrapolationTicks;
            RequireAuthority = requireAuthority;
            OnStale = onStale;
        }
    }

    public enum StaleBehavior : byte
    {
        /// <summary>Return nothing, let consumer handle.</summary>
        ReturnNone,
        /// <summary>Return last known good value.</summary>
        ReturnLastKnown,
        /// <summary>Extrapolate from last known.</summary>
        Extrapolate,
        /// <summary>Soft converge toward authority.</summary>
        SoftConverge
    }

    #endregion

    #region Resolved Cache

    /// <summary>
    /// Cache for resolved values with TTL and confidence tracking.
    /// Prevents re-resolving the same data multiple times per tick.
    /// </summary>
    public class ResolvedCache
    {
        private readonly ConcurrentDictionary<CacheKey, CacheEntry> _cache = new();
        private readonly int _defaultTtlTicks;

        public ResolvedCache(int defaultTtlTicks = 2)
        {
            _defaultTtlTicks = defaultTtlTicks;
        }

        public bool TryGet(NetId subject, SchemaId schemaId, long currentTick, out CacheEntry entry)
        {
            var key = new CacheKey(subject, schemaId);
            if (_cache.TryGetValue(key, out entry))
            {
                // Check if still valid
                if (currentTick <= entry.ExpiryTick && entry.Confidence >= 0.5f)
                {
                    return true;
                }
                // Expired or low confidence, remove
                _cache.TryRemove(key, out _);
            }
            entry = default;
            return false;
        }

        public void Store(NetId subject, SchemaId schemaId, object value, long sourceTick, long currentTick, float confidence, int? ttlTicks = null)
        {
            var key = new CacheKey(subject, schemaId);
            var entry = new CacheEntry
            {
                Value = value,
                SourceTick = sourceTick,
                ExpiryTick = currentTick + (ttlTicks ?? _defaultTtlTicks),
                Confidence = confidence
            };
            _cache[key] = entry;
        }

        public void Invalidate(NetId subject)
        {
            // Remove all entries for this subject
            var keysToRemove = new List<CacheKey>();
            foreach (var kvp in _cache)
            {
                if (kvp.Key.Subject.Equals(subject))
                {
                    keysToRemove.Add(kvp.Key);
                }
            }
            foreach (var key in keysToRemove)
            {
                _cache.TryRemove(key, out _);
            }
        }

        public void InvalidateAll()
        {
            _cache.Clear();
        }

        /// <summary>Prune expired entries (call periodically).</summary>
        public int Prune(long currentTick)
        {
            var keysToRemove = new List<CacheKey>();
            foreach (var kvp in _cache)
            {
                if (currentTick > kvp.Value.ExpiryTick)
                {
                    keysToRemove.Add(kvp.Key);
                }
            }
            foreach (var key in keysToRemove)
            {
                _cache.TryRemove(key, out _);
            }
            return keysToRemove.Count;
        }

        private readonly struct CacheKey : IEquatable<CacheKey>
        {
            public readonly NetId Subject;
            public readonly SchemaId SchemaId;

            public CacheKey(NetId subject, SchemaId schemaId)
            {
                Subject = subject;
                SchemaId = schemaId;
            }

            public bool Equals(CacheKey other) => Subject.Equals(other.Subject) && SchemaId.Equals(other.SchemaId);
            public override bool Equals(object? obj) => obj is CacheKey other && Equals(other);
            public override int GetHashCode() => HashCode.Combine(Subject, SchemaId);
        }

        public struct CacheEntry
        {
            public object Value;
            public long SourceTick;
            public long ExpiryTick;
            public float Confidence;
        }
    }

    #endregion

    #region Category-Specific Resolvers

    /// <summary>
    /// The semantic read resolver. Preconditions subsystem inputs instead of
    /// attempting omniscient memory interception.
    /// </summary>
    public class SemanticResolver
    {
        private readonly AuthorityRing _authorityRing;
        private readonly AttributeRing _attributeRing;
        private readonly TickClock _clock;
        private readonly ResolvedCache _cache;

        // Statistics
        private long _requestsTotal;
        private long _requestsAllowed;
        private long _requestsSubstituted;
        private long _requestsBlocked;
        private long _cacheHits;

        public SemanticResolver(
            AuthorityRing authorityRing,
            AttributeRing attributeRing,
            TickClock clock,
            ResolvedCache? cache = null)
        {
            _authorityRing = authorityRing;
            _attributeRing = attributeRing;
            _clock = clock;
            _cache = cache ?? new ResolvedCache();
        }

        /// <summary>
        /// Resolve a single request with full dimensional checking.
        /// </summary>
        public ReadResponse Resolve(ReadRequest request)
        {
            _requestsTotal++;
            var currentTick = _clock.CurrentTick;
            var budget = StalenessBudgets.ForReason(request.Reason);

            // 1. Check cache first (avoid re-resolving)
            if (_cache.TryGet(request.Subject, request.SchemaId, currentTick, out var cached))
            {
                // Validate cached data meets this request's requirements
                if (cached.Confidence >= budget.MinConfidence)
                {
                    _cacheHits++;
                    _requestsAllowed++;
                    return new ReadResponse(
                        ResponseSource.Cached,
                        cached.Value,
                        cached.Confidence,
                        (int)(cached.ExpiryTick - currentTick),
                        ResolveDecision.Allow,
                        cached.SourceTick);
                }
            }

            // 2. Try authority source (Ring3)
            var authorityResult = TryResolveFromAuthority(request, budget, currentTick);
            if (authorityResult.HasValue)
            {
                CacheAndTrack(request, authorityResult.Value);
                return authorityResult.Value;
            }

            // 3. Try presentation/prediction (Ring4)
            var presentationResult = TryResolveFromPresentation(request, budget, currentTick);
            if (presentationResult.HasValue)
            {
                CacheAndTrack(request, presentationResult.Value);
                return presentationResult.Value;
            }

            // 4. Handle stale data based on budget policy
            return HandleStaleBehavior(request, budget, currentTick);
        }

        private ReadResponse? TryResolveFromAuthority(ReadRequest request, StalenessBudget budget, long currentTick)
        {
            var state = _authorityRing.GetEntityState(request.Subject);
            if (state == null) return null;

            // Check staleness
            var staleness = currentTick - state.LastTick;
            if (staleness > budget.MaxStaleTicks)
            {
                // Data is too stale for this request type
                return null;
            }

            // Get value based on schema
            var (value, confidence) = ExtractValueForSchema(state, request.SchemaId);
            if (value == null) return null;

            // Confidence check
            if (confidence < budget.MinConfidence)
            {
                // Confidence too low for this request type
                if (budget.RequireAuthority)
                {
                    _requestsBlocked++;
                    return new ReadResponse(
                        ResponseSource.AuthorityCommit,
                        null,
                        confidence,
                        0,
                        ResolveDecision.Block,
                        state.LastTick,
                        reason: $"Confidence {confidence:F2} below threshold {budget.MinConfidence:F2}");
                }
                return null; // Try other sources
            }

            _requestsAllowed++;
            return new ReadResponse(
                ResponseSource.AuthorityCommit,
                value,
                confidence,
                budget.MaxStaleTicks - (int)staleness,
                ResolveDecision.Allow,
                state.LastTick);
        }

        private ReadResponse? TryResolveFromPresentation(ReadRequest request, StalenessBudget budget, long currentTick)
        {
            var presentation = _attributeRing.GetPresentationState(request.Subject, _clock.Now.ContinuousTime);
            if (presentation == null) return null;

            // Check if presentation data is acceptable for this request type
            var staleness = currentTick - presentation.SourceTick;

            // For physics, presentation isn't authoritative enough
            if (budget.RequireAuthority && request.Reason == RequestReason.PhysicsStep)
            {
                return null;
            }

            // Check confidence
            if (presentation.Confidence < budget.MinConfidence)
            {
                return null;
            }

            // Check extrapolation limits
            if (presentation.Mode == SampleMode.Extrapolate)
            {
                if (!budget.AllowExtrapolation || staleness > budget.MaxExtrapolationTicks)
                {
                    return null;
                }
            }

            // Extract value based on schema
            object? value = request.SchemaId.Kind switch
            {
                SchemaKind.Transform => new TransformPayload
                {
                    Position = presentation.Position,
                    Rotation = presentation.Rotation,
                    Velocity = presentation.Velocity
                },
                _ => null
            };

            if (value == null) return null;

            _requestsAllowed++;
            var decision = presentation.Mode == SampleMode.Extrapolate
                ? ResolveDecision.Substitute
                : ResolveDecision.Allow;

            if (decision == ResolveDecision.Substitute)
                _requestsSubstituted++;

            return new ReadResponse(
                ResponseSource.Predicted,
                value,
                presentation.Confidence,
                budget.MaxStaleTicks,
                decision,
                presentation.SourceTick);
        }

        private ReadResponse HandleStaleBehavior(ReadRequest request, StalenessBudget budget, long currentTick)
        {
            switch (budget.OnStale)
            {
                case StaleBehavior.ReturnNone:
                    _requestsBlocked++;
                    return ReadResponse.NotFound($"No valid data for {request.Subject} (stale behavior: none)");

                case StaleBehavior.ReturnLastKnown:
                    var lastKnown = TryGetLastKnown(request);
                    if (lastKnown.HasValue)
                    {
                        _requestsSubstituted++;
                        return new ReadResponse(
                            ResponseSource.LastKnownGood,
                            lastKnown.Value.value,
                            0.3f, // Low confidence for last-known
                            1,    // Very short TTL
                            ResolveDecision.Substitute,
                            lastKnown.Value.tick);
                    }
                    _requestsBlocked++;
                    return ReadResponse.NotFound("No last-known value available");

                case StaleBehavior.Extrapolate:
                    var extrapolated = TryExtrapolate(request, budget, currentTick);
                    if (extrapolated.HasValue)
                    {
                        _requestsSubstituted++;
                        return extrapolated.Value;
                    }
                    _requestsBlocked++;
                    return ReadResponse.NotFound("Cannot extrapolate");

                case StaleBehavior.SoftConverge:
                    // For physics/sync: return authority even if stale, let physics converge
                    var staleAuth = _authorityRing.GetEntityState(request.Subject);
                    if (staleAuth != null)
                    {
                        var (val, conf) = ExtractValueForSchema(staleAuth, request.SchemaId);
                        if (val != null)
                        {
                            _requestsSubstituted++;
                            return new ReadResponse(
                                ResponseSource.AuthorityCommit,
                                val,
                                conf * 0.5f, // Halve confidence for stale data
                                1,
                                ResolveDecision.Substitute,
                                staleAuth.LastTick,
                                reason: "Soft converging toward stale authority");
                        }
                    }
                    _requestsBlocked++;
                    return ReadResponse.NotFound("No data for soft convergence");

                default:
                    _requestsBlocked++;
                    return ReadResponse.NotFound("Unknown stale behavior");
            }
        }

        private (object? value, float confidence) ExtractValueForSchema(AuthorityEntityState state, SchemaId schemaId)
        {
            return schemaId.Kind switch
            {
                SchemaKind.Transform when state.Transform != null => (state.Transform, 1.0f),
                SchemaKind.Health when state.Health != null => (state.Health, 1.0f),
                SchemaKind.Inventory when state.Inventory != null => (state.Inventory, 1.0f),
                SchemaKind.AIState when state.AIState != null => (state.AIState, 1.0f),
                _ => (null, 0f)
            };
        }

        private (object value, long tick)? TryGetLastKnown(ReadRequest request)
        {
            // Check cache for any value (even expired)
            // This is intentionally lenient - last-known is a fallback
            var state = _authorityRing.GetEntityState(request.Subject);
            if (state != null)
            {
                var (val, _) = ExtractValueForSchema(state, request.SchemaId);
                if (val != null)
                    return (val, state.LastTick);
            }
            return null;
        }

        private ReadResponse? TryExtrapolate(ReadRequest request, StalenessBudget budget, long currentTick)
        {
            var state = _authorityRing.GetEntityState(request.Subject);
            if (state == null) return null;

            // Only extrapolate transforms
            if (request.SchemaId.Kind != SchemaKind.Transform || state.Transform == null)
                return null;

            var staleTicks = currentTick - state.LastTick;
            if (staleTicks > budget.MaxExtrapolationTicks)
                return null;

            // Dead reckon from last known position
            var lastTransform = state.Transform;
            var deltaTime = staleTicks * 0.016f; // Assume 60 tick/sec

            var (extrapolatedPos, confidence) = Interpolation.DeadReckonWithDecay(
                lastTransform.Position,
                lastTransform.Velocity,
                deltaTime,
                0.3f); // Decay rate

            var extrapolated = new TransformPayload
            {
                Position = extrapolatedPos,
                Rotation = lastTransform.Rotation,
                Velocity = lastTransform.Velocity
            };

            return new ReadResponse(
                ResponseSource.Predicted,
                extrapolated,
                confidence,
                1, // Short TTL for extrapolated
                ResolveDecision.Substitute,
                state.LastTick,
                lastTransform, // Original value for debugging
                "Extrapolated from dead reckoning");
        }

        private void CacheAndTrack(ReadRequest request, ReadResponse response)
        {
            if (response.Decision != ResolveDecision.Block && response.Value != null)
            {
                _cache.Store(
                    request.Subject,
                    request.SchemaId,
                    response.Value,
                    response.SourceTick,
                    _clock.CurrentTick,
                    response.Confidence,
                    response.TtlTicks);
            }
        }

        #region Batch Resolution Methods (Preconditioning)

        /// <summary>
        /// Resolve transforms for physics step.
        /// Returns only high-confidence, authoritative data.
        /// Missing entities should soft-converge or be skipped.
        /// </summary>
        public Dictionary<NetId, ReadResponse> ResolvePhysicsTransforms(IEnumerable<NetId> entities)
        {
            var results = new Dictionary<NetId, ReadResponse>();
            var schemaId = new SchemaId(SchemaKind.Transform, 1);

            foreach (var entityId in entities)
            {
                var request = ReadRequest.ForPhysics(_clock.CurrentTick, entityId, schemaId);
                results[entityId] = Resolve(request);
            }

            return results;
        }

        /// <summary>
        /// Resolve transforms for rendering.
        /// Never blocks - always returns something (interpolated, extrapolated, or default).
        /// </summary>
        public Dictionary<NetId, ReadResponse> ResolveRenderTransforms(IEnumerable<NetId> entities)
        {
            var results = new Dictionary<NetId, ReadResponse>();
            var schemaId = new SchemaId(SchemaKind.Transform, 1);

            foreach (var entityId in entities)
            {
                var request = ReadRequest.ForRender(_clock.CurrentTick, entityId, schemaId);
                var response = Resolve(request);

                // Render should never block - provide default if needed
                if (response.Decision == ResolveDecision.Block)
                {
                    response = new ReadResponse(
                        ResponseSource.Default,
                        new TransformPayload { Position = Vector3.Zero, Rotation = Quaternion.Identity },
                        0.1f,
                        1,
                        ResolveDecision.Substitute,
                        reason: "Default transform for blocked render");
                }

                results[entityId] = response;
            }

            return results;
        }

        /// <summary>
        /// Resolve target sets for AI decisions.
        /// Returns with confidence ordering - uncertain targets marked as such.
        /// Returns "none" if too uncertain rather than guessing.
        /// </summary>
        public AITargetResolution ResolveAITargets(NetId aiEntity, IEnumerable<NetId> potentialTargets)
        {
            var resolution = new AITargetResolution();
            var schemaId = new SchemaId(SchemaKind.Transform, 1);

            foreach (var targetId in potentialTargets)
            {
                var request = ReadRequest.ForAI(_clock.CurrentTick, targetId, schemaId);
                var response = Resolve(request);

                if (response.Decision == ResolveDecision.Allow)
                {
                    resolution.ConfidentTargets.Add((targetId, response.Confidence, response.Value));
                }
                else if (response.Decision == ResolveDecision.Substitute)
                {
                    resolution.UncertainTargets.Add((targetId, response.Confidence, response.Value, response.Reason ?? "substituted"));
                }
                // Blocked targets are not added - AI should treat as "unknown"
            }

            // Sort by confidence descending
            resolution.ConfidentTargets.Sort((a, b) => b.confidence.CompareTo(a.confidence));
            resolution.UncertainTargets.Sort((a, b) => b.confidence.CompareTo(a.confidence));

            return resolution;
        }

        /// <summary>
        /// Resolve animation states.
        /// Differentiates between cosmetic (can lag) and gameplay-linked (must be accurate).
        /// </summary>
        public Dictionary<NetId, ReadResponse> ResolveAnimationStates(
            IEnumerable<(NetId entity, bool gameplayLinked)> requests)
        {
            var results = new Dictionary<NetId, ReadResponse>();
            var schemaId = new SchemaId(SchemaKind.AnimState, 1);

            foreach (var (entityId, gameplayLinked) in requests)
            {
                var request = ReadRequest.ForAnimation(_clock.CurrentTick, entityId, schemaId, gameplayLinked);
                results[entityId] = Resolve(request);
            }

            return results;
        }

        #endregion

        #region Statistics

        public ResolverStats GetStats()
        {
            return new ResolverStats
            {
                TotalRequests = _requestsTotal,
                Allowed = _requestsAllowed,
                Substituted = _requestsSubstituted,
                Blocked = _requestsBlocked,
                CacheHits = _cacheHits,
                AllowRate = _requestsTotal > 0 ? _requestsAllowed / (float)_requestsTotal : 0,
                SubstituteRate = _requestsTotal > 0 ? _requestsSubstituted / (float)_requestsTotal : 0,
                CacheHitRate = _requestsTotal > 0 ? _cacheHits / (float)_requestsTotal : 0
            };
        }

        #endregion
    }

    public class AITargetResolution
    {
        /// <summary>Targets with high confidence, ordered by confidence descending.</summary>
        public List<(NetId targetId, float confidence, object? data)> ConfidentTargets { get; } = new();

        /// <summary>Targets with substituted/uncertain data.</summary>
        public List<(NetId targetId, float confidence, object? data, string reason)> UncertainTargets { get; } = new();

        /// <summary>Best target if available, null if no confident targets.</summary>
        public NetId? BestTarget => ConfidentTargets.Count > 0 ? ConfidentTargets[0].targetId : null;

        /// <summary>Whether the AI should wait for better data.</summary>
        public bool ShouldWait => ConfidentTargets.Count == 0 && UncertainTargets.Count > 0;
    }

    public struct ResolverStats
    {
        public long TotalRequests;
        public long Allowed;
        public long Substituted;
        public long Blocked;
        public long CacheHits;
        public float AllowRate;
        public float SubstituteRate;
        public float CacheHitRate;
    }

    #endregion

    #region Response Bus (Preconditioning)

    /// <summary>
    /// ResponseBus - Preconditions subsystem inputs before they act.
    ///
    /// Instead of intercepting memory reads (impossible), we:
    /// 1. Know what each subsystem needs
    /// 2. Resolve it all at once at the start of their update
    /// 3. Hand them a consistent snapshot
    ///
    /// This is the "gene expression" - translating authority into action.
    /// </summary>
    public class ResponseBus
    {
        private readonly SemanticResolver _resolver;
        private readonly TickClock _clock;

        // Pre-resolved data for each subsystem
        private Dictionary<NetId, ReadResponse>? _physicsSnapshot;
        private Dictionary<NetId, ReadResponse>? _renderSnapshot;
        private Dictionary<NetId, ReadResponse>? _animationSnapshot;
        private AITargetResolution? _aiSnapshot;

        private long _physicsSnapshotTick;
        private long _renderSnapshotTick;
        private long _animationSnapshotTick;
        private long _aiSnapshotTick;

        public ResponseBus(SemanticResolver resolver, TickClock clock)
        {
            _resolver = resolver;
            _clock = clock;
        }

        /// <summary>
        /// Precondition physics data before physics step.
        /// Call this BEFORE physics update runs.
        /// </summary>
        public Dictionary<NetId, ReadResponse> PreconditionPhysics(IEnumerable<NetId> entities)
        {
            _physicsSnapshot = _resolver.ResolvePhysicsTransforms(entities);
            _physicsSnapshotTick = _clock.CurrentTick;
            return _physicsSnapshot;
        }

        /// <summary>
        /// Precondition render data before render frame.
        /// Call this BEFORE rendering.
        /// </summary>
        public Dictionary<NetId, ReadResponse> PreconditionRender(IEnumerable<NetId> entities)
        {
            _renderSnapshot = _resolver.ResolveRenderTransforms(entities);
            _renderSnapshotTick = _clock.CurrentTick;
            return _renderSnapshot;
        }

        /// <summary>
        /// Precondition AI target data before AI tick.
        /// Call this BEFORE AI decision-making.
        /// </summary>
        public AITargetResolution PreconditionAI(NetId aiEntity, IEnumerable<NetId> potentialTargets)
        {
            _aiSnapshot = _resolver.ResolveAITargets(aiEntity, potentialTargets);
            _aiSnapshotTick = _clock.CurrentTick;
            return _aiSnapshot;
        }

        /// <summary>
        /// Precondition animation data before animation update.
        /// </summary>
        public Dictionary<NetId, ReadResponse> PreconditionAnimation(
            IEnumerable<(NetId entity, bool gameplayLinked)> requests)
        {
            _animationSnapshot = _resolver.ResolveAnimationStates(requests);
            _animationSnapshotTick = _clock.CurrentTick;
            return _animationSnapshot;
        }

        /// <summary>
        /// Get physics data (must call PreconditionPhysics first).
        /// </summary>
        public ReadResponse? GetPhysicsData(NetId entityId)
        {
            if (_physicsSnapshot == null || _physicsSnapshotTick != _clock.CurrentTick)
                return null;
            return _physicsSnapshot.TryGetValue(entityId, out var response) ? response : null;
        }

        /// <summary>
        /// Get render data (must call PreconditionRender first).
        /// </summary>
        public ReadResponse? GetRenderData(NetId entityId)
        {
            if (_renderSnapshot == null)
                return null;
            return _renderSnapshot.TryGetValue(entityId, out var response) ? response : null;
        }

        /// <summary>
        /// Get animation data (must call PreconditionAnimation first).
        /// </summary>
        public ReadResponse? GetAnimationData(NetId entityId)
        {
            if (_animationSnapshot == null)
                return null;
            return _animationSnapshot.TryGetValue(entityId, out var response) ? response : null;
        }

        /// <summary>
        /// Get AI target resolution (must call PreconditionAI first).
        /// </summary>
        public AITargetResolution? GetAITargets()
        {
            return _aiSnapshot;
        }

        /// <summary>
        /// Clear all snapshots (call at end of frame).
        /// </summary>
        public void ClearSnapshots()
        {
            _physicsSnapshot = null;
            _renderSnapshot = null;
            _animationSnapshot = null;
            _aiSnapshot = null;
        }
    }

    #endregion
}

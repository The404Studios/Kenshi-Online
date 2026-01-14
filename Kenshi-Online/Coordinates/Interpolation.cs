using System;
using System.Numerics;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Interpolation and extrapolation strategies for smooth presentation.
    ///
    /// The core problem:
    ///   - Authority (Ring3) operates in discrete tick-space
    ///   - Game renders at arbitrary frame times
    ///   - Network latency means authority arrives late
    ///
    /// Solution: multiple interpolation/extrapolation strategies that trade off
    /// smoothness, responsiveness, and accuracy.
    /// </summary>
    public static class Interpolation
    {
        #region Position Interpolation

        /// <summary>
        /// Linear interpolation between two positions.
        /// Simple but can look robotic.
        /// </summary>
        public static Vector3 Linear(Vector3 a, Vector3 b, float t)
        {
            return Vector3.Lerp(a, b, t);
        }

        /// <summary>
        /// Hermite spline interpolation using positions and velocities.
        /// Produces smooth curves that respect velocity at endpoints.
        /// </summary>
        public static Vector3 Hermite(
            Vector3 p0, Vector3 v0, // Start position and velocity
            Vector3 p1, Vector3 v1, // End position and velocity
            float t,
            float duration = 1f)
        {
            // Scale velocities by duration
            v0 *= duration;
            v1 *= duration;

            float t2 = t * t;
            float t3 = t2 * t;

            // Hermite basis functions
            float h00 = 2 * t3 - 3 * t2 + 1;
            float h10 = t3 - 2 * t2 + t;
            float h01 = -2 * t3 + 3 * t2;
            float h11 = t3 - t2;

            return h00 * p0 + h10 * v0 + h01 * p1 + h11 * v1;
        }

        /// <summary>
        /// Catmull-Rom spline through 4 points.
        /// Good for smooth paths through multiple samples.
        /// </summary>
        public static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
        {
            float t2 = t * t;
            float t3 = t2 * t;

            return 0.5f * (
                2 * p1 +
                (-p0 + p2) * t +
                (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                (-p0 + 3 * p1 - 3 * p2 + p3) * t3
            );
        }

        /// <summary>
        /// Smoothstep interpolation (ease in/out).
        /// </summary>
        public static Vector3 Smoothstep(Vector3 a, Vector3 b, float t)
        {
            t = t * t * (3f - 2f * t); // Smoothstep function
            return Vector3.Lerp(a, b, t);
        }

        #endregion

        #region Rotation Interpolation

        /// <summary>
        /// Spherical linear interpolation between quaternions.
        /// The standard for rotation interpolation.
        /// </summary>
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
        {
            return Quaternion.Slerp(a, b, t);
        }

        /// <summary>
        /// Normalized linear interpolation (faster than slerp, good enough for small angles).
        /// </summary>
        public static Quaternion Nlerp(Quaternion a, Quaternion b, float t)
        {
            // Ensure shortest path
            if (Quaternion.Dot(a, b) < 0)
                b = new Quaternion(-b.X, -b.Y, -b.Z, -b.W);

            var result = new Quaternion(
                a.X + (b.X - a.X) * t,
                a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t,
                a.W + (b.W - a.W) * t
            );

            return Quaternion.Normalize(result);
        }

        /// <summary>
        /// Squad - Spherical and Quadrangle interpolation.
        /// Smooth interpolation through multiple rotations.
        /// </summary>
        public static Quaternion Squad(Quaternion q0, Quaternion q1, Quaternion q2, Quaternion q3, float t)
        {
            var s1 = ComputeSquadIntermediate(q0, q1, q2);
            var s2 = ComputeSquadIntermediate(q1, q2, q3);
            return Quaternion.Slerp(
                Quaternion.Slerp(q1, q2, t),
                Quaternion.Slerp(s1, s2, t),
                2 * t * (1 - t)
            );
        }

        private static Quaternion ComputeSquadIntermediate(Quaternion q0, Quaternion q1, Quaternion q2)
        {
            var inv = Quaternion.Inverse(q1);
            var exp1 = QuaternionLog(inv * q0);
            var exp2 = QuaternionLog(inv * q2);
            var sum = new Quaternion(
                -(exp1.X + exp2.X) / 4,
                -(exp1.Y + exp2.Y) / 4,
                -(exp1.Z + exp2.Z) / 4,
                0
            );
            return q1 * QuaternionExp(sum);
        }

        private static Quaternion QuaternionLog(Quaternion q)
        {
            float a = MathF.Acos(q.W);
            float sina = MathF.Sin(a);
            if (MathF.Abs(sina) < 0.0001f)
                return new Quaternion(0, 0, 0, 0);
            float coeff = a / sina;
            return new Quaternion(q.X * coeff, q.Y * coeff, q.Z * coeff, 0);
        }

        private static Quaternion QuaternionExp(Quaternion q)
        {
            float a = MathF.Sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z);
            float sina = MathF.Sin(a);
            float cosa = MathF.Cos(a);
            if (MathF.Abs(a) < 0.0001f)
                return new Quaternion(0, 0, 0, 1);
            float coeff = sina / a;
            return new Quaternion(q.X * coeff, q.Y * coeff, q.Z * coeff, cosa);
        }

        #endregion

        #region Extrapolation (Dead Reckoning)

        /// <summary>
        /// Linear extrapolation using velocity.
        /// Simple dead reckoning.
        /// </summary>
        public static Vector3 LinearExtrapolate(Vector3 position, Vector3 velocity, float deltaTime)
        {
            return position + velocity * deltaTime;
        }

        /// <summary>
        /// Quadratic extrapolation using velocity and acceleration.
        /// Better for entities with changing velocity.
        /// </summary>
        public static Vector3 QuadraticExtrapolate(
            Vector3 position,
            Vector3 velocity,
            Vector3 acceleration,
            float deltaTime)
        {
            return position +
                   velocity * deltaTime +
                   0.5f * acceleration * deltaTime * deltaTime;
        }

        /// <summary>
        /// Extrapolate rotation using angular velocity.
        /// </summary>
        public static Quaternion ExtrapolateRotation(
            Quaternion rotation,
            Vector3 angularVelocity,
            float deltaTime)
        {
            if (angularVelocity.LengthSquared() < 0.0001f)
                return rotation;

            float angle = angularVelocity.Length() * deltaTime;
            var axis = Vector3.Normalize(angularVelocity);
            var delta = Quaternion.CreateFromAxisAngle(axis, angle);
            return Quaternion.Normalize(rotation * delta);
        }

        /// <summary>
        /// Dead reckoning with decay.
        /// Extrapolation confidence decays over time.
        /// </summary>
        public static (Vector3 position, float confidence) DeadReckonWithDecay(
            Vector3 position,
            Vector3 velocity,
            float deltaTime,
            float decayRate = 0.5f)
        {
            var extrapolated = LinearExtrapolate(position, velocity, deltaTime);
            float confidence = MathF.Exp(-decayRate * deltaTime);
            return (extrapolated, confidence);
        }

        #endregion

        #region Correction Blending

        /// <summary>
        /// Blend toward a correction over time.
        /// Snaps hard corrections smoothly into place.
        /// </summary>
        public static Vector3 BlendCorrection(
            Vector3 current,
            Vector3 target,
            float blendRate,
            float deltaTime)
        {
            float t = 1f - MathF.Pow(1f - blendRate, deltaTime * 60f); // Frame-rate independent
            return Vector3.Lerp(current, target, t);
        }

        /// <summary>
        /// Blend rotation correction.
        /// </summary>
        public static Quaternion BlendRotationCorrection(
            Quaternion current,
            Quaternion target,
            float blendRate,
            float deltaTime)
        {
            float t = 1f - MathF.Pow(1f - blendRate, deltaTime * 60f);
            return Quaternion.Slerp(current, target, t);
        }

        /// <summary>
        /// Snap correction if error is too large, otherwise blend.
        /// </summary>
        public static (Vector3 position, bool snapped) SnapOrBlend(
            Vector3 current,
            Vector3 target,
            float snapThreshold,
            float blendRate,
            float deltaTime)
        {
            float distance = Vector3.Distance(current, target);

            if (distance > snapThreshold)
            {
                return (target, true); // Snap
            }

            return (BlendCorrection(current, target, blendRate, deltaTime), false);
        }

        #endregion

        #region Jitter Buffer

        /// <summary>
        /// Calculate adaptive jitter buffer size based on recent latencies.
        /// </summary>
        public static float CalculateJitterBuffer(
            float[] recentLatencies,
            float targetPercentile = 0.95f,
            float minBuffer = 1f,
            float maxBuffer = 10f)
        {
            if (recentLatencies.Length == 0)
                return minBuffer;

            // Sort and get percentile
            var sorted = new float[recentLatencies.Length];
            Array.Copy(recentLatencies, sorted, recentLatencies.Length);
            Array.Sort(sorted);

            int index = (int)(sorted.Length * targetPercentile);
            index = Math.Clamp(index, 0, sorted.Length - 1);

            float buffer = sorted[index];
            return Math.Clamp(buffer, minBuffer, maxBuffer);
        }

        #endregion
    }

    /// <summary>
    /// Interpolation buffer for managing samples and producing smooth output.
    /// </summary>
    public class InterpolationBuffer
    {
        private readonly TransformSample[] _samples;
        private readonly int _capacity;
        private int _head;
        private int _count;

        // Adaptive interpolation delay
        private float _currentDelay;
        private readonly float[] _latencies;
        private int _latencyHead;

        public InterpolationBuffer(int capacity = 32, float initialDelay = 2f)
        {
            _capacity = capacity;
            _samples = new TransformSample[capacity];
            _latencies = new float[64];
            _currentDelay = initialDelay;
        }

        /// <summary>
        /// Add a new sample.
        /// </summary>
        public void AddSample(long tick, Vector3 position, Quaternion rotation, Vector3 velocity, float latency)
        {
            int index = _head;
            _samples[index] = new TransformSample
            {
                Tick = tick,
                Position = position,
                Rotation = rotation,
                Velocity = velocity
            };

            _head = (_head + 1) % _capacity;
            _count = Math.Min(_count + 1, _capacity);

            // Track latency for adaptive delay
            _latencies[_latencyHead] = latency;
            _latencyHead = (_latencyHead + 1) % _latencies.Length;

            // Update adaptive delay
            _currentDelay = Interpolation.CalculateJitterBuffer(_latencies, 0.9f, 1f, 10f);
        }

        /// <summary>
        /// Sample at a specific time with interpolation/extrapolation.
        /// </summary>
        public (Vector3 position, Quaternion rotation, Vector3 velocity, SampleMode mode) Sample(double time)
        {
            if (_count == 0)
                return (Vector3.Zero, Quaternion.Identity, Vector3.Zero, SampleMode.None);

            // Apply adaptive delay
            double targetTime = time - _currentDelay;

            // Find surrounding samples
            TransformSample? before = null;
            TransformSample? after = null;
            TransformSample? beforeBefore = null;
            TransformSample? afterAfter = null;

            for (int i = 0; i < _count; i++)
            {
                int idx = ((_head - 1 - i) % _capacity + _capacity) % _capacity;
                var sample = _samples[idx];

                if (sample.Tick <= targetTime)
                {
                    if (before == null)
                        before = sample;
                    else if (beforeBefore == null)
                        beforeBefore = sample;
                }
                else
                {
                    afterAfter = after;
                    after = sample;
                }
            }

            // Exact match
            if (before.HasValue && Math.Abs(before.Value.Tick - targetTime) < 0.001)
                return (before.Value.Position, before.Value.Rotation, before.Value.Velocity, SampleMode.Exact);

            // Interpolate with Hermite if we have velocities
            if (before.HasValue && after.HasValue && before.Value.Tick != after.Value.Tick)
            {
                float t = (float)((targetTime - before.Value.Tick) / (after.Value.Tick - before.Value.Tick));
                float duration = after.Value.Tick - before.Value.Tick;

                var position = Interpolation.Hermite(
                    before.Value.Position, before.Value.Velocity,
                    after.Value.Position, after.Value.Velocity,
                    t, duration);

                var rotation = Interpolation.Slerp(before.Value.Rotation, after.Value.Rotation, t);
                var velocity = Vector3.Lerp(before.Value.Velocity, after.Value.Velocity, t);

                return (position, rotation, velocity, SampleMode.Interpolate);
            }

            // Extrapolate
            if (before.HasValue)
            {
                float deltaTime = (float)(targetTime - before.Value.Tick);
                deltaTime = Math.Min(deltaTime, 10f); // Cap extrapolation

                var (position, confidence) = Interpolation.DeadReckonWithDecay(
                    before.Value.Position,
                    before.Value.Velocity,
                    deltaTime);

                return (position, before.Value.Rotation, before.Value.Velocity, SampleMode.Extrapolate);
            }

            return (Vector3.Zero, Quaternion.Identity, Vector3.Zero, SampleMode.None);
        }

        /// <summary>
        /// Current adaptive delay.
        /// </summary>
        public float CurrentDelay => _currentDelay;

        /// <summary>
        /// Number of samples in buffer.
        /// </summary>
        public int Count => _count;
    }

    /// <summary>
    /// Correction tracker for smooth error correction.
    /// </summary>
    public class CorrectionTracker
    {
        private Vector3 _positionError;
        private Quaternion _rotationError = Quaternion.Identity;
        private float _blendRate;
        private float _snapThreshold;

        public CorrectionTracker(float blendRate = 0.15f, float snapThreshold = 5f)
        {
            _blendRate = blendRate;
            _snapThreshold = snapThreshold;
        }

        /// <summary>
        /// Apply a correction from authority.
        /// </summary>
        public void ApplyCorrection(Vector3 authorityPosition, Vector3 currentPosition)
        {
            _positionError = authorityPosition - currentPosition;

            // Snap if error too large
            if (_positionError.Length() > _snapThreshold)
            {
                _positionError = Vector3.Zero; // Will snap next frame
            }
        }

        /// <summary>
        /// Apply rotation correction.
        /// </summary>
        public void ApplyRotationCorrection(Quaternion authorityRotation, Quaternion currentRotation)
        {
            _rotationError = Quaternion.Inverse(currentRotation) * authorityRotation;
        }

        /// <summary>
        /// Update corrections (call each frame).
        /// </summary>
        public (Vector3 positionAdjustment, Quaternion rotationAdjustment) Update(float deltaTime)
        {
            // Decay position error
            float t = 1f - MathF.Pow(1f - _blendRate, deltaTime * 60f);
            var posAdj = _positionError * t;
            _positionError -= posAdj;

            // Decay rotation error
            var rotAdj = Quaternion.Slerp(Quaternion.Identity, _rotationError, t);
            _rotationError = Quaternion.Slerp(_rotationError, Quaternion.Identity, t);

            return (posAdj, rotAdj);
        }

        /// <summary>
        /// Remaining position error magnitude.
        /// </summary>
        public float RemainingError => _positionError.Length();
    }
}

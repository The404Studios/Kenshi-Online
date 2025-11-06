using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Advanced interpolation engine for smooth client-side prediction and server reconciliation
    /// </summary>
    public class InterpolationEngine
    {
        private class InterpolationSnapshot
        {
            public long Timestamp { get; set; }
            public Vector3 Position { get; set; }
            public Vector3 Rotation { get; set; }
            public Vector3 Velocity { get; set; }
            public Dictionary<string, float> CustomValues { get; set; } = new Dictionary<string, float>();
        }

        private readonly ConcurrentDictionary<string, List<InterpolationSnapshot>> _entitySnapshots;
        private readonly int _bufferSize;
        private readonly float _interpolationDelay; // ms
        private readonly bool _useClientPrediction;

        public InterpolationEngine(int bufferSize = 10, float interpolationDelayMs = 100f, bool useClientPrediction = true)
        {
            _entitySnapshots = new ConcurrentDictionary<string, List<InterpolationSnapshot>>();
            _bufferSize = bufferSize;
            _interpolationDelay = interpolationDelayMs;
            _useClientPrediction = useClientPrediction;
        }

        /// <summary>
        /// Add a snapshot for an entity
        /// </summary>
        public void AddSnapshot(string entityId, Vector3 position, Vector3 rotation, Vector3 velocity, long timestamp, Dictionary<string, float> customValues = null)
        {
            var snapshot = new InterpolationSnapshot
            {
                Timestamp = timestamp,
                Position = position,
                Rotation = rotation,
                Velocity = velocity,
                CustomValues = customValues ?? new Dictionary<string, float>()
            };

            var snapshots = _entitySnapshots.GetOrAdd(entityId, _ => new List<InterpolationSnapshot>());

            lock (snapshots)
            {
                snapshots.Add(snapshot);

                // Sort by timestamp
                snapshots.Sort((a, b) => a.Timestamp.CompareTo(b.Timestamp));

                // Limit buffer size
                if (snapshots.Count > _bufferSize)
                {
                    snapshots.RemoveRange(0, snapshots.Count - _bufferSize);
                }
            }
        }

        /// <summary>
        /// Get interpolated position for an entity at current time
        /// </summary>
        public bool GetInterpolatedState(string entityId, long currentTime, out Vector3 position, out Vector3 rotation, out Dictionary<string, float> customValues)
        {
            position = Vector3.Zero;
            rotation = Vector3.Zero;
            customValues = new Dictionary<string, float>();

            if (!_entitySnapshots.TryGetValue(entityId, out var snapshots))
                return false;

            lock (snapshots)
            {
                if (snapshots.Count < 2)
                {
                    if (snapshots.Count == 1)
                    {
                        position = snapshots[0].Position;
                        rotation = snapshots[0].Rotation;
                        customValues = new Dictionary<string, float>(snapshots[0].CustomValues);
                        return true;
                    }
                    return false;
                }

                // Interpolate at a point slightly in the past for smoother movement
                long interpolationTime = currentTime - (long)_interpolationDelay;

                // Find the two snapshots to interpolate between
                InterpolationSnapshot from = null;
                InterpolationSnapshot to = null;

                for (int i = 0; i < snapshots.Count - 1; i++)
                {
                    if (snapshots[i].Timestamp <= interpolationTime && snapshots[i + 1].Timestamp >= interpolationTime)
                    {
                        from = snapshots[i];
                        to = snapshots[i + 1];
                        break;
                    }
                }

                // If we're ahead of all snapshots, use prediction
                if (from == null && to == null)
                {
                    var latest = snapshots[snapshots.Count - 1];
                    if (_useClientPrediction && interpolationTime > latest.Timestamp)
                    {
                        // Extrapolate using velocity
                        float deltaTime = (interpolationTime - latest.Timestamp) / 1000f;
                        position = latest.Position + latest.Velocity * deltaTime;
                        rotation = latest.Rotation;
                        customValues = new Dictionary<string, float>(latest.CustomValues);
                        return true;
                    }
                    else
                    {
                        position = latest.Position;
                        rotation = latest.Rotation;
                        customValues = new Dictionary<string, float>(latest.CustomValues);
                        return true;
                    }
                }

                // If we're behind all snapshots, use oldest
                if (from == null)
                {
                    position = snapshots[0].Position;
                    rotation = snapshots[0].Rotation;
                    customValues = new Dictionary<string, float>(snapshots[0].CustomValues);
                    return true;
                }

                // Interpolate between the two snapshots
                float t = (float)(interpolationTime - from.Timestamp) / (to.Timestamp - from.Timestamp);
                t = Math.Clamp(t, 0f, 1f);

                position = Vector3.Lerp(from.Position, to.Position, t);
                rotation = LerpRotation(from.Rotation, to.Rotation, t);

                // Interpolate custom values
                foreach (var key in from.CustomValues.Keys.Union(to.CustomValues.Keys))
                {
                    float fromValue = from.CustomValues.ContainsKey(key) ? from.CustomValues[key] : 0f;
                    float toValue = to.CustomValues.ContainsKey(key) ? to.CustomValues[key] : 0f;
                    customValues[key] = Lerp(fromValue, toValue, t);
                }

                return true;
            }
        }

        /// <summary>
        /// Hermite interpolation for smoother curves
        /// </summary>
        public bool GetHermiteInterpolatedPosition(string entityId, long currentTime, out Vector3 position)
        {
            position = Vector3.Zero;

            if (!_entitySnapshots.TryGetValue(entityId, out var snapshots))
                return false;

            lock (snapshots)
            {
                if (snapshots.Count < 4)
                {
                    // Fall back to linear interpolation
                    return GetInterpolatedState(entityId, currentTime, out position, out _, out _);
                }

                long interpolationTime = currentTime - (long)_interpolationDelay;

                // Find the four points for Hermite interpolation
                for (int i = 1; i < snapshots.Count - 2; i++)
                {
                    if (snapshots[i].Timestamp <= interpolationTime && snapshots[i + 1].Timestamp >= interpolationTime)
                    {
                        var p0 = snapshots[i - 1].Position;
                        var p1 = snapshots[i].Position;
                        var p2 = snapshots[i + 1].Position;
                        var p3 = snapshots[i + 2].Position;

                        float t = (float)(interpolationTime - snapshots[i].Timestamp) / (snapshots[i + 1].Timestamp - snapshots[i].Timestamp);
                        t = Math.Clamp(t, 0f, 1f);

                        position = HermiteInterpolate(p0, p1, p2, p3, t);
                        return true;
                    }
                }

                // Fall back to linear
                return GetInterpolatedState(entityId, currentTime, out position, out _, out _);
            }
        }

        /// <summary>
        /// Clear snapshots for an entity
        /// </summary>
        public void ClearEntity(string entityId)
        {
            _entitySnapshots.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Clear all snapshots
        /// </summary>
        public void ClearAll()
        {
            _entitySnapshots.Clear();
        }

        /// <summary>
        /// Get snapshot count for an entity (for debugging)
        /// </summary>
        public int GetSnapshotCount(string entityId)
        {
            if (_entitySnapshots.TryGetValue(entityId, out var snapshots))
            {
                lock (snapshots)
                {
                    return snapshots.Count;
                }
            }
            return 0;
        }

        #region Helper Methods

        private static float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        private static Vector3 LerpRotation(Vector3 from, Vector3 to, float t)
        {
            // Handle angle wrapping for rotation
            Vector3 result = new Vector3();
            result.X = LerpAngle(from.X, to.X, t);
            result.Y = LerpAngle(from.Y, to.Y, t);
            result.Z = LerpAngle(from.Z, to.Z, t);
            return result;
        }

        private static float LerpAngle(float from, float to, float t)
        {
            float delta = ((to - from + 180) % 360) - 180;
            return from + delta * t;
        }

        private static Vector3 HermiteInterpolate(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
        {
            float t2 = t * t;
            float t3 = t2 * t;

            float h1 = 2 * t3 - 3 * t2 + 1;
            float h2 = -2 * t3 + 3 * t2;
            float h3 = t3 - 2 * t2 + t;
            float h4 = t3 - t2;

            Vector3 m0 = (p2 - p0) * 0.5f;
            Vector3 m1 = (p3 - p1) * 0.5f;

            return h1 * p1 + h2 * p2 + h3 * m0 + h4 * m1;
        }

        #endregion
    }
}

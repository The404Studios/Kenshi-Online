using System;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    public class Position
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; } // Added Z coordinate for proper 3D positioning
        public float RotationX { get; set; } // Rotation around X axis
        public float RotationY { get; set; } // Rotation around Y axis
        public float RotationZ { get; set; } // Rotation around Z axis (yaw)
        public long Timestamp { get; set; } // For synchronization and interpolation

        // Default constructor
        public Position()
        {
            X = 0;
            Y = 0;
            Z = 0;
            RotationX = 0;
            RotationY = 0;
            RotationZ = 0;
            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        // Constructor with position parameters
        public Position(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
            RotationX = 0;
            RotationY = 0;
            RotationZ = 0;
            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        // Constructor with position and rotation parameters
        public Position(float x, float y, float z, float rotX, float rotY, float rotZ)
        {
            X = x;
            Y = y;
            Z = z;
            RotationX = rotX;
            RotationY = rotY;
            RotationZ = rotZ;
            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        // Full constructor with timestamp
        public Position(float x, float y, float z, float rotX, float rotY, float rotZ, long timestamp)
        {
            X = x;
            Y = y;
            Z = z;
            RotationX = rotX;
            RotationY = rotY;
            RotationZ = rotZ;
            Timestamp = timestamp;
        }

        // Calculate distance between positions (ignoring rotation)
        public float DistanceTo(Position other)
        {
            return (float)Math.Sqrt(
                Math.Pow(X - other.X, 2) +
                Math.Pow(Y - other.Y, 2) +
                Math.Pow(Z - other.Z, 2)
            );
        }

        // Linear interpolation between two positions for smooth movement
        public static Position Lerp(Position start, Position end, float factor)
        {
            factor = Math.Clamp(factor, 0.0f, 1.0f);

            return new Position(
                start.X + (end.X - start.X) * factor,
                start.Y + (end.Y - start.Y) * factor,
                start.Z + (end.Z - start.Z) * factor,
                start.RotationX + (end.RotationX - start.RotationX) * factor,
                start.RotationY + (end.RotationY - start.RotationY) * factor,
                start.RotationZ + (end.RotationZ - start.RotationZ) * factor,
                end.Timestamp
            );
        }

        // Check if position has changed significantly since last update
        public bool HasChangedSignificantly(Position other, float threshold = 0.05f)
        {
            return Math.Abs(X - other.X) > threshold ||
                  Math.Abs(Y - other.Y) > threshold ||
                  Math.Abs(Z - other.Z) > threshold ||
                  Math.Abs(RotationZ - other.RotationZ) > 5.0f; // Rotation threshold in degrees
        }

        // More lenient comparison for positions that's useful for syncing
        // Only checks if position has changed, not rotation
        public bool HasPositionChanged(Position other, float threshold = 0.1f)
        {
            return Math.Abs(X - other.X) > threshold ||
                  Math.Abs(Y - other.Y) > threshold ||
                  Math.Abs(Z - other.Z) > threshold;
        }

        // Create a copy of this position
        public Position Clone()
        {
            return new Position(X, Y, Z, RotationX, RotationY, RotationZ, Timestamp);
        }

        // Override ToString for easier debugging
        public override string ToString()
        {
            return $"Position(X:{X:F2}, Y:{Y:F2}, Z:{Z:F2}, Rot:{RotationZ:F1}°)";
        }
    }

    // Helper class for position prediction and history
    public class PositionBuffer
    {
        private Position[] positionHistory;
        private int currentIndex = 0;
        private int count = 0;
        private readonly int capacity;

        public PositionBuffer(int capacity = 10)
        {
            this.capacity = capacity;
            positionHistory = new Position[capacity];
        }

        // Add a position to the buffer
        public void AddPosition(Position position)
        {
            positionHistory[currentIndex] = position;
            currentIndex = (currentIndex + 1) % capacity;
            if (count < capacity)
                count++;
        }

        // Get the most recent position
        public Position GetLatestPosition()
        {
            if (count == 0)
                return null;

            int index = (currentIndex - 1 + capacity) % capacity;
            return positionHistory[index];
        }

        // Predict future position based on velocity
        public Position PredictPosition(float timeAhead)
        {
            if (count < 2)
                return GetLatestPosition()?.Clone();

            // Get the two most recent positions
            int latestIndex = (currentIndex - 1 + capacity) % capacity;
            int prevIndex = (currentIndex - 2 + capacity) % capacity;

            Position latest = positionHistory[latestIndex];
            Position prev = positionHistory[prevIndex];

            // Calculate time delta
            float deltaTime = (latest.Timestamp - prev.Timestamp) / 1000.0f;
            if (deltaTime <= 0)
                return latest.Clone();

            // Calculate velocity
            float velX = (latest.X - prev.X) / deltaTime;
            float velY = (latest.Y - prev.Y) / deltaTime;
            float velZ = (latest.Z - prev.Z) / deltaTime;
            float velRotZ = (latest.RotationZ - prev.RotationZ) / deltaTime;

            // Predict new position
            return new Position(
                latest.X + velX * timeAhead,
                latest.Y + velY * timeAhead,
                latest.Z + velZ * timeAhead,
                latest.RotationX,
                latest.RotationY,
                latest.RotationZ + velRotZ * timeAhead,
                latest.Timestamp + (long)(timeAhead * 1000)
            );
        }

        // Clear all positions
        public void Clear()
        {
            for (int i = 0; i < capacity; i++)
                positionHistory[i] = null;

            currentIndex = 0;
            count = 0;
        }
    }
}
using System;
using System.Numerics;
using System.Runtime.InteropServices;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Utility;

namespace KenshiOnline.Coordinates.Integration
{
    /// <summary>
    /// IMemoryActuator implementation that bridges the Coordinates system to Kenshi's game memory.
    ///
    /// This is where the rubber meets the road - abstract authority concepts become
    /// actual memory writes to the running game process.
    ///
    /// All writes go through the DataBus pipeline before reaching here, so by the time
    /// we're called, the data has been validated, normalized, authorized, and gated.
    /// </summary>
    public class KenshiMemoryActuator : IMemoryActuator
    {
        private readonly KenshiGameBridge _gameBridge;
        private readonly IntPtr _processHandle;
        private readonly long _baseAddress;

        // Statistics
        private long _transformReads;
        private long _transformWrites;
        private long _transformSnaps;
        private long _healthReads;
        private long _healthWrites;

        public KenshiMemoryActuator(KenshiGameBridge gameBridge)
        {
            _gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));

            // Get process info from game bridge
            if (_gameBridge.KenshiProcess?.MainModule != null)
            {
                _baseAddress = _gameBridge.KenshiProcess.MainModule.BaseAddress.ToInt64();
            }

            Logger.Log("[KenshiMemoryActuator] Initialized");
        }

        #region IMemoryActuator Implementation

        /// <summary>
        /// Read transform from game memory at the given handle.
        /// Handle is a pointer to a character/entity structure.
        /// </summary>
        public (Vector3 position, Quaternion rotation)? ReadTransform(IntPtr handle)
        {
            if (handle == IntPtr.Zero || !_gameBridge.IsConnected)
                return null;

            try
            {
                // Read position and rotation using RuntimeOffsets
                int posOffset = RuntimeOffsets.Character.Position;
                int rotOffset = RuntimeOffsets.Character.Rotation;

                float posX = ReadFloat(handle + posOffset);
                float posY = ReadFloat(handle + posOffset + 4);
                float posZ = ReadFloat(handle + posOffset + 8);

                float rotX = ReadFloat(handle + rotOffset);
                float rotY = ReadFloat(handle + rotOffset + 4);
                float rotZ = ReadFloat(handle + rotOffset + 8);

                // Convert Euler to Quaternion (Kenshi uses Euler angles)
                var rotation = QuaternionFromEuler(rotX, rotY, rotZ);

                _transformReads++;
                return (new Vector3(posX, posY, posZ), rotation);
            }
            catch (Exception ex)
            {
                Logger.Log($"[KenshiMemoryActuator] Error reading transform: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Write transform to game memory (soft write - for interpolated positions).
        /// This is the normal write path - smooth, may be overwritten by game physics.
        /// </summary>
        public void WriteTransform(IntPtr handle, Vector3 position, Quaternion rotation)
        {
            if (handle == IntPtr.Zero || !_gameBridge.IsConnected)
                return;

            try
            {
                int posOffset = RuntimeOffsets.Character.Position;
                int rotOffset = RuntimeOffsets.Character.Rotation;

                // Write position
                WriteFloat(handle + posOffset, position.X);
                WriteFloat(handle + posOffset + 4, position.Y);
                WriteFloat(handle + posOffset + 8, position.Z);

                // Convert Quaternion to Euler and write rotation
                var euler = EulerFromQuaternion(rotation);
                WriteFloat(handle + rotOffset, euler.X);
                WriteFloat(handle + rotOffset + 4, euler.Y);
                WriteFloat(handle + rotOffset + 8, euler.Z);

                _transformWrites++;
            }
            catch (Exception ex)
            {
                Logger.Log($"[KenshiMemoryActuator] Error writing transform: {ex.Message}");
            }
        }

        /// <summary>
        /// Write transform immediately (hard snap - for corrections and teleports).
        /// This bypasses any smoothing and forces the position immediately.
        /// </summary>
        public void WriteTransformImmediate(IntPtr handle, Vector3 position, Quaternion rotation)
        {
            if (handle == IntPtr.Zero || !_gameBridge.IsConnected)
                return;

            try
            {
                int posOffset = RuntimeOffsets.Character.Position;
                int rotOffset = RuntimeOffsets.Character.Rotation;

                // Write position
                WriteFloat(handle + posOffset, position.X);
                WriteFloat(handle + posOffset + 4, position.Y);
                WriteFloat(handle + posOffset + 8, position.Z);

                // Convert Quaternion to Euler and write rotation
                var euler = EulerFromQuaternion(rotation);
                WriteFloat(handle + rotOffset, euler.X);
                WriteFloat(handle + rotOffset + 4, euler.Y);
                WriteFloat(handle + rotOffset + 8, euler.Z);

                // For immediate writes, also update velocity to zero to prevent drift
                int velocityOffset = posOffset + 12; // Velocity usually follows position
                WriteFloat(handle + velocityOffset, 0f);
                WriteFloat(handle + velocityOffset + 4, 0f);
                WriteFloat(handle + velocityOffset + 8, 0f);

                _transformSnaps++;
            }
            catch (Exception ex)
            {
                Logger.Log($"[KenshiMemoryActuator] Error snapping transform: {ex.Message}");
            }
        }

        /// <summary>
        /// Read health values from game memory.
        /// </summary>
        public (float current, float max)? ReadHealth(IntPtr handle)
        {
            if (handle == IntPtr.Zero || !_gameBridge.IsConnected)
                return null;

            try
            {
                int healthOffset = RuntimeOffsets.Character.Health;
                int maxHealthOffset = RuntimeOffsets.Character.MaxHealth;

                float current = ReadFloat(handle + healthOffset);
                float max = ReadFloat(handle + maxHealthOffset);

                _healthReads++;
                return (current, max);
            }
            catch (Exception ex)
            {
                Logger.Log($"[KenshiMemoryActuator] Error reading health: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Write health values to game memory.
        /// </summary>
        public void WriteHealth(IntPtr handle, float current, float max)
        {
            if (handle == IntPtr.Zero || !_gameBridge.IsConnected)
                return;

            try
            {
                int healthOffset = RuntimeOffsets.Character.Health;
                int maxHealthOffset = RuntimeOffsets.Character.MaxHealth;

                WriteFloat(handle + healthOffset, current);
                WriteFloat(handle + maxHealthOffset, max);

                _healthWrites++;
            }
            catch (Exception ex)
            {
                Logger.Log($"[KenshiMemoryActuator] Error writing health: {ex.Message}");
            }
        }

        #endregion

        #region Memory Operations (via Windows API)

        [DllImport("kernel32.dll")]
        private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesRead);

        [DllImport("kernel32.dll")]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out int lpNumberOfBytesWritten);

        private IntPtr ProcessHandle
        {
            get
            {
                // Get handle from game bridge's process
                if (_gameBridge.KenshiProcess == null)
                    return IntPtr.Zero;

                // Use reflection or internal access to get the handle
                // For now, open a new handle (the bridge should expose this)
                return _gameBridge.KenshiProcess.Handle;
            }
        }

        private float ReadFloat(IntPtr address)
        {
            byte[] buffer = new byte[4];
            if (ReadProcessMemory(ProcessHandle, address, buffer, 4, out _))
            {
                return BitConverter.ToSingle(buffer, 0);
            }
            return 0f;
        }

        private void WriteFloat(IntPtr address, float value)
        {
            byte[] buffer = BitConverter.GetBytes(value);
            WriteProcessMemory(ProcessHandle, address, buffer, 4, out _);
        }

        #endregion

        #region Quaternion/Euler Conversion

        private static Quaternion QuaternionFromEuler(float x, float y, float z)
        {
            // Convert degrees to radians
            float rx = x * MathF.PI / 180f;
            float ry = y * MathF.PI / 180f;
            float rz = z * MathF.PI / 180f;

            return Quaternion.CreateFromYawPitchRoll(ry, rx, rz);
        }

        private static Vector3 EulerFromQuaternion(Quaternion q)
        {
            // Convert quaternion to Euler angles (in degrees)
            float sinr_cosp = 2 * (q.W * q.X + q.Y * q.Z);
            float cosr_cosp = 1 - 2 * (q.X * q.X + q.Y * q.Y);
            float roll = MathF.Atan2(sinr_cosp, cosr_cosp);

            float sinp = 2 * (q.W * q.Y - q.Z * q.X);
            float pitch;
            if (MathF.Abs(sinp) >= 1)
                pitch = MathF.CopySign(MathF.PI / 2, sinp);
            else
                pitch = MathF.Asin(sinp);

            float siny_cosp = 2 * (q.W * q.Z + q.X * q.Y);
            float cosy_cosp = 1 - 2 * (q.Y * q.Y + q.Z * q.Z);
            float yaw = MathF.Atan2(siny_cosp, cosy_cosp);

            // Convert radians to degrees
            return new Vector3(
                pitch * 180f / MathF.PI,
                yaw * 180f / MathF.PI,
                roll * 180f / MathF.PI
            );
        }

        #endregion

        #region Statistics

        public ActuatorStats GetStats()
        {
            return new ActuatorStats
            {
                TransformReads = _transformReads,
                TransformWrites = _transformWrites,
                TransformSnaps = _transformSnaps,
                HealthReads = _healthReads,
                HealthWrites = _healthWrites
            };
        }

        #endregion
    }

    public struct ActuatorStats
    {
        public long TransformReads;
        public long TransformWrites;
        public long TransformSnaps;
        public long HealthReads;
        public long HealthWrites;

        public long TotalReads => TransformReads + HealthReads;
        public long TotalWrites => TransformWrites + TransformSnaps + HealthWrites;
    }
}

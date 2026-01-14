using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// The type of spatial reference frame.
    /// </summary>
    public enum FrameType : byte
    {
        /// <summary>Absolute world coordinates - the canonical frame for Ring3 truth.</summary>
        World = 0,

        /// <summary>Local coordinates relative to a parent entity.</summary>
        Local = 1,

        /// <summary>Coordinates in a parent node's space (scene graph).</summary>
        Parented = 2,

        /// <summary>Animation root motion space (delta from animation origin).</summary>
        RootMotion = 3,

        /// <summary>Physics body space (center of mass).</summary>
        Physics = 4,

        /// <summary>Camera/view space.</summary>
        View = 5,

        /// <summary>Screen/UI space.</summary>
        Screen = 6
    }

    /// <summary>
    /// SpaceFrame - The Space Dimension
    ///
    /// Most desync in older engines comes from mixing frames:
    ///   - local vs world
    ///   - parented nodes vs absolute
    ///   - animation root motion vs physics body
    ///
    /// You must define the canonical frame for truth in Ring3:
    ///   "authoritative transform is world-space (pos, rot), derived velocity"
    ///
    /// Anything else becomes a view transform, not truth.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct SpaceFrame : IEquatable<SpaceFrame>
    {
        /// <summary>
        /// The type of reference frame.
        /// </summary>
        public readonly FrameType Type;

        /// <summary>
        /// The parent entity (for Local/Parented frames).
        /// Invalid for World frame.
        /// </summary>
        public readonly NetId ParentId;

        /// <summary>
        /// Optional bone/node index within parent (for skeletal attachments).
        /// -1 means root/no specific bone.
        /// </summary>
        public readonly short BoneIndex;

        /// <summary>
        /// Additional frame flags.
        /// </summary>
        public readonly SpaceFrameFlags Flags;

        /// <summary>
        /// The canonical world frame - use this for authoritative truth.
        /// </summary>
        public static readonly SpaceFrame World = new SpaceFrame(FrameType.World, NetId.Invalid, -1, SpaceFrameFlags.None);

        /// <summary>
        /// Physics frame (for physics-driven objects).
        /// </summary>
        public static readonly SpaceFrame Physics = new SpaceFrame(FrameType.Physics, NetId.Invalid, -1, SpaceFrameFlags.None);

        public SpaceFrame(FrameType type, NetId parentId, short boneIndex = -1, SpaceFrameFlags flags = SpaceFrameFlags.None)
        {
            Type = type;
            ParentId = parentId;
            BoneIndex = boneIndex;
            Flags = flags;
        }

        /// <summary>
        /// Create a local frame relative to a parent entity.
        /// </summary>
        public static SpaceFrame Local(NetId parent) => new SpaceFrame(FrameType.Local, parent);

        /// <summary>
        /// Create a parented frame attached to a specific bone.
        /// </summary>
        public static SpaceFrame Parented(NetId parent, short boneIndex) =>
            new SpaceFrame(FrameType.Parented, parent, boneIndex);

        /// <summary>
        /// True if this is an absolute frame (not relative to another entity).
        /// </summary>
        public bool IsAbsolute => Type == FrameType.World || Type == FrameType.Physics;

        /// <summary>
        /// True if this frame requires a parent transform to resolve.
        /// </summary>
        public bool RequiresParent => Type == FrameType.Local || Type == FrameType.Parented;

        public bool Equals(SpaceFrame other) =>
            Type == other.Type && ParentId == other.ParentId && BoneIndex == other.BoneIndex;

        public override bool Equals(object? obj) => obj is SpaceFrame other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Type, ParentId, BoneIndex);

        public static bool operator ==(SpaceFrame left, SpaceFrame right) => left.Equals(right);
        public static bool operator !=(SpaceFrame left, SpaceFrame right) => !left.Equals(right);

        public override string ToString()
        {
            return Type switch
            {
                FrameType.World => "Frame(World)",
                FrameType.Local => $"Frame(Local:{ParentId.ToShortString()})",
                FrameType.Parented => $"Frame(Parented:{ParentId.ToShortString()}[{BoneIndex}])",
                FrameType.RootMotion => "Frame(RootMotion)",
                FrameType.Physics => "Frame(Physics)",
                _ => $"Frame({Type})"
            };
        }
    }

    [Flags]
    public enum SpaceFrameFlags : byte
    {
        None = 0,

        /// <summary>Interpolate between frames smoothly.</summary>
        Interpolated = 1 << 0,

        /// <summary>This is derived/computed, not directly set.</summary>
        Derived = 1 << 1,

        /// <summary>Apply constraints after transform.</summary>
        Constrained = 1 << 2,

        /// <summary>This transform is client-predicted.</summary>
        Predicted = 1 << 3
    }

    /// <summary>
    /// A transform in a specific space frame.
    /// This is the complete spatial coordinate: position + rotation + frame.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct FramedTransform : IEquatable<FramedTransform>
    {
        /// <summary>
        /// Position in the frame's coordinate system.
        /// </summary>
        public readonly Vector3 Position;

        /// <summary>
        /// Rotation as a quaternion.
        /// </summary>
        public readonly Quaternion Rotation;

        /// <summary>
        /// The reference frame these coordinates are in.
        /// </summary>
        public readonly SpaceFrame Frame;

        /// <summary>
        /// Velocity (derived, world-space by convention).
        /// </summary>
        public readonly Vector3 Velocity;

        public FramedTransform(Vector3 position, Quaternion rotation, SpaceFrame frame, Vector3 velocity = default)
        {
            Position = position;
            Rotation = rotation;
            Frame = frame;
            Velocity = velocity;
        }

        /// <summary>
        /// Create a world-space transform.
        /// </summary>
        public static FramedTransform WorldSpace(Vector3 position, Quaternion rotation, Vector3 velocity = default)
        {
            return new FramedTransform(position, rotation, SpaceFrame.World, velocity);
        }

        /// <summary>
        /// Create a world-space transform from Euler angles (degrees).
        /// </summary>
        public static FramedTransform WorldSpace(float x, float y, float z, float yawDegrees, Vector3 velocity = default)
        {
            var position = new Vector3(x, y, z);
            var rotation = Quaternion.CreateFromYawPitchRoll(
                yawDegrees * MathF.PI / 180f, 0, 0);
            return new FramedTransform(position, rotation, SpaceFrame.World, velocity);
        }

        /// <summary>
        /// Create a local-space transform relative to a parent.
        /// </summary>
        public static FramedTransform LocalSpace(Vector3 localPos, Quaternion localRot, NetId parent)
        {
            return new FramedTransform(localPos, localRot, SpaceFrame.Local(parent));
        }

        /// <summary>
        /// Get the forward direction vector.
        /// </summary>
        public Vector3 Forward => Vector3.Transform(Vector3.UnitZ, Rotation);

        /// <summary>
        /// Get the right direction vector.
        /// </summary>
        public Vector3 Right => Vector3.Transform(Vector3.UnitX, Rotation);

        /// <summary>
        /// Get the up direction vector.
        /// </summary>
        public Vector3 Up => Vector3.Transform(Vector3.UnitY, Rotation);

        /// <summary>
        /// Get yaw angle in degrees.
        /// </summary>
        public float YawDegrees
        {
            get
            {
                // Extract yaw from quaternion
                var forward = Forward;
                return MathF.Atan2(forward.X, forward.Z) * 180f / MathF.PI;
            }
        }

        /// <summary>
        /// Calculate distance to another transform (in same frame).
        /// </summary>
        public float DistanceTo(FramedTransform other)
        {
            if (Frame != other.Frame)
                throw new InvalidOperationException("Cannot compare transforms in different frames");
            return Vector3.Distance(Position, other.Position);
        }

        /// <summary>
        /// Linear interpolation between transforms.
        /// </summary>
        public static FramedTransform Lerp(FramedTransform a, FramedTransform b, float t)
        {
            if (a.Frame != b.Frame)
                throw new InvalidOperationException("Cannot interpolate transforms in different frames");

            return new FramedTransform(
                Vector3.Lerp(a.Position, b.Position, t),
                Quaternion.Slerp(a.Rotation, b.Rotation, t),
                a.Frame,
                Vector3.Lerp(a.Velocity, b.Velocity, t)
            );
        }

        /// <summary>
        /// Check if position has changed significantly.
        /// </summary>
        public bool HasPositionChanged(FramedTransform other, float threshold = 0.01f)
        {
            return Vector3.DistanceSquared(Position, other.Position) > threshold * threshold;
        }

        /// <summary>
        /// Check if rotation has changed significantly.
        /// </summary>
        public bool HasRotationChanged(FramedTransform other, float thresholdDegrees = 1f)
        {
            float dot = Math.Abs(Quaternion.Dot(Rotation, other.Rotation));
            float angleDiff = MathF.Acos(Math.Min(dot, 1f)) * 2f * 180f / MathF.PI;
            return angleDiff > thresholdDegrees;
        }

        public bool Equals(FramedTransform other) =>
            Position == other.Position &&
            Rotation == other.Rotation &&
            Frame == other.Frame;

        public override bool Equals(object? obj) => obj is FramedTransform other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Position, Rotation, Frame);

        public static bool operator ==(FramedTransform left, FramedTransform right) => left.Equals(right);
        public static bool operator !=(FramedTransform left, FramedTransform right) => !left.Equals(right);

        public override string ToString()
        {
            return $"Transform({Frame.Type}: P({Position.X:F2},{Position.Y:F2},{Position.Z:F2}) Y:{YawDegrees:F1}°)";
        }
    }

    /// <summary>
    /// Resolves transforms from one frame to another.
    /// This is essential for converting local/parented coordinates to world space.
    /// </summary>
    public interface IFrameResolver
    {
        /// <summary>
        /// Resolve a transform to world space.
        /// </summary>
        FramedTransform ToWorld(FramedTransform transform);

        /// <summary>
        /// Convert a world-space transform to a specific frame.
        /// </summary>
        FramedTransform FromWorld(FramedTransform worldTransform, SpaceFrame targetFrame);

        /// <summary>
        /// Get the world transform of a parent entity.
        /// </summary>
        FramedTransform? GetParentTransform(NetId parentId, short boneIndex = -1);
    }

    /// <summary>
    /// Simple frame resolver that stores parent transforms.
    /// In practice, this would query the game's scene graph.
    /// </summary>
    public class SimpleFrameResolver : IFrameResolver
    {
        private readonly Func<NetId, short, FramedTransform?> _getParentTransform;

        public SimpleFrameResolver(Func<NetId, short, FramedTransform?> getParentTransform)
        {
            _getParentTransform = getParentTransform;
        }

        public FramedTransform ToWorld(FramedTransform transform)
        {
            if (transform.Frame.IsAbsolute)
                return transform;

            if (!transform.Frame.RequiresParent)
                return transform; // Non-world absolute frame, treat as world for now

            var parent = _getParentTransform(transform.Frame.ParentId, transform.Frame.BoneIndex);
            if (!parent.HasValue)
            {
                // Parent not found, return as-is with world frame
                return new FramedTransform(transform.Position, transform.Rotation, SpaceFrame.World, transform.Velocity);
            }

            // Recursively resolve parent to world
            var parentWorld = ToWorld(parent.Value);

            // Transform local to world
            var worldPos = parentWorld.Position + Vector3.Transform(transform.Position, parentWorld.Rotation);
            var worldRot = parentWorld.Rotation * transform.Rotation;

            return new FramedTransform(worldPos, worldRot, SpaceFrame.World, transform.Velocity);
        }

        public FramedTransform FromWorld(FramedTransform worldTransform, SpaceFrame targetFrame)
        {
            if (targetFrame.IsAbsolute)
                return new FramedTransform(worldTransform.Position, worldTransform.Rotation, targetFrame, worldTransform.Velocity);

            var parent = _getParentTransform(targetFrame.ParentId, targetFrame.BoneIndex);
            if (!parent.HasValue)
                return new FramedTransform(worldTransform.Position, worldTransform.Rotation, targetFrame, worldTransform.Velocity);

            var parentWorld = ToWorld(parent.Value);

            // Inverse transform: world to local
            var invRot = Quaternion.Inverse(parentWorld.Rotation);
            var localPos = Vector3.Transform(worldTransform.Position - parentWorld.Position, invRot);
            var localRot = invRot * worldTransform.Rotation;

            return new FramedTransform(localPos, localRot, targetFrame, worldTransform.Velocity);
        }

        public FramedTransform? GetParentTransform(NetId parentId, short boneIndex = -1)
        {
            return _getParentTransform(parentId, boneIndex);
        }
    }
}

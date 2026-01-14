using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Schema - The Meaning Dimension
    ///
    /// Memory is untyped. Multiplayer needs types.
    /// Each syncable property type has:
    ///   - a version
    ///   - serialization rules
    ///   - normalization rules
    ///
    /// If you don't version schema, "breakthrough" becomes "breaks whenever you update."
    /// </summary>
    public enum SchemaKind : ushort
    {
        Unknown = 0,

        // Core types (1-99)
        Transform = 1,
        Health = 2,
        Inventory = 3,
        InteractionState = 4,
        AICoarseState = 5,

        // Combat types (100-199)
        CombatAction = 100,
        DamageEvent = 101,
        StatusEffect = 102,
        LimbState = 103,

        // Input types (200-299)
        InputState = 200,
        MovementIntent = 201,
        ActionIntent = 202,

        // World types (300-399)
        SpawnEvent = 300,
        DespawnEvent = 301,
        OwnershipChange = 302,

        // Building types (400-499)
        BuildingState = 400,
        ConstructionProgress = 401,

        // Item types (500-599)
        ItemState = 500,
        ItemTransfer = 501,
        EquipmentChange = 502,

        // Faction types (600-699)
        FactionRelation = 600,
        ReputationChange = 601,

        // Custom/Extension (1000+)
        Custom = 1000
    }

    /// <summary>
    /// Schema version identifier.
    /// Combines kind + version for schema evolution.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct SchemaId : IEquatable<SchemaId>
    {
        public readonly SchemaKind Kind;
        public readonly ushort Version;

        public SchemaId(SchemaKind kind, ushort version)
        {
            Kind = kind;
            Version = version;
        }

        public static readonly SchemaId Invalid = default;

        public bool IsValid => Kind != SchemaKind.Unknown;

        public bool Equals(SchemaId other) => Kind == other.Kind && Version == other.Version;
        public override bool Equals(object? obj) => obj is SchemaId other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Kind, Version);

        public static bool operator ==(SchemaId left, SchemaId right) => left.Equals(right);
        public static bool operator !=(SchemaId left, SchemaId right) => !left.Equals(right);

        public override string ToString() => $"Schema({Kind}:v{Version})";
    }

    /// <summary>
    /// A typed, versioned payload that carries semantic meaning.
    /// This is what travels through the rings, not raw bytes.
    /// </summary>
    public abstract class SchemaPayload
    {
        public abstract SchemaId SchemaId { get; }

        /// <summary>
        /// Serialize to bytes for transmission.
        /// </summary>
        public abstract byte[] Serialize();

        /// <summary>
        /// Compute a hash for deduplication/verification.
        /// </summary>
        public virtual int ComputeHash()
        {
            var bytes = Serialize();
            return ComputeHashFromBytes(bytes);
        }

        protected static int ComputeHashFromBytes(ReadOnlySpan<byte> bytes)
        {
            unchecked
            {
                int hash = 17;
                foreach (byte b in bytes)
                    hash = hash * 31 + b;
                return hash;
            }
        }
    }

    /// <summary>
    /// Transform schema - the most common payload type.
    /// </summary>
    public class TransformPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.Transform, 1);
        public override SchemaId SchemaId => _schemaId;

        public Vector3 Position { get; set; }
        public Quaternion Rotation { get; set; }
        public Vector3 Velocity { get; set; }
        public SpaceFrame Frame { get; set; }

        public TransformPayload() { }

        public TransformPayload(FramedTransform transform)
        {
            Position = transform.Position;
            Rotation = transform.Rotation;
            Velocity = transform.Velocity;
            Frame = transform.Frame;
        }

        public FramedTransform ToFramedTransform()
        {
            return new FramedTransform(Position, Rotation, Frame, Velocity);
        }

        public override byte[] Serialize()
        {
            // Simple binary format: 13 floats + frame info
            var buffer = new byte[13 * sizeof(float) + 12]; // Position(3) + Rotation(4) + Velocity(3) + Frame data
            var span = buffer.AsSpan();
            int offset = 0;

            BitConverter.TryWriteBytes(span.Slice(offset), Position.X); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Position.Y); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Position.Z); offset += 4;

            BitConverter.TryWriteBytes(span.Slice(offset), Rotation.X); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Rotation.Y); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Rotation.Z); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Rotation.W); offset += 4;

            BitConverter.TryWriteBytes(span.Slice(offset), Velocity.X); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Velocity.Y); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Velocity.Z); offset += 4;

            // Frame type and parent
            span[offset++] = (byte)Frame.Type;
            BitConverter.TryWriteBytes(span.Slice(offset), Frame.ParentId.Packed); offset += 8;
            BitConverter.TryWriteBytes(span.Slice(offset), Frame.BoneIndex);

            return buffer;
        }

        public static TransformPayload Deserialize(ReadOnlySpan<byte> data)
        {
            var payload = new TransformPayload();
            int offset = 0;

            payload.Position = new Vector3(
                BitConverter.ToSingle(data.Slice(offset, 4)), offset += 4,
                BitConverter.ToSingle(data.Slice(offset - 4 + 4, 4)),
                BitConverter.ToSingle(data.Slice(offset - 4 + 8, 4)));
            offset += 8;

            payload.Rotation = new Quaternion(
                BitConverter.ToSingle(data.Slice(offset, 4)),
                BitConverter.ToSingle(data.Slice(offset + 4, 4)),
                BitConverter.ToSingle(data.Slice(offset + 8, 4)),
                BitConverter.ToSingle(data.Slice(offset + 12, 4)));
            offset += 16;

            payload.Velocity = new Vector3(
                BitConverter.ToSingle(data.Slice(offset, 4)),
                BitConverter.ToSingle(data.Slice(offset + 4, 4)),
                BitConverter.ToSingle(data.Slice(offset + 8, 4)));
            offset += 12;

            var frameType = (FrameType)data[offset++];
            var parentPacked = BitConverter.ToUInt64(data.Slice(offset, 8)); offset += 8;
            var boneIndex = BitConverter.ToInt16(data.Slice(offset, 2));

            payload.Frame = new SpaceFrame(frameType, new NetId(parentPacked), boneIndex);

            return payload;
        }
    }

    /// <summary>
    /// Health schema for entity health state.
    /// </summary>
    public class HealthPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.Health, 1);
        public override SchemaId SchemaId => _schemaId;

        public float Current { get; set; }
        public float Maximum { get; set; }
        public Dictionary<string, float>? LimbHealth { get; set; }
        public bool IsConscious { get; set; }
        public bool IsDead { get; set; }

        public float Ratio => Maximum > 0 ? Current / Maximum : 0;

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }

        public static HealthPayload Deserialize(ReadOnlySpan<byte> data)
        {
            return JsonSerializer.Deserialize<HealthPayload>(data) ?? new HealthPayload();
        }
    }

    /// <summary>
    /// Combat action schema.
    /// </summary>
    public class CombatActionPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.CombatAction, 1);
        public override SchemaId SchemaId => _schemaId;

        public NetId AttackerId { get; set; }
        public NetId TargetId { get; set; }
        public string ActionType { get; set; } = "";
        public string WeaponId { get; set; } = "";
        public int Damage { get; set; }
        public string TargetLimb { get; set; } = "";
        public bool WasBlocked { get; set; }
        public bool WasCritical { get; set; }

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }

        public static CombatActionPayload Deserialize(ReadOnlySpan<byte> data)
        {
            return JsonSerializer.Deserialize<CombatActionPayload>(data) ?? new CombatActionPayload();
        }
    }

    /// <summary>
    /// Input/intent schema for player input.
    /// </summary>
    public class InputPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.InputState, 1);
        public override SchemaId SchemaId => _schemaId;

        public Vector2 Movement { get; set; }
        public float LookYaw { get; set; }
        public float LookPitch { get; set; }
        public InputButtons Buttons { get; set; }
        public NetId TargetId { get; set; }
        public long SequenceNumber { get; set; }

        public override byte[] Serialize()
        {
            var buffer = new byte[32];
            var span = buffer.AsSpan();
            int offset = 0;

            BitConverter.TryWriteBytes(span.Slice(offset), Movement.X); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), Movement.Y); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), LookYaw); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), LookPitch); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), (uint)Buttons); offset += 4;
            BitConverter.TryWriteBytes(span.Slice(offset), TargetId.Packed); offset += 8;
            BitConverter.TryWriteBytes(span.Slice(offset), SequenceNumber);

            return buffer;
        }
    }

    [Flags]
    public enum InputButtons : uint
    {
        None = 0,
        MoveForward = 1 << 0,
        MoveBack = 1 << 1,
        MoveLeft = 1 << 2,
        MoveRight = 1 << 3,
        Jump = 1 << 4,
        Crouch = 1 << 5,
        Sprint = 1 << 6,
        Attack = 1 << 7,
        Block = 1 << 8,
        Interact = 1 << 9,
        Inventory = 1 << 10,
        UseItem = 1 << 11,
        DropItem = 1 << 12
    }

    /// <summary>
    /// Spawn event schema.
    /// </summary>
    public class SpawnPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.SpawnEvent, 1);
        public override SchemaId SchemaId => _schemaId;

        public NetId EntityId { get; set; }
        public EntityKind EntityKind { get; set; }
        public string TemplateId { get; set; } = "";
        public FramedTransform SpawnTransform { get; set; }
        public NetId OwnerId { get; set; }
        public Dictionary<string, object>? InitialState { get; set; }

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }
    }

    /// <summary>
    /// Despawn event schema.
    /// </summary>
    public class DespawnPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.DespawnEvent, 1);
        public override SchemaId SchemaId => _schemaId;

        public NetId EntityId { get; set; }
        public string Reason { get; set; } = "";
        public bool Destroyed { get; set; }

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }
    }

    /// <summary>
    /// AI coarse state schema - for NPC AI synchronization.
    /// </summary>
    public class AIStatePayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.AICoarseState, 1);
        public override SchemaId SchemaId => _schemaId;

        public string CurrentBehavior { get; set; } = "";
        public NetId TargetId { get; set; }
        public Vector3 GoalPosition { get; set; }
        public string Alertness { get; set; } = ""; // Idle, Alert, Combat, Fleeing
        public Dictionary<string, float>? Drives { get; set; } // Hunger, Fatigue, etc.

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }
    }

    /// <summary>
    /// Inventory schema for inventory state.
    /// </summary>
    public class InventoryPayload : SchemaPayload
    {
        private static readonly SchemaId _schemaId = new SchemaId(SchemaKind.Inventory, 1);
        public override SchemaId SchemaId => _schemaId;

        public Dictionary<string, int> Items { get; set; } = new();
        public Dictionary<string, string> Equipment { get; set; } = new();
        public int Currency { get; set; }
        public int MaxWeight { get; set; }
        public int CurrentWeight { get; set; }

        public override byte[] Serialize()
        {
            return JsonSerializer.SerializeToUtf8Bytes(this);
        }
    }

    /// <summary>
    /// Registry of schema deserializers.
    /// </summary>
    public static class SchemaRegistry
    {
        private static readonly ConcurrentDictionary<SchemaId, Func<ReadOnlySpan<byte>, SchemaPayload>> _deserializers = new();

        static SchemaRegistry()
        {
            // Register built-in schemas
            Register(new SchemaId(SchemaKind.Transform, 1), data => TransformPayload.Deserialize(data));
            Register(new SchemaId(SchemaKind.Health, 1), data => HealthPayload.Deserialize(data));
            Register(new SchemaId(SchemaKind.CombatAction, 1), data => CombatActionPayload.Deserialize(data));
        }

        public static void Register(SchemaId schemaId, Func<ReadOnlySpan<byte>, SchemaPayload> deserializer)
        {
            _deserializers[schemaId] = deserializer;
        }

        public static SchemaPayload? Deserialize(SchemaId schemaId, ReadOnlySpan<byte> data)
        {
            if (_deserializers.TryGetValue(schemaId, out var deserializer))
                return deserializer(data);
            return null;
        }

        public static bool IsRegistered(SchemaId schemaId) => _deserializers.ContainsKey(schemaId);
    }

    /// <summary>
    /// Normalizes payloads to canonical form.
    /// This ensures that two equivalent states produce identical serialized output.
    /// </summary>
    public static class PayloadNormalizer
    {
        /// <summary>
        /// Normalize a transform payload (snap small values to zero, round positions).
        /// </summary>
        public static TransformPayload NormalizeTransform(TransformPayload payload, float positionPrecision = 0.001f)
        {
            var normalized = new TransformPayload
            {
                Position = new Vector3(
                    MathF.Round(payload.Position.X / positionPrecision) * positionPrecision,
                    MathF.Round(payload.Position.Y / positionPrecision) * positionPrecision,
                    MathF.Round(payload.Position.Z / positionPrecision) * positionPrecision),
                Rotation = Quaternion.Normalize(payload.Rotation),
                Velocity = SnapSmallToZero(payload.Velocity, 0.01f),
                Frame = payload.Frame
            };

            // Normalize quaternion to canonical form (positive W)
            if (normalized.Rotation.W < 0)
            {
                normalized.Rotation = new Quaternion(
                    -normalized.Rotation.X,
                    -normalized.Rotation.Y,
                    -normalized.Rotation.Z,
                    -normalized.Rotation.W);
            }

            return normalized;
        }

        private static Vector3 SnapSmallToZero(Vector3 v, float threshold)
        {
            return new Vector3(
                MathF.Abs(v.X) < threshold ? 0 : v.X,
                MathF.Abs(v.Y) < threshold ? 0 : v.Y,
                MathF.Abs(v.Z) < threshold ? 0 : v.Z);
        }
    }
}

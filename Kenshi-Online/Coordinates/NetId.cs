using System;
using System.Runtime.InteropServices;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Entity types in the identity dimension.
    /// Each type occupies a separate index space.
    /// </summary>
    public enum EntityKind : byte
    {
        Invalid = 0,
        Player = 1,
        NPC = 2,
        Building = 3,
        Item = 4,
        Vehicle = 5,
        Projectile = 6,
        Effect = 7,
        Zone = 8,
        Trigger = 9
    }

    /// <summary>
    /// NetId - The Identity Dimension
    ///
    /// A stable identifier that is NOT a pointer. Memory addresses are
    /// "coordinates in memory" - they are not identity. NetId lifts
    /// entity references into a proper identity dimension.
    ///
    /// Structure: (Type, Index, Generation)
    /// - Type: What kind of entity (8 bits)
    /// - Index: Slot in the type's pool (24 bits, ~16M entities per type)
    /// - Generation: Reuse counter to prevent ABA bugs (32 bits)
    ///
    /// Total: 64 bits, fits in a register, comparable, hashable.
    ///
    /// The generation prevents the classic bug:
    ///   1. Entity A spawns at index 5
    ///   2. Entity A despawns, index 5 freed
    ///   3. Entity B spawns at index 5 (reuse)
    ///   4. Old reference to "index 5" now wrongly points to B
    ///
    /// With generation:
    ///   - A has NetId(Player, 5, gen=1)
    ///   - B has NetId(Player, 5, gen=2)
    ///   - Old references still hold gen=1, which != 2, so lookup fails safely
    /// </summary>
    [StructLayout(LayoutKind.Explicit)]
    public readonly struct NetId : IEquatable<NetId>, IComparable<NetId>
    {
        // Packed representation for efficient storage/transmission
        [FieldOffset(0)]
        private readonly ulong _packed;

        // Overlay fields for direct access
        [FieldOffset(0)]
        private readonly uint _indexAndType;
        [FieldOffset(4)]
        private readonly uint _generation;

        /// <summary>
        /// Invalid/null NetId constant.
        /// </summary>
        public static readonly NetId Invalid = default;

        /// <summary>
        /// The entity kind (type dimension).
        /// </summary>
        public EntityKind Kind => (EntityKind)(_indexAndType >> 24);

        /// <summary>
        /// The slot index within the entity kind's pool.
        /// </summary>
        public int Index => (int)(_indexAndType & 0x00FFFFFF);

        /// <summary>
        /// The generation counter. Increments each time the slot is reused.
        /// </summary>
        public uint Generation => _generation;

        /// <summary>
        /// The raw packed 64-bit value for serialization.
        /// </summary>
        public ulong Packed => _packed;

        /// <summary>
        /// True if this is a valid NetId (not default/invalid).
        /// </summary>
        public bool IsValid => Kind != EntityKind.Invalid && _generation > 0;

        /// <summary>
        /// Create a new NetId.
        /// </summary>
        public NetId(EntityKind kind, int index, uint generation)
        {
            _packed = 0; // Required for struct overlay
            _indexAndType = ((uint)kind << 24) | ((uint)index & 0x00FFFFFF);
            _generation = generation;
        }

        /// <summary>
        /// Create from packed representation (for deserialization).
        /// </summary>
        public NetId(ulong packed)
        {
            _indexAndType = 0;
            _generation = 0;
            _packed = packed;
        }

        /// <summary>
        /// Create the next generation of this NetId (for slot reuse).
        /// </summary>
        public NetId NextGeneration()
        {
            return new NetId(Kind, Index, _generation + 1);
        }

        /// <summary>
        /// Check if this NetId refers to the same slot (ignoring generation).
        /// Used when you want to know if two IDs are "the same entity slot"
        /// even if one is stale.
        /// </summary>
        public bool SameSlot(NetId other)
        {
            return Kind == other.Kind && Index == other.Index;
        }

        /// <summary>
        /// Check if this NetId is newer than another (higher generation).
        /// Only valid to compare if SameSlot() is true.
        /// </summary>
        public bool IsNewerThan(NetId other)
        {
            return SameSlot(other) && _generation > other._generation;
        }

        #region Equality and Comparison

        public bool Equals(NetId other) => _packed == other._packed;
        public override bool Equals(object? obj) => obj is NetId other && Equals(other);
        public override int GetHashCode() => _packed.GetHashCode();
        public int CompareTo(NetId other) => _packed.CompareTo(other._packed);

        public static bool operator ==(NetId left, NetId right) => left.Equals(right);
        public static bool operator !=(NetId left, NetId right) => !left.Equals(right);
        public static bool operator <(NetId left, NetId right) => left._packed < right._packed;
        public static bool operator >(NetId left, NetId right) => left._packed > right._packed;
        public static bool operator <=(NetId left, NetId right) => left._packed <= right._packed;
        public static bool operator >=(NetId left, NetId right) => left._packed >= right._packed;

        #endregion

        public override string ToString()
        {
            if (!IsValid) return "NetId(Invalid)";
            return $"NetId({Kind}:{Index}@gen{_generation})";
        }

        /// <summary>
        /// Short string for logging (e.g., "P:42@3" for Player index 42 gen 3).
        /// </summary>
        public string ToShortString()
        {
            if (!IsValid) return "?";
            char typeChar = Kind switch
            {
                EntityKind.Player => 'P',
                EntityKind.NPC => 'N',
                EntityKind.Building => 'B',
                EntityKind.Item => 'I',
                EntityKind.Vehicle => 'V',
                EntityKind.Projectile => 'X',
                EntityKind.Effect => 'E',
                EntityKind.Zone => 'Z',
                EntityKind.Trigger => 'T',
                _ => '?'
            };
            return $"{typeChar}:{Index}@{_generation}";
        }
    }

    /// <summary>
    /// Manages NetId allocation for a single EntityKind.
    /// Thread-safe via interlocked operations.
    /// </summary>
    public class NetIdAllocator
    {
        private readonly EntityKind _kind;
        private readonly uint[] _generations;
        private readonly bool[] _alive;
        private readonly object _lock = new();
        private int _nextFreeIndex;
        private int _freeCount;
        private readonly int[] _freeList;

        public EntityKind Kind => _kind;
        public int Capacity { get; }
        public int AliveCount => Capacity - _freeCount;

        public NetIdAllocator(EntityKind kind, int capacity)
        {
            _kind = kind;
            Capacity = capacity;
            _generations = new uint[capacity];
            _alive = new bool[capacity];
            _freeList = new int[capacity];
            _freeCount = capacity;

            // Initialize free list
            for (int i = 0; i < capacity; i++)
            {
                _freeList[i] = i;
                _generations[i] = 1; // Start at gen 1 so gen 0 is always invalid
            }
        }

        /// <summary>
        /// Allocate a new NetId from this pool.
        /// Returns Invalid if pool is exhausted.
        /// </summary>
        public NetId Allocate()
        {
            lock (_lock)
            {
                if (_freeCount == 0)
                    return NetId.Invalid;

                int index = _freeList[--_freeCount];
                _alive[index] = true;
                return new NetId(_kind, index, _generations[index]);
            }
        }

        /// <summary>
        /// Free a NetId, making its slot available for reuse.
        /// Increments generation to invalidate old references.
        /// Returns false if the NetId was already freed or invalid.
        /// </summary>
        public bool Free(NetId id)
        {
            if (id.Kind != _kind) return false;
            int index = id.Index;
            if (index < 0 || index >= Capacity) return false;

            lock (_lock)
            {
                if (!_alive[index]) return false;
                if (_generations[index] != id.Generation) return false; // Stale reference

                _alive[index] = false;
                _generations[index]++; // Increment generation for next allocation
                _freeList[_freeCount++] = index;
                return true;
            }
        }

        /// <summary>
        /// Check if a NetId is currently valid (allocated and not freed).
        /// </summary>
        public bool IsAlive(NetId id)
        {
            if (id.Kind != _kind) return false;
            int index = id.Index;
            if (index < 0 || index >= Capacity) return false;

            lock (_lock)
            {
                return _alive[index] && _generations[index] == id.Generation;
            }
        }

        /// <summary>
        /// Get the current generation for a slot (even if freed).
        /// Useful for debugging stale references.
        /// </summary>
        public uint GetCurrentGeneration(int index)
        {
            if (index < 0 || index >= Capacity) return 0;
            lock (_lock)
            {
                return _generations[index];
            }
        }
    }

    /// <summary>
    /// Central registry for all NetId allocators.
    /// One allocator per EntityKind.
    /// </summary>
    public class NetIdRegistry
    {
        private readonly NetIdAllocator[] _allocators;

        public NetIdRegistry(int playersCapacity = 1024,
                            int npcsCapacity = 16384,
                            int buildingsCapacity = 8192,
                            int itemsCapacity = 65536,
                            int vehiclesCapacity = 1024,
                            int projectilesCapacity = 4096,
                            int effectsCapacity = 4096,
                            int zonesCapacity = 256,
                            int triggersCapacity = 1024)
        {
            _allocators = new NetIdAllocator[10]; // EntityKind values 0-9
            _allocators[(int)EntityKind.Player] = new NetIdAllocator(EntityKind.Player, playersCapacity);
            _allocators[(int)EntityKind.NPC] = new NetIdAllocator(EntityKind.NPC, npcsCapacity);
            _allocators[(int)EntityKind.Building] = new NetIdAllocator(EntityKind.Building, buildingsCapacity);
            _allocators[(int)EntityKind.Item] = new NetIdAllocator(EntityKind.Item, itemsCapacity);
            _allocators[(int)EntityKind.Vehicle] = new NetIdAllocator(EntityKind.Vehicle, vehiclesCapacity);
            _allocators[(int)EntityKind.Projectile] = new NetIdAllocator(EntityKind.Projectile, projectilesCapacity);
            _allocators[(int)EntityKind.Effect] = new NetIdAllocator(EntityKind.Effect, effectsCapacity);
            _allocators[(int)EntityKind.Zone] = new NetIdAllocator(EntityKind.Zone, zonesCapacity);
            _allocators[(int)EntityKind.Trigger] = new NetIdAllocator(EntityKind.Trigger, triggersCapacity);
        }

        public NetId Allocate(EntityKind kind)
        {
            var allocator = GetAllocator(kind);
            return allocator?.Allocate() ?? NetId.Invalid;
        }

        public bool Free(NetId id)
        {
            var allocator = GetAllocator(id.Kind);
            return allocator?.Free(id) ?? false;
        }

        public bool IsAlive(NetId id)
        {
            var allocator = GetAllocator(id.Kind);
            return allocator?.IsAlive(id) ?? false;
        }

        public NetIdAllocator? GetAllocator(EntityKind kind)
        {
            int index = (int)kind;
            if (index <= 0 || index >= _allocators.Length)
                return null;
            return _allocators[index];
        }

        /// <summary>
        /// Get statistics for all allocators.
        /// </summary>
        public (EntityKind Kind, int Alive, int Capacity)[] GetStats()
        {
            var stats = new (EntityKind, int, int)[_allocators.Length - 1];
            for (int i = 1; i < _allocators.Length; i++)
            {
                var alloc = _allocators[i];
                if (alloc != null)
                    stats[i - 1] = (alloc.Kind, alloc.AliveCount, alloc.Capacity);
            }
            return stats;
        }
    }
}

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Ring 1: Container Ring (Identity + Space + Ownership)
    ///
    /// This is where you convert "pointers" into "IDs."
    ///
    /// Invariant: a container entry is valid only if it is:
    ///   - bound to a stable NetId
    ///   - tied to a "type signature" (entity kind)
    ///   - associated with a current "generation" (to prevent reuse bugs)
    ///
    /// This ring is your "ontology": what exists and what it is.
    /// </summary>
    public class ContainerRing
    {
        private readonly ConcurrentDictionary<NetId, ContainerEntry> _entries = new();
        private readonly NetIdRegistry _netIdRegistry;
        private readonly AuthorityTracker _authorityTracker;

        // Ring buffer for recent changes (for iteration/replication)
        private readonly ContainerEvent[] _eventBuffer;
        private long _eventHead;
        private readonly int _eventCapacity;
        private readonly object _eventLock = new();

        public ContainerRing(NetIdRegistry netIdRegistry, AuthorityTracker authorityTracker, int eventCapacity = 4096)
        {
            _netIdRegistry = netIdRegistry;
            _authorityTracker = authorityTracker;
            _eventCapacity = eventCapacity;
            _eventBuffer = new ContainerEvent[eventCapacity];
        }

        /// <summary>
        /// Register a new entity in the container ring.
        /// </summary>
        public NetId Register(
            EntityKind kind,
            IntPtr memoryHandle,
            SpaceFrame frame,
            AuthorityCoordinate authority,
            long spawnTick,
            string? templateId = null)
        {
            // Allocate NetId
            NetId netId = _netIdRegistry.Allocate(kind);
            if (!netId.IsValid)
                return NetId.Invalid;

            var entry = new ContainerEntry
            {
                NetId = netId,
                Kind = kind,
                MemoryHandle = memoryHandle,
                Frame = frame,
                Authority = authority,
                SpawnTick = spawnTick,
                LastUpdateTick = spawnTick,
                Alive = true,
                TemplateId = templateId ?? ""
            };

            if (!_entries.TryAdd(netId, entry))
            {
                _netIdRegistry.Free(netId);
                return NetId.Invalid;
            }

            // Set up authority tracking
            _authorityTracker.GetOrCreate(netId).SetAuthority(authority.Scope, authority);

            // Record spawn event
            RecordEvent(new ContainerEvent
            {
                Type = ContainerEventType.Spawn,
                EntityId = netId,
                Tick = spawnTick,
                Entry = entry
            });

            return netId;
        }

        /// <summary>
        /// Unregister an entity (despawn).
        /// </summary>
        public bool Unregister(NetId netId, long despawnTick, string reason = "")
        {
            if (!_entries.TryRemove(netId, out var entry))
                return false;

            entry.Alive = false;
            entry.DespawnTick = despawnTick;

            // Free the NetId (increments generation)
            _netIdRegistry.Free(netId);

            // Remove authority tracking
            _authorityTracker.Remove(netId);

            // Record despawn event
            RecordEvent(new ContainerEvent
            {
                Type = ContainerEventType.Despawn,
                EntityId = netId,
                Tick = despawnTick,
                Reason = reason
            });

            return true;
        }

        /// <summary>
        /// Get an entry by NetId.
        /// </summary>
        public ContainerEntry? Get(NetId netId)
        {
            return _entries.TryGetValue(netId, out var entry) ? entry : null;
        }

        /// <summary>
        /// Check if an entity exists and is alive.
        /// </summary>
        public bool IsAlive(NetId netId)
        {
            return _entries.TryGetValue(netId, out var entry) && entry.Alive;
        }

        /// <summary>
        /// Update the memory handle for an entity (e.g., after reallocation).
        /// </summary>
        public bool UpdateHandle(NetId netId, IntPtr newHandle, long tick)
        {
            if (!_entries.TryGetValue(netId, out var entry))
                return false;

            var oldHandle = entry.MemoryHandle;
            entry.MemoryHandle = newHandle;
            entry.LastUpdateTick = tick;

            RecordEvent(new ContainerEvent
            {
                Type = ContainerEventType.HandleChange,
                EntityId = netId,
                Tick = tick,
                OldHandle = oldHandle,
                NewHandle = newHandle
            });

            return true;
        }

        /// <summary>
        /// Update the reference frame for an entity.
        /// </summary>
        public bool UpdateFrame(NetId netId, SpaceFrame newFrame, long tick)
        {
            if (!_entries.TryGetValue(netId, out var entry))
                return false;

            entry.Frame = newFrame;
            entry.LastUpdateTick = tick;

            RecordEvent(new ContainerEvent
            {
                Type = ContainerEventType.FrameChange,
                EntityId = netId,
                Tick = tick
            });

            return true;
        }

        /// <summary>
        /// Transfer ownership/authority for an entity.
        /// </summary>
        public bool TransferAuthority(NetId netId, AuthorityCoordinate newAuthority, long tick)
        {
            if (!_entries.TryGetValue(netId, out var entry))
                return false;

            var oldAuthority = entry.Authority;

            // Try to transfer in authority tracker
            var entityAuth = _authorityTracker.GetOrCreate(netId);
            if (!entityAuth.TransferAuthority(newAuthority.Scope, newAuthority))
                return false;

            entry.Authority = newAuthority;
            entry.LastUpdateTick = tick;

            RecordEvent(new ContainerEvent
            {
                Type = ContainerEventType.AuthorityTransfer,
                EntityId = netId,
                Tick = tick,
                OldAuthority = oldAuthority,
                NewAuthority = newAuthority
            });

            return true;
        }

        /// <summary>
        /// Get all entities of a specific kind.
        /// </summary>
        public IEnumerable<ContainerEntry> GetByKind(EntityKind kind)
        {
            foreach (var kvp in _entries)
            {
                if (kvp.Value.Kind == kind && kvp.Value.Alive)
                    yield return kvp.Value;
            }
        }

        /// <summary>
        /// Get all alive entities.
        /// </summary>
        public IEnumerable<ContainerEntry> GetAll()
        {
            foreach (var kvp in _entries)
            {
                if (kvp.Value.Alive)
                    yield return kvp.Value;
            }
        }

        /// <summary>
        /// Get entities owned by a specific authority.
        /// </summary>
        public IEnumerable<ContainerEntry> GetByOwner(AuthorityOwner owner, NetId? ownerId = null)
        {
            foreach (var kvp in _entries)
            {
                if (!kvp.Value.Alive) continue;
                if (kvp.Value.Authority.Owner != owner) continue;
                if (ownerId.HasValue && kvp.Value.Authority.OwnerId != ownerId.Value) continue;
                yield return kvp.Value;
            }
        }

        /// <summary>
        /// Get the count of alive entities.
        /// </summary>
        public int Count => _entries.Count;

        /// <summary>
        /// Get counts by entity kind.
        /// </summary>
        public Dictionary<EntityKind, int> GetCountsByKind()
        {
            var counts = new Dictionary<EntityKind, int>();
            foreach (var kvp in _entries)
            {
                if (!kvp.Value.Alive) continue;
                var kind = kvp.Value.Kind;
                counts[kind] = counts.GetValueOrDefault(kind, 0) + 1;
            }
            return counts;
        }

        #region Event Buffer

        private void RecordEvent(ContainerEvent evt)
        {
            lock (_eventLock)
            {
                int index = (int)(_eventHead % _eventCapacity);
                _eventBuffer[index] = evt;
                Interlocked.Increment(ref _eventHead);
            }
        }

        /// <summary>
        /// Get events since a given event index.
        /// </summary>
        public IEnumerable<ContainerEvent> GetEventsSince(long fromIndex)
        {
            long head = Interlocked.Read(ref _eventHead);
            long start = Math.Max(fromIndex, head - _eventCapacity);

            for (long i = start; i < head; i++)
            {
                int index = (int)(i % _eventCapacity);
                yield return _eventBuffer[index];
            }
        }

        /// <summary>
        /// Get the current event head index.
        /// </summary>
        public long EventHead => Interlocked.Read(ref _eventHead);

        #endregion

        #region Sanity Checks

        /// <summary>
        /// Validate all entries for consistency.
        /// </summary>
        public IEnumerable<string> ValidateEntries()
        {
            foreach (var kvp in _entries)
            {
                var entry = kvp.Value;

                // Check NetId validity
                if (!_netIdRegistry.IsAlive(entry.NetId))
                    yield return $"{entry.NetId}: NetId not alive in registry";

                // Check authority consistency
                var auth = _authorityTracker.GetOrCreate(entry.NetId).GetAuthority(entry.Authority.Scope);
                if (!auth.HasValue || auth.Value.Epoch != entry.Authority.Epoch)
                    yield return $"{entry.NetId}: Authority mismatch in tracker";

                // Check memory handle (non-null for alive entities)
                if (entry.Alive && entry.MemoryHandle == IntPtr.Zero)
                    yield return $"{entry.NetId}: Alive entity has null memory handle";
            }
        }

        #endregion
    }

    /// <summary>
    /// An entry in the Container Ring.
    /// Represents the "ontological fact" of an entity's existence.
    /// </summary>
    public class ContainerEntry
    {
        /// <summary>
        /// The stable network identity.
        /// </summary>
        public NetId NetId { get; set; }

        /// <summary>
        /// What kind of entity this is.
        /// </summary>
        public EntityKind Kind { get; set; }

        /// <summary>
        /// Current memory location (pointer to game object).
        /// This is implementation detail, not identity.
        /// </summary>
        public IntPtr MemoryHandle { get; set; }

        /// <summary>
        /// The reference frame for this entity's transform.
        /// </summary>
        public SpaceFrame Frame { get; set; }

        /// <summary>
        /// Who has authority over this entity.
        /// </summary>
        public AuthorityCoordinate Authority { get; set; }

        /// <summary>
        /// When this entity spawned.
        /// </summary>
        public long SpawnTick { get; set; }

        /// <summary>
        /// When this entity despawned (0 if still alive).
        /// </summary>
        public long DespawnTick { get; set; }

        /// <summary>
        /// When this entry was last updated.
        /// </summary>
        public long LastUpdateTick { get; set; }

        /// <summary>
        /// Is this entity currently alive?
        /// </summary>
        public bool Alive { get; set; }

        /// <summary>
        /// Template/prefab ID for spawning.
        /// </summary>
        public string TemplateId { get; set; } = "";

        /// <summary>
        /// Lifetime in ticks.
        /// </summary>
        public long LifetimeTicks => Alive ? 0 : DespawnTick - SpawnTick;

        /// <summary>
        /// Answer: Who is it?
        /// </summary>
        public string WhoIsIt => NetId.ToString();

        public override string ToString()
        {
            return $"Container({NetId.ToShortString()} {Kind} {(Alive ? "alive" : "dead")} @{Frame.Type})";
        }
    }

    /// <summary>
    /// Event types in the container ring.
    /// </summary>
    public enum ContainerEventType : byte
    {
        Spawn,
        Despawn,
        HandleChange,
        FrameChange,
        AuthorityTransfer
    }

    /// <summary>
    /// An event in the container ring's history.
    /// </summary>
    public struct ContainerEvent
    {
        public ContainerEventType Type;
        public NetId EntityId;
        public long Tick;
        public ContainerEntry? Entry;
        public string Reason;
        public IntPtr OldHandle;
        public IntPtr NewHandle;
        public AuthorityCoordinate OldAuthority;
        public AuthorityCoordinate NewAuthority;
    }
}

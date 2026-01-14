using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// Who is allowed to make authoritative decisions.
    /// </summary>
    public enum AuthorityOwner : byte
    {
        /// <summary>No authority - invalid state.</summary>
        None = 0,

        /// <summary>Server has exclusive authority.</summary>
        Server = 1,

        /// <summary>A specific client has authority (for player-owned entities).</summary>
        Client = 2,

        /// <summary>Host in peer-to-peer topology.</summary>
        Host = 3,

        /// <summary>A game subsystem has temporary authority (e.g., physics, AI).</summary>
        Subsystem = 4,

        /// <summary>Authority is shared/contested (requires consensus).</summary>
        Shared = 5
    }

    /// <summary>
    /// Which fields/properties the authority owner controls.
    /// Using flags allows combining scopes.
    /// </summary>
    [Flags]
    public enum AuthorityScope : uint
    {
        None = 0,

        // Transform fields (Tier 0 - Transient)
        Position = 1 << 0,
        Rotation = 1 << 1,
        Velocity = 1 << 2,
        Transform = Position | Rotation | Velocity,

        // Animation/Visual (Tier 0 - Transient)
        Animation = 1 << 3,
        Visual = 1 << 4,

        // Combat (Tier 1 - Event)
        CombatAction = 1 << 5,
        Targeting = 1 << 6,
        Combat = CombatAction | Targeting,

        // Status (Tier 1/2)
        Health = 1 << 7,
        Status = 1 << 8,
        LimbHealth = 1 << 9,

        // Inventory (Tier 2 - Persistent)
        Inventory = 1 << 10,
        Equipment = 1 << 11,
        Currency = 1 << 12,
        Items = Inventory | Equipment | Currency,

        // Progression (Tier 2 - Persistent)
        Skills = 1 << 13,
        Experience = 1 << 14,
        Progression = Skills | Experience,

        // AI/Behavior (varies)
        AIState = 1 << 15,
        Behavior = 1 << 16,
        AI = AIState | Behavior,

        // Ownership/Relations
        Ownership = 1 << 17,
        Faction = 1 << 18,

        // Existence
        Spawn = 1 << 19,
        Despawn = 1 << 20,
        Existence = Spawn | Despawn,

        // Full authority
        All = 0xFFFFFFFF
    }

    /// <summary>
    /// AuthorityCoordinate - The Authority Dimension
    ///
    /// Even if "authority cycles," you still need a coordinate:
    ///   - authority_owner: server / host / client / subsystem
    ///   - authority_scope: which fields are owned (pos? health? inventory?)
    ///   - authority_epoch: which authority cycle version
    ///
    /// Without this, you'll get two writers fighting the same state.
    ///
    /// Rule: every field must have exactly one writer at any given epoch.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct AuthorityCoordinate : IEquatable<AuthorityCoordinate>
    {
        /// <summary>
        /// Who owns authority for this scope.
        /// </summary>
        public readonly AuthorityOwner Owner;

        /// <summary>
        /// Which fields this authority covers.
        /// </summary>
        public readonly AuthorityScope Scope;

        /// <summary>
        /// The epoch/cycle when this authority was granted.
        /// Higher epoch = more recent authority decision.
        /// Used to resolve conflicts when authority transfers.
        /// </summary>
        public readonly uint Epoch;

        /// <summary>
        /// The specific owner ID (client ID, subsystem ID, etc.).
        /// Only meaningful when Owner != Server.
        /// </summary>
        public readonly NetId OwnerId;

        /// <summary>
        /// Tick when this authority was established.
        /// </summary>
        public readonly long GrantedAtTick;

        /// <summary>
        /// Tick when this authority expires (0 = never).
        /// </summary>
        public readonly long ExpiresAtTick;

        public static readonly AuthorityCoordinate ServerFull = new AuthorityCoordinate(
            AuthorityOwner.Server, AuthorityScope.All, 0, NetId.Invalid, 0, 0);

        public AuthorityCoordinate(
            AuthorityOwner owner,
            AuthorityScope scope,
            uint epoch,
            NetId ownerId,
            long grantedAtTick,
            long expiresAtTick = 0)
        {
            Owner = owner;
            Scope = scope;
            Epoch = epoch;
            OwnerId = ownerId;
            GrantedAtTick = grantedAtTick;
            ExpiresAtTick = expiresAtTick;
        }

        /// <summary>
        /// Create server authority for specific scope.
        /// </summary>
        public static AuthorityCoordinate Server(AuthorityScope scope, uint epoch, long tick)
        {
            return new AuthorityCoordinate(AuthorityOwner.Server, scope, epoch, NetId.Invalid, tick);
        }

        /// <summary>
        /// Create client authority for specific scope.
        /// </summary>
        public static AuthorityCoordinate Client(NetId clientId, AuthorityScope scope, uint epoch, long tick)
        {
            return new AuthorityCoordinate(AuthorityOwner.Client, scope, epoch, clientId, tick);
        }

        /// <summary>
        /// Check if this authority covers a specific scope.
        /// </summary>
        public bool HasScope(AuthorityScope check) => (Scope & check) == check;

        /// <summary>
        /// Check if this authority covers any of the specified scopes.
        /// </summary>
        public bool HasAnyScope(AuthorityScope check) => (Scope & check) != 0;

        /// <summary>
        /// Check if this authority is valid at the given tick.
        /// </summary>
        public bool IsValidAt(long tick)
        {
            if (tick < GrantedAtTick) return false;
            if (ExpiresAtTick > 0 && tick > ExpiresAtTick) return false;
            return true;
        }

        /// <summary>
        /// Check if this authority supersedes another (higher epoch).
        /// </summary>
        public bool Supersedes(AuthorityCoordinate other)
        {
            return Epoch > other.Epoch;
        }

        /// <summary>
        /// Create a new authority with incremented epoch.
        /// </summary>
        public AuthorityCoordinate NextEpoch(long tick)
        {
            return new AuthorityCoordinate(Owner, Scope, Epoch + 1, OwnerId, tick, ExpiresAtTick);
        }

        /// <summary>
        /// Create a new authority with reduced scope.
        /// </summary>
        public AuthorityCoordinate WithScope(AuthorityScope newScope)
        {
            return new AuthorityCoordinate(Owner, newScope, Epoch, OwnerId, GrantedAtTick, ExpiresAtTick);
        }

        /// <summary>
        /// Create a new authority that expires at a specific tick.
        /// </summary>
        public AuthorityCoordinate ExpiringAt(long tick)
        {
            return new AuthorityCoordinate(Owner, Scope, Epoch, OwnerId, GrantedAtTick, tick);
        }

        public bool Equals(AuthorityCoordinate other) =>
            Owner == other.Owner &&
            Scope == other.Scope &&
            Epoch == other.Epoch &&
            OwnerId == other.OwnerId;

        public override bool Equals(object? obj) => obj is AuthorityCoordinate other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Owner, Scope, Epoch, OwnerId);

        public static bool operator ==(AuthorityCoordinate left, AuthorityCoordinate right) => left.Equals(right);
        public static bool operator !=(AuthorityCoordinate left, AuthorityCoordinate right) => !left.Equals(right);

        public override string ToString()
        {
            string ownerStr = Owner switch
            {
                AuthorityOwner.Server => "Server",
                AuthorityOwner.Client => $"Client({OwnerId.ToShortString()})",
                AuthorityOwner.Host => "Host",
                AuthorityOwner.Subsystem => $"Subsystem({OwnerId.ToShortString()})",
                AuthorityOwner.Shared => "Shared",
                _ => "None"
            };
            return $"Auth({ownerStr}:{Scope}@E{Epoch})";
        }
    }

    /// <summary>
    /// Tracks authority for all fields of an entity.
    /// Allows different owners to have authority over different fields.
    /// </summary>
    public class EntityAuthority
    {
        public NetId EntityId { get; }

        // Authority by scope - each scope can have different authority
        private readonly Dictionary<AuthorityScope, AuthorityCoordinate> _scopeAuthority;
        private readonly object _lock = new();

        public EntityAuthority(NetId entityId)
        {
            EntityId = entityId;
            _scopeAuthority = new Dictionary<AuthorityScope, AuthorityCoordinate>();
        }

        /// <summary>
        /// Set authority for a specific scope.
        /// </summary>
        public void SetAuthority(AuthorityScope scope, AuthorityCoordinate authority)
        {
            lock (_lock)
            {
                _scopeAuthority[scope] = authority;
            }
        }

        /// <summary>
        /// Get authority for a specific scope.
        /// </summary>
        public AuthorityCoordinate? GetAuthority(AuthorityScope scope)
        {
            lock (_lock)
            {
                // Check for exact match first
                if (_scopeAuthority.TryGetValue(scope, out var auth))
                    return auth;

                // Check for containing authority
                foreach (var kvp in _scopeAuthority)
                {
                    if ((kvp.Key & scope) == scope)
                        return kvp.Value;
                }

                return null;
            }
        }

        /// <summary>
        /// Check if an owner can write to a specific scope at a given tick.
        /// </summary>
        public bool CanWrite(AuthorityOwner owner, NetId ownerId, AuthorityScope scope, long tick)
        {
            var auth = GetAuthority(scope);
            if (!auth.HasValue) return false;
            if (!auth.Value.IsValidAt(tick)) return false;
            if (auth.Value.Owner != owner) return false;
            if (owner == AuthorityOwner.Client && auth.Value.OwnerId != ownerId) return false;
            return true;
        }

        /// <summary>
        /// Transfer authority for a scope to a new owner.
        /// Returns false if the transfer fails (e.g., due to epoch conflict).
        /// </summary>
        public bool TransferAuthority(AuthorityScope scope, AuthorityCoordinate newAuthority)
        {
            lock (_lock)
            {
                if (_scopeAuthority.TryGetValue(scope, out var existing))
                {
                    // Only allow transfer if new epoch is higher
                    if (newAuthority.Epoch <= existing.Epoch)
                        return false;
                }
                _scopeAuthority[scope] = newAuthority;
                return true;
            }
        }

        /// <summary>
        /// Get all current authority assignments.
        /// </summary>
        public IReadOnlyDictionary<AuthorityScope, AuthorityCoordinate> GetAllAuthority()
        {
            lock (_lock)
            {
                return new Dictionary<AuthorityScope, AuthorityCoordinate>(_scopeAuthority);
            }
        }
    }

    /// <summary>
    /// Central authority tracker for all entities.
    /// </summary>
    public class AuthorityTracker
    {
        private readonly ConcurrentDictionary<NetId, EntityAuthority> _entities = new();
        private uint _globalEpoch;
        private readonly object _epochLock = new();

        /// <summary>
        /// Get or create authority tracking for an entity.
        /// </summary>
        public EntityAuthority GetOrCreate(NetId entityId)
        {
            return _entities.GetOrAdd(entityId, id => new EntityAuthority(id));
        }

        /// <summary>
        /// Remove authority tracking for an entity (on despawn).
        /// </summary>
        public bool Remove(NetId entityId)
        {
            return _entities.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Get the next global epoch number.
        /// </summary>
        public uint NextEpoch()
        {
            lock (_epochLock)
            {
                return ++_globalEpoch;
            }
        }

        /// <summary>
        /// Grant full authority to server for an entity.
        /// </summary>
        public void GrantServerAuthority(NetId entityId, long tick)
        {
            var auth = GetOrCreate(entityId);
            var epoch = NextEpoch();
            auth.SetAuthority(AuthorityScope.All, AuthorityCoordinate.Server(AuthorityScope.All, epoch, tick));
        }

        /// <summary>
        /// Grant client authority for specific scopes.
        /// </summary>
        public void GrantClientAuthority(NetId entityId, NetId clientId, AuthorityScope scope, long tick)
        {
            var auth = GetOrCreate(entityId);
            var epoch = NextEpoch();
            auth.SetAuthority(scope, AuthorityCoordinate.Client(clientId, scope, epoch, tick));
        }

        /// <summary>
        /// Check if a write is authorized.
        /// </summary>
        public bool IsAuthorized(NetId entityId, AuthorityOwner owner, NetId ownerId, AuthorityScope scope, long tick)
        {
            if (!_entities.TryGetValue(entityId, out var auth))
                return false;
            return auth.CanWrite(owner, ownerId, scope, tick);
        }
    }
}

using System;
using System.Collections.Generic;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Entity types that can be synchronized.
    /// Kenshi has many entity types; we only sync what matters for multiplayer.
    /// </summary>
    public enum SyncEntityType
    {
        Player,     // Player-controlled characters
        NPC,        // Non-player characters (AI-controlled)
        Item,       // World items (dropped, containers)
        Building,   // Structures, walls, machines
        Squad       // Group of characters
    }

    /// <summary>
    /// EntityState is the minimal sync unit.
    /// This is what we ACTUALLY sync - not all Kenshi state.
    ///
    /// Design principle: Sync the minimum needed for gameplay.
    /// More sync = more bandwidth = more desync opportunities.
    /// </summary>
    public class EntityState
    {
        /// <summary>
        /// Unique identifier for this entity.
        /// Format: "{type}_{uuid}" e.g., "player_abc123", "npc_def456"
        /// </summary>
        public string EntityId { get; set; }

        /// <summary>
        /// What kind of entity this is.
        /// </summary>
        public SyncEntityType Type { get; set; }

        /// <summary>
        /// Display name (character name, item name, etc.)
        /// </summary>
        public string Name { get; set; }

        // ============================================
        // POSITION - Always synced for all entities
        // ============================================

        /// <summary>X coordinate in world space</summary>
        public float X { get; set; }

        /// <summary>Y coordinate in world space (height)</summary>
        public float Y { get; set; }

        /// <summary>Z coordinate in world space</summary>
        public float Z { get; set; }

        /// <summary>Rotation around Y axis (facing direction)</summary>
        public float RotationY { get; set; }

        // ============================================
        // HEALTH - Synced for characters
        // ============================================

        /// <summary>Current health</summary>
        public float Health { get; set; }

        /// <summary>Maximum health</summary>
        public float MaxHealth { get; set; }

        /// <summary>Is this entity dead?</summary>
        public bool IsDead => Health <= 0;

        // ============================================
        // OWNERSHIP - Who controls this entity
        // ============================================

        /// <summary>
        /// Player ID who owns this entity, or "SERVER" for server-controlled.
        /// Players own their characters.
        /// Server owns NPCs, world items, buildings.
        /// </summary>
        public string OwnerId { get; set; }

        /// <summary>
        /// Is this entity owned by the server?
        /// </summary>
        public bool IsServerOwned => OwnerId == "SERVER";

        // ============================================
        // TYPE-SPECIFIC DATA
        // ============================================

        /// <summary>
        /// Additional data specific to entity type.
        ///
        /// For Player:
        ///   - "Faction": string (faction ID)
        ///   - "Money": int
        ///   - "Hunger": float
        ///   - "Blood": float
        ///   - "EquippedWeapon": string (item ID)
        ///   - "EquippedArmor": string (item ID)
        ///
        /// For NPC:
        ///   - "Faction": string
        ///   - "IsHostile": bool
        ///   - "AIState": string (patrol, combat, flee)
        ///
        /// For Item:
        ///   - "ItemType": string
        ///   - "Quantity": int
        ///   - "ContainerId": string (if in container)
        ///
        /// For Building:
        ///   - "BuildingType": string
        ///   - "IsComplete": bool
        /// </summary>
        public Dictionary<string, object> Data { get; set; } = new();

        // ============================================
        // METADATA
        // ============================================

        /// <summary>Last tick this entity was updated</summary>
        public ulong LastUpdatedTick { get; set; }

        /// <summary>Unix timestamp of last update</summary>
        public long LastUpdatedTime { get; set; }

        /// <summary>
        /// Create an empty entity state.
        /// </summary>
        public EntityState() { }

        /// <summary>
        /// Create a player entity state.
        /// </summary>
        public static EntityState CreatePlayer(string playerId, string name, float x, float y, float z)
        {
            return new EntityState
            {
                EntityId = $"player_{playerId}",
                Type = SyncEntityType.Player,
                Name = name,
                X = x,
                Y = y,
                Z = z,
                Health = 100,
                MaxHealth = 100,
                OwnerId = playerId,
                LastUpdatedTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Create an NPC entity state.
        /// </summary>
        public static EntityState CreateNPC(string npcId, string name, float x, float y, float z, string faction)
        {
            return new EntityState
            {
                EntityId = $"npc_{npcId}",
                Type = SyncEntityType.NPC,
                Name = name,
                X = x,
                Y = y,
                Z = z,
                Health = 100,
                MaxHealth = 100,
                OwnerId = "SERVER",
                Data = new Dictionary<string, object>
                {
                    ["Faction"] = faction,
                    ["IsHostile"] = false,
                    ["AIState"] = "idle"
                },
                LastUpdatedTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Create an item entity state.
        /// </summary>
        public static EntityState CreateItem(string itemId, string itemType, float x, float y, float z, int quantity = 1)
        {
            return new EntityState
            {
                EntityId = $"item_{itemId}",
                Type = SyncEntityType.Item,
                Name = itemType,
                X = x,
                Y = y,
                Z = z,
                OwnerId = "SERVER",
                Data = new Dictionary<string, object>
                {
                    ["ItemType"] = itemType,
                    ["Quantity"] = quantity
                },
                LastUpdatedTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Get typed data value.
        /// </summary>
        public T GetData<T>(string key, T defaultValue = default)
        {
            if (Data.TryGetValue(key, out var value))
            {
                if (value is T typed)
                    return typed;

                // Handle JSON deserialization edge cases
                if (typeof(T) == typeof(int) && value is long longVal)
                    return (T)(object)(int)longVal;
                if (typeof(T) == typeof(float) && value is double doubleVal)
                    return (T)(object)(float)doubleVal;
            }
            return defaultValue;
        }

        /// <summary>
        /// Set typed data value.
        /// </summary>
        public void SetData<T>(string key, T value)
        {
            Data[key] = value;
        }

        /// <summary>
        /// Calculate distance to another entity.
        /// </summary>
        public float DistanceTo(EntityState other)
        {
            float dx = X - other.X;
            float dy = Y - other.Y;
            float dz = Z - other.Z;
            return (float)Math.Sqrt(dx * dx + dy * dy + dz * dz);
        }

        /// <summary>
        /// Calculate horizontal distance (ignoring Y).
        /// </summary>
        public float HorizontalDistanceTo(EntityState other)
        {
            float dx = X - other.X;
            float dz = Z - other.Z;
            return (float)Math.Sqrt(dx * dx + dz * dz);
        }

        /// <summary>
        /// Check if within range of another entity.
        /// </summary>
        public bool IsInRange(EntityState other, float range)
        {
            return DistanceTo(other) <= range;
        }

        /// <summary>
        /// Create a copy of this state.
        /// </summary>
        public EntityState Clone()
        {
            return new EntityState
            {
                EntityId = EntityId,
                Type = Type,
                Name = Name,
                X = X,
                Y = Y,
                Z = Z,
                RotationY = RotationY,
                Health = Health,
                MaxHealth = MaxHealth,
                OwnerId = OwnerId,
                Data = new Dictionary<string, object>(Data),
                LastUpdatedTick = LastUpdatedTick,
                LastUpdatedTime = LastUpdatedTime
            };
        }
    }

    /// <summary>
    /// Collection of entities for efficient lookup.
    /// </summary>
    public class EntityCollection
    {
        private readonly Dictionary<string, EntityState> _entities = new();
        private readonly Dictionary<string, HashSet<string>> _byOwner = new();
        private readonly Dictionary<SyncEntityType, HashSet<string>> _byType = new();
        private readonly object _lock = new();

        public int Count => _entities.Count;

        public void Add(EntityState entity)
        {
            lock (_lock)
            {
                _entities[entity.EntityId] = entity;

                // Index by owner
                if (!_byOwner.TryGetValue(entity.OwnerId, out var ownerSet))
                {
                    ownerSet = new HashSet<string>();
                    _byOwner[entity.OwnerId] = ownerSet;
                }
                ownerSet.Add(entity.EntityId);

                // Index by type
                if (!_byType.TryGetValue(entity.Type, out var typeSet))
                {
                    typeSet = new HashSet<string>();
                    _byType[entity.Type] = typeSet;
                }
                typeSet.Add(entity.EntityId);
            }
        }

        public EntityState Get(string entityId)
        {
            lock (_lock)
            {
                return _entities.TryGetValue(entityId, out var entity) ? entity : null;
            }
        }

        public void Remove(string entityId)
        {
            lock (_lock)
            {
                if (_entities.TryGetValue(entityId, out var entity))
                {
                    _entities.Remove(entityId);

                    if (_byOwner.TryGetValue(entity.OwnerId, out var ownerSet))
                        ownerSet.Remove(entityId);

                    if (_byType.TryGetValue(entity.Type, out var typeSet))
                        typeSet.Remove(entityId);
                }
            }
        }

        public IEnumerable<EntityState> GetByOwner(string ownerId)
        {
            lock (_lock)
            {
                if (_byOwner.TryGetValue(ownerId, out var ids))
                {
                    foreach (var id in ids)
                    {
                        if (_entities.TryGetValue(id, out var entity))
                            yield return entity;
                    }
                }
            }
        }

        public IEnumerable<EntityState> GetByType(SyncEntityType type)
        {
            lock (_lock)
            {
                if (_byType.TryGetValue(type, out var ids))
                {
                    foreach (var id in ids)
                    {
                        if (_entities.TryGetValue(id, out var entity))
                            yield return entity;
                    }
                }
            }
        }

        public IEnumerable<EntityState> GetAll()
        {
            lock (_lock)
            {
                return new List<EntityState>(_entities.Values);
            }
        }

        public IEnumerable<EntityState> GetInRange(float x, float z, float range)
        {
            lock (_lock)
            {
                foreach (var entity in _entities.Values)
                {
                    float dx = entity.X - x;
                    float dz = entity.Z - z;
                    if (dx * dx + dz * dz <= range * range)
                        yield return entity;
                }
            }
        }
    }
}

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using KenshiOnline.Core.Entities;

namespace KenshiOnline.Core.Synchronization
{
    /// <summary>
    /// Central entity registry and lifecycle manager
    /// Handles entity creation, destruction, and synchronization
    /// </summary>
    public class EntityManager
    {
        private readonly ConcurrentDictionary<Guid, Entity> _entities;
        private readonly ConcurrentDictionary<string, Guid> _playerIdToEntityId;
        private readonly object _lock = new object();

        // Entity update tracking
        private readonly ConcurrentQueue<Entity> _dirtyEntities;
        private readonly HashSet<Guid> _dirtyEntitySet;

        // Spatial optimization
        private readonly Dictionary<string, HashSet<Guid>> _spatialGrid;
        private const float GridCellSize = 100f;

        // Statistics
        public int TotalEntities => _entities.Count;
        public int PlayerCount => _entities.Values.Count(e => e.Type == EntityType.Player);
        public int NPCCount => _entities.Values.Count(e => e.Type == EntityType.NPC);
        public int ItemCount => _entities.Values.Count(e => e.Type == EntityType.Item);

        public EntityManager()
        {
            _entities = new ConcurrentDictionary<Guid, Entity>();
            _playerIdToEntityId = new ConcurrentDictionary<string, Guid>();
            _dirtyEntities = new ConcurrentQueue<Entity>();
            _dirtyEntitySet = new HashSet<Guid>();
            _spatialGrid = new Dictionary<string, HashSet<Guid>>();
        }

        #region Entity Lifecycle

        /// <summary>
        /// Register a new entity
        /// </summary>
        public bool RegisterEntity(Entity entity)
        {
            if (entity == null || entity.Id == Guid.Empty)
                return false;

            if (_entities.TryAdd(entity.Id, entity))
            {
                // Track player entities by player ID
                if (entity is PlayerEntity player && !string.IsNullOrEmpty(player.PlayerId))
                {
                    _playerIdToEntityId[player.PlayerId] = entity.Id;
                }

                // Add to spatial grid
                AddToSpatialGrid(entity);

                // Subscribe to dirty events
                entity.OnDirty += OnEntityDirty;

                return true;
            }

            return false;
        }

        /// <summary>
        /// Unregister an entity
        /// </summary>
        public bool UnregisterEntity(Guid entityId)
        {
            if (_entities.TryRemove(entityId, out var entity))
            {
                // Remove from player tracking
                if (entity is PlayerEntity player && !string.IsNullOrEmpty(player.PlayerId))
                {
                    _playerIdToEntityId.TryRemove(player.PlayerId, out _);
                }

                // Remove from spatial grid
                RemoveFromSpatialGrid(entity);

                // Unsubscribe from dirty events
                entity.OnDirty -= OnEntityDirty;

                return true;
            }

            return false;
        }

        /// <summary>
        /// Get entity by ID
        /// </summary>
        public Entity GetEntity(Guid entityId)
        {
            _entities.TryGetValue(entityId, out var entity);
            return entity;
        }

        /// <summary>
        /// Get entity by type
        /// </summary>
        public T GetEntity<T>(Guid entityId) where T : Entity
        {
            return GetEntity(entityId) as T;
        }

        /// <summary>
        /// Get player entity by player ID
        /// </summary>
        public PlayerEntity GetPlayerByPlayerId(string playerId)
        {
            if (_playerIdToEntityId.TryGetValue(playerId, out var entityId))
            {
                return GetEntity<PlayerEntity>(entityId);
            }
            return null;
        }

        /// <summary>
        /// Get all entities
        /// </summary>
        public IEnumerable<Entity> GetAllEntities()
        {
            return _entities.Values;
        }

        /// <summary>
        /// Get all entities of type
        /// </summary>
        public IEnumerable<T> GetEntitiesOfType<T>() where T : Entity
        {
            return _entities.Values.OfType<T>();
        }

        #endregion

        #region Spatial Queries

        /// <summary>
        /// Get entities in radius
        /// </summary>
        public IEnumerable<Entity> GetEntitiesInRadius(Vector3 position, float radius)
        {
            var results = new List<Entity>();
            var radiusSquared = radius * radius;

            // Get cells that might contain entities in radius
            var minCell = GetGridCell(new Vector3(position.X - radius, position.Y - radius, position.Z - radius));
            var maxCell = GetGridCell(new Vector3(position.X + radius, position.Y + radius, position.Z + radius));

            for (int x = minCell.Item1; x <= maxCell.Item1; x++)
            {
                for (int y = minCell.Item2; y <= maxCell.Item2; y++)
                {
                    for (int z = minCell.Item3; z <= maxCell.Item3; z++)
                    {
                        var cellKey = GetGridKey(x, y, z);
                        if (_spatialGrid.TryGetValue(cellKey, out var cellEntities))
                        {
                            foreach (var entityId in cellEntities)
                            {
                                if (_entities.TryGetValue(entityId, out var entity))
                                {
                                    var distSquared = Vector3.DistanceSquared(position, entity.Position);
                                    if (distSquared <= radiusSquared)
                                    {
                                        results.Add(entity);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return results;
        }

        /// <summary>
        /// Get entities in sync range of an entity
        /// </summary>
        public IEnumerable<Entity> GetEntitiesInSyncRange(Entity entity)
        {
            return GetEntitiesInRadius(entity.Position, entity.SyncRadius);
        }

        #endregion

        #region Synchronization

        /// <summary>
        /// Get all dirty entities (entities that changed since last sync)
        /// </summary>
        public IEnumerable<Entity> GetDirtyEntities()
        {
            var entities = new List<Entity>();

            while (_dirtyEntities.TryDequeue(out var entity))
            {
                lock (_lock)
                {
                    _dirtyEntitySet.Remove(entity.Id);
                }
                entities.Add(entity);
            }

            return entities;
        }

        /// <summary>
        /// Get dirty entities for a specific observer
        /// Only returns entities in sync range
        /// </summary>
        public IEnumerable<Entity> GetDirtyEntitiesForObserver(Entity observer)
        {
            var dirtyEntities = GetDirtyEntities();
            return dirtyEntities.Where(e => e.IsInSyncRange(observer));
        }

        /// <summary>
        /// Get entity snapshot for observer (all entities in range)
        /// </summary>
        public IEnumerable<Entity> GetSnapshotForObserver(Entity observer)
        {
            return GetEntitiesInSyncRange(observer);
        }

        /// <summary>
        /// Mark entity as dirty (called by entity when it changes)
        /// </summary>
        private void OnEntityDirty(Entity entity)
        {
            lock (_lock)
            {
                if (!_dirtyEntitySet.Contains(entity.Id))
                {
                    _dirtyEntitySet.Add(entity.Id);
                    _dirtyEntities.Enqueue(entity);
                }
            }
        }

        /// <summary>
        /// Update all entities (call this every frame)
        /// </summary>
        public void Update(float deltaTime)
        {
            // Update all NPC AI
            foreach (var npc in GetEntitiesOfType<NPCEntity>())
            {
                npc.UpdateAI(deltaTime);
            }

            // Update item spoilage
            foreach (var item in GetEntitiesOfType<ItemEntity>())
            {
                item.UpdateSpoilage(deltaTime);
            }

            // Update spatial grid (entities might have moved)
            UpdateSpatialGrid();
        }

        #endregion

        #region Spatial Grid

        private (int, int, int) GetGridCell(Vector3 position)
        {
            return (
                (int)Math.Floor(position.X / GridCellSize),
                (int)Math.Floor(position.Y / GridCellSize),
                (int)Math.Floor(position.Z / GridCellSize)
            );
        }

        private string GetGridKey(int x, int y, int z)
        {
            return $"{x},{y},{z}";
        }

        private string GetGridKey(Vector3 position)
        {
            var cell = GetGridCell(position);
            return GetGridKey(cell.Item1, cell.Item2, cell.Item3);
        }

        private void AddToSpatialGrid(Entity entity)
        {
            var key = GetGridKey(entity.Position);
            lock (_lock)
            {
                if (!_spatialGrid.ContainsKey(key))
                {
                    _spatialGrid[key] = new HashSet<Guid>();
                }
                _spatialGrid[key].Add(entity.Id);
            }
        }

        private void RemoveFromSpatialGrid(Entity entity)
        {
            var key = GetGridKey(entity.Position);
            lock (_lock)
            {
                if (_spatialGrid.TryGetValue(key, out var cell))
                {
                    cell.Remove(entity.Id);
                    if (cell.Count == 0)
                    {
                        _spatialGrid.Remove(key);
                    }
                }
            }
        }

        private void UpdateSpatialGrid()
        {
            // Rebuild spatial grid (could be optimized to only update moved entities)
            lock (_lock)
            {
                _spatialGrid.Clear();
                foreach (var entity in _entities.Values)
                {
                    AddToSpatialGrid(entity);
                }
            }
        }

        #endregion

        #region Bulk Operations

        /// <summary>
        /// Create player entity
        /// </summary>
        public PlayerEntity CreatePlayer(string playerId, string playerName, Vector3 position)
        {
            var player = new PlayerEntity
            {
                Id = Guid.NewGuid(),
                PlayerId = playerId,
                PlayerName = playerName,
                Position = position
            };

            if (RegisterEntity(player))
            {
                return player;
            }

            return null;
        }

        /// <summary>
        /// Create NPC entity
        /// </summary>
        public NPCEntity CreateNPC(string npcName, string npcType, Vector3 position)
        {
            var npc = new NPCEntity
            {
                Id = Guid.NewGuid(),
                NPCName = npcName,
                NPCType = npcType,
                Position = position
            };

            if (RegisterEntity(npc))
            {
                return npc;
            }

            return null;
        }

        /// <summary>
        /// Create item entity
        /// </summary>
        public ItemEntity CreateItem(string itemName, string itemType, Vector3 position)
        {
            var item = new ItemEntity
            {
                Id = Guid.NewGuid(),
                ItemName = itemName,
                ItemType = itemType,
                Position = position,
                IsOnGround = true
            };

            if (RegisterEntity(item))
            {
                return item;
            }

            return null;
        }

        /// <summary>
        /// Remove all entities
        /// </summary>
        public void Clear()
        {
            foreach (var entity in _entities.Values.ToList())
            {
                UnregisterEntity(entity.Id);
            }
        }

        #endregion

        #region Serialization

        /// <summary>
        /// Serialize entity to dictionary
        /// </summary>
        public Dictionary<string, object> SerializeEntity(Entity entity)
        {
            return entity.Serialize();
        }

        /// <summary>
        /// Deserialize entity from dictionary
        /// </summary>
        public Entity DeserializeEntity(Dictionary<string, object> data)
        {
            if (!data.TryGetValue("type", out var typeObj))
                return null;

            var typeStr = typeObj.ToString();
            if (!Enum.TryParse<EntityType>(typeStr, out var type))
                return null;

            Entity entity = type switch
            {
                EntityType.Player => new PlayerEntity(),
                EntityType.NPC => new NPCEntity(),
                EntityType.Item => new ItemEntity(),
                _ => null
            };

            if (entity != null)
            {
                entity.Deserialize(data);
            }

            return entity;
        }

        #endregion
    }
}

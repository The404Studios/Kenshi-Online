using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Simulation
{
    /// <summary>
    /// Authoritative World Model
    ///
    /// CORE PRINCIPLE: The server must be able to simulate the world with ZERO clients connected.
    ///
    /// This is not "co-op illusion" - this is true multiplayer infrastructure.
    /// The world exists independently of players observing it.
    ///
    /// The world model contains:
    /// - Terrain/Zones
    /// - Time-of-day
    /// - Factions & relations
    /// - NPC brains
    /// - Economy
    /// - Save system integration
    /// </summary>
    public class AuthoritativeWorld : IDisposable
    {
        private const string LOG_PREFIX = "[World] ";

        // World identity
        public string WorldId { get; }
        public long CreatedAt { get; }
        public long CurrentTick { get; private set; }

        // Time system
        public WorldTime Time { get; }

        // Entity registry (all entities live here)
        public EntityRegistry Entities { get; }

        // Faction system
        public FactionSystem Factions { get; }

        // Zone/spatial system
        public ZoneSystem Zones { get; }

        // Economy
        public EconomySystem Economy { get; }

        // Event log (event-sourced)
        public WorldEventLog EventLog { get; }

        // State versioning
        private long stateVersion = 0;
        private readonly object stateLock = new object();

        // Simulation state
        private bool isSimulating;

        public AuthoritativeWorld(string worldId = null)
        {
            WorldId = worldId ?? Guid.NewGuid().ToString();
            CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            CurrentTick = 0;

            Time = new WorldTime();
            Entities = new EntityRegistry();
            Factions = new FactionSystem();
            Zones = new ZoneSystem();
            Economy = new EconomySystem();
            EventLog = new WorldEventLog();

            Logger.Log(LOG_PREFIX + $"World {WorldId} created");
        }

        #region Simulation Core

        /// <summary>
        /// Advance the world by one tick (deterministic)
        /// This can run with zero clients connected
        /// </summary>
        public WorldTickResult SimulateTick(float deltaTime)
        {
            var result = new WorldTickResult { TickId = CurrentTick };

            lock (stateLock)
            {
                isSimulating = true;
                CurrentTick++;
                stateVersion++;

                try
                {
                    // 1. Advance time
                    Time.Advance(deltaTime);
                    result.WorldTimeHours = Time.CurrentHour;

                    // 2. Update zones (spawn/despawn NPCs, etc.)
                    Zones.Update(deltaTime, this);

                    // 3. Update all entities
                    foreach (var entity in Entities.GetAllEntities())
                    {
                        if (entity.IsActive)
                        {
                            entity.Update(deltaTime, this);
                        }
                    }

                    // 4. Update faction relations (decay, events)
                    Factions.Update(deltaTime);

                    // 5. Update economy
                    Economy.Update(deltaTime);

                    // 6. Process queued events
                    var processedEvents = EventLog.ProcessPendingEvents(this);
                    result.EventsProcessed = processedEvents.Count;

                    // 7. Clean up dead entities
                    var cleaned = Entities.CleanupDeadEntities();
                    result.EntitiesCleaned = cleaned;

                    result.Success = true;
                    result.StateVersion = stateVersion;
                }
                catch (Exception ex)
                {
                    result.Success = false;
                    result.Error = ex.Message;
                    Logger.Log(LOG_PREFIX + $"Tick {CurrentTick} error: {ex.Message}");
                }
                finally
                {
                    isSimulating = false;
                }
            }

            return result;
        }

        /// <summary>
        /// Create a full snapshot of the world state
        /// </summary>
        public WorldSnapshot CreateSnapshot()
        {
            lock (stateLock)
            {
                return new WorldSnapshot
                {
                    WorldId = WorldId,
                    TickId = CurrentTick,
                    StateVersion = stateVersion,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    TimeState = Time.CreateSnapshot(),
                    EntitySnapshots = Entities.CreateSnapshot(),
                    FactionState = Factions.CreateSnapshot(),
                    ZoneState = Zones.CreateSnapshot(),
                    EconomyState = Economy.CreateSnapshot()
                };
            }
        }

        /// <summary>
        /// Restore world from a snapshot
        /// </summary>
        public bool RestoreFromSnapshot(WorldSnapshot snapshot)
        {
            lock (stateLock)
            {
                try
                {
                    CurrentTick = snapshot.TickId;
                    stateVersion = snapshot.StateVersion;

                    Time.RestoreFromSnapshot(snapshot.TimeState);
                    Entities.RestoreFromSnapshot(snapshot.EntitySnapshots);
                    Factions.RestoreFromSnapshot(snapshot.FactionState);
                    Zones.RestoreFromSnapshot(snapshot.ZoneState);
                    Economy.RestoreFromSnapshot(snapshot.EconomyState);

                    Logger.Log(LOG_PREFIX + $"World restored from tick {snapshot.TickId}");
                    return true;
                }
                catch (Exception ex)
                {
                    Logger.Log(LOG_PREFIX + $"Restore failed: {ex.Message}");
                    return false;
                }
            }
        }

        #endregion

        #region State Access

        public long GetStateVersion() => stateVersion;

        public bool IsSimulating => isSimulating;

        #endregion

        public void Dispose()
        {
            EventLog?.Dispose();
            Logger.Log(LOG_PREFIX + $"World {WorldId} disposed");
        }
    }

    #region World Time

    /// <summary>
    /// Deterministic world time system
    /// </summary>
    public class WorldTime
    {
        // Kenshi-like day cycle (24 hours)
        public const float REAL_SECONDS_PER_GAME_HOUR = 120f; // 2 minutes real = 1 hour game

        public float CurrentHour { get; private set; } = 6f; // Start at 6 AM
        public int CurrentDay { get; private set; } = 1;
        public float TotalGameHours { get; private set; } = 0f;

        public bool IsDay => CurrentHour >= 6f && CurrentHour < 20f;
        public bool IsNight => !IsDay;

        public void Advance(float deltaTime)
        {
            float hoursAdvanced = deltaTime / REAL_SECONDS_PER_GAME_HOUR;
            CurrentHour += hoursAdvanced;
            TotalGameHours += hoursAdvanced;

            while (CurrentHour >= 24f)
            {
                CurrentHour -= 24f;
                CurrentDay++;
            }
        }

        public WorldTimeSnapshot CreateSnapshot()
        {
            return new WorldTimeSnapshot
            {
                CurrentHour = CurrentHour,
                CurrentDay = CurrentDay,
                TotalGameHours = TotalGameHours
            };
        }

        public void RestoreFromSnapshot(WorldTimeSnapshot snapshot)
        {
            CurrentHour = snapshot.CurrentHour;
            CurrentDay = snapshot.CurrentDay;
            TotalGameHours = snapshot.TotalGameHours;
        }
    }

    public class WorldTimeSnapshot
    {
        public float CurrentHour { get; set; }
        public int CurrentDay { get; set; }
        public float TotalGameHours { get; set; }
    }

    #endregion

    #region Entity Registry

    /// <summary>
    /// Global entity registry - all entities must be registered here
    ///
    /// RULES:
    /// - No entity without an ID
    /// - No reuse of IDs
    /// - No client-created entities
    /// </summary>
    public class EntityRegistry
    {
        private readonly ConcurrentDictionary<string, SimulationEntity> entities = new();
        private long nextEntityId = 1;
        private readonly object idLock = new object();

        /// <summary>
        /// Generate a new unique entity ID (server-side only)
        /// </summary>
        public string GenerateEntityId(EntityType type)
        {
            lock (idLock)
            {
                return $"{type}_{nextEntityId++}";
            }
        }

        /// <summary>
        /// Register a new entity
        /// </summary>
        public bool RegisterEntity(SimulationEntity entity)
        {
            if (string.IsNullOrEmpty(entity.EntityId))
                return false;

            return entities.TryAdd(entity.EntityId, entity);
        }

        /// <summary>
        /// Get entity by ID
        /// </summary>
        public SimulationEntity GetEntity(string entityId)
        {
            entities.TryGetValue(entityId, out var entity);
            return entity;
        }

        /// <summary>
        /// Get all entities
        /// </summary>
        public IEnumerable<SimulationEntity> GetAllEntities()
        {
            return entities.Values;
        }

        /// <summary>
        /// Get entities by type
        /// </summary>
        public IEnumerable<SimulationEntity> GetEntitiesByType(EntityType type)
        {
            foreach (var entity in entities.Values)
            {
                if (entity.Type == type)
                    yield return entity;
            }
        }

        /// <summary>
        /// Get entities in radius
        /// </summary>
        public IEnumerable<SimulationEntity> GetEntitiesInRadius(float x, float y, float z, float radius)
        {
            float radiusSq = radius * radius;
            foreach (var entity in entities.Values)
            {
                float dx = entity.X - x;
                float dy = entity.Y - y;
                float dz = entity.Z - z;
                if (dx * dx + dy * dy + dz * dz <= radiusSq)
                    yield return entity;
            }
        }

        /// <summary>
        /// Remove entity
        /// </summary>
        public bool RemoveEntity(string entityId)
        {
            return entities.TryRemove(entityId, out _);
        }

        /// <summary>
        /// Clean up dead/removed entities
        /// </summary>
        public int CleanupDeadEntities()
        {
            int cleaned = 0;
            var toRemove = new List<string>();

            foreach (var kvp in entities)
            {
                if (!kvp.Value.IsActive && kvp.Value.MarkedForRemoval)
                {
                    toRemove.Add(kvp.Key);
                }
            }

            foreach (var id in toRemove)
            {
                if (entities.TryRemove(id, out _))
                    cleaned++;
            }

            return cleaned;
        }

        public Dictionary<string, EntitySnapshot> CreateSnapshot()
        {
            var snapshot = new Dictionary<string, EntitySnapshot>();
            foreach (var kvp in entities)
            {
                snapshot[kvp.Key] = kvp.Value.CreateSnapshot();
            }
            return snapshot;
        }

        public void RestoreFromSnapshot(Dictionary<string, EntitySnapshot> snapshot)
        {
            entities.Clear();
            foreach (var kvp in snapshot)
            {
                var entity = SimulationEntity.FromSnapshot(kvp.Value);
                entities[kvp.Key] = entity;
            }
        }
    }

    #endregion

    #region Simulation Entity

    /// <summary>
    /// Base simulation entity - exists in the world simulation
    /// </summary>
    public class SimulationEntity
    {
        public string EntityId { get; set; }
        public EntityType Type { get; set; }
        public string OwnerId { get; set; } // "SERVER" or player ID

        // Position
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Rotation { get; set; }

        // State
        public bool IsActive { get; set; } = true;
        public bool MarkedForRemoval { get; set; }
        public long LastUpdateTick { get; set; }
        public long StateVersion { get; set; }

        // Components (ECS-style)
        public Dictionary<string, object> Components { get; set; } = new();

        /// <summary>
        /// Update entity (called each tick)
        /// </summary>
        public virtual void Update(float deltaTime, AuthoritativeWorld world)
        {
            // Override in derived classes
            StateVersion++;
        }

        public EntitySnapshot CreateSnapshot()
        {
            return new EntitySnapshot
            {
                EntityId = EntityId,
                Type = Type,
                OwnerId = OwnerId,
                X = X,
                Y = Y,
                Z = Z,
                Rotation = Rotation,
                IsActive = IsActive,
                StateVersion = StateVersion,
                Components = new Dictionary<string, object>(Components)
            };
        }

        public static SimulationEntity FromSnapshot(EntitySnapshot snapshot)
        {
            return new SimulationEntity
            {
                EntityId = snapshot.EntityId,
                Type = snapshot.Type,
                OwnerId = snapshot.OwnerId,
                X = snapshot.X,
                Y = snapshot.Y,
                Z = snapshot.Z,
                Rotation = snapshot.Rotation,
                IsActive = snapshot.IsActive,
                StateVersion = snapshot.StateVersion,
                Components = new Dictionary<string, object>(snapshot.Components)
            };
        }
    }

    public class EntitySnapshot
    {
        public string EntityId { get; set; }
        public EntityType Type { get; set; }
        public string OwnerId { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Rotation { get; set; }
        public bool IsActive { get; set; }
        public long StateVersion { get; set; }
        public Dictionary<string, object> Components { get; set; }
    }

    public enum EntityType
    {
        Player,
        NPC,
        Item,
        Building,
        Vehicle,
        Projectile,
        Effect
    }

    #endregion

    #region Faction System

    public class FactionSystem
    {
        private readonly Dictionary<string, Faction> factions = new();
        private readonly Dictionary<(string, string), int> relations = new(); // -100 to 100

        public void RegisterFaction(string factionId, string name)
        {
            factions[factionId] = new Faction { FactionId = factionId, Name = name };
        }

        public int GetRelation(string faction1, string faction2)
        {
            if (faction1 == faction2) return 100; // Same faction = allied

            var key = faction1.CompareTo(faction2) < 0
                ? (faction1, faction2)
                : (faction2, faction1);

            return relations.TryGetValue(key, out var rel) ? rel : 0;
        }

        public void SetRelation(string faction1, string faction2, int value)
        {
            var key = faction1.CompareTo(faction2) < 0
                ? (faction1, faction2)
                : (faction2, faction1);

            relations[key] = Math.Clamp(value, -100, 100);
        }

        public void Update(float deltaTime)
        {
            // Faction relation decay, events, etc.
        }

        public FactionSnapshot CreateSnapshot()
        {
            return new FactionSnapshot
            {
                Factions = new Dictionary<string, Faction>(factions),
                Relations = new Dictionary<(string, string), int>(relations)
            };
        }

        public void RestoreFromSnapshot(FactionSnapshot snapshot)
        {
            factions.Clear();
            relations.Clear();
            foreach (var kvp in snapshot.Factions)
                factions[kvp.Key] = kvp.Value;
            foreach (var kvp in snapshot.Relations)
                relations[kvp.Key] = kvp.Value;
        }
    }

    public class Faction
    {
        public string FactionId { get; set; }
        public string Name { get; set; }
    }

    public class FactionSnapshot
    {
        public Dictionary<string, Faction> Factions { get; set; }
        public Dictionary<(string, string), int> Relations { get; set; }
    }

    #endregion

    #region Zone System

    public class ZoneSystem
    {
        private readonly Dictionary<string, Zone> zones = new();

        public void RegisterZone(string zoneId, float centerX, float centerZ, float radius)
        {
            zones[zoneId] = new Zone
            {
                ZoneId = zoneId,
                CenterX = centerX,
                CenterZ = centerZ,
                Radius = radius
            };
        }

        public Zone GetZoneAt(float x, float z)
        {
            foreach (var zone in zones.Values)
            {
                float dx = x - zone.CenterX;
                float dz = z - zone.CenterZ;
                if (dx * dx + dz * dz <= zone.Radius * zone.Radius)
                    return zone;
            }
            return null;
        }

        public void Update(float deltaTime, AuthoritativeWorld world)
        {
            // Zone updates, NPC spawning, etc.
        }

        public ZoneSnapshot CreateSnapshot()
        {
            return new ZoneSnapshot
            {
                Zones = new Dictionary<string, Zone>(zones)
            };
        }

        public void RestoreFromSnapshot(ZoneSnapshot snapshot)
        {
            zones.Clear();
            foreach (var kvp in snapshot.Zones)
                zones[kvp.Key] = kvp.Value;
        }
    }

    public class Zone
    {
        public string ZoneId { get; set; }
        public float CenterX { get; set; }
        public float CenterZ { get; set; }
        public float Radius { get; set; }
        public string ControllingFaction { get; set; }
    }

    public class ZoneSnapshot
    {
        public Dictionary<string, Zone> Zones { get; set; }
    }

    #endregion

    #region Economy System

    public class EconomySystem
    {
        private readonly Dictionary<string, float> globalPrices = new();

        public float GetPrice(string itemId)
        {
            return globalPrices.TryGetValue(itemId, out var price) ? price : 100f;
        }

        public void SetPrice(string itemId, float price)
        {
            globalPrices[itemId] = price;
        }

        public void Update(float deltaTime)
        {
            // Price fluctuation, supply/demand, etc.
        }

        public EconomySnapshot CreateSnapshot()
        {
            return new EconomySnapshot
            {
                Prices = new Dictionary<string, float>(globalPrices)
            };
        }

        public void RestoreFromSnapshot(EconomySnapshot snapshot)
        {
            globalPrices.Clear();
            foreach (var kvp in snapshot.Prices)
                globalPrices[kvp.Key] = kvp.Value;
        }
    }

    public class EconomySnapshot
    {
        public Dictionary<string, float> Prices { get; set; }
    }

    #endregion

    #region World Snapshot

    public class WorldSnapshot
    {
        public string WorldId { get; set; }
        public long TickId { get; set; }
        public long StateVersion { get; set; }
        public long Timestamp { get; set; }

        public WorldTimeSnapshot TimeState { get; set; }
        public Dictionary<string, EntitySnapshot> EntitySnapshots { get; set; }
        public FactionSnapshot FactionState { get; set; }
        public ZoneSnapshot ZoneState { get; set; }
        public EconomySnapshot EconomyState { get; set; }
    }

    public class WorldTickResult
    {
        public long TickId { get; set; }
        public bool Success { get; set; }
        public string Error { get; set; }
        public long StateVersion { get; set; }
        public float WorldTimeHours { get; set; }
        public int EventsProcessed { get; set; }
        public int EntitiesCleaned { get; set; }
    }

    #endregion
}

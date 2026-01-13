using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Text.Json;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Simulation
{
    /// <summary>
    /// Event-Sourced Architecture
    ///
    /// PRINCIPLE: Multiplayer is event systems pretending to be state.
    ///
    /// Instead of: "Here's the new state"
    /// We say:     "Entity 451 attacked Entity 312 at tick 9182"
    ///
    /// Server stores:
    /// - Ordered event log
    /// - Periodic snapshots
    ///
    /// Clients:
    /// - Replay events
    /// - Reconcile corrections
    ///
    /// This unlocks:
    /// - Replay
    /// - Rollback
    /// - Testing
    /// - Determinism
    /// </summary>
    public class WorldEventLog : IDisposable
    {
        private const string LOG_PREFIX = "[EventLog] ";

        // Event storage
        private readonly ConcurrentQueue<WorldEvent> pendingEvents = new();
        private readonly List<WorldEvent> processedEvents = new();
        private readonly object processedLock = new object();

        // Event ID generation
        private long nextEventId = 1;

        // Snapshot storage
        private readonly Dictionary<long, WorldSnapshot> snapshots = new();
        private const int SNAPSHOT_INTERVAL_TICKS = 600; // Every 30 seconds at 20Hz
        private const int MAX_SNAPSHOTS = 10;

        // Persistence
        private StreamWriter eventLogWriter;
        private string eventLogPath;

        public WorldEventLog(string logPath = null)
        {
            if (!string.IsNullOrEmpty(logPath))
            {
                eventLogPath = logPath;
                eventLogWriter = new StreamWriter(logPath, append: true);
            }
        }

        #region Event Submission

        /// <summary>
        /// Submit an event to be processed
        /// </summary>
        public long SubmitEvent(WorldEvent evt)
        {
            evt.EventId = nextEventId++;
            evt.SubmittedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            pendingEvents.Enqueue(evt);

            // Write to log file
            WriteEventToLog(evt);

            return evt.EventId;
        }

        /// <summary>
        /// Create and submit a movement event
        /// </summary>
        public long SubmitMovement(string entityId, float x, float y, float z, long tickId)
        {
            return SubmitEvent(new WorldEvent
            {
                Type = WorldEventType.EntityMoved,
                EntityId = entityId,
                TickId = tickId,
                Data = new Dictionary<string, object>
                {
                    { "x", x },
                    { "y", y },
                    { "z", z }
                }
            });
        }

        /// <summary>
        /// Create and submit a combat event
        /// </summary>
        public long SubmitCombat(string attackerId, string targetId, string action, int damage, long tickId)
        {
            return SubmitEvent(new WorldEvent
            {
                Type = WorldEventType.CombatAction,
                EntityId = attackerId,
                TargetEntityId = targetId,
                TickId = tickId,
                Data = new Dictionary<string, object>
                {
                    { "action", action },
                    { "damage", damage }
                }
            });
        }

        /// <summary>
        /// Create and submit an inventory event
        /// </summary>
        public long SubmitInventoryChange(string entityId, string itemId, int quantity, string action, long tickId)
        {
            return SubmitEvent(new WorldEvent
            {
                Type = WorldEventType.InventoryChanged,
                EntityId = entityId,
                TickId = tickId,
                Data = new Dictionary<string, object>
                {
                    { "itemId", itemId },
                    { "quantity", quantity },
                    { "action", action }
                }
            });
        }

        /// <summary>
        /// Create and submit an entity spawn event
        /// </summary>
        public long SubmitSpawn(string entityId, EntityType type, float x, float y, float z, long tickId)
        {
            return SubmitEvent(new WorldEvent
            {
                Type = WorldEventType.EntitySpawned,
                EntityId = entityId,
                TickId = tickId,
                Data = new Dictionary<string, object>
                {
                    { "entityType", type.ToString() },
                    { "x", x },
                    { "y", y },
                    { "z", z }
                }
            });
        }

        /// <summary>
        /// Create and submit an entity death event
        /// </summary>
        public long SubmitDeath(string entityId, string killerId, long tickId)
        {
            return SubmitEvent(new WorldEvent
            {
                Type = WorldEventType.EntityDied,
                EntityId = entityId,
                TickId = tickId,
                Data = new Dictionary<string, object>
                {
                    { "killerId", killerId }
                }
            });
        }

        #endregion

        #region Event Processing

        /// <summary>
        /// Process all pending events (called each tick)
        /// </summary>
        public List<WorldEvent> ProcessPendingEvents(AuthoritativeWorld world)
        {
            var processed = new List<WorldEvent>();

            while (pendingEvents.TryDequeue(out var evt))
            {
                evt.ProcessedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

                try
                {
                    bool success = ApplyEvent(evt, world);
                    evt.WasApplied = success;

                    if (!success)
                    {
                        evt.RejectionReason = "Event application failed";
                    }
                }
                catch (Exception ex)
                {
                    evt.WasApplied = false;
                    evt.RejectionReason = ex.Message;
                    Logger.Log(LOG_PREFIX + $"Event {evt.EventId} failed: {ex.Message}");
                }

                processed.Add(evt);

                lock (processedLock)
                {
                    processedEvents.Add(evt);
                }
            }

            // Take snapshot if needed
            if (world.CurrentTick % SNAPSHOT_INTERVAL_TICKS == 0)
            {
                TakeSnapshot(world);
            }

            return processed;
        }

        /// <summary>
        /// Apply a single event to the world (deterministic)
        /// </summary>
        private bool ApplyEvent(WorldEvent evt, AuthoritativeWorld world)
        {
            switch (evt.Type)
            {
                case WorldEventType.EntityMoved:
                    return ApplyMovement(evt, world);

                case WorldEventType.CombatAction:
                    return ApplyCombat(evt, world);

                case WorldEventType.InventoryChanged:
                    return ApplyInventoryChange(evt, world);

                case WorldEventType.EntitySpawned:
                    return ApplySpawn(evt, world);

                case WorldEventType.EntityDied:
                    return ApplyDeath(evt, world);

                case WorldEventType.FactionRelationChanged:
                    return ApplyFactionChange(evt, world);

                default:
                    Logger.Log(LOG_PREFIX + $"Unknown event type: {evt.Type}");
                    return false;
            }
        }

        private bool ApplyMovement(WorldEvent evt, AuthoritativeWorld world)
        {
            var entity = world.Entities.GetEntity(evt.EntityId);
            if (entity == null) return false;

            entity.X = Convert.ToSingle(evt.Data["x"]);
            entity.Y = Convert.ToSingle(evt.Data["y"]);
            entity.Z = Convert.ToSingle(evt.Data["z"]);
            entity.LastUpdateTick = evt.TickId;

            return true;
        }

        private bool ApplyCombat(WorldEvent evt, AuthoritativeWorld world)
        {
            var attacker = world.Entities.GetEntity(evt.EntityId);
            var target = world.Entities.GetEntity(evt.TargetEntityId);

            if (attacker == null || target == null) return false;

            // Apply damage to target
            int damage = Convert.ToInt32(evt.Data["damage"]);

            if (target.Components.TryGetValue("Health", out var healthObj))
            {
                float health = Convert.ToSingle(healthObj);
                health -= damage;
                target.Components["Health"] = health;

                if (health <= 0)
                {
                    target.IsActive = false;
                    target.MarkedForRemoval = true;
                }
            }

            return true;
        }

        private bool ApplyInventoryChange(WorldEvent evt, AuthoritativeWorld world)
        {
            var entity = world.Entities.GetEntity(evt.EntityId);
            if (entity == null) return false;

            // Get or create inventory component
            if (!entity.Components.TryGetValue("Inventory", out var invObj))
            {
                invObj = new Dictionary<string, int>();
                entity.Components["Inventory"] = invObj;
            }

            var inventory = invObj as Dictionary<string, int> ?? new Dictionary<string, int>();

            string itemId = evt.Data["itemId"].ToString();
            int quantity = Convert.ToInt32(evt.Data["quantity"]);
            string action = evt.Data["action"].ToString();

            if (action == "add" || action == "pickup")
            {
                inventory[itemId] = inventory.GetValueOrDefault(itemId) + quantity;
            }
            else if (action == "remove" || action == "drop")
            {
                int current = inventory.GetValueOrDefault(itemId);
                inventory[itemId] = Math.Max(0, current - quantity);
                if (inventory[itemId] == 0)
                    inventory.Remove(itemId);
            }

            entity.Components["Inventory"] = inventory;
            return true;
        }

        private bool ApplySpawn(WorldEvent evt, AuthoritativeWorld world)
        {
            string entityType = evt.Data["entityType"].ToString();
            if (!Enum.TryParse<EntityType>(entityType, out var type))
                return false;

            var entity = new SimulationEntity
            {
                EntityId = evt.EntityId,
                Type = type,
                X = Convert.ToSingle(evt.Data["x"]),
                Y = Convert.ToSingle(evt.Data["y"]),
                Z = Convert.ToSingle(evt.Data["z"]),
                IsActive = true,
                OwnerId = "SERVER"
            };

            return world.Entities.RegisterEntity(entity);
        }

        private bool ApplyDeath(WorldEvent evt, AuthoritativeWorld world)
        {
            var entity = world.Entities.GetEntity(evt.EntityId);
            if (entity == null) return false;

            entity.IsActive = false;
            entity.MarkedForRemoval = true;

            return true;
        }

        private bool ApplyFactionChange(WorldEvent evt, AuthoritativeWorld world)
        {
            string faction1 = evt.Data["faction1"].ToString();
            string faction2 = evt.Data["faction2"].ToString();
            int change = Convert.ToInt32(evt.Data["change"]);

            int current = world.Factions.GetRelation(faction1, faction2);
            world.Factions.SetRelation(faction1, faction2, current + change);

            return true;
        }

        #endregion

        #region Snapshots

        /// <summary>
        /// Take a world snapshot
        /// </summary>
        public void TakeSnapshot(AuthoritativeWorld world)
        {
            var snapshot = world.CreateSnapshot();
            snapshots[snapshot.TickId] = snapshot;

            // Clean old snapshots
            if (snapshots.Count > MAX_SNAPSHOTS)
            {
                long minTick = world.CurrentTick - (MAX_SNAPSHOTS * SNAPSHOT_INTERVAL_TICKS);
                var toRemove = new List<long>();
                foreach (var tick in snapshots.Keys)
                {
                    if (tick < minTick)
                        toRemove.Add(tick);
                }
                foreach (var tick in toRemove)
                    snapshots.Remove(tick);
            }

            Logger.Log(LOG_PREFIX + $"Snapshot taken at tick {snapshot.TickId}");
        }

        /// <summary>
        /// Get nearest snapshot before a tick
        /// </summary>
        public WorldSnapshot GetSnapshotBefore(long tickId)
        {
            WorldSnapshot best = null;
            long bestTick = -1;

            foreach (var kvp in snapshots)
            {
                if (kvp.Key <= tickId && kvp.Key > bestTick)
                {
                    bestTick = kvp.Key;
                    best = kvp.Value;
                }
            }

            return best;
        }

        #endregion

        #region Replay

        /// <summary>
        /// Get events in a tick range for replay
        /// </summary>
        public List<WorldEvent> GetEventsInRange(long fromTick, long toTick)
        {
            var events = new List<WorldEvent>();

            lock (processedLock)
            {
                foreach (var evt in processedEvents)
                {
                    if (evt.TickId >= fromTick && evt.TickId <= toTick)
                    {
                        events.Add(evt);
                    }
                }
            }

            return events;
        }

        /// <summary>
        /// Replay events to reconstruct state at a specific tick
        /// </summary>
        public bool ReplayToTick(AuthoritativeWorld world, long targetTick)
        {
            // Find nearest snapshot before target
            var snapshot = GetSnapshotBefore(targetTick);
            if (snapshot == null)
            {
                Logger.Log(LOG_PREFIX + "No snapshot found for replay");
                return false;
            }

            // Restore from snapshot
            if (!world.RestoreFromSnapshot(snapshot))
            {
                return false;
            }

            // Get events between snapshot and target
            var events = GetEventsInRange(snapshot.TickId, targetTick);

            // Apply events in order
            foreach (var evt in events)
            {
                ApplyEvent(evt, world);
            }

            Logger.Log(LOG_PREFIX + $"Replayed to tick {targetTick} ({events.Count} events)");
            return true;
        }

        #endregion

        #region Query

        /// <summary>
        /// Get recent events
        /// </summary>
        public List<WorldEvent> GetRecentEvents(int count = 100)
        {
            lock (processedLock)
            {
                int start = Math.Max(0, processedEvents.Count - count);
                return processedEvents.GetRange(start, processedEvents.Count - start);
            }
        }

        /// <summary>
        /// Get events for an entity
        /// </summary>
        public List<WorldEvent> GetEntityEvents(string entityId, int count = 100)
        {
            var events = new List<WorldEvent>();

            lock (processedLock)
            {
                for (int i = processedEvents.Count - 1; i >= 0 && events.Count < count; i--)
                {
                    var evt = processedEvents[i];
                    if (evt.EntityId == entityId || evt.TargetEntityId == entityId)
                    {
                        events.Add(evt);
                    }
                }
            }

            events.Reverse();
            return events;
        }

        #endregion

        #region Persistence

        private void WriteEventToLog(WorldEvent evt)
        {
            if (eventLogWriter == null) return;

            try
            {
                string json = JsonSerializer.Serialize(evt);
                eventLogWriter.WriteLine(json);
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"Failed to write event: {ex.Message}");
            }
        }

        /// <summary>
        /// Flush event log to disk
        /// </summary>
        public void Flush()
        {
            eventLogWriter?.Flush();
        }

        /// <summary>
        /// Load events from log file
        /// </summary>
        public List<WorldEvent> LoadEventsFromFile(string path)
        {
            var events = new List<WorldEvent>();

            if (!File.Exists(path)) return events;

            foreach (var line in File.ReadLines(path))
            {
                try
                {
                    var evt = JsonSerializer.Deserialize<WorldEvent>(line);
                    if (evt != null)
                        events.Add(evt);
                }
                catch { }
            }

            return events;
        }

        #endregion

        public void Dispose()
        {
            Flush();
            eventLogWriter?.Close();
        }
    }

    #region World Event

    /// <summary>
    /// A discrete event that affects the world
    /// Events are the source of truth - state is derived
    /// </summary>
    public class WorldEvent
    {
        // Identity
        public long EventId { get; set; }
        public WorldEventType Type { get; set; }

        // Timing
        public long TickId { get; set; }           // Simulation tick
        public long SubmittedAt { get; set; }      // Real timestamp
        public long ProcessedAt { get; set; }

        // Entities involved
        public string EntityId { get; set; }       // Primary entity
        public string TargetEntityId { get; set; } // Optional target

        // Event data
        public Dictionary<string, object> Data { get; set; } = new();

        // Processing result
        public bool WasApplied { get; set; }
        public string RejectionReason { get; set; }

        // Source
        public string SourcePlayerId { get; set; } // Who caused this event
    }

    public enum WorldEventType
    {
        // Entity lifecycle
        EntitySpawned,
        EntityDied,
        EntityRemoved,

        // Movement
        EntityMoved,
        EntityTeleported,

        // Combat
        CombatAction,
        DamageDealt,
        HealingApplied,
        StatusEffectApplied,
        StatusEffectRemoved,

        // Inventory
        InventoryChanged,
        ItemDropped,
        ItemPickedUp,
        ItemEquipped,
        ItemUnequipped,

        // Interaction
        InteractionStarted,
        InteractionCompleted,

        // Faction
        FactionRelationChanged,
        FactionMemberJoined,
        FactionMemberLeft,

        // World
        TimeAdvanced,
        ZoneChanged,
        BuildingPlaced,
        BuildingDestroyed,

        // Player
        PlayerConnected,
        PlayerDisconnected,
        PlayerControlTransferred
    }

    #endregion
}

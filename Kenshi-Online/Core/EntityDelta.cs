using System;
using System.Collections.Generic;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Types of changes that can occur to an entity.
    /// </summary>
    public enum DeltaType
    {
        /// <summary>Entity was created (new player joined, item spawned)</summary>
        Created,

        /// <summary>Entity was updated (position moved, health changed)</summary>
        Updated,

        /// <summary>Entity was destroyed (item picked up, character died)</summary>
        Destroyed,

        /// <summary>Entity ownership changed (item traded, NPC recruited)</summary>
        Authority
    }

    /// <summary>
    /// EntityDelta represents a CHANGE to an entity.
    /// Instead of sending full EntityState every tick, we send only what changed.
    ///
    /// Bandwidth savings: ~90% reduction for typical gameplay.
    ///
    /// Example:
    ///   Full state: 500 bytes per entity
    ///   Position delta: 24 bytes (EntityId + X,Y,Z,Tick)
    /// </summary>
    public class EntityDelta
    {
        /// <summary>
        /// Entity that changed.
        /// </summary>
        public string EntityId { get; set; }

        /// <summary>
        /// What kind of change occurred.
        /// </summary>
        public DeltaType Type { get; set; }

        /// <summary>
        /// Changed fields and their new values.
        /// Only contains fields that actually changed.
        ///
        /// Common patterns:
        ///   Position: { "X": 100.5, "Y": 0.0, "Z": 200.3 }
        ///   Health: { "Health": 75.0 }
        ///   Ownership: { "OwnerId": "player_abc123" }
        /// </summary>
        public Dictionary<string, object> Changes { get; set; } = new();

        /// <summary>
        /// Tick when this change occurred.
        /// Used for ordering and conflict resolution.
        /// </summary>
        public ulong SourceTick { get; set; }

        /// <summary>
        /// Who caused this change.
        /// "SERVER" for server-initiated, player ID for player actions.
        /// </summary>
        public string SourceId { get; set; }

        /// <summary>
        /// Sequence number for ordering within a tick.
        /// Multiple changes can occur in same tick; this orders them.
        /// </summary>
        public int Sequence { get; set; }

        /// <summary>
        /// Create a position delta (most common type).
        /// </summary>
        public static EntityDelta CreatePositionDelta(string entityId, float x, float y, float z, ulong tick)
        {
            return new EntityDelta
            {
                EntityId = entityId,
                Type = DeltaType.Updated,
                Changes = new Dictionary<string, object>
                {
                    ["X"] = x,
                    ["Y"] = y,
                    ["Z"] = z
                },
                SourceTick = tick,
                SourceId = "SERVER"
            };
        }

        /// <summary>
        /// Create a health delta.
        /// </summary>
        public static EntityDelta CreateHealthDelta(string entityId, float health, ulong tick, string sourceId)
        {
            return new EntityDelta
            {
                EntityId = entityId,
                Type = DeltaType.Updated,
                Changes = new Dictionary<string, object>
                {
                    ["Health"] = health
                },
                SourceTick = tick,
                SourceId = sourceId
            };
        }

        /// <summary>
        /// Create a creation delta (entity spawned).
        /// </summary>
        public static EntityDelta CreateSpawnDelta(EntityState entity, ulong tick)
        {
            var changes = new Dictionary<string, object>
            {
                ["Type"] = entity.Type.ToString(),
                ["Name"] = entity.Name,
                ["X"] = entity.X,
                ["Y"] = entity.Y,
                ["Z"] = entity.Z,
                ["Health"] = entity.Health,
                ["MaxHealth"] = entity.MaxHealth,
                ["OwnerId"] = entity.OwnerId
            };

            foreach (var kvp in entity.Data)
            {
                changes[$"Data_{kvp.Key}"] = kvp.Value;
            }

            return new EntityDelta
            {
                EntityId = entity.EntityId,
                Type = DeltaType.Created,
                Changes = changes,
                SourceTick = tick,
                SourceId = "SERVER"
            };
        }

        /// <summary>
        /// Create a destruction delta (entity removed).
        /// </summary>
        public static EntityDelta CreateDestroyDelta(string entityId, ulong tick, string reason)
        {
            return new EntityDelta
            {
                EntityId = entityId,
                Type = DeltaType.Destroyed,
                Changes = new Dictionary<string, object>
                {
                    ["Reason"] = reason
                },
                SourceTick = tick,
                SourceId = "SERVER"
            };
        }

        /// <summary>
        /// Create an authority change delta (ownership transfer).
        /// </summary>
        public static EntityDelta CreateAuthorityDelta(string entityId, string newOwnerId, ulong tick)
        {
            return new EntityDelta
            {
                EntityId = entityId,
                Type = DeltaType.Authority,
                Changes = new Dictionary<string, object>
                {
                    ["OwnerId"] = newOwnerId
                },
                SourceTick = tick,
                SourceId = "SERVER"
            };
        }

        /// <summary>
        /// Apply this delta to an entity state.
        /// </summary>
        public void ApplyTo(EntityState entity)
        {
            if (entity == null || entity.EntityId != EntityId)
                return;

            switch (Type)
            {
                case DeltaType.Created:
                case DeltaType.Updated:
                    ApplyChanges(entity);
                    break;

                case DeltaType.Authority:
                    if (Changes.TryGetValue("OwnerId", out var ownerId))
                        entity.OwnerId = ownerId?.ToString();
                    break;

                case DeltaType.Destroyed:
                    // Destruction handled by removing from collection
                    break;
            }

            entity.LastUpdatedTick = SourceTick;
            entity.LastUpdatedTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        private void ApplyChanges(EntityState entity)
        {
            foreach (var kvp in Changes)
            {
                switch (kvp.Key)
                {
                    case "X":
                        entity.X = Convert.ToSingle(kvp.Value);
                        break;
                    case "Y":
                        entity.Y = Convert.ToSingle(kvp.Value);
                        break;
                    case "Z":
                        entity.Z = Convert.ToSingle(kvp.Value);
                        break;
                    case "RotationY":
                        entity.RotationY = Convert.ToSingle(kvp.Value);
                        break;
                    case "Health":
                        entity.Health = Convert.ToSingle(kvp.Value);
                        break;
                    case "MaxHealth":
                        entity.MaxHealth = Convert.ToSingle(kvp.Value);
                        break;
                    case "OwnerId":
                        entity.OwnerId = kvp.Value?.ToString();
                        break;
                    case "Name":
                        entity.Name = kvp.Value?.ToString();
                        break;
                    default:
                        // Handle Data_ prefixed keys
                        if (kvp.Key.StartsWith("Data_"))
                        {
                            var dataKey = kvp.Key.Substring(5);
                            entity.Data[dataKey] = kvp.Value;
                        }
                        break;
                }
            }
        }

        /// <summary>
        /// Get estimated size in bytes (for bandwidth tracking).
        /// </summary>
        public int EstimatedSize()
        {
            // EntityId (assume 32 bytes avg) + Type (4) + Tick (8) + Sequence (4) + overhead
            int size = 48 + EntityId?.Length ?? 0;

            foreach (var kvp in Changes)
            {
                size += kvp.Key.Length;
                size += EstimateValueSize(kvp.Value);
            }

            return size;
        }

        private int EstimateValueSize(object value)
        {
            return value switch
            {
                null => 0,
                bool => 1,
                int => 4,
                long => 8,
                float => 4,
                double => 8,
                string s => s.Length,
                _ => 32 // Default estimate
            };
        }

        /// <summary>
        /// Serialize to JSON.
        /// </summary>
        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        /// <summary>
        /// Deserialize from JSON.
        /// </summary>
        public static EntityDelta FromJson(string json)
        {
            return JsonSerializer.Deserialize<EntityDelta>(json);
        }
    }

    /// <summary>
    /// Batch of deltas for efficient network transmission.
    /// </summary>
    public class DeltaBatch
    {
        public ulong StartTick { get; set; }
        public ulong EndTick { get; set; }
        public List<EntityDelta> Deltas { get; set; } = new();

        public void Add(EntityDelta delta)
        {
            Deltas.Add(delta);
            if (delta.SourceTick < StartTick || StartTick == 0)
                StartTick = delta.SourceTick;
            if (delta.SourceTick > EndTick)
                EndTick = delta.SourceTick;
        }

        public int TotalSize()
        {
            int size = 16; // Header
            foreach (var delta in Deltas)
            {
                size += delta.EstimatedSize();
            }
            return size;
        }

        /// <summary>
        /// Compress deltas by merging same-entity updates.
        /// </summary>
        public DeltaBatch Compress()
        {
            var compressed = new DeltaBatch
            {
                StartTick = StartTick,
                EndTick = EndTick
            };

            var byEntity = new Dictionary<string, EntityDelta>();

            foreach (var delta in Deltas)
            {
                if (delta.Type == DeltaType.Destroyed)
                {
                    // Destruction deltas always kept
                    byEntity.Remove(delta.EntityId);
                    compressed.Deltas.Add(delta);
                    continue;
                }

                if (byEntity.TryGetValue(delta.EntityId, out var existing))
                {
                    // Merge changes into existing delta
                    foreach (var kvp in delta.Changes)
                    {
                        existing.Changes[kvp.Key] = kvp.Value;
                    }
                    existing.SourceTick = delta.SourceTick;
                }
                else
                {
                    byEntity[delta.EntityId] = delta;
                }
            }

            foreach (var delta in byEntity.Values)
            {
                compressed.Deltas.Add(delta);
            }

            return compressed;
        }
    }

    /// <summary>
    /// Computes deltas between two entity states.
    /// </summary>
    public static class DeltaComputer
    {
        /// <summary>
        /// Compute what changed between old and new state.
        /// </summary>
        public static EntityDelta ComputeDelta(EntityState oldState, EntityState newState, ulong tick)
        {
            if (oldState == null && newState == null)
                return null;

            if (oldState == null)
                return EntityDelta.CreateSpawnDelta(newState, tick);

            if (newState == null)
                return EntityDelta.CreateDestroyDelta(oldState.EntityId, tick, "removed");

            var changes = new Dictionary<string, object>();

            // Position changes (with threshold to avoid noise)
            const float POS_THRESHOLD = 0.01f;
            if (Math.Abs(newState.X - oldState.X) > POS_THRESHOLD)
                changes["X"] = newState.X;
            if (Math.Abs(newState.Y - oldState.Y) > POS_THRESHOLD)
                changes["Y"] = newState.Y;
            if (Math.Abs(newState.Z - oldState.Z) > POS_THRESHOLD)
                changes["Z"] = newState.Z;
            if (Math.Abs(newState.RotationY - oldState.RotationY) > POS_THRESHOLD)
                changes["RotationY"] = newState.RotationY;

            // Health changes
            const float HEALTH_THRESHOLD = 0.1f;
            if (Math.Abs(newState.Health - oldState.Health) > HEALTH_THRESHOLD)
                changes["Health"] = newState.Health;

            // Owner changes
            if (newState.OwnerId != oldState.OwnerId)
                changes["OwnerId"] = newState.OwnerId;

            // Data changes
            foreach (var kvp in newState.Data)
            {
                if (!oldState.Data.TryGetValue(kvp.Key, out var oldVal) ||
                    !Equals(oldVal, kvp.Value))
                {
                    changes[$"Data_{kvp.Key}"] = kvp.Value;
                }
            }

            if (changes.Count == 0)
                return null;

            return new EntityDelta
            {
                EntityId = newState.EntityId,
                Type = DeltaType.Updated,
                Changes = changes,
                SourceTick = tick,
                SourceId = "SERVER"
            };
        }
    }
}

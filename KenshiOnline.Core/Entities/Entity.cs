using System;
using System.Collections.Generic;
using System.Numerics;

namespace KenshiOnline.Core.Entities
{
    /// <summary>
    /// Entity types in the game
    /// </summary>
    public enum EntityType
    {
        Player,
        NPC,
        Animal,
        Building,
        Item,
        Squad
    }

    /// <summary>
    /// Base entity class for all synchronized game objects
    /// </summary>
    public class Entity
    {
        public Guid Id { get; set; }
        public EntityType Type { get; set; }
        public string Name { get; set; }
        public ulong GameId { get; set; } // Kenshi's internal ID

        // Transform
        public Vector3 Position { get; set; }
        public Quaternion Rotation { get; set; }
        public Vector3 Velocity { get; set; }

        // State
        public bool IsActive { get; set; }
        public bool IsDirty { get; set; } // Needs sync
        public DateTime LastUpdate { get; set; }
        public string OwnerId { get; set; } // Player who owns this entity

        // Network
        public int Priority { get; set; } = 1; // Sync priority (1-10)
        public float SyncRadius { get; set; } = 100f; // Only sync within this radius

        public Entity()
        {
            Id = Guid.NewGuid();
            IsActive = true;
            LastUpdate = DateTime.UtcNow;
        }

        /// <summary>
        /// Serialize entity to network format
        /// </summary>
        public virtual Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                { "id", Id.ToString() },
                { "type", Type.ToString() },
                { "name", Name ?? "" },
                { "gameId", GameId },
                { "posX", Position.X },
                { "posY", Position.Y },
                { "posZ", Position.Z },
                { "rotX", Rotation.X },
                { "rotY", Rotation.Y },
                { "rotZ", Rotation.Z },
                { "rotW", Rotation.W },
                { "velX", Velocity.X },
                { "velY", Velocity.Y },
                { "velZ", Velocity.Z },
                { "active", IsActive },
                { "owner", OwnerId ?? "" },
                { "lastUpdate", LastUpdate.Ticks }
            };
        }

        /// <summary>
        /// Deserialize entity from network data
        /// </summary>
        public virtual void Deserialize(Dictionary<string, object> data)
        {
            if (data.TryGetValue("id", out var id))
                Id = Guid.Parse(id.ToString());
            if (data.TryGetValue("type", out var type))
                Type = Enum.Parse<EntityType>(type.ToString());
            if (data.TryGetValue("name", out var name))
                Name = name.ToString();
            if (data.TryGetValue("gameId", out var gameId))
                GameId = Convert.ToUInt64(gameId);

            if (data.TryGetValue("posX", out var posX))
                Position = new Vector3(
                    Convert.ToSingle(posX),
                    Convert.ToSingle(data["posY"]),
                    Convert.ToSingle(data["posZ"])
                );

            if (data.TryGetValue("rotX", out var rotX))
                Rotation = new Quaternion(
                    Convert.ToSingle(rotX),
                    Convert.ToSingle(data["rotY"]),
                    Convert.ToSingle(data["rotZ"]),
                    Convert.ToSingle(data["rotW"])
                );

            if (data.TryGetValue("velX", out var velX))
                Velocity = new Vector3(
                    Convert.ToSingle(velX),
                    Convert.ToSingle(data["velY"]),
                    Convert.ToSingle(data["velZ"])
                );

            if (data.TryGetValue("active", out var active))
                IsActive = Convert.ToBoolean(active);
            if (data.TryGetValue("owner", out var owner))
                OwnerId = owner.ToString();
            if (data.TryGetValue("lastUpdate", out var lastUpdate))
                LastUpdate = new DateTime(Convert.ToInt64(lastUpdate));
        }

        /// <summary>
        /// Calculate distance to another entity
        /// </summary>
        public float DistanceTo(Entity other)
        {
            return Vector3.Distance(Position, other.Position);
        }

        /// <summary>
        /// Check if entity is within sync radius of another
        /// </summary>
        public bool IsInSyncRange(Entity other)
        {
            return DistanceTo(other) <= SyncRadius;
        }

        /// <summary>
        /// Mark entity as needing synchronization
        /// </summary>
        public void MarkDirty()
        {
            IsDirty = true;
            LastUpdate = DateTime.UtcNow;
        }
    }
}

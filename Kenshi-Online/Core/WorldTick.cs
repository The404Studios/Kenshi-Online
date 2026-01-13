using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// WorldTick is the server's source of truth for a single moment in time.
    /// Every 50ms (20 Hz), the server produces a new WorldTick.
    /// Clients interpolate between ticks; they never advance "truth time".
    /// </summary>
    public class WorldTick
    {
        /// <summary>
        /// Monotonic tick counter. Never resets during a session.
        /// </summary>
        public ulong TickId { get; set; }

        /// <summary>
        /// Unix timestamp in milliseconds when this tick was produced.
        /// </summary>
        public long Timestamp { get; set; }

        /// <summary>
        /// All entities in the world at this tick.
        /// Used for full snapshots (every 60 ticks / 3 seconds).
        /// </summary>
        public List<EntityState> Entities { get; set; } = new();

        /// <summary>
        /// Changes since the previous tick.
        /// Used for delta updates (most common).
        /// </summary>
        public List<EntityDelta> Deltas { get; set; } = new();

        /// <summary>
        /// SHA256 hash of world state for integrity verification.
        /// Clients can verify they're in sync.
        /// </summary>
        public string WorldHash { get; set; }

        /// <summary>
        /// Session this tick belongs to.
        /// </summary>
        public string SessionId { get; set; }

        /// <summary>
        /// Number of connected players at this tick.
        /// </summary>
        public int PlayerCount { get; set; }

        /// <summary>
        /// True if this is a full snapshot (contains all entities).
        /// False if this is a delta update (contains only changes).
        /// </summary>
        public bool IsFullSnapshot { get; set; }

        /// <summary>
        /// Tick rate constants
        /// </summary>
        public static class TickRates
        {
            public const int GAME_TICK_MS = 50;       // 20 Hz - standard tick
            public const int COMBAT_TICK_MS = 33;     // 30 Hz - combat actions
            public const int NPC_TICK_MS = 100;       // 10 Hz - NPC sync
            public const int SNAPSHOT_INTERVAL = 60;  // Full snapshot every 60 ticks (3 sec)
        }

        /// <summary>
        /// Create a new tick with current timestamp.
        /// </summary>
        public static WorldTick Create(ulong tickId, string sessionId)
        {
            return new WorldTick
            {
                TickId = tickId,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                SessionId = sessionId,
                IsFullSnapshot = (tickId % TickRates.SNAPSHOT_INTERVAL) == 0
            };
        }

        /// <summary>
        /// Compute hash of current world state.
        /// Used for sync verification.
        /// </summary>
        public void ComputeHash()
        {
            var sb = new StringBuilder();
            sb.Append(TickId);
            sb.Append(SessionId);

            foreach (var entity in Entities)
            {
                sb.Append(entity.EntityId);
                sb.Append(entity.X);
                sb.Append(entity.Y);
                sb.Append(entity.Z);
                sb.Append(entity.Health);
            }

            using var sha = SHA256.Create();
            var bytes = Encoding.UTF8.GetBytes(sb.ToString());
            var hash = sha.ComputeHash(bytes);
            WorldHash = Convert.ToBase64String(hash);
        }

        /// <summary>
        /// Add an entity state change as a delta.
        /// </summary>
        public void AddDelta(string entityId, DeltaType type, Dictionary<string, object> changes)
        {
            Deltas.Add(new EntityDelta
            {
                EntityId = entityId,
                Type = type,
                Changes = changes,
                SourceTick = TickId
            });
        }

        /// <summary>
        /// Serialize to JSON for network transmission.
        /// </summary>
        public string ToJson()
        {
            return JsonSerializer.Serialize(this, new JsonSerializerOptions
            {
                WriteIndented = false
            });
        }

        /// <summary>
        /// Deserialize from JSON.
        /// </summary>
        public static WorldTick FromJson(string json)
        {
            return JsonSerializer.Deserialize<WorldTick>(json);
        }
    }

    /// <summary>
    /// Tick history for reconciliation and replay.
    /// Server keeps last N ticks for desync recovery.
    /// </summary>
    public class TickHistory
    {
        private readonly Queue<WorldTick> _history = new();
        private readonly int _maxSize;
        private readonly object _lock = new();

        public TickHistory(int maxSize = 100)
        {
            _maxSize = maxSize;
        }

        public void Add(WorldTick tick)
        {
            lock (_lock)
            {
                _history.Enqueue(tick);
                while (_history.Count > _maxSize)
                {
                    _history.Dequeue();
                }
            }
        }

        public WorldTick Get(ulong tickId)
        {
            lock (_lock)
            {
                foreach (var tick in _history)
                {
                    if (tick.TickId == tickId)
                        return tick;
                }
                return null;
            }
        }

        public WorldTick GetLatest()
        {
            lock (_lock)
            {
                return _history.Count > 0 ? _history.ToArray()[^1] : null;
            }
        }

        public int Count => _history.Count;
    }
}

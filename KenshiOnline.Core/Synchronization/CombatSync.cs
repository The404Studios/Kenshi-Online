using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using KenshiOnline.Core.Entities;

namespace KenshiOnline.Core.Synchronization
{
    /// <summary>
    /// Combat event types
    /// </summary>
    public enum CombatEventType
    {
        AttackStarted,
        AttackHit,
        AttackMissed,
        AttackBlocked,
        Damage,
        Death,
        Unconscious,
        Revival,
        StanceChange,
        TargetChange
    }

    /// <summary>
    /// Combat event data
    /// </summary>
    public class CombatEvent
    {
        public Guid EventId { get; set; }
        public CombatEventType Type { get; set; }
        public Guid AttackerId { get; set; }
        public Guid? DefenderId { get; set; }
        public float Damage { get; set; }
        public Vector3 HitPosition { get; set; }
        public string HitBodyPart { get; set; }
        public string AttackAnimation { get; set; }
        public float Timestamp { get; set; }
        public Dictionary<string, object> ExtraData { get; set; }

        public CombatEvent()
        {
            EventId = Guid.NewGuid();
            ExtraData = new Dictionary<string, object>();
            Timestamp = GetCurrentTime();
        }

        private static float GetCurrentTime()
        {
            return (float)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["eventId"] = EventId.ToString(),
                ["type"] = Type.ToString(),
                ["attackerId"] = AttackerId.ToString(),
                ["defenderId"] = DefenderId?.ToString() ?? "",
                ["damage"] = Damage,
                ["hitPosition"] = HitPosition.Serialize(),
                ["hitBodyPart"] = HitBodyPart ?? "",
                ["attackAnimation"] = AttackAnimation ?? "",
                ["timestamp"] = Timestamp,
                ["extraData"] = ExtraData
            };
        }

        public static CombatEvent Deserialize(Dictionary<string, object> data)
        {
            var evt = new CombatEvent();

            if (data.TryGetValue("eventId", out var eventId))
                evt.EventId = Guid.Parse(eventId.ToString());
            if (data.TryGetValue("type", out var type))
                evt.Type = Enum.Parse<CombatEventType>(type.ToString());
            if (data.TryGetValue("attackerId", out var attackerId))
                evt.AttackerId = Guid.Parse(attackerId.ToString());
            if (data.TryGetValue("defenderId", out var defenderId) && !string.IsNullOrEmpty(defenderId.ToString()))
                evt.DefenderId = Guid.Parse(defenderId.ToString());
            if (data.TryGetValue("damage", out var damage))
                evt.Damage = Convert.ToSingle(damage);
            if (data.TryGetValue("hitPosition", out var hitPosition) && hitPosition is Dictionary<string, object> posDict)
                evt.HitPosition = Vector3.Deserialize(posDict);
            if (data.TryGetValue("hitBodyPart", out var hitBodyPart))
                evt.HitBodyPart = hitBodyPart.ToString();
            if (data.TryGetValue("attackAnimation", out var attackAnimation))
                evt.AttackAnimation = attackAnimation.ToString();
            if (data.TryGetValue("timestamp", out var timestamp))
                evt.Timestamp = Convert.ToSingle(timestamp);
            if (data.TryGetValue("extraData", out var extraData) && extraData is Dictionary<string, object> extra)
                evt.ExtraData = extra;

            return evt;
        }
    }

    /// <summary>
    /// Handles combat synchronization across clients
    /// Server-authoritative combat system
    /// </summary>
    public class CombatSync
    {
        private readonly EntityManager _entityManager;
        private readonly ConcurrentQueue<CombatEvent> _pendingEvents;
        private readonly ConcurrentDictionary<Guid, CombatEvent> _recentEvents;
        private readonly object _lock = new object();

        // Combat settings
        public bool ServerAuthoritative { get; set; } = true;
        public float EventRetentionTime { get; set; } = 5.0f; // Keep events for 5 seconds
        public float HitVerificationWindow { get; set; } = 0.2f; // 200ms window for hit verification

        // Statistics
        public int TotalCombatEvents { get; private set; }
        public int TotalDamageDealt { get; private set; }

        public CombatSync(EntityManager entityManager)
        {
            _entityManager = entityManager;
            _pendingEvents = new ConcurrentQueue<CombatEvent>();
            _recentEvents = new ConcurrentDictionary<Guid, CombatEvent>();
        }

        #region Combat Events

        /// <summary>
        /// Process attack from client
        /// Server validates and applies damage
        /// </summary>
        public CombatEvent ProcessAttack(Guid attackerId, Guid defenderId, float damage, string animation)
        {
            var attacker = _entityManager.GetEntity(attackerId);
            var defender = _entityManager.GetEntity(defenderId);

            if (attacker == null || defender == null)
                return null;

            // Validate attack
            if (!CanAttack(attacker, defender))
                return null;

            // Calculate actual damage (server-side)
            var actualDamage = CalculateDamage(attacker, defender, damage);

            // Create combat event
            var evt = new CombatEvent
            {
                Type = actualDamage > 0 ? CombatEventType.AttackHit : CombatEventType.AttackMissed,
                AttackerId = attackerId,
                DefenderId = defenderId,
                Damage = actualDamage,
                HitPosition = defender.Position,
                AttackAnimation = animation
            };

            // Apply damage
            if (actualDamage > 0)
            {
                ApplyDamage(defender, actualDamage, evt);
            }

            // Queue event for broadcasting
            _pendingEvents.Enqueue(evt);
            _recentEvents[evt.EventId] = evt;
            TotalCombatEvents++;
            TotalDamageDealt += (int)actualDamage;

            return evt;
        }

        /// <summary>
        /// Process damage to entity
        /// </summary>
        public void ProcessDamage(Guid targetId, float damage, Guid? attackerId = null)
        {
            var target = _entityManager.GetEntity(targetId);
            if (target == null)
                return;

            var evt = new CombatEvent
            {
                Type = CombatEventType.Damage,
                AttackerId = attackerId ?? Guid.Empty,
                DefenderId = targetId,
                Damage = damage
            };

            ApplyDamage(target, damage, evt);

            _pendingEvents.Enqueue(evt);
            _recentEvents[evt.EventId] = evt;
        }

        /// <summary>
        /// Process stance change
        /// </summary>
        public void ProcessStanceChange(Guid entityId, string newStance)
        {
            var entity = _entityManager.GetEntity(entityId);
            if (entity == null)
                return;

            // Update stance
            if (entity is PlayerEntity player)
            {
                player.CombatStance = newStance;
                player.MarkDirty();
            }
            else if (entity is NPCEntity npc)
            {
                npc.CombatStance = newStance;
                npc.MarkDirty();
            }

            var evt = new CombatEvent
            {
                Type = CombatEventType.StanceChange,
                AttackerId = entityId
            };
            evt.ExtraData["stance"] = newStance;

            _pendingEvents.Enqueue(evt);
            _recentEvents[evt.EventId] = evt;
        }

        /// <summary>
        /// Process target change
        /// </summary>
        public void ProcessTargetChange(Guid entityId, Guid? newTargetId)
        {
            var entity = _entityManager.GetEntity(entityId);
            if (entity == null)
                return;

            // Update target
            if (entity is PlayerEntity player)
            {
                player.TargetId = newTargetId;
                player.IsInCombat = newTargetId.HasValue;
                player.MarkDirty();
            }
            else if (entity is NPCEntity npc)
            {
                npc.TargetId = newTargetId;
                npc.IsInCombat = newTargetId.HasValue;
                npc.MarkDirty();
            }

            var evt = new CombatEvent
            {
                Type = CombatEventType.TargetChange,
                AttackerId = entityId,
                DefenderId = newTargetId
            };

            _pendingEvents.Enqueue(evt);
            _recentEvents[evt.EventId] = evt;
        }

        #endregion

        #region Combat Logic

        /// <summary>
        /// Check if attacker can attack defender
        /// </summary>
        private bool CanAttack(Entity attacker, Entity defender)
        {
            // Check if entities can act
            if (attacker is PlayerEntity attackerPlayer && !attackerPlayer.CanAct())
                return false;
            if (attacker is NPCEntity attackerNPC && !attackerNPC.CanAct())
                return false;

            if (defender is PlayerEntity defenderPlayer && !defenderPlayer.CanAct())
                return false;
            if (defender is NPCEntity defenderNPC && !defenderNPC.CanAct())
                return false;

            // Check range (simple distance check)
            var distance = Vector3.Distance(attacker.Position, defender.Position);
            if (distance > 5.0f) // 5 meters max attack range
                return false;

            return true;
        }

        /// <summary>
        /// Calculate actual damage (server-side validation)
        /// </summary>
        private float CalculateDamage(Entity attacker, Entity defender, float baseDamage)
        {
            if (!ServerAuthoritative)
                return baseDamage; // Trust client

            float damage = baseDamage;

            // Apply attacker's weapon damage
            if (attacker is PlayerEntity attackerPlayer && attackerPlayer.Equipment.TryGetValue("Weapon", out var weaponId))
            {
                var weapon = _entityManager.GetEntity<ItemEntity>(weaponId);
                if (weapon != null)
                {
                    damage = weapon.WeaponDamage;
                }
            }
            else if (attacker is NPCEntity attackerNPC && attackerNPC.Equipment.TryGetValue("Weapon", out weaponId))
            {
                var weapon = _entityManager.GetEntity<ItemEntity>(weaponId);
                if (weapon != null)
                {
                    damage = weapon.WeaponDamage;
                }
            }

            // Apply defender's armor
            if (defender is PlayerEntity defenderPlayer && defenderPlayer.Equipment.TryGetValue("Armor", out var armorId))
            {
                var armor = _entityManager.GetEntity<ItemEntity>(armorId);
                if (armor != null)
                {
                    damage *= (1.0f - (armor.ArmorValue / 100f));
                }
            }
            else if (defender is NPCEntity defenderNPC && defenderNPC.Equipment.TryGetValue("Armor", out armorId))
            {
                var armor = _entityManager.GetEntity<ItemEntity>(armorId);
                if (armor != null)
                {
                    damage *= (1.0f - (armor.ArmorValue / 100f));
                }
            }

            // Apply skill modifiers (simplified)
            // In real Kenshi, this would be much more complex

            return Math.Max(0, damage);
        }

        /// <summary>
        /// Apply damage to entity
        /// </summary>
        private void ApplyDamage(Entity target, float damage, CombatEvent evt)
        {
            if (target is PlayerEntity player)
            {
                player.TakeDamage(damage);

                if (!player.IsAlive)
                {
                    var deathEvent = new CombatEvent
                    {
                        Type = CombatEventType.Death,
                        AttackerId = evt.AttackerId,
                        DefenderId = target.Id
                    };
                    _pendingEvents.Enqueue(deathEvent);
                }
                else if (player.IsUnconscious)
                {
                    var unconsciousEvent = new CombatEvent
                    {
                        Type = CombatEventType.Unconscious,
                        AttackerId = evt.AttackerId,
                        DefenderId = target.Id
                    };
                    _pendingEvents.Enqueue(unconsciousEvent);
                }
            }
            else if (target is NPCEntity npc)
            {
                npc.TakeDamage(damage);

                if (!npc.IsAlive)
                {
                    var deathEvent = new CombatEvent
                    {
                        Type = CombatEventType.Death,
                        AttackerId = evt.AttackerId,
                        DefenderId = target.Id
                    };
                    _pendingEvents.Enqueue(deathEvent);
                }
                else if (npc.IsUnconscious)
                {
                    var unconsciousEvent = new CombatEvent
                    {
                        Type = CombatEventType.Unconscious,
                        AttackerId = evt.AttackerId,
                        DefenderId = target.Id
                    };
                    _pendingEvents.Enqueue(unconsciousEvent);
                }
            }
        }

        #endregion

        #region Event Management

        /// <summary>
        /// Get pending combat events (for broadcasting to clients)
        /// </summary>
        public IEnumerable<CombatEvent> GetPendingEvents()
        {
            var events = new List<CombatEvent>();
            while (_pendingEvents.TryDequeue(out var evt))
            {
                events.Add(evt);
            }
            return events;
        }

        /// <summary>
        /// Get recent combat events for observer
        /// </summary>
        public IEnumerable<CombatEvent> GetEventsForObserver(Entity observer)
        {
            var events = new List<CombatEvent>();
            var currentTime = (float)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;

            foreach (var evt in _recentEvents.Values)
            {
                // Only send events within retention time
                if (currentTime - evt.Timestamp > EventRetentionTime)
                    continue;

                // Only send events in observer's sync range
                Entity relevantEntity = null;
                if (evt.DefenderId.HasValue)
                    relevantEntity = _entityManager.GetEntity(evt.DefenderId.Value);
                else
                    relevantEntity = _entityManager.GetEntity(evt.AttackerId);

                if (relevantEntity != null && relevantEntity.IsInSyncRange(observer))
                {
                    events.Add(evt);
                }
            }

            return events;
        }

        /// <summary>
        /// Clean up old events
        /// </summary>
        public void Update(float deltaTime)
        {
            var currentTime = (float)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
            var toRemove = new List<Guid>();

            foreach (var kvp in _recentEvents)
            {
                if (currentTime - kvp.Value.Timestamp > EventRetentionTime)
                {
                    toRemove.Add(kvp.Key);
                }
            }

            foreach (var id in toRemove)
            {
                _recentEvents.TryRemove(id, out _);
            }
        }

        #endregion
    }
}

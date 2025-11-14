using System;
using System.Collections.Generic;

namespace KenshiOnline.Core.Entities
{
    /// <summary>
    /// Player character entity with full state
    /// </summary>
    public class PlayerEntity : Entity
    {
        // Player info
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public int Level { get; set; }

        // Character stats
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public float Hunger { get; set; }
        public float Blood { get; set; }

        // Character state
        public bool IsAlive { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsInCombat { get; set; }
        public bool IsSneaking { get; set; }
        public bool IsRunning { get; set; }

        // Skills (Kenshi has ~20 combat/athletic skills)
        public Dictionary<string, int> Skills { get; set; }

        // Equipment
        public Dictionary<string, Guid> Equipment { get; set; } // Slot -> Item ID

        // Inventory
        public List<Guid> Inventory { get; set; }
        public int InventoryCapacity { get; set; }

        // Faction
        public string FactionId { get; set; }
        public Dictionary<string, float> FactionRelations { get; set; }

        // Squad
        public Guid? SquadId { get; set; }
        public bool IsSquadLeader { get; set; }

        // Combat
        public Guid? TargetId { get; set; }
        public string CombatStance { get; set; }
        public float CombatSpeed { get; set; }

        // Animation state (for visual sync)
        public string CurrentAnimation { get; set; }
        public float AnimationTime { get; set; }

        public PlayerEntity()
        {
            Type = EntityType.Player;
            Skills = new Dictionary<string, int>();
            Equipment = new Dictionary<string, Guid>();
            Inventory = new List<Guid>();
            FactionRelations = new Dictionary<string, float>();
            IsAlive = true;
            Health = 100f;
            MaxHealth = 100f;
            Hunger = 100f;
            Blood = 100f;
            InventoryCapacity = 100;
            Priority = 10; // High priority for players
        }

        public override Dictionary<string, object> Serialize()
        {
            var data = base.Serialize();

            // Player specific data
            data["playerId"] = PlayerId ?? "";
            data["playerName"] = PlayerName ?? "";
            data["level"] = Level;

            // Stats
            data["health"] = Health;
            data["maxHealth"] = MaxHealth;
            data["hunger"] = Hunger;
            data["blood"] = Blood;

            // State
            data["isAlive"] = IsAlive;
            data["isUnconscious"] = IsUnconscious;
            data["isInCombat"] = IsInCombat;
            data["isSneaking"] = IsSneaking;
            data["isRunning"] = IsRunning;

            // Skills
            data["skills"] = Skills;

            // Equipment
            data["equipment"] = Equipment;

            // Inventory
            data["inventory"] = Inventory;
            data["inventoryCapacity"] = InventoryCapacity;

            // Faction
            data["factionId"] = FactionId ?? "";
            data["factionRelations"] = FactionRelations;

            // Squad
            data["squadId"] = SquadId?.ToString() ?? "";
            data["isSquadLeader"] = IsSquadLeader;

            // Combat
            data["targetId"] = TargetId?.ToString() ?? "";
            data["combatStance"] = CombatStance ?? "";
            data["combatSpeed"] = CombatSpeed;

            // Animation
            data["currentAnimation"] = CurrentAnimation ?? "";
            data["animationTime"] = AnimationTime;

            return data;
        }

        public override void Deserialize(Dictionary<string, object> data)
        {
            base.Deserialize(data);

            // Player specific
            if (data.TryGetValue("playerId", out var playerId))
                PlayerId = playerId.ToString();
            if (data.TryGetValue("playerName", out var playerName))
                PlayerName = playerName.ToString();
            if (data.TryGetValue("level", out var level))
                Level = Convert.ToInt32(level);

            // Stats
            if (data.TryGetValue("health", out var health))
                Health = Convert.ToSingle(health);
            if (data.TryGetValue("maxHealth", out var maxHealth))
                MaxHealth = Convert.ToSingle(maxHealth);
            if (data.TryGetValue("hunger", out var hunger))
                Hunger = Convert.ToSingle(hunger);
            if (data.TryGetValue("blood", out var blood))
                Blood = Convert.ToSingle(blood);

            // State
            if (data.TryGetValue("isAlive", out var isAlive))
                IsAlive = Convert.ToBoolean(isAlive);
            if (data.TryGetValue("isUnconscious", out var isUnconscious))
                IsUnconscious = Convert.ToBoolean(isUnconscious);
            if (data.TryGetValue("isInCombat", out var isInCombat))
                IsInCombat = Convert.ToBoolean(isInCombat);
            if (data.TryGetValue("isSneaking", out var isSneaking))
                IsSneaking = Convert.ToBoolean(isSneaking);
            if (data.TryGetValue("isRunning", out var isRunning))
                IsRunning = Convert.ToBoolean(isRunning);

            // Equipment
            if (data.TryGetValue("equipment", out var equipment) && equipment is Dictionary<string, object> equipDict)
            {
                Equipment.Clear();
                foreach (var kvp in equipDict)
                {
                    Equipment[kvp.Key] = Guid.Parse(kvp.Value.ToString());
                }
            }

            // Inventory
            if (data.TryGetValue("inventory", out var inventory) && inventory is List<object> invList)
            {
                Inventory.Clear();
                foreach (var item in invList)
                {
                    Inventory.Add(Guid.Parse(item.ToString()));
                }
            }

            // Faction
            if (data.TryGetValue("factionId", out var factionId))
                FactionId = factionId.ToString();

            // Squad
            if (data.TryGetValue("squadId", out var squadId) && !string.IsNullOrEmpty(squadId.ToString()))
                SquadId = Guid.Parse(squadId.ToString());
            if (data.TryGetValue("isSquadLeader", out var isSquadLeader))
                IsSquadLeader = Convert.ToBoolean(isSquadLeader);

            // Combat
            if (data.TryGetValue("targetId", out var targetId) && !string.IsNullOrEmpty(targetId.ToString()))
                TargetId = Guid.Parse(targetId.ToString());
            if (data.TryGetValue("combatStance", out var combatStance))
                CombatStance = combatStance.ToString();
            if (data.TryGetValue("combatSpeed", out var combatSpeed))
                CombatSpeed = Convert.ToSingle(combatSpeed);

            // Animation
            if (data.TryGetValue("currentAnimation", out var currentAnimation))
                CurrentAnimation = currentAnimation.ToString();
            if (data.TryGetValue("animationTime", out var animationTime))
                AnimationTime = Convert.ToSingle(animationTime);
        }

        /// <summary>
        /// Get health percentage
        /// </summary>
        public float GetHealthPercent()
        {
            return MaxHealth > 0 ? (Health / MaxHealth) * 100f : 0f;
        }

        /// <summary>
        /// Check if player can perform actions
        /// </summary>
        public bool CanAct()
        {
            return IsAlive && !IsUnconscious;
        }

        /// <summary>
        /// Apply damage to player
        /// </summary>
        public void TakeDamage(float amount)
        {
            Health = Math.Max(0, Health - amount);
            if (Health <= 0)
            {
                IsAlive = false;
                IsUnconscious = true;
            }
            MarkDirty();
        }

        /// <summary>
        /// Heal player
        /// </summary>
        public void Heal(float amount)
        {
            Health = Math.Min(MaxHealth, Health + amount);
            if (Health > 0 && !IsAlive)
            {
                IsAlive = true;
                IsUnconscious = false;
            }
            MarkDirty();
        }
    }
}

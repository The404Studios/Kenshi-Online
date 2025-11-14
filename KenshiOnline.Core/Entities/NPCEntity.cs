using System;
using System.Collections.Generic;

namespace KenshiOnline.Core.Entities
{
    /// <summary>
    /// NPC entity with AI state and behavior
    /// </summary>
    public class NPCEntity : Entity
    {
        // NPC info
        public string NPCName { get; set; }
        public string NPCType { get; set; } // Guard, Merchant, Animal, etc.
        public int Level { get; set; }

        // Character stats (same as player)
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

        // AI state
        public string AIState { get; set; } // Idle, Patrol, Combat, Flee, etc.
        public Guid? AITargetId { get; set; }
        public List<Vector3> PatrolRoute { get; set; }
        public int PatrolIndex { get; set; }
        public float AIUpdateInterval { get; set; } = 1.0f;
        public float LastAIUpdate { get; set; }

        // Skills
        public Dictionary<string, int> Skills { get; set; }

        // Equipment
        public Dictionary<string, Guid> Equipment { get; set; }

        // Inventory
        public List<Guid> Inventory { get; set; }
        public int InventoryCapacity { get; set; }

        // Faction
        public string FactionId { get; set; }
        public Dictionary<string, float> FactionRelations { get; set; }

        // Squad
        public Guid? SquadId { get; set; }
        public bool IsSquadLeader { get; set; }
        public List<Guid> SquadMembers { get; set; }

        // Combat
        public Guid? TargetId { get; set; }
        public string CombatStance { get; set; }
        public float CombatSpeed { get; set; }

        // Animation state
        public string CurrentAnimation { get; set; }
        public float AnimationTime { get; set; }

        // Merchant specific
        public bool IsMerchant { get; set; }
        public Dictionary<string, int> MerchantInventory { get; set; } // ItemID -> Quantity
        public int Money { get; set; }

        // Job/Schedule
        public string CurrentJob { get; set; }
        public string Schedule { get; set; }

        public NPCEntity()
        {
            Type = EntityType.NPC;
            Skills = new Dictionary<string, int>();
            Equipment = new Dictionary<string, Guid>();
            Inventory = new List<Guid>();
            FactionRelations = new Dictionary<string, float>();
            PatrolRoute = new List<Vector3>();
            SquadMembers = new List<Guid>();
            MerchantInventory = new Dictionary<string, int>();
            IsAlive = true;
            Health = 100f;
            MaxHealth = 100f;
            Hunger = 100f;
            Blood = 100f;
            InventoryCapacity = 100;
            Priority = 5; // Medium priority for NPCs
            AIState = "Idle";
        }

        public override Dictionary<string, object> Serialize()
        {
            var data = base.Serialize();

            // NPC info
            data["npcName"] = NPCName ?? "";
            data["npcType"] = NPCType ?? "";
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

            // AI state
            data["aiState"] = AIState ?? "Idle";
            data["aiTargetId"] = AITargetId?.ToString() ?? "";
            data["patrolIndex"] = PatrolIndex;

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
            data["squadMembers"] = SquadMembers;

            // Combat
            data["targetId"] = TargetId?.ToString() ?? "";
            data["combatStance"] = CombatStance ?? "";
            data["combatSpeed"] = CombatSpeed;

            // Animation
            data["currentAnimation"] = CurrentAnimation ?? "";
            data["animationTime"] = AnimationTime;

            // Merchant
            data["isMerchant"] = IsMerchant;
            data["money"] = Money;

            // Job
            data["currentJob"] = CurrentJob ?? "";

            return data;
        }

        public override void Deserialize(Dictionary<string, object> data)
        {
            base.Deserialize(data);

            // NPC info
            if (data.TryGetValue("npcName", out var npcName))
                NPCName = npcName.ToString();
            if (data.TryGetValue("npcType", out var npcType))
                NPCType = npcType.ToString();
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

            // AI state
            if (data.TryGetValue("aiState", out var aiState))
                AIState = aiState.ToString();
            if (data.TryGetValue("aiTargetId", out var aiTargetId) && !string.IsNullOrEmpty(aiTargetId.ToString()))
                AITargetId = Guid.Parse(aiTargetId.ToString());
            if (data.TryGetValue("patrolIndex", out var patrolIndex))
                PatrolIndex = Convert.ToInt32(patrolIndex);

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
            if (data.TryGetValue("squadMembers", out var squadMembers) && squadMembers is List<object> squadList)
            {
                SquadMembers.Clear();
                foreach (var member in squadList)
                {
                    SquadMembers.Add(Guid.Parse(member.ToString()));
                }
            }

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

            // Merchant
            if (data.TryGetValue("isMerchant", out var isMerchant))
                IsMerchant = Convert.ToBoolean(isMerchant);
            if (data.TryGetValue("money", out var money))
                Money = Convert.ToInt32(money);

            // Job
            if (data.TryGetValue("currentJob", out var currentJob))
                CurrentJob = currentJob.ToString();
        }

        /// <summary>
        /// Get health percentage
        /// </summary>
        public float GetHealthPercent()
        {
            return MaxHealth > 0 ? (Health / MaxHealth) * 100f : 0f;
        }

        /// <summary>
        /// Check if NPC can perform actions
        /// </summary>
        public bool CanAct()
        {
            return IsAlive && !IsUnconscious;
        }

        /// <summary>
        /// Apply damage to NPC
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
        /// Heal NPC
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

        /// <summary>
        /// Update AI state (called by server)
        /// </summary>
        public void UpdateAI(float deltaTime)
        {
            LastAIUpdate += deltaTime;
            if (LastAIUpdate >= AIUpdateInterval)
            {
                LastAIUpdate = 0f;

                // AI state machine would go here
                // This is server-authoritative
                switch (AIState)
                {
                    case "Patrol":
                        UpdatePatrol();
                        break;
                    case "Combat":
                        UpdateCombat();
                        break;
                    case "Flee":
                        UpdateFlee();
                        break;
                    case "Idle":
                    default:
                        UpdateIdle();
                        break;
                }

                MarkDirty();
            }
        }

        private void UpdatePatrol()
        {
            // Move along patrol route
            if (PatrolRoute.Count > 0)
            {
                var target = PatrolRoute[PatrolIndex];
                var distance = Vector3.Distance(Position, target);

                if (distance < 5.0f)
                {
                    // Reached waypoint, move to next
                    PatrolIndex = (PatrolIndex + 1) % PatrolRoute.Count;
                }
            }
        }

        private void UpdateCombat()
        {
            // Combat AI logic would go here
            // Check if target is still valid, in range, etc.
        }

        private void UpdateFlee()
        {
            // Flee AI logic would go here
            // Run away from threat
        }

        private void UpdateIdle()
        {
            // Idle AI logic would go here
            // Maybe random wandering, looking around, etc.
        }
    }
}

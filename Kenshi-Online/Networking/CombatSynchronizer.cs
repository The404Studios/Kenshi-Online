using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Manages deterministic combat synchronization across all clients
    /// </summary>
    public class CombatSynchronizer
    {
        // Combat state management
        private readonly ConcurrentDictionary<string, CombatSession> activeCombats = new ConcurrentDictionary<string, CombatSession>();
        private readonly ConcurrentDictionary<string, CombatantState> combatants = new ConcurrentDictionary<string, CombatantState>();
        
        // Deterministic combat resolution
        private readonly CombatResolver combatResolver = new CombatResolver();
        private readonly DamageCalculator damageCalculator = new DamageCalculator();
        private readonly LimbSystemManager limbSystem = new LimbSystemManager();
        private readonly BlockingSystem blockingSystem = new BlockingSystem();
        
        // Animation synchronization
        private readonly AnimationSynchronizer animationSync = new AnimationSynchronizer();
        
        // Configuration
        private readonly CombatConfig config;
        private readonly int combatTickRate = 30; // 30Hz for combat
        private readonly float maxCombatRange = 5.0f;
        
        // Random seed for deterministic outcomes
        private readonly Random deterministicRandom;
        
        public CombatSynchronizer(CombatConfig configuration = null)
        {
            config = configuration ?? new CombatConfig();
            deterministicRandom = new Random(config.RandomSeed);
            
            InitializeSystems();
        }
        
        /// <summary>
        /// Initialize combat subsystems
        /// </summary>
        private void InitializeSystems()
        {
            combatResolver.Initialize(config);
            damageCalculator.Initialize(config.DamageSettings);
            limbSystem.Initialize(config.LimbSettings);
            blockingSystem.Initialize(config.BlockSettings);
            animationSync.Initialize(config.AnimationSettings);
            
            Logger.Log("Combat synchronizer initialized");
        }
        
        /// <summary>
        /// Process combat action from player
        /// </summary>
        public CombatResult ProcessCombatAction(string attackerId, CombatAction action)
        {
            // Validate action
            if (!ValidateCombatAction(attackerId, action))
            {
                return new CombatResult
                {
                    Success = false,
                    Reason = "Invalid combat action"
                };
            }
            
            // Get or create combat session
            var session = GetOrCreateCombatSession(attackerId, action.TargetId);
            
            // Get combatant states
            var attacker = GetCombatantState(attackerId);
            var target = GetCombatantState(action.TargetId);
            
            // Check combat conditions
            if (!CanPerformCombat(attacker, target, action))
            {
                return new CombatResult
                {
                    Success = false,
                    Reason = "Combat conditions not met"
                };
            }
            
            // Resolve combat action
            var result = ResolveCombatAction(attacker, target, action, session);
            
            // Apply results
            ApplyCombatResults(result, attacker, target);
            
            // Sync animation
            SyncCombatAnimation(attackerId, action.TargetId, action, result);
            
            // Update session
            UpdateCombatSession(session, result);
            
            // Broadcast result
            BroadcastCombatResult(result);
            
            return result;
        }
        
        /// <summary>
        /// Validate combat action
        /// </summary>
        private bool ValidateCombatAction(string attackerId, CombatAction action)
        {
            // Check if attacker exists
            if (!combatants.ContainsKey(attackerId))
                return false;
            
            // Check if target exists
            if (!combatants.ContainsKey(action.TargetId))
                return false;
            
            // Check action type
            if (!IsValidActionType(action.Action))
                return false;
            
            // Check weapon validity
            if (!string.IsNullOrEmpty(action.WeaponId) && !IsValidWeapon(action.WeaponId))
                return false;
            
            return true;
        }
        
        /// <summary>
        /// Check if combat can be performed
        /// </summary>
        private bool CanPerformCombat(CombatantState attacker, CombatantState target, CombatAction action)
        {
            // Check if attacker is alive and conscious
            if (attacker.Health <= 0 || attacker.IsUnconscious)
                return false;
            
            // Check if target is valid
            if (target.Health <= 0)
                return false;
            
            // Check range
            var distance = Vector3.Distance(attacker.Position, target.Position);
            var weaponReach = GetWeaponReach(action.WeaponId);
            
            if (distance > weaponReach + 0.5f) // Small buffer for lag
                return false;
            
            // Check attack cooldown
            var timeSinceLastAttack = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - attacker.LastAttackTime;
            var attackSpeed = GetAttackSpeed(attacker, action.WeaponId);
            
            if (timeSinceLastAttack < attackSpeed)
                return false;
            
            // Check stamina
            if (attacker.Stamina < GetActionStaminaCost(action.Action))
                return false;
            
            return true;
        }
        
        /// <summary>
        /// Resolve combat action deterministically
        /// </summary>
        private CombatResult ResolveCombatAction(CombatantState attacker, CombatantState target, CombatAction action, CombatSession session)
        {
            var result = new CombatResult
            {
                AttackerId = attacker.Id,
                TargetId = target.Id,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
            
            // Check if target is blocking
            if (target.IsBlocking && blockingSystem.CheckBlock(attacker, target, action))
            {
                result.Blocked = true;
                result.BlockDamage = CalculateBlockDamage(action);
                
                // Stamina drain for blocking
                target.Stamina -= result.BlockDamage * 0.5f;
                
                // Chance to break block
                if (deterministicRandom.NextDouble() < 0.1f) // 10% chance
                {
                    target.IsBlocking = false;
                    result.BlockBroken = true;
                }
                
                return result;
            }
            
            // Calculate hit chance
            float hitChance = CalculateHitChance(attacker, target, action);
            
            // Use deterministic random with seed based on combat state
            var combatSeed = GenerateCombatSeed(session, attacker, target);
            var combatRandom = new Random(combatSeed);
            
            if (combatRandom.NextDouble() > hitChance)
            {
                result.Hit = false;
                result.Miss = true;
                return result;
            }
            
            result.Hit = true;
            
            // Determine hit location
            result.HitLocation = DetermineHitLocation(action, target, combatRandom);
            
            // Calculate damage
            var damageInfo = damageCalculator.CalculateDamage(attacker, target, action, result.HitLocation);
            result.Damage = damageInfo.TotalDamage;
            result.DamageType = damageInfo.Type;
            
            // Check for critical hit
            if (combatRandom.NextDouble() < CalculateCritChance(attacker, target))
            {
                result.Critical = true;
                result.Damage *= config.CriticalMultiplier;
            }
            
            // Apply limb damage
            var limbDamage = limbSystem.ApplyLimbDamage(target, result.HitLocation, result.Damage, result.DamageType);
            result.LimbDamage = limbDamage;
            
            // Check for dismemberment
            if (limbDamage.Severed)
            {
                result.Dismemberment = true;
                result.SeveredLimb = result.HitLocation;
            }
            
            // Calculate bleeding
            if (result.DamageType == "Cut" || result.DamageType == "Pierce")
            {
                result.BleedDamage = CalculateBleedDamage(result.Damage, result.HitLocation);
                target.BleedRate += result.BleedDamage;
            }
            
            // Knockback/stun
            if (result.Damage > target.Health * 0.3f)
            {
                result.Knockback = true;
                result.KnockbackForce = CalculateKnockback(action, result.Damage);
            }
            
            // Update attacker state
            attacker.LastAttackTime = result.Timestamp;
            attacker.Stamina -= GetActionStaminaCost(action.Action);
            
            return result;
        }
        
        /// <summary>
        /// Calculate hit chance
        /// </summary>
        private float CalculateHitChance(CombatantState attacker, CombatantState target, CombatAction action)
        {
            // Base hit chance
            float hitChance = 0.75f;
            
            // Attacker skill bonus
            float attackSkill = attacker.Skills.GetValueOrDefault(GetWeaponSkill(action.WeaponId), 0);
            hitChance += attackSkill / 200.0f; // Max +50% at 100 skill
            
            // Target dodge skill
            float dodgeSkill = target.Skills.GetValueOrDefault("Dodge", 0);
            hitChance -= dodgeSkill / 400.0f; // Max -25% at 100 dodge
            
            // Dexterity difference
            float dexDiff = (attacker.Dexterity - target.Dexterity) / 100.0f;
            hitChance += dexDiff * 0.2f;
            
            // Encumbrance penalty
            hitChance -= attacker.Encumbrance * 0.3f;
            
            // Injury penalties
            if (attacker.LimbHealth["RightArm"] < 50)
                hitChance -= 0.2f;
            
            // Environmental factors (would need to be passed in)
            // Rain, darkness, etc.
            
            return Math.Max(0.1f, Math.Min(0.95f, hitChance));
        }
        
        /// <summary>
        /// Calculate critical hit chance
        /// </summary>
        private float CalculateCritChance(CombatantState attacker, CombatantState target)
        {
            float critChance = 0.05f; // Base 5%
            
            // Dexterity bonus
            critChance += attacker.Dexterity / 1000.0f; // Max +10% at 100 dex
            
            // Weapon skill bonus
            float weaponSkill = attacker.Skills.GetValueOrDefault("Melee Attack", 0);
            critChance += weaponSkill / 500.0f; // Max +20% at 100 skill
            
            // Target toughness reduces crit chance
            critChance -= target.Toughness / 2000.0f; // Max -5% at 100 toughness
            
            return Math.Max(0.01f, Math.Min(0.5f, critChance));
        }
        
        /// <summary>
        /// Determine hit location
        /// </summary>
        private string DetermineHitLocation(CombatAction action, CombatantState target, Random random)
        {
            // If specific limb targeted
            if (!string.IsNullOrEmpty(action.TargetLimb))
            {
                // 70% chance to hit targeted limb
                if (random.NextDouble() < 0.7f)
                    return action.TargetLimb;
            }
            
            // Random hit location with weights
            var locations = new[]
            {
                ("Head", 0.1f),
                ("Chest", 0.3f),
                ("Stomach", 0.2f),
                ("LeftArm", 0.1f),
                ("RightArm", 0.1f),
                ("LeftLeg", 0.1f),
                ("RightLeg", 0.1f)
            };
            
            float roll = (float)random.NextDouble();
            float current = 0;
            
            foreach (var (location, weight) in locations)
            {
                current += weight;
                if (roll <= current)
                    return location;
            }
            
            return "Chest"; // Default
        }
        
        /// <summary>
        /// Calculate bleed damage
        /// </summary>
        private float CalculateBleedDamage(int damage, string location)
        {
            float bleedRate = damage * 0.1f; // Base 10% of damage as bleed
            
            // Location modifiers
            bleedRate *= location switch
            {
                "Head" => 1.5f,
                "Chest" => 1.2f,
                "Stomach" => 1.3f,
                _ => 1.0f
            };
            
            return bleedRate;
        }
        
        /// <summary>
        /// Calculate knockback force
        /// </summary>
        private float CalculateKnockback(CombatAction action, int damage)
        {
            float force = damage / 10.0f;
            
            // Weapon weight modifier
            if (action.WeaponId?.Contains("heavy") == true)
                force *= 1.5f;
            
            return Math.Min(force, 10.0f);
        }
        
        /// <summary>
        /// Apply combat results to combatants
        /// </summary>
        private void ApplyCombatResults(CombatResult result, CombatantState attacker, CombatantState target)
        {
            if (result.Hit)
            {
                // Apply damage
                target.Health -= result.Damage;
                
                // Apply limb damage
                if (result.LimbDamage != null)
                {
                    target.LimbHealth[result.HitLocation] = result.LimbDamage.RemainingHealth;
                    
                    if (result.LimbDamage.Severed)
                    {
                        target.SeveredLimbs.Add(result.HitLocation);
                    }
                }
                
                // Apply knockback
                if (result.Knockback)
                {
                    var knockbackDir = Vector3.Normalize(target.Position - attacker.Position);
                    target.Velocity = knockbackDir * result.KnockbackForce;
                    target.IsStunned = true;
                    target.StunDuration = 1000; // 1 second
                }
                
                // Check for unconscious
                if (target.Health <= 0)
                {
                    target.IsUnconscious = true;
                    target.ConsciousnessTimer = 300000; // 5 minutes
                }
                else if (target.Health < 20 && result.HitLocation == "Head")
                {
                    // Head trauma can cause unconsciousness
                    if (deterministicRandom.NextDouble() < 0.3f)
                    {
                        target.IsUnconscious = true;
                        target.ConsciousnessTimer = 60000; // 1 minute
                    }
                }
                
                // Update combat stats
                attacker.HitsLanded++;
                target.HitsTaken++;
                
                if (result.Critical)
                    attacker.CriticalHits++;
            }
            else
            {
                attacker.Misses++;
                
                if (result.Blocked)
                {
                    target.BlocksSuccessful++;
                    
                    if (result.BlockBroken)
                    {
                        target.BlocksBroken++;
                    }
                }
            }
        }
        
        /// <summary>
        /// Sync combat animations across clients
        /// </summary>
        private void SyncCombatAnimation(string attackerId, string targetId, CombatAction action, CombatResult result)
        {
            var attackAnim = new AnimationData
            {
                EntityId = attackerId,
                AnimationName = GetAttackAnimation(action, result),
                Speed = 1.0f,
                StartTime = result.Timestamp
            };
            
            var targetAnim = new AnimationData
            {
                EntityId = targetId,
                AnimationName = GetHitAnimation(result),
                Speed = 1.0f,
                StartTime = result.Timestamp + 100 // Slight delay for impact
            };
            
            animationSync.QueueAnimation(attackAnim);
            animationSync.QueueAnimation(targetAnim);
        }
        
        /// <summary>
        /// Get attack animation name
        /// </summary>
        private string GetAttackAnimation(CombatAction action, CombatResult result)
        {
            if (result.Miss)
                return "attack_miss";
            
            string baseAnim = action.Action switch
            {
                "attack" => "attack_normal",
                "heavy_attack" => "attack_heavy",
                "thrust" => "attack_thrust",
                _ => "attack_normal"
            };
            
            if (result.Critical)
                baseAnim += "_critical";
            
            return baseAnim;
        }
        
        /// <summary>
        /// Get hit animation name
        /// </summary>
        private string GetHitAnimation(CombatResult result)
        {
            if (result.Blocked)
                return result.BlockBroken ? "block_broken" : "block_success";
            
            if (result.Miss)
                return "dodge_success";
            
            if (result.Knockback)
                return "hit_knockback";
            
            if (result.Dismemberment)
                return $"dismember_{result.SeveredLimb.ToLower()}";
            
            return $"hit_{result.HitLocation.ToLower()}";
        }
        
        /// <summary>
        /// Generate deterministic combat seed
        /// </summary>
        private int GenerateCombatSeed(CombatSession session, CombatantState attacker, CombatantState target)
        {
            unchecked
            {
                int hash = 17;
                hash = hash * 31 + session.Id.GetHashCode();
                hash = hash * 31 + session.ActionCount;
                hash = hash * 31 + attacker.Id.GetHashCode();
                hash = hash * 31 + target.Id.GetHashCode();
                hash = hash * 31 + (int)attacker.LastAttackTime;
                return hash;
            }
        }
        
        /// <summary>
        /// Get or create combat session
        /// </summary>
        private CombatSession GetOrCreateCombatSession(string attacker, string target)
        {
            var sessionId = GenerateSessionId(attacker, target);
            
            return activeCombats.GetOrAdd(sessionId, id => new CombatSession
            {
                Id = id,
                Participants = new List<string> { attacker, target },
                StartTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            });
        }
        
        /// <summary>
        /// Update combat session
        /// </summary>
        private void UpdateCombatSession(CombatSession session, CombatResult result)
        {
            session.ActionCount++;
            session.LastActionTime = result.Timestamp;
            
            // Check if combat should end
            if (ShouldEndCombat(session))
            {
                EndCombatSession(session);
            }
        }
        
        /// <summary>
        /// Check if combat should end
        /// </summary>
        private bool ShouldEndCombat(CombatSession session)
        {
            // Check if any participant is dead/unconscious
            foreach (var participantId in session.Participants)
            {
                if (combatants.TryGetValue(participantId, out var combatant))
                {
                    if (combatant.Health <= 0 || combatant.IsUnconscious)
                        return true;
                }
            }
            
            // Check for timeout (no action for 30 seconds)
            var timeSinceLastAction = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - session.LastActionTime;
            if (timeSinceLastAction > 30000)
                return true;
            
            return false;
        }
        
        /// <summary>
        /// End combat session
        /// </summary>
        private void EndCombatSession(CombatSession session)
        {
            activeCombats.TryRemove(session.Id, out _);
            
            // Reset combat states
            foreach (var participantId in session.Participants)
            {
                if (combatants.TryGetValue(participantId, out var combatant))
                {
                    combatant.InCombat = false;
                    combatant.IsBlocking = false;
                }
            }
            
            Logger.Log($"Combat session {session.Id} ended");
        }
        
        /// <summary>
        /// Get combatant state
        /// </summary>
        private CombatantState GetCombatantState(string combatantId)
        {
            return combatants.GetOrAdd(combatantId, id => new CombatantState
            {
                Id = id,
                Health = 100,
                Stamina = 100,
                LimbHealth = InitializeLimbHealth(),
                Skills = new Dictionary<string, float>(),
                Position = new Vector3()
            });
        }
        
        /// <summary>
        /// Initialize limb health
        /// </summary>
        private Dictionary<string, int> InitializeLimbHealth()
        {
            return new Dictionary<string, int>
            {
                { "Head", 100 },
                { "Chest", 100 },
                { "Stomach", 100 },
                { "LeftArm", 100 },
                { "RightArm", 100 },
                { "LeftLeg", 100 },
                { "RightLeg", 100 }
            };
        }
        
        /// <summary>
        /// Broadcast combat result
        /// </summary>
        private void BroadcastCombatResult(CombatResult result)
        {
            var message = new GameMessage
            {
                Type = "combat_result",
                Data = new Dictionary<string, object>
                {
                    { "result", result }
                }
            };
            
            // Send through network layer
            // server.BroadcastMessage(message);
        }
        
        // Helper methods
        
        private float GetWeaponReach(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return 1.5f; // Unarmed
            
            if (weaponId.Contains("polearm"))
                return 3.5f;
            if (weaponId.Contains("nodachi"))
                return 3.0f;
            if (weaponId.Contains("sword"))
                return 2.5f;
            if (weaponId.Contains("dagger"))
                return 1.8f;
            
            return 2.0f; // Default
        }
        
        private long GetAttackSpeed(CombatantState attacker, string weaponId)
        {
            float baseSpeed = 1000; // 1 second base
            
            // Weapon weight affects speed
            if (weaponId?.Contains("heavy") == true)
                baseSpeed *= 1.5f;
            else if (weaponId?.Contains("light") == true)
                baseSpeed *= 0.7f;
            
            // Dexterity reduces attack time
            baseSpeed *= (1.0f - attacker.Dexterity / 200.0f); // Max 50% reduction at 100 dex
            
            return (long)baseSpeed;
        }
        
        private float GetActionStaminaCost(string action)
        {
            return action switch
            {
                "attack" => 5.0f,
                "heavy_attack" => 10.0f,
                "block" => 3.0f,
                "dodge" => 8.0f,
                _ => 5.0f
            };
        }
        
        private string GetWeaponSkill(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return "Martial Arts";
            
            if (weaponId.Contains("katana") || weaponId.Contains("sword"))
                return "Katanas";
            if (weaponId.Contains("sabre"))
                return "Sabres";
            if (weaponId.Contains("heavy"))
                return "Heavy Weapons";
            if (weaponId.Contains("polearm"))
                return "Polearms";
            
            return "Melee Attack";
        }
        
        private int CalculateBlockDamage(CombatAction action)
        {
            int baseDamage = 5;
            
            if (action.Action == "heavy_attack")
                baseDamage *= 2;
            
            return baseDamage;
        }
        
        private bool IsValidActionType(string action)
        {
            var validActions = new[] { "attack", "heavy_attack", "thrust", "block", "dodge", "parry" };
            return validActions.Contains(action);
        }
        
        private bool IsValidWeapon(string weaponId)
        {
            // Check against weapon database
            return true; // Simplified
        }
        
        private string GenerateSessionId(string attacker, string target)
        {
            // Sort IDs to ensure consistent session ID regardless of who attacks first
            var sorted = new[] { attacker, target }.OrderBy(x => x).ToArray();
            return $"{sorted[0]}_{sorted[1]}";
        }
    }
    
    // Supporting classes
    
    public class CombatSession
    {
        public string Id { get; set; }
        public List<string> Participants { get; set; } = new List<string>();
        public long StartTime { get; set; }
        public long LastActionTime { get; set; }
        public int ActionCount { get; set; }
    }
    
    public class CombatantState
    {
        public string Id { get; set; }
        public int Health { get; set; }
        public float Stamina { get; set; }
        public Dictionary<string, int> LimbHealth { get; set; }
        public List<string> SeveredLimbs { get; set; } = new List<string>();
        public Dictionary<string, float> Skills { get; set; }
        public Vector3 Position { get; set; }
        public Vector3 Velocity { get; set; }
        
        // Combat stats
        public float Strength { get; set; }
        public float Dexterity { get; set; }
        public float Toughness { get; set; }
        public float Perception { get; set; }
        
        // Status
        public bool InCombat { get; set; }
        public bool IsBlocking { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsStunned { get; set; }
        public long StunDuration { get; set; }
        public long ConsciousnessTimer { get; set; }
        
        // Combat tracking
        public long LastAttackTime { get; set; }
        public int HitsLanded { get; set; }
        public int HitsTaken { get; set; }
        public int Misses { get; set; }
        public int CriticalHits { get; set; }
        public int BlocksSuccessful { get; set; }
        public int BlocksBroken { get; set; }
        
        // Effects
        public float BleedRate { get; set; }
        public float Encumbrance { get; set; }
    }
    
    public class CombatResolver
    {
        private CombatConfig config;
        
        public void Initialize(CombatConfig cfg)
        {
            config = cfg;
        }
    }
    
    public class DamageCalculator
    {
        private DamageSettings settings;
        
        public void Initialize(DamageSettings cfg)
        {
            settings = cfg;
        }
        
        public DamageInfo CalculateDamage(CombatantState attacker, CombatantState target, CombatAction action, string hitLocation)
        {
            var info = new DamageInfo();
            
            // Base weapon damage
            float baseDamage = GetWeaponDamage(action.WeaponId);
            
            // Strength modifier
            baseDamage *= (1.0f + attacker.Strength / 100.0f);
            
            // Skill modifier
            float skill = attacker.Skills.GetValueOrDefault("Melee Attack", 0);
            baseDamage *= (1.0f + skill / 200.0f);
            
            // Armor reduction
            float armor = GetArmorValue(target, hitLocation);
            float armorReduction = armor / (armor + 100.0f); // Diminishing returns
            baseDamage *= (1.0f - armorReduction);
            
            // Damage type
            info.Type = GetDamageType(action.WeaponId);
            
            // Location modifier
            baseDamage *= GetLocationModifier(hitLocation);
            
            info.TotalDamage = (int)baseDamage;
            
            return info;
        }
        
        private float GetWeaponDamage(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return 10; // Unarmed
            
            // Weapon damage values from Kenshi
            if (weaponId.Contains("meitou"))
                return 50;
            if (weaponId.Contains("edge"))
                return 40;
            if (weaponId.Contains("catun"))
                return 35;
            if (weaponId.Contains("heavy"))
                return 45;
            
            return 25; // Default
        }
        
        private string GetDamageType(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return "Blunt";
            
            if (weaponId.Contains("katana") || weaponId.Contains("sword"))
                return "Cut";
            if (weaponId.Contains("heavy"))
                return "Blunt";
            if (weaponId.Contains("crossbow"))
                return "Pierce";
            
            return "Cut";
        }
        
        private float GetArmorValue(CombatantState target, string location)
        {
            // Would check equipped armor
            return 20.0f; // Default armor value
        }
        
        private float GetLocationModifier(string location)
        {
            return location switch
            {
                "Head" => 1.5f,
                "Chest" => 1.0f,
                "Stomach" => 1.1f,
                "LeftArm" or "RightArm" => 0.8f,
                "LeftLeg" or "RightLeg" => 0.9f,
                _ => 1.0f
            };
        }
    }
    
    public class LimbSystemManager
    {
        private LimbSettings settings;
        
        public void Initialize(LimbSettings cfg)
        {
            settings = cfg;
        }
        
        public LimbDamageResult ApplyLimbDamage(CombatantState target, string limb, int damage, string damageType)
        {
            var result = new LimbDamageResult
            {
                Limb = limb,
                PreviousHealth = target.LimbHealth[limb]
            };
            
            // Apply damage
            target.LimbHealth[limb] -= damage;
            
            // Check for severing
            if (target.LimbHealth[limb] <= -100 && damageType == "Cut")
            {
                result.Severed = true;
                target.LimbHealth[limb] = -100;
            }
            
            result.RemainingHealth = Math.Max(-100, target.LimbHealth[limb]);
            
            return result;
        }
    }
    
    public class BlockingSystem
    {
        private BlockSettings settings;
        
        public void Initialize(BlockSettings cfg)
        {
            settings = cfg;
        }
        
        public bool CheckBlock(CombatantState attacker, CombatantState target, CombatAction action)
        {
            // Block chance based on skill
            float blockSkill = target.Skills.GetValueOrDefault("Block", 0);
            float blockChance = 0.5f + blockSkill / 200.0f;
            
            // Heavy attacks are harder to block
            if (action.Action == "heavy_attack")
                blockChance *= 0.5f;
            
            // Use deterministic random
            var seed = attacker.Id.GetHashCode() ^ target.Id.GetHashCode() ^ action.Timestamp.GetHashCode();
            var random = new Random(seed);
            
            return random.NextDouble() < blockChance;
        }
    }
    
    public class AnimationSynchronizer
    {
        private readonly Queue<AnimationData> animationQueue = new Queue<AnimationData>();
        private AnimationSettings settings;
        
        public void Initialize(AnimationSettings cfg)
        {
            settings = cfg;
        }
        
        public void QueueAnimation(AnimationData animation)
        {
            animationQueue.Enqueue(animation);
        }
        
        public List<AnimationData> GetPendingAnimations()
        {
            var animations = new List<AnimationData>();
            
            while (animationQueue.Count > 0)
            {
                animations.Add(animationQueue.Dequeue());
            }
            
            return animations;
        }
    }
    
    public class DamageInfo
    {
        public int TotalDamage { get; set; }
        public string Type { get; set; }
    }
    
    public class LimbDamageResult
    {
        public string Limb { get; set; }
        public int PreviousHealth { get; set; }
        public int RemainingHealth { get; set; }
        public bool Severed { get; set; }
    }
    
    public class AnimationData
    {
        public string EntityId { get; set; }
        public string AnimationName { get; set; }
        public float Speed { get; set; }
        public long StartTime { get; set; }
    }
    
    // Configuration classes
    
    public class CombatConfig
    {
        public int RandomSeed { get; set; } = 12345;
        public float CriticalMultiplier { get; set; } = 2.0f;
        public DamageSettings DamageSettings { get; set; } = new DamageSettings();
        public LimbSettings LimbSettings { get; set; } = new LimbSettings();
        public BlockSettings BlockSettings { get; set; } = new BlockSettings();
        public AnimationSettings AnimationSettings { get; set; } = new AnimationSettings();
    }
    
    public class DamageSettings
    {
        public float BaseMultiplier { get; set; } = 1.0f;
    }
    
    public class LimbSettings
    {
        public int MaxLimbHealth { get; set; } = 100;
        public int SeverThreshold { get; set; } = -100;
    }
    
    public class BlockSettings
    {
        public float BaseBlockChance { get; set; } = 0.5f;
    }
    
    public class AnimationSettings
    {
        public float BlendTime { get; set; } = 0.2f;
    }
}
using System;
using System.Collections.Generic;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
{
    public class CombatAction
    {
        // Basic properties from original implementation
        public string TargetId { get; set; }
        public string Action { get; set; } // e.g., "attack", "defend", "block", etc.

        // Enhanced properties
        public string WeaponId { get; set; }      // ID of the weapon being used
        public string AttackType { get; set; }    // Slash, Blunt, Cut, Pierce, etc.
        public string TargetLimb { get; set; }    // Head, Chest, LeftArm, etc.
        public float Power { get; set; } = 1.0f;  // Attack power multiplier (0.0-2.0)
        public float Damage { get; set; }         // Base damage value
        public float Range { get; set; } = 2.0f;  // Attack range in meters
        public bool IsCritical { get; set; }      // Whether this is a critical hit
        public long Timestamp { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        // Position data for attack validation
        public float AttackerPosX { get; set; }
        public float AttackerPosY { get; set; }
        public float AttackerPosZ { get; set; }

        // Status effects applied by this attack
        public List<StatusEffect> StatusEffects { get; set; } = new List<StatusEffect>();

        // Animation data
        public string AnimationName { get; set; }
        public float AnimationSpeed { get; set; } = 1.0f;

        // Default constructor
        public CombatAction()
        {
        }

        // Constructor with basic parameters
        public CombatAction(string targetId, string action)
        {
            TargetId = targetId;
            Action = action;
        }

        // Full constructor
        public CombatAction(string targetId, string action, string weaponId, string attackType, string targetLimb,
                         float power, bool isCritical, float attackerX, float attackerY, float attackerZ)
        {
            TargetId = targetId;
            Action = action;
            WeaponId = weaponId;
            AttackType = attackType;
            TargetLimb = targetLimb;
            Power = power;
            IsCritical = isCritical;
            AttackerPosX = attackerX;
            AttackerPosY = attackerY;
            AttackerPosZ = attackerZ;
        }

        // Add a status effect to this combat action
        public void AddStatusEffect(string effectType, float duration, float power)
        {
            StatusEffects.Add(new StatusEffect
            {
                Type = effectType,
                Duration = duration,
                Power = power
            });
        }

        // Validate if this combat action is possible
        // Returns true if valid, false if invalid
        public bool Validate(PlayerData attacker, PlayerData target)
        {
            // Check if players exist
            if (attacker == null || target == null)
                return false;

            // Check if target is in range
            float distance = CalculateDistance(
                attacker.CurrentPosition.X, attacker.CurrentPosition.Y, attacker.CurrentPosition.Z,
                target.CurrentPosition.X, target.CurrentPosition.Y, target.CurrentPosition.Z);

            // Get weapon reach
            float weaponReach = GetWeaponReach(WeaponId);

            // Check if target is in weapon range
            if (distance > weaponReach)
                return false;

            // Check if attacker can perform the action (not unconscious/dead)
            if (attacker.CurrentState == PlayerState.Unconscious ||
                attacker.CurrentState == PlayerState.Dead)
                return false;

            // TODO: Additional validation logic as needed

            return true;
        }

        // Calculates distance between two 3D points
        private float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2)
        {
            return (float)Math.Sqrt(
                Math.Pow(x2 - x1, 2) +
                Math.Pow(y2 - y1, 2) +
                Math.Pow(z2 - z1, 2));
        }

        // Weapon database - maps weapon types to their stats
        private static readonly Dictionary<string, WeaponStats> WeaponDatabase = new Dictionary<string, WeaponStats>(StringComparer.OrdinalIgnoreCase)
        {
            // Unarmed
            { "unarmed", new WeaponStats { Reach = 1.5f, BaseDamage = 10f, AttackSpeed = 1.2f, WeaponType = "Blunt" } },
            { "martial_arts", new WeaponStats { Reach = 1.5f, BaseDamage = 15f, AttackSpeed = 1.5f, WeaponType = "Blunt" } },

            // Daggers & Short blades
            { "dagger", new WeaponStats { Reach = 1.8f, BaseDamage = 20f, AttackSpeed = 1.4f, WeaponType = "Cut" } },
            { "knife", new WeaponStats { Reach = 1.6f, BaseDamage = 15f, AttackSpeed = 1.5f, WeaponType = "Cut" } },
            { "wakizashi", new WeaponStats { Reach = 2.0f, BaseDamage = 25f, AttackSpeed = 1.3f, WeaponType = "Cut" } },
            { "ninja_blade", new WeaponStats { Reach = 2.2f, BaseDamage = 30f, AttackSpeed = 1.2f, WeaponType = "Cut" } },

            // Standard Swords
            { "katana", new WeaponStats { Reach = 2.5f, BaseDamage = 40f, AttackSpeed = 1.0f, WeaponType = "Cut" } },
            { "sword", new WeaponStats { Reach = 2.4f, BaseDamage = 35f, AttackSpeed = 1.0f, WeaponType = "Cut" } },
            { "sabre", new WeaponStats { Reach = 2.6f, BaseDamage = 38f, AttackSpeed = 0.95f, WeaponType = "Cut" } },
            { "desert_sabre", new WeaponStats { Reach = 2.7f, BaseDamage = 42f, AttackSpeed = 0.9f, WeaponType = "Cut" } },
            { "foreign_sabre", new WeaponStats { Reach = 2.8f, BaseDamage = 45f, AttackSpeed = 0.85f, WeaponType = "Cut" } },
            { "longsword", new WeaponStats { Reach = 2.8f, BaseDamage = 40f, AttackSpeed = 0.9f, WeaponType = "Cut" } },

            // Heavy Weapons
            { "nodachi", new WeaponStats { Reach = 3.2f, BaseDamage = 55f, AttackSpeed = 0.7f, WeaponType = "Cut" } },
            { "falling_sun", new WeaponStats { Reach = 3.5f, BaseDamage = 65f, AttackSpeed = 0.6f, WeaponType = "Cut" } },
            { "fragment_axe", new WeaponStats { Reach = 3.0f, BaseDamage = 60f, AttackSpeed = 0.65f, WeaponType = "Cut" } },
            { "plank", new WeaponStats { Reach = 2.8f, BaseDamage = 45f, AttackSpeed = 0.75f, WeaponType = "Blunt" } },

            // Polearms
            { "polearm", new WeaponStats { Reach = 4.0f, BaseDamage = 50f, AttackSpeed = 0.8f, WeaponType = "Cut" } },
            { "naginata", new WeaponStats { Reach = 3.8f, BaseDamage = 48f, AttackSpeed = 0.85f, WeaponType = "Cut" } },
            { "staff", new WeaponStats { Reach = 3.5f, BaseDamage = 25f, AttackSpeed = 1.1f, WeaponType = "Blunt" } },
            { "halberd", new WeaponStats { Reach = 4.2f, BaseDamage = 55f, AttackSpeed = 0.7f, WeaponType = "Cut" } },

            // Hackers (Anti-armor)
            { "hacker", new WeaponStats { Reach = 2.5f, BaseDamage = 35f, AttackSpeed = 0.9f, WeaponType = "Cut", ArmorPenetration = 0.5f } },
            { "heavy_jitte", new WeaponStats { Reach = 2.3f, BaseDamage = 30f, AttackSpeed = 1.0f, WeaponType = "Blunt", ArmorPenetration = 0.4f } },

            // Blunt Weapons
            { "club", new WeaponStats { Reach = 2.0f, BaseDamage = 30f, AttackSpeed = 1.0f, WeaponType = "Blunt" } },
            { "iron_club", new WeaponStats { Reach = 2.2f, BaseDamage = 40f, AttackSpeed = 0.85f, WeaponType = "Blunt" } },
            { "jitte", new WeaponStats { Reach = 2.0f, BaseDamage = 25f, AttackSpeed = 1.1f, WeaponType = "Blunt" } },
            { "mace", new WeaponStats { Reach = 2.2f, BaseDamage = 38f, AttackSpeed = 0.9f, WeaponType = "Blunt" } },

            // Crossbows (ranged)
            { "crossbow", new WeaponStats { Reach = 50.0f, BaseDamage = 60f, AttackSpeed = 0.3f, WeaponType = "Pierce", IsRanged = true } },
            { "oldworld_bow_mkii", new WeaponStats { Reach = 60.0f, BaseDamage = 80f, AttackSpeed = 0.25f, WeaponType = "Pierce", IsRanged = true } },
            { "toothpick", new WeaponStats { Reach = 40.0f, BaseDamage = 45f, AttackSpeed = 0.5f, WeaponType = "Pierce", IsRanged = true } },
        };

        // Get the reach of a weapon based on its ID
        private float GetWeaponReach(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return 1.5f; // Unarmed reach

            // Check exact match first
            if (WeaponDatabase.TryGetValue(weaponId, out WeaponStats exactMatch))
                return exactMatch.Reach;

            // Check partial matches (weapon ID contains type name)
            string lowerWeaponId = weaponId.ToLower();
            foreach (var kvp in WeaponDatabase)
            {
                if (lowerWeaponId.Contains(kvp.Key.ToLower()))
                    return kvp.Value.Reach;
            }

            // Default weapon reach
            return 2.0f;
        }

        // Get full weapon stats
        public static WeaponStats GetWeaponStats(string weaponId)
        {
            if (string.IsNullOrEmpty(weaponId))
                return WeaponDatabase["unarmed"];

            // Check exact match first
            if (WeaponDatabase.TryGetValue(weaponId, out WeaponStats exactMatch))
                return exactMatch;

            // Check partial matches
            string lowerWeaponId = weaponId.ToLower();
            foreach (var kvp in WeaponDatabase)
            {
                if (lowerWeaponId.Contains(kvp.Key.ToLower()))
                    return kvp.Value;
            }

            // Return default sword stats
            return new WeaponStats { Reach = 2.0f, BaseDamage = 30f, AttackSpeed = 1.0f, WeaponType = "Cut" };
        }
    }

    // Weapon stats class
    public class WeaponStats
    {
        public float Reach { get; set; } = 2.0f;
        public float BaseDamage { get; set; } = 30f;
        public float AttackSpeed { get; set; } = 1.0f;
        public string WeaponType { get; set; } = "Cut"; // Cut, Blunt, Pierce
        public float ArmorPenetration { get; set; } = 0f;
        public bool IsRanged { get; set; } = false;
        public float BleedMultiplier { get; set; } = 1.0f;
        public float StunChance { get; set; } = 0f;
    }

    // Status effect class for combat
    public class StatusEffect
    {
        public string Type { get; set; }       // Bleed, Stun, Poison, etc.
        public float Duration { get; set; }    // In seconds
        public float Power { get; set; } = 1.0f; // Effect power multiplier
        public long AppliedAt { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        // Check if this effect is still active
        public bool IsActive()
        {
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            long endTime = AppliedAt + (long)(Duration * 1000);
            return now < endTime;
        }

        // Get remaining time in seconds
        public float GetRemainingTime()
        {
            if (!IsActive())
                return 0;

            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            long endTime = AppliedAt + (long)(Duration * 1000);
            return (endTime - now) / 1000.0f;
        }
    }

    // Combat result class - returned after combat resolution
    public class CombatResult
    {
        // Core properties
        public string AttackerId { get; set; }
        public string TargetId { get; set; }
        public long Timestamp { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        // Hit/Miss properties
        public bool Hit { get; set; }
        public bool Miss { get; set; }
        public bool Success { get; set; }
        public string Reason { get; set; }

        // Damage properties
        public int Damage { get; set; }
        public string DamageType { get; set; }
        public float BleedDamage { get; set; }
        public LimbDamageResult LimbDamage { get; set; }

        // Location properties
        public string AffectedLimb { get; set; }
        public string HitLocation { get; set; }

        // Block properties
        public bool Blocked { get; set; }
        public float BlockDamage { get; set; }
        public bool BlockBroken { get; set; }

        // Critical/Special properties
        public bool IsCritical { get; set; }
        public bool Critical { get; set; }
        public bool Dismemberment { get; set; }
        public string SeveredLimb { get; set; }

        // Knockback properties
        public bool Knockback { get; set; }
        public float KnockbackForce { get; set; }

        // Effects and messages
        public List<StatusEffect> AppliedEffects { get; set; } = new List<StatusEffect>();
        public string ResultMessage { get; set; }

        // Generate a descriptive message of what happened
        public string GetDetailedMessage(string attackerName, string targetName)
        {
            if (!Hit)
                return $"{attackerName}'s attack missed {targetName}.";

            string message = $"{attackerName} hit {targetName}";

            if (!string.IsNullOrEmpty(AffectedLimb))
                message += $" in the {AffectedLimb}";

            message += $" for {Damage} damage";

            if (IsCritical)
                message += " (CRITICAL)";

            if (AppliedEffects.Count > 0)
            {
                message += " causing ";
                for (int i = 0; i < AppliedEffects.Count; i++)
                {
                    if (i > 0)
                        message += i == AppliedEffects.Count - 1 ? " and " : ", ";

                    message += AppliedEffects[i].Type.ToLower();
                }
            }

            return message + ".";
        }
    }
}
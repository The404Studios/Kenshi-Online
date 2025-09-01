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

        // Get the reach of a weapon based on its ID
        // In a real implementation, this would query a weapon database
        private float GetWeaponReach(string weaponId)
        {
            // TODO: Implement proper weapon reach database
            // Default values for now
            if (string.IsNullOrEmpty(weaponId))
                return 1.5f; // Unarmed reach

            if (weaponId.Contains("dagger") || weaponId.Contains("knife"))
                return 1.8f;

            if (weaponId.Contains("sword") || weaponId.Contains("sabre"))
                return 2.5f;

            if (weaponId.Contains("nodachi") || weaponId.Contains("polearm"))
                return 3.5f;

            // Default weapon reach
            return 2.0f;
        }
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
        public bool Hit { get; set; }
        public int Damage { get; set; }
        public string DamageType { get; set; }
        public string AffectedLimb { get; set; }
        public bool IsCritical { get; set; }
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
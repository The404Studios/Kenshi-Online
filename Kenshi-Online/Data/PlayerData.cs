using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Data
{
    public class PlayerData
    {
        // Basic identification
        public string PlayerId { get; set; }
        public string DisplayName { get; set; }

        // Position data
        [JsonIgnore] // Don't serialize this in player data file
        public Position CurrentPosition { get; set; } = new Position();

        // Alias for compatibility with new code
        [JsonIgnore]
        public Position Position
        {
            get => CurrentPosition;
            set => CurrentPosition = value;
        }

        // For persistence only - we'll serialize these values
        public float PositionX { get; set; }
        public float PositionY { get; set; }
        public float PositionZ { get; set; }
        public float RotationZ { get; set; }

        // Character stats
        public float Health { get; set; } = 100f;
        public float MaxHealth { get; set; } = 100f;
        public int Hunger { get; set; } = 100;
        public int MaxHunger { get; set; } = 100;
        public int Thirst { get; set; } = 100;
        public int MaxThirst { get; set; } = 100;

        // Limb health - Kenshi specific
        public Dictionary<string, int> LimbHealth { get; set; } = new Dictionary<string, int>();

        // Inventory data
        public Dictionary<string, int> Inventory { get; set; } = new Dictionary<string, int>();
        public List<InventoryItem> DetailedInventory { get; set; } = new List<InventoryItem>();

        // Equipment
        public Dictionary<string, string> EquippedItems { get; set; } = new Dictionary<string, string>();

        // Character progression
        public Dictionary<string, float> Skills { get; set; } = new Dictionary<string, float>();
        public int Level { get; set; } = 1;
        public int Experience { get; set; } = 0;
        public int ExperienceToNextLevel { get; set; } = 1000;

        // Faction data
        public string FactionId { get; set; }
        public string FactionRank { get; set; }

        // Gameplay state
        public PlayerState CurrentState { get; set; } = PlayerState.Idle;
        public string CurrentAction { get; set; }
        public string TargetId { get; set; }

        // Last update time for synchronization
        public long LastUpdateTime { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        // Session data
        [JsonIgnore]
        public string SessionId { get; set; }

        // Constructor
        public PlayerData()
        {
            // Initialize default limb health
            InitializeLimbHealth();
        }

        private void InitializeLimbHealth()
        {
            // Kenshi's limbs system
            string[] limbs = new string[]
            {
                "Head", "Chest", "Stomach",
                "LeftArm", "RightArm", "LeftLeg", "RightLeg"
            };

            foreach (var limb in limbs)
            {
                LimbHealth[limb] = 100;
            }
        }

        // Save position to persistent values
        public void SavePosition()
        {
            PositionX = CurrentPosition.X;
            PositionY = CurrentPosition.Y;
            PositionZ = CurrentPosition.Z;
            RotationZ = CurrentPosition.RotationZ;
        }

        // Load position from persistent values
        public void LoadPosition()
        {
            CurrentPosition = new Position(PositionX, PositionY, PositionZ, 0, 0, RotationZ);
        }

        // Add experience and handle level ups
        public void AddExperience(int amount)
        {
            Experience += amount;

            // Check for level up
            while (Experience >= ExperienceToNextLevel)
            {
                LevelUp();
            }
        }

        private void LevelUp()
        {
            Level++;
            Experience -= ExperienceToNextLevel;

            // Increase max health slightly with each level
            MaxHealth += 5;
            Health = MaxHealth;

            // Calculate experience needed for next level - geometric progression
            ExperienceToNextLevel = (int)(1000 * Math.Pow(1.2, Level - 1));

            // Increase all skills slightly
            foreach (var skill in Skills.Keys.ToArray())
            {
                Skills[skill] += 1.0f;
                if (Skills[skill] > 100)
                    Skills[skill] = 100;
            }
        }

        // Add or remove an item from inventory
        public void UpdateInventory(string itemId, int quantityChange)
        {
            if (!Inventory.ContainsKey(itemId))
            {
                Inventory[itemId] = 0;
            }

            Inventory[itemId] += quantityChange;

            // Remove item if quantity is 0 or less
            if (Inventory[itemId] <= 0)
            {
                Inventory.Remove(itemId);
            }
        }

        // Check if player has a specific item
        public bool HasItem(string itemId, int quantity = 1)
        {
            return Inventory.ContainsKey(itemId) && Inventory[itemId] >= quantity;
        }

        // Equip an item
        public bool EquipItem(string itemId, string slot)
        {
            // Check if player has the item
            if (!HasItem(itemId))
                return false;

            // Unequip current item in this slot if any
            if (EquippedItems.ContainsKey(slot))
            {
                string currentItemId = EquippedItems[slot];
                UpdateInventory(currentItemId, 1); // Add current item back to inventory
            }

            // Equip new item
            EquippedItems[slot] = itemId;
            UpdateInventory(itemId, -1); // Remove from inventory

            return true;
        }

        // Unequip an item
        public bool UnequipItem(string slot)
        {
            if (!EquippedItems.ContainsKey(slot))
                return false;

            string itemId = EquippedItems[slot];
            UpdateInventory(itemId, 1); // Add back to inventory
            EquippedItems.Remove(slot);

            return true;
        }

        // Apply damage to a specific limb
        public void ApplyLimbDamage(string limb, int damage)
        {
            if (!LimbHealth.ContainsKey(limb))
                return;

            LimbHealth[limb] -= damage;
            if (LimbHealth[limb] < 0)
                LimbHealth[limb] = 0;

            // Apply health reduction based on limb damage
            double healthDamage = damage * 0.5;
        }
    }
}
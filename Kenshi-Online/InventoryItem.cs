using System;
using System.Collections.Generic;

namespace KenshiMultiplayer
{
    public class InventoryItem
    {
        // Basic properties from original implementation
        public string ItemName { get; set; }
        public int Quantity { get; set; }

        // Enhanced properties
        public string ItemId { get; set; } = Guid.NewGuid().ToString();
        public string ItemType { get; set; } // Weapon, Armor, Food, Resource, etc.
        public float Condition { get; set; } = 1.0f; // 0.0-1.0 representing condition
        public float Weight { get; set; } = 1.0f; // Weight in kg
        public int Value { get; set; } = 0; // Value in cats (currency)

        // Item quality affects stats
        public ItemQuality Quality { get; set; } = ItemQuality.Normal;

        // For stackable items, track individual conditions
        public List<float> StackConditions { get; set; } = new List<float>();

        // Equipment properties
        public bool IsEquippable { get; set; }
        public string EquipSlot { get; set; } // Head, Chest, Legs, etc.
        public bool IsEquipped { get; set; }

        // Weapon properties
        public float Damage { get; set; }
        public float AttackSpeed { get; set; } = 1.0f;
        public float Reach { get; set; }
        public string DamageType { get; set; } // Cutting, Blunt, etc.

        // Armor properties
        public float BluntProtection { get; set; }
        public float CutProtection { get; set; }
        public float PierceProtection { get; set; }

        // Food properties
        public int Nutrition { get; set; }
        public int Hydration { get; set; }
        public bool IsPerishable { get; set; }
        public float SpoilRate { get; set; } = 0.0f;
        public DateTime ExpiryDate { get; set; } = DateTime.MaxValue;

        // Crafting/building
        public bool IsCraftingMaterial { get; set; }
        public bool IsBuildingMaterial { get; set; }

        // Special effects when used/equipped
        public Dictionary<string, float> StatModifiers { get; set; } = new Dictionary<string, float>();

        // Default constructor
        public InventoryItem()
        {
        }

        // Basic constructor
        public InventoryItem(string name, int quantity)
        {
            ItemName = name;
            Quantity = quantity;
        }

        // Constructor with type
        public InventoryItem(string name, string type, int quantity)
        {
            ItemName = name;
            ItemType = type;
            Quantity = quantity;
        }

        // Split stack into a new item
        public InventoryItem SplitStack(int splitQuantity)
        {
            if (splitQuantity <= 0 || splitQuantity >= Quantity)
                return null;

            InventoryItem newItem = new InventoryItem
            {
                ItemName = this.ItemName,
                ItemType = this.ItemType,
                Quantity = splitQuantity,
                Condition = this.Condition,
                Weight = this.Weight,
                Value = this.Value,
                Quality = this.Quality,
                IsEquippable = this.IsEquippable,
                EquipSlot = this.EquipSlot,
                Damage = this.Damage,
                AttackSpeed = this.AttackSpeed,
                Reach = this.Reach,
                DamageType = this.DamageType,
                BluntProtection = this.BluntProtection,
                CutProtection = this.CutProtection,
                PierceProtection = this.PierceProtection,
                Nutrition = this.Nutrition,
                Hydration = this.Hydration,
                IsPerishable = this.IsPerishable,
                SpoilRate = this.SpoilRate,
                ExpiryDate = this.ExpiryDate,
                IsCraftingMaterial = this.IsCraftingMaterial,
                IsBuildingMaterial = this.IsBuildingMaterial
            };

            // Handle stack conditions
            if (StackConditions.Count > 0)
            {
                for (int i = 0; i < splitQuantity && i < StackConditions.Count; i++)
                {
                    newItem.StackConditions.Add(StackConditions[i]);
                }

                // Remove the transferred conditions from this stack
                StackConditions.RemoveRange(0, Math.Min(splitQuantity, StackConditions.Count));
            }

            // Copy stat modifiers
            foreach (var kvp in StatModifiers)
            {
                newItem.StatModifiers[kvp.Key] = kvp.Value;
            }

            // Decrease quantity of this stack
            Quantity -= splitQuantity;

            return newItem;
        }

        // Merge another item into this stack
        public bool MergeStack(InventoryItem other)
        {
            // Check if items can be merged
            if (other.ItemName != this.ItemName || other.ItemType != this.ItemType)
                return false;

            // Add quantity
            Quantity += other.Quantity;

            // Merge stack conditions
            if (other.StackConditions.Count > 0)
            {
                StackConditions.AddRange(other.StackConditions);
            }
            else if (other.Condition < 1.0f)
            {
                // If the other item has a specific condition but no stack conditions
                for (int i = 0; i < other.Quantity; i++)
                {
                    StackConditions.Add(other.Condition);
                }
            }

            // Recalculate average condition
            UpdateAverageCondition();

            return true;
        }

        // Update the average condition based on stack conditions
        private void UpdateAverageCondition()
        {
            if (StackConditions.Count == 0)
                return;

            float total = 0;
            foreach (float cond in StackConditions)
            {
                total += cond;
            }

            Condition = total / StackConditions.Count;
        }

        // Apply wear and tear to the item
        public void ApplyWear(float amount)
        {
            Condition -= amount;
            if (Condition < 0)
                Condition = 0;

            // Apply to all in stack if we're tracking individual conditions
            for (int i = 0; i < StackConditions.Count; i++)
            {
                StackConditions[i] -= amount;
                if (StackConditions[i] < 0)
                    StackConditions[i] = 0;
            }
        }

        // Repair the item
        public void Repair(float amount)
        {
            Condition = Math.Min(1.0f, Condition + amount);

            // Apply to all in stack if we're tracking individual conditions
            for (int i = 0; i < StackConditions.Count; i++)
            {
                StackConditions[i] = Math.Min(1.0f, StackConditions[i] + amount);
            }
        }

        // Check if item is completely broken
        public bool IsBroken()
        {
            return Condition <= 0;
        }

        // Get modified item stats based on condition and quality
        public float GetModifiedDamage()
        {
            float qualityMod = GetQualityModifier();
            float conditionMod = Condition;
            return Damage * qualityMod * conditionMod;
        }

        public float GetModifiedBluntProtection()
        {
            float qualityMod = GetQualityModifier();
            float conditionMod = Condition;
            return BluntProtection * qualityMod * conditionMod;
        }

        public float GetModifiedCutProtection()
        {
            float qualityMod = GetQualityModifier();
            float conditionMod = Condition;
            return CutProtection * qualityMod * conditionMod;
        }

        public float GetModifiedPierceProtection()
        {
            float qualityMod = GetQualityModifier();
            float conditionMod = Condition;
            return PierceProtection * qualityMod * conditionMod;
        }

        // Get quality modifier
        private float GetQualityModifier()
        {
            switch (Quality)
            {
                case ItemQuality.Poor: return 0.7f;
                case ItemQuality.Normal: return 1.0f;
                case ItemQuality.Good: return 1.2f;
                case ItemQuality.Excellent: return 1.5f;
                case ItemQuality.Masterwork: return 2.0f;
                default: return 1.0f;
            }
        }

        // Get total weight of the stack
        public float GetTotalWeight()
        {
            return Weight * Quantity;
        }

        // Get total value of the stack
        public int GetTotalValue()
        {
            // Value is affected by condition
            float conditionFactor = (Condition * 0.8f) + 0.2f; // Even at 0 condition, an item has 20% of its value
            return (int)(Value * Quantity * conditionFactor);
        }
    }

    // Item quality enum
    public enum ItemQuality
    {
        Poor,
        Normal,
        Good,
        Excellent,
        Masterwork
    }
}
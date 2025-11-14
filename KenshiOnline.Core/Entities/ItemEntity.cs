using System;
using System.Collections.Generic;

namespace KenshiOnline.Core.Entities
{
    /// <summary>
    /// Item entity for weapons, armor, resources, etc.
    /// </summary>
    public class ItemEntity : Entity
    {
        // Item info
        public string ItemName { get; set; }
        public string ItemType { get; set; } // Weapon, Armor, Resource, Food, etc.
        public string ItemSubType { get; set; } // Sword, Helmet, Iron, Bread, etc.
        public string ItemID { get; set; } // Game's internal item ID

        // Item state
        public float Durability { get; set; }
        public float MaxDurability { get; set; }
        public int Quantity { get; set; }
        public int StackSize { get; set; }
        public float Weight { get; set; }
        public int Value { get; set; } // In cats (Kenshi currency)

        // Container info
        public Guid? ContainerId { get; set; } // Building, chest, etc.
        public Guid? OwnerId { get; set; } // Player or NPC who owns this
        public bool IsEquipped { get; set; }
        public string EquipSlot { get; set; } // Head, Chest, Weapon, etc.

        // World state
        public bool IsOnGround { get; set; }
        public bool IsInContainer { get; set; }
        public bool IsInInventory { get; set; }

        // Weapon specific
        public float WeaponDamage { get; set; }
        public float WeaponSpeed { get; set; }
        public string WeaponClass { get; set; } // Katana, Heavy Weapon, etc.
        public float WeaponReach { get; set; }

        // Armor specific
        public float ArmorValue { get; set; }
        public Dictionary<string, float> ArmorCoverage { get; set; } // Body part -> coverage %

        // Crafting
        public bool IsCraftable { get; set; }
        public Dictionary<string, int> CraftingRecipe { get; set; } // Material -> Quantity
        public int CraftingSkillRequired { get; set; }

        // Food specific
        public float NutritionValue { get; set; }
        public bool IsSpoiled { get; set; }
        public float SpoilTimer { get; set; }

        // Research/blueprint
        public bool IsResearchItem { get; set; }
        public string UnlocksRecipe { get; set; }

        public ItemEntity()
        {
            Type = EntityType.Item;
            ArmorCoverage = new Dictionary<string, float>();
            CraftingRecipe = new Dictionary<string, int>();
            Quantity = 1;
            StackSize = 1;
            Durability = 100f;
            MaxDurability = 100f;
            Priority = 2; // Low priority for items
            SyncRadius = 50f; // Items sync in smaller radius
        }

        public override Dictionary<string, object> Serialize()
        {
            var data = base.Serialize();

            // Item info
            data["itemName"] = ItemName ?? "";
            data["itemType"] = ItemType ?? "";
            data["itemSubType"] = ItemSubType ?? "";
            data["itemID"] = ItemID ?? "";

            // Item state
            data["durability"] = Durability;
            data["maxDurability"] = MaxDurability;
            data["quantity"] = Quantity;
            data["stackSize"] = StackSize;
            data["weight"] = Weight;
            data["value"] = Value;

            // Container info
            data["containerId"] = ContainerId?.ToString() ?? "";
            data["ownerId"] = OwnerId?.ToString() ?? "";
            data["isEquipped"] = IsEquipped;
            data["equipSlot"] = EquipSlot ?? "";

            // World state
            data["isOnGround"] = IsOnGround;
            data["isInContainer"] = IsInContainer;
            data["isInInventory"] = IsInInventory;

            // Weapon specific
            data["weaponDamage"] = WeaponDamage;
            data["weaponSpeed"] = WeaponSpeed;
            data["weaponClass"] = WeaponClass ?? "";
            data["weaponReach"] = WeaponReach;

            // Armor specific
            data["armorValue"] = ArmorValue;
            data["armorCoverage"] = ArmorCoverage;

            // Food specific
            data["nutritionValue"] = NutritionValue;
            data["isSpoiled"] = IsSpoiled;
            data["spoilTimer"] = SpoilTimer;

            // Research
            data["isResearchItem"] = IsResearchItem;
            data["unlocksRecipe"] = UnlocksRecipe ?? "";

            return data;
        }

        public override void Deserialize(Dictionary<string, object> data)
        {
            base.Deserialize(data);

            // Item info
            if (data.TryGetValue("itemName", out var itemName))
                ItemName = itemName.ToString();
            if (data.TryGetValue("itemType", out var itemType))
                ItemType = itemType.ToString();
            if (data.TryGetValue("itemSubType", out var itemSubType))
                ItemSubType = itemSubType.ToString();
            if (data.TryGetValue("itemID", out var itemID))
                ItemID = itemID.ToString();

            // Item state
            if (data.TryGetValue("durability", out var durability))
                Durability = Convert.ToSingle(durability);
            if (data.TryGetValue("maxDurability", out var maxDurability))
                MaxDurability = Convert.ToSingle(maxDurability);
            if (data.TryGetValue("quantity", out var quantity))
                Quantity = Convert.ToInt32(quantity);
            if (data.TryGetValue("stackSize", out var stackSize))
                StackSize = Convert.ToInt32(stackSize);
            if (data.TryGetValue("weight", out var weight))
                Weight = Convert.ToSingle(weight);
            if (data.TryGetValue("value", out var value))
                Value = Convert.ToInt32(value);

            // Container info
            if (data.TryGetValue("containerId", out var containerId) && !string.IsNullOrEmpty(containerId.ToString()))
                ContainerId = Guid.Parse(containerId.ToString());
            if (data.TryGetValue("ownerId", out var ownerId) && !string.IsNullOrEmpty(ownerId.ToString()))
                OwnerId = Guid.Parse(ownerId.ToString());
            if (data.TryGetValue("isEquipped", out var isEquipped))
                IsEquipped = Convert.ToBoolean(isEquipped);
            if (data.TryGetValue("equipSlot", out var equipSlot))
                EquipSlot = equipSlot.ToString();

            // World state
            if (data.TryGetValue("isOnGround", out var isOnGround))
                IsOnGround = Convert.ToBoolean(isOnGround);
            if (data.TryGetValue("isInContainer", out var isInContainer))
                IsInContainer = Convert.ToBoolean(isInContainer);
            if (data.TryGetValue("isInInventory", out var isInInventory))
                IsInInventory = Convert.ToBoolean(isInInventory);

            // Weapon specific
            if (data.TryGetValue("weaponDamage", out var weaponDamage))
                WeaponDamage = Convert.ToSingle(weaponDamage);
            if (data.TryGetValue("weaponSpeed", out var weaponSpeed))
                WeaponSpeed = Convert.ToSingle(weaponSpeed);
            if (data.TryGetValue("weaponClass", out var weaponClass))
                WeaponClass = weaponClass.ToString();
            if (data.TryGetValue("weaponReach", out var weaponReach))
                WeaponReach = Convert.ToSingle(weaponReach);

            // Armor specific
            if (data.TryGetValue("armorValue", out var armorValue))
                ArmorValue = Convert.ToSingle(armorValue);
            if (data.TryGetValue("armorCoverage", out var armorCoverage) && armorCoverage is Dictionary<string, object> armorDict)
            {
                ArmorCoverage.Clear();
                foreach (var kvp in armorDict)
                {
                    ArmorCoverage[kvp.Key] = Convert.ToSingle(kvp.Value);
                }
            }

            // Food specific
            if (data.TryGetValue("nutritionValue", out var nutritionValue))
                NutritionValue = Convert.ToSingle(nutritionValue);
            if (data.TryGetValue("isSpoiled", out var isSpoiled))
                IsSpoiled = Convert.ToBoolean(isSpoiled);
            if (data.TryGetValue("spoilTimer", out var spoilTimer))
                SpoilTimer = Convert.ToSingle(spoilTimer);

            // Research
            if (data.TryGetValue("isResearchItem", out var isResearchItem))
                IsResearchItem = Convert.ToBoolean(isResearchItem);
            if (data.TryGetValue("unlocksRecipe", out var unlocksRecipe))
                UnlocksRecipe = unlocksRecipe.ToString();
        }

        /// <summary>
        /// Get durability percentage
        /// </summary>
        public float GetDurabilityPercent()
        {
            return MaxDurability > 0 ? (Durability / MaxDurability) * 100f : 0f;
        }

        /// <summary>
        /// Apply wear to item
        /// </summary>
        public void ApplyWear(float amount)
        {
            Durability = Math.Max(0, Durability - amount);
            MarkDirty();
        }

        /// <summary>
        /// Repair item
        /// </summary>
        public void Repair(float amount)
        {
            Durability = Math.Min(MaxDurability, Durability + amount);
            MarkDirty();
        }

        /// <summary>
        /// Check if item is broken
        /// </summary>
        public bool IsBroken()
        {
            return Durability <= 0;
        }

        /// <summary>
        /// Check if item is stackable
        /// </summary>
        public bool IsStackable()
        {
            return StackSize > 1;
        }

        /// <summary>
        /// Add to stack
        /// </summary>
        public bool AddToStack(int amount)
        {
            if (!IsStackable())
                return false;

            if (Quantity + amount <= StackSize)
            {
                Quantity += amount;
                MarkDirty();
                return true;
            }

            return false;
        }

        /// <summary>
        /// Remove from stack
        /// </summary>
        public bool RemoveFromStack(int amount)
        {
            if (Quantity >= amount)
            {
                Quantity -= amount;
                MarkDirty();
                return true;
            }

            return false;
        }

        /// <summary>
        /// Update spoil timer (for food)
        /// </summary>
        public void UpdateSpoilage(float deltaTime)
        {
            if (ItemType == "Food" && !IsSpoiled)
            {
                SpoilTimer += deltaTime;
                if (SpoilTimer >= 3600f) // 1 hour = spoiled
                {
                    IsSpoiled = true;
                    NutritionValue *= 0.1f; // Reduce nutrition
                    Value = 0; // Worthless
                    MarkDirty();
                }
            }
        }
    }
}

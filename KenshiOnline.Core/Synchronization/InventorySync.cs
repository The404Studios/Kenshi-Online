using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using KenshiOnline.Core.Entities;

namespace KenshiOnline.Core.Synchronization
{
    /// <summary>
    /// Inventory action types
    /// </summary>
    public enum InventoryActionType
    {
        PickupItem,
        DropItem,
        EquipItem,
        UnequipItem,
        TransferItem,
        UseItem,
        SplitStack,
        MergeStack,
        ContainerOpen,
        ContainerClose
    }

    /// <summary>
    /// Inventory action data
    /// </summary>
    public class InventoryAction
    {
        public Guid ActionId { get; set; }
        public InventoryActionType Type { get; set; }
        public Guid EntityId { get; set; } // Player/NPC performing action
        public Guid ItemId { get; set; }
        public Guid? TargetId { get; set; } // For transfers/containers
        public string EquipSlot { get; set; }
        public int Quantity { get; set; }
        public float Timestamp { get; set; }
        public Dictionary<string, object> ExtraData { get; set; }

        public InventoryAction()
        {
            ActionId = Guid.NewGuid();
            ExtraData = new Dictionary<string, object>();
            Timestamp = GetCurrentTime();
            Quantity = 1;
        }

        private static float GetCurrentTime()
        {
            return (float)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["actionId"] = ActionId.ToString(),
                ["type"] = Type.ToString(),
                ["entityId"] = EntityId.ToString(),
                ["itemId"] = ItemId.ToString(),
                ["targetId"] = TargetId?.ToString() ?? "",
                ["equipSlot"] = EquipSlot ?? "",
                ["quantity"] = Quantity,
                ["timestamp"] = Timestamp,
                ["extraData"] = ExtraData
            };
        }

        public static InventoryAction Deserialize(Dictionary<string, object> data)
        {
            var action = new InventoryAction();

            if (data.TryGetValue("actionId", out var actionId))
                action.ActionId = Guid.Parse(actionId.ToString());
            if (data.TryGetValue("type", out var type))
                action.Type = Enum.Parse<InventoryActionType>(type.ToString());
            if (data.TryGetValue("entityId", out var entityId))
                action.EntityId = Guid.Parse(entityId.ToString());
            if (data.TryGetValue("itemId", out var itemId))
                action.ItemId = Guid.Parse(itemId.ToString());
            if (data.TryGetValue("targetId", out var targetId) && !string.IsNullOrEmpty(targetId.ToString()))
                action.TargetId = Guid.Parse(targetId.ToString());
            if (data.TryGetValue("equipSlot", out var equipSlot))
                action.EquipSlot = equipSlot.ToString();
            if (data.TryGetValue("quantity", out var quantity))
                action.Quantity = Convert.ToInt32(quantity);
            if (data.TryGetValue("timestamp", out var timestamp))
                action.Timestamp = Convert.ToSingle(timestamp);
            if (data.TryGetValue("extraData", out var extraData) && extraData is Dictionary<string, object> extra)
                action.ExtraData = extra;

            return action;
        }
    }

    /// <summary>
    /// Handles inventory synchronization across clients
    /// Server-authoritative inventory system
    /// </summary>
    public class InventorySync
    {
        private readonly EntityManager _entityManager;
        private readonly ConcurrentQueue<InventoryAction> _pendingActions;
        private readonly ConcurrentDictionary<Guid, InventoryAction> _recentActions;
        private readonly object _lock = new object();

        // Inventory settings
        public bool ServerAuthoritative { get; set; } = true;
        public float ActionRetentionTime { get; set; } = 5.0f;
        public float PickupRange { get; set; } = 3.0f; // 3 meters

        // Statistics
        public int TotalInventoryActions { get; private set; }

        public InventorySync(EntityManager entityManager)
        {
            _entityManager = entityManager;
            _pendingActions = new ConcurrentQueue<InventoryAction>();
            _recentActions = new ConcurrentDictionary<Guid, InventoryAction>();
        }

        #region Inventory Actions

        /// <summary>
        /// Process item pickup
        /// </summary>
        public InventoryAction ProcessPickup(Guid entityId, Guid itemId)
        {
            var entity = _entityManager.GetEntity(entityId);
            var item = _entityManager.GetEntity<ItemEntity>(itemId);

            if (entity == null || item == null)
                return null;

            // Validate pickup
            if (!CanPickup(entity, item))
                return null;

            // Add to inventory
            bool success = false;
            if (entity is PlayerEntity player)
            {
                if (player.Inventory.Count < player.InventoryCapacity)
                {
                    player.Inventory.Add(itemId);
                    player.MarkDirty();
                    success = true;
                }
            }
            else if (entity is NPCEntity npc)
            {
                if (npc.Inventory.Count < npc.InventoryCapacity)
                {
                    npc.Inventory.Add(itemId);
                    npc.MarkDirty();
                    success = true;
                }
            }

            if (success)
            {
                // Update item state
                item.IsOnGround = false;
                item.IsInInventory = true;
                item.OwnerId = entityId;
                item.MarkDirty();

                var action = new InventoryAction
                {
                    Type = InventoryActionType.PickupItem,
                    EntityId = entityId,
                    ItemId = itemId
                };

                _pendingActions.Enqueue(action);
                _recentActions[action.ActionId] = action;
                TotalInventoryActions++;

                return action;
            }

            return null;
        }

        /// <summary>
        /// Process item drop
        /// </summary>
        public InventoryAction ProcessDrop(Guid entityId, Guid itemId)
        {
            var entity = _entityManager.GetEntity(entityId);
            var item = _entityManager.GetEntity<ItemEntity>(itemId);

            if (entity == null || item == null)
                return null;

            // Remove from inventory
            bool success = false;
            if (entity is PlayerEntity player)
            {
                if (player.Inventory.Remove(itemId))
                {
                    player.MarkDirty();
                    success = true;
                }
            }
            else if (entity is NPCEntity npc)
            {
                if (npc.Inventory.Remove(itemId))
                {
                    npc.MarkDirty();
                    success = true;
                }
            }

            if (success)
            {
                // Update item state
                item.IsOnGround = true;
                item.IsInInventory = false;
                item.IsEquipped = false;
                item.OwnerId = null;
                item.Position = entity.Position; // Drop at entity position
                item.MarkDirty();

                var action = new InventoryAction
                {
                    Type = InventoryActionType.DropItem,
                    EntityId = entityId,
                    ItemId = itemId
                };

                _pendingActions.Enqueue(action);
                _recentActions[action.ActionId] = action;
                TotalInventoryActions++;

                return action;
            }

            return null;
        }

        /// <summary>
        /// Process item equip
        /// </summary>
        public InventoryAction ProcessEquip(Guid entityId, Guid itemId, string slot)
        {
            var entity = _entityManager.GetEntity(entityId);
            var item = _entityManager.GetEntity<ItemEntity>(itemId);

            if (entity == null || item == null)
                return null;

            // Validate equip
            if (!CanEquip(entity, item, slot))
                return null;

            // Equip item
            bool success = false;
            Guid? unequippedItemId = null;

            if (entity is PlayerEntity player)
            {
                // Unequip current item in slot
                if (player.Equipment.TryGetValue(slot, out var currentItemId))
                {
                    var currentItem = _entityManager.GetEntity<ItemEntity>(currentItemId);
                    if (currentItem != null)
                    {
                        currentItem.IsEquipped = false;
                        currentItem.EquipSlot = null;
                        currentItem.MarkDirty();
                        unequippedItemId = currentItemId;
                    }
                }

                player.Equipment[slot] = itemId;
                player.MarkDirty();
                success = true;
            }
            else if (entity is NPCEntity npc)
            {
                // Unequip current item in slot
                if (npc.Equipment.TryGetValue(slot, out var currentItemId))
                {
                    var currentItem = _entityManager.GetEntity<ItemEntity>(currentItemId);
                    if (currentItem != null)
                    {
                        currentItem.IsEquipped = false;
                        currentItem.EquipSlot = null;
                        currentItem.MarkDirty();
                        unequippedItemId = currentItemId;
                    }
                }

                npc.Equipment[slot] = itemId;
                npc.MarkDirty();
                success = true;
            }

            if (success)
            {
                // Update item state
                item.IsEquipped = true;
                item.EquipSlot = slot;
                item.MarkDirty();

                var action = new InventoryAction
                {
                    Type = InventoryActionType.EquipItem,
                    EntityId = entityId,
                    ItemId = itemId,
                    EquipSlot = slot
                };

                if (unequippedItemId.HasValue)
                {
                    action.ExtraData["unequippedItemId"] = unequippedItemId.Value.ToString();
                }

                _pendingActions.Enqueue(action);
                _recentActions[action.ActionId] = action;
                TotalInventoryActions++;

                return action;
            }

            return null;
        }

        /// <summary>
        /// Process item unequip
        /// </summary>
        public InventoryAction ProcessUnequip(Guid entityId, string slot)
        {
            var entity = _entityManager.GetEntity(entityId);
            if (entity == null)
                return null;

            Guid? itemId = null;

            if (entity is PlayerEntity player)
            {
                if (player.Equipment.TryGetValue(slot, out var equippedItemId))
                {
                    itemId = equippedItemId;
                    player.Equipment.Remove(slot);
                    player.MarkDirty();
                }
            }
            else if (entity is NPCEntity npc)
            {
                if (npc.Equipment.TryGetValue(slot, out var equippedItemId))
                {
                    itemId = equippedItemId;
                    npc.Equipment.Remove(slot);
                    npc.MarkDirty();
                }
            }

            if (itemId.HasValue)
            {
                var item = _entityManager.GetEntity<ItemEntity>(itemId.Value);
                if (item != null)
                {
                    item.IsEquipped = false;
                    item.EquipSlot = null;
                    item.MarkDirty();
                }

                var action = new InventoryAction
                {
                    Type = InventoryActionType.UnequipItem,
                    EntityId = entityId,
                    ItemId = itemId.Value,
                    EquipSlot = slot
                };

                _pendingActions.Enqueue(action);
                _recentActions[action.ActionId] = action;
                TotalInventoryActions++;

                return action;
            }

            return null;
        }

        /// <summary>
        /// Process item transfer between entities
        /// </summary>
        public InventoryAction ProcessTransfer(Guid sourceId, Guid targetId, Guid itemId, int quantity = 1)
        {
            var source = _entityManager.GetEntity(sourceId);
            var target = _entityManager.GetEntity(targetId);
            var item = _entityManager.GetEntity<ItemEntity>(itemId);

            if (source == null || target == null || item == null)
                return null;

            // Validate transfer
            if (!CanTransfer(source, target, item))
                return null;

            // Remove from source
            bool sourceSuccess = false;
            if (source is PlayerEntity sourcePlayer)
            {
                if (sourcePlayer.Inventory.Remove(itemId))
                {
                    sourcePlayer.MarkDirty();
                    sourceSuccess = true;
                }
            }
            else if (source is NPCEntity sourceNPC)
            {
                if (sourceNPC.Inventory.Remove(itemId))
                {
                    sourceNPC.MarkDirty();
                    sourceSuccess = true;
                }
            }

            if (!sourceSuccess)
                return null;

            // Add to target
            bool targetSuccess = false;
            if (target is PlayerEntity targetPlayer)
            {
                if (targetPlayer.Inventory.Count < targetPlayer.InventoryCapacity)
                {
                    targetPlayer.Inventory.Add(itemId);
                    targetPlayer.MarkDirty();
                    targetSuccess = true;
                }
            }
            else if (target is NPCEntity targetNPC)
            {
                if (targetNPC.Inventory.Count < targetNPC.InventoryCapacity)
                {
                    targetNPC.Inventory.Add(itemId);
                    targetNPC.MarkDirty();
                    targetSuccess = true;
                }
            }

            if (targetSuccess)
            {
                // Update item owner
                item.OwnerId = targetId;
                item.MarkDirty();

                var action = new InventoryAction
                {
                    Type = InventoryActionType.TransferItem,
                    EntityId = sourceId,
                    TargetId = targetId,
                    ItemId = itemId,
                    Quantity = quantity
                };

                _pendingActions.Enqueue(action);
                _recentActions[action.ActionId] = action;
                TotalInventoryActions++;

                return action;
            }
            else
            {
                // Transfer failed, add back to source
                if (source is PlayerEntity sourcePlayer)
                {
                    sourcePlayer.Inventory.Add(itemId);
                    sourcePlayer.MarkDirty();
                }
                else if (source is NPCEntity sourceNPC)
                {
                    sourceNPC.Inventory.Add(itemId);
                    sourceNPC.MarkDirty();
                }
            }

            return null;
        }

        #endregion

        #region Validation

        /// <summary>
        /// Check if entity can pickup item
        /// </summary>
        private bool CanPickup(Entity entity, ItemEntity item)
        {
            // Check if item is on ground
            if (!item.IsOnGround)
                return false;

            // Check range
            var distance = Vector3.Distance(entity.Position, item.Position);
            if (distance > PickupRange)
                return false;

            // Check if entity can act
            if (entity is PlayerEntity player && !player.CanAct())
                return false;
            if (entity is NPCEntity npc && !npc.CanAct())
                return false;

            return true;
        }

        /// <summary>
        /// Check if entity can equip item
        /// </summary>
        private bool CanEquip(Entity entity, ItemEntity item, string slot)
        {
            // Check if item is in inventory
            bool inInventory = false;
            if (entity is PlayerEntity player)
                inInventory = player.Inventory.Contains(item.Id);
            else if (entity is NPCEntity npc)
                inInventory = npc.Inventory.Contains(item.Id);

            if (!inInventory)
                return false;

            // Check if item can be equipped in slot (simplified)
            // In real game, this would check item type vs slot compatibility
            return true;
        }

        /// <summary>
        /// Check if transfer is allowed
        /// </summary>
        private bool CanTransfer(Entity source, Entity target, ItemEntity item)
        {
            // Check range
            var distance = Vector3.Distance(source.Position, target.Position);
            if (distance > PickupRange * 2) // Double range for transfers
                return false;

            // Check if entities can act
            if (source is PlayerEntity sourcePlayer && !sourcePlayer.CanAct())
                return false;
            if (source is NPCEntity sourceNPC && !sourceNPC.CanAct())
                return false;

            return true;
        }

        #endregion

        #region Event Management

        /// <summary>
        /// Get pending inventory actions (for broadcasting to clients)
        /// </summary>
        public IEnumerable<InventoryAction> GetPendingActions()
        {
            var actions = new List<InventoryAction>();
            while (_pendingActions.TryDequeue(out var action))
            {
                actions.Add(action);
            }
            return actions;
        }

        /// <summary>
        /// Clean up old actions
        /// </summary>
        public void Update(float deltaTime)
        {
            var currentTime = (float)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
            var toRemove = new List<Guid>();

            foreach (var kvp in _recentActions)
            {
                if (currentTime - kvp.Value.Timestamp > ActionRetentionTime)
                {
                    toRemove.Add(kvp.Key);
                }
            }

            foreach (var id in toRemove)
            {
                _recentActions.TryRemove(id, out _);
            }
        }

        #endregion
    }
}

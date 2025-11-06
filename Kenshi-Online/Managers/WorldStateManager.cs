using System;
using System.Collections.Generic;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages the state of the game world
    /// </summary>
    public class WorldStateManager
    {
        private readonly Dictionary<string, PlayerData> players;
        public Dictionary<string, EntityState> Entities { get; private set; }

        public WorldStateManager()
        {
            players = new Dictionary<string, PlayerData>();
            Entities = new Dictionary<string, EntityState>();
        }

        /// <summary>
        /// Check if a player exists in the world state
        /// </summary>
        public bool PlayerExists(string playerId)
        {
            return players.ContainsKey(playerId);
        }

        /// <summary>
        /// Get a player by ID
        /// </summary>
        public PlayerData? GetPlayer(string playerId)
        {
            if (players.TryGetValue(playerId, out var player))
            {
                return player;
            }
            return null;
        }

        /// <summary>
        /// Add or update a player in the world state
        /// </summary>
        public void AddOrUpdatePlayer(string playerId, PlayerData playerData)
        {
            players[playerId] = playerData;

            // Also update entity state
            UpdateEntityFromPlayer(playerId, playerData);
        }

        /// <summary>
        /// Apply an action result to the world state
        /// </summary>
        public void ApplyActionResult(ActionResult result)
        {
            if (result == null || result.Action == null || !result.Success)
            {
                Console.WriteLine($"Skipping invalid or failed action result");
                return;
            }

            string playerId = result.Action.PlayerId;
            if (string.IsNullOrEmpty(playerId))
            {
                Console.WriteLine("Action result has no player ID");
                return;
            }

            // Get or create player data
            if (!players.TryGetValue(playerId, out PlayerData? player))
            {
                Console.WriteLine($"Player {playerId} not found in world state, cannot apply result");
                return;
            }

            // Apply changes from the result
            if (result.Changes != null && result.Changes.Count > 0)
            {
                ApplyChanges(player, result.Changes);
            }

            // Update timestamp
            player.LastUpdateTime = result.Timestamp;

            // Update entity state to reflect changes
            UpdateEntityFromPlayer(playerId, player);

            Console.WriteLine($"Applied action result for player {playerId}: {result.Action.Type}");
        }

        /// <summary>
        /// Apply specific changes to player data
        /// </summary>
        private void ApplyChanges(PlayerData player, Dictionary<string, object> changes)
        {
            foreach (var change in changes)
            {
                try
                {
                    switch (change.Key.ToLower())
                    {
                        case "position":
                            if (change.Value is System.Numerics.Vector3 pos)
                            {
                                player.CurrentPosition = new Position(pos.X, pos.Y, pos.Z, 0, 0, player.CurrentPosition.RotationZ);
                            }
                            else if (change.Value is Position position)
                            {
                                player.CurrentPosition = position;
                            }
                            break;

                        case "health":
                            if (change.Value is float health)
                            {
                                player.Health = Math.Max(0, Math.Min(health, player.MaxHealth));
                            }
                            else if (change.Value is int healthInt)
                            {
                                player.Health = Math.Max(0, Math.Min(healthInt, player.MaxHealth));
                            }
                            break;

                        case "healthdelta":
                            if (change.Value is float delta)
                            {
                                player.Health = Math.Max(0, Math.Min(player.Health + delta, player.MaxHealth));
                            }
                            else if (change.Value is int deltaInt)
                            {
                                player.Health = Math.Max(0, Math.Min(player.Health + deltaInt, player.MaxHealth));
                            }
                            break;

                        case "limbdamage":
                            if (change.Value is Dictionary<string, object> limbData)
                            {
                                if (limbData.TryGetValue("limb", out object limbObj) &&
                                    limbData.TryGetValue("damage", out object damageObj))
                                {
                                    string limb = limbObj?.ToString() ?? "";
                                    int damage = Convert.ToInt32(damageObj);
                                    player.ApplyLimbDamage(limb, damage);
                                }
                            }
                            break;

                        case "inventory":
                            if (change.Value is Dictionary<string, object> inventoryChanges)
                            {
                                foreach (var item in inventoryChanges)
                                {
                                    int quantity = Convert.ToInt32(item.Value);
                                    player.UpdateInventory(item.Key, quantity);
                                }
                            }
                            break;

                        case "additem":
                            if (change.Value is Dictionary<string, object> itemData)
                            {
                                if (itemData.TryGetValue("id", out object idObj) &&
                                    itemData.TryGetValue("quantity", out object qtyObj))
                                {
                                    string itemId = idObj?.ToString() ?? "";
                                    int quantity = Convert.ToInt32(qtyObj);
                                    player.UpdateInventory(itemId, quantity);
                                }
                            }
                            break;

                        case "removeitem":
                            if (change.Value is Dictionary<string, object> removeData)
                            {
                                if (removeData.TryGetValue("id", out object idObj) &&
                                    removeData.TryGetValue("quantity", out object qtyObj))
                                {
                                    string itemId = idObj?.ToString() ?? "";
                                    int quantity = Convert.ToInt32(qtyObj);
                                    player.UpdateInventory(itemId, -quantity);
                                }
                            }
                            break;

                        case "experience":
                            if (change.Value is int exp)
                            {
                                player.AddExperience(exp);
                            }
                            break;

                        case "skill":
                            if (change.Value is Dictionary<string, object> skillData)
                            {
                                if (skillData.TryGetValue("name", out object nameObj) &&
                                    skillData.TryGetValue("value", out object valueObj))
                                {
                                    string skillName = nameObj?.ToString() ?? "";
                                    float skillValue = Convert.ToSingle(valueObj);
                                    player.Skills[skillName] = Math.Max(0, Math.Min(skillValue, 100));
                                }
                            }
                            break;

                        case "state":
                            if (change.Value is PlayerState state)
                            {
                                player.CurrentState = state;
                            }
                            else if (change.Value is string stateStr && Enum.TryParse<PlayerState>(stateStr, out PlayerState parsedState))
                            {
                                player.CurrentState = parsedState;
                            }
                            break;

                        case "targetid":
                            player.TargetId = change.Value?.ToString() ?? "";
                            break;

                        case "currentaction":
                            player.CurrentAction = change.Value?.ToString() ?? "";
                            break;

                        default:
                            Console.WriteLine($"Unknown change type: {change.Key}");
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error applying change {change.Key}: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Update entity state from player data
        /// </summary>
        private void UpdateEntityFromPlayer(string playerId, PlayerData player)
        {
            if (!Entities.ContainsKey(playerId))
            {
                Entities[playerId] = new EntityState();
            }

            var entity = Entities[playerId];
            entity.Id = playerId;
            entity.Position = new System.Numerics.Vector3(
                player.CurrentPosition.X,
                player.CurrentPosition.Y,
                player.CurrentPosition.Z
            );
            entity.Health = player.Health;
            entity.Type = "player";
            entity.CurrentState = player.CurrentState.ToString();
        }
    }

    /// <summary>
    /// Represents an entity in the game world
    /// </summary>
    public class EntityState
    {
        public string Id { get; set; } = string.Empty;
        public System.Numerics.Vector3 Position { get; set; }
        public float Health { get; set; }
        public string Type { get; set; } = string.Empty;
        public string CurrentState { get; set; } = string.Empty;
    }
}

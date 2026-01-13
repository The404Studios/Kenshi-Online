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
        /// Apply an action result to the world state
        /// </summary>
        public void ApplyActionResult(ActionResult result)
        {
            if (result == null || !result.Success)
                return;

            var action = result.Action;
            if (action == null)
                return;

            // Apply changes based on action type
            if (result.Changes != null)
            {
                foreach (var change in result.Changes)
                {
                    switch (change.Key)
                    {
                        case "position":
                            if (players.TryGetValue(action.PlayerId, out var player) && change.Value is Position pos)
                            {
                                player.Position = pos;
                            }
                            break;
                        case "health":
                            if (players.TryGetValue(action.PlayerId, out var healthPlayer) && change.Value is float health)
                            {
                                healthPlayer.Health = health;
                            }
                            break;
                        case "entity":
                            if (change.Value is EntityState entityState)
                            {
                                Entities[entityState.Id] = entityState;
                            }
                            break;
                    }
                }
            }

            Logger.Log($"Applied action result for player {action.PlayerId}: {action.Type}");
        }

        /// <summary>
        /// Add a player to the world state
        /// </summary>
        public void AddPlayer(string playerId, PlayerData data)
        {
            players[playerId] = data;
        }

        /// <summary>
        /// Remove a player from the world state
        /// </summary>
        public void RemovePlayer(string playerId)
        {
            players.Remove(playerId);
        }

        /// <summary>
        /// Update an entity in the world state
        /// </summary>
        public void UpdateEntity(string entityId, EntityState state)
        {
            Entities[entityId] = state;
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

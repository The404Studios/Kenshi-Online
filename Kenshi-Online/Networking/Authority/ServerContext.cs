using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking.Authority;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Server context that holds all shared services for the multiplayer system.
    /// This is the central integration point for authority, state replication, and persistence.
    /// </summary>
    public class ServerContext : IDisposable
    {
        private const string LOG_PREFIX = "[ServerContext] ";

        // Core services
        public AuthorityManager Authority { get; }
        public StateReplicator StateReplicator { get; }
        public ServerSaveManager SaveManager { get; }

        // Player tracking
        private readonly ConcurrentDictionary<string, ConnectedPlayer> connectedPlayers = new();

        // Events
        public event Action<string, AuthorityValidationResult> OnActionRejected;
        public event Action<string, string> OnPlayerSaveUpdated;

        public ServerContext(string savePath)
        {
            Authority = new AuthorityManager();
            StateReplicator = new StateReplicator();
            SaveManager = new ServerSaveManager(savePath);

            // Wire up save manager events
            SaveManager.OnPlayerSaved += (playerId, data) =>
            {
                Logger.Log(LOG_PREFIX + $"Player {playerId} save updated (v{data.SaveVersion})");
                OnPlayerSaveUpdated?.Invoke(playerId, $"v{data.SaveVersion}");
            };

            SaveManager.OnSaveError += error =>
            {
                Logger.Log(LOG_PREFIX + $"Save error: {error}");
            };

            Logger.Log(LOG_PREFIX + "ServerContext initialized");
        }

        #region Player Management

        /// <summary>
        /// Register a new player when they connect
        /// </summary>
        public async Task<bool> RegisterPlayer(string playerId, string username)
        {
            try
            {
                // Register player entity with authority manager
                Authority.RegisterPlayerEntity(playerId);

                // Load player save
                var saveData = await SaveManager.LoadPlayerSave(playerId);

                // Track connected player
                var player = new ConnectedPlayer
                {
                    PlayerId = playerId,
                    Username = username,
                    ConnectedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    SaveData = saveData
                };
                connectedPlayers[playerId] = player;

                Logger.Log(LOG_PREFIX + $"Player {playerId} ({username}) registered");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR registering player {playerId}: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Unregister a player when they disconnect
        /// </summary>
        public async Task UnregisterPlayer(string playerId)
        {
            try
            {
                if (connectedPlayers.TryRemove(playerId, out var player))
                {
                    // Save player data before disconnect
                    if (player.SaveData != null)
                    {
                        player.SaveData.IsDirty = true;
                        await SaveManager.SavePlayerData(playerId, player.SaveData);
                    }

                    // Remove from authority tracking
                    Authority.RemovePlayer(playerId);

                    Logger.Log(LOG_PREFIX + $"Player {playerId} unregistered and saved");
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR unregistering player {playerId}: {ex.Message}");
            }
        }

        /// <summary>
        /// Get a connected player
        /// </summary>
        public ConnectedPlayer GetPlayer(string playerId)
        {
            connectedPlayers.TryGetValue(playerId, out var player);
            return player;
        }

        /// <summary>
        /// Get all connected players
        /// </summary>
        public IEnumerable<ConnectedPlayer> GetAllPlayers()
        {
            return connectedPlayers.Values;
        }

        #endregion

        #region Action Validation

        /// <summary>
        /// Validate and process a position update request
        /// </summary>
        public AuthorityValidationResult ValidatePositionUpdate(string playerId, float x, float y, float z,
            float previousX, float previousY, float previousZ, float timeDelta)
        {
            float dx = x - previousX;
            float dy = y - previousY;
            float dz = z - previousZ;
            float distance = MathF.Sqrt(dx * dx + dy * dy + dz * dz);

            var request = new PositionUpdateRequest
            {
                X = x,
                Y = y,
                Z = z,
                Distance = distance,
                TimeDeltaSeconds = timeDelta
            };

            var result = Authority.ValidateAction(playerId, playerId, GameSystem.Position, request);

            if (!result.IsValid)
            {
                Logger.Log(LOG_PREFIX + $"Position rejected for {playerId}: {result.RejectionReason}");
                OnActionRejected?.Invoke(playerId, result);
            }
            else
            {
                // Update transient state
                StateReplicator.UpdateTransient(playerId, "Position", new { X = x, Y = y, Z = z }, playerId);
            }

            return result;
        }

        /// <summary>
        /// Validate and process a combat action request
        /// </summary>
        public AuthorityValidationResult ValidateCombatAction(string attackerId, string targetId,
            string actionType, string weaponId = null)
        {
            var request = new CombatActionRequest
            {
                AttackerId = attackerId,
                TargetId = targetId,
                ActionType = actionType,
                WeaponId = weaponId
            };

            var result = Authority.ValidateAction(attackerId, attackerId, GameSystem.Combat, request);

            if (!result.IsValid)
            {
                Logger.Log(LOG_PREFIX + $"Combat rejected for {attackerId}: {result.RejectionReason}");
                OnActionRejected?.Invoke(attackerId, result);
            }
            else
            {
                // Queue combat event for Tier 1 replication
                var combatEvent = new ReplicatedEvent
                {
                    EventType = "CombatAction",
                    EntityId = attackerId,
                    SourcePlayerId = attackerId,
                    TargetEntityId = targetId,
                    Data = new Dictionary<string, object>
                    {
                        { "actionType", actionType },
                        { "weaponId", weaponId }
                    }
                };
                StateReplicator.QueueEvent(combatEvent);
            }

            return result;
        }

        /// <summary>
        /// Validate and process an inventory change request
        /// </summary>
        public async Task<AuthorityValidationResult> ValidateInventoryChange(string playerId,
            string itemId, int quantityChange, string changeType)
        {
            var result = new AuthorityValidationResult { IsValid = true };

            // Get player's current inventory from save
            var player = GetPlayer(playerId);
            if (player?.SaveData == null)
            {
                result.IsValid = false;
                result.RejectionReason = "Player not found";
                return result;
            }

            var inventory = player.SaveData.Inventory ?? new Dictionary<string, int>();

            // Validate based on change type
            switch (changeType.ToLower())
            {
                case "pickup":
                    // Server must verify item exists in world (handled by caller)
                    if (quantityChange <= 0)
                    {
                        result.IsValid = false;
                        result.RejectionReason = "Invalid pickup quantity";
                        return result;
                    }
                    break;

                case "drop":
                    // Verify player has the item
                    if (!inventory.TryGetValue(itemId, out int currentQty) || currentQty < Math.Abs(quantityChange))
                    {
                        result.IsValid = false;
                        result.RejectionReason = "Insufficient items to drop";
                        return result;
                    }
                    break;

                case "use":
                    // Verify player has the item
                    if (!inventory.TryGetValue(itemId, out int qty) || qty < Math.Abs(quantityChange))
                    {
                        result.IsValid = false;
                        result.RejectionReason = "Insufficient items to use";
                        return result;
                    }
                    break;

                default:
                    result.IsValid = false;
                    result.RejectionReason = $"Unknown change type: {changeType}";
                    return result;
            }

            // Apply the change
            if (result.IsValid)
            {
                // Update inventory
                if (!inventory.ContainsKey(itemId))
                    inventory[itemId] = 0;

                inventory[itemId] += quantityChange;

                // Remove if zero or negative
                if (inventory[itemId] <= 0)
                    inventory.Remove(itemId);

                // Update persistent state
                StateReplicator.UpdatePersistent(playerId, "Inventory", inventory, AuthorityManager.SERVER_OWNER_ID);

                // Mark save as dirty
                player.SaveData.Inventory = inventory;
                player.SaveData.IsDirty = true;

                // Queue inventory event
                var inventoryEvent = new ReplicatedEvent
                {
                    EventType = "InventoryChange",
                    EntityId = playerId,
                    SourcePlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "itemId", itemId },
                        { "change", quantityChange },
                        { "type", changeType }
                    }
                };
                StateReplicator.QueueEvent(inventoryEvent);
            }

            return result;
        }

        /// <summary>
        /// Update player stats (server-authoritative)
        /// </summary>
        public async Task<bool> UpdatePlayerStats(string playerId, string stat, object value)
        {
            var player = GetPlayer(playerId);
            if (player?.SaveData == null)
                return false;

            // Update via save manager (validates and applies)
            bool success = await SaveManager.UpdatePlayerPersistentState(playerId, stat, value);

            if (success)
            {
                // Update persistent state replicator
                StateReplicator.UpdatePersistent(playerId, stat, value, AuthorityManager.SERVER_OWNER_ID);
            }

            return success;
        }

        #endregion

        #region State Synchronization

        /// <summary>
        /// Get state updates to send to a client
        /// </summary>
        public StateUpdatePacket GetStateUpdatesForClient(string clientId)
        {
            var packet = new StateUpdatePacket
            {
                ClientId = clientId,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            // Get dirty transient states
            foreach (var state in StateReplicator.GetDirtyTransient())
            {
                packet.TransientUpdates.Add(new TransientStateUpdate
                {
                    EntityId = state.EntityId,
                    Property = state.PropertyPath,
                    Value = state.Value,
                    Version = state.Version
                });
            }

            // Get pending events
            foreach (var evt in StateReplicator.GetPendingEvents())
            {
                packet.Events.Add(evt);
            }

            return packet;
        }

        /// <summary>
        /// Get save snapshot for client sync
        /// </summary>
        public SaveSnapshot GetSaveSnapshot(string playerId)
        {
            return SaveManager.CreateClientSnapshot(playerId);
        }

        /// <summary>
        /// Process client acknowledgment
        /// </summary>
        public void ProcessAcknowledgment(string eventId)
        {
            StateReplicator.AcknowledgeEvent(eventId);
        }

        #endregion

        #region NPC Management

        /// <summary>
        /// Register an NPC (server-owned)
        /// </summary>
        public void RegisterNPC(string npcId)
        {
            Authority.RegisterNPC(npcId);
        }

        /// <summary>
        /// Update NPC state (server-authoritative)
        /// </summary>
        public void UpdateNPCState(string npcId, float x, float y, float z, int health, string state)
        {
            // NPCs are always Tier 0 (transient)
            StateReplicator.UpdateTransient(npcId, "Position", new { X = x, Y = y, Z = z }, AuthorityManager.SERVER_OWNER_ID);
            StateReplicator.UpdateTransient(npcId, "Health", health, AuthorityManager.SERVER_OWNER_ID);
            StateReplicator.UpdateTransient(npcId, "State", state, AuthorityManager.SERVER_OWNER_ID);
        }

        #endregion

        #region Persistence

        /// <summary>
        /// Force save all dirty data
        /// </summary>
        public async Task ForceSaveAll()
        {
            await SaveManager.SaveAllDirty();
            Logger.Log(LOG_PREFIX + "Force saved all dirty data");
        }

        /// <summary>
        /// Clean up old backups
        /// </summary>
        public void CleanupBackups(int keepCount = 10)
        {
            SaveManager.CleanupOldBackups(keepCount);
        }

        #endregion

        public void Dispose()
        {
            // Save all before shutdown
            SaveManager.SaveAllDirty().Wait();
            SaveManager.Dispose();
            Logger.Log(LOG_PREFIX + "ServerContext disposed");
        }
    }

    /// <summary>
    /// Represents a connected player with their session data
    /// </summary>
    public class ConnectedPlayer
    {
        public string PlayerId { get; set; }
        public string Username { get; set; }
        public long ConnectedAt { get; set; }
        public PlayerSaveData SaveData { get; set; }
        public float LastX { get; set; }
        public float LastY { get; set; }
        public float LastZ { get; set; }
        public long LastUpdateTime { get; set; }
    }

    /// <summary>
    /// Packet of state updates to send to a client
    /// </summary>
    public class StateUpdatePacket
    {
        public string ClientId { get; set; }
        public long Timestamp { get; set; }
        public List<TransientStateUpdate> TransientUpdates { get; set; } = new();
        public List<ReplicatedEvent> Events { get; set; } = new();
    }

    /// <summary>
    /// Single transient state update
    /// </summary>
    public class TransientStateUpdate
    {
        public string EntityId { get; set; }
        public string Property { get; set; }
        public object Value { get; set; }
        public long Version { get; set; }
    }
}

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Linq;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// High-level player controller that manages player actions and state
    /// </summary>
    public class PlayerController
    {
        private readonly KenshiGameBridge gameBridge;
        private readonly Dictionary<string, PlayerData> players = new Dictionary<string, PlayerData>();
        private readonly Dictionary<string, PlayerControlState> controlStates = new Dictionary<string, PlayerControlState>();
        private readonly Logger logger = new Logger("PlayerController");

        public PlayerController(KenshiGameBridge gameBridge)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            logger.Log("PlayerController initialized");
        }

        #region Player Registration

        /// <summary>
        /// Register a player for control
        /// </summary>
        public bool RegisterPlayer(string playerId, PlayerData playerData)
        {
            try
            {
                if (players.ContainsKey(playerId))
                {
                    logger.Log($"Player {playerId} already registered, updating data...");
                    players[playerId] = playerData;
                    return true;
                }

                players[playerId] = playerData;
                controlStates[playerId] = new PlayerControlState
                {
                    PlayerId = playerId,
                    IsActive = false,
                    LastUpdateTime = DateTime.UtcNow
                };

                logger.Log($"Registered player {playerId} ({playerData.DisplayName})");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR registering player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Unregister a player
        /// </summary>
        public bool UnregisterPlayer(string playerId)
        {
            try
            {
                if (!players.ContainsKey(playerId))
                    return false;

                players.Remove(playerId);
                controlStates.Remove(playerId);
                logger.Log($"Unregistered player {playerId}");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR unregistering player: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Movement Control

        /// <summary>
        /// Move player to a target position
        /// </summary>
        public bool MovePlayer(string playerId, Position targetPosition)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                logger.Log($"Moving player {playerId} to {targetPosition.X}, {targetPosition.Y}, {targetPosition.Z}");

                // Update player data
                var playerData = players[playerId];
                playerData.Position = targetPosition;
                playerData.CurrentState = PlayerState.Moving;

                // Send move command to game
                bool success = gameBridge.SendGameCommand(playerId, "move",
                    targetPosition.X, targetPosition.Y, targetPosition.Z);

                if (success)
                {
                    UpdateControlState(playerId);
                }

                return success;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR moving player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Make player follow another character
        /// </summary>
        public bool FollowPlayer(string playerId, string targetPlayerId)
        {
            if (!ValidatePlayer(playerId) || !ValidatePlayer(targetPlayerId))
                return false;

            try
            {
                logger.Log($"Player {playerId} following {targetPlayerId}");

                var playerData = players[playerId];
                playerData.CurrentState = PlayerState.Moving;
                playerData.TargetId = targetPlayerId;

                bool success = gameBridge.SendGameCommand(playerId, "follow", targetPlayerId);

                if (success)
                {
                    UpdateControlState(playerId);
                    OnPlayerFollowing?.Invoke(playerId, targetPlayerId);
                }

                return success;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in follow command: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Stop player movement
        /// </summary>
        public bool StopPlayer(string playerId)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                var playerData = players[playerId];
                var currentPos = gameBridge.GetPlayerPosition(playerId);

                if (currentPos != null)
                {
                    playerData.Position = currentPos;
                    playerData.CurrentState = PlayerState.Idle;
                    playerData.TargetId = null;

                    UpdateControlState(playerId);
                    logger.Log($"Stopped player {playerId}");
                    return true;
                }

                return false;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR stopping player: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Combat Control

        /// <summary>
        /// Make player attack a target
        /// </summary>
        public bool AttackTarget(string playerId, string targetId)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                logger.Log($"Player {playerId} attacking {targetId}");

                var playerData = players[playerId];
                playerData.CurrentState = PlayerState.Fighting;
                playerData.TargetId = targetId;

                bool success = gameBridge.SendGameCommand(playerId, "attack", targetId);

                if (success)
                {
                    UpdateControlState(playerId);
                    OnPlayerAttacking?.Invoke(playerId, targetId);
                }

                return success;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in attack command: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Make player defend/block
        /// </summary>
        public bool DefendPlayer(string playerId)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                var playerData = players[playerId];
                playerData.CurrentState = PlayerState.Fighting;

                // Set defensive stance
                logger.Log($"Player {playerId} entering defensive stance");
                UpdateControlState(playerId);
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in defend command: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Interaction Control

        /// <summary>
        /// Make player pickup an item
        /// </summary>
        public bool PickupItem(string playerId, string itemId)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                logger.Log($"Player {playerId} picking up item {itemId}");

                var playerData = players[playerId];
                playerData.CurrentState = PlayerState.Looting;

                bool success = gameBridge.SendGameCommand(playerId, "pickup", itemId);

                if (success)
                {
                    UpdateControlState(playerId);
                    OnPlayerPickupItem?.Invoke(playerId, itemId);
                }

                return success;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in pickup command: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Make player talk to NPC
        /// </summary>
        public bool TalkToNPC(string playerId, string npcId)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                logger.Log($"Player {playerId} talking to NPC {npcId}");

                var playerData = players[playerId];
                playerData.CurrentState = PlayerState.Talking;
                playerData.TargetId = npcId;

                UpdateControlState(playerId);
                OnPlayerTalking?.Invoke(playerId, npcId);
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in talk command: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region State Management

        /// <summary>
        /// Update player state from game
        /// </summary>
        public PlayerData UpdatePlayerState(string playerId)
        {
            if (!ValidatePlayer(playerId))
                return null;

            try
            {
                var playerData = players[playerId];

                // Get current position from game
                var currentPos = gameBridge.GetPlayerPosition(playerId);
                if (currentPos != null)
                {
                    // Check if position changed significantly
                    if (playerData.Position == null ||
                        currentPos.DistanceTo(playerData.Position) > 0.1f)
                    {
                        playerData.Position = currentPos;
                        OnPlayerPositionChanged?.Invoke(playerId, currentPos);
                    }
                }

                UpdateControlState(playerId);
                return playerData;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR updating player state: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Get player data
        /// </summary>
        public PlayerData GetPlayerData(string playerId)
        {
            return players.TryGetValue(playerId, out var data) ? data : null;
        }

        /// <summary>
        /// Get all active players
        /// </summary>
        public List<PlayerData> GetActivePlayers()
        {
            return players.Values.ToList();
        }

        /// <summary>
        /// Set player health
        /// </summary>
        public bool SetPlayerHealth(string playerId, float health)
        {
            if (!ValidatePlayer(playerId))
                return false;

            try
            {
                var playerData = players[playerId];
                playerData.Health = Math.Max(0, Math.Min(health, playerData.MaxHealth));

                if (playerData.Health <= 0)
                {
                    playerData.CurrentState = PlayerState.Dead;
                    OnPlayerDied?.Invoke(playerId);
                }
                else if (playerData.Health < playerData.MaxHealth * 0.3f)
                {
                    playerData.CurrentState = PlayerState.Unconscious;
                }

                UpdateControlState(playerId);
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR setting player health: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Squad Management

        /// <summary>
        /// Create a squad with multiple players
        /// </summary>
        public string CreateSquad(List<string> playerIds, string squadName = null)
        {
            try
            {
                string squadId = Guid.NewGuid().ToString();
                logger.Log($"Creating squad {squadId} with {playerIds.Count} players");

                foreach (var playerId in playerIds)
                {
                    if (ValidatePlayer(playerId))
                    {
                        var playerData = players[playerId];
                        // Store squad info (would need to add SquadId to PlayerData)
                        logger.Log($"Added player {playerId} to squad {squadId}");
                    }
                }

                OnSquadCreated?.Invoke(squadId, playerIds);
                return squadId;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR creating squad: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Make entire squad follow a player
        /// </summary>
        public bool SquadFollow(string squadId, string targetPlayerId)
        {
            try
            {
                // Would need squad tracking to implement this fully
                logger.Log($"Squad {squadId} following {targetPlayerId}");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in squad follow: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Events

        public event Action<string, Position> OnPlayerPositionChanged;
        public event Action<string, string> OnPlayerFollowing;
        public event Action<string, string> OnPlayerAttacking;
        public event Action<string, string> OnPlayerPickupItem;
        public event Action<string, string> OnPlayerTalking;
        public event Action<string> OnPlayerDied;
        public event Action<string, List<string>> OnSquadCreated;

        #endregion

        #region Helpers

        private bool ValidatePlayer(string playerId)
        {
            if (string.IsNullOrEmpty(playerId))
            {
                logger.Log("ERROR: Player ID is null or empty");
                return false;
            }

            if (!players.ContainsKey(playerId))
            {
                logger.Log($"ERROR: Player {playerId} not registered");
                return false;
            }

            return true;
        }

        private void UpdateControlState(string playerId)
        {
            if (controlStates.TryGetValue(playerId, out var state))
            {
                state.LastUpdateTime = DateTime.UtcNow;
                state.IsActive = true;
            }
        }

        #endregion

        /// <summary>
        /// Player control state tracking
        /// </summary>
        private class PlayerControlState
        {
            public string PlayerId { get; set; }
            public bool IsActive { get; set; }
            public DateTime LastUpdateTime { get; set; }
            public string CurrentCommand { get; set; }
        }
    }
}

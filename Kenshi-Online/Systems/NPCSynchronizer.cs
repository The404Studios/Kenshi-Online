using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Kenshi_Online.Game;
using Kenshi_Online.Data;
using Kenshi_Online.Networking;
using Kenshi_Online.Utility;

namespace Kenshi_Online.Systems
{
    /// <summary>
    /// Synchronizes NPCs and world entities across clients
    /// </summary>
    public class NPCSynchronizer
    {
        private readonly EnhancedGameBridge gameBridge;
        private readonly Dictionary<int, NPCState> npcStates = new Dictionary<int, NPCState>();
        private readonly Dictionary<int, DateTime> lastUpdateTimes = new Dictionary<int, DateTime>();
        private readonly Logger logger = new Logger("NPCSynchronizer");

        private Timer updateTimer;
        private bool isRunning;

        // Configuration
        private const int UPDATE_INTERVAL_MS = 100; // 10 Hz for NPCs
        private const float UPDATE_DISTANCE_THRESHOLD = 2.0f; // 2 meters
        private const int MAX_NPCS_PER_UPDATE = 50;
        private const float PLAYER_SYNC_RADIUS = 100.0f; // Only sync NPCs within 100m of players

        public NPCSynchronizer(EnhancedGameBridge gameBridge)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
        }

        #region Start/Stop

        public void Start()
        {
            if (isRunning) return;

            logger.Log("Starting NPC synchronizer...");
            isRunning = true;

            updateTimer = new Timer(UpdateNPCs, null, 0, UPDATE_INTERVAL_MS);

            logger.Log("NPC synchronizer started");
        }

        public void Stop()
        {
            if (!isRunning) return;

            logger.Log("Stopping NPC synchronizer...");
            isRunning = false;

            updateTimer?.Dispose();
            updateTimer = null;

            logger.Log("NPC synchronizer stopped");
        }

        #endregion

        #region Update Loop

        private void UpdateNPCs(object state)
        {
            if (!isRunning || !gameBridge.IsConnected)
                return;

            try
            {
                // Get all characters from game
                var allCharacters = gameBridge.GetAllCharacters();

                // Filter to NPCs only (not player-controlled)
                var npcs = allCharacters.Where(c => c.IsPlayerControlled == 0).ToList();

                // Get player positions for relevance filtering
                var players = gameBridge.GetPlayerCharacters();
                var playerPositions = players.Select(p => new Position(p.PosX, p.PosY, p.PosZ)).ToList();

                // Update relevant NPCs
                int updatedCount = 0;
                foreach (var npc in npcs)
                {
                    if (updatedCount >= MAX_NPCS_PER_UPDATE)
                        break;

                    // Check if NPC is near any player
                    var npcPos = new Position(npc.PosX, npc.PosY, npc.PosZ);
                    bool isRelevant = playerPositions.Any(p => npcPos.DistanceTo(p) <= PLAYER_SYNC_RADIUS);

                    if (!isRelevant)
                        continue;

                    // Check if NPC needs updating
                    if (ShouldUpdateNPC(npc.CharacterID, npcPos))
                    {
                        UpdateNPCState(npc);
                        updatedCount++;
                    }
                }
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR in NPC update: {ex.Message}");
            }
        }

        private bool ShouldUpdateNPC(int npcId, Position currentPos)
        {
            if (!npcStates.TryGetValue(npcId, out var lastState))
                return true; // New NPC

            if (!lastUpdateTimes.TryGetValue(npcId, out var lastUpdate))
                return true;

            // Always update if enough time has passed
            if ((DateTime.UtcNow - lastUpdate).TotalMilliseconds >= UPDATE_INTERVAL_MS * 5)
                return true;

            // Update if position changed significantly
            if (lastState.Position == null || currentPos.DistanceTo(lastState.Position) > UPDATE_DISTANCE_THRESHOLD)
                return true;

            return false;
        }

        private void UpdateNPCState(Character npc)
        {
            try
            {
                var state = new NPCState
                {
                    CharacterID = npc.CharacterID,
                    Name = gameBridge.ReadString(npc.NamePtr) ?? $"NPC_{npc.CharacterID}",
                    Position = new Position(npc.PosX, npc.PosY, npc.PosZ),
                    Health = npc.Health,
                    MaxHealth = npc.MaxHealth,
                    State = (CharacterState)npc.CharacterState,
                    FactionID = npc.FactionID,
                    IsInCombat = npc.IsInCombat == 1,
                    IsUnconscious = npc.IsUnconscious == 1,
                    IsDead = npc.IsDead == 1,
                    LastUpdate = DateTime.UtcNow
                };

                npcStates[npc.CharacterID] = state;
                lastUpdateTimes[npc.CharacterID] = DateTime.UtcNow;

                OnNPCUpdated?.Invoke(state);
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR updating NPC state: {ex.Message}");
            }
        }

        #endregion

        #region Query

        /// <summary>
        /// Get all tracked NPC states
        /// </summary>
        public List<NPCState> GetAllNPCs()
        {
            return npcStates.Values.ToList();
        }

        /// <summary>
        /// Get NPCs near a position
        /// </summary>
        public List<NPCState> GetNPCsNear(Position position, float radius)
        {
            return npcStates.Values
                .Where(npc => npc.Position != null && npc.Position.DistanceTo(position) <= radius)
                .ToList();
        }

        /// <summary>
        /// Get NPC by ID
        /// </summary>
        public NPCState GetNPC(int npcId)
        {
            return npcStates.TryGetValue(npcId, out var state) ? state : null;
        }

        /// <summary>
        /// Get NPCs by faction
        /// </summary>
        public List<NPCState> GetNPCsByFaction(int factionId)
        {
            return npcStates.Values
                .Where(npc => npc.FactionID == factionId)
                .ToList();
        }

        /// <summary>
        /// Get hostile NPCs near position
        /// </summary>
        public List<NPCState> GetHostileNPCsNear(Position position, float radius, int playerFactionId)
        {
            // Would need faction system reference to determine hostility
            // For now, return NPCs in combat
            return npcStates.Values
                .Where(npc => npc.Position != null &&
                              npc.Position.DistanceTo(position) <= radius &&
                              npc.IsInCombat &&
                              npc.FactionID != playerFactionId)
                .ToList();
        }

        #endregion

        #region Events

        public event Action<NPCState> OnNPCUpdated;
        public event Action<int> OnNPCDied;
        public event Action<int> OnNPCSpawned;

        #endregion

        #region Helpers

        /// <summary>
        /// Clear old NPC data
        /// </summary>
        public void CleanupOldData(TimeSpan maxAge)
        {
            var cutoff = DateTime.UtcNow - maxAge;
            var oldNPCs = lastUpdateTimes
                .Where(kvp => kvp.Value < cutoff)
                .Select(kvp => kvp.Key)
                .ToList();

            foreach (var npcId in oldNPCs)
            {
                npcStates.Remove(npcId);
                lastUpdateTimes.Remove(npcId);
            }

            if (oldNPCs.Count > 0)
            {
                logger.Log($"Cleaned up {oldNPCs.Count} old NPC entries");
            }
        }

        #endregion
    }

    #region NPC State

    public class NPCState
    {
        public int CharacterID { get; set; }
        public string Name { get; set; }
        public Position Position { get; set; }
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public CharacterState State { get; set; }
        public int FactionID { get; set; }
        public bool IsInCombat { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsDead { get; set; }
        public DateTime LastUpdate { get; set; }
    }

    #endregion
}

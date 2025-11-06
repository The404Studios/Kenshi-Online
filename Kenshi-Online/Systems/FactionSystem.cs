using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Kenshi_Online.Game;
using Kenshi_Online.Utility;
using Kenshi_Online.Data;

namespace Kenshi_Online.Systems
{
    /// <summary>
    /// Complete faction system with persistence and synchronization
    /// </summary>
    public class FactionSystem
    {
        private readonly EnhancedGameBridge gameBridge;
        private readonly Dictionary<int, FactionData> factions = new Dictionary<int, FactionData>();
        private readonly Dictionary<(int, int), int> relations = new Dictionary<(int, int), int>(); // (faction1, faction2) -> relation
        private readonly Logger logger = new Logger("FactionSystem");
        private readonly string savePath;

        private const string FACTIONS_FILE = "factions.json";
        private const string RELATIONS_FILE = "faction_relations.json";

        public FactionSystem(EnhancedGameBridge gameBridge, string dataPath = "./data")
        {
            this.gameBridge = gameBridge;
            this.savePath = dataPath;
            Directory.CreateDirectory(savePath);

            LoadFromDisk();
        }

        #region Faction Management

        /// <summary>
        /// Sync factions from game
        /// </summary>
        public void SyncFromGame()
        {
            if (!gameBridge.IsConnected)
                return;

            try
            {
                logger.Log("Syncing factions from game...");

                var gameFactions = gameBridge.GetAllFactions();

                foreach (var gameFaction in gameFactions)
                {
                    var factionData = new FactionData
                    {
                        FactionID = gameFaction.FactionID,
                        Name = gameBridge.ReadString(gameFaction.NamePtr) ?? $"Faction_{gameFaction.FactionID}",
                        FactionType = (FactionType)gameFaction.FactionType,
                        IsPlayerFaction = gameFaction.IsPlayerFaction == 1,
                        IsHostile = gameFaction.IsHostileToPlayer == 1,
                        CanRecruit = gameFaction.CanRecruit == 1,
                        Wealth = gameFaction.Wealth,
                        MemberCount = gameFaction.MemberCount
                    };

                    factions[gameFaction.FactionID] = factionData;
                }

                logger.Log($"Synced {factions.Count} factions from game");

                // Sync relations
                SyncRelationsFromGame();

                // Save to disk
                SaveToDisk();
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR syncing factions: {ex.Message}");
            }
        }

        /// <summary>
        /// Sync faction relations from game
        /// </summary>
        private void SyncRelationsFromGame()
        {
            try
            {
                var factionIds = factions.Keys.ToList();

                foreach (var factionId1 in factionIds)
                {
                    foreach (var factionId2 in factionIds)
                    {
                        if (factionId1 == factionId2) continue;

                        int relation = gameBridge.GetFactionRelation(factionId1, factionId2);
                        relations[(factionId1, factionId2)] = relation;
                    }
                }

                logger.Log($"Synced {relations.Count} faction relations");
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR syncing relations: {ex.Message}");
            }
        }

        /// <summary>
        /// Get faction by ID
        /// </summary>
        public FactionData GetFaction(int factionId)
        {
            return factions.TryGetValue(factionId, out var faction) ? faction : null;
        }

        /// <summary>
        /// Get all factions
        /// </summary>
        public List<FactionData> GetAllFactions()
        {
            return factions.Values.ToList();
        }

        /// <summary>
        /// Get player factions
        /// </summary>
        public List<FactionData> GetPlayerFactions()
        {
            return factions.Values.Where(f => f.IsPlayerFaction).ToList();
        }

        /// <summary>
        /// Create a new player faction
        /// </summary>
        public FactionData CreatePlayerFaction(string name, string leaderId)
        {
            try
            {
                int newId = factions.Keys.Any() ? factions.Keys.Max() + 1 : 1000;

                var faction = new FactionData
                {
                    FactionID = newId,
                    Name = name,
                    LeaderPlayerId = leaderId,
                    FactionType = FactionType.Player,
                    IsPlayerFaction = true,
                    IsHostile = false,
                    CanRecruit = true,
                    Wealth = 0,
                    MemberCount = 1,
                    Members = new List<string> { leaderId },
                    CreatedDate = DateTime.UtcNow
                };

                factions[newId] = faction;

                // Set default relations
                SetDefaultRelations(newId);

                SaveToDisk();

                logger.Log($"Created player faction: {name} (ID: {newId})");
                OnFactionCreated?.Invoke(faction);

                return faction;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR creating faction: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Add member to faction
        /// </summary>
        public bool AddMember(int factionId, string playerId)
        {
            try
            {
                if (!factions.TryGetValue(factionId, out var faction))
                    return false;

                if (faction.Members.Contains(playerId))
                    return false;

                faction.Members.Add(playerId);
                faction.MemberCount = faction.Members.Count;

                SaveToDisk();

                logger.Log($"Added {playerId} to faction {faction.Name}");
                OnMemberAdded?.Invoke(factionId, playerId);

                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR adding member: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Remove member from faction
        /// </summary>
        public bool RemoveMember(int factionId, string playerId)
        {
            try
            {
                if (!factions.TryGetValue(factionId, out var faction))
                    return false;

                if (!faction.Members.Remove(playerId))
                    return false;

                faction.MemberCount = faction.Members.Count;

                // If leader leaves, disband or assign new leader
                if (faction.LeaderPlayerId == playerId)
                {
                    if (faction.Members.Count > 0)
                    {
                        faction.LeaderPlayerId = faction.Members[0];
                    }
                    else
                    {
                        // Disband faction
                        factions.Remove(factionId);
                    }
                }

                SaveToDisk();

                logger.Log($"Removed {playerId} from faction {faction.Name}");
                OnMemberRemoved?.Invoke(factionId, playerId);

                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR removing member: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Relations

        /// <summary>
        /// Get relation between two factions
        /// </summary>
        public int GetRelation(int factionId1, int factionId2)
        {
            if (factionId1 == factionId2) return 100; // Same faction

            if (relations.TryGetValue((factionId1, factionId2), out int relation))
                return relation;

            return 0; // Neutral by default
        }

        /// <summary>
        /// Set relation between two factions
        /// </summary>
        public bool SetRelation(int factionId1, int factionId2, int relation)
        {
            try
            {
                // Clamp relation
                relation = Math.Max(-100, Math.Min(100, relation));

                relations[(factionId1, factionId2)] = relation;
                relations[(factionId2, factionId1)] = relation; // Symmetric

                // Update in game if connected
                if (gameBridge.IsConnected)
                {
                    gameBridge.SetFactionRelation(factionId1, factionId2, relation);
                    gameBridge.SetFactionRelation(factionId2, factionId1, relation);
                }

                SaveToDisk();

                logger.Log($"Set faction relation: {factionId1} <-> {factionId2} = {relation}");
                OnRelationChanged?.Invoke(factionId1, factionId2, relation);

                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR setting relation: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Modify relation (add/subtract)
        /// </summary>
        public bool ModifyRelation(int factionId1, int factionId2, int delta)
        {
            int currentRelation = GetRelation(factionId1, factionId2);
            return SetRelation(factionId1, factionId2, currentRelation + delta);
        }

        /// <summary>
        /// Check if factions are hostile
        /// </summary>
        public bool AreHostile(int factionId1, int factionId2)
        {
            return GetRelation(factionId1, factionId2) < -25;
        }

        /// <summary>
        /// Check if factions are allied
        /// </summary>
        public bool AreAllied(int factionId1, int factionId2)
        {
            return GetRelation(factionId1, factionId2) > 50;
        }

        /// <summary>
        /// Set default relations for new faction
        /// </summary>
        private void SetDefaultRelations(int factionId)
        {
            foreach (var existingFactionId in factions.Keys)
            {
                if (existingFactionId == factionId) continue;

                var existingFaction = factions[existingFactionId];

                // Set default neutral relations
                int defaultRelation = 0;

                // Hostile to known hostile factions
                if (existingFaction.IsHostile)
                {
                    defaultRelation = -50;
                }

                SetRelation(factionId, existingFactionId, defaultRelation);
            }
        }

        #endregion

        #region Persistence

        /// <summary>
        /// Save factions and relations to disk
        /// </summary>
        public void SaveToDisk()
        {
            try
            {
                // Save factions
                string factionsPath = Path.Combine(savePath, FACTIONS_FILE);
                var factionsJson = JsonSerializer.Serialize(factions, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(factionsPath, factionsJson);

                // Save relations
                string relationsPath = Path.Combine(savePath, RELATIONS_FILE);
                var relationsDict = relations.ToDictionary(
                    kvp => $"{kvp.Key.Item1}_{kvp.Key.Item2}",
                    kvp => kvp.Value
                );
                var relationsJson = JsonSerializer.Serialize(relationsDict, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(relationsPath, relationsJson);

                logger.Log($"Saved {factions.Count} factions and {relations.Count} relations to disk");
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR saving to disk: {ex.Message}");
            }
        }

        /// <summary>
        /// Load factions and relations from disk
        /// </summary>
        public void LoadFromDisk()
        {
            try
            {
                // Load factions
                string factionsPath = Path.Combine(savePath, FACTIONS_FILE);
                if (File.Exists(factionsPath))
                {
                    string factionsJson = File.ReadAllText(factionsPath);
                    var loadedFactions = JsonSerializer.Deserialize<Dictionary<int, FactionData>>(factionsJson);

                    if (loadedFactions != null)
                    {
                        factions.Clear();
                        foreach (var kvp in loadedFactions)
                        {
                            factions[kvp.Key] = kvp.Value;
                        }
                    }

                    logger.Log($"Loaded {factions.Count} factions from disk");
                }

                // Load relations
                string relationsPath = Path.Combine(savePath, RELATIONS_FILE);
                if (File.Exists(relationsPath))
                {
                    string relationsJson = File.ReadAllText(relationsPath);
                    var loadedRelations = JsonSerializer.Deserialize<Dictionary<string, int>>(relationsJson);

                    if (loadedRelations != null)
                    {
                        relations.Clear();
                        foreach (var kvp in loadedRelations)
                        {
                            var parts = kvp.Key.Split('_');
                            if (parts.Length == 2 &&
                                int.TryParse(parts[0], out int f1) &&
                                int.TryParse(parts[1], out int f2))
                            {
                                relations[(f1, f2)] = kvp.Value;
                            }
                        }
                    }

                    logger.Log($"Loaded {relations.Count} faction relations from disk");
                }
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR loading from disk: {ex.Message}");
            }
        }

        #endregion

        #region Events

        public event Action<FactionData> OnFactionCreated;
        public event Action<int, string> OnMemberAdded;
        public event Action<int, string> OnMemberRemoved;
        public event Action<int, int, int> OnRelationChanged;

        #endregion
    }

    #region Data Classes

    public class FactionData
    {
        public int FactionID { get; set; }
        public string Name { get; set; }
        public string LeaderPlayerId { get; set; }
        public FactionType FactionType { get; set; }
        public bool IsPlayerFaction { get; set; }
        public bool IsHostile { get; set; }
        public bool CanRecruit { get; set; }
        public int Wealth { get; set; }
        public int MemberCount { get; set; }
        public List<string> Members { get; set; } = new List<string>();
        public DateTime CreatedDate { get; set; }
        public Dictionary<string, object> CustomData { get; set; } = new Dictionary<string, object>();
    }

    public enum FactionType
    {
        Player = 0,
        Hostile = 1,
        Neutral = 2,
        Friendly = 3,
        Allied = 4
    }

    #endregion
}

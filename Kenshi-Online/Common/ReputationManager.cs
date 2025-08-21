using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common
{
    using global::KenshiMultiplayer.Auth;
    using global::KenshiMultiplayer.Common.NotificationManager;
    using global::KenshiMultiplayer.Networking;
    using System;
    using System.Collections.Generic;
    using System.IO;
    using System.Linq;
    using System.Text.Json;

    namespace KenshiMultiplayer
    {
        public enum FactionType
        {FileName
            Major,      // Holy Nation, United Cities, Shek Kingdom
            Minor,      // Smaller factions like Tech Hunters, Flotsam Ninjas
            Bandit,     // Dust Bandits, Starving Bandits, etc.
            Wildlife,   // Animals, Cannibals, Fogmen
            Player,     // Player-created factions
            Neutral     // Traders, Nomads
        }

        public enum ReputationLevel
        {
            Nemesis = -100,     // Kill on sight
            Hostile = -50,      // Attack on sight
            Unfriendly = -25,   // Suspicious, higher prices
            Neutral = 0,        // Default
            Friendly = 25,      // Lower prices, helpful
            Allied = 50,        // Will assist in combat
            Honored = 100       // Maximum reputation
        }

        public class Faction
        {
            public string Id { get; set; }
            public string Name { get; set; }
            public FactionType Type { get; set; }
            public string Description { get; set; }
            public string LeaderName { get; set; }
            public Position Headquarters { get; set; }
            public List<string> ControlledTowns { get; set; } = new List<string>();
            public Dictionary<string, float> FactionRelations { get; set; } = new Dictionary<string, float>();
            public Dictionary<string, string> Ranks { get; set; } = new Dictionary<string, string>();
            public string Color { get; set; } = "#FFFFFF";
            public string Icon { get; set; }
            public bool IsPlayerFaction { get; set; } = false;
            public string FounderId { get; set; }
            public DateTime FoundedDate { get; set; }
            public int MemberCount { get; set; }
            public Dictionary<string, object> CustomData { get; set; } = new Dictionary<string, object>();
        }

        public class PlayerReputation
        {
            public string PlayerId { get; set; }
            public string FactionId { get; set; }
            public float ReputationValue { get; set; } = 0;
            public string CurrentRank { get; set; }
            public DateTime LastUpdated { get; set; } = DateTime.UtcNow;
            public List<ReputationEvent> History { get; set; } = new List<ReputationEvent>();
            public Dictionary<string, int> ActionCounts { get; set; } = new Dictionary<string, int>();
        }

        public class ReputationEvent
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string EventType { get; set; } // kill, trade, quest, rescue, etc.
            public float ReputationChange { get; set; }
            public string Description { get; set; }
            public DateTime Timestamp { get; set; } = DateTime.UtcNow;
            public string Location { get; set; }
            public Dictionary<string, object> Details { get; set; } = new Dictionary<string, object>();
        }

        public class FactionWar
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string AggressorFactionId { get; set; }
            public string DefenderFactionId { get; set; }
            public DateTime StartDate { get; set; }
            public DateTime? EndDate { get; set; }
            public bool IsActive { get; set; } = true;
            public Dictionary<string, int> Casualties { get; set; } = new Dictionary<string, int>();
            public List<string> ContestedTerritories { get; set; } = new List<string>();
            public string CurrentWinner { get; set; }
        }

        public class ReputationManager
        {
            private readonly Dictionary<string, Faction> factions = new Dictionary<string, Faction>();
            private readonly Dictionary<string, PlayerReputation> playerReputations = new Dictionary<string, PlayerReputation>();
            private readonly List<FactionWar> activeWars = new List<FactionWar>();

            private readonly string dataFilePath;
            private readonly EnhancedClient client;
            private readonly NotificationManager notificationManager;

            // Events
            public event EventHandler<(string factionId, float oldRep, float newRep)> ReputationChanged;
            public event EventHandler<(string factionId, ReputationLevel oldLevel, ReputationLevel newLevel)> ReputationLevelChanged;
            public event EventHandler<Faction> FactionCreated;
            public event EventHandler<FactionWar> WarDeclared;
            public event EventHandler<FactionWar> WarEnded;

            // Reputation change values
            private readonly Dictionary<string, float> actionReputationValues = new Dictionary<string, float>
        {
            { "kill_member", -5.0f },
            { "kill_leader", -25.0f },
            { "kill_civilian", -10.0f },
            { "rescue_member", 3.0f },
            { "complete_quest", 5.0f },
            { "trade", 0.1f },
            { "steal", -2.0f },
            { "trespass", -1.0f },
            { "assault", -3.0f },
            { "heal_member", 1.0f },
            { "donate", 2.0f },
            { "defend_town", 10.0f },
            { "raid_enemy", 5.0f }
        };

            public ReputationManager(EnhancedClient clientInstance, NotificationManager notificationManager, string dataDirectory = "data")
            {
                this.client = clientInstance;
                this.notificationManager = notificationManager;
                dataFilePath = Path.Combine(dataDirectory, "reputation_data.json");
                Directory.CreateDirectory(dataDirectory);

                LoadData();
                InitializeKenshiFactions();

                if (client != null)
                {
                    client.MessageReceived += OnMessageReceived;
                }

                // Start reputation decay timer
                System.Threading.Tasks.Task.Run(async () =>
                {
                    while (true)
                    {
                        await System.Threading.Tasks.Task.Delay(TimeSpan.FromHours(1));
                        ApplyReputationDecay();
                    }
                });
            }

            private void LoadData()
            {
                try
                {
                    if (File.Exists(dataFilePath))
                    {
                        string json = File.ReadAllText(dataFilePath);
                        var data = JsonSerializer.Deserialize<ReputationData>(json);

                        if (data != null)
                        {
                            foreach (var faction in data.Factions)
                            {
                                factions[faction.Id] = faction;
                            }

                            foreach (var rep in data.PlayerReputations)
                            {
                                string key = GetReputationKey(rep.PlayerId, rep.FactionId);
                                playerReputations[key] = rep;
                            }

                            activeWars = data.ActiveWars ?? new List<FactionWar>();
                        }

                        Logger.Log($"Loaded {factions.Count} factions, {playerReputations.Count} reputation entries");
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error loading reputation data: {ex.Message}");
                }
            }

            private void SaveData()
            {
                try
                {
                    var data = new ReputationData
                    {
                        Factions = factions.Values.ToList(),
                        PlayerReputations = playerReputations.Values.ToList(),
                        ActiveWars = activeWars
                    };

                    string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                    File.WriteAllText(dataFilePath, json);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error saving reputation data: {ex.Message}");
                }
            }

            private void InitializeKenshiFactions()
            {
                // Major factions
                CreateFaction("holy_nation", "Holy Nation", FactionType.Major,
                    "Religious zealots who worship Okran", "#FFD700");
                CreateFaction("united_cities", "United Cities", FactionType.Major,
                    "Corrupt empire ruled by nobles and slavery", "#800080");
                CreateFaction("shek_kingdom", "Shek Kingdom", FactionType.Major,
                    "Warrior race seeking honor in battle", "#8B4513");

                // Minor factions
                CreateFaction("tech_hunters", "Tech Hunters", FactionType.Minor,
                    "Explorers and archaeologists", "#00CED1");
                CreateFaction("flotsam_ninjas", "Flotsam Ninjas", FactionType.Minor,
                    "Rebels fighting against Holy Nation", "#228B22");
                CreateFaction("anti_slavers", "Anti-Slavers", FactionType.Minor,
                    "Freedom fighters opposing slavery", "#FF6347");
                CreateFaction("crab_raiders", "Crab Raiders", FactionType.Minor,
                    "Crab-obsessed warriors", "#FF8C00");

                // Bandit factions
                CreateFaction("dust_bandits", "Dust Bandits", FactionType.Bandit,
                    "Common bandits roaming the wastes", "#8B4513");
                CreateFaction("starving_bandits", "Starving Bandits", FactionType.Bandit,
                    "Desperate and hungry outlaws", "#696969");
                CreateFaction("black_dragon_ninjas", "Black Dragon Ninjas", FactionType.Bandit,
                    "Elite ninja assassins", "#000000");

                // Wildlife factions
                CreateFaction("cannibals", "Cannibals", FactionType.Wildlife,
                    "Savage tribes that eat human flesh", "#8B0000");
                CreateFaction("fogmen", "Fogmen", FactionType.Wildlife,
                    "Degenerated cannibals of the Fog Islands", "#778899");
                CreateFaction("skin_bandits", "Skin Bandits", FactionType.Wildlife,
                    "Insane robots wearing human skin", "#DDA0DD");

                // Set default faction relations
                SetFactionRelations();
            }

            private void SetFactionRelations()
            {
                // Holy Nation relations
                SetFactionRelation("holy_nation", "united_cities", -30);
                SetFactionRelation("holy_nation", "shek_kingdom", -50);
                SetFactionRelation("holy_nation", "flotsam_ninjas", -100);
                SetFactionRelation("holy_nation", "tech_hunters", -20);

                // United Cities relations
                SetFactionRelation("united_cities", "anti_slavers", -100);
                SetFactionRelation("united_cities", "tech_hunters", 10);
                SetFactionRelation("united_cities", "dust_bandits", -40);

                // Shek Kingdom relations
                SetFactionRelation("shek_kingdom", "holy_nation", -50);
                SetFactionRelation("shek_kingdom", "united_cities", -10);
                SetFactionRelation("shek_kingdom", "crab_raiders", 20);

                // Everyone hates bandits and cannibals
                foreach (var faction in factions.Values.Where(f => f.Type == FactionType.Major || f.Type == FactionType.Minor))
                {
                    foreach (var bandit in factions.Values.Where(f => f.Type == FactionType.Bandit || f.Type == FactionType.Wildlife))
                    {
                        SetFactionRelation(faction.Id, bandit.Id, -50);
                    }
                }
            }

            private void CreateFaction(string id, string name, FactionType type, string description, string color)
            {
                if (!factions.ContainsKey(id))
                {
                    factions[id] = new Faction
                    {
                        Id = id,
                        Name = name,
                        Type = type,
                        Description = description,
                        Color = color
                    };
                }
            }

            private void SetFactionRelation(string faction1Id, string faction2Id, float relation)
            {
                if (factions.TryGetValue(faction1Id, out var faction1))
                {
                    faction1.FactionRelations[faction2Id] = relation;
                }

                if (factions.TryGetValue(faction2Id, out var faction2))
                {
                    faction2.FactionRelations[faction1Id] = relation;
                }
            }

            // Reputation management
            public void ModifyReputation(string factionId, string action, string targetFactionId = null)
            {
                if (!factions.ContainsKey(factionId))
                    return;

                if (!actionReputationValues.TryGetValue(action, out float baseChange))
                    return;

                float actualChange = baseChange;

                // Apply faction relation modifiers
                if (!string.IsNullOrEmpty(targetFactionId) && factions.ContainsKey(targetFactionId))
                {
                    float relation = GetFactionRelation(factionId, targetFactionId);
                    if (relation > 0)
                    {
                        // Positive action against ally = bonus
                        // Negative action against ally = extra penalty
                        actualChange *= baseChange > 0 ? 1.5f : 2.0f;
                    }
                    else if (relation < 0)
                    {
                        // Positive action against enemy = reduced gain
                        // Negative action against enemy = reduced penalty (or even gain)
                        if (baseChange > 0)
                            actualChange *= 0.5f;
                        else
                            actualChange *= -0.5f; // Attacking enemies can gain rep
                    }
                }

                // Apply the reputation change
                ChangeReputation(client.CurrentUsername, factionId, actualChange, action);

                // Ripple effects to allied/enemy factions
                ApplyRippleEffects(factionId, actualChange, action);
            }

            private void ChangeReputation(string playerId, string factionId, float change, string reason)
            {
                string key = GetReputationKey(playerId, factionId);

                if (!playerReputations.TryGetValue(key, out var reputation))
                {
                    reputation = new PlayerReputation
                    {
                        PlayerId = playerId,
                        FactionId = factionId
                    };
                    playerReputations[key] = reputation;
                }

                float oldRep = reputation.ReputationValue;
                ReputationLevel oldLevel = GetReputationLevel(oldRep);

                reputation.ReputationValue = Math.Clamp(reputation.ReputationValue + change, -100, 100);
                reputation.LastUpdated = DateTime.UtcNow;

                // Track action
                if (!reputation.ActionCounts.ContainsKey(reason))
                    reputation.ActionCounts[reason] = 0;
                reputation.ActionCounts[reason]++;

                // Add to history
                reputation.History.Add(new ReputationEvent
                {
                    EventType = reason,
                    ReputationChange = change,
                    Description = GetActionDescription(reason, change)
                });

                // Keep history manageable
                if (reputation.History.Count > 100)
                {
                    reputation.History = reputation.History.TakeLast(50).ToList();
                }

                float newRep = reputation.ReputationValue;
                ReputationLevel newLevel = GetReputationLevel(newRep);

                // Send update to server
                var message = new GameMessage
                {
                    Type = "reputation_update",
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                {
                    { "factionId", factionId },
                    { "reputation", newRep },
                    { "change", change },
                    { "reason", reason }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();

                // Raise events
                ReputationChanged?.Invoke(this, (factionId, oldRep, newRep));

                if (oldLevel != newLevel)
                {
                    ReputationLevelChanged?.Invoke(this, (factionId, oldLevel, newLevel));

                    // Notify player of significant changes
                    var faction = factions[factionId];
                    notificationManager?.CreateNotification(
                        change > 0 ? NotificationType.Success : NotificationType.Warning,
                        $"Reputation Changed: {faction.Name}",
                        $"You are now {GetReputationLevelName(newLevel)} with {faction.Name}",
                        priority: NotificationPriority.Normal
                    );

                    // Check for special statuses
                    if (newLevel == ReputationLevel.Nemesis)
                    {
                        notificationManager?.CreateNotification(
                            NotificationType.Warning,
                            "NEMESIS STATUS",
                            $"{faction.Name} will now attack you on sight!",
                            priority: NotificationPriority.High
                        );
                    }
                    else if (newLevel == ReputationLevel.Allied)
                    {
                        notificationManager?.CreateNotification(
                            NotificationType.Achievement,
                            "Allied Status Achieved!",
                            $"You are now allied with {faction.Name}!",
                            priority: NotificationPriority.High
                        );
                    }
                }
            }

            private void ApplyRippleEffects(string factionId, float change, string action)
            {
                if (!factions.TryGetValue(factionId, out var faction))
                    return;

                // Apply smaller changes to allied/enemy factions
                foreach (var relation in faction.FactionRelations)
                {
                    float relationValue = relation.Value;
                    float rippleChange = 0;

                    if (relationValue > 25) // Allied faction
                    {
                        rippleChange = change * 0.25f; // 25% of original change
                    }
                    else if (relationValue < -25) // Enemy faction
                    {
                        rippleChange = change * -0.25f; // Opposite effect
                    }

                    if (Math.Abs(rippleChange) > 0.1f)
                    {
                        ChangeReputation(client.CurrentUsername, relation.Key, rippleChange, $"ripple_from_{action}");
                    }
                }
            }

            // Player faction creation
            public Faction CreatePlayerFaction(string name, string description, Position headquarters)
            {
                string factionId = $"player_{Guid.NewGuid().ToString().Substring(0, 8)}";

                var faction = new Faction
                {
                    Id = factionId,
                    Name = name,
                    Type = FactionType.Player,
                    Description = description,
                    Headquarters = headquarters,
                    IsPlayerFaction = true,
                    FounderId = client.CurrentUsername,
                    FoundedDate = DateTime.UtcNow,
                    MemberCount = 1,
                    Color = GenerateRandomColor()
                };

                // Set default ranks
                faction.Ranks = new Dictionary<string, string>
            {
                { "0", "Recruit" },
                { "10", "Member" },
                { "25", "Veteran" },
                { "50", "Officer" },
                { "75", "Commander" },
                { "100", "Leader" }
            };

                factions[factionId] = faction;

                // Send to server
                var message = new GameMessage
                {
                    Type = MessageType.FactionCreate,
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                {
                    { "faction", JsonSerializer.Serialize(faction) }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();
                FactionCreated?.Invoke(this, faction);

                notificationManager?.NotifyAchievement(
                    "Faction Created!",
                    $"You have founded {name}!"
                );

                return faction;
            }

            // War system
            public bool DeclareWar(string aggressorFactionId, string defenderFactionId)
            {
                // Check if war already exists
                if (activeWars.Any(w => w.IsActive &&
                    ((w.AggressorFactionId == aggressorFactionId && w.DefenderFactionId == defenderFactionId) ||
                     (w.AggressorFactionId == defenderFactionId && w.DefenderFactionId == aggressorFactionId))))
                {
                    return false;
                }

                var war = new FactionWar
                {
                    AggressorFactionId = aggressorFactionId,
                    DefenderFactionId = defenderFactionId,
                    StartDate = DateTime.UtcNow
                };

                activeWars.Add(war);

                // Set faction relations to hostile
                SetFactionRelation(aggressorFactionId, defenderFactionId, -100);

                SaveData();
                WarDeclared?.Invoke(this, war);

                if (factions.TryGetValue(aggressorFactionId, out var aggressor) &&
                    factions.TryGetValue(defenderFactionId, out var defender))
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Warning,
                        "WAR DECLARED!",
                        $"{aggressor.Name} has declared war on {defender.Name}!",
                        priority: NotificationPriority.Critical
                    );
                }

                return true;
            }

            // Getters
            public float GetReputation(string factionId)
            {
                string key = GetReputationKey(client.CurrentUsername, factionId);
                return playerReputations.TryGetValue(key, out var rep) ? rep.ReputationValue : 0;
            }

            public ReputationLevel GetReputationLevel(float reputation)
            {
                if (reputation <= -75) return ReputationLevel.Nemesis;
                if (reputation <= -40) return ReputationLevel.Hostile;
                if (reputation <= -10) return ReputationLevel.Unfriendly;
                if (reputation < 10) return ReputationLevel.Neutral;
                if (reputation < 40) return ReputationLevel.Friendly;
                if (reputation < 75) return ReputationLevel.Allied;
                return ReputationLevel.Honored;
            }

            public string GetReputationLevelName(ReputationLevel level)
            {
                return level switch
                {
                    ReputationLevel.Nemesis => "Nemesis",
                    ReputationLevel.Hostile => "Hostile",
                    ReputationLevel.Unfriendly => "Unfriendly",
                    ReputationLevel.Neutral => "Neutral",
                    ReputationLevel.Friendly => "Friendly",
                    ReputationLevel.Allied => "Allied",
                    ReputationLevel.Honored => "Honored",
                    _ => "Unknown"
                };
            }

            public List<Faction> GetAllFactions()
            {
                return factions.Values.ToList();
            }

            public List<Faction> GetHostileFactions()
            {
                return factions.Values
                    .Where(f => GetReputation(f.Id) <= -40)
                    .ToList();
            }

            public List<Faction> GetAlliedFactions()
            {
                return factions.Values
                    .Where(f => GetReputation(f.Id) >= 40)
                    .ToList();
            }

            public float GetFactionRelation(string faction1Id, string faction2Id)
            {
                if (factions.TryGetValue(faction1Id, out var faction))
                {
                    return faction.FactionRelations.GetValueOrDefault(faction2Id, 0);
                }
                return 0;
            }

            public List<FactionWar> GetActiveWars()
            {
                return activeWars.Where(w => w.IsActive).ToList();
            }

            // Helper methods
            private string GetReputationKey(string playerId, string factionId)
            {
                return $"{playerId}:{factionId}";
            }

            private string GetActionDescription(string action, float change)
            {
                return action switch
                {
                    "kill_member" => $"Killed faction member ({change:+0.0})",
                    "kill_leader" => $"Killed faction leader ({change:+0.0})",
                    "rescue_member" => $"Rescued faction member ({change:+0.0})",
                    "complete_quest" => $"Completed faction quest ({change:+0.0})",
                    "trade" => $"Traded with faction ({change:+0.0})",
                    "defend_town" => $"Defended faction town ({change:+0.0})",
                    _ => $"{action} ({change:+0.0})"
                };
            }

            private string GenerateRandomColor()
            {
                Random rand = new Random();
                return $"#{rand.Next(0x1000000):X6}";
            }

            private void ApplyReputationDecay()
            {
                // Slowly decay reputation towards neutral over time
                foreach (var rep in playerReputations.Values)
                {
                    if (Math.Abs(rep.ReputationValue) > 5)
                    {
                        float decay = rep.ReputationValue > 0 ? -0.5f : 0.5f;
                        rep.ReputationValue = Math.Max(-100, Math.Min(100, rep.ReputationValue + decay));
                    }
                }

                SaveData();
            }

            // Message handlers
            private void OnMessageReceived(object sender, GameMessage message)
            {
                switch (message.Type)
                {
                    case "reputation_broadcast":
                        HandleReputationBroadcast(message);
                        break;
                    case "faction_created":
                        HandleFactionCreated(message);
                        break;
                    case "war_declared":
                        HandleWarDeclared(message);
                        break;
                }
            }

            private void HandleReputationBroadcast(GameMessage message)
            {
                // Handle reputation changes from other players that might affect us
                if (message.Data.TryGetValue("playerId", out var playerIdObj) &&
                    message.Data.TryGetValue("factionId", out var factionIdObj) &&
                    message.Data.TryGetValue("change", out var changeObj))
                {
                    string playerId = playerIdObj.ToString();
                    string factionId = factionIdObj.ToString();
                    float change = Convert.ToSingle(changeObj);

                    // If it's a player faction we're part of, we might care
                    if (factions.TryGetValue(factionId, out var faction) && faction.IsPlayerFaction)
                    {
                        // Notify about significant reputation changes
                        if (Math.Abs(change) >= 10)
                        {
                            notificationManager?.CreateNotification(
                                NotificationType.Info,
                                "Faction Reputation",
                                $"{playerId} changed {faction.Name} reputation by {change:+0.0}",
                                priority: NotificationPriority.Low
                            );
                        }
                    }
                }
            }

            private void HandleFactionCreated(GameMessage message)
            {
                if (message.Data.TryGetValue("faction", out var factionObj))
                {
                    var faction = JsonSerializer.Deserialize<Faction>(factionObj.ToString());
                    factions[faction.Id] = faction;

                    if (faction.FounderId != client.CurrentUsername)
                    {
                        notificationManager?.CreateNotification(
                            NotificationType.Info,
                            "New Faction",
                            $"{faction.FounderId} has created {faction.Name}",
                            priority: NotificationPriority.Low
                        );
                    }
                }
            }

            private void HandleWarDeclared(GameMessage message)
            {
                if (message.Data.TryGetValue("war", out var warObj))
                {
                    var war = JsonSerializer.Deserialize<FactionWar>(warObj.ToString());
                    activeWars.Add(war);

                    if (factions.TryGetValue(war.AggressorFactionId, out var aggressor) &&
                        factions.TryGetValue(war.DefenderFactionId, out var defender))
                    {
                        notificationManager?.CreateNotification(
                            NotificationType.Warning,
                            "War Declared",
                            $"{aggressor.Name} has declared war on {defender.Name}",
                            priority: NotificationPriority.High
                        );
                    }
                }
            }
        }

        public class ReputationData
        {
            public List<Faction> Factions { get; set; } = new List<Faction>();
            public List<PlayerReputation> PlayerReputations { get; set; } = new List<PlayerReputation>();
            public List<FactionWar> ActiveWars { get; set; } = new List<FactionWar>();
        }
    }
}
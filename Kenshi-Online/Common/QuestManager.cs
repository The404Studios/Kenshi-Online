using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common.KenshiMultiplayer;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common.QuestManager
{
   public enum QuestType
    {
        Delivery,
        Escort,
        Kill,
        Collect,
        Rescue,
        Explore,
        Build,
        Craft,
        Trade,
        Diplomacy,
        Survival,
        Training
    }

    public enum QuestStatus
    {
        Available,
        Active,
        Completed,
        Failed,
        Abandoned,
        Turned_In
    }

    public enum QuestDifficulty
    {
        Easy,
        Normal,
        Hard,
        Extreme,
        Legendary
    }

    public class QuestObjective
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Description { get; set; }
        public bool IsCompleted { get; set; } = false;
        public bool IsOptional { get; set; } = false;
        public Dictionary<string, object> Requirements { get; set; } = new Dictionary<string, object>();
        public Dictionary<string, object> Progress { get; set; } = new Dictionary<string, object>();
        public List<string> CompletedBy { get; set; } = new List<string>();
    }

    public class QuestReward
    {
        public string Type { get; set; } // "item", "currency", "reputation", "experience", "skill"
        public string Id { get; set; }
        public string Name { get; set; }
        public int Amount { get; set; }
        public float Chance { get; set; } = 1.0f; // For random rewards
        public Dictionary<string, object> Properties { get; set; } = new Dictionary<string, object>();
    }

    public class Quest
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public string Description { get; set; }
        public QuestType Type { get; set; }
        public QuestStatus Status { get; set; } = QuestStatus.Available;
        public QuestDifficulty Difficulty { get; set; } = QuestDifficulty.Normal;

        // Quest giver
        public string GiverId { get; set; } // NPC or player ID
        public string GiverName { get; set; }
        public string GiverFaction { get; set; }
        public Position GiverLocation { get; set; }

        // Requirements
        public int MinLevel { get; set; } = 1;
        public List<string> RequiredQuests { get; set; } = new List<string>();
        public Dictionary<string, float> RequiredSkills { get; set; } = new Dictionary<string, float>();
        public Dictionary<string, float> RequiredReputation { get; set; } = new Dictionary<string, float>();

        // Objectives
        public List<QuestObjective> Objectives { get; set; } = new List<QuestObjective>();
        public bool RequireAllObjectives { get; set; } = true;

        // Rewards
        public List<QuestReward> Rewards { get; set; } = new List<QuestReward>();
        public int ExperienceReward { get; set; }
        public int CurrencyReward { get; set; }

        // Time limits
        public DateTime? TimeLimit { get; set; }
        public TimeSpan? Duration { get; set; }

        // Sharing
        public bool IsShareable { get; set; } = true;
        public int MaxParticipants { get; set; } = 1;
        public List<string> Participants { get; set; } = new List<string>();
        public string SharedBy { get; set; }

        // Progress
        public DateTime AcceptedAt { get; set; }
        public DateTime? CompletedAt { get; set; }
        public Dictionary<string, object> QuestData { get; set; } = new Dictionary<string, object>();

        // Story
        public List<string> DialogueLines { get; set; } = new List<string>();
        public string CompletionText { get; set; }
        public string FailureText { get; set; }
    }

    public class QuestChain
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public string Description { get; set; }
        public List<string> QuestIds { get; set; } = new List<string>();
        public int CurrentQuestIndex { get; set; } = 0;
        public bool IsCompleted { get; set; } = false;
        public Dictionary<string, object> ChainData { get; set; } = new Dictionary<string, object>();
    }

    public class DailyQuest
    {
        public string QuestId { get; set; }
        public DateTime Date { get; set; }
        public bool IsCompleted { get; set; }
        public int Streak { get; set; }
    }

    public class QuestManager
    {
        private readonly Dictionary<string, Quest> availableQuests = new Dictionary<string, Quest>();
        private readonly Dictionary<string, List<Quest>> playerQuests = new Dictionary<string, List<Quest>>();
        private readonly Dictionary<string, List<Quest>> completedQuests = new Dictionary<string, List<Quest>>();
        private readonly Dictionary<string, QuestChain> questChains = new Dictionary<string, QuestChain>();
        private readonly Dictionary<string, List<DailyQuest>> dailyQuests = new Dictionary<string, List<DailyQuest>>();

        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly NotificationManager notificationManager;
        private readonly ReputationManager reputationManager;

        // Events
        public event EventHandler<Quest> QuestAccepted;
        public event EventHandler<Quest> QuestCompleted;
        public event EventHandler<Quest> QuestFailed;
        public event EventHandler<Quest> QuestShared;
        public event EventHandler<(Quest, QuestObjective)> ObjectiveCompleted;
        public event EventHandler<QuestReward> RewardReceived;

        public QuestManager(EnhancedClient clientInstance, NotificationManager notificationManager,
            ReputationManager reputationManager, string dataDirectory = "data")
        {
            this.client = clientInstance;
            this.notificationManager = notificationManager;
            this.reputationManager = reputationManager;
            dataFilePath = Path.Combine(dataDirectory, "quests.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData(GetPlayerQuests(), CompletedQuests, DailyQuests);
            InitializeQuests();

            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }

            // Start quest update timer
            System.Threading.Tasks.Task.Run(async () => {
                while (true)
                {
                    await System.Threading.Tasks.Task.Delay(TimeSpan.FromSeconds(10));
                    UpdateQuestProgress();
                    CheckQuestTimers();
                }
            });

            // Daily quest reset
            System.Threading.Tasks.Task.Run(async () => {
                while (true)
                {
                    await System.Threading.Tasks.Task.Delay(TimeSpan.FromHours(1));
                    CheckDailyReset();
                }
            });
        }

        private Dictionary<string, List<Quest>> GetPlayerQuests()
        {
            return playerQuests;
        }

        private Dictionary<string, List<Quest>> CompletedQuests => completedQuests;

        private Dictionary<string, List<DailyQuest>> DailyQuests => dailyQuests;

        private void LoadData(Dictionary<string, List<Quest>> playerQuests, Dictionary<string, List<Quest>> completedQuests, Dictionary<string, List<DailyQuest>> dailyQuests)
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<QuestData>(json);

                    if (data != null)
                    {
                        foreach (var quest in data.AvailableQuests)
                        {
                            availableQuests[quest.Id] = quest;
                        }

                        playerQuests = data.PlayerQuests ?? new Dictionary<string, List<Quest>>();
                        completedQuests = data.CompletedQuests ?? new Dictionary<string, List<Quest>>();

                        foreach (var chain in data.QuestChains ?? new List<QuestChain>())
                        {
                            questChains[chain.Id] = chain;
                        }

                        dailyQuests = data.DailyQuests ?? new Dictionary<string, List<DailyQuest>>();
                    }

                    Logger.Log($"Loaded {availableQuests.Count} available quests");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading quest data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new QuestData
                {
                    AvailableQuests = availableQuests.Values.ToList(),
                    PlayerQuests = playerQuests,
                    CompletedQuests = completedQuests,
                    QuestChains = questChains.Values.ToList(),
                    DailyQuests = dailyQuests
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving quest data: {ex.Message}");
            }
        }

        private void InitializeQuests()
        {
            // Initialize some default quests if none exist
            if (availableQuests.Count == 0)
            {
                // Starter quest
                CreateQuest(
                    "Welcome to the Wastes",
                    "Learn the basics of survival in Kenshi",
                    QuestType.Training,
                    new List<QuestObjective>
                    {
                        new QuestObjective { Description = "Mine 10 copper ore" },
                        new QuestObjective { Description = "Craft or buy food" },
                        new QuestObjective { Description = "Build a storage box" }
                    },
                    new List<QuestReward>
                    {
                        new QuestReward { Type = "currency", Name = "Cats", Amount = 1000 },
                        new QuestReward { Type = "item", Name = "First Aid Kit", Amount = 3 }
                    }
                );

                // Faction quest
                CreateQuest(
                    "Proving Your Worth",
                    "Help the Shek Kingdom defend against bandits",
                    QuestType.Kill,
                    new List<QuestObjective>
                    {
                        new QuestObjective { Description = "Kill 10 Dust Bandits" },
                        new QuestObjective { Description = "Return to Squin", IsOptional = true }
                    },
                    new List<QuestReward>
                    {
                        new QuestReward { Type = "reputation", Id = "shek_kingdom", Name = "Shek Kingdom", Amount = 10 },
                        new QuestReward { Type = "currency", Name = "Cats", Amount = 5000 }
                    },
                    difficulty: QuestDifficulty.Normal,
                    giverFaction: "shek_kingdom"
                );

                // Trade quest
                CreateQuest(
                    "The Merchant's Request",
                    "Deliver goods to a distant town",
                    QuestType.Delivery,
                    new List<QuestObjective>
                    {
                        new QuestObjective { Description = "Obtain 20 units of Cactus Rum" },
                        new QuestObjective { Description = "Deliver to Heft" },
                        new QuestObjective { Description = "Return with payment" }
                    },
                    new List<QuestReward>
                    {
                        new QuestReward { Type = "currency", Name = "Cats", Amount = 10000 },
                        new QuestReward { Type = "reputation", Id = "united_cities", Name = "United Cities", Amount = 5 }
                    }
                );
            }
        }

        private void CreateQuest(string name, string description, QuestType type,
            List<QuestObjective> objectives, List<QuestReward> rewards,
            QuestDifficulty difficulty = QuestDifficulty.Normal, string giverFaction = null)
        {
            var quest = new Quest
            {
                Name = name,
                Description = description,
                Type = type,
                Objectives = objectives,
                Rewards = rewards,
                Difficulty = difficulty,
                GiverFaction = giverFaction ?? "neutral",
                ExperienceReward = 100 * (int)difficulty,
                CurrencyReward = 1000 * (int)difficulty
            };

            availableQuests[quest.Id] = quest;
        }

        // Quest management
        public bool AcceptQuest(string questId)
        {
            if (!availableQuests.TryGetValue(questId, out var quest))
                return false;

            if (quest.Status != QuestStatus.Available)
                return false;

            // Check requirements
            if (!MeetsQuestRequirements(quest))
            {
                notificationManager?.CreateNotification(
                    NotificationType.Warning,
                    "Requirements Not Met",
                    "You don't meet the requirements for this quest"
                );
                return false;
            }

            // Initialize player quest list if needed
            if (!playerQuests.TryGetValue(client.CurrentUsername, out var quests))
            {
                quests = new List<Quest>();
                playerQuests[client.CurrentUsername] = quests;
            }

            // Clone the quest for the player
            var playerQuest = JsonSerializer.Deserialize<Quest>(JsonSerializer.Serialize(quest));
            playerQuest.Status = QuestStatus.Active;
            playerQuest.AcceptedAt = DateTime.UtcNow;
            playerQuest.Participants.Add(client.CurrentUsername);

            quests.Add(playerQuest);

            // Send to server
            var message = new GameMessage
            {
                Type = Auth.MessageType.QuestAccept,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "questId", questId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            QuestAccepted?.Invoke(this, playerQuest);

            notificationManager?.CreateNotification(
                NotificationType.Info,
                "Quest Accepted",
                $"Started: {playerQuest.Name}",
                priority: NotificationPriority.Normal
            );

            return true;
        }

        public bool ShareQuest(string questId, List<string> playerIds)
        {
            var quest = GetActiveQuest(questId);
            if (quest == null || !quest.IsShareable)
                return false;

            if (quest.Participants.Count + playerIds.Count > quest.MaxParticipants)
            {
                notificationManager?.CreateNotification(
                    NotificationType.Warning,
                    "Quest Full",
                    $"This quest can only have {quest.MaxParticipants} participants"
                );
                return false;
            }

            quest.SharedBy = client.CurrentUsername;

            // Send to server
            var message = new GameMessage
            {
                Type = Auth.MessageType.QuestOffer,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "questId", questId },
                    { "playerIds", playerIds }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            QuestShared?.Invoke(this, quest);

            return true;
        }

        public bool UpdateObjectiveProgress(string questId, string objectiveId, Dictionary<string, object> progress)
        {
            var quest = GetActiveQuest(questId);
            if (quest == null)
                return false;

            var objective = quest.Objectives.FirstOrDefault(o => o.Id == objectiveId);
            if (objective == null || objective.IsCompleted)
                return false;

            // Update progress
            foreach (var kvp in progress)
            {
                objective.Progress[kvp.Key] = kvp.Value;
            }

            // Check if objective is completed
            if (CheckObjectiveCompletion(objective))
            {
                objective.IsCompleted = true;
                objective.CompletedBy.Add(client.CurrentUsername);

                ObjectiveCompleted?.Invoke(this, (quest, objective));

                notificationManager?.CreateNotification(
                    NotificationType.Success,
                    "Objective Complete",
                    objective.Description,
                    priority: NotificationPriority.Normal
                );

                // Check if quest is completed
                CheckQuestCompletion(quest);
            }

            // Send to server
            var message = new GameMessage
            {
                Type = Auth.MessageType.QuestUpdate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "questId", questId },
                    { "objectiveId", objectiveId },
                    { "progress", progress }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();

            return true;
        }

        public bool CompleteQuest(string questId)
        {
            var quest = GetActiveQuest(questId);
            if (quest == null || quest.Status != QuestStatus.Active)
                return false;

            // Check if all required objectives are completed
            bool allCompleted = quest.RequireAllObjectives
                ? quest.Objectives.Where(o => !o.IsOptional).All(o => o.IsCompleted)
                : quest.Objectives.Any(o => o.IsCompleted && !o.IsOptional);

            if (!allCompleted)
            {
                notificationManager?.CreateNotification(
                    NotificationType.Warning,
                    "Quest Incomplete",
                    "Not all objectives have been completed"
                );
                return false;
            }

            quest.Status = QuestStatus.Completed;
            quest.CompletedAt = DateTime.UtcNow;

            // Grant rewards
            GrantQuestRewards(quest);

            // Move to completed quests
            var activeQuests = playerQuests[client.CurrentUsername];
            activeQuests.Remove(quest);

            if (!completedQuests.ContainsKey(client.CurrentUsername))
                completedQuests[client.CurrentUsername] = new List<Quest>();

            completedQuests[client.CurrentUsername].Add(quest);

            // Send to server
            var message = new GameMessage
            {
                Type = Auth.MessageType.QuestComplete,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "questId", questId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            QuestCompleted?.Invoke(this, quest);

            notificationManager?.CreateNotification(
                NotificationType.Success,
                "Quest Completed!",
                quest.Name,
                priority: NotificationPriority.High
            );

            // Check for quest chain progression
            CheckQuestChainProgression(questId);

            return true;
        }

        public bool AbandonQuest(string questId)
        {
            var quest = GetActiveQuest(questId);
            if (quest == null)
                return false;

            quest.Status = QuestStatus.Abandoned;

            var activeQuests = playerQuests[client.CurrentUsername];
            activeQuests.Remove(quest);

            // Send to server
            var message = new GameMessage
            {
                Type = Auth.MessageType.QuestDecline,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "questId", questId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();

            return true;
        }

        // Daily quests
        public List<Quest> GetDailyQuests()
        {
            var today = DateTime.UtcNow.Date;

            if (!dailyQuests.TryGetValue(client.CurrentUsername, out var playerDailies))
            {
                playerDailies = new List<DailyQuest>();
                dailyQuests[client.CurrentUsername] = playerDailies;
            }

            // Check if we need to generate new dailies
            var todaysDailies = playerDailies.Where(d => d.Date.Date == today).ToList();

            if (todaysDailies.Count < 3) // Generate 3 daily quests
            {
                GenerateDailyQuests();
                todaysDailies = playerDailies.Where(d => d.Date.Date == today).ToList();
            }

            // Return the actual quest objects
            var quests = new List<Quest>();
            foreach (var daily in todaysDailies)
            {
                if (availableQuests.TryGetValue(daily.QuestId, out var quest))
                {
                    quests.Add(quest);
                }
            }

            return quests;
        }

        private void GenerateDailyQuests()
        {
            var today = DateTime.UtcNow.Date;
            var playerDailies = dailyQuests[client.CurrentUsername];

            // Remove old dailies
            playerDailies.RemoveAll(d => d.Date.Date < today);

            // Types of daily quests to generate
            var dailyTypes = new[] { QuestType.Kill, QuestType.Collect, QuestType.Delivery };

            foreach (var type in dailyTypes)
            {
                var quest = CreateDailyQuest(type);
                availableQuests[quest.Id] = quest;

                playerDailies.Add(new DailyQuest
                {
                    QuestId = quest.Id,
                    Date = today,
                    IsCompleted = false
                });
            }

            SaveData();
        }

        private Quest CreateDailyQuest(QuestType type)
        {
            var quest = new Quest
            {
                Name = $"Daily {type} Quest",
                Type = type,
                Difficulty = QuestDifficulty.Normal,
                TimeLimit = DateTime.UtcNow.Date.AddDays(1),
                IsShareable = false,
                ExperienceReward = 500,
                CurrencyReward = 2000
            };

            switch (type)
            {
                case QuestType.Kill:
                    quest.Name = "Daily Bounty";
                    quest.Description = "Eliminate threats to the region";
                    quest.Objectives.Add(new QuestObjective
                    {
                        Description = "Kill 20 bandits",
                        Requirements = new Dictionary<string, object> { { "enemyType", "bandit" }, { "count", 20 } }
                    });
                    break;

                case QuestType.Collect:
                    quest.Name = "Daily Gathering";
                    quest.Description = "Gather resources for the community";
                    quest.Objectives.Add(new QuestObjective
                    {
                        Description = "Collect 50 units of ore",
                        Requirements = new Dictionary<string, object> { { "resourceType", "ore" }, { "count", 50 } }
                    });
                    break;

                case QuestType.Delivery:
                    quest.Name = "Daily Delivery";
                    quest.Description = "Help with local trade routes";
                    quest.Objectives.Add(new QuestObjective
                    {
                        Description = "Deliver goods to any town",
                        Requirements = new Dictionary<string, object> { { "deliveryCount", 1 } }
                    });
                    break;
            }

            quest.Rewards.Add(new QuestReward
            {
                Type = "item",
                Name = "Daily Reward Crate",
                Amount = 1
            });

            return quest;
        }

        // Helper methods
        private bool MeetsQuestRequirements(Quest quest)
        {
            // Check level
            // In real implementation, get player level from player data
            int playerLevel = 10;
            if (playerLevel < quest.MinLevel)
                return false;

            // Check required quests
            if (completedQuests.TryGetValue(client.CurrentUsername, out var completed))
            {
                foreach (var requiredQuestId in quest.RequiredQuests)
                {
                    if (!completed.Any(q => q.Id == requiredQuestId))
                        return false;
                }
            }

            // Check reputation requirements
            foreach (var repReq in quest.RequiredReputation)
            {
                float currentRep = reputationManager.GetReputation(repReq.Key);
                if (currentRep < repReq.Value)
                    return false;
            }

            return true;
        }

        private bool CheckObjectiveCompletion(QuestObjective objective)
        {
            foreach (var requirement in objective.Requirements)
            {
                if (!objective.Progress.TryGetValue(requirement.Key, out var progressValue))
                    return false;

                // Simple comparison for now
                if (requirement.Value is int reqInt && progressValue is int progInt)
                {
                    if (progInt < reqInt)
                        return false;
                }
            }

            return true;
        }

        private void CheckQuestCompletion(Quest quest)
        {
            bool allCompleted = quest.RequireAllObjectives
                ? quest.Objectives.Where(o => !o.IsOptional).All(o => o.IsCompleted)
                : quest.Objectives.Any(o => o.IsCompleted && !o.IsOptional);

            if (allCompleted)
            {
                CompleteQuest(quest.Id);
            }
        }

        private void GrantQuestRewards(Quest quest)
        {
            foreach (var reward in quest.Rewards)
            {
                // Check if reward should be granted (for random rewards)
                if (reward.Chance < 1.0f)
                {
                    Random rand = new Random();
                    if (rand.NextDouble() > reward.Chance)
                        continue;
                }

                switch (reward.Type)
                {
                    case "currency":
                        // Add currency to player
                        notificationManager?.NotifyItemReceived($"{reward.Amount} {reward.Name}", 1);
                        break;

                    case "item":
                        // Add item to inventory
                        notificationManager?.NotifyItemReceived(reward.Name, reward.Amount);
                        break;

                    case "reputation":
                        // Modify faction reputation
                        for (int i = 0; i < reward.Amount; i++)
                        {
                            reputationManager.ModifyReputation(reward.Id, "complete_quest");
                        }
                        break;

                    case "experience":
                        // Add experience
                        notificationManager?.CreateNotification(
                            NotificationType.Success,
                            "Experience Gained",
                            $"+{reward.Amount} XP"
                        );
                        break;
                }

                RewardReceived?.Invoke(this, reward);
            }

            // Grant base rewards
            if (quest.ExperienceReward > 0)
            {
                notificationManager?.CreateNotification(
                    NotificationType.Success,
                    "Experience Gained",
                    $"+{quest.ExperienceReward} XP"
                );
            }

            if (quest.CurrencyReward > 0)
            {
                notificationManager?.NotifyItemReceived($"{quest.CurrencyReward} Cats", 1);
            }
        }

        private void CheckQuestChainProgression(string completedQuestId)
        {
            foreach (var chain in questChains.Values)
            {
                if (chain.IsCompleted)
                    continue;

                int index = chain.QuestIds.IndexOf(completedQuestId);
                if (index == chain.CurrentQuestIndex)
                {
                    chain.CurrentQuestIndex++;

                    if (chain.CurrentQuestIndex >= chain.QuestIds.Count)
                    {
                        chain.IsCompleted = true;
                        notificationManager?.NotifyAchievement(
                            "Quest Chain Completed!",
                            chain.Name
                        );
                    }
                    else
                    {
                        // Unlock next quest in chain
                        string nextQuestId = chain.QuestIds[chain.CurrentQuestIndex];
                        if (availableQuests.TryGetValue(nextQuestId, out var nextQuest))
                        {
                            nextQuest.Status = QuestStatus.Available;
                            notificationManager?.CreateNotification(
                                NotificationType.Info,
                                "New Quest Available",
                                nextQuest.Name
                            );
                        }
                    }

                    SaveData();
                    break;
                }
            }
        }

        private void UpdateQuestProgress()
        {
            // This would be called to update quest progress based on game events
            // For example, tracking kills, items collected, etc.
        }

        private void CheckQuestTimers()
        {
            if (!playerQuests.TryGetValue(client.CurrentUsername, out var quests))
                return;

            var now = DateTime.UtcNow;
            var timedOutQuests = new List<Quest>();

            foreach (var quest in quests)
            {
                if (quest.TimeLimit.HasValue && quest.TimeLimit.Value < now)
                {
                    timedOutQuests.Add(quest);
                }
            }

            foreach (var quest in timedOutQuests)
            {
                quest.Status = QuestStatus.Failed;
                quests.Remove(quest);

                QuestFailed?.Invoke(this, quest);

                notificationManager?.CreateNotification(
                    NotificationType.Error,
                    "Quest Failed",
                    $"{quest.Name} - Time limit exceeded",
                    priority: NotificationPriority.High
                );
            }

            if (timedOutQuests.Count > 0)
            {
                SaveData();
            }
        }

        private void CheckDailyReset()
        {
            var now = DateTime.UtcNow;

            // Check if it's past midnight UTC
            if (now.Hour == 0)
            {
                // Reset daily quests
                foreach (var playerDailies in dailyQuests.Values)
                {
                    playerDailies.RemoveAll(d => d.Date.Date < now.Date);
                }

                SaveData();

                notificationManager?.CreateNotification(
                    NotificationType.Info,
                    "Daily Reset",
                    "New daily quests are available!"
                );
            }
        }

        // Getters
        public List<Quest> GetAvailableQuests()
        {
            return availableQuests.Values
                .Where(q => q.Status == QuestStatus.Available && MeetsQuestRequirements(q))
                .ToList();
        }

        public List<Quest> GetActiveQuests()
        {
            if (playerQuests.TryGetValue(client.CurrentUsername, out var quests))
            {
                return quests.Where(q => q.Status == QuestStatus.Active).ToList();
            }
            return new List<Quest>();
        }

        public Quest GetActiveQuest(string questId)
        {
            if (playerQuests.TryGetValue(client.CurrentUsername, out var quests))
            {
                return quests.FirstOrDefault(q => q.Id == questId && q.Status == QuestStatus.Active);
            }
            return null;
        }

        public List<Quest> GetCompletedQuests()
        {
            if (completedQuests.TryGetValue(client.CurrentUsername, out var quests))
            {
                return quests;
            }
            return new List<Quest>();
        }

        public int GetDailyStreak()
        {
            if (!dailyQuests.TryGetValue(client.CurrentUsername, out var playerDailies))
                return 0;

            // Calculate consecutive days of completed dailies
            int streak = 0;
            var date = DateTime.UtcNow.Date;

            while (true)
            {
                var dailiesForDate = playerDailies.Where(d => d.Date.Date == date && d.IsCompleted).ToList();
                if (dailiesForDate.Count >= 3) // All 3 dailies completed
                {
                    streak++;
                    date = date.AddDays(-1);
                }
                else
                {
                    break;
                }
            }

            return streak;
        }

        // Message handlers
        private void OnMessageReceived(object sender, GameMessage message)
        {
            switch (message.Type)
            {
                case Auth.MessageType.QuestOffer:
                    HandleQuestOffer(message);
                    break;
                case Auth.MessageType.QuestUpdate:
                    HandleQuestUpdate(message);
                    break;
                case Auth.MessageType.QuestComplete:
                    HandleQuestComplete(message);
                    break;
            }
        }

        private void HandleQuestOffer(GameMessage message)
        {
            if (message.Data.TryGetValue("questId", out var questIdObj) &&
                message.Data.TryGetValue("sharedBy", out var sharedByObj))
            {
                string questId = questIdObj.ToString();
                string sharedBy = sharedByObj.ToString();

                if (availableQuests.TryGetValue(questId, out var quest))
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Info,
                        "Quest Shared",
                        $"{sharedBy} wants to share '{quest.Name}' with you",
                        new List<NotificationAction>
                        {
                            new NotificationAction
                            {
                                Id = "accept",
                                Label = "Accept Quest",
                                ActionType = "quest_accept",
                                ActionData = new Dictionary<string, object> { { "questId", questId } }
                            },
                            new NotificationAction
                            {
                                Id = "decline",
                                Label = "Decline",
                                ActionType = "quest_decline"
                            }
                        }
                    );
                }
            }
        }

        private void HandleQuestUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("questId", out var questIdObj) &&
                message.Data.TryGetValue("playerId", out var playerIdObj))
            {
                string questId = questIdObj.ToString();
                string playerId = playerIdObj.ToString();

                // Update shared quest progress
                var quest = GetActiveQuest(questId);
                if (quest != null && quest.Participants.Contains(playerId))
                {
                    // Sync objective progress
                    if (message.Data.TryGetValue("objectives", out var objectivesObj))
                    {
                        var objectives = JsonSerializer.Deserialize<List<QuestObjective>>(objectivesObj.ToString());
                        quest.Objectives = objectives;
                    }
                }
            }
        }

        private void HandleQuestComplete(GameMessage message)
        {
            if (message.Data.TryGetValue("questId", out var questIdObj) &&
                message.Data.TryGetValue("completedBy", out var completedByObj))
            {
                string questId = questIdObj.ToString();
                string completedBy = completedByObj.ToString();

                var quest = GetActiveQuest(questId);
                if (quest != null && quest.Participants.Contains(completedBy))
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Success,
                        "Shared Quest Completed",
                        $"{completedBy} completed '{quest.Name}'"
                    );

                    // Grant rewards to all participants
                    if (quest.Participants.Contains(client.CurrentUsername))
                    {
                        GrantQuestRewards(quest);
                    }
                }
            }
        }
    }

    public class QuestData
    {
        public List<Quest> AvailableQuests { get; set; } = new List<Quest>();
        public Dictionary<string, List<Quest>> PlayerQuests { get; set; } = new Dictionary<string, List<Quest>>();
        public Dictionary<string, List<Quest>> CompletedQuests { get; set; } = new Dictionary<string, List<Quest>>();
        public List<QuestChain> QuestChains { get; set; } = new List<QuestChain>();
        public Dictionary<string, List<DailyQuest>> DailyQuests { get; set; } = new Dictionary<string, List<DailyQuest>>();
    }
}

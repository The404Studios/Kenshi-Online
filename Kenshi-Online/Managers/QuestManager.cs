using System;
using System.Collections.Generic;
using System.Linq;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages quest system for players
    /// </summary>
    public class QuestManager
    {
        private readonly Dictionary<string, QuestData> activeQuests;
        private readonly Dictionary<string, List<QuestData>> playerQuests;
        private readonly Dictionary<string, QuestData> questTemplates;
        private readonly NetworkManager? networkManager;

        public QuestManager(NetworkManager? networkManager = null)
        {
            this.networkManager = networkManager;
            activeQuests = new Dictionary<string, QuestData>();
            playerQuests = new Dictionary<string, List<QuestData>>();
            questTemplates = new Dictionary<string, QuestData>();

            InitializeQuestTemplates();
        }

        /// <summary>
        /// Initialize default quest templates
        /// </summary>
        private void InitializeQuestTemplates()
        {
            // Example quest templates - can be loaded from files
            questTemplates["tutorial_welcome"] = new QuestData
            {
                QuestId = "tutorial_welcome",
                QuestName = "Welcome to Kenshi",
                Description = "Learn the basics of survival in the harsh world of Kenshi.",
                GiverId = "system",
                Status = QuestStatus.NotStarted,
                Objectives = new List<QuestObjective>
                {
                    new QuestObjective
                    {
                        ObjectiveId = "obj1",
                        Description = "Move to the marked location",
                        Type = ObjectiveType.Explore,
                        RequiredCount = 1,
                        CurrentCount = 0
                    },
                    new QuestObjective
                    {
                        ObjectiveId = "obj2",
                        Description = "Collect 5 iron ore",
                        Type = ObjectiveType.Collect,
                        TargetId = "iron_ore",
                        RequiredCount = 5,
                        CurrentCount = 0
                    }
                },
                Rewards = new List<QuestReward>
                {
                    new QuestReward
                    {
                        Type = RewardType.Experience,
                        Experience = 100
                    },
                    new QuestReward
                    {
                        Type = RewardType.Money,
                        Amount = 250
                    }
                },
                RequiredLevel = 1
            };

            questTemplates["bounty_hunt"] = new QuestData
            {
                QuestId = "bounty_hunt",
                QuestName = "Bounty Hunter",
                Description = "Hunt down a dangerous bandit for a reward.",
                GiverId = "guard_captain",
                Status = QuestStatus.NotStarted,
                Objectives = new List<QuestObjective>
                {
                    new QuestObjective
                    {
                        ObjectiveId = "obj1",
                        Description = "Defeat the bandit leader",
                        Type = ObjectiveType.Kill,
                        TargetId = "bandit_leader",
                        RequiredCount = 1,
                        CurrentCount = 0
                    }
                },
                Rewards = new List<QuestReward>
                {
                    new QuestReward
                    {
                        Type = RewardType.Experience,
                        Experience = 500
                    },
                    new QuestReward
                    {
                        Type = RewardType.Money,
                        Amount = 1000
                    },
                    new QuestReward
                    {
                        Type = RewardType.Reputation,
                        Reputation = 10
                    }
                },
                RequiredLevel = 5
            };
        }

        /// <summary>
        /// Start a quest for a player
        /// </summary>
        public bool StartQuest(string playerId, string questTemplateId)
        {
            if (!questTemplates.TryGetValue(questTemplateId, out QuestData? template))
            {
                Console.WriteLine($"Quest template {questTemplateId} not found");
                return false;
            }

            // Check if player already has this quest
            if (HasQuest(playerId, questTemplateId))
            {
                Console.WriteLine($"Player {playerId} already has quest {questTemplateId}");
                return false;
            }

            // Create a new quest instance from template
            var quest = CloneQuest(template);
            quest.QuestId = $"{questTemplateId}_{playerId}_{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}";
            quest.Status = QuestStatus.Active;
            quest.StartTime = DateTime.UtcNow;

            // Add to player's quest list
            if (!playerQuests.ContainsKey(playerId))
            {
                playerQuests[playerId] = new List<QuestData>();
            }
            playerQuests[playerId].Add(quest);
            activeQuests[quest.QuestId] = quest;

            Console.WriteLine($"Player {playerId} started quest: {quest.QuestName}");

            // Notify player if network manager available
            NotifyQuestUpdate(playerId, quest, "started");

            return true;
        }

        /// <summary>
        /// Update quest progress
        /// </summary>
        public void UpdateQuestProgress(string playerId, string objectiveType, string targetId = "", int count = 1)
        {
            if (!playerQuests.TryGetValue(playerId, out List<QuestData>? quests))
                return;

            foreach (var quest in quests.Where(q => q.Status == QuestStatus.Active))
            {
                bool questUpdated = false;

                foreach (var objective in quest.Objectives.Where(o => !o.IsCompleted))
                {
                    // Check if this objective matches the update
                    if (objective.Type.ToString().ToLower() == objectiveType.ToLower() &&
                        (string.IsNullOrEmpty(objective.TargetId) || objective.TargetId == targetId))
                    {
                        objective.CurrentCount += count;
                        questUpdated = true;

                        // Check if objective is now complete
                        if (objective.CurrentCount >= objective.RequiredCount)
                        {
                            objective.IsCompleted = true;
                            Console.WriteLine($"Player {playerId} completed objective: {objective.Description}");
                            NotifyQuestUpdate(playerId, quest, "objective_completed");
                        }
                    }
                }

                // Check if all objectives are complete
                if (questUpdated && quest.Objectives.All(o => o.IsCompleted))
                {
                    CompleteQuest(playerId, quest.QuestId);
                }
                else if (questUpdated)
                {
                    NotifyQuestUpdate(playerId, quest, "progress");
                }
            }
        }

        /// <summary>
        /// Complete a quest and grant rewards
        /// </summary>
        public bool CompleteQuest(string playerId, string questId)
        {
            if (!activeQuests.TryGetValue(questId, out QuestData? quest))
            {
                Console.WriteLine($"Quest {questId} not found");
                return false;
            }

            // Verify all objectives are complete
            if (!quest.Objectives.All(o => o.IsCompleted))
            {
                Console.WriteLine($"Quest {questId} objectives not all completed");
                return false;
            }

            // Mark quest as completed
            quest.Status = QuestStatus.Completed;
            quest.CompletionTime = DateTime.UtcNow;

            Console.WriteLine($"Player {playerId} completed quest: {quest.QuestName}");

            // Grant rewards (this would integrate with player inventory/stats system)
            GrantRewards(playerId, quest.Rewards);

            // Notify player
            NotifyQuestUpdate(playerId, quest, "completed");

            return true;
        }

        /// <summary>
        /// Abandon a quest
        /// </summary>
        public bool AbandonQuest(string playerId, string questId)
        {
            if (!playerQuests.TryGetValue(playerId, out List<QuestData>? quests))
                return false;

            var quest = quests.FirstOrDefault(q => q.QuestId == questId);
            if (quest == null)
                return false;

            quest.Status = QuestStatus.Abandoned;
            quests.Remove(quest);
            activeQuests.Remove(questId);

            Console.WriteLine($"Player {playerId} abandoned quest: {quest.QuestName}");
            NotifyQuestUpdate(playerId, quest, "abandoned");

            return true;
        }

        /// <summary>
        /// Get all quests for a player
        /// </summary>
        public List<QuestData> GetPlayerQuests(string playerId, QuestStatus? filterStatus = null)
        {
            if (!playerQuests.TryGetValue(playerId, out List<QuestData>? quests))
                return new List<QuestData>();

            if (filterStatus.HasValue)
            {
                return quests.Where(q => q.Status == filterStatus.Value).ToList();
            }

            return new List<QuestData>(quests);
        }

        /// <summary>
        /// Check if player has a specific quest
        /// </summary>
        public bool HasQuest(string playerId, string questTemplateId)
        {
            if (!playerQuests.TryGetValue(playerId, out List<QuestData>? quests))
                return false;

            return quests.Any(q => q.QuestId.StartsWith(questTemplateId) &&
                                  (q.Status == QuestStatus.Active || q.Status == QuestStatus.NotStarted));
        }

        /// <summary>
        /// Get available quests for a player based on level and other requirements
        /// </summary>
        public List<QuestData> GetAvailableQuests(string playerId, int playerLevel)
        {
            var available = new List<QuestData>();

            foreach (var template in questTemplates.Values)
            {
                // Check level requirement
                if (template.RequiredLevel > playerLevel)
                    continue;

                // Check if player already has this quest
                if (HasQuest(playerId, template.QuestId))
                    continue;

                available.Add(template);
            }

            return available;
        }

        /// <summary>
        /// Grant quest rewards to player
        /// </summary>
        private void GrantRewards(string playerId, List<QuestReward> rewards)
        {
            foreach (var reward in rewards)
            {
                switch (reward.Type)
                {
                    case RewardType.Experience:
                        Console.WriteLine($"Granting {reward.Experience} experience to {playerId}");
                        // This would integrate with player experience system
                        break;

                    case RewardType.Money:
                        Console.WriteLine($"Granting {reward.Amount} money to {playerId}");
                        // This would integrate with player inventory/currency system
                        break;

                    case RewardType.Item:
                        Console.WriteLine($"Granting {reward.Amount}x {reward.ItemId} to {playerId}");
                        // This would integrate with player inventory system
                        break;

                    case RewardType.Reputation:
                        Console.WriteLine($"Granting {reward.Reputation} reputation to {playerId}");
                        // This would integrate with faction reputation system
                        break;

                    case RewardType.Skill:
                        Console.WriteLine($"Granting skill points to {playerId}");
                        // This would integrate with player skill system
                        break;
                }
            }
        }

        /// <summary>
        /// Notify player of quest updates
        /// </summary>
        private void NotifyQuestUpdate(string playerId, QuestData quest, string updateType)
        {
            if (networkManager == null)
                return;

            var message = new GameMessage
            {
                Type = MessageType.Quest,
                SenderId = "system",
                TargetId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "updateType", updateType },
                    { "questId", quest.QuestId },
                    { "questName", quest.QuestName },
                    { "status", quest.Status.ToString() },
                    { "objectives", quest.Objectives }
                }
            };

            try
            {
                networkManager.SendToPlayer(playerId, message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to send quest notification: {ex.Message}");
            }
        }

        /// <summary>
        /// Clone a quest template to create a new instance
        /// </summary>
        private QuestData CloneQuest(QuestData template)
        {
            return new QuestData
            {
                QuestId = template.QuestId,
                QuestName = template.QuestName,
                Description = template.Description,
                GiverId = template.GiverId,
                Status = template.Status,
                Objectives = template.Objectives.Select(o => new QuestObjective
                {
                    ObjectiveId = o.ObjectiveId,
                    Description = o.Description,
                    Type = o.Type,
                    TargetId = o.TargetId,
                    RequiredCount = o.RequiredCount,
                    CurrentCount = 0,
                    IsCompleted = false
                }).ToList(),
                Rewards = new List<QuestReward>(template.Rewards),
                RequiredLevel = template.RequiredLevel
            };
        }

        /// <summary>
        /// Register a custom quest template
        /// </summary>
        public void RegisterQuestTemplate(QuestData questTemplate)
        {
            questTemplates[questTemplate.QuestId] = questTemplate;
            Console.WriteLine($"Registered quest template: {questTemplate.QuestName}");
        }

        /// <summary>
        /// Get quest statistics
        /// </summary>
        public QuestStatistics GetStatistics(string playerId)
        {
            if (!playerQuests.TryGetValue(playerId, out List<QuestData>? quests))
            {
                return new QuestStatistics();
            }

            return new QuestStatistics
            {
                TotalQuests = quests.Count,
                ActiveQuests = quests.Count(q => q.Status == QuestStatus.Active),
                CompletedQuests = quests.Count(q => q.Status == QuestStatus.Completed),
                FailedQuests = quests.Count(q => q.Status == QuestStatus.Failed),
                AbandonedQuests = quests.Count(q => q.Status == QuestStatus.Abandoned)
            };
        }
    }

    /// <summary>
    /// Quest statistics for a player
    /// </summary>
    public class QuestStatistics
    {
        public int TotalQuests { get; set; }
        public int ActiveQuests { get; set; }
        public int CompletedQuests { get; set; }
        public int FailedQuests { get; set; }
        public int AbandonedQuests { get; set; }
    }
}

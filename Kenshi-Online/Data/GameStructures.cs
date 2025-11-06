using System;
using System.Collections.Generic;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Data
{
    /// <summary>
    /// Dialog system structures
    /// </summary>
    public class DialogData
    {
        public string DialogId { get; set; } = string.Empty;
        public string SpeakerId { get; set; } = string.Empty;
        public string SpeakerName { get; set; } = string.Empty;
        public List<DialogOption> Options { get; set; } = new List<DialogOption>();
        public Dictionary<string, int> RequiredStats { get; set; } = new Dictionary<string, int>();
        public int ReputationRequirement { get; set; }
    }

    public class DialogOption
    {
        public string OptionId { get; set; } = string.Empty;
        public string Text { get; set; } = string.Empty;
        public string NextDialogId { get; set; } = string.Empty;
        public DialogAction Action { get; set; }
        public bool RequiresItem { get; set; }
        public string RequiredItemId { get; set; } = string.Empty;
    }

    public enum DialogAction
    {
        None,
        Trade,
        Recruit,
        Quest,
        Barter,
        Attack,
        Leave
    }

    /// <summary>
    /// AI State structures
    /// </summary>
    public class AIState
    {
        public string EntityId { get; set; } = string.Empty;
        public AIGoal CurrentGoal { get; set; }
        public string CurrentPackage { get; set; } = string.Empty;
        public string TargetEntityId { get; set; } = string.Empty;
        public List<AITask> TaskQueue { get; set; } = new List<AITask>();
        public float Aggression { get; set; }
        public float Fear { get; set; }
        public bool IsHostile { get; set; }
    }

    public enum AIGoal
    {
        Idle,
        Patrol,
        Guard,
        Follow,
        Attack,
        Flee,
        Loot,
        Trade,
        Craft,
        Mine,
        Farm,
        Research
    }

    public class AITask
    {
        public string TaskType { get; set; } = string.Empty;
        public string TargetId { get; set; } = string.Empty;
        public Dictionary<string, object> Parameters { get; set; } = new Dictionary<string, object>();
        public int Priority { get; set; }
    }

    /// <summary>
    /// Quest system structures
    /// </summary>
    public class QuestData
    {
        public string QuestId { get; set; } = string.Empty;
        public string QuestName { get; set; } = string.Empty;
        public string Description { get; set; } = string.Empty;
        public string GiverId { get; set; } = string.Empty;
        public QuestStatus Status { get; set; }
        public List<QuestObjective> Objectives { get; set; } = new List<QuestObjective>();
        public List<QuestReward> Rewards { get; set; } = new List<QuestReward>();
        public int RequiredLevel { get; set; }
        public DateTime StartTime { get; set; }
        public DateTime? CompletionTime { get; set; }
    }

    public enum QuestStatus
    {
        NotStarted,
        Active,
        Completed,
        Failed,
        Abandoned
    }

    public class QuestObjective
    {
        public string ObjectiveId { get; set; } = string.Empty;
        public string Description { get; set; } = string.Empty;
        public ObjectiveType Type { get; set; }
        public string TargetId { get; set; } = string.Empty;
        public int RequiredCount { get; set; }
        public int CurrentCount { get; set; }
        public bool IsCompleted { get; set; }
    }

    public enum ObjectiveType
    {
        Kill,
        Collect,
        Deliver,
        Escort,
        Defend,
        Explore,
        Craft,
        Talk
    }

    public class QuestReward
    {
        public RewardType Type { get; set; }
        public string ItemId { get; set; } = string.Empty;
        public int Amount { get; set; }
        public int Experience { get; set; }
        public int Reputation { get; set; }
    }

    public enum RewardType
    {
        Money,
        Item,
        Experience,
        Reputation,
        Skill
    }

    /// <summary>
    /// Crafting system structures
    /// </summary>
    public class CraftingRecipe
    {
        public string RecipeId { get; set; } = string.Empty;
        public string ResultItemId { get; set; } = string.Empty;
        public int ResultQuantity { get; set; } = 1;
        public List<CraftingIngredient> Ingredients { get; set; } = new List<CraftingIngredient>();
        public CraftingStation RequiredStation { get; set; }
        public int RequiredSkillLevel { get; set; }
        public float CraftingTime { get; set; }
        public int ExperienceGained { get; set; }
    }

    public class CraftingIngredient
    {
        public string ItemId { get; set; } = string.Empty;
        public string ItemName { get; set; } = string.Empty;
        public int Quantity { get; set; }
        public bool IsConsumed { get; set; } = true;
    }

    public enum CraftingStation
    {
        None,
        Workbench,
        Forge,
        ResearchBench,
        CookingStove,
        Tailoring,
        Pharmacy,
        Farm,
        Mine
    }

    /// <summary>
    /// Advanced combat structures
    /// </summary>
    public class LimbState
    {
        public string LimbName { get; set; } = string.Empty;
        public int CurrentHealth { get; set; }
        public int MaxHealth { get; set; }
        public bool IsSevered { get; set; }
        public bool IsBroken { get; set; }
        public BleedingState Bleeding { get; set; }
        public List<string> AppliedEffects { get; set; } = new List<string>();
    }

    public class BleedingState
    {
        public bool IsBleeding { get; set; }
        public float BleedRate { get; set; }
        public float TotalBloodLost { get; set; }
        public DateTime BleedStartTime { get; set; }
    }

    /// <summary>
    /// Faction advanced structures
    /// </summary>
    public class CriminalRecord
    {
        public string CharacterId { get; set; } = string.Empty;
        public string FactionId { get; set; } = string.Empty;
        public List<Crime> Crimes { get; set; } = new List<Crime>();
        public int TotalBounty { get; set; }
        public DateTime LastCrimeTime { get; set; }
        public bool IsWanted { get; set; }
    }

    public class Crime
    {
        public CrimeType Type { get; set; }
        public string VictimId { get; set; } = string.Empty;
        public string Location { get; set; } = string.Empty;
        public int Bounty { get; set; }
        public DateTime CrimeTime { get; set; }
        public List<string> Witnesses { get; set; } = new List<string>();
    }

    public enum CrimeType
    {
        Theft,
        Murder,
        Assault,
        Trespassing,
        Kidnapping,
        Slavery,
        Smuggling
    }

    /// <summary>
    /// Character relationships
    /// </summary>
    public class CharacterRelationship
    {
        public string Character1Id { get; set; } = string.Empty;
        public string Character2Id { get; set; } = string.Empty;
        public RelationshipType Type { get; set; }
        public int AffectionLevel { get; set; }
        public DateTime EstablishedDate { get; set; }
        public List<string> SharedHistory { get; set; } = new List<string>();
    }

    public enum RelationshipType
    {
        Stranger,
        Acquaintance,
        Friend,
        Enemy,
        Ally,
        Rival,
        Family,
        Romantic,
        Married,
        Master,
        Slave
    }
}

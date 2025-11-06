using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace Kenshi_Online.Game
{
    /// <summary>
    /// Reverse-engineered Kenshi game structures
    /// Based on memory analysis and community research
    /// </summary>

    #region Core Game Structures

    /// <summary>
    /// Main game world instance
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct GameWorld
    {
        public IntPtr VTable;
        public IntPtr GameStatePtr;
        public IntPtr CharacterListPtr;
        public IntPtr NPCListPtr;
        public IntPtr FactionListPtr;
        public IntPtr BuildingListPtr;
        public IntPtr WorldItemsPtr;
        public float GameTime;
        public int GameDay;
        public IntPtr WeatherSystemPtr;
        public IntPtr PhysicsWorldPtr;
    }

    /// <summary>
    /// Character/Squad member structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Character
    {
        public IntPtr VTable;
        public int CharacterID;
        public IntPtr NamePtr;
        public IntPtr RacePtr;

        // Position and rotation
        public float PosX;
        public float PosY;
        public float PosZ;
        public float RotX;
        public float RotY;
        public float RotZ;
        public float RotW; // Quaternion

        // Stats
        public IntPtr StatsPtr;
        public IntPtr SkillsPtr;

        // Combat
        public float Health;
        public float MaxHealth;
        public float BloodLevel;
        public float Hunger;
        public float HungerRate;

        // Limbs
        public IntPtr LimbsPtr;
        public int LimbCount;

        // Equipment and inventory
        public IntPtr InventoryPtr;
        public IntPtr EquipmentPtr;
        public int InventorySize;

        // AI and state
        public IntPtr AIControllerPtr;
        public IntPtr CurrentActionPtr;
        public int CharacterState; // 0=idle, 1=moving, 2=fighting, etc.
        public IntPtr TargetPtr;

        // Faction and relations
        public int FactionID;
        public IntPtr SquadPtr;
        public IntPtr PlayerOwnerPtr; // null if NPC

        // Animation
        public IntPtr AnimationStatePtr;
        public IntPtr SkeletonPtr;

        // Misc
        public byte IsUnconscious;
        public byte IsDead;
        public byte IsPlayerControlled;
        public byte IsInCombat;
    }

    /// <summary>
    /// Body part/limb structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct BodyPart
    {
        public int LimbType; // 0=head, 1=chest, 2=stomach, 3=left arm, etc.
        public float Health;
        public float MaxHealth;
        public float BleedRate;
        public int DamageLevel; // 0=fine, 1=light, 2=damaged, 3=severe, 4=critical
        public byte IsBleeding;
        public byte IsBroken;
        public byte IsMissing;
        public IntPtr ArmorPtr;
    }

    /// <summary>
    /// Faction data structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Faction
    {
        public IntPtr VTable;
        public int FactionID;
        public IntPtr NamePtr;
        public IntPtr LeaderPtr;

        // Relations (array of faction ID -> relationship value)
        public IntPtr RelationsPtr;
        public int RelationCount;

        // Members
        public IntPtr MemberListPtr;
        public int MemberCount;

        // Properties
        public int FactionType; // 0=player, 1=hostile, 2=neutral, 3=friendly
        public int Wealth;
        public IntPtr TerritoryPtr;

        // Flags
        public byte IsPlayerFaction;
        public byte IsHostileToPlayer;
        public byte CanRecruit;
        public byte IsActive;
    }

    /// <summary>
    /// Inventory item structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct GameItem
    {
        public IntPtr VTable;
        public int ItemID;
        public IntPtr ItemTypePtr;
        public IntPtr NamePtr;

        public int Quantity;
        public float Condition; // 0.0 to 1.0
        public float Weight;
        public int Value;

        // Weapon stats (if applicable)
        public float Damage;
        public float AttackSpeed;
        public float Reach;
        public int DamageType; // 0=cut, 1=blunt, 2=pierce

        // Armor stats (if applicable)
        public float CutResistance;
        public float BluntResistance;
        public float PierceResistance;
        public int Coverage; // Body parts covered

        // Food stats (if applicable)
        public float Nutrition;
        public float Hydration;
        public int Perishability;

        public IntPtr OwnerPtr;
        public int ContainerSlot;
    }

    /// <summary>
    /// Building/structure data
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Building
    {
        public IntPtr VTable;
        public int BuildingID;
        public IntPtr BuildingTypePtr;
        public IntPtr NamePtr;

        public float PosX;
        public float PosY;
        public float PosZ;
        public float RotationYaw;

        public float Health;
        public float MaxHealth;
        public int OwnerFactionID;

        public IntPtr InventoryPtr; // Storage containers
        public int BuildingState; // 0=blueprint, 1=building, 2=complete, 3=damaged

        public byte IsPlayerOwned;
        public byte IsOperational;
        public byte RequiresPower;
    }

    /// <summary>
    /// Squad structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Squad
    {
        public IntPtr VTable;
        public int SquadID;
        public IntPtr NamePtr;
        public IntPtr LeaderPtr;

        public IntPtr MemberListPtr;
        public int MemberCount;
        public int MaxMembers;

        public int OwnerFactionID;
        public IntPtr AICommanderPtr;

        // Squad state
        public int CurrentOrder; // 0=hold, 1=follow, 2=patrol, etc.
        public IntPtr TargetLocationPtr;
        public IntPtr TargetEnemyPtr;

        public byte IsPlayerSquad;
        public byte IsInCombat;
    }

    /// <summary>
    /// AI package/behavior
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct AIPackage
    {
        public IntPtr VTable;
        public int PackageType; // 0=idle, 1=wander, 2=guard, 3=patrol, etc.
        public IntPtr TargetPtr;
        public IntPtr PathPtr;
        public int CurrentPathNode;
        public float Priority;
        public byte IsActive;
        public byte IsInterruptible;
    }

    /// <summary>
    /// World item (items on ground)
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WorldItem
    {
        public IntPtr VTable;
        public int WorldItemID;
        public IntPtr ItemPtr;

        public float PosX;
        public float PosY;
        public float PosZ;

        public IntPtr OwnerPtr; // Who dropped it
        public float TimeDropped;
        public byte IsLootable;
    }

    #endregion

    #region Memory Addresses (Kenshi v0.98.50+)

    /// <summary>
    /// Known memory addresses for Kenshi game structures
    /// IMPORTANT: These may change with game updates!
    /// </summary>
    public static class KenshiMemory
    {
        // Base address (ASLR - calculate at runtime)
        public static long BaseAddress = 0x140000000;

        // Core game pointers
        public static class Game
        {
            public const long WorldInstance = 0x24D8F40;
            public const long GameState = 0x24D8F48;
            public const long GameTime = 0x24D8F50;
            public const long GameDay = 0x24D8F58;
        }

        // Character management
        public static class Characters
        {
            public const long PlayerSquadList = 0x24C5A20;
            public const long PlayerSquadCount = 0x24C5A28;
            public const long AllCharactersList = 0x24C5B00;
            public const long AllCharactersCount = 0x24C5B08;
            public const long SelectedCharacter = 0x24C5A30;
        }

        // Faction system
        public static class Factions
        {
            public const long FactionList = 0x24D2100;
            public const long FactionCount = 0x24D2108;
            public const long PlayerFaction = 0x24D2110;
            public const long RelationMatrix = 0x24D2200;
        }

        // World and buildings
        public static class World
        {
            public const long BuildingList = 0x24E1000;
            public const long BuildingCount = 0x24E1008;
            public const long WorldItemsList = 0x24E1100;
            public const long WorldItemsCount = 0x24E1108;
        }

        // Physics and rendering
        public static class Engine
        {
            public const long PhysicsWorld = 0x24F0000;
            public const long Camera = 0x24E7C20;
            public const long CameraTarget = 0x24E7C38;
            public const long Renderer = 0x24F5000;
        }

        // Input and UI
        public static class Input
        {
            public const long InputHandler = 0x24F2D80;
            public const long CommandQueue = 0x24F2D90;
            public const long SelectedUnits = 0x24F2DA0;
            public const long UIState = 0x24F3000;
        }

        // Functions
        public static class Functions
        {
            public const long SpawnCharacter = 0x8B3C80;
            public const long DespawnCharacter = 0x8B4120;
            public const long AddToSquad = 0x8B4500;
            public const long RemoveFromSquad = 0x8B4600;
            public const long AddItemToInventory = 0x9C2100;
            public const long RemoveItemFromInventory = 0x9C2200;
            public const long SetCharacterState = 0x8C1000;
            public const long IssueCommand = 0x8D5000;
            public const long CreateFaction = 0x7A2000;
            public const long SetFactionRelation = 0x7A2500;
        }
    }

    #endregion

    #region Helper Enums

    public enum CharacterState
    {
        Idle = 0,
        Moving = 1,
        Fighting = 2,
        Looting = 3,
        Crafting = 4,
        Sleeping = 5,
        Eating = 6,
        Healing = 7,
        Unconscious = 8,
        Dead = 9,
        Working = 10,
        Talking = 11
    }

    public enum LimbType
    {
        Head = 0,
        Chest = 1,
        Stomach = 2,
        LeftArm = 3,
        RightArm = 4,
        LeftLeg = 5,
        RightLeg = 6
    }

    public enum DamageType
    {
        Cut = 0,
        Blunt = 1,
        Pierce = 2
    }

    public enum FactionRelation
    {
        Hostile = -100,
        Unfriendly = -50,
        Neutral = 0,
        Friendly = 50,
        Allied = 100
    }

    public enum ItemType
    {
        Weapon = 0,
        Armor = 1,
        Food = 2,
        Medical = 3,
        Resource = 4,
        Tool = 5,
        Building = 6,
        Book = 7,
        Money = 8
    }

    public enum WeaponClass
    {
        Katana = 0,
        Sabre = 1,
        HeavyWeapon = 2,
        Blunt = 3,
        Polearm = 4,
        Hacker = 5,
        Martial = 6,
        Crossbow = 7
    }

    public enum CommandType
    {
        Hold = 0,
        Follow = 1,
        Move = 2,
        Attack = 3,
        Loot = 4,
        Work = 5,
        Patrol = 6,
        Guard = 7,
        Sneak = 8,
        Passive = 9,
        Taunt = 10
    }

    #endregion

    #region Stats and Skills

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct CharacterStats
    {
        public float Strength;
        public float Toughness;
        public float Dexterity;
        public float Perception;

        // Combat skills
        public float MeleeAttack;
        public float MeleeDefence;
        public float Dodge;
        public float MartialArts;

        // Ranged
        public float Turrets;
        public float Precision;

        // Thievery
        public float Stealth;
        public float Lockpicking;
        public float Thievery;
        public float Assassination;

        // Athletic
        public float Athletics;
        public float Swimming;

        // Science
        public float Science;
        public float Engineering;
        public float Robotics;

        // Crafting
        public float WeaponSmith;
        public float ArmourSmith;
        public float Crossbows;

        // Medical
        public float Medic;
        public float Cooking;

        // Production
        public float Farming;
        public float Labouring;
    }

    #endregion

    #region Pathfinding

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct NavigationPath
    {
        public IntPtr NodesPtr;
        public int NodeCount;
        public int CurrentNode;
        public float TotalDistance;
        public byte IsValid;
        public byte IsComplete;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct PathNode
    {
        public float X;
        public float Y;
        public float Z;
        public float Cost;
        public int NextNodeIndex;
    }

    #endregion

    #region Combat System

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct CombatData
    {
        public IntPtr AttackerPtr;
        public IntPtr DefenderPtr;
        public int AttackType; // 0=normal, 1=power, 2=quick
        public int TargetLimb;
        public float Damage;
        public int DamageType;
        public byte WasBlocked;
        public byte WasDodged;
        public byte IsCritical;
        public float Knockback;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Weapon
    {
        public IntPtr ItemPtr;
        public int WeaponClass;
        public float BaseDamage;
        public float AttackSpeed;
        public float DefenceBonus;
        public float Reach;
        public float Weight;
        public int DamageType;
        public byte IsTwoHanded;
        public byte IsRanged;
    }

    #endregion

    #region Weather and Environment

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WeatherSystem
    {
        public IntPtr VTable;
        public int CurrentWeather; // 0=clear, 1=rain, 2=sandstorm, 3=acidrain
        public float Intensity;
        public float Temperature;
        public float WindSpeed;
        public float WindDirection;
        public int TimeOfDay; // 0-23
    }

    #endregion
}

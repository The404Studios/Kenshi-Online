using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Reverse-engineered Kenshi game structures for multiplayer synchronization
    /// Based on Kenshi v1.0.x (64-bit) memory analysis
    ///
    /// IMPORTANT: These structures must match the C++ definitions in KenshiMemoryStructures.h
    /// </summary>

    #region Pattern Signatures for Dynamic Offset Discovery

    /// <summary>
    /// Pattern signature definitions for finding offsets across game versions
    /// Format: byte pattern with '?' wildcards, mask ('x' = match, '?' = wildcard)
    /// </summary>
    public static class PatternSignatures
    {
        public static readonly PatternDef GameWorld = new PatternDef(
            "GameWorld",
            new byte[] { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x00, 0x48, 0x8B, 0x40, 0x08 },
            "xxx????xxxx?xxxx",
            3, true, 7
        );

        public static readonly PatternDef PlayerSquadList = new PatternDef(
            "PlayerSquadList",
            new byte[] { 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD8 },
            "xxx????x????xxx",
            3, true, 7
        );

        public static readonly PatternDef AllCharactersList = new PatternDef(
            "AllCharactersList",
            new byte[] { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x04, 0xC8, 0x48, 0x85, 0xC0 },
            "xxx????xxxxxxx",
            3, true, 7
        );

        public static readonly PatternDef FactionManager = new PatternDef(
            "FactionManager",
            new byte[] { 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0x48, 0x8B, 0x10 },
            "xxx????x????xxxxxx",
            3, true, 7
        );

        public static readonly PatternDef WeatherSystem = new PatternDef(
            "WeatherSystem",
            new byte[] { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x40, 0x00, 0xF3, 0x0F, 0x58, 0x05 },
            "xxx????xxxx?xxxx",
            3, true, 7
        );

        public static readonly PatternDef SpawnCharacter = new PatternDef(
            "SpawnCharacter",
            new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x00, 0x48, 0x89, 0x74, 0x24, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9 },
            "xxxx?xxxx?xxxxxxxxxxx",
            0, false, 0
        );

        public static readonly PatternDef CharacterUpdate = new PatternDef(
            "CharacterUpdate",
            new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x00, 0x48, 0x89, 0x6C, 0x24, 0x00, 0x48, 0x89, 0x74, 0x24, 0x00, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xE9 },
            "xxxx?xxxx?xxxx?xxxxxxxxxxxx",
            0, false, 0
        );

        public static readonly PatternDef CombatSystem = new PatternDef(
            "CombatSystem",
            new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x00, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x00, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF2 },
            "xxxx?xxxxxxxxxxxxxxx?xxx????xxx",
            0, false, 0
        );
    }

    /// <summary>
    /// Pattern definition structure
    /// </summary>
    public class PatternDef
    {
        public string Name { get; }
        public byte[] Pattern { get; }
        public string Mask { get; }
        public int Offset { get; }
        public bool IsRelative { get; }
        public int RelativeBase { get; }

        public PatternDef(string name, byte[] pattern, string mask, int offset, bool isRelative, int relativeBase)
        {
            Name = name;
            Pattern = pattern;
            Mask = mask;
            Offset = offset;
            IsRelative = isRelative;
            RelativeBase = relativeBase;
        }
    }

    #endregion

    #region Basic Types

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector2
    {
        public float X;
        public float Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }

        public float Length() => MathF.Sqrt(X * X + Y * Y + Z * Z);
        public float LengthSquared() => X * X + Y * Y + Z * Z;

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.X * s, a.Y * s, a.Z * s);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector4
    {
        public float X;
        public float Y;
        public float Z;
        public float W;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Quaternion
    {
        public float X;
        public float Y;
        public float Z;
        public float W;

        public static Quaternion Identity => new Quaternion { X = 0, Y = 0, Z = 0, W = 1 };
    }

    #endregion

    #region Enumerations

    public enum SkillType
    {
        MeleeAttack = 0,
        MeleeDefence = 1,
        Dodge = 2,
        MartialArts = 3,
        Turrets = 4,
        Crossbows = 5,
        Precision = 6,
        Stealth = 7,
        Lockpicking = 8,
        Thievery = 9,
        Assassination = 10,
        Athletics = 11,
        Swimming = 12,
        Strength = 13,
        Toughness = 14,
        Dexterity = 15,
        Perception = 16,
        Science = 17,
        Engineering = 18,
        Robotics = 19,
        WeaponSmithing = 20,
        ArmourSmithing = 21,
        FieldMedic = 22,
        Cooking = 23,
        Farming = 24,
        Labouring = 25,
        SKILL_COUNT = 26
    }

    public enum LimbType
    {
        Head = 0,
        Chest = 1,
        Stomach = 2,
        LeftArm = 3,
        RightArm = 4,
        LeftLeg = 5,
        RightLeg = 6,
        LIMB_COUNT = 7
    }

    public enum LimbCondition
    {
        Healthy = 0,
        Injured = 1,
        Damaged = 2,
        SeverelyDamaged = 3,
        Crippled = 4,
        Missing = 5
    }

    public enum AIState
    {
        Idle = 0,
        Moving = 1,
        Fighting = 2,
        Looting = 3,
        Crafting = 4,
        Sleeping = 5,
        Eating = 6,
        Healing = 7,
        FirstAid = 8,
        Building = 9,
        Mining = 10,
        Farming = 11,
        Working = 12,
        Talking = 13,
        Trading = 14,
        Fleeing = 15,
        Unconscious = 16,
        Dead = 17,
        PlayingDead = 18,
        Recovery = 19,
        Following = 20,
        Patrolling = 21,
        Guarding = 22,
        STATE_COUNT = 23
    }

    public enum AIPackageType
    {
        None = 0,
        Idle = 1,
        Wander = 2,
        Patrol = 3,
        Guard = 4,
        Follow = 5,
        Attack = 6,
        Flee = 7,
        Work = 8,
        Rest = 9,
        Medic = 10,
        Loot = 11,
        Trade = 12,
        Dialogue = 13,
        PACKAGE_COUNT = 14
    }

    public enum AnimationType
    {
        Idle = 0,
        Walk = 1,
        Run = 2,
        Sprint = 3,
        Sneak = 4,
        Jump = 5,
        Fall = 6,
        Land = 7,
        Swim = 8,
        AttackLight = 10,
        AttackHeavy = 11,
        AttackCombo = 12,
        Block = 13,
        DodgeAnim = 14,
        Stagger = 15,
        KnockDown = 16,
        GetUp = 17,
        PickUp = 20,
        PutDown = 21,
        Use = 22,
        Talk = 23,
        Trade = 24,
        Sleep = 25,
        Eat = 26,
        Heal = 27,
        Craft = 28,
        Build = 29,
        Mine = 30,
        Farm = 31,
        Die = 40,
        PlayDead = 41,
        UnconsciousAnim = 42,
        Crawl = 43,
        ANIM_COUNT = 50
    }

    public enum DamageType
    {
        Cut = 0,
        Blunt = 1,
        Pierce = 2
    }

    public enum AttackType
    {
        Light = 0,
        Heavy = 1,
        Power = 2,
        Combo = 3,
        Counter = 4,
        TYPE_COUNT = 5
    }

    public enum ItemCategory
    {
        Weapon = 0,
        Armor = 1,
        Food = 2,
        Medical = 3,
        Resource = 4,
        Tool = 5,
        Blueprint = 6,
        Book = 7,
        Money = 8,
        Junk = 9,
        CATEGORY_COUNT = 10
    }

    public enum WeaponType
    {
        Katana = 0,
        Sabre = 1,
        Hackers = 2,
        HeavyWeapons = 3,
        Blunt = 4,
        Polearms = 5,
        MartialArts = 6,
        Crossbow = 7,
        WEAPON_TYPE_COUNT = 8
    }

    public enum ItemGrade
    {
        Rusted = 0,
        Rusting = 1,
        Standard = 2,
        Catun = 3,
        Mk1 = 4,
        Mk2 = 5,
        Mk3 = 6,
        Edge1 = 7,
        Edge2 = 8,
        Edge3 = 9,
        Meitou = 10
    }

    public enum SquadOrder
    {
        None = 0,
        Hold = 1,
        Follow = 2,
        Move = 3,
        Attack = 4,
        Patrol = 5,
        Guard = 6,
        Work = 7,
        Passive = 8,
        Aggressive = 9,
        Defensive = 10,
        ORDER_COUNT = 11
    }

    public enum FormationType
    {
        None = 0,
        Line = 1,
        Column = 2,
        Wedge = 3,
        Circle = 4,
        Scattered = 5,
        FORMATION_COUNT = 6
    }

    public enum FactionRelation
    {
        Allied = 100,
        Friendly = 50,
        Neutral = 0,
        Unfriendly = -50,
        Hostile = -100
    }

    public enum FactionType
    {
        Player = 0,
        Major = 1,
        Minor = 2,
        Bandit = 3,
        Animal = 4,
        NeutralFaction = 5,
        FACTION_TYPE_COUNT = 6
    }

    public enum WeatherType
    {
        Clear = 0,
        Cloudy = 1,
        Rain = 2,
        Storm = 3,
        Dust = 4,
        Sandstorm = 5,
        AcidRain = 6,
        Fog = 7,
        WEATHER_COUNT = 8
    }

    public enum BuildingState
    {
        Blueprint = 0,
        UnderConstruction = 1,
        Complete = 2,
        Damaged = 3,
        Destroyed = 4,
        STATE_COUNT = 5
    }

    public enum BuildingType
    {
        House = 0,
        Wall = 1,
        Gate = 2,
        Tower = 3,
        Workshop = 4,
        Storage = 5,
        Farm = 6,
        Mine = 7,
        Generator = 8,
        Turret = 9,
        Research = 10,
        Medical = 11,
        Training = 12,
        TYPE_COUNT = 13
    }

    public enum CharacterType
    {
        Human = 0,
        Shek = 1,
        Hive = 2,
        Skeleton = 3,
        Animal = 4,
        TYPE_COUNT = 5
    }

    public enum EquipSlot
    {
        Head = 0,
        Chest = 1,
        Legs = 2,
        Boots = 3,
        MainHand = 4,
        OffHand = 5,
        Backpack = 6,
        SLOT_COUNT = 7
    }

    #endregion

    #region Core Game Structures

    /// <summary>
    /// Single skill data
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Skill
    {
        public float Level;
        public float Experience;
        public float ExperienceMultiplier;
        public float LevelCap;
        public byte IsLocked;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public byte[] Padding;
    }

    /// <summary>
    /// Body part/limb structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct BodyPart
    {
        public LimbType Type;
        public float Health;
        public float MaxHealth;
        public float BleedRate;
        public float Damage;
        public LimbCondition Condition;
        public byte IsBleeding;
        public byte IsBroken;
        public byte IsBandaged;
        public byte IsSplinted;
        public byte IsMissing;
        public byte IsRobotic;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] Padding;
        public IntPtr EquippedArmor;
        public float ArmorCoverage;
        public float WearDamage;
    }

    /// <summary>
    /// Main game world - Your "GameState" container
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct GameWorld
    {
        public IntPtr VTable;

        // Time
        public float GameTime;
        public int GameDay;
        public int GameYear;
        public float TimeScale;
        public float RealTime;

        // Weather
        public IntPtr Weather;

        // Global lists
        public IntPtr AllCharacters;
        public int CharacterCount;
        public IntPtr AllSquads;
        public int SquadCount;
        public IntPtr AllFactions;
        public int FactionCount;
        public IntPtr AllBuildings;
        public int BuildingCount;
        public IntPtr WorldItems;
        public int WorldItemCount;

        // Player data
        public IntPtr PlayerSquad;
        public IntPtr SelectedCharacters;
        public int SelectedCount;
        public IntPtr PlayerFaction;
        public int PlayerMoney;

        // World grid
        public IntPtr Cells;
        public int CellCountX;
        public int CellCountY;
        public IntPtr ActiveCell;

        // Physics
        public IntPtr PhysicsWorld;
        public IntPtr CollisionWorld;

        // AI Director
        public IntPtr AIDirector;
        public float GlobalThreatLevel;
        public float GlobalActivityLevel;

        // Object managers
        public IntPtr CharacterManager;
        public IntPtr SquadManager;
        public IntPtr FactionManager;
        public IntPtr BuildingManager;
        public IntPtr ItemManager;

        // Game state flags
        public byte IsPaused;
        public byte IsLoading;
        public byte IsSaving;
        public byte IsSimulating;
    }

    /// <summary>
    /// Character structure - Your "PlayerController"
    /// This is the main character structure representing both players and NPCs
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Character
    {
        // Base Object
        public IntPtr VTable;
        public uint CharacterId;
        public uint TemplateId;
        public IntPtr Name; // OgreString
        public uint Flags;
        public byte IsActive;
        public byte IsVisible;
        public byte IsLoaded;
        public byte Padding1;
        public IntPtr SceneNode;

        // Identity
        public CharacterType CharacterType;
        public IntPtr RaceName;
        public IntPtr RaceData;
        public byte Gender;
        public byte Age;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] Padding2;

        // Transform
        public Vector3 Position;
        public Quaternion Rotation;
        public Vector3 Velocity;
        public float MoveSpeed;
        public float BaseSpeed;

        // Stats and Skills
        public IntPtr Stats;
        public IntPtr RawSkillLevels;
        public int SkillCount;

        // Body and Health
        public IntPtr Body;
        public float Health;
        public float MaxHealth;
        public float BloodLevel;
        public float BloodMax;
        public float Hunger;
        public float HungerMax;
        public float HungerRate;
        public float Thirst;
        public float ThirstMax;
        public float ThirstRate;
        public float Fatigue;
        public float FatigueMax;

        // Inventory and Equipment
        public IntPtr Inventory;
        public IntPtr Equipment;
        public IntPtr MainWeapon;
        public IntPtr OffWeapon;

        // AI and Control
        public IntPtr AIController;
        public AIState CurrentState;
        public IntPtr CurrentAction;
        public IntPtr TargetObject;
        public Vector3 TargetPosition;

        // Animation
        public IntPtr AnimState;
        public IntPtr Ragdoll;
        public IntPtr ModelInstance;

        // Faction and Social
        public int FactionId;
        public IntPtr Faction;
        public IntPtr Squad;
        public IntPtr SquadLeader;
        public IntPtr DialogueState;

        // Player Control
        public IntPtr PlayerOwner;
        public byte IsPlayerControlled;
        public byte IsSelected;
        public byte IsUnconscious;
        public byte IsDead;
        public byte IsPlayingDead;
        public byte IsInCombat;
        public byte IsHostile;
        public byte IsSneaking;
        public byte IsCarrying;
        public byte IsBeingCarried;
        public byte IsCrippled;
        public byte IsEating;
        public byte IsSleeping;
        public byte IsWorking;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] Padding3;

        // Combat Data
        public IntPtr CombatTarget;
        public float CombatTimer;
        public float LastAttackTime;
        public float AttackCooldown;
        public float BlockCooldown;
        public float DodgeCooldown;
        public int ComboCounter;
        public float DamageDealt;
        public float DamageTaken;
        public int KillCount;
        public int KnockoutCount;

        // World Cell / Location
        public IntPtr CurrentCell;
        public int CellX;
        public int CellY;
        public IntPtr CurrentBuilding;
        public IntPtr CurrentZone;

        // Bounty System
        public int Bounty;
        public IntPtr FactionBounties;
        public int BountyFactionCount;
    }

    /// <summary>
    /// Squad container - acts like a GameState for groups
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Squad
    {
        public IntPtr VTable;
        public uint SquadId;
        public uint Padding;
        public IntPtr Name;

        // Members
        public IntPtr Members;
        public int MemberCount;
        public int MaxMembers;
        public IntPtr Leader;

        // Faction
        public int FactionId;
        public IntPtr Faction;

        // Orders
        public SquadOrder CurrentOrder;
        public Vector3 OrderTarget;
        public IntPtr TargetEnemy;
        public IntPtr TargetBuilding;

        // Formation
        public FormationType Formation;
        public float FormationSpacing;

        // Pathfinding
        public IntPtr SquadPath;
        public Vector3 Destination;
        public float PathfindingRadius;

        // AI
        public IntPtr SquadAI;
        public float ThreatLevel;
        public byte IsInCombat;
        public byte IsPlayerSquad;
        public byte IsMoving;
        public byte Padding2;
    }

    /// <summary>
    /// Faction data structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Faction
    {
        public IntPtr VTable;
        public uint FactionId;
        public uint Padding;
        public IntPtr Name;

        public FactionType Type;

        // Relations
        public IntPtr Relations;
        public int RelationCount;

        // Members
        public IntPtr Members;
        public int MemberCount;
        public IntPtr Squads;
        public int SquadCount;

        // Leader
        public IntPtr Leader;

        // Territory
        public IntPtr Territory;
        public int TerritoryCount;

        // Economy
        public int Wealth;
        public int Population;

        // Flags
        public byte IsPlayerFaction;
        public byte IsHostileToPlayer;
        public byte CanRecruit;
        public byte IsActiveFaction;
        public byte IsHidden;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public byte[] Padding2;
    }

    /// <summary>
    /// AI Controller (state machine)
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct AIController
    {
        public IntPtr VTable;
        public IntPtr Owner;

        public AIState CurrentState;
        public AIState PreviousState;
        public float StateTime;

        // Task system
        public IntPtr TaskStack;
        public int TaskCount;
        public int MaxTasks;
        public IntPtr CurrentTask;

        // Navigation
        public IntPtr CurrentPath;
        public Vector3 MoveTarget;
        public float MoveSpeedAI;
        public float ArrivalRadius;

        // Targeting
        public IntPtr Targets;
        public int TargetCount;
        public IntPtr PrimaryTarget;

        // Combat AI
        public float AggressionLevel;
        public float FleeThreshold;
        public float ConfidenceLevel;
        public float ThreatLevel;

        // Awareness
        public float PerceptionRange;
        public float HearingRange;
        public float Alertness;

        // Behavior flags
        public byte IsAutonomous;
        public byte IsInCombatAI;
        public byte IsAggressive;
        public byte IsDefensive;
        public byte IsPassive;
        public byte IsSneakingAI;
        public byte IsOnJob;
        public byte Padding;

        // Jobs
        public IntPtr JobSchedule;
        public int CurrentJobIndex;
    }

    /// <summary>
    /// Animation state
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct AnimationState
    {
        public IntPtr VTable;
        public IntPtr Owner;

        public AnimationType CurrentAnim;
        public AnimationType NextAnim;

        public float AnimTime;
        public float AnimDuration;
        public float AnimSpeed;
        public float BlendWeight;

        public IntPtr Skeleton;
        public IntPtr AnimController;

        public AnimationType UpperBodyAnim;
        public AnimationType LowerBodyAnim;
        public float UpperBlend;
        public float LowerBlend;

        // Combat timing
        public float AttackStartTime;
        public float AttackHitTime;
        public float AttackEndTime;
        public float BlockWindow;

        // Flags
        public byte IsPlaying;
        public byte IsLooping;
        public byte IsPaused;
        public byte IsBlending;
        public byte CanCancel;
        public byte InAttackPhase;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] Padding;
    }

    /// <summary>
    /// Weather system
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WeatherSystem
    {
        public IntPtr VTable;
        public WeatherType CurrentWeather;
        public WeatherType NextWeather;
        public float TransitionProgress;
        public float Intensity;
        public float Temperature;
        public float WindSpeed;
        public float WindDirection;
        public Vector3 WindVector;
        public int TimeOfDay;
        public float DayNightCycle;
    }

    /// <summary>
    /// Building structure
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Building
    {
        public IntPtr VTable;
        public uint BuildingId;
        public uint TemplateId;
        public IntPtr Name;
        public BuildingType Type;
        public BuildingState State;

        public Vector3 Position;
        public float RotationY;

        public float HealthBuilding;
        public float MaxHealthBuilding;

        public float ConstructionProgress;
        public IntPtr RequiredMaterials;

        public int OwnerFactionId;
        public IntPtr OwnerFaction;
        public IntPtr AssignedSquad;

        public IntPtr Storage;
        public int MaxStorage;

        public IntPtr ProductionQueue;
        public float ProductionProgress;

        public byte IsPlayerOwned;
        public byte IsOperational;
        public byte RequiresPower;
        public byte HasPower;
        public byte IsUnderAttack;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public byte[] Padding;

        public IntPtr Workers;
        public int WorkerCount;
        public int MaxWorkers;
    }

    /// <summary>
    /// Inventory item
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct GameItem
    {
        public IntPtr VTable;
        public uint ItemId;
        public uint TemplateId;
        public IntPtr Name;
        public ItemCategory Category;
        public ItemGrade Grade;

        public float Condition;
        public float MaxCondition;
        public float Weight;
        public int Value;
        public int StackCount;
        public int MaxStack;

        // Weapon-specific
        public WeaponType WeaponTypeValue;
        public DamageType DamageTypeValue;
        public float BaseDamage;
        public float DamageMin;
        public float DamageMax;
        public float AttackSpeed;
        public float DefenceBonus;
        public float IndoorPenalty;
        public float Reach;
        public float BloodLoss;
        public byte IsTwoHanded;
        public byte IsRanged;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] WeaponPadding;

        public IntPtr Owner;
        public IntPtr ContainerPtr;
        public int SlotIndex;
    }

    /// <summary>
    /// Combat event for synchronization
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct CombatEvent
    {
        public uint AttackerId;
        public uint DefenderId;
        public AttackType AttackTypeValue;
        public DamageType DamageTypeValue;
        public LimbType TargetLimb;

        public float Damage;
        public float DamageBlocked;
        public float BleedDamage;
        public float Knockback;

        public byte WasBlocked;
        public byte WasDodged;
        public byte WasParried;
        public byte IsCritical;
        public byte CausedKnockdown;
        public byte CausedStagger;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public byte[] Padding;

        public float Timestamp;
    }

    /// <summary>
    /// Navigation path
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct NavigationPath
    {
        public IntPtr Nodes;
        public int NodeCount;
        public int CurrentNode;
        public float TotalDistance;
        public float DistanceTraveled;
        public Vector3 Destination;
        public byte IsValid;
        public byte IsComplete;
        public byte IsPaused;
        public byte Padding;
    }

    #endregion

    #region Network Sync Structures

    /// <summary>
    /// Lightweight player state for network synchronization
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct PlayerState
    {
        public uint CharacterId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Name;

        public Vector3 Position;
        public Quaternion Rotation;
        public Vector3 Velocity;

        public float Health;
        public float MaxHealth;
        public float BloodLevel;
        public float Hunger;
        public float Thirst;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)]
        public float[] LimbHealth;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)]
        public byte[] LimbBleeding;

        public AIState State;
        public AnimationType CurrentAnimation;
        public byte IsUnconscious;
        public byte IsDead;
        public byte IsInCombat;
        public byte IsSneaking;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)]
        public uint[] EquippedItems;

        public int FactionId;
        public int SquadId;

        public uint CombatTargetId;
        public float AttackCooldown;

        public float GameTime;
        public ulong SyncTick;
    }

    /// <summary>
    /// Squad state for network sync
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct SquadState
    {
        public uint SquadId;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Name;

        public int FactionId;
        public uint LeaderId;

        public SquadOrder CurrentOrder;
        public FormationType Formation;
        public Vector3 OrderTarget;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public uint[] MemberIds;
        public int MemberCount;

        public byte IsInCombat;
        public byte IsPlayerSquad;
        public byte IsMoving;
        public byte Padding;

        public ulong SyncTick;
    }

    /// <summary>
    /// World state snapshot
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WorldStateSnapshot
    {
        public float GameTime;
        public int GameDay;
        public int GameYear;
        public float TimeScale;

        public WeatherType Weather;
        public float WeatherIntensity;
        public float Temperature;
        public float WindSpeed;
        public float WindDirection;

        public int PlayerMoney;
        public int PlayerFactionId;

        public int PlayerCharacterCount;
        public int TotalNPCCount;
        public int TotalBuildingCount;

        public ulong SyncTick;
    }

    #endregion

    #region Memory Addresses (Kenshi v1.0.x)

    /// <summary>
    /// Known memory addresses for Kenshi game structures
    /// IMPORTANT: These are fallback values. Use RuntimeOffsets for dynamic online-fetched offsets!
    /// Call KenshiMemory.InitializeOnlineOffsets() at startup to fetch from online source.
    /// </summary>
    public static class KenshiMemory
    {
        // Base address (ASLR - calculate at runtime)
        public static long BaseAddress = 0x140000000;

        // Dynamic offset storage (populated from online source)
        private static GameOffsetsData? _onlineOffsets;
        private static FunctionOffsetsData? _onlineFunctions;
        private static bool _onlineInitialized;

        /// <summary>
        /// Initialize offsets from online source. Call this before accessing memory.
        /// </summary>
        public static async System.Threading.Tasks.Task<bool> InitializeOnlineOffsetsAsync()
        {
            if (_onlineInitialized) return true;

            var provider = OnlineOffsetProvider.Instance;
            var success = await provider.InitializeAsync();

            if (success)
            {
                _onlineOffsets = provider.GetCurrentOffsets();
                _onlineFunctions = FunctionOffsetsData.CreateHardcodedDefaults();
                Console.WriteLine("[KenshiMemory] Online offsets loaded successfully");
            }

            _onlineInitialized = true;
            return success;
        }

        /// <summary>
        /// Synchronous initialization
        /// </summary>
        public static bool InitializeOnlineOffsets()
        {
            return InitializeOnlineOffsetsAsync().GetAwaiter().GetResult();
        }

        /// <summary>
        /// Check if online offsets are loaded
        /// </summary>
        public static bool HasOnlineOffsets => _onlineOffsets != null;

        /// <summary>
        /// Get dynamic offset value (online or fallback to hardcoded)
        /// </summary>
        public static long GetOffset(string category, string name)
        {
            if (_onlineOffsets == null) return 0;

            return (category, name) switch
            {
                ("Game", "WorldInstance") => _onlineOffsets.WorldInstance,
                ("Game", "GameState") => _onlineOffsets.GameState,
                ("Game", "GameTime") => _onlineOffsets.GameTime,
                ("Game", "GameDay") => _onlineOffsets.GameDay,
                ("Characters", "PlayerSquadList") => _onlineOffsets.PlayerSquadList,
                ("Characters", "PlayerSquadCount") => _onlineOffsets.PlayerSquadCount,
                ("Characters", "AllCharactersList") => _onlineOffsets.AllCharactersList,
                ("Characters", "AllCharactersCount") => _onlineOffsets.AllCharactersCount,
                ("Characters", "SelectedCharacter") => _onlineOffsets.SelectedCharacter,
                ("Factions", "FactionList") => _onlineOffsets.FactionList,
                ("Factions", "FactionCount") => _onlineOffsets.FactionCount,
                ("Factions", "PlayerFaction") => _onlineOffsets.PlayerFaction,
                ("World", "BuildingList") => _onlineOffsets.BuildingList,
                ("World", "BuildingCount") => _onlineOffsets.BuildingCount,
                ("World", "WeatherSystem") => _onlineOffsets.WeatherSystem,
                ("Engine", "PhysicsWorld") => _onlineOffsets.PhysicsWorld,
                ("Engine", "Camera") => _onlineOffsets.Camera,
                ("Input", "InputHandler") => _onlineOffsets.InputHandler,
                _ => 0
            };
        }

        /// <summary>
        /// Get absolute address using online or hardcoded offset
        /// </summary>
        public static long GetAbsoluteAddress(string category, string name)
        {
            var offset = GetOffset(category, name);
            if (offset == 0)
            {
                // Fall back to hardcoded
                offset = (category, name) switch
                {
                    ("Game", "WorldInstance") => Game.WorldInstance,
                    ("Game", "GameState") => Game.GameState,
                    ("Characters", "PlayerSquadList") => Characters.PlayerSquadList,
                    ("Characters", "AllCharactersList") => Characters.AllCharactersList,
                    _ => 0
                };
            }
            return BaseAddress + offset;
        }

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
            public const long WeatherSystem = 0x24E7000;
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
            public const long PathfindRequest = 0x7B1000;
            public const long CombatAttack = 0x8E2000;
        }

        // Character structure offsets
        public static class CharacterOffsets
        {
            public const int Position = 0x70;
            public const int Rotation = 0x7C;
            public const int Health = 0xC0;
            public const int MaxHealth = 0xC4;
            public const int Blood = 0xC8;
            public const int Hunger = 0xD0;
            public const int Inventory = 0xF0;
            public const int Equipment = 0xF8;
            public const int AI = 0x110;
            public const int State = 0x118;
            public const int Faction = 0x158;
            public const int Squad = 0x168;
            public const int AnimState = 0x140;
            public const int Body = 0xB8;
        }
    }

    #endregion
}

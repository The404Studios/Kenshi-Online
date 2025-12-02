/*
 * Kenshi Memory Structures
 * Reverse-engineered game structures for multiplayer synchronization
 *
 * CRITICAL: These structures are based on Kenshi v1.0.x (64-bit)
 * Memory layouts may change with game updates - use pattern scanning for offsets
 */

#pragma once

#include <cstdint>
#include <Windows.h>

namespace Kenshi
{
    //=========================================================================
    // FORWARD DECLARATIONS
    //=========================================================================

    struct GameWorld;
    struct Character;
    struct Squad;
    struct Faction;
    struct AIController;
    struct AnimationState;
    struct Inventory;
    struct Item;
    struct Building;
    struct WeatherSystem;
    struct PhysicsWorld;
    struct WorldCell;
    struct NavigationMesh;

    //=========================================================================
    // BASIC TYPES
    //=========================================================================

    struct Vector2
    {
        float x, y;
    };

    struct Vector3
    {
        float x, y, z;

        Vector3 operator+(const Vector3& other) const { return { x + other.x, y + other.y, z + other.z }; }
        Vector3 operator-(const Vector3& other) const { return { x - other.x, y - other.y, z - other.z }; }
        Vector3 operator*(float scalar) const { return { x * scalar, y * scalar, z * scalar }; }
        float Length() const { return sqrtf(x * x + y * y + z * z); }
        float LengthSquared() const { return x * x + y * y + z * z; }
    };

    struct Vector4
    {
        float x, y, z, w;
    };

    // Quaternion for rotation (Kenshi uses quaternions internally)
    struct Quaternion
    {
        float x, y, z, w;

        static Quaternion Identity() { return { 0, 0, 0, 1 }; }
    };

    // Transform component
    struct Transform
    {
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
    };

    // Bounding box for collision/culling
    struct AABB
    {
        Vector3 min;
        Vector3 max;
    };

    //=========================================================================
    // OGRE ENGINE TYPES (Kenshi uses Ogre3D)
    //=========================================================================

    // Ogre::String is std::string in Kenshi builds
    struct OgreString
    {
        char* data;
        size_t length;
        size_t capacity;

        // Small string optimization buffer
        char sso_buffer[16];
    };

    // Ogre SceneNode reference
    struct SceneNode
    {
        void* vtable;
        OgreString name;
        Transform localTransform;
        Transform worldTransform;
        SceneNode* parent;
        void* children;  // std::vector<SceneNode*>
        int childCount;
    };

    //=========================================================================
    // KENSHI GAME OBJECT BASE
    //=========================================================================

    // Base class for all game objects
    struct GameObject
    {
        void* vtable;                    // 0x00 - Virtual function table
        uint32_t objectId;               // 0x08 - Unique object ID
        uint32_t objectType;             // 0x0C - Type identifier
        OgreString name;                 // 0x10 - Object name
        uint32_t flags;                  // 0x30 - Object flags
        uint8_t isActive;                // 0x34
        uint8_t isVisible;               // 0x35
        uint8_t isLoaded;                // 0x36
        uint8_t padding;                 // 0x37
        SceneNode* sceneNode;            // 0x38 - Ogre scene node
    };

    //=========================================================================
    // STATS AND SKILLS
    //=========================================================================

    // Skill IDs matching Kenshi's internal order
    enum class SkillType : int32_t
    {
        // Combat
        MeleeAttack = 0,
        MeleeDefence = 1,
        Dodge = 2,
        MartialArts = 3,

        // Ranged
        Turrets = 4,
        Crossbows = 5,
        Precision = 6,

        // Thievery
        Stealth = 7,
        Lockpicking = 8,
        Thievery = 9,
        Assassination = 10,

        // Athletic
        Athletics = 11,
        Swimming = 12,

        // Core Stats
        Strength = 13,
        Toughness = 14,
        Dexterity = 15,
        Perception = 16,

        // Science & Engineering
        Science = 17,
        Engineering = 18,
        Robotics = 19,

        // Crafting
        WeaponSmithing = 20,
        ArmourSmithing = 21,

        // Medical
        FieldMedic = 22,
        Cooking = 23,

        // Labor
        Farming = 24,
        Labouring = 25,

        SKILL_COUNT = 26
    };

    // Single skill data
    struct Skill
    {
        float level;                     // Current level (0-100)
        float experience;                // Current XP
        float experienceMultiplier;      // XP gain multiplier
        float levelCap;                  // Max level cap
        uint8_t isLocked;                // Cannot gain XP
        uint8_t padding[3];
    };

    // Complete stats container
    struct CharacterStats
    {
        Skill skills[(int)SkillType::SKILL_COUNT];

        // Derived stats (calculated)
        float combatSpeed;
        float attackSlots;
        float dodgeChance;
        float blockChance;
        float moveSpeed;
        float encumbrance;
        float encumbranceMax;
    };

    //=========================================================================
    // BODY / LIMB SYSTEM
    //=========================================================================

    enum class LimbType : int32_t
    {
        Head = 0,
        Chest = 1,
        Stomach = 2,
        LeftArm = 3,
        RightArm = 4,
        LeftLeg = 5,
        RightLeg = 6,

        LIMB_COUNT = 7
    };

    enum class LimbCondition : int32_t
    {
        Healthy = 0,
        Injured = 1,
        Damaged = 2,
        SeverelyDamaged = 3,
        Crippled = 4,
        Missing = 5
    };

    // Individual body part
    struct BodyPart
    {
        LimbType type;                   // 0x00
        float health;                    // 0x04 - Current health
        float maxHealth;                 // 0x08 - Maximum health
        float bleedRate;                 // 0x0C - Blood loss per second
        float damage;                    // 0x10 - Accumulated damage
        LimbCondition condition;         // 0x14
        uint8_t isBleeding;              // 0x18
        uint8_t isBroken;                // 0x19
        uint8_t isBandaged;              // 0x1A
        uint8_t isSplinted;              // 0x1B
        uint8_t isMissing;               // 0x1C
        uint8_t isRobotic;               // 0x1D - Prosthetic limb
        uint8_t padding[2];              // 0x1E
        Item* equippedArmor;             // 0x20 - Armor on this limb
        float armorCoverage;             // 0x28
        float wearDamage;                // 0x2C - Accumulated wear on armor
    };

    // Body system container
    struct BodySystem
    {
        BodyPart parts[(int)LimbType::LIMB_COUNT];
        float bloodLevel;                // 0-100, death at 0
        float bloodMax;
        float bloodRegenRate;
        float overallHealth;             // Calculated aggregate
        uint8_t isUnconscious;
        uint8_t isDead;
        uint8_t isPlayingDead;
        uint8_t padding;
    };

    //=========================================================================
    // INVENTORY SYSTEM
    //=========================================================================

    enum class ItemCategory : int32_t
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
    };

    enum class WeaponType : int32_t
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
    };

    enum class DamageType : int32_t
    {
        Cut = 0,
        Blunt = 1,
        Pierce = 2
    };

    // Item quality grades
    enum class ItemGrade : int32_t
    {
        Rusted = 0,       // -40%
        Rusting = 1,      // -20%
        Standard = 2,     // 0%
        Catun = 3,        // +10%
        Mk1 = 4,          // +20%
        Mk2 = 5,          // +40%
        Mk3 = 6,          // +60%
        Edge1 = 7,        // +80%
        Edge2 = 8,        // +100%
        Edge3 = 9,        // +120%
        Meitou = 10       // Special unique
    };

    // Single item instance
    struct Item
    {
        void* vtable;                    // 0x00
        uint32_t itemId;                 // 0x08 - Unique instance ID
        uint32_t templateId;             // 0x0C - Item template reference
        OgreString name;                 // 0x10
        ItemCategory category;           // 0x30
        ItemGrade grade;                 // 0x34

        float condition;                 // 0x38 - 0.0 to 1.0
        float maxCondition;              // 0x3C
        float weight;                    // 0x40
        int32_t value;                   // 0x44 - Base sell value
        int32_t stackCount;              // 0x48 - For stackable items
        int32_t maxStack;                // 0x4C

        // Weapon-specific (only if category == Weapon)
        union
        {
            struct
            {
                WeaponType weaponType;
                DamageType damageType;
                float baseDamage;
                float damageMin;
                float damageMax;
                float attackSpeed;
                float defenceBonus;
                float indoorPenalty;
                float reach;
                float bloodLoss;
                uint8_t isTwoHanded;
                uint8_t isRanged;
                uint8_t padding[2];
            } weapon;

            // Armor-specific (only if category == Armor)
            struct
            {
                float cutResist;
                float bluntResist;
                float pierceResist;
                uint32_t coverageMask;   // Bitmask of covered limbs
                float encumbrance;
                float stealthPenalty;
                float combatSpeedPenalty;
            } armor;

            // Food-specific (only if category == Food)
            struct
            {
                float nutrition;
                float hydration;
                float spoilRate;
                float freshness;         // 0.0 = spoiled, 1.0 = fresh
                uint8_t isPerishable;
                uint8_t padding[3];
            } food;
        };

        Character* owner;                // Current owner (if equipped/carried)
        void* containerPtr;              // Parent container
        int32_t slotIndex;               // Inventory slot
    };

    // Inventory slot
    struct InventorySlot
    {
        Item* item;
        int32_t slotIndex;
        uint8_t isLocked;
        uint8_t padding[3];
    };

    // Inventory container
    struct Inventory
    {
        void* vtable;
        Character* owner;
        InventorySlot* slots;            // Array of slots
        int32_t slotCount;
        int32_t maxSlots;
        float totalWeight;
        float maxWeight;
        int32_t totalValue;
        int32_t money;                   // Cats currency
    };

    // Equipment slots
    enum class EquipSlot : int32_t
    {
        Head = 0,
        Chest = 1,
        Legs = 2,
        Boots = 3,
        MainHand = 4,
        OffHand = 5,
        Backpack = 6,

        SLOT_COUNT = 7
    };

    struct Equipment
    {
        Item* slots[(int)EquipSlot::SLOT_COUNT];
        float totalArmorRating;
        float totalEncumbrance;
        float totalCombatPenalty;
    };

    //=========================================================================
    // AI SYSTEM
    //=========================================================================

    enum class AIState : int32_t
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
    };

    enum class AIPackageType : int32_t
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
    };

    // Pathfinding waypoint
    struct PathNode
    {
        Vector3 position;
        float cost;
        int32_t nextIndex;
        uint8_t isWalkable;
        uint8_t isObstructed;
        uint8_t padding[2];
    };

    // Navigation path
    struct NavigationPath
    {
        PathNode* nodes;
        int32_t nodeCount;
        int32_t currentNode;
        float totalDistance;
        float distanceTraveled;
        Vector3 destination;
        uint8_t isValid;
        uint8_t isComplete;
        uint8_t isPaused;
        uint8_t padding;
    };

    // AI target information
    struct AITarget
    {
        GameObject* target;              // Current target object
        Vector3 targetPosition;          // Target location
        float distance;                  // Distance to target
        float lastSeen;                  // Time last seen
        uint8_t isVisible;
        uint8_t isHostile;
        uint8_t isReachable;
        uint8_t priority;
    };

    // AI task/goal
    struct AITask
    {
        AIPackageType type;
        AITarget target;
        float priority;
        float timeStarted;
        float timeout;
        uint8_t isActive;
        uint8_t isInterruptible;
        uint8_t isComplete;
        uint8_t padding;
    };

    // AI Controller (state machine)
    struct AIController
    {
        void* vtable;                    // 0x00
        Character* owner;                // 0x08

        AIState currentState;            // 0x10
        AIState previousState;           // 0x14
        float stateTime;                 // 0x18 - Time in current state

        // Task system
        AITask* taskStack;               // 0x20 - Stack of tasks
        int32_t taskCount;               // 0x28
        int32_t maxTasks;                // 0x2C
        AITask* currentTask;             // 0x30

        // Navigation
        NavigationPath* currentPath;     // 0x38
        Vector3 moveTarget;              // 0x40
        float moveSpeed;                 // 0x4C
        float arrivalRadius;             // 0x50

        // Targeting
        AITarget* targets;               // 0x58 - Potential targets
        int32_t targetCount;             // 0x60
        AITarget* primaryTarget;         // 0x68 - Main focus

        // Combat AI
        float aggressionLevel;           // 0x70 - 0.0 to 1.0
        float fleeThreshold;             // 0x74 - HP% to flee
        float confidenceLevel;           // 0x78 - Battle confidence
        float threatLevel;               // 0x7C - Perceived danger

        // Awareness
        float perceptionRange;           // 0x80
        float hearingRange;              // 0x84
        float alertness;                 // 0x88 - 0.0 to 1.0

        // Behavior flags
        uint8_t isAutonomous;            // 0x8C - AI makes own decisions
        uint8_t isInCombat;              // 0x8D
        uint8_t isAggressive;            // 0x8E
        uint8_t isDefensive;             // 0x8F
        uint8_t isPassive;               // 0x90
        uint8_t isSneaking;              // 0x91
        uint8_t isOnJob;                 // 0x92
        uint8_t padding;                 // 0x93

        // Scheduled jobs
        void* jobSchedule;               // 0x98
        int32_t currentJobIndex;         // 0xA0
    };

    //=========================================================================
    // ANIMATION SYSTEM
    //=========================================================================

    enum class AnimationType : int32_t
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

        // Combat
        AttackLight = 10,
        AttackHeavy = 11,
        AttackCombo = 12,
        Block = 13,
        Dodge = 14,
        Stagger = 15,
        KnockDown = 16,
        GetUp = 17,

        // Interactions
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

        // Death/Injury
        Die = 40,
        PlayDead = 41,
        Unconscious = 42,
        Crawl = 43,

        ANIM_COUNT = 50
    };

    // Animation state
    struct AnimationState
    {
        void* vtable;                    // 0x00
        Character* owner;                // 0x08

        AnimationType currentAnim;       // 0x10
        AnimationType nextAnim;          // 0x14 - Queued animation

        float animTime;                  // 0x18 - Current time in animation
        float animDuration;              // 0x1C - Total animation length
        float animSpeed;                 // 0x20 - Playback speed multiplier
        float blendWeight;               // 0x24 - For animation blending

        // Skeleton reference (Ogre)
        void* skeleton;                  // 0x28
        void* animController;            // 0x30 - Ogre AnimationController

        // Layered animations
        AnimationType upperBodyAnim;     // 0x38
        AnimationType lowerBodyAnim;     // 0x3C
        float upperBlend;                // 0x40
        float lowerBlend;                // 0x44

        // Combat timing
        float attackStartTime;           // 0x48 - When attack begins
        float attackHitTime;             // 0x4C - When damage applies
        float attackEndTime;             // 0x50 - When attack completes
        float blockWindow;               // 0x54 - Time window to block

        // Flags
        uint8_t isPlaying;               // 0x58
        uint8_t isLooping;               // 0x59
        uint8_t isPaused;                // 0x5A
        uint8_t isBlending;              // 0x5B
        uint8_t canCancel;               // 0x5C
        uint8_t inAttackPhase;           // 0x5D - Currently in attack hitbox
        uint8_t padding[2];              // 0x5E
    };

    //=========================================================================
    // CHARACTER (PLAYER / NPC)
    //=========================================================================

    enum class CharacterType : int32_t
    {
        Human = 0,
        Shek = 1,
        Hive = 2,
        Skeleton = 3,
        Animal = 4,

        TYPE_COUNT = 5
    };

    // The main character structure - this is your "PlayerController"
    struct Character
    {
        // === Base Object (0x00 - 0x3F) ===
        void* vtable;                    // 0x00
        uint32_t characterId;            // 0x08 - Unique character ID
        uint32_t templateId;             // 0x0C - Character template
        OgreString name;                 // 0x10
        uint32_t flags;                  // 0x30
        uint8_t isActive;                // 0x34
        uint8_t isVisible;               // 0x35
        uint8_t isLoaded;                // 0x36
        uint8_t padding1;                // 0x37
        SceneNode* sceneNode;            // 0x38

        // === Identity (0x40 - 0x5F) ===
        CharacterType characterType;     // 0x40
        OgreString raceName;             // 0x44
        void* raceData;                  // 0x64 - Race template pointer
        uint8_t gender;                  // 0x6C - 0=male, 1=female
        uint8_t age;                     // 0x6D
        uint8_t padding2[2];             // 0x6E

        // === Transform (0x70 - 0x9F) ===
        Vector3 position;                // 0x70
        Quaternion rotation;             // 0x7C
        Vector3 velocity;                // 0x8C - Current movement velocity
        float moveSpeed;                 // 0x98
        float baseSpeed;                 // 0x9C

        // === Stats and Skills (0xA0 - 0xBF) ===
        CharacterStats* stats;           // 0xA0
        float* rawSkillLevels;           // 0xA8 - Direct skill array
        int32_t skillCount;              // 0xB0

        // === Body and Health (0xB8 - 0xFF) ===
        BodySystem* body;                // 0xB8
        float health;                    // 0xC0 - Overall health
        float maxHealth;                 // 0xC4
        float bloodLevel;                // 0xC8
        float bloodMax;                  // 0xCC
        float hunger;                    // 0xD0
        float hungerMax;                 // 0xD4
        float hungerRate;                // 0xD8 - Hunger per hour
        float thirst;                    // 0xDC
        float thirstMax;                 // 0xE0
        float thirstRate;                // 0xE4
        float fatigue;                   // 0xE8
        float fatigueMax;                // 0xEC

        // === Inventory and Equipment (0xF0 - 0x10F) ===
        Inventory* inventory;            // 0xF0
        Equipment* equipment;            // 0xF8
        Item* mainWeapon;                // 0x100 - Currently equipped weapon
        Item* offWeapon;                 // 0x108

        // === AI and Control (0x110 - 0x14F) ===
        AIController* aiController;      // 0x110
        AIState currentState;            // 0x118
        void* currentAction;             // 0x120 - Current action object
        void* targetObject;              // 0x128 - Current target
        Vector3 targetPosition;          // 0x130

        // === Animation (0x140 - 0x15F) ===
        AnimationState* animState;       // 0x140
        void* ragdoll;                   // 0x148 - Physics ragdoll
        void* modelInstance;             // 0x150 - Ogre Entity

        // === Faction and Social (0x158 - 0x17F) ===
        int32_t factionId;               // 0x158
        Faction* faction;                // 0x160
        Squad* squad;                    // 0x168
        Character* squad_leader;         // 0x170 - If in a squad
        void* dialogueState;             // 0x178

        // === Player Control (0x180 - 0x1AF) ===
        void* playerOwner;               // 0x180 - Player struct if controlled
        uint8_t isPlayerControlled;      // 0x188
        uint8_t isSelected;              // 0x189
        uint8_t isUnconscious;           // 0x18A
        uint8_t isDead;                  // 0x18B
        uint8_t isPlayingDead;           // 0x18C
        uint8_t isInCombat;              // 0x18D
        uint8_t isHostile;               // 0x18E
        uint8_t isSneaking;              // 0x18F
        uint8_t isCarrying;              // 0x190 - Carrying another character
        uint8_t isBeingCarried;          // 0x191
        uint8_t isCrippled;              // 0x192
        uint8_t isEating;                // 0x193
        uint8_t isSleeping;              // 0x194
        uint8_t isWorking;               // 0x195
        uint8_t padding3[2];             // 0x196

        // === Combat Data (0x198 - 0x1CF) ===
        Character* combatTarget;         // 0x198
        float combatTimer;               // 0x1A0
        float lastAttackTime;            // 0x1A4
        float attackCooldown;            // 0x1A8
        float blockCooldown;             // 0x1AC
        float dodgeCooldown;             // 0x1B0
        int32_t comboCounter;            // 0x1B4
        float damageDealt;               // 0x1B8
        float damageTaken;               // 0x1BC
        int32_t killCount;               // 0x1C0
        int32_t knockoutCount;           // 0x1C4

        // === World Cell / Location (0x1D0 - 0x1FF) ===
        WorldCell* currentCell;          // 0x1D0
        int32_t cellX;                   // 0x1D8
        int32_t cellY;                   // 0x1DC
        Building* currentBuilding;       // 0x1E0 - If inside a building
        void* currentZone;               // 0x1E8 - Zone/biome info

        // === Bounty System (0x1F0 - 0x20F) ===
        int32_t bounty;                  // 0x1F0 - Criminal bounty
        int32_t* factionBounties;        // 0x1F8 - Per-faction bounties
        int32_t bountyFactionCount;      // 0x200
    };

    //=========================================================================
    // SQUAD SYSTEM
    //=========================================================================

    enum class SquadOrder : int32_t
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
    };

    enum class FormationType : int32_t
    {
        None = 0,
        Line = 1,
        Column = 2,
        Wedge = 3,
        Circle = 4,
        Scattered = 5,

        FORMATION_COUNT = 6
    };

    // Squad container - acts like a GameState for groups
    struct Squad
    {
        void* vtable;                    // 0x00
        uint32_t squadId;                // 0x08
        uint32_t padding;                // 0x0C
        OgreString name;                 // 0x10

        // Members
        Character** members;             // 0x30 - Array of character pointers
        int32_t memberCount;             // 0x38
        int32_t maxMembers;              // 0x3C
        Character* leader;               // 0x40

        // Faction
        int32_t factionId;               // 0x48
        Faction* faction;                // 0x50

        // Orders
        SquadOrder currentOrder;         // 0x58
        Vector3 orderTarget;             // 0x5C
        Character* targetEnemy;          // 0x68
        Building* targetBuilding;        // 0x70

        // Formation
        FormationType formation;         // 0x78
        float formationSpacing;          // 0x7C

        // Pathfinding
        NavigationPath* squadPath;       // 0x80
        Vector3 destination;             // 0x88
        float pathfindingRadius;         // 0x94

        // AI package for squad-level AI
        void* squadAI;                   // 0x98
        float threatLevel;               // 0xA0
        uint8_t isInCombat;              // 0xA4
        uint8_t isPlayerSquad;           // 0xA5
        uint8_t isMoving;                // 0xA6
        uint8_t padding2;                // 0xA7
    };

    //=========================================================================
    // FACTION SYSTEM
    //=========================================================================

    enum class FactionRelation : int32_t
    {
        Allied = 100,
        Friendly = 50,
        Neutral = 0,
        Unfriendly = -50,
        Hostile = -100
    };

    enum class FactionType : int32_t
    {
        Player = 0,
        Major = 1,
        Minor = 2,
        Bandit = 3,
        Animal = 4,
        Neutral = 5,

        FACTION_TYPE_COUNT = 6
    };

    // Faction data
    struct Faction
    {
        void* vtable;                    // 0x00
        uint32_t factionId;              // 0x08
        uint32_t padding;                // 0x0C
        OgreString name;                 // 0x10

        FactionType type;                // 0x30

        // Relations array (indexed by faction ID)
        int32_t* relations;              // 0x38
        int32_t relationCount;           // 0x40

        // Members
        Character** members;             // 0x48
        int32_t memberCount;             // 0x50

        Squad** squads;                  // 0x58
        int32_t squadCount;              // 0x60

        // Leader
        Character* leader;               // 0x68

        // Territory
        void* territory;                 // 0x70 - Territory zones
        int32_t territoryCount;          // 0x78

        // Economy
        int32_t wealth;                  // 0x7C
        int32_t population;              // 0x80

        // Flags
        uint8_t isPlayerFaction;         // 0x84
        uint8_t isHostileToPlayer;       // 0x85
        uint8_t canRecruit;              // 0x86
        uint8_t isActive;                // 0x87
        uint8_t isHidden;                // 0x88
        uint8_t padding2[3];             // 0x89
    };

    //=========================================================================
    // WORLD SYSTEM
    //=========================================================================

    enum class WeatherType : int32_t
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
    };

    // Weather system
    struct WeatherSystem
    {
        void* vtable;                    // 0x00
        WeatherType currentWeather;      // 0x08
        WeatherType nextWeather;         // 0x0C
        float transitionProgress;        // 0x10
        float intensity;                 // 0x14
        float temperature;               // 0x18
        float windSpeed;                 // 0x1C
        float windDirection;             // 0x20
        Vector3 windVector;              // 0x24
        int32_t timeOfDay;               // 0x30 - Hour 0-23
        float dayNightCycle;             // 0x34 - 0.0 to 1.0
    };

    // World cell/zone
    struct WorldCell
    {
        void* vtable;                    // 0x00
        int32_t cellX;                   // 0x08
        int32_t cellY;                   // 0x0C
        OgreString zoneName;             // 0x10
        OgreString biomeName;            // 0x30

        // Bounds
        Vector3 minBound;                // 0x50
        Vector3 maxBound;                // 0x5C

        // Entities in cell
        Character** characters;          // 0x68
        int32_t characterCount;          // 0x70
        Building** buildings;            // 0x78
        int32_t buildingCount;           // 0x80
        Item** groundItems;              // 0x88
        int32_t groundItemCount;         // 0x90

        // Environment
        float dangerLevel;               // 0x94
        float resourceLevel;             // 0x98

        // Navigation
        NavigationMesh* navMesh;         // 0xA0

        // Flags
        uint8_t isLoaded;                // 0xA8
        uint8_t isActive;                // 0xA9
        uint8_t hasHostiles;             // 0xAA
        uint8_t padding;                 // 0xAB
    };

    // Navigation mesh
    struct NavigationMesh
    {
        void* vtable;
        Vector3* vertices;
        int32_t vertexCount;
        int32_t* triangles;
        int32_t triangleCount;
        float cellSize;
        float agentRadius;
        float agentHeight;
        uint8_t isBuilt;
        uint8_t padding[3];
    };

    //=========================================================================
    // GAME WORLD / GLOBAL STATE
    //=========================================================================

    // Main game world - This is your "GameState" container
    struct GameWorld
    {
        void* vtable;                    // 0x00

        // Time
        float gameTime;                  // 0x08 - Time of day (0.0 - 24.0)
        int32_t gameDay;                 // 0x0C - Current day
        int32_t gameYear;                // 0x10
        float timeScale;                 // 0x14 - Game speed multiplier
        float realTime;                  // 0x18 - Real seconds since start

        // Weather
        WeatherSystem* weather;          // 0x20

        // Global lists
        Character** allCharacters;       // 0x28
        int32_t characterCount;          // 0x30
        Squad** allSquads;               // 0x38
        int32_t squadCount;              // 0x40
        Faction** allFactions;           // 0x48
        int32_t factionCount;            // 0x50
        Building** allBuildings;         // 0x58
        int32_t buildingCount;           // 0x60
        Item** worldItems;               // 0x68 - Items on ground
        int32_t worldItemCount;          // 0x70

        // Player data
        Squad* playerSquad;              // 0x78 - Main player squad
        Character** selectedCharacters;  // 0x80
        int32_t selectedCount;           // 0x88
        Faction* playerFaction;          // 0x90
        int32_t playerMoney;             // 0x98

        // World grid
        WorldCell** cells;               // 0xA0
        int32_t cellCountX;              // 0xA8
        int32_t cellCountY;              // 0xAC
        WorldCell* activeCell;           // 0xB0 - Player's current cell

        // Physics
        void* physicsWorld;              // 0xB8
        void* collisionWorld;            // 0xC0

        // AI Director
        void* aiDirector;                // 0xC8
        float globalThreatLevel;         // 0xD0
        float globalActivityLevel;       // 0xD4

        // Object managers
        void* characterManager;          // 0xD8
        void* squadManager;              // 0xE0
        void* factionManager;            // 0xE8
        void* buildingManager;           // 0xF0
        void* itemManager;               // 0xF8

        // Game state flags
        uint8_t isPaused;                // 0x100
        uint8_t isLoading;               // 0x101
        uint8_t isSaving;                // 0x102
        uint8_t isSimulating;            // 0x103
    };

    //=========================================================================
    // BUILDING SYSTEM
    //=========================================================================

    enum class BuildingState : int32_t
    {
        Blueprint = 0,
        UnderConstruction = 1,
        Complete = 2,
        Damaged = 3,
        Destroyed = 4,

        STATE_COUNT = 5
    };

    enum class BuildingType : int32_t
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
    };

    struct Building
    {
        void* vtable;                    // 0x00
        uint32_t buildingId;             // 0x08
        uint32_t templateId;             // 0x0C
        OgreString name;                 // 0x10
        BuildingType type;               // 0x30
        BuildingState state;             // 0x34

        // Transform
        Vector3 position;                // 0x38
        float rotation;                  // 0x44 - Y axis rotation

        // Health
        float health;                    // 0x48
        float maxHealth;                 // 0x4C

        // Construction
        float constructionProgress;      // 0x50 - 0.0 to 1.0
        void* requiredMaterials;         // 0x58

        // Ownership
        int32_t ownerFactionId;          // 0x60
        Faction* ownerFaction;           // 0x68
        Squad* assignedSquad;            // 0x70

        // Storage/Inventory
        Inventory* storage;              // 0x78
        int32_t maxStorage;              // 0x80

        // Production
        void* productionQueue;           // 0x88
        float productionProgress;        // 0x90

        // Flags
        uint8_t isPlayerOwned;           // 0x94
        uint8_t isOperational;           // 0x95
        uint8_t requiresPower;           // 0x96
        uint8_t hasPower;                // 0x97
        uint8_t isUnderAttack;           // 0x98
        uint8_t padding[3];              // 0x99

        // Workers
        Character** workers;             // 0xA0
        int32_t workerCount;             // 0xA8
        int32_t maxWorkers;              // 0xAC
    };

    //=========================================================================
    // COMBAT SYSTEM
    //=========================================================================

    enum class AttackType : int32_t
    {
        Light = 0,
        Heavy = 1,
        Power = 2,
        Combo = 3,
        Counter = 4,

        TYPE_COUNT = 5
    };

    struct CombatEvent
    {
        Character* attacker;
        Character* defender;
        AttackType attackType;
        DamageType damageType;
        LimbType targetLimb;

        float damage;
        float damageBlocked;
        float bleedDamage;
        float knockback;

        uint8_t wasBlocked;
        uint8_t wasDodged;
        uint8_t wasParried;
        uint8_t isCritical;
        uint8_t causedKnockdown;
        uint8_t causedStagger;
        uint8_t padding[2];

        float timestamp;
    };

    struct CombatState
    {
        Character* combatant;
        Character* opponent;

        float attackTimer;
        float blockTimer;
        float staggerTimer;
        float recoveryTimer;

        int32_t comboCount;
        int32_t hitsTaken;
        int32_t hitsLanded;
        int32_t blocksSuccessful;

        float totalDamageDealt;
        float totalDamageTaken;

        uint8_t isBlocking;
        uint8_t isAttacking;
        uint8_t isStaggered;
        uint8_t isKnockedDown;
        uint8_t isRecovering;
        uint8_t padding[3];
    };

} // namespace Kenshi

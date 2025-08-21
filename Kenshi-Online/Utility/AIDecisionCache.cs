using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.IO;
using System.Runtime.InteropServices;
using KenshiMultiplayer.Common;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Makes Kenshi's AI decisions deterministic by caching and synchronizing them
    /// </summary>
    public class AIDecisionCache
    {
        // Kenshi AI memory offsets (from RE_Kenshi research)
        private const long AI_GOAL_MANAGER_OFFSET = 0x1B4C890;
        private const long AI_PACKAGE_OFFSET = 0x1B4C920;
        private const long SQUAD_AI_OFFSET = 0x1C2A100;

        // AI decision storage
        private readonly ConcurrentDictionary<ulong, CachedDecision> decisionCache = new ConcurrentDictionary<ulong, CachedDecision>();
        private readonly ConcurrentDictionary<string, AIPackageTemplate> packageTemplates = new ConcurrentDictionary<string, AIPackageTemplate>();
        private readonly ConcurrentDictionary<string, SquadBehavior> squadBehaviors = new ConcurrentDictionary<string, SquadBehavior>();

        // Decision history for learning
        private readonly CircularBuffer<DecisionRecord> decisionHistory = new CircularBuffer<DecisionRecord>(10000);

        // Random seed for deterministic randomness
        private readonly Random deterministicRandom;
        private readonly int globalSeed;

        public AIDecisionCache(int seed = 12345)
        {
            globalSeed = seed;
            deterministicRandom = new Random(seed);

            InitializePackageTemplates();
            LoadCachedDecisions();
        }

        /// <summary>
        /// Initialize standard AI package templates from Kenshi
        /// </summary>
        private void InitializePackageTemplates()
        {
            // Combat packages
            packageTemplates["Attack_Enemies"] = new AIPackageTemplate
            {
                Name = "Attack_Enemies",
                Priority = 0, // Urgent
                Goals = new List<AIGoal>
                {
                    new AIGoal { Type = "FindTarget", Weight = 1.0f },
                    new AIGoal { Type = "MoveToTarget", Weight = 0.9f },
                    new AIGoal { Type = "AttackTarget", Weight = 1.0f }
                },
                TransitionConditions = new List<string> { "EnemyInRange", "HealthAbove20" }
            };

            // Medic behavior
            packageTemplates["Medic"] = new AIPackageTemplate
            {
                Name = "Medic",
                Priority = 0,
                Goals = new List<AIGoal>
                {
                    new AIGoal { Type = "FindInjured", Weight = 1.0f },
                    new AIGoal { Type = "MoveToInjured", Weight = 0.9f },
                    new AIGoal { Type = "HealTarget", Weight = 1.0f }
                },
                TransitionConditions = new List<string> { "AllyInjured", "HasMedkit" }
            };

            // Patrol behavior
            packageTemplates["Patrol"] = new AIPackageTemplate
            {
                Name = "Patrol",
                Priority = 2,
                Goals = new List<AIGoal>
                {
                    new AIGoal { Type = "MoveToWaypoint", Weight = 1.0f },
                    new AIGoal { Type = "WaitAtWaypoint", Weight = 0.3f },
                    new AIGoal { Type = "ScanForEnemies", Weight = 0.5f }
                },
                TransitionConditions = new List<string> { "NoEnemiesNearby", "PatrolRoute" }
            };

            // Trade caravan
            packageTemplates["TradeCaravan"] = new AIPackageTemplate
            {
                Name = "TradeCaravan",
                Priority = 1,
                Goals = new List<AIGoal>
                {
                    new AIGoal { Type = "MoveToCity", Weight = 1.0f },
                    new AIGoal { Type = "Trade", Weight = 0.8f },
                    new AIGoal { Type = "RestockSupplies", Weight = 0.6f }
                },
                TransitionConditions = new List<string> { "HasTradeGoods", "CityInRange" }
            };

            // Flee behavior
            packageTemplates["Flee"] = new AIPackageTemplate
            {
                Name = "Flee",
                Priority = 0,
                Goals = new List<AIGoal>
                {
                    new AIGoal { Type = "FindSafeLocation", Weight = 1.0f },
                    new AIGoal { Type = "RunAway", Weight = 1.0f }
                },
                TransitionConditions = new List<string> { "HealthBelow30", "Outnumbered" }
            };

            Logger.Log($"Initialized {packageTemplates.Count} AI package templates");
        }

        /// <summary>
        /// Get deterministic AI decision for given state
        /// </summary>
        public CachedDecision GetDecision(AIState state)
        {
            // Generate state hash
            ulong stateHash = GenerateStateHash(state);

            // Check cache
            if (decisionCache.TryGetValue(stateHash, out var cached))
            {
                cached.UseCount++;
                return cached;
            }

            // Generate new deterministic decision
            var decision = GenerateDecision(state);

            // Cache it
            decisionCache[stateHash] = decision;

            // Record for learning
            RecordDecision(state, decision);

            // Broadcast to other clients
            BroadcastDecision(decision);

            return decision;
        }

        /// <summary>
        /// Generate deterministic decision for uncached state
        /// </summary>
        private CachedDecision GenerateDecision(AIState state)
        {
            var decision = new CachedDecision
            {
                StateHash = GenerateStateHash(state),
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            // Evaluate all applicable packages
            var applicablePackages = GetApplicablePackages(state);

            if (applicablePackages.Count == 0)
            {
                // Default idle behavior
                decision.SelectedPackage = "Idle";
                decision.SelectedGoal = "Wait";
                decision.TargetId = null;
                return decision;
            }

            // Select package based on priority and deterministic randomness
            var selectedPackage = SelectPackageDeterministically(applicablePackages, state);
            decision.SelectedPackage = selectedPackage.Name;

            // Select goal within package
            var selectedGoal = SelectGoalDeterministically(selectedPackage, state);
            decision.SelectedGoal = selectedGoal.Type;

            // Determine target
            decision.TargetId = DetermineTarget(selectedGoal, state);

            // Calculate action parameters
            decision.Parameters = CalculateParameters(selectedGoal, state, decision.TargetId);

            return decision;
        }

        /// <summary>
        /// Get packages that apply to current state
        /// </summary>
        private List<AIPackageTemplate> GetApplicablePackages(AIState state)
        {
            var applicable = new List<AIPackageTemplate>();

            foreach (var package in packageTemplates.Values)
            {
                if (EvaluateConditions(package.TransitionConditions, state))
                {
                    applicable.Add(package);
                }
            }

            // Sort by priority
            applicable.Sort((a, b) => a.Priority.CompareTo(b.Priority));

            return applicable;
        }

        /// <summary>
        /// Evaluate if conditions are met
        /// </summary>
        private bool EvaluateConditions(List<string> conditions, AIState state)
        {
            foreach (var condition in conditions)
            {
                if (!EvaluateCondition(condition, state))
                    return false;
            }
            return true;
        }

        /// <summary>
        /// Evaluate single condition
        /// </summary>
        private bool EvaluateCondition(string condition, AIState state)
        {
            switch (condition)
            {
                case "EnemyInRange":
                    return state.NearbyEnemies.Count > 0;

                case "HealthAbove20":
                    return state.Health > 20;

                case "HealthBelow30":
                    return state.Health < 30;

                case "AllyInjured":
                    return state.NearbyAllies.Any(a => a.Health < 50);

                case "HasMedkit":
                    return state.Inventory.ContainsKey("medkit");

                case "Outnumbered":
                    return state.NearbyEnemies.Count > state.NearbyAllies.Count + 1;

                case "NoEnemiesNearby":
                    return state.NearbyEnemies.Count == 0;

                case "HasTradeGoods":
                    return state.Inventory.Any(i => i.Value > 0);

                case "CityInRange":
                    return state.DistanceToNearestCity < 10000;

                default:
                    return true;
            }
        }

        /// <summary>
        /// Select package using deterministic randomness
        /// </summary>
        private AIPackageTemplate SelectPackageDeterministically(List<AIPackageTemplate> packages, AIState state)
        {
            if (packages.Count == 1)
                return packages[0];

            // Use state hash as seed for consistent selection
            var stateRandom = new Random((int)(GenerateStateHash(state) % int.MaxValue));

            // Weight packages by priority
            var weights = packages.Select(p => 1.0f / (p.Priority + 1)).ToList();
            var totalWeight = weights.Sum();

            var roll = stateRandom.NextDouble() * totalWeight;
            var current = 0.0;

            for (int i = 0; i < packages.Count; i++)
            {
                current += weights[i];
                if (roll <= current)
                    return packages[i];
            }

            return packages[0];
        }

        /// <summary>
        /// Select goal within package deterministically
        /// </summary>
        private AIGoal SelectGoalDeterministically(AIPackageTemplate package, AIState state)
        {
            if (package.Goals.Count == 1)
                return package.Goals[0];

            // Use state hash for consistency
            var stateRandom = new Random((int)(GenerateStateHash(state) % int.MaxValue));

            // Weight-based selection
            var totalWeight = package.Goals.Sum(g => g.Weight);
            var roll = stateRandom.NextDouble() * totalWeight;
            var current = 0.0;

            foreach (var goal in package.Goals)
            {
                current += goal.Weight;
                if (roll <= current)
                    return goal;
            }

            return package.Goals[0];
        }

        /// <summary>
        /// Determine target for AI action
        /// </summary>
        private string DetermineTarget(AIGoal goal, AIState state)
        {
            switch (goal.Type)
            {
                case "FindTarget":
                case "AttackTarget":
                    return FindNearestEnemy(state);

                case "FindInjured":
                case "HealTarget":
                    return FindMostInjuredAlly(state);

                case "MoveToWaypoint":
                    return GetNextWaypoint(state);

                case "MoveToCity":
                    return FindNearestCity(state);

                case "RunAway":
                    return FindSafeLocation(state);

                default:
                    return null;
            }
        }

        /// <summary>
        /// Calculate action parameters
        /// </summary>
        private Dictionary<string, object> CalculateParameters(AIGoal goal, AIState state, string targetId)
        {
            var parameters = new Dictionary<string, object>();

            switch (goal.Type)
            {
                case "AttackTarget":
                    parameters["attackType"] = DetermineAttackType(state);
                    parameters["targetLimb"] = DetermineTargetLimb(state);
                    break;

                case "MoveToTarget":
                case "MoveToWaypoint":
                    parameters["speed"] = DetermineMovementSpeed(state);
                    parameters["formation"] = DetermineFormation(state);
                    break;

                case "Trade":
                    parameters["tradeItems"] = DetermineTradeItems(state);
                    parameters["priceModifier"] = CalculatePriceModifier(state);
                    break;
            }

            return parameters;
        }

        // Helper methods for target/parameter determination

        private string FindNearestEnemy(AIState state)
        {
            if (state.NearbyEnemies.Count == 0)
                return null;

            return state.NearbyEnemies
                .OrderBy(e => Vector3.Distance(state.Position, e.Position))
                .First().Id;
        }

        private string FindMostInjuredAlly(AIState state)
        {
            if (state.NearbyAllies.Count == 0)
                return null;

            return state.NearbyAllies
                .Where(a => a.Health < 100)
                .OrderBy(a => a.Health)
                .FirstOrDefault()?.Id;
        }

        private string GetNextWaypoint(AIState state)
        {
            // Deterministic waypoint selection based on entity ID
            var waypointIndex = Math.Abs(state.EntityId.GetHashCode()) % 5;
            return $"waypoint_{waypointIndex}";
        }

        private string FindNearestCity(AIState state)
        {
            // Return nearest city based on position
            var cities = new[] { "Hub", "Squin", "Stack", "Admag", "Shark" };
            var cityIndex = Math.Abs(state.Position.GetHashCode()) % cities.Length;
            return cities[cityIndex];
        }

        private string FindSafeLocation(AIState state)
        {
            // Calculate safe direction away from enemies
            if (state.NearbyEnemies.Count == 0)
                return "nearest_city";

            // Average enemy position
            var avgEnemyX = state.NearbyEnemies.Average(e => e.Position.X);
            var avgEnemyY = state.NearbyEnemies.Average(e => e.Position.Y);

            // Run opposite direction
            var safeX = state.Position.X * 2 - avgEnemyX;
            var safeY = state.Position.Y * 2 - avgEnemyY;

            return $"location_{safeX:F0}_{safeY:F0}";
        }

        private string DetermineAttackType(AIState state)
        {
            // Based on weapon and situation
            if (state.EquippedWeapon?.Contains("heavy") == true)
                return "heavy_swing";

            if (state.Health < 30)
                return "defensive";

            return "normal_attack";
        }

        private string DetermineTargetLimb(AIState state)
        {
            // Deterministic limb selection
            var limbs = new[] { "Head", "Chest", "LeftArm", "RightArm", "LeftLeg", "RightLeg", "Stomach" };
            var limbIndex = Math.Abs(state.EntityId.GetHashCode() + state.TargetId?.GetHashCode() ?? 0) % limbs.Length;
            return limbs[limbIndex];
        }

        private float DetermineMovementSpeed(AIState state)
        {
            if (state.Health < 30)
                return 2.0f; // Limp

            if (state.NearbyEnemies.Count > 0)
                return 8.0f; // Run

            return 5.0f; // Walk
        }

        private string DetermineFormation(AIState state)
        {
            if (state.SquadSize > 5)
                return "spread";

            if (state.NearbyEnemies.Count > 0)
                return "combat";

            return "column";
        }

        private List<string> DetermineTradeItems(AIState state)
        {
            // Trade high-value items
            return state.Inventory
                .Where(i => i.Value > 100)
                .Select(i => i.Key)
                .Take(5)
                .ToList();
        }

        private float CalculatePriceModifier(AIState state)
        {
            // Based on relations and skills
            var baseModifier = 1.0f;

            if (state.FactionRelation > 50)
                baseModifier *= 0.9f; // Discount for good relations

            if (state.Skills.ContainsKey("Trading"))
                baseModifier *= (1.0f - state.Skills["Trading"] / 200.0f);

            return baseModifier;
        }

        /// <summary>
        /// Generate hash for AI state
        /// </summary>
        private ulong GenerateStateHash(AIState state)
        {
            unchecked
            {
                ulong hash = 17;

                // Core state
                hash = hash * 31 + (ulong)state.EntityId.GetHashCode();
                hash = hash * 31 + (ulong)state.Health;
                hash = hash * 31 + (ulong)state.Position.GetHashCode();

                // Nearby entities
                hash = hash * 31 + (ulong)state.NearbyEnemies.Count;
                hash = hash * 31 + (ulong)state.NearbyAllies.Count;

                // Context
                if (!string.IsNullOrEmpty(state.CurrentPackage))
                    hash = hash * 31 + (ulong)state.CurrentPackage.GetHashCode();

                if (!string.IsNullOrEmpty(state.TargetId))
                    hash = hash * 31 + (ulong)state.TargetId.GetHashCode();

                return hash;
            }
        }

        /// <summary>
        /// Record decision for learning
        /// </summary>
        private void RecordDecision(AIState state, CachedDecision decision)
        {
            decisionHistory.Add(new DecisionRecord
            {
                State = state,
                Decision = decision,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            });
        }

        /// <summary>
        /// Broadcast decision to other clients
        /// </summary>
        private void BroadcastDecision(CachedDecision decision)
        {
            // This would integrate with your network layer
            var message = new GameMessage
            {
                Type = "ai_decision",
                Data = new Dictionary<string, object>
                {
                    { "decision", JsonSerializer.Serialize(decision) }
                }
            };

            // Send through server
        }

        /// <summary>
        /// Squad-level behavior coordination
        /// </summary>
        public SquadDecision GetSquadDecision(SquadState squadState)
        {
            var squadHash = GenerateSquadHash(squadState);

            // Check if we have cached squad behavior
            if (squadBehaviors.TryGetValue(squadState.SquadId, out var behavior))
            {
                return ApplySquadBehavior(behavior, squadState);
            }

            // Generate new squad behavior
            var decision = GenerateSquadDecision(squadState);

            // Cache it
            var newBehavior = new SquadBehavior
            {
                SquadId = squadState.SquadId,
                Formation = decision.Formation,
                Objective = decision.Objective,
                LastUpdate = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            squadBehaviors[squadState.SquadId] = newBehavior;

            return decision;
        }

        /// <summary>
        /// Generate squad-level decision
        /// </summary>
        private SquadDecision GenerateSquadDecision(SquadState state)
        {
            var decision = new SquadDecision
            {
                SquadId = state.SquadId
            };

            // Determine squad objective
            if (state.AverageHealth < 30)
            {
                decision.Objective = "Retreat";
                decision.Formation = "Scatter";
            }
            else if (state.EnemiesNearby > state.Members.Count * 2)
            {
                decision.Objective = "DefensiveStand";
                decision.Formation = "Circle";
            }
            else if (state.HasTradeGoods)
            {
                decision.Objective = "TradeRoute";
                decision.Formation = "Column";
            }
            else
            {
                decision.Objective = "Patrol";
                decision.Formation = "Line";
            }

            // Assign roles to squad members
            decision.MemberRoles = AssignSquadRoles(state, decision.Objective);

            return decision;
        }

        /// <summary>
        /// Assign roles to squad members
        /// </summary>
        private Dictionary<string, string> AssignSquadRoles(SquadState state, string objective)
        {
            var roles = new Dictionary<string, string>();

            switch (objective)
            {
                case "Retreat":
                    foreach (var member in state.Members)
                    {
                        roles[member.Id] = member.Health < 20 ? "CarryWounded" : "CoverRetreat";
                    }
                    break;

                case "DefensiveStand":
                    var tanks = state.Members.OrderByDescending(m => m.Armor).Take(2);
                    var medics = state.Members.Where(m => m.HasMedkit).Take(1);

                    foreach (var member in state.Members)
                    {
                        if (tanks.Contains(member))
                            roles[member.Id] = "Tank";
                        else if (medics.Contains(member))
                            roles[member.Id] = "Medic";
                        else
                            roles[member.Id] = "DPS";
                    }
                    break;

                case "TradeRoute":
                    roles[state.Members[0].Id] = "Leader";
                    for (int i = 1; i < state.Members.Count; i++)
                    {
                        roles[state.Members[i].Id] = i < state.Members.Count / 2 ? "Scout" : "Guard";
                    }
                    break;

                default:
                    foreach (var member in state.Members)
                    {
                        roles[member.Id] = "Follow";
                    }
                    break;
            }

            return roles;
        }

        private SquadDecision ApplySquadBehavior(SquadBehavior behavior, SquadState state)
        {
            return new SquadDecision
            {
                SquadId = state.SquadId,
                Formation = behavior.Formation,
                Objective = behavior.Objective,
                MemberRoles = AssignSquadRoles(state, behavior.Objective)
            };
        }

        private ulong GenerateSquadHash(SquadState state)
        {
            unchecked
            {
                ulong hash = 17;
                hash = hash * 31 + (ulong)state.SquadId.GetHashCode();
                hash = hash * 31 + (ulong)state.Members.Count;
                hash = hash * 31 + (ulong)state.AverageHealth;
                hash = hash * 31 + (ulong)state.EnemiesNearby;
                return hash;
            }
        }

        /// <summary>
        /// Save cached decisions to disk
        /// </summary>
        public void SaveCache()
        {
            try
            {
                var cacheData = new AIDecisionCacheData
                {
                    Decisions = decisionCache.Values.ToList(),
                    PackageTemplates = packageTemplates.Values.ToList(),
                    SquadBehaviors = squadBehaviors.Values.ToList()
                };

                string json = JsonSerializer.Serialize(cacheData, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText("ai_cache.json", json);

                Logger.Log($"Saved {decisionCache.Count} AI decisions to cache");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving AI cache: {ex.Message}");
            }
        }

        /// <summary>
        /// Load cached decisions from disk
        /// </summary>
        private void LoadCachedDecisions()
        {
            try
            {
                if (File.Exists("ai_cache.json"))
                {
                    string json = File.ReadAllText("ai_cache.json");
                    var cacheData = JsonSerializer.Deserialize<AIDecisionCacheData>(json);

                    if (cacheData != null)
                    {
                        foreach (var decision in cacheData.Decisions)
                        {
                            decisionCache[decision.StateHash] = decision;
                        }

                        foreach (var behavior in cacheData.SquadBehaviors)
                        {
                            squadBehaviors[behavior.SquadId] = behavior;
                        }

                        Logger.Log($"Loaded {decisionCache.Count} AI decisions from cache");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading AI cache: {ex.Message}");
            }
        }

        /// <summary>
        /// Inject AI decision hooks into game
        /// </summary>
        public void InjectAIHooks(IntPtr processHandle)
        {
            // Hook AI goal manager
            IntPtr aiGoalAddress = IntPtr.Add(processHandle, (int)AI_GOAL_MANAGER_OFFSET);
            // Install hook similar to PathInjector

            // Hook squad AI
            IntPtr squadAiAddress = IntPtr.Add(processHandle, (int)SQUAD_AI_OFFSET);
            // Install hook

            Logger.Log("AI decision hooks installed");
        }
    }

    // Supporting classes

    public class AIState
    {
        public string EntityId { get; set; }
        public int Health { get; set; }
        public Vector3 Position { get; set; }
        public List<EntityInfo> NearbyEnemies { get; set; } = new List<EntityInfo>();
        public List<EntityInfo> NearbyAllies { get; set; } = new List<EntityInfo>();
        public Dictionary<string, int> Inventory { get; set; } = new Dictionary<string, int>();
        public Dictionary<string, float> Skills { get; set; } = new Dictionary<string, float>();
        public string CurrentPackage { get; set; }
        public string TargetId { get; set; }
        public string EquippedWeapon { get; set; }
        public int SquadSize { get; set; }
        public int FactionRelation { get; set; }
        public float DistanceToNearestCity { get; set; }
    }

    public class EntityInfo
    {
        public string Id { get; set; }
        public Vector3 Position { get; set; }
        public int Health { get; set; }
        public int Armor { get; set; }
        public bool HasMedkit { get; set; }
    }

    public class CachedDecision
    {
        public ulong StateHash { get; set; }
        public string SelectedPackage { get; set; }
        public string SelectedGoal { get; set; }
        public string TargetId { get; set; }
        public Dictionary<string, object> Parameters { get; set; } = new Dictionary<string, object>();
        public long Timestamp { get; set; }
        public int UseCount { get; set; }
    }

    public class AIPackageTemplate
    {
        public string Name { get; set; }
        public int Priority { get; set; }
        public List<AIGoal> Goals { get; set; } = new List<AIGoal>();
        public List<string> TransitionConditions { get; set; } = new List<string>();
    }

    public class AIGoal
    {
        public string Type { get; set; }
        public float Weight { get; set; }
    }

    public class SquadState
    {
        public string SquadId { get; set; }
        public List<EntityInfo> Members { get; set; } = new List<EntityInfo>();
        public int AverageHealth { get; set; }
        public int EnemiesNearby { get; set; }
        public bool HasTradeGoods { get; set; }
        public Vector3 CenterPosition { get; set; }
    }

    public class SquadDecision
    {
        public string SquadId { get; set; }
        public string Formation { get; set; }
        public string Objective { get; set; }
        public Dictionary<string, string> MemberRoles { get; set; } = new Dictionary<string, string>();
    }

    public class SquadBehavior
    {
        public string SquadId { get; set; }
        public string Formation { get; set; }
        public string Objective { get; set; }
        public long LastUpdate { get; set; }
    }

    public class DecisionRecord
    {
        public AIState State { get; set; }
        public CachedDecision Decision { get; set; }
        public long Timestamp { get; set; }
    }

    public class AIDecisionCacheData
    {
        public List<CachedDecision> Decisions { get; set; } = new List<CachedDecision>();
        public List<AIPackageTemplate> PackageTemplates { get; set; } = new List<AIPackageTemplate>();
        public List<SquadBehavior> SquadBehaviors { get; set; } = new List<SquadBehavior>();
    }

    public class CircularBuffer<T>
    {
        private readonly T[] buffer;
        private int head = 0;
        private int tail = 0;
        private int count = 0;

        public CircularBuffer(int capacity)
        {
            buffer = new T[capacity];
        }

        public void Add(T item)
        {
            buffer[head] = item;
            head = (head + 1) % buffer.Length;

            if (count < buffer.Length)
                count++;
            else
                tail = (tail + 1) % buffer.Length;
        }

        public IEnumerable<T> GetAll()
        {
            for (int i = 0; i < count; i++)
            {
                yield return buffer[(tail + i) % buffer.Length];
            }
        }
    }
}
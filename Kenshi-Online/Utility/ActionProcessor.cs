using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Handles deterministic processing of player actions with conflict detection and resolution
    /// </summary>
    public class ActionProcessor
    {
        // Action pools for different types
        private readonly ConcurrentDictionary<ActionType, ActionPool> actionPools;
        private readonly ConcurrentQueue<PlayerAction> pendingActions;
        private readonly Dictionary<string, ActionExecutor> executors;
        
        // Processing configuration
        private readonly int tickRate;
        private readonly int batchSize;
        private readonly int maxActionsPerPlayer;
        
        // State management
        private readonly WorldStateManager worldState;
        private readonly PathInjector pathInjector;
        private readonly NetworkManager networkManager;
        
        // Performance monitoring
        private readonly PerformanceMonitor performanceMonitor;
        private readonly AutoTuner autoTuner;
        
        private CancellationTokenSource cancellationTokenSource;
        private Task processingTask;
        private bool isRunning;

        public ActionProcessor(WorldStateManager worldState, PathInjector pathInjector, NetworkManager networkManager, int tickRate = 20, int batchSize = 50)
        {
            this.worldState = worldState;
            this.pathInjector = pathInjector;
            this.networkManager = networkManager;
            this.tickRate = tickRate;
            this.batchSize = batchSize;
            this.maxActionsPerPlayer = 5;
            
            actionPools = new ConcurrentDictionary<ActionType, ActionPool>();
            pendingActions = new ConcurrentQueue<PlayerAction>();
            executors = new Dictionary<string, ActionExecutor>();
            
            performanceMonitor = new PerformanceMonitor();
            autoTuner = new AutoTuner(this);
            
            InitializeActionPools();
            InitializeExecutors();
        }

        /// <summary>
        /// Initialize action pools with priorities
        /// </summary>
        private void InitializeActionPools()
        {
            // Combat has highest priority
            actionPools[ActionType.Combat] = new ActionPool(ActionType.Combat, 0, true);
            
            // Movement is parallel-safe for different players
            actionPools[ActionType.Movement] = new ActionPool(ActionType.Movement, 1, false);
            
            // Interaction needs sequential processing
            actionPools[ActionType.Interaction] = new ActionPool(ActionType.Interaction, 2, true);
            
            // Economy/trading
            actionPools[ActionType.Economy] = new ActionPool(ActionType.Economy, 3, true);
            
            // Building/construction
            actionPools[ActionType.Building] = new ActionPool(ActionType.Building, 4, true);
            
            // Squad management
            actionPools[ActionType.Squad] = new ActionPool(ActionType.Squad, 5, false);
        }

        /// <summary>
        /// Initialize action executors
        /// </summary>
        private void InitializeExecutors()
        {
            executors["move"] = new MoveExecutor(pathInjector, worldState);
            executors["attack"] = new CombatExecutor(worldState);
            executors["interact"] = new InteractionExecutor(worldState);
            executors["trade"] = new TradeExecutor(worldState);
            executors["build"] = new BuildingExecutor(worldState);
            executors["squad"] = new SquadExecutor(worldState);
        }

        /// <summary>
        /// Start action processing loop
        /// </summary>
        public void Start()
        {
            if (isRunning)
                return;
            
            isRunning = true;
            cancellationTokenSource = new CancellationTokenSource();
            processingTask = Task.Run(() => ProcessingLoop(cancellationTokenSource.Token));
            
            Logger.Log($"ActionProcessor started with tick rate {tickRate}Hz");
        }

        /// <summary>
        /// Stop action processing
        /// </summary>
        public void Stop()
        {
            if (!isRunning)
                return;

            isRunning = false;
            cancellationTokenSource?.Cancel();
            processingTask?.Wait(5000);

            Logger.Log("ActionProcessor stopped");
        }

        /// <summary>
        /// Alias for Start() for compatibility
        /// </summary>
        public void StartProcessing() => Start();

        /// <summary>
        /// Alias for Stop() for compatibility
        /// </summary>
        public void StopProcessing() => Stop();

        /// <summary>
        /// Queue an action for processing
        /// </summary>
        public bool QueueAction(PlayerAction action)
        {
            // Validate action
            if (!ValidateAction(action))
            {
                Logger.Log($"Invalid action from {action.PlayerId}: {action.Type}");
                return false;
            }
            
            // Check player action limit
            int playerActionCount = pendingActions.Count(a => a.PlayerId == action.PlayerId);
            if (playerActionCount >= maxActionsPerPlayer)
            {
                Logger.Log($"Player {action.PlayerId} exceeded action limit");
                return false;
            }
            
            // Add timestamp if not set
            if (action.Timestamp == 0)
            {
                action.Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
            
            pendingActions.Enqueue(action);
            return true;
        }

        /// <summary>
        /// Main processing loop
        /// </summary>
        private async Task ProcessingLoop(CancellationToken cancellationToken)
        {
            int tickInterval = 1000 / tickRate; // Milliseconds per tick
            var stopwatch = new Stopwatch();
            
            while (!cancellationToken.IsCancellationRequested)
            {
                stopwatch.Restart();
                
                try
                {
                    // Create batch from pending actions
                    var batch = CreateBatch();
                    
                    if (batch.Count > 0)
                    {
                        // Process the batch
                        var results = await ProcessBatch(batch);
                        
                        // Apply results to world state
                        ApplyResults(results);
                        
                        // Broadcast state updates
                        await BroadcastUpdates(results);
                        
                        // Update performance metrics
                        performanceMonitor.RecordBatch(batch.Count, stopwatch.ElapsedMilliseconds);
                    }
                    
                    // Auto-tune if needed
                    autoTuner.CheckAndAdjust(performanceMonitor);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error in processing loop: {ex.Message}");
                }
                
                // Wait for next tick
                int elapsed = (int)stopwatch.ElapsedMilliseconds;
                int delay = Math.Max(0, tickInterval - elapsed);
                
                if (delay > 0)
                {
                    await Task.Delay(delay, cancellationToken);
                }
                else
                {
                    Logger.Log($"Processing overrun: {elapsed}ms (target: {tickInterval}ms)");
                }
            }
        }

        /// <summary>
        /// Create a batch of actions to process
        /// </summary>
        private List<PlayerAction> CreateBatch()
        {
            var batch = new List<PlayerAction>();
            
            // Clear pools
            foreach (var pool in actionPools.Values)
            {
                pool.Clear();
            }
            
            // Sort actions into pools
            while (batch.Count < batchSize && pendingActions.TryDequeue(out PlayerAction action))
            {
                ActionType type = DetermineActionType(action);
                if (actionPools.TryGetValue(type, out ActionPool pool))
                {
                    pool.Add(action);
                    batch.Add(action);
                }
            }
            
            return batch;
        }

        /// <summary>
        /// Process a batch of actions
        /// </summary>
        private async Task<List<ActionResult>> ProcessBatch(List<PlayerAction> batch)
        {
            var results = new List<ActionResult>();
            
            // Process pools by priority
            var sortedPools = actionPools.Values.OrderBy(p => p.Priority).ToList();
            
            foreach (var pool in sortedPools)
            {
                if (pool.Actions.Count == 0)
                    continue;
                
                // Detect conflicts within pool
                var conflicts = DetectConflicts(pool.Actions);
                
                // Resolve conflicts
                var resolved = ResolveConflicts(pool.Actions, conflicts);
                
                // Execute actions
                if (pool.RequiresSequential)
                {
                    // Sequential execution
                    foreach (var action in resolved)
                    {
                        var result = await ExecuteAction(action);
                        results.Add(result);
                    }
                }
                else
                {
                    // Parallel execution
                    var tasks = resolved.Select(ExecuteAction);
                    var poolResults = await Task.WhenAll(tasks);
                    results.AddRange(poolResults);
                }
            }
            
            return results;
        }

        /// <summary>
        /// Detect conflicts between actions
        /// </summary>
        private List<ActionConflict> DetectConflicts(List<PlayerAction> actions)
        {
            var conflicts = new List<ActionConflict>();
            
            for (int i = 0; i < actions.Count; i++)
            {
                for (int j = i + 1; j < actions.Count; j++)
                {
                    var conflict = CheckConflict(actions[i], actions[j]);
                    if (conflict != null)
                    {
                        conflicts.Add(conflict);
                    }
                }
            }
            
            return conflicts;
        }

        /// <summary>
        /// Check if two actions conflict
        /// </summary>
        private ActionConflict CheckConflict(PlayerAction a1, PlayerAction a2)
        {
            // Same target conflicts
            if (a1.TargetId == a2.TargetId && !string.IsNullOrEmpty(a1.TargetId))
            {
                return new ActionConflict
                {
                    Action1 = a1,
                    Action2 = a2,
                    Type = ConflictType.SameTarget
                };
            }
            
            // Same position conflicts (for building/placement)
            if (a1.Type == "build" && a2.Type == "build")
            {
                var pos1 = a1.GetData<Vector3>("position");
                var pos2 = a2.GetData<Vector3>("position");
                
                if (Vector3.Distance(pos1, pos2) < 5.0f) // 5 meter radius
                {
                    return new ActionConflict
                    {
                        Action1 = a1,
                        Action2 = a2,
                        Type = ConflictType.SamePosition
                    };
                }
            }
            
            // Resource conflicts (for trading/economy)
            if (a1.Type == "trade" && a2.Type == "trade")
            {
                var item1 = a1.GetData<string>("itemId");
                var item2 = a2.GetData<string>("itemId");
                
                if (item1 == item2)
                {
                    return new ActionConflict
                    {
                        Action1 = a1,
                        Action2 = a2,
                        Type = ConflictType.ResourceConflict
                    };
                }
            }
            
            return null;
        }

        /// <summary>
        /// Resolve conflicts between actions
        /// </summary>
        private List<PlayerAction> ResolveConflicts(List<PlayerAction> actions, List<ActionConflict> conflicts)
        {
            var resolved = new List<PlayerAction>(actions);
            var toRemove = new HashSet<PlayerAction>();
            
            foreach (var conflict in conflicts)
            {
                PlayerAction winner = null;
                PlayerAction loser = null;
                
                switch (conflict.Type)
                {
                    case ConflictType.SameTarget:
                        // First action wins
                        winner = conflict.Action1.Timestamp < conflict.Action2.Timestamp ? 
                                conflict.Action1 : conflict.Action2;
                        loser = winner == conflict.Action1 ? conflict.Action2 : conflict.Action1;
                        break;
                    
                    case ConflictType.SamePosition:
                        // Higher priority player wins (could be based on various factors)
                        winner = GetPriorityAction(conflict.Action1, conflict.Action2);
                        loser = winner == conflict.Action1 ? conflict.Action2 : conflict.Action1;
                        break;
                    
                    case ConflictType.ResourceConflict:
                        // Check who has sufficient resources
                        winner = CheckResourceAvailability(conflict.Action1, conflict.Action2);
                        loser = winner == conflict.Action1 ? conflict.Action2 : conflict.Action1;
                        break;
                }
                
                if (loser != null)
                {
                    toRemove.Add(loser);
                    
                    // Notify player of conflict
                    networkManager.SendToPlayer(loser.PlayerId, new GameMessage
                    {
                        Type = MessageType.Notification,
                        Data = new Dictionary<string, object>
                        {
                            { "message", $"Action conflicted with {winner.PlayerId}" },
                            { "action", loser.Type }
                        }
                    });
                }
            }
            
            // Remove conflicted actions
            resolved.RemoveAll(a => toRemove.Contains(a));
            
            return resolved;
        }

        /// <summary>
        /// Execute a single action
        /// </summary>
        private async Task<ActionResult> ExecuteAction(PlayerAction action)
        {
            var result = new ActionResult
            {
                Action = action,
                Success = false,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
            
            try
            {
                // Find appropriate executor
                if (!executors.TryGetValue(action.Type, out ActionExecutor executor))
                {
                    result.Error = "Unknown action type";
                    return result;
                }
                
                // Execute with timeout
                var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
                result = await executor.Execute(action, cts.Token);
            }
            catch (Exception ex)
            {
                result.Error = ex.Message;
                Logger.Log($"Action execution failed: {ex.Message}");
            }
            
            return result;
        }

        /// <summary>
        /// Apply action results to world state
        /// </summary>
        private void ApplyResults(List<ActionResult> results)
        {
            foreach (var result in results)
            {
                if (!result.Success)
                    continue;
                
                try
                {
                    worldState.ApplyActionResult(result);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Failed to apply result: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Broadcast state updates to clients
        /// </summary>
        private async Task BroadcastUpdates(List<ActionResult> results)
        {
            // Group results by affected players
            var playerUpdates = new Dictionary<string, List<ActionResult>>();
            
            foreach (var result in results)
            {
                if (!playerUpdates.ContainsKey(result.Action.PlayerId))
                {
                    playerUpdates[result.Action.PlayerId] = new List<ActionResult>();
                }
                playerUpdates[result.Action.PlayerId].Add(result);
                
                // Also add to affected players
                if (!string.IsNullOrEmpty(result.Action.TargetId))
                {
                    if (!playerUpdates.ContainsKey(result.Action.TargetId))
                    {
                        playerUpdates[result.Action.TargetId] = new List<ActionResult>();
                    }
                    playerUpdates[result.Action.TargetId].Add(result);
                }
            }
            
            // Send updates to each player
            var tasks = new List<Task>();
            foreach (var kvp in playerUpdates)
            {
                var message = new GameMessage
                {
                    Type = MessageType.WorldState,
                    Data = new Dictionary<string, object>
                    {
                        { "results", kvp.Value },
                        { "timestamp", DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() }
                    }
                };
                
                tasks.Add(networkManager.SendToPlayerAsync(kvp.Key, message));
            }
            
            await Task.WhenAll(tasks);
        }

        /// <summary>
        /// Validate an action before queuing
        /// </summary>
        private bool ValidateAction(PlayerAction action)
        {
            // Check required fields
            if (string.IsNullOrEmpty(action.PlayerId) || string.IsNullOrEmpty(action.Type))
                return false;
            
            // Check player exists
            if (!worldState.PlayerExists(action.PlayerId))
                return false;
            
            // Type-specific validation
            switch (action.Type)
            {
                case "move":
                    return action.Data.ContainsKey("destination");
                
                case "attack":
                    return !string.IsNullOrEmpty(action.TargetId);
                
                case "trade":
                    return action.Data.ContainsKey("itemId") && action.Data.ContainsKey("quantity");
                
                case "build":
                    return action.Data.ContainsKey("buildingType") && action.Data.ContainsKey("position");
                
                default:
                    return true;
            }
        }

        /// <summary>
        /// Determine action type from action string
        /// </summary>
        private ActionType DetermineActionType(PlayerAction action)
        {
            switch (action.Type)
            {
                case "move":
                case "follow":
                case "patrol":
                    return ActionType.Movement;
                
                case "attack":
                case "defend":
                case "block":
                    return ActionType.Combat;
                
                case "interact":
                case "talk":
                case "loot":
                    return ActionType.Interaction;
                
                case "trade":
                case "buy":
                case "sell":
                    return ActionType.Economy;
                
                case "build":
                case "demolish":
                case "repair":
                    return ActionType.Building;
                
                case "squad":
                case "recruit":
                case "dismiss":
                    return ActionType.Squad;
                
                default:
                    return ActionType.Movement;
            }
        }

        /// <summary>
        /// Get priority action based on game rules
        /// </summary>
        private PlayerAction GetPriorityAction(PlayerAction a1, PlayerAction a2)
        {
            // Could be based on faction standing, player level, etc.
            var player1 = worldState.GetPlayer(a1.PlayerId);
            var player2 = worldState.GetPlayer(a2.PlayerId);

            if (player1 == null || player2 == null)
                return a1.Timestamp < a2.Timestamp ? a1 : a2;

            if (player1.Level > player2.Level)
                return a1;
            else if (player2.Level > player1.Level)
                return a2;
            else
                return a1.Timestamp < a2.Timestamp ? a1 : a2;
        }

        /// <summary>
        /// Check resource availability for actions
        /// </summary>
        private PlayerAction CheckResourceAvailability(PlayerAction a1, PlayerAction a2)
        {
            // Check who has the resources
            var player1 = worldState.GetPlayer(a1.PlayerId);
            var player2 = worldState.GetPlayer(a2.PlayerId);

            if (player1 == null || player2 == null)
                return a1.Timestamp < a2.Timestamp ? a1 : a2;

            var item = a1.GetData<string>("itemId");
            var quantity1 = a1.GetData<int>("quantity");
            var quantity2 = a2.GetData<int>("quantity");

            bool p1HasResources = player1.HasItem(item, quantity1);
            bool p2HasResources = player2.HasItem(item, quantity2);

            if (p1HasResources && !p2HasResources)
                return a1;
            else if (p2HasResources && !p1HasResources)
                return a2;
            else
                return a1.Timestamp < a2.Timestamp ? a1 : a2;
        }

        /// <summary>
        /// Get current processing statistics
        /// </summary>
        public ProcessingStatistics GetStatistics()
        {
            return new ProcessingStatistics
            {
                PendingActions = pendingActions.Count,
                ProcessedActions = performanceMonitor.TotalProcessed,
                AverageLatency = performanceMonitor.AverageLatency,
                CurrentTickRate = tickRate,
                CurrentBatchSize = batchSize
            };
        }
    }

    /// <summary>
    /// Action pool for grouping similar actions
    /// </summary>
    public class ActionPool
    {
        public ActionType Type { get; }
        public int Priority { get; }
        public bool RequiresSequential { get; }
        public List<PlayerAction> Actions { get; }

        public ActionPool(ActionType type, int priority, bool requiresSequential)
        {
            Type = type;
            Priority = priority;
            RequiresSequential = requiresSequential;
            Actions = new List<PlayerAction>();
        }

        public void Add(PlayerAction action)
        {
            Actions.Add(action);
        }

        public void Clear()
        {
            Actions.Clear();
        }
    }

    /// <summary>
    /// Player action data
    /// </summary>
    public class PlayerAction
    {
        public string PlayerId { get; set; }
        public string Type { get; set; }
        public string TargetId { get; set; }
        public Dictionary<string, object> Data { get; set; }
        public long Timestamp { get; set; }
        public int Priority { get; set; }

        public T GetData<T>(string key)
        {
            if (Data != null && Data.TryGetValue(key, out object value))
            {
                return (T)Convert.ChangeType(value, typeof(T));
            }
            return default(T);
        }
    }

    /// <summary>
    /// Action execution result
    /// </summary>
    public class ActionResult
    {
        public PlayerAction Action { get; set; }
        public bool Success { get; set; }
        public string Error { get; set; }
        public Dictionary<string, object> Changes { get; set; }
        public long Timestamp { get; set; }
    }

    /// <summary>
    /// Action conflict data
    /// </summary>
    public class ActionConflict
    {
        public PlayerAction Action1 { get; set; }
        public PlayerAction Action2 { get; set; }
        public ConflictType Type { get; set; }
    }

    /// <summary>
    /// Types of actions
    /// </summary>
    public enum ActionType
    {
        Movement,
        Combat,
        Interaction,
        Economy,
        Building,
        Squad
    }

    /// <summary>
    /// Types of conflicts
    /// </summary>
    public enum ConflictType
    {
        SameTarget,
        SamePosition,
        ResourceConflict,
        TimeConflict
    }

    /// <summary>
    /// Processing statistics
    /// </summary>
    public class ProcessingStatistics
    {
        public int PendingActions { get; set; }
        public long ProcessedActions { get; set; }
        public double AverageLatency { get; set; }
        public int CurrentTickRate { get; set; }
        public int CurrentBatchSize { get; set; }
    }

    /// <summary>
    /// Performance monitoring
    /// </summary>
    public class PerformanceMonitor
    {
        private readonly Queue<long> latencies;
        private readonly Queue<int> batchSizes;
        private long totalProcessed;

        public long TotalProcessed => totalProcessed;
        public double AverageLatency => latencies.Count > 0 ? latencies.Average() : 0;

        public PerformanceMonitor()
        {
            latencies = new Queue<long>();
            batchSizes = new Queue<int>();
        }

        public void RecordBatch(int size, long latency)
        {
            totalProcessed += size;
            
            latencies.Enqueue(latency);
            if (latencies.Count > 100)
                latencies.Dequeue();
            
            batchSizes.Enqueue(size);
            if (batchSizes.Count > 100)
                batchSizes.Dequeue();
        }
    }

    /// <summary>
    /// Auto-tuning for optimal performance
    /// </summary>
    public class AutoTuner
    {
        private readonly ActionProcessor processor;
        private DateTime lastAdjustment;

        public AutoTuner(ActionProcessor processor)
        {
            this.processor = processor;
            lastAdjustment = DateTime.UtcNow;
        }

        public void CheckAndAdjust(PerformanceMonitor monitor)
        {
            // Only adjust every 30 seconds
            if ((DateTime.UtcNow - lastAdjustment).TotalSeconds < 30)
                return;
            
            double avgLatency = monitor.AverageLatency;
            
            // If latency is too high, reduce batch size or tick rate
            if (avgLatency > 100) // Over 100ms
            {
                // Implement adjustment logic
                Logger.Log($"Performance warning: Average latency {avgLatency}ms");
            }
            
            lastAdjustment = DateTime.UtcNow;
        }
    }
}
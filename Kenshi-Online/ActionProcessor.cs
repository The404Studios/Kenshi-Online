using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Text.Json;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Networking.Player;
using KenshiMultiplayer.Networking;
using System.Numerics;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Processes player actions in batches with deterministic execution
    /// </summary>
    public class ActionProcessor
    {
        // Action queues and pools
        private readonly ConcurrentQueue<GameAction> actionQueue = new ConcurrentQueue<GameAction>();
        private readonly ConcurrentDictionary<string, ActionPool> actionPools = new ConcurrentDictionary<string, ActionPool>();
        private readonly ConcurrentDictionary<string, ActionBatch> pendingBatches = new ConcurrentDictionary<string, ActionBatch>();

        // Processing configuration
        private readonly int batchSize = 50;
        private readonly int maxBatchWaitMs = 100;
        private readonly int tickRate = 20; // Process 20 times per second

        // Dependencies
        private readonly DeterministicPathManager pathManager;
        private readonly EnhancedServer server;
        private readonly StateReconciler stateReconciler;

        // Processing state
        private bool isProcessing = false;
        private long currentTick = 0;
        private readonly object processingLock = new object();

        // Performance metrics
        private readonly PerformanceMonitor perfMonitor = new PerformanceMonitor();

        public ActionProcessor(DeterministicPathManager pathMgr, EnhancedServer srv)
        {
            pathManager = pathMgr;
            server = srv;
            stateReconciler = new StateReconciler();

            InitializeActionPools();
        }

        /// <summary>
        /// Initialize action pools for different action types
        /// </summary>
        private void InitializeActionPools()
        {
            // Combat pool - highest priority
            actionPools["combat"] = new ActionPool
            {
                Priority = 1,
                MaxSize = 100,
                ProcessingStrategy = ProcessingStrategy.Immediate
            };

            // Movement pool - medium priority
            actionPools["movement"] = new ActionPool
            {
                Priority = 2,
                MaxSize = 200,
                ProcessingStrategy = ProcessingStrategy.Batched
            };

            // Interaction pool - lower priority
            actionPools["interaction"] = new ActionPool
            {
                Priority = 3,
                MaxSize = 150,
                ProcessingStrategy = ProcessingStrategy.Batched
            };

            // Economy pool - lowest priority
            actionPools["economy"] = new ActionPool
            {
                Priority = 4,
                MaxSize = 100,
                ProcessingStrategy = ProcessingStrategy.Deferred
            };
        }

        /// <summary>
        /// Start the action processing loop
        /// </summary>
        public void StartProcessing()
        {
            if (isProcessing)
                return;

            isProcessing = true;

            // Start main processing thread
            Task.Run(() => ProcessingLoop());

            // Start batch optimizer thread
            Task.Run(() => BatchOptimizationLoop());

            Logger.Log("Action processor started");
        }

        /// <summary>
        /// Main processing loop
        /// </summary>
        private async void ProcessingLoop()
        {
            var tickInterval = TimeSpan.FromMilliseconds(1000 / tickRate);

            while (isProcessing)
            {
                var tickStart = DateTime.UtcNow;
                currentTick++;

                try
                {
                    // Process each tick
                    await ProcessTick();
                }
                catch (Exception ex)
                {
                    Logger.Log($"Processing error: {ex.Message}");
                }

                // Maintain tick rate
                var elapsed = DateTime.UtcNow - tickStart;
                if (elapsed < tickInterval)
                {
                    await Task.Delay(tickInterval - elapsed);
                }

                // Update metrics
                perfMonitor.RecordTick(elapsed.TotalMilliseconds);
            }
        }

        /// <summary>
        /// Process a single tick
        /// </summary>
        private async Task ProcessTick()
        {
            // Collect actions from queue
            var actions = CollectActions();

            if (actions.Count == 0)
                return;

            // Sort actions into pools
            foreach (var action in actions)
            {
                string poolName = GetPoolForAction(action);
                if (actionPools.TryGetValue(poolName, out var pool))
                {
                    pool.AddAction(action);
                }
            }

            // Process pools by priority
            var sortedPools = actionPools.OrderBy(p => p.Value.Priority);

            foreach (var poolEntry in sortedPools)
            {
                await ProcessPool(poolEntry.Key, poolEntry.Value);
            }

            // Reconcile state after processing
            await ReconcileState();
        }

        /// <summary>
        /// Collect actions from the queue
        /// </summary>
        private List<GameAction> CollectActions()
        {
            var actions = new List<GameAction>();

            while (actions.Count < batchSize && actionQueue.TryDequeue(out var action))
            {
                // Validate action
                if (ValidateAction(action))
                {
                    actions.Add(action);
                }
            }

            return actions;
        }

        /// <summary>
        /// Process a specific action pool
        /// </summary>
        private async Task ProcessPool(string poolName, ActionPool pool)
        {
            var batch = pool.CreateBatch();

            if (batch.Actions.Count == 0)
                return;

            switch (pool.ProcessingStrategy)
            {
                case ProcessingStrategy.Immediate:
                    await ProcessImmediateBatch(batch);
                    break;

                case ProcessingStrategy.Batched:
                    await ProcessBatchedActions(batch);
                    break;

                case ProcessingStrategy.Deferred:
                    DeferBatch(batch);
                    break;
            }

            // Record metrics
            perfMonitor.RecordBatch(poolName, batch.Actions.Count);
        }

        /// <summary>
        /// Process immediate actions (combat, etc.)
        /// </summary>
        private async Task ProcessImmediateBatch(ActionBatch batch)
        {
            // Sort by timestamp for proper ordering
            batch.Actions.Sort((a, b) => a.Timestamp.CompareTo(b.Timestamp));

            foreach (var action in batch.Actions)
            {
                try
                {
                    var result = await ExecuteAction(action);

                    // Broadcast result immediately
                    BroadcastActionResult(action, result);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Failed to execute immediate action: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process batched actions with optimization
        /// </summary>
        private async Task ProcessBatchedActions(ActionBatch batch)
        {
            // Group compatible actions
            var groups = GroupCompatibleActions(batch.Actions);

            foreach (var group in groups)
            {
                // Check for conflicts
                var conflicts = DetectConflicts(group);

                if (conflicts.Count > 0)
                {
                    // Resolve conflicts
                    group = ResolveConflicts(group, conflicts);
                }

                // Execute group in parallel if possible
                if (CanExecuteInParallel(group))
                {
                    await ExecuteParallel(group);
                }
                else
                {
                    await ExecuteSequential(group);
                }
            }
        }

        /// <summary>
        /// Execute a single action
        /// </summary>
        private async Task<ActionResult> ExecuteAction(GameAction action)
        {
            var result = new ActionResult
            {
                ActionId = action.Id,
                Success = false,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            try
            {
                switch (action.Type)
                {
                    case "movement":
                        result = await ExecuteMovement(action);
                        break;

                    case "combat":
                        result = await ExecuteCombat(action);
                        break;

                    case "interaction":
                        result = await ExecuteInteraction(action);
                        break;

                    case "trade":
                        result = await ExecuteTrade(action);
                        break;

                    default:
                        Logger.Log($"Unknown action type: {action.Type}");
                        break;
                }
            }
            catch (Exception ex)
            {
                result.Error = ex.Message;
                Logger.Log($"Action execution error: {ex.Message}");
            }

            return result;
        }

        /// <summary>
        /// Execute movement with deterministic pathing
        /// </summary>
        private async Task<ActionResult> ExecuteMovement(GameAction action)
        {
            var movementData = JsonSerializer.Deserialize<MovementAction>(action.Data);

            // Get deterministic path
            var path = pathManager.GetSynchronizedPath(
                movementData.Start,
                movementData.End,
                action.PlayerId
            );

            if (path == null)
            {
                return new ActionResult
                {
                    ActionId = action.Id,
                    Success = false,
                    Error = "No path found"
                };
            }

            // Apply movement to game state
            var stateUpdate = new StateUpdate
            {
                EntityId = action.PlayerId,
                Type = "position_path",
                Data = new Dictionary<string, object>
                {
                    { "path", path },
                    { "speed", movementData.Speed }
                }
            };

            await ApplyStateUpdate(stateUpdate);

            return new ActionResult
            {
                ActionId = action.Id,
                Success = true,
                Data = new Dictionary<string, object>
                {
                    { "pathId", path.PathId },
                    { "waypoints", path.Waypoints.Count },
                    { "distance", path.Distance }
                }
            };
        }

        /// <summary>
        /// Execute combat action
        /// </summary>
        private async Task<ActionResult> ExecuteCombat(GameAction action)
        {
            var combatData = JsonSerializer.Deserialize<CombatAction>(action.Data);

            // Validate combat action
            if (!ValidateCombatAction(combatData))
            {
                return new ActionResult
                {
                    ActionId = action.Id,
                    Success = false,
                    Error = "Invalid combat action"
                };
            }

            // Calculate deterministic combat result
            var combatResult = CalculateCombatResult(combatData);

            // Apply damage and effects
            var stateUpdate = new StateUpdate
            {
                EntityId = combatData.TargetId,
                Type = "damage",
                Data = new Dictionary<string, object>
                {
                    { "damage", combatResult.Damage },
                    { "limb", combatResult.AffectedLimb },
                    { "effects", combatResult.AppliedEffects }
                }
            };

            await ApplyStateUpdate(stateUpdate);

            return new ActionResult
            {
                ActionId = action.Id,
                Success = true,
                Data = new Dictionary<string, object>
                {
                    { "result", combatResult }
                }
            };
        }

        /// <summary>
        /// Execute interaction (looting, talking, etc.)
        /// </summary>
        private async Task<ActionResult> ExecuteInteraction(GameAction action)
        {
            var interactionData = JsonSerializer.Deserialize<InteractionAction>(action.Data);

            // Check if target is valid
            if (!IsValidInteractionTarget(interactionData.TargetId))
            {
                return new ActionResult
                {
                    ActionId = action.Id,
                    Success = false,
                    Error = "Invalid target"
                };
            }

            // Process interaction
            var result = await ProcessInteraction(interactionData);

            return new ActionResult
            {
                ActionId = action.Id,
                Success = result.Success,
                Data = result.Data
            };
        }

        /// <summary>
        /// Execute trade action
        /// </summary>
        private async Task<ActionResult> ExecuteTrade(GameAction action)
        {
            var tradeData = JsonSerializer.Deserialize<TradeAction>(action.Data);

            // Validate trade
            if (!ValidateTrade(tradeData))
            {
                return new ActionResult
                {
                    ActionId = action.Id,
                    Success = false,
                    Error = "Invalid trade"
                };
            }

            // Execute trade atomically
            lock (processingLock)
            {
                // Transfer items
                TransferItems(tradeData.FromPlayer, tradeData.ToPlayer, tradeData.Items);

                // Transfer currency
                if (tradeData.Currency > 0)
                {
                    TransferCurrency(tradeData.FromPlayer, tradeData.ToPlayer, tradeData.Currency);
                }
            }

            return new ActionResult
            {
                ActionId = action.Id,
                Success = true,
                Data = new Dictionary<string, object>
                {
                    { "itemsTransferred", tradeData.Items.Count },
                    { "currencyTransferred", tradeData.Currency }
                }
            };
        }

        /// <summary>
        /// Group actions that can be processed together
        /// </summary>
        private List<List<GameAction>> GroupCompatibleActions(List<GameAction> actions)
        {
            var groups = new List<List<GameAction>>();
            var processed = new HashSet<string>();

            foreach (var action in actions)
            {
                if (processed.Contains(action.Id))
                    continue;

                var group = new List<GameAction> { action };
                processed.Add(action.Id);

                // Find compatible actions
                foreach (var other in actions)
                {
                    if (processed.Contains(other.Id))
                        continue;

                    if (AreCompatible(action, other))
                    {
                        group.Add(other);
                        processed.Add(other.Id);
                    }
                }

                groups.Add(group);
            }

            return groups;
        }

        /// <summary>
        /// Check if two actions are compatible for batching
        /// </summary>
        private bool AreCompatible(GameAction a, GameAction b)
        {
            // Same type actions are usually compatible
            if (a.Type != b.Type)
                return false;

            // Check for resource conflicts
            if (HasResourceConflict(a, b))
                return false;

            // Check for spatial conflicts
            if (HasSpatialConflict(a, b))
                return false;

            return true;
        }

        /// <summary>
        /// Detect conflicts in action group
        /// </summary>
        private List<ActionConflict> DetectConflicts(List<GameAction> group)
        {
            var conflicts = new List<ActionConflict>();

            for (int i = 0; i < group.Count - 1; i++)
            {
                for (int j = i + 1; j < group.Count; j++)
                {
                    var conflict = CheckConflict(group[i], group[j]);
                    if (conflict != null)
                    {
                        conflicts.Add(conflict);
                    }
                }
            }

            return conflicts;
        }

        /// <summary>
        /// Resolve conflicts between actions
        /// </summary>
        private List<GameAction> ResolveConflicts(List<GameAction> group, List<ActionConflict> conflicts)
        {
            foreach (var conflict in conflicts)
            {
                switch (conflict.Type)
                {
                    case ConflictType.Resource:
                        // First come, first served
                        group.Remove(conflict.Action2);
                        break;

                    case ConflictType.Spatial:
                        // Merge into single action if possible
                        if (CanMergeActions(conflict.Action1, conflict.Action2))
                        {
                            var merged = MergeActions(conflict.Action1, conflict.Action2);
                            group.Remove(conflict.Action1);
                            group.Remove(conflict.Action2);
                            group.Add(merged);
                        }
                        else
                        {
                            // Priority based resolution
                            if (GetActionPriority(conflict.Action1) > GetActionPriority(conflict.Action2))
                            {
                                group.Remove(conflict.Action2);
                            }
                            else
                            {
                                group.Remove(conflict.Action1);
                            }
                        }
                        break;

                    case ConflictType.Timing:
                        // Reorder based on timestamp
                        group.Sort((a, b) => a.Timestamp.CompareTo(b.Timestamp));
                        break;
                }
            }

            return group;
        }

        /// <summary>
        /// Execute actions in parallel
        /// </summary>
        private async Task ExecuteParallel(List<GameAction> actions)
        {
            var tasks = actions.Select(action => ExecuteAction(action));
            var results = await Task.WhenAll(tasks);

            // Broadcast all results
            for (int i = 0; i < actions.Count; i++)
            {
                BroadcastActionResult(actions[i], results[i]);
            }
        }

        /// <summary>
        /// Execute actions sequentially
        /// </summary>
        private async Task ExecuteSequential(List<GameAction> actions)
        {
            foreach (var action in actions)
            {
                var result = await ExecuteAction(action);
                BroadcastActionResult(action, result);
            }
        }

        /// <summary>
        /// Apply state update to game
        /// </summary>
        private async Task ApplyStateUpdate(StateUpdate update)
        {
            // This would integrate with Kenshi's memory
            // For now, track in our state manager
            stateReconciler.ApplyUpdate(update);

            // Broadcast state change
            var message = new GameMessage
            {
                Type = MessageType.WorldState,
                Data = new Dictionary<string, object>
                {
                    { "update", update }
                }
            };

            // Send to all clients
            await Task.CompletedTask; // Placeholder for actual broadcast
        }

        /// <summary>
        /// Reconcile game state after batch processing
        /// </summary>
        private async Task ReconcileState()
        {
            var reconciliation = stateReconciler.Reconcile();

            if (reconciliation.HasConflicts)
            {
                Logger.Log($"State conflicts detected: {reconciliation.Conflicts.Count}");

                // Resolve conflicts
                foreach (var conflict in reconciliation.Conflicts)
                {
                    ResolveStateConflict(conflict);
                }
            }

            // Broadcast reconciled state
            var stateSnapshot = stateReconciler.CreateSnapshot();
            BroadcastStateSnapshot(stateSnapshot);

            await Task.CompletedTask;
        }

        /// <summary>
        /// Broadcast action result to clients
        /// </summary>
        private void BroadcastActionResult(GameAction action, ActionResult result)
        {
            var message = new GameMessage
            {
                Type = "action_result",
                PlayerId = action.PlayerId,
                Data = new Dictionary<string, object>
                {
                    { "action", action },
                    { "result", result }
                }
            };

            // Send through server
            // server.BroadcastMessage(message);
        }

        /// <summary>
        /// Broadcast state snapshot
        /// </summary>
        private void BroadcastStateSnapshot(StateSnapshot snapshot)
        {
            var message = new GameMessage
            {
                Type = MessageType.WorldState,
                Data = new Dictionary<string, object>
                {
                    { "snapshot", snapshot },
                    { "tick", currentTick }
                }
            };

            // Send through server
            // server.BroadcastMessage(message);
        }

        // Helper methods

        private string GetPoolForAction(GameAction action)
        {
            switch (action.Type)
            {
                case "combat":
                case "damage":
                    return "combat";

                case "movement":
                case "pathfinding":
                    return "movement";

                case "trade":
                case "craft":
                    return "economy";

                default:
                    return "interaction";
            }
        }

        private bool ValidateAction(GameAction action)
        {
            // Check timestamp isn't too old
            var age = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - action.Timestamp;
            if (age > 5000) // 5 seconds
                return false;

            // Validate player exists
            // Additional validation...

            return true;
        }

        private bool CanExecuteInParallel(List<GameAction> actions)
        {
            // Movement actions to different locations can be parallel
            // Trade actions between different player pairs can be parallel
            // etc.

            return actions.All(a => a.Type == "movement") &&
                   !actions.Any(a => HasSpatialConflict(a, actions.Where(x => x != a).FirstOrDefault()));
        }

        private bool HasResourceConflict(GameAction a, GameAction b)
        {
            // Check if actions compete for same resource
            // e.g., same inventory item
            return false; // Simplified
        }

        private bool HasSpatialConflict(GameAction a, GameAction b)
        {
            // Check if actions affect same space
            if (a.Type == "movement" && b.Type == "movement")
            {
                // Check if paths intersect
                return false; // Simplified
            }

            return false;
        }

        private ActionConflict CheckConflict(GameAction a, GameAction b)
        {
            if (HasResourceConflict(a, b))
            {
                return new ActionConflict
                {
                    Type = ConflictType.Resource,
                    Action1 = a,
                    Action2 = b
                };
            }

            if (HasSpatialConflict(a, b))
            {
                return new ActionConflict
                {
                    Type = ConflictType.Spatial,
                    Action1 = a,
                    Action2 = b
                };
            }

            return null;
        }

        private bool CanMergeActions(GameAction a, GameAction b)
        {
            // Check if actions can be combined
            return false; // Simplified
        }

        private GameAction MergeActions(GameAction a, GameAction b)
        {
            // Combine two compatible actions
            return a; // Simplified
        }

        private int GetActionPriority(GameAction action)
        {
            // Return priority value
            switch (action.Type)
            {
                case "combat": return 10;
                case "movement": return 5;
                default: return 1;
            }
        }

        private void DeferBatch(ActionBatch batch)
        {
            // Store for later processing
            pendingBatches[Guid.NewGuid().ToString()] = batch;
        }

        private bool ValidateCombatAction(CombatAction action)
        {
            // Validate combat is possible
            return true; // Simplified
        }

        private CombatResult CalculateCombatResult(CombatAction action)
        {
            // Deterministic combat calculation
            return new CombatResult
            {
                Hit = true,
                Damage = 10,
                AffectedLimb = "Chest"
            };
        }

        private bool IsValidInteractionTarget(string targetId)
        {
            // Check if target exists and is interactable
            return true; // Simplified
        }

        private async Task<InteractionResult> ProcessInteraction(InteractionAction action)
        {
            // Process the interaction
            return new InteractionResult
            {
                Success = true,
                Data = new Dictionary<string, object>()
            };
        }

        private bool ValidateTrade(TradeAction trade)
        {
            // Validate both players have required items
            return true; // Simplified
        }

        private void TransferItems(string from, string to, List<TradeItem> items)
        {
            // Transfer items between players
        }

        private void TransferCurrency(string from, string to, int amount)
        {
            // Transfer money between players
        }

        private void ResolveStateConflict(StateConflict conflict)
        {
            // Resolve state conflict
            // Use authoritative source or timestamp
        }

        /// <summary>
        /// Batch optimization loop
        /// </summary>
        private async void BatchOptimizationLoop()
        {
            while (isProcessing)
            {
                await Task.Delay(1000); // Run every second

                // Analyze performance metrics
                var metrics = perfMonitor.GetMetrics();

                // Adjust batch sizes based on performance
                if (metrics.AverageTickTime > 40) // Taking too long
                {
                    batchSize = Math.Max(10, batchSize - 10);
                }
                else if (metrics.AverageTickTime < 20) // Room for more
                {
                    batchSize = Math.Min(100, batchSize + 10);
                }

                // Process deferred batches if system is idle
                if (metrics.QueueSize < 10)
                {
                    ProcessDeferredBatches();
                }
            }
        }

        private void ProcessDeferredBatches()
        {
            foreach (var batch in pendingBatches.Values.Take(5))
            {
                Task.Run(async () => await ProcessBatchedActions(batch));
            }
        }

        /// <summary>
        /// Queue an action for processing
        /// </summary>
        public void QueueAction(GameAction action)
        {
            actionQueue.Enqueue(action);
            perfMonitor.RecordQueueSize(actionQueue.Count);
        }

        /// <summary>
        /// Stop processing
        /// </summary>
        public void StopProcessing()
        {
            isProcessing = false;
            Logger.Log("Action processor stopped");
        }
    }

    // Supporting classes

    public class GameAction
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string Data { get; set; }
        public long Timestamp { get; set; }
        public int Priority { get; set; }
    }

    public class ActionResult
    {
        public string ActionId { get; set; }
        public bool Success { get; set; }
        public string Error { get; set; }
        public Dictionary<string, object> Data { get; set; }
        public long Timestamp { get; set; }
    }

    public class ActionPool
    {
        public int Priority { get; set; }
        public int MaxSize { get; set; }
        public ProcessingStrategy ProcessingStrategy { get; set; }
        private readonly List<GameAction> actions = new List<GameAction>();

        public void AddAction(GameAction action)
        {
            if (actions.Count < MaxSize)
            {
                actions.Add(action);
            }
        }

        public ActionBatch CreateBatch()
        {
            var batch = new ActionBatch
            {
                Actions = new List<GameAction>(actions)
            };
            actions.Clear();
            return batch;
        }
    }

    public class ActionBatch
    {
        public List<GameAction> Actions { get; set; } = new List<GameAction>();
        public long CreatedAt { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }

    public enum ProcessingStrategy
    {
        Immediate,
        Batched,
        Deferred
    }

    public class ActionConflict
    {
        public ConflictType Type { get; set; }
        public GameAction Action1 { get; set; }
        public GameAction Action2 { get; set; }
    }

    public enum ConflictType
    {
        Resource,
        Spatial,
        Timing
    }

    public class StateUpdate
    {
        public string EntityId { get; set; }
        public string Type { get; set; }
        public Dictionary<string, object> Data { get; set; }
    }

    public class StateSnapshot
    {
        public long Tick { get; set; }
        public Dictionary<string, EntityState> Entities { get; set; }
        public long Timestamp { get; set; }
    }

    public class EntityState
    {
        public string Id { get; set; }
        public Vector3 Position { get; set; }
        public int Health { get; set; }
        public Dictionary<string, object> Properties { get; set; }
    }

    public class StateReconciler
    {
        private readonly Dictionary<string, EntityState> currentState = new Dictionary<string, EntityState>();
        private readonly List<StateUpdate> pendingUpdates = new List<StateUpdate>();

        public void ApplyUpdate(StateUpdate update)
        {
            pendingUpdates.Add(update);
        }

        public StateReconciliation Reconcile()
        {
            var reconciliation = new StateReconciliation();

            foreach (var update in pendingUpdates)
            {
                // Apply update to state
                if (currentState.ContainsKey(update.EntityId))
                {
                    // Update existing entity
                    ApplyToEntity(currentState[update.EntityId], update);
                }
                else
                {
                    // Create new entity
                    currentState[update.EntityId] = CreateEntity(update);
                }
            }

            pendingUpdates.Clear();
            return reconciliation;
        }

        public StateSnapshot CreateSnapshot()
        {
            return new StateSnapshot
            {
                Entities = new Dictionary<string, EntityState>(currentState),
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        private void ApplyToEntity(EntityState entity, StateUpdate update)
        {
            // Apply update to entity
        }

        private EntityState CreateEntity(StateUpdate update)
        {
            return new EntityState
            {
                Id = update.EntityId,
                Properties = update.Data
            };
        }
    }

    public class StateReconciliation
    {
        public bool HasConflicts { get; set; }
        public List<StateConflict> Conflicts { get; set; } = new List<StateConflict>();
    }

    public class StateConflict
    {
        public string EntityId { get; set; }
        public string ConflictType { get; set; }
        public object Expected { get; set; }
        public object Actual { get; set; }
    }

    public class PerformanceMonitor
    {
        private readonly Queue<double> tickTimes = new Queue<double>();
        private readonly Dictionary<string, int> batchCounts = new Dictionary<string, int>();
        private int queueSize;

        public void RecordTick(double milliseconds)
        {
            tickTimes.Enqueue(milliseconds);
            if (tickTimes.Count > 100)
                tickTimes.Dequeue();
        }

        public void RecordBatch(string pool, int count)
        {
            if (!batchCounts.ContainsKey(pool))
                batchCounts[pool] = 0;
            batchCounts[pool] += count;
        }

        public void RecordQueueSize(int size)
        {
            queueSize = size;
        }

        public PerformanceMetrics GetMetrics()
        {
            return new PerformanceMetrics
            {
                AverageTickTime = tickTimes.Count > 0 ? tickTimes.Average() : 0,
                QueueSize = queueSize,
                BatchCounts = new Dictionary<string, int>(batchCounts)
            };
        }
    }

    public class PerformanceMetrics
    {
        public double AverageTickTime { get; set; }
        public int QueueSize { get; set; }
        public Dictionary<string, int> BatchCounts { get; set; }
    }

    // Action type classes

    public class MovementAction
    {
        public Vector3 Start { get; set; }
        public Vector3 End { get; set; }
        public float Speed { get; set; }
    }

    public class InteractionAction
    {
        public string TargetId { get; set; }
        public string InteractionType { get; set; }
    }

    public class InteractionResult
    {
        public bool Success { get; set; }
        public Dictionary<string, object> Data { get; set; }
    }

    public class TradeAction
    {
        public string FromPlayer { get; set; }
        public string ToPlayer { get; set; }
        public List<TradeItem> Items { get; set; }
        public int Currency { get; set; }
    }
}
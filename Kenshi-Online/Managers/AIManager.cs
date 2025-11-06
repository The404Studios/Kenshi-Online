using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages AI behavior synchronization for multiplayer
    /// </summary>
    public class AIManager
    {
        private readonly Dictionary<string, AIState> activeAIStates;
        private readonly Dictionary<string, List<AITask>> aiTaskQueues;
        private readonly NetworkManager? networkManager;
        private bool isRunning;

        public AIManager(NetworkManager? networkManager = null)
        {
            this.networkManager = networkManager;
            activeAIStates = new Dictionary<string, AIState>();
            aiTaskQueues = new Dictionary<string, List<AITask>>();
        }

        /// <summary>
        /// Register an AI entity
        /// </summary>
        public void RegisterAI(string entityId, AIGoal initialGoal = AIGoal.Idle)
        {
            if (activeAIStates.ContainsKey(entityId))
            {
                Console.WriteLine($"AI already registered for {entityId}");
                return;
            }

            var aiState = new AIState
            {
                EntityId = entityId,
                CurrentGoal = initialGoal,
                TargetEntityId = "",
                TargetPosition = System.Numerics.Vector3.Zero,
                CurrentTask = "",
                TaskProgress = 0.0f,
                AlertLevel = 0,
                LastUpdate = DateTime.UtcNow
            };

            activeAIStates[entityId] = aiState;
            aiTaskQueues[entityId] = new List<AITask>();

            Console.WriteLine($"Registered AI for entity {entityId} with goal {initialGoal}");
        }

        /// <summary>
        /// Unregister an AI entity
        /// </summary>
        public void UnregisterAI(string entityId)
        {
            if (activeAIStates.Remove(entityId))
            {
                aiTaskQueues.Remove(entityId);
                Console.WriteLine($"Unregistered AI for entity {entityId}");
            }
        }

        /// <summary>
        /// Update AI goal
        /// </summary>
        public bool SetGoal(string entityId, AIGoal goal, string targetId = "", System.Numerics.Vector3? targetPosition = null)
        {
            if (!activeAIStates.TryGetValue(entityId, out AIState? state))
            {
                Console.WriteLine($"AI not found for entity {entityId}");
                return false;
            }

            state.CurrentGoal = goal;
            state.TargetEntityId = targetId;
            state.TargetPosition = targetPosition ?? System.Numerics.Vector3.Zero;
            state.LastUpdate = DateTime.UtcNow;

            Console.WriteLine($"Set AI goal for {entityId}: {goal}");

            // Clear existing tasks when goal changes
            if (aiTaskQueues.TryGetValue(entityId, out List<AITask>? tasks))
            {
                tasks.Clear();
            }

            // Generate tasks for new goal
            GenerateTasksForGoal(entityId, state);

            // Notify network
            NotifyAIUpdate(entityId, state);

            return true;
        }

        /// <summary>
        /// Queue AI task
        /// </summary>
        public bool QueueTask(string entityId, AITask task)
        {
            if (!aiTaskQueues.TryGetValue(entityId, out List<AITask>? tasks))
            {
                Console.WriteLine($"AI task queue not found for {entityId}");
                return false;
            }

            tasks.Add(task);
            Console.WriteLine($"Queued AI task for {entityId}: {task.Type}");

            return true;
        }

        /// <summary>
        /// Update AI task progress
        /// </summary>
        public bool UpdateTaskProgress(string entityId, float progress)
        {
            if (!activeAIStates.TryGetValue(entityId, out AIState? state))
                return false;

            state.TaskProgress = Math.Clamp(progress, 0.0f, 1.0f);
            state.LastUpdate = DateTime.UtcNow;

            // Check if task completed
            if (state.TaskProgress >= 1.0f)
            {
                CompleteCurrentTask(entityId);
            }

            return true;
        }

        /// <summary>
        /// Complete current task and start next
        /// </summary>
        private void CompleteCurrentTask(string entityId)
        {
            if (!aiTaskQueues.TryGetValue(entityId, out List<AITask>? tasks) || tasks.Count == 0)
                return;

            if (!activeAIStates.TryGetValue(entityId, out AIState? state))
                return;

            // Remove completed task
            var completedTask = tasks[0];
            tasks.RemoveAt(0);

            Console.WriteLine($"AI {entityId} completed task: {completedTask.Type}");

            // Start next task if available
            if (tasks.Count > 0)
            {
                var nextTask = tasks[0];
                state.CurrentTask = nextTask.Type.ToString();
                state.TaskProgress = 0.0f;
                state.LastUpdate = DateTime.UtcNow;

                Console.WriteLine($"AI {entityId} starting task: {nextTask.Type}");
            }
            else
            {
                // No more tasks, set to idle
                state.CurrentGoal = AIGoal.Idle;
                state.CurrentTask = "";
                state.TaskProgress = 0.0f;
            }

            NotifyAIUpdate(entityId, state);
        }

        /// <summary>
        /// Generate tasks based on AI goal
        /// </summary>
        private void GenerateTasksForGoal(string entityId, AIState state)
        {
            if (!aiTaskQueues.TryGetValue(entityId, out List<AITask>? tasks))
                return;

            switch (state.CurrentGoal)
            {
                case AIGoal.Patrol:
                    // Generate patrol waypoints
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.MoveTo,
                        TargetPosition = state.TargetPosition,
                        Priority = 1
                    });
                    break;

                case AIGoal.Follow:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Follow,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 2
                    });
                    break;

                case AIGoal.Attack:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.MoveTo,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 3
                    });
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Attack,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 3
                    });
                    break;

                case AIGoal.Flee:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Flee,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 4
                    });
                    break;

                case AIGoal.Gather:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.MoveTo,
                        TargetPosition = state.TargetPosition,
                        Priority = 1
                    });
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Gather,
                        TargetPosition = state.TargetPosition,
                        Priority = 1
                    });
                    break;

                case AIGoal.Build:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.MoveTo,
                        TargetPosition = state.TargetPosition,
                        Priority = 1
                    });
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Build,
                        TargetPosition = state.TargetPosition,
                        Priority = 1
                    });
                    break;

                case AIGoal.Heal:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Heal,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 3
                    });
                    break;

                case AIGoal.Trade:
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.MoveTo,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 1
                    });
                    tasks.Add(new AITask
                    {
                        Type = AITaskType.Trade,
                        TargetEntityId = state.TargetEntityId,
                        Priority = 1
                    });
                    break;

                case AIGoal.Idle:
                default:
                    // No tasks for idle
                    break;
            }

            if (tasks.Count > 0)
            {
                state.CurrentTask = tasks[0].Type.ToString();
            }
        }

        /// <summary>
        /// Get AI state for entity
        /// </summary>
        public AIState? GetAIState(string entityId)
        {
            return activeAIStates.TryGetValue(entityId, out AIState? state) ? state : null;
        }

        /// <summary>
        /// Get AI task queue for entity
        /// </summary>
        public List<AITask> GetTaskQueue(string entityId)
        {
            return aiTaskQueues.TryGetValue(entityId, out List<AITask>? tasks)
                ? new List<AITask>(tasks)
                : new List<AITask>();
        }

        /// <summary>
        /// Update alert level
        /// </summary>
        public bool SetAlertLevel(string entityId, int alertLevel)
        {
            if (!activeAIStates.TryGetValue(entityId, out AIState? state))
                return false;

            state.AlertLevel = Math.Clamp(alertLevel, 0, 100);
            state.LastUpdate = DateTime.UtcNow;

            // High alert might change behavior
            if (state.AlertLevel > 75 && state.CurrentGoal == AIGoal.Idle)
            {
                state.CurrentGoal = AIGoal.Guard;
            }

            NotifyAIUpdate(entityId, state);
            return true;
        }

        /// <summary>
        /// Process AI updates for all entities
        /// </summary>
        public async Task ProcessAIUpdates()
        {
            var now = DateTime.UtcNow;

            foreach (var kvp in activeAIStates.ToList())
            {
                var entityId = kvp.Key;
                var state = kvp.Value;

                // Check if AI needs update (every 5 seconds)
                if ((now - state.LastUpdate).TotalSeconds < 5.0)
                    continue;

                // Auto-progress tasks (placeholder logic)
                if (state.TaskProgress < 1.0f && !string.IsNullOrEmpty(state.CurrentTask))
                {
                    state.TaskProgress += 0.1f;
                    state.LastUpdate = now;

                    if (state.TaskProgress >= 1.0f)
                    {
                        CompleteCurrentTask(entityId);
                    }
                }
            }

            await Task.CompletedTask;
        }

        /// <summary>
        /// Notify network of AI update
        /// </summary>
        private void NotifyAIUpdate(string entityId, AIState state)
        {
            if (networkManager == null)
                return;

            var message = new GameMessage
            {
                Type = "ai_update",
                SenderId = "system",
                Data = new Dictionary<string, object>
                {
                    { "entityId", entityId },
                    { "goal", state.CurrentGoal.ToString() },
                    { "targetEntity", state.TargetEntityId },
                    { "targetPosition", state.TargetPosition },
                    { "currentTask", state.CurrentTask },
                    { "progress", state.TaskProgress },
                    { "alertLevel", state.AlertLevel }
                }
            };

            try
            {
                networkManager.BroadcastToAll(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to send AI update: {ex.Message}");
            }
        }

        /// <summary>
        /// Get AI statistics
        /// </summary>
        public AIStatistics GetStatistics()
        {
            int activeCount = 0;
            int idleCount = 0;
            int combatCount = 0;

            foreach (var state in activeAIStates.Values)
            {
                if (state.CurrentGoal == AIGoal.Idle)
                    idleCount++;
                else if (state.CurrentGoal == AIGoal.Attack || state.CurrentGoal == AIGoal.Defend)
                    combatCount++;
                else
                    activeCount++;
            }

            return new AIStatistics
            {
                TotalAIs = activeAIStates.Count,
                ActiveAIs = activeCount,
                IdleAIs = idleCount,
                CombatAIs = combatCount
            };
        }
    }

    /// <summary>
    /// AI task types
    /// </summary>
    public enum AITaskType
    {
        MoveTo,
        Follow,
        Attack,
        Flee,
        Gather,
        Build,
        Heal,
        Trade,
        Wait
    }

    /// <summary>
    /// AI statistics
    /// </summary>
    public class AIStatistics
    {
        public int TotalAIs { get; set; }
        public int ActiveAIs { get; set; }
        public int IdleAIs { get; set; }
        public int CombatAIs { get; set; }
    }
}

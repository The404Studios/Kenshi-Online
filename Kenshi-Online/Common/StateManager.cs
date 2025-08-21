using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Manages game state synchronization and consistency across clients
    /// </summary>
    public class StateManager
    {
        private bool isServer;

        // State snapshots
        private GameState currentState;
        private GameState previousState;
        private ConcurrentDictionary<int, GameState> stateHistory;
        private int currentStateId = 0;

        // Delta compression
        private DeltaCompressor deltaCompressor;
        private ConcurrentDictionary<string, ClientStateInfo> clientStates;

        // Prediction and interpolation
        private PredictionEngine predictionEngine;
        private InterpolationEngine interpolationEngine;

        // State validation
        private StateValidator stateValidator;
        private ConcurrentQueue<StateConflict> conflicts;

        // Time synchronization
        private double serverTime;
        private double localTime;
        private double timeDelta;
        private float timeMultiplier = 1.0f;

        // Events
        public event Action<StateChange> OnStateChanged;
        public event Action<StateConflict> OnConflictDetected;
        public event Action<StateSyncData> OnStateSync;

        // Configuration
        private readonly int maxStateHistory = 100;
        private readonly int stateSnapshotInterval = 1000; // ms
        private readonly int deltaCompressionThreshold = 1024; // bytes

        public StateManager(bool isServerMode)
        {
            isServer = isServerMode;

            currentState = new GameState();
            previousState = new GameState();
            stateHistory = new ConcurrentDictionary<int, GameState>();

            deltaCompressor = new DeltaCompressor();
            clientStates = new ConcurrentDictionary<string, ClientStateInfo>();

            predictionEngine = new PredictionEngine();
            interpolationEngine = new InterpolationEngine();
            stateValidator = new StateValidator();

            conflicts = new ConcurrentQueue<StateConflict>();

            StartStateManagement();
        }

        /// <summary>
        /// Start state management loops
        /// </summary>
        private void StartStateManagement()
        {
            // Snapshot loop
            Task.Run(async () =>
            {
                while (true)
                {
                    await CreateStateSnapshot();
                    await Task.Delay(stateSnapshotInterval);
                }
            });

            // Conflict resolution loop
            if (isServer)
            {
                Task.Run(async () =>
                {
                    while (true)
                    {
                        await ResolveConflicts();
                        await Task.Delay(100);
                    }
                });
            }
        }

        /// <summary>
        /// Update character state
        /// </summary>
        public void UpdateCharacterState(string characterId, CharacterState state)
        {
            lock (currentState)
            {
                if (!currentState.Characters.ContainsKey(characterId))
                {
                    currentState.Characters[characterId] = new CharacterState();
                }

                var oldState = currentState.Characters[characterId].Clone();
                currentState.Characters[characterId] = state;

                // Validate state change
                if (isServer && !stateValidator.ValidateCharacterState(oldState, state))
                {
                    // Revert invalid change
                    currentState.Characters[characterId] = oldState;

                    conflicts.Enqueue(new StateConflict
                    {
                        Type = ConflictType.InvalidState,
                        EntityId = characterId,
                        Details = "Invalid character state transition"
                    });

                    return;
                }

                // Notify change
                OnStateChanged?.Invoke(new StateChange
                {
                    Type = StateChangeType.Character,
                    EntityId = characterId,
                    OldState = oldState,
                    NewState = state
                });
            }
        }

        /// <summary>
        /// Update squad state
        /// </summary>
        public void UpdateSquadState(string squadId, SquadState state)
        {
            lock (currentState)
            {
                if (!currentState.Squads.ContainsKey(squadId))
                {
                    currentState.Squads[squadId] = new SquadState();
                }

                var oldState = currentState.Squads[squadId].Clone();
                currentState.Squads[squadId] = state;

                OnStateChanged?.Invoke(new StateChange
                {
                    Type = StateChangeType.Squad,
                    EntityId = squadId,
                    OldState = oldState,
                    NewState = state
                });
            }
        }

        /// <summary>
        /// Update building state
        /// </summary>
        public void UpdateBuildingState(string buildingId, BuildingState state)
        {
            lock (currentState)
            {
                if (!currentState.Buildings.ContainsKey(buildingId))
                {
                    currentState.Buildings[buildingId] = new BuildingState();
                }

                var oldState = currentState.Buildings[buildingId].Clone();
                currentState.Buildings[buildingId] = state;

                OnStateChanged?.Invoke(new StateChange
                {
                    Type = StateChangeType.Building,
                    EntityId = buildingId,
                    OldState = oldState,
                    NewState = state
                });
            }
        }

        /// <summary>
        /// Update item state
        /// </summary>
        public void UpdateItemState(string itemId, ItemState state)
        {
            lock (currentState)
            {
                if (!currentState.Items.ContainsKey(itemId))
                {
                    currentState.Items[itemId] = new ItemState();
                }

                var oldState = currentState.Items[itemId].Clone();
                currentState.Items[itemId] = state;

                OnStateChanged?.Invoke(new StateChange
                {
                    Type = StateChangeType.Item,
                    EntityId = itemId,
                    OldState = oldState,
                    NewState = state
                });
            }
        }

        /// <summary>
        /// Apply state delta from client
        /// </summary>
        public async Task<bool> ApplyStateDelta(string clientId, StateDelta delta)
        {
            if (!isServer)
                return false;

            try
            {
                // Validate client has authority over entities
                if (!ValidateClientAuthority(clientId, delta))
                {
                    conflicts.Enqueue(new StateConflict
                    {
                        Type = ConflictType.AuthorityViolation,
                        ClientId = clientId,
                        Details = "Client attempted to modify unauthorized entities"
                    });
                    return false;
                }

                // Validate state transitions
                if (!stateValidator.ValidateDelta(currentState, delta))
                {
                    conflicts.Enqueue(new StateConflict
                    {
                        Type = ConflictType.InvalidDelta,
                        ClientId = clientId,
                        Details = "Invalid state delta"
                    });
                    return false;
                }

                // Apply delta
                ApplyDelta(delta);

                // Update client state info
                if (clientStates.TryGetValue(clientId, out var clientInfo))
                {
                    clientInfo.LastStateId = delta.BaseStateId;
                    clientInfo.LastUpdateTime = GetCurrentTime();
                }

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to apply state delta: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Get state delta for client
        /// </summary>
        public StateDelta GetStateDelta(string clientId)
        {
            if (!clientStates.TryGetValue(clientId, out var clientInfo))
            {
                // Send full state
                return CreateFullStateDelta();
            }

            // Get delta from client's last known state
            if (stateHistory.TryGetValue(clientInfo.LastStateId, out var clientState))
            {
                return deltaCompressor.CreateDelta(clientState, currentState);
            }

            // Fallback to full state
            return CreateFullStateDelta();
        }

        /// <summary>
        /// Predict future state for client-side prediction
        /// </summary>
        public GameState PredictState(double futureTime)
        {
            return predictionEngine.Predict(currentState, futureTime - GetCurrentTime());
        }

        /// <summary>
        /// Interpolate between states for smooth rendering
        /// </summary>
        public GameState InterpolateState(GameState from, GameState to, float t)
        {
            return interpolationEngine.Interpolate(from, to, t);
        }

        /// <summary>
        /// Create state snapshot
        /// </summary>
        private async Task CreateStateSnapshot()
        {
            try
            {
                lock (currentState)
                {
                    // Clone current state
                    var snapshot = currentState.Clone();
                    snapshot.StateId = Interlocked.Increment(ref currentStateId);
                    snapshot.Timestamp = GetCurrentTime();

                    // Store in history
                    stateHistory[snapshot.StateId] = snapshot;

                    // Cleanup old snapshots
                    if (stateHistory.Count > maxStateHistory)
                    {
                        var oldestKey = stateHistory.Keys.Min();
                        stateHistory.TryRemove(oldestKey, out _);
                    }

                    previousState = snapshot;
                }

                // Broadcast state to clients if server
                if (isServer)
                {
                    await BroadcastState();
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to create state snapshot: {ex.Message}");
            }
        }

        /// <summary>
        /// Broadcast current state to all clients
        /// </summary>
        private async Task BroadcastState()
        {
            foreach (var clientId in clientStates.Keys)
            {
                var delta = GetStateDelta(clientId);

                OnStateSync?.Invoke(new StateSyncData
                {
                    ClientId = clientId,
                    Delta = delta,
                    ServerTime = GetCurrentTime()
                });
            }
        }

        /// <summary>
        /// Resolve state conflicts
        /// </summary>
        private async Task ResolveConflicts()
        {
            while (conflicts.TryDequeue(out var conflict))
            {
                switch (conflict.Type)
                {
                    case ConflictType.PositionMismatch:
                        ResolvePositionConflict(conflict);
                        break;

                    case ConflictType.InventoryMismatch:
                        ResolveInventoryConflict(conflict);
                        break;

                    case ConflictType.InvalidState:
                        ResolveInvalidState(conflict);
                        break;

                    case ConflictType.AuthorityViolation:
                        HandleAuthorityViolation(conflict);
                        break;
                }

                OnConflictDetected?.Invoke(conflict);
            }
        }

        /// <summary>
        /// Resolve position conflict
        /// </summary>
        private void ResolvePositionConflict(StateConflict conflict)
        {
            // Server position is authoritative
            if (currentState.Characters.TryGetValue(conflict.EntityId, out var character))
            {
                // Force position correction
                character.RequiresPositionCorrection = true;
            }
        }

        /// <summary>
        /// Resolve inventory conflict
        /// </summary>
        private void ResolveInventoryConflict(StateConflict conflict)
        {
            // Server inventory is authoritative
            if (currentState.Characters.TryGetValue(conflict.EntityId, out var character))
            {
                // Force inventory resync
                character.RequiresInventorySync = true;
            }
        }

        /// <summary>
        /// Resolve invalid state
        /// </summary>
        private void ResolveInvalidState(StateConflict conflict)
        {
            // Rollback to last valid state
            if (previousState != null && previousState.Characters.ContainsKey(conflict.EntityId))
            {
                currentState.Characters[conflict.EntityId] = previousState.Characters[conflict.EntityId].Clone();
            }
        }

        /// <summary>
        /// Handle authority violation
        /// </summary>
        private void HandleAuthorityViolation(StateConflict conflict)
        {
            Logger.Log($"Authority violation by client {conflict.ClientId}: {conflict.Details}");
            // Could kick client or send warning
        }

        /// <summary>
        /// Validate client authority over entities
        /// </summary>
        private bool ValidateClientAuthority(string clientId, StateDelta delta)
        {
            if (!clientStates.TryGetValue(clientId, out var clientInfo))
                return false;

            // Check each entity in delta
            foreach (var characterId in delta.CharacterChanges.Keys)
            {
                if (!clientInfo.AuthorizedCharacters.Contains(characterId))
                    return false;
            }

            foreach (var squadId in delta.SquadChanges.Keys)
            {
                if (!clientInfo.AuthorizedSquads.Contains(squadId))
                    return false;
            }

            return true;
        }

        /// <summary>
        /// Apply delta to current state
        /// </summary>
        private void ApplyDelta(StateDelta delta)
        {
            lock (currentState)
            {
                // Apply character changes
                foreach (var kvp in delta.CharacterChanges)
                {
                    currentState.Characters[kvp.Key] = kvp.Value;
                }

                // Apply squad changes
                foreach (var kvp in delta.SquadChanges)
                {
                    currentState.Squads[kvp.Key] = kvp.Value;
                }

                // Apply building changes
                foreach (var kvp in delta.BuildingChanges)
                {
                    currentState.Buildings[kvp.Key] = kvp.Value;
                }

                // Apply item changes
                foreach (var kvp in delta.ItemChanges)
                {
                    currentState.Items[kvp.Key] = kvp.Value;
                }
            }
        }

        /// <summary>
        /// Create full state delta
        /// </summary>
        private StateDelta CreateFullStateDelta()
        {
            return new StateDelta
            {
                BaseStateId = 0,
                TargetStateId = currentStateId,
                CharacterChanges = new Dictionary<string, CharacterState>(currentState.Characters),
                SquadChanges = new Dictionary<string, SquadState>(currentState.Squads),
                BuildingChanges = new Dictionary<string, BuildingState>(currentState.Buildings),
                ItemChanges = new Dictionary<string, ItemState>(currentState.Items)
            };
        }

        /// <summary>
        /// Update game time
        /// </summary>
        public void UpdateGameTime(double time, float multiplier)
        {
            serverTime = time;
            timeMultiplier = multiplier;

            if (!isServer)
            {
                // Calculate time delta for synchronization
                timeDelta = serverTime - localTime;
            }
        }

        /// <summary>
        /// Get current synchronized time
        /// </summary>
        public double GetCurrentTime()
        {
            if (isServer)
                return serverTime;
            else
                return localTime + timeDelta;
        }

        /// <summary>
        /// Register client (server only)
        /// </summary>
        public void RegisterClient(string clientId, List<string> authorizedCharacters, List<string> authorizedSquads)
        {
            clientStates[clientId] = new ClientStateInfo
            {
                ClientId = clientId,
                LastStateId = currentStateId,
                LastUpdateTime = GetCurrentTime(),
                AuthorizedCharacters = new HashSet<string>(authorizedCharacters),
                AuthorizedSquads = new HashSet<string>(authorizedSquads)
            };
        }

        /// <summary>
        /// Unregister client
        /// </summary>
        public void UnregisterClient(string clientId)
        {
            clientStates.TryRemove(clientId, out _);
        }
    }

    /// <summary>
    /// Complete game state
    /// </summary>
    public class GameState
    {
        public int StateId { get; set; }
        public double Timestamp { get; set; }

        public Dictionary<string, CharacterState> Characters { get; set; } = new Dictionary<string, CharacterState>();
        public Dictionary<string, SquadState> Squads { get; set; } = new Dictionary<string, SquadState>();
        public Dictionary<string, BuildingState> Buildings { get; set; } = new Dictionary<string, BuildingState>();
        public Dictionary<string, ItemState> Items { get; set; } = new Dictionary<string, ItemState>();
        public Dictionary<string, FactionState> Factions { get; set; } = new Dictionary<string, FactionState>();
        public WorldState World { get; set; } = new WorldState();

        public GameState Clone()
        {
            return new GameState
            {
                StateId = StateId,
                Timestamp = Timestamp,
                Characters = Characters.ToDictionary(k => k.Key, v => v.Value.Clone()),
                Squads = Squads.ToDictionary(k => k.Key, v => v.Value.Clone()),
                Buildings = Buildings.ToDictionary(k => k.Key, v => v.Value.Clone()),
                Items = Items.ToDictionary(k => k.Key, v => v.Value.Clone()),
                Factions = Factions.ToDictionary(k => k.Key, v => v.Value.Clone()),
                World = World.Clone()
            };
        }
    }

    /// <summary>
    /// Character state
    /// </summary>
    public class CharacterState
    {
        public string Id { get; set; }
        public string PlayerId { get; set; }
        public Vector3 Position { get; set; }
        public float Rotation { get; set; }
        public string CurrentAction { get; set; }
        public Dictionary<string, int> LimbHealth { get; set; }
        public Dictionary<string, float> Skills { get; set; }
        public List<string> InventoryItems { get; set; }
        public float Hunger { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsDead { get; set; }
        public bool RequiresPositionCorrection { get; set; }
        public bool RequiresInventorySync { get; set; }

        public CharacterState Clone()
        {
            return new CharacterState
            {
                Id = Id,
                PlayerId = PlayerId,
                Position = Position,
                Rotation = Rotation,
                CurrentAction = CurrentAction,
                LimbHealth = new Dictionary<string, int>(LimbHealth ?? new Dictionary<string, int>()),
                Skills = new Dictionary<string, float>(Skills ?? new Dictionary<string, float>()),
                InventoryItems = new List<string>(InventoryItems ?? new List<string>()),
                Hunger = Hunger,
                IsUnconscious = IsUnconscious,
                IsDead = IsDead
            };
        }
    }

    /// <summary>
    /// Squad state
    /// </summary>
    public class SquadState
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public List<string> Members { get; set; }
        public string LeaderId { get; set; }
        public string CurrentOrder { get; set; }
        public Vector3 TargetPosition { get; set; }
        public string Formation { get; set; }

        public SquadState Clone()
        {
            return new SquadState
            {
                Id = Id,
                Name = Name,
                Members = new List<string>(Members ?? new List<string>()),
                LeaderId = LeaderId,
                CurrentOrder = CurrentOrder,
                TargetPosition = TargetPosition,
                Formation = Formation
            };
        }
    }

    /// <summary>
    /// Building state
    /// </summary>
    public class BuildingState
    {
        public string Id { get; set; }
        public string Type { get; set; }
        public Vector3 Position { get; set; }
        public float Rotation { get; set; }
        public int Health { get; set; }
        public int MaxHealth { get; set; }
        public string OwnerId { get; set; }
        public bool IsConstructed { get; set; }
        public float ConstructionProgress { get; set; }
        public List<string> StoredItems { get; set; }

        public BuildingState Clone()
        {
            return new BuildingState
            {
                Id = Id,
                Type = Type,
                Position = Position,
                Rotation = Rotation,
                Health = Health,
                MaxHealth = MaxHealth,
                OwnerId = OwnerId,
                IsConstructed = IsConstructed,
                ConstructionProgress = ConstructionProgress,
                StoredItems = new List<string>(StoredItems ?? new List<string>())
            };
        }
    }

    /// <summary>
    /// Item state
    /// </summary>
    public class ItemState
    {
        public string Id { get; set; }
        public string Type { get; set; }
        public int Quantity { get; set; }
        public float Quality { get; set; }
        public string ContainerId { get; set; } // Character or building ID
        public Vector3 WorldPosition { get; set; } // If dropped
        public bool IsEquipped { get; set; }
        public string EquipSlot { get; set; }

        public ItemState Clone()
        {
            return new ItemState
            {
                Id = Id,
                Type = Type,
                Quantity = Quantity,
                Quality = Quality,
                ContainerId = ContainerId,
                WorldPosition = WorldPosition,
                IsEquipped = IsEquipped,
                EquipSlot = EquipSlot
            };
        }
    }

    /// <summary>
    /// Faction state
    /// </summary>
    public class FactionState
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public Dictionary<string, float> Relations { get; set; } // Relations with other factions
        public List<string> OwnedTowns { get; set; }
        public List<string> Members { get; set; }

        public FactionState Clone()
        {
            return new FactionState
            {
                Id = Id,
                Name = Name,
                Relations = new Dictionary<string, float>(Relations ?? new Dictionary<string, float>()),
                OwnedTowns = new List<string>(OwnedTowns ?? new List<string>()),
                Members = new List<string>(Members ?? new List<string>())
            };
        }
    }

    /// <summary>
    /// World state
    /// </summary>
    public class WorldState
    {
        public double GameTime { get; set; }
        public string Weather { get; set; }
        public float TimeMultiplier { get; set; }
        public List<string> ActiveEvents { get; set; }
        public Dictionary<string, TownState> Towns { get; set; }

        public WorldState Clone()
        {
            return new WorldState
            {
                GameTime = GameTime,
                Weather = Weather,
                TimeMultiplier = TimeMultiplier,
                ActiveEvents = new List<string>(ActiveEvents ?? new List<string>()),
                Towns = Towns?.ToDictionary(k => k.Key, v => v.Value.Clone()) ?? new Dictionary<string, TownState>()
            };
        }
    }

    /// <summary>
    /// Town state
    /// </summary>
    public class TownState
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string OwnerId { get; set; }
        public int Population { get; set; }
        public Dictionary<string, int> Resources { get; set; }

        public TownState Clone()
        {
            return new TownState
            {
                Id = Id,
                Name = Name,
                OwnerId = OwnerId,
                Population = Population,
                Resources = new Dictionary<string, int>(Resources ?? new Dictionary<string, int>())
            };
        }
    }

    /// <summary>
    /// State delta for efficient synchronization
    /// </summary>
    public class StateDelta
    {
        public int BaseStateId { get; set; }
        public int TargetStateId { get; set; }
        public Dictionary<string, CharacterState> CharacterChanges { get; set; }
        public Dictionary<string, SquadState> SquadChanges { get; set; }
        public Dictionary<string, BuildingState> BuildingChanges { get; set; }
        public Dictionary<string, ItemState> ItemChanges { get; set; }
    }

    /// <summary>
    /// State change event
    /// </summary>
    public class StateChange
    {
        public StateChangeType Type { get; set; }
        public string EntityId { get; set; }
        public object OldState { get; set; }
        public object NewState { get; set; }
    }

    public enum StateChangeType
    {
        Character,
        Squad,
        Building,
        Item,
        Faction,
        World
    }

    /// <summary>
    /// State conflict
    /// </summary>
    public class StateConflict
    {
        public ConflictType Type { get; set; }
        public string EntityId { get; set; }
        public string ClientId { get; set; }
        public string Details { get; set; }
    }

    public enum ConflictType
    {
        PositionMismatch,
        InventoryMismatch,
        InvalidState,
        AuthorityViolation,
        InvalidDelta
    }

    /// <summary>
    /// Client state information
    /// </summary>
    public class ClientStateInfo
    {
        public string ClientId { get; set; }
        public int LastStateId { get; set; }
        public double LastUpdateTime { get; set; }
        public HashSet<string> AuthorizedCharacters { get; set; }
        public HashSet<string> AuthorizedSquads { get; set; }
    }

    /// <summary>
    /// State validator
    /// </summary>
    public class StateValidator
    {
        public bool ValidateCharacterState(CharacterState oldState, CharacterState newState)
        {
            // Validate position changes aren't too large (anti-teleport)
            if (Vector3.Distance(oldState.Position, newState.Position) > 100)
                return false;

            // Validate health changes
            foreach (var limb in newState.LimbHealth)
            {
                if (limb.Value > 100 || limb.Value < 0)
                    return false;
            }

            // More validation...
            return true;
        }

        public bool ValidateDelta(GameState currentState, StateDelta delta)
        {
            // Validate delta is based on known state
            if (delta.BaseStateId > currentState.StateId)
                return false;

            // More validation...
            return true;
        }
    }

    /// <summary>
    /// Delta compression engine
    /// </summary>
    public class DeltaCompressor
    {
        public StateDelta CreateDelta(GameState from, GameState to)
        {
            var delta = new StateDelta
            {
                BaseStateId = from.StateId,
                TargetStateId = to.StateId,
                CharacterChanges = new Dictionary<string, CharacterState>(),
                SquadChanges = new Dictionary<string, SquadState>(),
                BuildingChanges = new Dictionary<string, BuildingState>(),
                ItemChanges = new Dictionary<string, ItemState>()
            };

            // Find character changes
            foreach (var kvp in to.Characters)
            {
                if (!from.Characters.ContainsKey(kvp.Key) ||
                    !AreCharacterStatesEqual(from.Characters[kvp.Key], kvp.Value))
                {
                    delta.CharacterChanges[kvp.Key] = kvp.Value;
                }
            }

            // Similar for other entity types...

            return delta;
        }

        private bool AreCharacterStatesEqual(CharacterState a, CharacterState b)
        {
            return a.Position == b.Position &&
                   a.Rotation == b.Rotation &&
                   a.CurrentAction == b.CurrentAction;
            // More comparisons...
        }
    }

    /// <summary>
    /// Prediction engine for client-side prediction
    /// </summary>
    public class PredictionEngine
    {
        public GameState Predict(GameState currentState, double deltaTime)
        {
            var predictedState = currentState.Clone();

            // Predict character positions based on velocity
            foreach (var character in predictedState.Characters.Values)
            {
                // Simple linear prediction
                // In real implementation, would consider current action, terrain, etc.
            }

            return predictedState;
        }
    }

    /// <summary>
    /// Interpolation engine for smooth rendering
    /// </summary>
    public class InterpolationEngine
    {
        public GameState Interpolate(GameState from, GameState to, float t)
        {
            var interpolated = from.Clone();

            // Interpolate character positions
            foreach (var characterId in from.Characters.Keys)
            {
                if (to.Characters.ContainsKey(characterId))
                {
                    var fromChar = from.Characters[characterId];
                    var toChar = to.Characters[characterId];

                    interpolated.Characters[characterId].Position = Vector3.Lerp(
                        fromChar.Position,
                        toChar.Position,
                        t
                    );

                    interpolated.Characters[characterId].Rotation =
                        fromChar.Rotation + (toChar.Rotation - fromChar.Rotation) * t;
                }
            }

            return interpolated;
        }
    }
}
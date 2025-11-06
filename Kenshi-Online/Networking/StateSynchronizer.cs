using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.IO;
using System.IO.Compression;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Manages state synchronization between server and clients using delta compression
    /// </summary>
    public class StateSynchronizer
    {
        // State management
        private WorldState currentWorldState;
        private readonly ConcurrentDictionary<long, WorldState> stateHistory = new ConcurrentDictionary<long, WorldState>();
        private readonly ConcurrentDictionary<string, ClientSyncState> clientStates = new ConcurrentDictionary<string, ClientSyncState>();
        
        // Delta compression
        private readonly DeltaCompressor deltaCompressor = new DeltaCompressor();
        private readonly int maxHistorySize = 100;
        private long currentStateVersion = 0;
        
        // Synchronization configuration
        private readonly int tickRate = 20; // 20 Hz
        private readonly int snapshotInterval = 60; // Full snapshot every 3 seconds
        private readonly int maxDeltaSize = 4096; // Max delta packet size
        
        // Interest management
        private readonly InterestManager interestManager = new InterestManager();
        
        // Prediction and interpolation
        private readonly ClientPrediction clientPrediction = new ClientPrediction();
        private readonly int interpolationDelay = 100; // 100ms interpolation buffer
        
        public StateSynchronizer()
        {
            currentWorldState = new WorldState
            {
                Version = 0,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Entities = new Dictionary<string, EntityState>(),
                GlobalState = new GlobalGameState()
            };
        }
        
        /// <summary>
        /// Update world state and generate deltas
        /// </summary>
        public void UpdateWorldState(StateUpdate update)
        {
            lock (currentWorldState)
            {
                // Clone current state for history
                var previousState = currentWorldState.Clone();
                stateHistory[currentStateVersion] = previousState;
                
                // Apply update
                ApplyStateUpdate(currentWorldState, update);
                
                // Increment version
                currentStateVersion++;
                currentWorldState.Version = currentStateVersion;
                currentWorldState.Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                
                // Clean old history
                CleanStateHistory();
                
                // Generate deltas for all clients
                GenerateClientDeltas();
            }
        }
        
        /// <summary>
        /// Apply state update to world state
        /// </summary>
        private void ApplyStateUpdate(WorldState state, StateUpdate update)
        {
            switch (update.Type)
            {
                case "entity_update":
                    UpdateEntity(state, update);
                    break;
                    
                case "entity_spawn":
                    SpawnEntity(state, update);
                    break;
                    
                case "entity_despawn":
                    DespawnEntity(state, update);
                    break;
                    
                case "global_update":
                    UpdateGlobalState(state, update);
                    break;
                    
                case "faction_update":
                    UpdateFactionState(state, update);
                    break;
                    
                case "weather_update":
                    UpdateWeatherState(state, update);
                    break;
            }
        }
        
        /// <summary>
        /// Update entity state
        /// </summary>
        private void UpdateEntity(WorldState state, StateUpdate update)
        {
            if (!state.Entities.TryGetValue(update.EntityId, out var entity))
            {
                entity = new EntityState { Id = update.EntityId };
                state.Entities[update.EntityId] = entity;
            }
            
            // Update entity properties
            foreach (var kvp in update.Data)
            {
                switch (kvp.Key)
                {
                    case "position":
                        entity.Position = JsonSerializer.Deserialize<Vector3>(kvp.Value.ToString());
                        entity.LastPositionUpdate = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                        break;
                        
                    case "health":
                        entity.Health = Convert.ToInt32(kvp.Value);
                        break;
                        
                    case "inventory":
                        entity.Inventory = JsonSerializer.Deserialize<Dictionary<string, int>>(kvp.Value.ToString());
                        break;
                        
                    case "state":
                        entity.CurrentState = kvp.Value.ToString();
                        break;
                        
                    case "velocity":
                        entity.Velocity = JsonSerializer.Deserialize<Vector3>(kvp.Value.ToString());
                        break;
                        
                    case "animation":
                        entity.CurrentAnimation = kvp.Value.ToString();
                        break;
                }
            }
        }
        
        /// <summary>
        /// Generate deltas for all connected clients
        /// </summary>
        private void GenerateClientDeltas()
        {
            Parallel.ForEach(clientStates, clientKvp =>
            {
                var clientId = clientKvp.Key;
                var clientState = clientKvp.Value;
                
                // Check if client needs full snapshot
                bool needsSnapshot = ShouldSendSnapshot(clientState);
                
                if (needsSnapshot)
                {
                    SendSnapshot(clientId, clientState);
                }
                else
                {
                    SendDelta(clientId, clientState);
                }
            });
        }
        
        /// <summary>
        /// Determine if client needs full snapshot
        /// </summary>
        private bool ShouldSendSnapshot(ClientSyncState clientState)
        {
            // Send snapshot if:
            // 1. Client has never received a snapshot
            if (clientState.LastSnapshotVersion == 0)
                return true;
            
            // 2. Too many versions behind
            if (currentStateVersion - clientState.LastAcknowledgedVersion > 30)
                return true;
            
            // 3. Time for periodic snapshot
            if (currentStateVersion - clientState.LastSnapshotVersion >= snapshotInterval)
                return true;
            
            // 4. Client requested snapshot
            if (clientState.SnapshotRequested)
                return true;
            
            return false;
        }
        
        /// <summary>
        /// Send full snapshot to client
        /// </summary>
        private void SendSnapshot(string clientId, ClientSyncState clientState)
        {
            // Get entities in client's interest area
            var relevantEntities = interestManager.GetRelevantEntities(clientId, currentWorldState);
            
            // Create snapshot
            var snapshot = new StateSnapshot
            {
                Version = currentStateVersion,
                Timestamp = currentWorldState.Timestamp,
                IsFullSnapshot = true,
                Entities = relevantEntities,
                GlobalState = currentWorldState.GlobalState
            };
            
            // Compress snapshot
            var compressedData = CompressSnapshot(snapshot);
            
            // Send to client
            SendToClient(clientId, new NetworkPacket
            {
                Type = PacketType.StateSnapshot,
                Data = compressedData,
                Version = currentStateVersion,
                RequiresAck = true
            });
            
            // Update client state
            clientState.LastSnapshotVersion = currentStateVersion;
            clientState.SnapshotRequested = false;
            
            Logger.Log($"Sent snapshot v{currentStateVersion} to {clientId} ({compressedData.Length} bytes)");
        }
        
        /// <summary>
        /// Send delta update to client
        /// </summary>
        private void SendDelta(string clientId, ClientSyncState clientState)
        {
            // Get base version for delta
            long baseVersion = clientState.LastAcknowledgedVersion;
            
            if (!stateHistory.TryGetValue(baseVersion, out var baseState))
            {
                // Base state not in history, need snapshot
                clientState.SnapshotRequested = true;
                return;
            }
            
            // Generate delta
            var delta = deltaCompressor.GenerateDelta(baseState, currentWorldState, clientId);
            
            // Check delta size
            if (delta.CompressedSize > maxDeltaSize)
            {
                // Delta too large, send snapshot instead
                clientState.SnapshotRequested = true;
                return;
            }
            
            // Send delta to client
            SendToClient(clientId, new NetworkPacket
            {
                Type = PacketType.StateDelta,
                Data = delta.CompressedData,
                Version = currentStateVersion,
                BaseVersion = baseVersion,
                RequiresAck = true
            });
            
            Logger.Log($"Sent delta v{baseVersion}->{currentStateVersion} to {clientId} ({delta.CompressedSize} bytes)");
        }
        
        /// <summary>
        /// Handle client acknowledgment
        /// </summary>
        public void HandleClientAck(string clientId, long version)
        {
            if (clientStates.TryGetValue(clientId, out var clientState))
            {
                clientState.LastAcknowledgedVersion = Math.Max(clientState.LastAcknowledgedVersion, version);
                clientState.LastAckTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                
                // Update RTT estimate
                if (clientState.PendingAcks.TryRemove(version, out var sentTime))
                {
                    var rtt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - sentTime;
                    clientState.UpdateRTT(rtt);
                }
            }
        }
        
        /// <summary>
        /// Handle client input for prediction
        /// </summary>
        public void HandleClientInput(string clientId, ClientInput input)
        {
            // Validate input
            if (!ValidateClientInput(clientId, input))
            {
                Logger.Log($"Invalid input from {clientId}");
                return;
            }
            
            // Apply input to world state
            var update = new StateUpdate
            {
                Type = "entity_update",
                EntityId = clientId,
                Data = new Dictionary<string, object>
                {
                    { "position", input.Position },
                    { "velocity", input.Velocity },
                    { "input_sequence", input.SequenceNumber }
                }
            };
            
            UpdateWorldState(update);
            
            // Send prediction correction if needed
            if (clientStates.TryGetValue(clientId, out var clientState))
            {
                var correction = clientPrediction.CheckPrediction(clientId, input, currentWorldState);
                
                if (correction != null)
                {
                    SendPredictionCorrection(clientId, correction);
                }
            }
        }
        
        /// <summary>
        /// Validate client input
        /// </summary>
        private bool ValidateClientInput(string clientId, ClientInput input)
        {
            // Check sequence number
            if (clientStates.TryGetValue(clientId, out var clientState))
            {
                if (input.SequenceNumber <= clientState.LastProcessedInput)
                    return false; // Old input
                
                // Check for speed hacks
                if (input.Velocity.Magnitude() > 15.0f) // Max speed in Kenshi
                    return false;
                
                // Check for teleportation
                if (currentWorldState.Entities.TryGetValue(clientId, out var entity))
                {
                    var distance = Vector3.Distance(entity.Position, input.Position);
                    var timeDelta = (input.Timestamp - entity.LastPositionUpdate) / 1000.0f;
                    
                    if (distance / timeDelta > 20.0f) // Way too fast
                        return false;
                }
                
                clientState.LastProcessedInput = input.SequenceNumber;
            }
            
            return true;
        }
        
        /// <summary>
        /// Send prediction correction to client
        /// </summary>
        private void SendPredictionCorrection(string clientId, PredictionCorrection correction)
        {
            SendToClient(clientId, new NetworkPacket
            {
                Type = PacketType.PredictionCorrection,
                Data = SerializeCorrection(correction),
                Version = currentStateVersion,
                RequiresAck = false
            });
        }
        
        /// <summary>
        /// Register new client for synchronization
        /// </summary>
        public void RegisterClient(string clientId, ClientInfo info)
        {
            var clientState = new ClientSyncState
            {
                ClientId = clientId,
                ConnectionTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                InterestArea = new InterestArea
                {
                    Center = info.InitialPosition,
                    Radius = 5000 // 5km view distance
                }
            };
            
            clientStates[clientId] = clientState;
            interestManager.RegisterClient(clientId, clientState.InterestArea);
            
            Logger.Log($"Registered client {clientId} for state sync");
        }
        
        /// <summary>
        /// Unregister client
        /// </summary>
        public void UnregisterClient(string clientId)
        {
            clientStates.TryRemove(clientId, out _);
            interestManager.UnregisterClient(clientId);
            
            Logger.Log($"Unregistered client {clientId} from state sync");
        }
        
        /// <summary>
        /// Clean old state history
        /// </summary>
        private void CleanStateHistory()
        {
            var cutoffVersion = currentStateVersion - maxHistorySize;
            
            var toRemove = stateHistory.Keys.Where(v => v < cutoffVersion).ToList();
            foreach (var version in toRemove)
            {
                stateHistory.TryRemove(version, out _);
            }
        }
        
        /// <summary>
        /// Compress snapshot for network transmission
        /// </summary>
        private byte[] CompressSnapshot(StateSnapshot snapshot)
        {
            var json = JsonSerializer.Serialize(snapshot);
            var bytes = Encoding.UTF8.GetBytes(json);
            
            using (var output = new MemoryStream())
            {
                using (var gzip = new GZipStream(output, CompressionLevel.Optimal))
                {
                    gzip.Write(bytes, 0, bytes.Length);
                }
                return output.ToArray();
            }
        }
        
        private byte[] SerializeCorrection(PredictionCorrection correction)
        {
            var json = JsonSerializer.Serialize(correction);
            return Encoding.UTF8.GetBytes(json);
        }
        
        private void SendToClient(string clientId, NetworkPacket packet)
        {
            // This would integrate with your network layer
            // Track pending acks if required
            if (packet.RequiresAck && clientStates.TryGetValue(clientId, out var clientState))
            {
                clientState.PendingAcks[packet.Version] = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
        }
        
        private void SpawnEntity(WorldState state, StateUpdate update)
        {
            var entity = new EntityState
            {
                Id = update.EntityId,
                Position = new Vector3(),
                Health = 100,
                Inventory = new Dictionary<string, int>(),
                CurrentState = "idle"
            };
            
            state.Entities[update.EntityId] = entity;
        }
        
        private void DespawnEntity(WorldState state, StateUpdate update)
        {
            state.Entities.Remove(update.EntityId);
        }
        
        private void UpdateGlobalState(WorldState state, StateUpdate update)
        {
            foreach (var kvp in update.Data)
            {
                switch (kvp.Key)
                {
                    case "time":
                        state.GlobalState.GameTime = Convert.ToSingle(kvp.Value);
                        break;
                    case "weather":
                        state.GlobalState.Weather = kvp.Value.ToString();
                        break;
                }
            }
        }
        
        private void UpdateFactionState(WorldState state, StateUpdate update)
        {
            // Update faction relationships
            if (update.Data.TryGetValue("relations", out var relations))
            {
                state.GlobalState.FactionRelations = JsonSerializer.Deserialize<Dictionary<string, int>>(relations.ToString());
            }
        }
        
        private void UpdateWeatherState(WorldState state, StateUpdate update)
        {
            if (update.Data.TryGetValue("weather", out var weather))
            {
                state.GlobalState.Weather = weather.ToString();
            }
        }
    }
    
    /// <summary>
    /// Delta compression for state updates
    /// </summary>
    public class DeltaCompressor
    {
        public StateDelta GenerateDelta(WorldState oldState, WorldState newState, string clientId)
        {
            var delta = new StateDelta
            {
                BaseVersion = oldState.Version,
                TargetVersion = newState.Version
            };
            
            // Find entity changes
            foreach (var kvp in newState.Entities)
            {
                var entityId = kvp.Key;
                var newEntity = kvp.Value;
                
                if (!oldState.Entities.TryGetValue(entityId, out var oldEntity))
                {
                    // New entity
                    delta.AddedEntities[entityId] = newEntity;
                }
                else if (HasEntityChanged(oldEntity, newEntity))
                {
                    // Modified entity
                    delta.ModifiedEntities[entityId] = GenerateEntityDelta(oldEntity, newEntity);
                }
            }
            
            // Find removed entities
            foreach (var entityId in oldState.Entities.Keys)
            {
                if (!newState.Entities.ContainsKey(entityId))
                {
                    delta.RemovedEntities.Add(entityId);
                }
            }
            
            // Compress delta
            delta.CompressedData = CompressDelta(delta);
            delta.CompressedSize = delta.CompressedData.Length;
            
            return delta;
        }
        
        private bool HasEntityChanged(EntityState old, EntityState current)
        {
            if (!old.Position.Equals(current.Position)) return true;
            if (old.Health != current.Health) return true;
            if (old.CurrentState != current.CurrentState) return true;
            if (!old.Velocity.Equals(current.Velocity)) return true;
            
            return false;
        }
        
        private EntityDelta GenerateEntityDelta(EntityState old, EntityState current)
        {
            var delta = new EntityDelta { Id = current.Id };
            
            if (!old.Position.Equals(current.Position))
            {
                delta.Position = current.Position;
                delta.HasPositionChange = true;
            }
            
            if (old.Health != current.Health)
            {
                delta.Health = current.Health;
                delta.HasHealthChange = true;
            }
            
            if (old.CurrentState != current.CurrentState)
            {
                delta.State = current.CurrentState;
                delta.HasStateChange = true;
            }
            
            if (!old.Velocity.Equals(current.Velocity))
            {
                delta.Velocity = current.Velocity;
                delta.HasVelocityChange = true;
            }
            
            return delta;
        }
        
        private byte[] CompressDelta(StateDelta delta)
        {
            var json = JsonSerializer.Serialize(delta);
            var bytes = Encoding.UTF8.GetBytes(json);
            
            using (var output = new MemoryStream())
            {
                using (var gzip = new GZipStream(output, CompressionLevel.Fastest))
                {
                    gzip.Write(bytes, 0, bytes.Length);
                }
                return output.ToArray();
            }
        }
    }
    
    /// <summary>
    /// Interest management for bandwidth optimization
    /// </summary>
    public class InterestManager
    {
        private readonly ConcurrentDictionary<string, InterestArea> clientInterests = new ConcurrentDictionary<string, InterestArea>();
        
        public void RegisterClient(string clientId, InterestArea area)
        {
            clientInterests[clientId] = area;
        }
        
        public void UnregisterClient(string clientId)
        {
            clientInterests.TryRemove(clientId, out _);
        }
        
        public void UpdateClientInterest(string clientId, Vector3 position)
        {
            if (clientInterests.TryGetValue(clientId, out var area))
            {
                area.Center = position;
            }
        }
        
        public Dictionary<string, EntityState> GetRelevantEntities(string clientId, WorldState worldState)
        {
            if (!clientInterests.TryGetValue(clientId, out var area))
                return worldState.Entities; // Return all if no interest area
            
            var relevant = new Dictionary<string, EntityState>();
            
            foreach (var kvp in worldState.Entities)
            {
                var entity = kvp.Value;
                
                // Always include the client's own entity
                if (kvp.Key == clientId)
                {
                    relevant[kvp.Key] = entity;
                    continue;
                }
                
                // Check if entity is in interest area
                var distance = Vector3.Distance(area.Center, entity.Position);
                
                if (distance <= area.Radius)
                {
                    relevant[kvp.Key] = entity;
                }
                else if (distance <= area.Radius * 1.5f && entity.Priority > 0)
                {
                    // Include high-priority entities at extended range
                    relevant[kvp.Key] = entity;
                }
            }
            
            return relevant;
        }
    }
    
    /// <summary>
    /// Client-side prediction system
    /// </summary>
    public class ClientPrediction
    {
        private readonly ConcurrentDictionary<string, PredictionState> clientPredictions = new ConcurrentDictionary<string, PredictionState>();
        
        public PredictionCorrection CheckPrediction(string clientId, ClientInput input, WorldState worldState)
        {
            if (!worldState.Entities.TryGetValue(clientId, out var serverEntity))
                return null;
            
            // Get or create prediction state
            if (!clientPredictions.TryGetValue(clientId, out var prediction))
            {
                prediction = new PredictionState { ClientId = clientId };
                clientPredictions[clientId] = prediction;
            }
            
            // Calculate predicted position based on input
            var predictedPos = PredictPosition(input);
            
            // Check divergence
            var divergence = Vector3.Distance(serverEntity.Position, predictedPos);
            
            if (divergence > 0.5f) // Threshold for correction
            {
                return new PredictionCorrection
                {
                    SequenceNumber = input.SequenceNumber,
                    ServerPosition = serverEntity.Position,
                    ServerVelocity = serverEntity.Velocity,
                    Divergence = divergence
                };
            }
            
            return null;
        }
        
        private Vector3 PredictPosition(ClientInput input)
        {
            // Simple prediction based on velocity
            var deltaTime = 0.05f; // 50ms tick
            return new Vector3(
                input.Position.X + input.Velocity.X * deltaTime,
                input.Position.Y + input.Velocity.Y * deltaTime,
                input.Position.Z + input.Velocity.Z * deltaTime
            );
        }
    }
    
    // Supporting classes
    
    public class WorldState
    {
        public long Version { get; set; }
        public long Timestamp { get; set; }
        public Dictionary<string, EntityState> Entities { get; set; }
        public GlobalGameState GlobalState { get; set; }
        
        public WorldState Clone()
        {
            var json = JsonSerializer.Serialize(this);
            return JsonSerializer.Deserialize<WorldState>(json);
        }
    }
    
    public class EntityState
    {
        public string Id { get; set; }
        public Vector3 Position { get; set; }
        public Vector3 Velocity { get; set; }
        public int Health { get; set; }
        public Dictionary<string, int> Inventory { get; set; }
        public string CurrentState { get; set; }
        public string CurrentAnimation { get; set; }
        public long LastPositionUpdate { get; set; }
        public int Priority { get; set; } // For interest management
    }
    
    public class GlobalGameState
    {
        public float GameTime { get; set; }
        public string Weather { get; set; }
        public Dictionary<string, int> FactionRelations { get; set; } = new Dictionary<string, int>();
    }
    
    public class ClientSyncState
    {
        public string ClientId { get; set; }
        public long LastAcknowledgedVersion { get; set; }
        public long LastSnapshotVersion { get; set; }
        public long LastProcessedInput { get; set; }
        public bool SnapshotRequested { get; set; }
        public long ConnectionTime { get; set; }
        public long LastAckTime { get; set; }
        public InterestArea InterestArea { get; set; }
        public ConcurrentDictionary<long, long> PendingAcks { get; set; } = new ConcurrentDictionary<long, long>();
        
        private double estimatedRTT = 100; // Start with 100ms estimate
        private double rttVariance = 0;
        
        public void UpdateRTT(long measuredRTT)
        {
            // Exponential moving average
            estimatedRTT = 0.875 * estimatedRTT + 0.125 * measuredRTT;
            rttVariance = 0.75 * rttVariance + 0.25 * Math.Abs(measuredRTT - estimatedRTT);
        }
        
        public double GetRTT() => estimatedRTT;
        public double GetJitter() => rttVariance;
    }
    
    public class StateSnapshot
    {
        public long Version { get; set; }
        public long Timestamp { get; set; }
        public bool IsFullSnapshot { get; set; }
        public Dictionary<string, EntityState> Entities { get; set; }
        public GlobalGameState GlobalState { get; set; }
    }
    
    public class StateDelta
    {
        public long BaseVersion { get; set; }
        public long TargetVersion { get; set; }
        public Dictionary<string, EntityState> AddedEntities { get; set; } = new Dictionary<string, EntityState>();
        public Dictionary<string, EntityDelta> ModifiedEntities { get; set; } = new Dictionary<string, EntityDelta>();
        public List<string> RemovedEntities { get; set; } = new List<string>();
        public byte[] CompressedData { get; set; }
        public int CompressedSize { get; set; }
    }
    
    public class EntityDelta
    {
        public string Id { get; set; }
        public Vector3 Position { get; set; }
        public bool HasPositionChange { get; set; }
        public Vector3 Velocity { get; set; }
        public bool HasVelocityChange { get; set; }
        public int Health { get; set; }
        public bool HasHealthChange { get; set; }
        public string State { get; set; }
        public bool HasStateChange { get; set; }
    }
    
    public class ClientInput
    {
        public long SequenceNumber { get; set; }
        public long Timestamp { get; set; }
        public Vector3 Position { get; set; }
        public Vector3 Velocity { get; set; }
        public Dictionary<string, float> Actions { get; set; } = new Dictionary<string, float>();
    }
    
    public class PredictionCorrection
    {
        public long SequenceNumber { get; set; }
        public Vector3 ServerPosition { get; set; }
        public Vector3 ServerVelocity { get; set; }
        public float Divergence { get; set; }
    }
    
    public class InterestArea
    {
        public Vector3 Center { get; set; }
        public float Radius { get; set; }
    }
    
    public class ClientInfo
    {
        public Vector3 InitialPosition { get; set; }
        public string Username { get; set; }
    }
    
    public class NetworkPacket
    {
        public PacketType Type { get; set; }
        public byte[] Data { get; set; }
        public long Version { get; set; }
        public long BaseVersion { get; set; }
        public bool RequiresAck { get; set; }
    }
    
    public enum PacketType
    {
        StateSnapshot,
        StateDelta,
        PredictionCorrection,
        ClientInput,
        Acknowledgment
    }
    
    public class PredictionState
    {
        public string ClientId { get; set; }
        public Queue<ClientInput> InputHistory { get; set; } = new Queue<ClientInput>();
        public Vector3 LastPredictedPosition { get; set; }
    }
}
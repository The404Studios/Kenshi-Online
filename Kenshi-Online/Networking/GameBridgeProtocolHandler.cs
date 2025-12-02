using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Numerics;
using System.Net.Sockets;
using System.Text;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Handles the enhanced GameBridge protocol messages from the C++ DLL
    /// Protocol uses pipe-delimited format for efficiency
    /// </summary>
    public class GameBridgeProtocolHandler
    {
        // Connected clients with their player states
        private readonly ConcurrentDictionary<TcpClient, ClientGameState> _clientStates = new();

        // World state (server authoritative)
        private ServerWorldState _worldState = new();

        // Combat events pending broadcast
        private readonly ConcurrentQueue<CombatEventData> _pendingCombatEvents = new();

        // Sync tick for ordering
        private ulong _serverTick = 0;

        // Tick rate configuration
        private const int TICK_RATE_HZ = 20;
        private const int WORLD_SYNC_INTERVAL_TICKS = 60; // Every 3 seconds

        /// <summary>
        /// Process an incoming message from the DLL
        /// Returns response messages to send back, or null if broadcast needed
        /// </summary>
        public List<string> ProcessMessage(string message, TcpClient client)
        {
            if (string.IsNullOrEmpty(message))
                return null;

            var parts = message.Split('|');
            if (parts.Length == 0)
                return null;

            var command = parts[0];
            var responses = new List<string>();

            try
            {
                switch (command)
                {
                    case "STATE2":
                        HandleEnhancedState(parts, client);
                        break;

                    case "WORLD":
                        HandleWorldState(parts, client);
                        break;

                    case "COMBAT":
                        HandleCombatEvent(parts, client);
                        break;

                    case "INVENTORY":
                        HandleInventoryEvent(parts, client);
                        break;

                    case "STATE":
                        // Legacy state message - convert to new format
                        HandleLegacyState(parts, client);
                        break;

                    default:
                        // Unknown command - pass through to standard handler
                        return null;
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"[GameBridgeProtocol] Error processing {command}: {ex.Message}");
            }

            return responses;
        }

        /// <summary>
        /// Handle enhanced state message: STATE2|charID|name|x|y|z|qx|qy|qz|qw|health|maxHealth|blood|hunger|thirst|state|faction|combat|unconscious|dead|tick
        /// </summary>
        private void HandleEnhancedState(string[] parts, TcpClient client)
        {
            if (parts.Length < 20)
                return;

            var state = new PlayerCharacterState
            {
                CharacterId = uint.Parse(parts[1]),
                Name = parts[2],
                Position = new Vector3(
                    float.Parse(parts[3]),
                    float.Parse(parts[4]),
                    float.Parse(parts[5])
                ),
                Rotation = new Quaternion(
                    float.Parse(parts[6]),
                    float.Parse(parts[7]),
                    float.Parse(parts[8]),
                    float.Parse(parts[9])
                ),
                Health = float.Parse(parts[10]),
                MaxHealth = float.Parse(parts[11]),
                BloodLevel = float.Parse(parts[12]),
                Hunger = float.Parse(parts[13]),
                Thirst = float.Parse(parts[14]),
                State = (AIState)int.Parse(parts[15]),
                FactionId = int.Parse(parts[16]),
                IsInCombat = parts[17] == "1",
                IsUnconscious = parts[18] == "1",
                IsDead = parts[19] == "1",
                SyncTick = parts.Length > 20 ? ulong.Parse(parts[20]) : 0,
                LastUpdate = DateTime.UtcNow
            };

            // Update client state
            if (!_clientStates.TryGetValue(client, out var clientState))
            {
                clientState = new ClientGameState();
                _clientStates[client] = clientState;
            }

            clientState.Characters[state.CharacterId] = state;
            clientState.LastUpdate = DateTime.UtcNow;

            // Broadcast to other clients
            BroadcastStateUpdate(state, client);
        }

        /// <summary>
        /// Handle world state message: WORLD|time|day|year|timeScale|weather|weatherIntensity|temperature|money|tick
        /// </summary>
        private void HandleWorldState(string[] parts, TcpClient client)
        {
            if (parts.Length < 9)
                return;

            // Only accept world state from host or first client
            var receivedWorld = new ServerWorldState
            {
                GameTime = float.Parse(parts[1]),
                GameDay = int.Parse(parts[2]),
                GameYear = int.Parse(parts[3]),
                TimeScale = float.Parse(parts[4]),
                Weather = (WeatherType)int.Parse(parts[5]),
                WeatherIntensity = float.Parse(parts[6]),
                Temperature = float.Parse(parts[7]),
                PlayerMoney = int.Parse(parts[8]),
                SyncTick = parts.Length > 9 ? ulong.Parse(parts[9]) : 0
            };

            // Server is authoritative - merge or accept based on policy
            // For now, accept if newer
            if (receivedWorld.SyncTick > _worldState.SyncTick)
            {
                _worldState = receivedWorld;
                BroadcastWorldState(client);
            }
        }

        /// <summary>
        /// Handle combat event: COMBAT|attackerId|defenderId|attackType|damageType|limb|damage|blocked|dodged|crit|knockdown|timestamp
        /// </summary>
        private void HandleCombatEvent(string[] parts, TcpClient client)
        {
            if (parts.Length < 11)
                return;

            var combat = new CombatEventData
            {
                AttackerId = uint.Parse(parts[1]),
                DefenderId = uint.Parse(parts[2]),
                AttackType = (AttackType)int.Parse(parts[3]),
                DamageType = (DamageType)int.Parse(parts[4]),
                TargetLimb = (LimbType)int.Parse(parts[5]),
                Damage = float.Parse(parts[6]),
                WasBlocked = parts[7] == "1",
                WasDodged = parts[8] == "1",
                IsCritical = parts[9] == "1",
                CausedKnockdown = parts[10] == "1",
                Timestamp = parts.Length > 11 ? float.Parse(parts[11]) : 0f,
                SourceClient = client
            };

            // Validate combat (basic anti-cheat)
            if (ValidateCombatEvent(combat))
            {
                // Broadcast to all other clients
                BroadcastCombatEvent(combat, client);
            }
        }

        /// <summary>
        /// Handle inventory event: INVENTORY|charId|itemId|templateId|quantityChange|slot|timestamp
        /// </summary>
        private void HandleInventoryEvent(string[] parts, TcpClient client)
        {
            if (parts.Length < 6)
                return;

            var inventory = new InventoryEventData
            {
                CharacterId = uint.Parse(parts[1]),
                ItemId = uint.Parse(parts[2]),
                ItemTemplateId = uint.Parse(parts[3]),
                QuantityChange = int.Parse(parts[4]),
                SlotIndex = int.Parse(parts[5]),
                Timestamp = parts.Length > 6 ? float.Parse(parts[6]) : 0f
            };

            // Broadcast to other clients
            BroadcastInventoryEvent(inventory, client);
        }

        /// <summary>
        /// Handle legacy state message: STATE|charID|x|y|z|health|maxHealth|state|faction|combat
        /// </summary>
        private void HandleLegacyState(string[] parts, TcpClient client)
        {
            if (parts.Length < 10)
                return;

            var state = new PlayerCharacterState
            {
                CharacterId = uint.Parse(parts[1]),
                Position = new Vector3(
                    float.Parse(parts[2]),
                    float.Parse(parts[3]),
                    float.Parse(parts[4])
                ),
                Health = float.Parse(parts[5]),
                MaxHealth = float.Parse(parts[6]),
                State = (AIState)int.Parse(parts[7]),
                FactionId = int.Parse(parts[8]),
                IsInCombat = parts[9] == "1",
                LastUpdate = DateTime.UtcNow
            };

            if (!_clientStates.TryGetValue(client, out var clientState))
            {
                clientState = new ClientGameState();
                _clientStates[client] = clientState;
            }

            clientState.Characters[state.CharacterId] = state;
            BroadcastStateUpdate(state, client);
        }

        #region Broadcasting

        /// <summary>
        /// Broadcast state update to all clients except sender
        /// </summary>
        private void BroadcastStateUpdate(PlayerCharacterState state, TcpClient sender)
        {
            var message = $"STATE_UPDATE|{state.CharacterId}|" +
                         $"{state.Position.X:F2}|{state.Position.Y:F2}|{state.Position.Z:F2}|" +
                         $"{state.Rotation.X:F4}|{state.Rotation.Y:F4}|{state.Rotation.Z:F4}|{state.Rotation.W:F4}|" +
                         $"{state.Health:F2}|{state.BloodLevel:F2}|{state.Hunger:F2}|" +
                         $"{(int)state.State}|{(state.IsInCombat ? 1 : 0)}";

            BroadcastToOthers(message, sender);
        }

        /// <summary>
        /// Broadcast world state to all clients except sender
        /// </summary>
        private void BroadcastWorldState(TcpClient sender)
        {
            var message = $"SET_TIME|{_worldState.GameTime:F2}\n" +
                         $"SET_WEATHER|{(int)_worldState.Weather}|{_worldState.WeatherIntensity:F2}";

            BroadcastToOthers(message, sender);
        }

        /// <summary>
        /// Broadcast combat event to all clients except sender
        /// </summary>
        private void BroadcastCombatEvent(CombatEventData combat, TcpClient sender)
        {
            var message = $"COMBAT_EVENT|{combat.AttackerId}|{combat.DefenderId}|" +
                         $"{(int)combat.AttackType}|{(int)combat.DamageType}|{(int)combat.TargetLimb}|" +
                         $"{combat.Damage:F2}|{(combat.WasBlocked ? 1 : 0)}|{(combat.WasDodged ? 1 : 0)}|" +
                         $"{(combat.IsCritical ? 1 : 0)}|{(combat.CausedKnockdown ? 1 : 0)}";

            BroadcastToOthers(message, sender);
        }

        /// <summary>
        /// Broadcast inventory event to all clients except sender
        /// </summary>
        private void BroadcastInventoryEvent(InventoryEventData inventory, TcpClient sender)
        {
            // For now, just log - full sync would need item validation
            Logger.Log($"[Inventory] Character {inventory.CharacterId} item change: " +
                      $"{inventory.ItemTemplateId} x{inventory.QuantityChange}");
        }

        /// <summary>
        /// Send message to all connected clients except the sender
        /// </summary>
        private void BroadcastToOthers(string message, TcpClient sender)
        {
            var data = Encoding.UTF8.GetBytes(message + "\n");

            foreach (var kvp in _clientStates)
            {
                if (kvp.Key == sender)
                    continue;

                try
                {
                    if (kvp.Key.Connected)
                    {
                        kvp.Key.GetStream().Write(data, 0, data.Length);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"[Broadcast] Failed to send to client: {ex.Message}");
                }
            }
        }

        #endregion

        #region Validation

        /// <summary>
        /// Basic combat validation (anti-cheat)
        /// </summary>
        private bool ValidateCombatEvent(CombatEventData combat)
        {
            // Check damage is within reasonable bounds
            if (combat.Damage < 0 || combat.Damage > 1000)
            {
                Logger.Log($"[AntiCheat] Suspicious damage value: {combat.Damage}");
                return false;
            }

            // Check attacker owns the character
            if (combat.SourceClient != null &&
                _clientStates.TryGetValue(combat.SourceClient, out var clientState))
            {
                if (!clientState.Characters.ContainsKey(combat.AttackerId))
                {
                    Logger.Log($"[AntiCheat] Client attacking with non-owned character");
                    return false;
                }
            }

            return true;
        }

        #endregion

        #region Client Management

        /// <summary>
        /// Register a new client connection
        /// </summary>
        public void RegisterClient(TcpClient client, string username)
        {
            _clientStates[client] = new ClientGameState
            {
                Username = username,
                ConnectedAt = DateTime.UtcNow
            };
        }

        /// <summary>
        /// Remove a disconnected client
        /// </summary>
        public void RemoveClient(TcpClient client)
        {
            _clientStates.TryRemove(client, out _);
        }

        /// <summary>
        /// Get all player states for a client's interest area
        /// </summary>
        public IEnumerable<PlayerCharacterState> GetPlayersInRange(Vector3 position, float range)
        {
            var rangeSq = range * range;

            foreach (var clientState in _clientStates.Values)
            {
                foreach (var character in clientState.Characters.Values)
                {
                    var diff = character.Position - position;
                    var distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;

                    if (distSq <= rangeSq)
                    {
                        yield return character;
                    }
                }
            }
        }

        /// <summary>
        /// Update server tick
        /// </summary>
        public void Tick()
        {
            _serverTick++;
        }

        #endregion
    }

    #region Data Classes

    /// <summary>
    /// State for a connected client
    /// </summary>
    public class ClientGameState
    {
        public string Username { get; set; }
        public DateTime ConnectedAt { get; set; }
        public DateTime LastUpdate { get; set; }
        public ConcurrentDictionary<uint, PlayerCharacterState> Characters { get; set; } = new();
    }

    /// <summary>
    /// Player character state for sync
    /// </summary>
    public class PlayerCharacterState
    {
        public uint CharacterId { get; set; }
        public string Name { get; set; }
        public Vector3 Position { get; set; }
        public Quaternion Rotation { get; set; }
        public Vector3 Velocity { get; set; }
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public float BloodLevel { get; set; }
        public float Hunger { get; set; }
        public float Thirst { get; set; }
        public AIState State { get; set; }
        public int FactionId { get; set; }
        public bool IsInCombat { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsDead { get; set; }
        public ulong SyncTick { get; set; }
        public DateTime LastUpdate { get; set; }
    }

    /// <summary>
    /// Server-side world state
    /// </summary>
    public class ServerWorldState
    {
        public float GameTime { get; set; }
        public int GameDay { get; set; }
        public int GameYear { get; set; }
        public float TimeScale { get; set; } = 1.0f;
        public WeatherType Weather { get; set; }
        public float WeatherIntensity { get; set; }
        public float Temperature { get; set; }
        public int PlayerMoney { get; set; }
        public ulong SyncTick { get; set; }
    }

    /// <summary>
    /// Combat event data
    /// </summary>
    public class CombatEventData
    {
        public uint AttackerId { get; set; }
        public uint DefenderId { get; set; }
        public AttackType AttackType { get; set; }
        public DamageType DamageType { get; set; }
        public LimbType TargetLimb { get; set; }
        public float Damage { get; set; }
        public bool WasBlocked { get; set; }
        public bool WasDodged { get; set; }
        public bool IsCritical { get; set; }
        public bool CausedKnockdown { get; set; }
        public float Timestamp { get; set; }
        public TcpClient SourceClient { get; set; }
    }

    /// <summary>
    /// Inventory event data
    /// </summary>
    public class InventoryEventData
    {
        public uint CharacterId { get; set; }
        public uint ItemId { get; set; }
        public uint ItemTemplateId { get; set; }
        public int QuantityChange { get; set; }
        public int SlotIndex { get; set; }
        public float Timestamp { get; set; }
    }

    #endregion
}

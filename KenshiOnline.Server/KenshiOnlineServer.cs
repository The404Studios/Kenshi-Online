using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiOnline.Core.Entities;
using KenshiOnline.Core.Synchronization;
using KenshiOnline.Core.Session;
using KenshiOnline.Core.Admin;

namespace KenshiOnline.Server
{
    /// <summary>
    /// Message types for network protocol
    /// </summary>
    public enum MessageType
    {
        // Connection
        Connect,
        Disconnect,
        Heartbeat,

        // Entity sync
        EntityUpdate,
        EntityCreate,
        EntityDestroy,
        EntitySnapshot,

        // Combat
        CombatEvent,

        // Inventory
        InventoryAction,

        // World
        WorldState,

        // Admin
        AdminCommand,

        // Response
        Response
    }

    /// <summary>
    /// Network message
    /// </summary>
    public class NetworkMessage
    {
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string SessionId { get; set; }
        public Dictionary<string, object> Data { get; set; }
        public long Timestamp { get; set; }

        public NetworkMessage()
        {
            Data = new Dictionary<string, object>();
            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }
    }

    /// <summary>
    /// Kenshi Online Server
    /// Complete multiplayer server with entity synchronization, combat, inventory, and world state
    /// </summary>
    public class KenshiOnlineServer
    {
        private TcpListener _listener;
        private readonly int _port;
        private CancellationTokenSource _cts;
        private bool _isRunning;

        // Core systems
        private readonly EntityManager _entityManager;
        private readonly SessionManager _sessionManager;
        private readonly WorldStateManager _worldStateManager;
        private readonly CombatSync _combatSync;
        private readonly InventorySync _inventorySync;
        private readonly AdminCommands _adminCommands;

        // Client connections
        private readonly Dictionary<string, TcpClient> _clients; // SessionId -> Client
        private readonly object _clientsLock = new object();

        // Update rate
        private const float TargetUpdateRate = 20f; // 20 Hz
        private const float UpdateInterval = 1.0f / TargetUpdateRate;

        public KenshiOnlineServer(int port = 7777)
        {
            _port = port;
            _clients = new Dictionary<string, TcpClient>();

            // Initialize systems
            _entityManager = new EntityManager();
            _sessionManager = new SessionManager();
            _worldStateManager = new WorldStateManager();
            _combatSync = new CombatSync(_entityManager);
            _inventorySync = new InventorySync(_entityManager);
            _adminCommands = new AdminCommands(
                _entityManager,
                _sessionManager,
                _worldStateManager,
                _combatSync,
                _inventorySync
            );

            // Configure server info
            _sessionManager.SetServerName("Kenshi Online Server");
            _sessionManager.SetServerDescription("Complete multiplayer experience for Kenshi");
            _sessionManager.SetMaxPlayers(32);

            // Setup event handlers
            SetupEventHandlers();
        }

        private void SetupEventHandlers()
        {
            _sessionManager.OnPlayerJoined += OnPlayerJoined;
            _sessionManager.OnPlayerLeft += OnPlayerLeft;
            _sessionManager.OnPlayerAuthenticated += OnPlayerAuthenticated;
        }

        #region Server Lifecycle

        public void Start()
        {
            _cts = new CancellationTokenSource();
            _isRunning = true;

            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();

            PrintBanner();

            // Start tasks
            Task.Run(() => AcceptClientsAsync(_cts.Token));
            Task.Run(() => UpdateLoop(_cts.Token));
            Task.Run(() => HandleConsoleInput());
        }

        public void Stop()
        {
            Console.WriteLine("\nShutting down server...");

            _isRunning = false;
            _cts?.Cancel();

            lock (_clientsLock)
            {
                foreach (var client in _clients.Values)
                {
                    client?.Close();
                }
                _clients.Clear();
            }

            _listener?.Stop();

            Console.WriteLine("Server stopped.");
        }

        private void PrintBanner()
        {
            Console.Clear();
            Console.WriteLine("╔════════════════════════════════════════════════════════╗");
            Console.WriteLine("║          Kenshi Online Server v2.0                     ║");
            Console.WriteLine("║          Complete Multiplayer System                   ║");
            Console.WriteLine("╚════════════════════════════════════════════════════════╝");
            Console.WriteLine();
            Console.WriteLine($"Server listening on port {_port}");
            Console.WriteLine($"Max players: {_sessionManager.GetServerInfo().MaxPlayers}");
            Console.WriteLine();
            Console.WriteLine("Systems initialized:");
            Console.WriteLine("  ✓ Entity Manager");
            Console.WriteLine("  ✓ Session Manager");
            Console.WriteLine("  ✓ World State Manager");
            Console.WriteLine("  ✓ Combat Synchronization");
            Console.WriteLine("  ✓ Inventory Synchronization");
            Console.WriteLine("  ✓ Admin Commands");
            Console.WriteLine();
            Console.WriteLine("Type 'help' for commands");
            Console.WriteLine("Waiting for players...\n");
        }

        #endregion

        #region Client Connection

        private async Task AcceptClientsAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _isRunning)
            {
                try
                {
                    var client = await _listener.AcceptTcpClientAsync();
                    var ipAddress = ((IPEndPoint)client.Client.RemoteEndPoint).Address.ToString();

                    // Create session
                    var session = _sessionManager.CreateSession(ipAddress);
                    if (session != null)
                    {
                        lock (_clientsLock)
                        {
                            _clients[session.SessionId] = client;
                        }

                        _ = Task.Run(() => HandleClientAsync(client, session, ct), ct);
                    }
                    else
                    {
                        client.Close();
                    }
                }
                catch (Exception ex)
                {
                    if (_isRunning)
                    {
                        Console.WriteLine($"[ERROR] Accept client failed: {ex.Message}");
                    }
                }
            }
        }

        private async Task HandleClientAsync(TcpClient client, PlayerSession session, CancellationToken ct)
        {
            var stream = client.GetStream();
            var buffer = new byte[8192];

            try
            {
                while (!ct.IsCancellationRequested && client.Connected)
                {
                    var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0)
                        break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    var lines = json.Split('\n', StringSplitOptions.RemoveEmptyEntries);

                    foreach (var line in lines)
                    {
                        try
                        {
                            var message = JsonSerializer.Deserialize<NetworkMessage>(line);
                            if (message != null)
                            {
                                await HandleMessage(message, session);
                            }
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"[ERROR] Parse message failed: {ex.Message}");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Client handler error: {ex.Message}");
            }
            finally
            {
                // Clean up
                lock (_clientsLock)
                {
                    _clients.Remove(session.SessionId);
                }

                _sessionManager.RemoveSession(session.SessionId);
                client.Close();
            }
        }

        #endregion

        #region Message Handling

        private async Task HandleMessage(NetworkMessage message, PlayerSession session)
        {
            switch (message.Type.ToLower())
            {
                case "connect":
                    await HandleConnect(message, session);
                    break;

                case "heartbeat":
                    HandleHeartbeat(message, session);
                    break;

                case "entity_update":
                    HandleEntityUpdate(message, session);
                    break;

                case "combat_event":
                    HandleCombatEvent(message, session);
                    break;

                case "inventory_action":
                    HandleInventoryAction(message, session);
                    break;

                case "admin_command":
                    HandleAdminCommand(message, session);
                    break;

                default:
                    Console.WriteLine($"[WARN] Unknown message type: {message.Type}");
                    break;
            }
        }

        private async Task HandleConnect(NetworkMessage message, PlayerSession session)
        {
            if (message.Data.TryGetValue("playerId", out var playerIdObj) &&
                message.Data.TryGetValue("playerName", out var playerNameObj))
            {
                var playerId = playerIdObj.ToString();
                var playerName = playerNameObj.ToString();

                // Authenticate session
                _sessionManager.AuthenticateSession(session.SessionId, playerId, playerName);

                // Create player entity
                var player = _entityManager.CreatePlayer(playerId, playerName, new Vector3(0, 0, 0));

                // Send response
                var response = new NetworkMessage
                {
                    Type = "response",
                    SessionId = session.SessionId,
                    Data = new Dictionary<string, object>
                    {
                        ["success"] = true,
                        ["sessionId"] = session.SessionId,
                        ["playerId"] = playerId,
                        ["entityId"] = player.Id.ToString()
                    }
                };

                await SendToClient(session.SessionId, response);

                // Send world snapshot
                await SendWorldSnapshot(session.SessionId);
            }
        }

        private void HandleHeartbeat(NetworkMessage message, PlayerSession session)
        {
            var ping = message.Data.TryGetValue("ping", out var pingObj) ? Convert.ToInt32(pingObj) : 0;
            _sessionManager.UpdateHeartbeat(session.SessionId, ping);
        }

        private void HandleEntityUpdate(NetworkMessage message, PlayerSession session)
        {
            if (!session.IsAuthenticated)
                return;

            var player = _entityManager.GetPlayerByPlayerId(session.PlayerId);
            if (player == null)
                return;

            // Update player entity from client data
            if (message.Data.TryGetValue("position", out var posObj) && posObj is Dictionary<string, object> posDict)
            {
                player.Position = Vector3.Deserialize(posDict);
            }

            if (message.Data.TryGetValue("velocity", out var velObj) && velObj is Dictionary<string, object> velDict)
            {
                player.Velocity = Vector3.Deserialize(velDict);
            }

            if (message.Data.TryGetValue("rotation", out var rotObj) && rotObj is Dictionary<string, object> rotDict)
            {
                player.Rotation = Quaternion.Deserialize(rotDict);
            }

            if (message.Data.TryGetValue("health", out var healthObj))
            {
                player.Health = Convert.ToSingle(healthObj);
            }

            if (message.Data.TryGetValue("isInCombat", out var combatObj))
            {
                player.IsInCombat = Convert.ToBoolean(combatObj);
            }

            player.MarkDirty();
        }

        private void HandleCombatEvent(NetworkMessage message, PlayerSession session)
        {
            if (!session.IsAuthenticated)
                return;

            // Process combat event
            if (message.Data.TryGetValue("defenderId", out var defenderIdObj) &&
                message.Data.TryGetValue("damage", out var damageObj))
            {
                var player = _entityManager.GetPlayerByPlayerId(session.PlayerId);
                if (player != null)
                {
                    var defenderId = Guid.Parse(defenderIdObj.ToString());
                    var damage = Convert.ToSingle(damageObj);
                    var animation = message.Data.TryGetValue("animation", out var animObj) ? animObj.ToString() : "";

                    _combatSync.ProcessAttack(player.Id, defenderId, damage, animation);
                }
            }
        }

        private void HandleInventoryAction(NetworkMessage message, PlayerSession session)
        {
            if (!session.IsAuthenticated)
                return;

            var player = _entityManager.GetPlayerByPlayerId(session.PlayerId);
            if (player == null)
                return;

            if (message.Data.TryGetValue("action", out var actionObj))
            {
                var action = actionObj.ToString();

                switch (action.ToLower())
                {
                    case "pickup":
                        if (message.Data.TryGetValue("itemId", out var pickupItemIdObj))
                        {
                            _inventorySync.ProcessPickup(player.Id, Guid.Parse(pickupItemIdObj.ToString()));
                        }
                        break;

                    case "drop":
                        if (message.Data.TryGetValue("itemId", out var dropItemIdObj))
                        {
                            _inventorySync.ProcessDrop(player.Id, Guid.Parse(dropItemIdObj.ToString()));
                        }
                        break;

                    case "equip":
                        if (message.Data.TryGetValue("itemId", out var equipItemIdObj) &&
                            message.Data.TryGetValue("slot", out var slotObj))
                        {
                            _inventorySync.ProcessEquip(player.Id, Guid.Parse(equipItemIdObj.ToString()), slotObj.ToString());
                        }
                        break;

                    case "unequip":
                        if (message.Data.TryGetValue("slot", out var unequipSlotObj))
                        {
                            _inventorySync.ProcessUnequip(player.Id, unequipSlotObj.ToString());
                        }
                        break;
                }
            }
        }

        private async Task HandleAdminCommand(NetworkMessage message, PlayerSession session)
        {
            if (!session.IsAuthenticated)
                return;

            if (message.Data.TryGetValue("command", out var commandObj))
            {
                var command = commandObj.ToString();
                var result = _adminCommands.ExecuteCommand(command, session.PlayerId);

                var response = new NetworkMessage
                {
                    Type = "admin_response",
                    SessionId = session.SessionId,
                    Data = new Dictionary<string, object>
                    {
                        ["success"] = result.Success,
                        ["message"] = result.Message,
                        ["data"] = result.Data
                    }
                };

                await SendToClient(session.SessionId, response);
            }
        }

        #endregion

        #region Update Loop

        private async Task UpdateLoop(CancellationToken ct)
        {
            var lastUpdate = DateTime.UtcNow;

            while (!ct.IsCancellationRequested && _isRunning)
            {
                var now = DateTime.UtcNow;
                var deltaTime = (float)(now - lastUpdate).TotalSeconds;
                lastUpdate = now;

                try
                {
                    // Update all systems
                    _entityManager.Update(deltaTime);
                    _worldStateManager.Update(deltaTime);
                    _combatSync.Update(deltaTime);
                    _inventorySync.Update(deltaTime);
                    _sessionManager.Update();

                    // Broadcast updates
                    await BroadcastUpdates();

                    // Sleep to maintain update rate
                    await Task.Delay((int)(UpdateInterval * 1000), ct);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[ERROR] Update loop error: {ex.Message}");
                }
            }
        }

        private async Task BroadcastUpdates()
        {
            // Get dirty entities
            var dirtyEntities = _entityManager.GetDirtyEntities();

            // Broadcast entity updates
            foreach (var entity in dirtyEntities)
            {
                var message = new NetworkMessage
                {
                    Type = "entity_update",
                    Data = entity.Serialize()
                };

                await BroadcastToAll(message);
            }

            // Broadcast combat events
            var combatEvents = _combatSync.GetPendingEvents();
            foreach (var evt in combatEvents)
            {
                var message = new NetworkMessage
                {
                    Type = "combat_event",
                    Data = evt.Serialize()
                };

                await BroadcastToAll(message);
            }

            // Broadcast inventory actions
            var inventoryActions = _inventorySync.GetPendingActions();
            foreach (var action in inventoryActions)
            {
                var message = new NetworkMessage
                {
                    Type = "inventory_action",
                    Data = action.Serialize()
                };

                await BroadcastToAll(message);
            }

            // Broadcast world state (if changed)
            if (_worldStateManager.IsDirty)
            {
                var message = new NetworkMessage
                {
                    Type = "world_state",
                    Data = _worldStateManager.GetSnapshot()
                };

                await BroadcastToAll(message);
            }
        }

        #endregion

        #region Network Sending

        private async Task SendToClient(string sessionId, NetworkMessage message)
        {
            TcpClient client;
            lock (_clientsLock)
            {
                if (!_clients.TryGetValue(sessionId, out client))
                    return;
            }

            try
            {
                var json = JsonSerializer.Serialize(message) + "\n";
                var bytes = Encoding.UTF8.GetBytes(json);
                await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Send to client failed: {ex.Message}");
            }
        }

        private async Task BroadcastToAll(NetworkMessage message)
        {
            List<string> sessionIds;
            lock (_clientsLock)
            {
                sessionIds = new List<string>(_clients.Keys);
            }

            foreach (var sessionId in sessionIds)
            {
                await SendToClient(sessionId, message);
            }
        }

        private async Task SendWorldSnapshot(string sessionId)
        {
            var entities = _entityManager.GetAllEntities();
            foreach (var entity in entities)
            {
                var message = new NetworkMessage
                {
                    Type = "entity_create",
                    Data = entity.Serialize()
                };

                await SendToClient(sessionId, message);
            }

            var worldState = new NetworkMessage
            {
                Type = "world_state",
                Data = _worldStateManager.GetSnapshot()
            };

            await SendToClient(sessionId, worldState);
        }

        #endregion

        #region Event Handlers

        private void OnPlayerJoined(PlayerSession session)
        {
            Console.WriteLine($"[JOIN] {session.IPAddress} connected (Session: {session.SessionId})");
        }

        private void OnPlayerLeft(PlayerSession session)
        {
            Console.WriteLine($"[LEAVE] {session.PlayerName ?? session.IPAddress} disconnected");

            // Remove player entity
            if (!string.IsNullOrEmpty(session.PlayerId))
            {
                var player = _entityManager.GetPlayerByPlayerId(session.PlayerId);
                if (player != null)
                {
                    _entityManager.UnregisterEntity(player.Id);
                }
            }
        }

        private void OnPlayerAuthenticated(PlayerSession session)
        {
            Console.WriteLine($"[AUTH] {session.PlayerName} authenticated (ID: {session.PlayerId})");
        }

        #endregion

        #region Console Commands

        private void HandleConsoleInput()
        {
            while (_isRunning)
            {
                try
                {
                    var input = Console.ReadLine();
                    if (string.IsNullOrEmpty(input))
                        continue;

                    ProcessConsoleCommand(input.Trim());
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[ERROR] Console command error: {ex.Message}");
                }
            }
        }

        private void ProcessConsoleCommand(string command)
        {
            var parts = command.Split(' ', 2);
            var cmd = parts[0].ToLower();

            switch (cmd)
            {
                case "help":
                    ShowHelp();
                    break;

                case "status":
                    ShowStatus();
                    break;

                case "players":
                    ShowPlayers();
                    break;

                case "stop":
                case "quit":
                case "exit":
                    Stop();
                    Environment.Exit(0);
                    break;

                default:
                    // Try as admin command
                    var result = _adminCommands.ExecuteCommand(command, "console");
                    Console.WriteLine($"{(result.Success ? "[OK]" : "[ERROR]")} {result.Message}");
                    if (result.Data.Count > 0)
                    {
                        Console.WriteLine(JsonSerializer.Serialize(result.Data, new JsonSerializerOptions { WriteIndented = true }));
                    }
                    break;
            }
        }

        private void ShowHelp()
        {
            Console.WriteLine("\nServer Commands:");
            Console.WriteLine("  help - Show this help");
            Console.WriteLine("  status - Show server status");
            Console.WriteLine("  players - List connected players");
            Console.WriteLine("  stop - Stop server");
            Console.WriteLine("\nAdmin commands are also available (type 'help' as admin command for list)\n");
        }

        private void ShowStatus()
        {
            var stats = _sessionManager.GetStatistics();
            Console.WriteLine("\n=== Server Status ===");
            Console.WriteLine($"Players: {_sessionManager.AuthenticatedSessions}/{_sessionManager.GetServerInfo().MaxPlayers}");
            Console.WriteLine($"Entities: {_entityManager.TotalEntities} (P:{_entityManager.PlayerCount}, N:{_entityManager.NPCCount}, I:{_entityManager.ItemCount})");
            Console.WriteLine($"World Time: {_worldStateManager.GetTimeOfDayString()} ({_worldStateManager.GetDateString()})");
            Console.WriteLine($"Game Speed: {_worldStateManager.State.GameSpeedMultiplier}x");
            Console.WriteLine($"Combat Events: {_combatSync.TotalCombatEvents}");
            Console.WriteLine($"Inventory Actions: {_inventorySync.TotalInventoryActions}");
            Console.WriteLine();
        }

        private void ShowPlayers()
        {
            var players = _sessionManager.GetPlayerList();
            Console.WriteLine($"\n=== Players Online ({players.Count}) ===");
            foreach (var player in players)
            {
                Console.WriteLine($"  {player["playerName"]} (ID: {player["playerId"]}) - Ping: {player["ping"]}ms");
            }
            Console.WriteLine();
        }

        #endregion
    }
}

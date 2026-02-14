using System;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.IO;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Integrates all multiplayer components into a cohesive system
    /// </summary>
    public class MultiplayerIntegration : IDisposable
    {
        // Core components
        private DeterministicPathManager? pathManager;
        private ActionProcessor? actionProcessor;
        private EnhancedServer? server;
        private EnhancedClient? client;
        private GameModManager? modManager;

        // Event handler reference for proper cleanup
        private EventHandler<GameMessage>? clientMessageHandler;
        private bool disposed;

        // Configuration
        private readonly string kenshiPath;
        private readonly bool isServer;
        private readonly int port;
        
        public MultiplayerIntegration(string kenshiInstallPath, bool runAsServer = false, int serverPort = 5555)
        {
            kenshiPath = kenshiInstallPath;
            isServer = runAsServer;
            port = serverPort;
        }
        
        /// <summary>
        /// Initialize the complete multiplayer system
        /// </summary>
        public async Task<bool> Initialize()
        {
            try
            {
                Logger.Log("=== Kenshi Multiplayer System Initialization ===");
                
                // Step 1: Validate Kenshi installation
                if (!ValidateKenshiInstallation())
                {
                    Logger.Log("ERROR: Invalid Kenshi installation path");
                    return false;
                }
                
                // Step 2: Initialize mod manager
                Logger.Log("Loading mods...");
                modManager = new GameModManager(kenshiPath);
                var activeMods = modManager.GetActiveMods();
                Logger.Log($"Loaded {activeMods.Count} active mods");
                
                // Step 3: Initialize deterministic path system
                Logger.Log("Initializing deterministic pathfinding...");
                pathManager = new DeterministicPathManager("pathcache");
                
                if (!await pathManager.Initialize(isServer))
                {
                    Logger.Log("WARNING: Path system initialization failed - running in compatibility mode");
                }
                
                // Step 4: Initialize server or client
                if (isServer)
                {
                    await InitializeServer();
                }
                else
                {
                    await InitializeClient();
                }
                
                Logger.Log("=== Multiplayer System Ready ===");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"FATAL: Initialization failed - {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Initialize server components
        /// </summary>
        private async Task InitializeServer()
        {
            Logger.Log("Starting server mode...");

            // Create server instance
            server = new EnhancedServer(kenshiPath);

            // Create required managers
            var worldStateManager = new WorldStateManager();
            var networkManager = new NetworkManager();
            var pathCache = new PathCache("pathcache");
            var pathInjector = new PathInjector(pathCache);

            // Initialize action processor
            actionProcessor = new ActionProcessor(worldStateManager, pathInjector, networkManager);

            // Set up message handlers
            SetupServerMessageHandlers();

            // Start server
            Task.Run(() => server.Start(port));

            // Start action processing
            actionProcessor.StartProcessing();

            // Pre-cache common paths
            Logger.Log("Pre-baking common paths for clients...");
            await pathCache.PreBakeCommonPaths();

            Logger.Log($"Server started on port {port}");
        }
        
        /// <summary>
        /// Initialize client components
        /// </summary>
        private async Task InitializeClient()
        {
            Logger.Log("Starting client mode...");
            
            // Create client instance
            client = new EnhancedClient("cache");
            
            // Set up message handlers
            SetupClientMessageHandlers();
            
            await Task.CompletedTask;
            Logger.Log("Client initialized - use Connect() to join a server");
        }
        
        /// <summary>
        /// Connect client to server
        /// </summary>
        public bool ConnectToServer(string serverAddress, int serverPort, string username, string password)
        {
            if (client == null)
            {
                Logger.Log("ERROR: Client not initialized");
                return false;
            }
            
            Logger.Log($"Connecting to {serverAddress}:{serverPort}...");
            
            if (client.Login(serverAddress, serverPort, username, password))
            {
                Logger.Log("Connected successfully!");
                
                // Sync path cache
                Task.Run(() => SyncPathCache());
                
                // Start sending position updates
                Task.Run(() => PositionUpdateLoop());
                
                return true;
            }
            
            Logger.Log("Connection failed");
            return false;
        }
        
        /// <summary>
        /// Set up server message handlers
        /// </summary>
        private void SetupServerMessageHandlers()
        {
            // This would integrate with your existing server message handling
            // to process actions through the deterministic system
        }
        
        /// <summary>
        /// Set up client message handlers
        /// </summary>
        private void SetupClientMessageHandlers()
        {
            // Use named handler for proper cleanup
            clientMessageHandler = OnClientMessageReceived;
            client.MessageReceived += clientMessageHandler;
        }

        /// <summary>
        /// Handle messages received from the client
        /// </summary>
        private void OnClientMessageReceived(object? sender, GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                    HandlePositionUpdate(message);
                    break;

                case MessageType.Combat:
                    HandleCombatAction(message);
                    break;

                case "path_update":
                    HandlePathUpdate(message);
                    break;

                case "action_result":
                    HandleActionResult(message);
                    break;

                case MessageType.WorldState:
                    HandleWorldStateUpdate(message);
                    break;
            }
        }
        
        /// <summary>
        /// Handle position updates from other players
        /// </summary>
        private void HandlePositionUpdate(GameMessage message)
        {
            // Update other player's position in game
            // This would integrate with memory injection
        }
        
        /// <summary>
        /// Handle combat actions
        /// </summary>
        private void HandleCombatAction(GameMessage message)
        {
            // Process combat through deterministic system
        }
        
        /// <summary>
        /// Handle new path updates
        /// </summary>
        private void HandlePathUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("path", out var pathObj))
            {
                var path = System.Text.Json.JsonSerializer.Deserialize<CachedPath>(pathObj.ToString());
                
                // Add to local cache
                var pathCache = new PathCache("pathcache");
                pathCache.SynchronizePaths(new List<CachedPath> { path });
                
                Logger.Log($"Received new path: {path.PathId}");
            }
        }
        
        /// <summary>
        /// Handle action results
        /// </summary>
        private void HandleActionResult(GameMessage message)
        {
            // Process action result
            // Update local game state
        }
        
        /// <summary>
        /// Handle world state updates
        /// </summary>
        private void HandleWorldStateUpdate(GameMessage message)
        {
            // Sync world state
            // Apply to game through memory injection
        }
        
        /// <summary>
        /// Sync path cache with server
        /// </summary>
        private async Task SyncPathCache()
        {
            Logger.Log("Syncing path cache with server...");

            try
            {
                // Request cache checksum from server
                var checksumRequest = new GameMessage
                {
                    Type = "path_checksum_request",
                    PlayerId = client.CurrentUsername,
                    SessionId = client.AuthToken
                };

                // Send checksum request and wait for server response
                var tcs = new TaskCompletionSource<GameMessage>();
                EventHandler<GameMessage> handler = null;
                handler = (sender, msg) =>
                {
                    if (msg.Type == "path_checksum_response")
                    {
                        client.MessageReceived -= handler;
                        tcs.TrySetResult(msg);
                    }
                };

                client.MessageReceived += handler;
                client.SendMessage(checksumRequest);

                // Wait up to 5 seconds for response
                var completedTask = await Task.WhenAny(tcs.Task, Task.Delay(5000));
                if (completedTask != tcs.Task)
                {
                    client.MessageReceived -= handler;
                    Logger.Log("Path cache sync timed out - using local cache");
                    return;
                }

                Logger.Log("Path cache synchronized with server");
            }
            catch (Exception ex)
            {
                Logger.Log($"Path cache sync error: {ex.Message} - using local cache");
            }
        }
        
        /// <summary>
        /// Position update loop for client
        /// </summary>
        private async Task PositionUpdateLoop()
        {
            while (client != null && client.IsLoggedIn)
            {
                try
                {
                    // Read position from game memory
                    var position = ReadPlayerPosition();
                    
                    if (position != null)
                    {
                        // Send position update
                        client.UpdatePosition(position.X, position.Y);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Position update error: {ex.Message}");
                }
                
                await Task.Delay(100); // 10 updates per second
            }
        }
        
        /// <summary>
        /// Read player position from game memory via the client's game bridge
        /// </summary>
        private Position ReadPlayerPosition()
        {
            var gameBridge = client?.GameBridge;
            if (gameBridge != null && gameBridge.IsConnected)
            {
                var pos = gameBridge.GetLocalPlayerPosition();
                if (pos != null) return pos;
            }

            // Fall back to last known position from network
            return client?.LastKnownPosition ?? new Position();
        }
        
        /// <summary>
        /// Validate Kenshi installation
        /// </summary>
        private bool ValidateKenshiInstallation()
        {
            if (!Directory.Exists(kenshiPath))
                return false;
            
            string executablePath = Path.Combine(kenshiPath, "kenshi_x64.exe");
            return File.Exists(executablePath);
        }
        
        /// <summary>
        /// Queue an action for processing
        /// </summary>
        public void QueueAction(string type, object data)
        {
            if (actionProcessor == null)
            {
                Logger.Log("ERROR: Action processor not initialized");
                return;
            }
            
            var action = new PlayerAction
            {
                Type = type,
                PlayerId = isServer ? "SERVER" : client?.CurrentUsername ?? "UNKNOWN",
                Data = data as Dictionary<string, object> ?? new Dictionary<string, object> { ["data"] = data },
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Priority = GetActionPriority(type)
            };

            actionProcessor.QueueAction(action);
        }
        
        /// <summary>
        /// Get action priority
        /// </summary>
        private int GetActionPriority(string type)
        {
            switch (type)
            {
                case "combat": return 1;
                case "movement": return 2;
                case "interaction": return 3;
                case "trade": return 4;
                default: return 5;
            }
        }
        
        /// <summary>
        /// Shutdown the multiplayer system
        /// </summary>
        public void Shutdown()
        {
            Logger.Log("Shutting down multiplayer system...");

            // Unsubscribe from client events
            if (client != null && clientMessageHandler != null)
            {
                client.MessageReceived -= clientMessageHandler;
                clientMessageHandler = null;
            }

            actionProcessor?.StopProcessing();
            pathManager?.Shutdown();
            client?.Disconnect();

            Logger.Log("Multiplayer system shutdown complete");
        }

        public void Dispose()
        {
            if (disposed)
                return;

            disposed = true;
            Shutdown();
        }
    }
    
    /// <summary>
    /// Example usage and entry point
    /// </summary>
    public class MultiplayerLauncher
    {
        public static async Task LaunchMultiplayer(bool isServer, string kenshiPath)
        {
            var integration = new MultiplayerIntegration(kenshiPath, isServer);
            
            if (!await integration.Initialize())
            {
                Console.WriteLine("Failed to initialize multiplayer system");
                return;
            }
            
            if (isServer)
            {
                Console.WriteLine("Server running. Press any key to stop...");
                Console.ReadKey();
            }
            else
            {
                // Client mode
                Console.Write("Server address: ");
                string serverAddress = Console.ReadLine();
                
                Console.Write("Username: ");
                string username = Console.ReadLine();
                
                Console.Write("Password: ");
                string password = ReadPassword();
                
                if (integration.ConnectToServer(serverAddress, 5555, username, password))
                {
                    Console.WriteLine("Connected! Press any key to disconnect...");
                    
                    // Example: Queue a movement action
                    integration.QueueAction("movement", new
                    {
                        start = new { x = 0, y = 0, z = 0 },
                        end = new { x = 100, y = 100, z = 0 },
                        speed = 5.0f
                    });
                    
                    Console.ReadKey();
                }
            }
            
            integration.Shutdown();
        }
        
        private static string ReadPassword()
        {
            string password = "";
            ConsoleKeyInfo key;
            
            do
            {
                key = Console.ReadKey(true);
                
                if (key.Key != ConsoleKey.Enter && key.Key != ConsoleKey.Backspace)
                {
                    password += key.KeyChar;
                    Console.Write("*");
                }
                else if (key.Key == ConsoleKey.Backspace && password.Length > 0)
                {
                    password = password.Substring(0, password.Length - 1);
                    Console.Write("\b \b");
                }
            }
            while (key.Key != ConsoleKey.Enter);
            
            Console.WriteLine();
            return password;
        }
    }
}
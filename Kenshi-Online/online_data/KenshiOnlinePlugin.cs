using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using MemorySharp;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Main entry point for the Kenshi Online integration
    /// </summary>
    public class KenshiOnlinePlugin
    {
        // Components
        private KenshiMultiplayerController controller;
        private MemorySharp memory;
        private KenshiMemoryScanner memoryScanner;
        private RemotePlayerManager playerManager;
        
        // Configuration
        private string serverAddress = "localhost";
        private int serverPort = 5555;
        private string cachePath = "cache";
        private string configPath = "kenshi_online.cfg";
        
        // State
        private bool isRunning = false;
        private CancellationTokenSource cancellationToken;
        private Task mainLoopTask;
        
        /// <summary>
        /// Initialize the plugin
        /// </summary>
        public bool Initialize()
        {
            try
            {
                Console.WriteLine("Initializing Kenshi Online Plugin...");
                
                // Load configuration
                LoadConfig();
                
                // Create multiplayer controller
                controller = new KenshiMultiplayerController(cachePath);
                if (!controller.Initialize(serverAddress, serverPort))
                {
                    Console.WriteLine("Failed to initialize controller");
                    return false;
                }
                
                // Subscribe to message events
                controller.MessageReceived += OnMessageReceived;
                
                Console.WriteLine("Kenshi Online Plugin initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error initializing plugin: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Connect to the server
        /// </summary>
        public async Task<bool> Connect(string username, string password)
        {
            try
            {
                // Attempt to connect to Kenshi process
                if (!ConnectToKenshiProcess())
                {
                    Console.WriteLine("Failed to connect to Kenshi process");
                    return false;
                }
                
                // Login to server
                bool success = await controller.Login(username, password);
                
                if (success)
                {
                    // Start the main loop
                    StartMainLoop();
                    
                    Console.WriteLine($"Connected to server as {username}");
                }
                else
                {
                    Console.WriteLine("Failed to connect to server");
                }
                
                return success;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error connecting: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Register a new account
        /// </summary>
        public async Task<bool> Register(string username, string password, string email)
        {
            try
            {
                bool success = await controller.Register(username, password, email);
                
                if (success)
                {
                    Console.WriteLine($"Registered new account: {username}");
                }
                else
                {
                    Console.WriteLine("Failed to register account");
                }
                
                return success;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error registering: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Disconnect from the server
        /// </summary>
        public void Disconnect()
        {
            try
            {
                isRunning = false;
                cancellationToken?.Cancel();
                
                try
                {
                    mainLoopTask?.Wait(1000);
                }
                catch { }
                
                controller?.Disconnect();
                memory?.Dispose();
                
                Console.WriteLine("Disconnected from server");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error disconnecting: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Send a chat message
        /// </summary>
        public void SendChatMessage(string message)
        {
            controller?.SendChatMessage(message);
        }
        
        /// <summary>
        /// Handler for incoming messages
        /// </summary>
        private void OnMessageReceived(object sender, GameMessage message)
        {
            // Process message based on type
            switch (message.Type)
            {
                case MessageType.Position:
                    HandlePositionMessage(message);
                    break;
                
                case MessageType.Health:
                    HandleHealthMessage(message);
                    break;
                
                case MessageType.Combat:
                    HandleCombatMessage(message);
                    break;
                
                case MessageType.Chat:
                    HandleChatMessage(message);
                    break;
                
                case MessageType.SystemMessage:
                    HandleSystemMessage(message);
                    break;
            }
        }
        
        /// <summary>
        /// Handle position update messages
        /// </summary>
        private void HandlePositionMessage(GameMessage message)
        {
            if (playerManager == null || message.PlayerId == null)
                return;
            
            try
            {
                // Extract position data
                if (message.Data.TryGetValue("X", out object xObj) && 
                    message.Data.TryGetValue("Y", out object yObj) &&
                    message.Data.TryGetValue("Z", out object zObj))
                {
                    float x = Convert.ToSingle(xObj);
                    float y = Convert.ToSingle(yObj);
                    float z = Convert.ToSingle(zObj);
                    
                    // Create position object
                    var position = new Position(x, y, z);
                    
                    // Get or create player
                    var player = playerManager.GetOrCreatePlayer(message.PlayerId, message.PlayerId);
                    
                    if (player != null)
                    {
                        // Update player position
                        playerManager.UpdatePlayerPosition(message.PlayerId, position);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling position message: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Handle health update messages
        /// </summary>
        private void HandleHealthMessage(GameMessage message)
        {
            if (playerManager == null || message.PlayerId == null)
                return;
            
            try
            {
                // Extract health data
                if (message.Data.TryGetValue("CurrentHealth", out object currentHealthObj) && 
                    message.Data.TryGetValue("MaxHealth", out object maxHealthObj))
                {
                    int currentHealth = Convert.ToInt32(currentHealthObj);
                    int maxHealth = Convert.ToInt32(maxHealthObj);
                    
                    // Get or create player
                    var player = playerManager.GetOrCreatePlayer(message.PlayerId, message.PlayerId);
                    
                    if (player != null)
                    {
                        // Update player health
                        playerManager.UpdatePlayerHealth(message.PlayerId, currentHealth, maxHealth);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling health message: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Handle combat action messages
        /// </summary>
        private void HandleCombatMessage(GameMessage message)
        {
            // This would require a more complex implementation that interacts
            // with Kenshi's combat system through memory manipulation
            
            // For now we'll just log the message
            try
            {
                if (message.Data.TryGetValue("Action", out object actionObj) &&
                    message.Data.TryGetValue("TargetId", out object targetObj))
                {
                    string action = actionObj.ToString();
                    string targetId = targetObj.ToString();
                    
                    Console.WriteLine($"Combat action from {message.PlayerId}: {action} on {targetId}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling combat message: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Handle chat messages
        /// </summary>
        private void HandleChatMessage(GameMessage message)
        {
            try
            {
                if (message.Data.TryGetValue("message", out object messageObj))
                {
                    string chatMessage = messageObj.ToString();
                    Console.WriteLine($"[CHAT] {message.PlayerId}: {chatMessage}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling chat message: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Handle system messages
        /// </summary>
        private void HandleSystemMessage(GameMessage message)
        {
            try
            {
                if (message.Data.TryGetValue("message", out object messageObj))
                {
                    string systemMessage = messageObj.ToString();
                    Console.WriteLine($"[SYSTEM] {systemMessage}");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling system message: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Connect to the Kenshi process
        /// </summary>
        private bool ConnectToKenshiProcess()
        {
            try
            {
                // Find Kenshi process
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }
                
                if (processes.Length == 0)
                {
                    Console.WriteLine("Kenshi process not found. Is the game running?");
                    return false;
                }
                
                // Connect to the process
                memory = new MemorySharp(processes[0]);
                
                // Create memory scanner
                memoryScanner = new KenshiMemoryScanner(memory);
                
                // Create player manager
                playerManager = new RemotePlayerManager(memory);
                playerManager.Initialize();
                
                Console.WriteLine("Connected to Kenshi process successfully");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error connecting to Kenshi process: {ex.Message}");
                return false;
            }
        }
        
        /// <summary>
        /// Start the main plugin loop
        /// </summary>
        private void StartMainLoop()
        {
            if (isRunning)
                return;
            
            isRunning = true;
            cancellationToken = new CancellationTokenSource();
            
            mainLoopTask = Task.Run(() => MainLoop(cancellationToken.Token), cancellationToken.Token);
        }
        
        /// <summary>
        /// Main plugin loop
        /// </summary>
        private async Task MainLoop(CancellationToken token)
        {
            while (!token.IsCancellationRequested && isRunning)
            {
                try
                {
                    // Check if Kenshi is still running
                    if (memory?.Process.HasExited ?? true)
                    {
                        Console.WriteLine("Kenshi process has exited");
                        isRunning = false;
                        break;
                    }
                    
                    // Process timeouts for players
                    playerManager.CheckTimeouts();
                    
                    // Wait a short time before next iteration
                    await Task.Delay(1000, token);
                }
                catch (OperationCanceledException)
                {
                    // Normal cancellation
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error in main loop: {ex.Message}");
                    await Task.Delay(5000, token); // Longer delay on error
                }
            }
        }
        
        /// <summary>
        /// Load configuration from file
        /// </summary>
        private void LoadConfig()
        {
            try
            {
                if (File.Exists(configPath))
                {
                    string[] lines = File.ReadAllLines(configPath);
                    
                    foreach (string line in lines)
                    {
                        string trimmed = line.Trim();
                        
                        // Skip comments and empty lines
                        if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith("#"))
                            continue;
                        
                        // Parse key-value pairs
                        int separator = trimmed.IndexOf('=');
                        if (separator > 0)
                        {
                            string key = trimmed.Substring(0, separator).Trim();
                            string value = trimmed.Substring(separator + 1).Trim();
                            
                            // Apply configuration
                            switch (key.ToLower())
                            {
                                case "server":
                                    serverAddress = value;
                                    break;
                                
                                case "port":
                                    if (int.TryParse(value, out int port))
                                        serverPort = port;
                                    break;
                                
                                case "cache":
                                    cachePath = value;
                                    break;
                            }
                        }
                    }
                    
                    Console.WriteLine("Configuration loaded successfully");
                }
                else
                {
                    // Create default configuration
                    SaveConfig();
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error loading configuration: {ex.Message}");
            }
        }
        
        /// <summary>
        /// Save configuration to file
        /// </summary>
        private void SaveConfig()
        {
            try
            {
                string[] lines = new string[]
                {
                    "# Kenshi Online Configuration",
                    "",
                    "# Server address",
                    $"server={serverAddress}",
                    "",
                    "# Server port",
                    $"port={serverPort}",
                    "",
                    "# Cache directory",
                    $"cache={cachePath}"
                };
                
                File.WriteAllLines(configPath, lines);
                Console.WriteLine("Configuration saved successfully");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error saving configuration: {ex.Message}");
            }
        }
    }
}
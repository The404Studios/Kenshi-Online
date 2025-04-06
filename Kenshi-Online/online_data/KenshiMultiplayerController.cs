using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Main controller class that coordinates the multiplayer functionality
    /// </summary>
    public class KenshiMultiplayerController
    {
        private EnhancedClient client;
        private KenshiMemoryIntegration memoryIntegration;
        private string serverAddress;
        private int serverPort;
        private string username;
        private string cachePath;
        private bool isConnected = false;
        private bool isAuthenticated = false;
        
        // Event for receiving game messages
        public event EventHandler<GameMessage> MessageReceived;
        
        public KenshiMultiplayerController(string cacheDir = "cache")
        {
            cachePath = cacheDir;
            Directory.CreateDirectory(cachePath);
        }
        
        public bool Initialize(string server, int port)
        {
            try
            {
                serverAddress = server;
                serverPort = port;
                
                // Initialize the network client
                client = new EnhancedClient(cachePath);
                
                // Subscribe to client message events
                client.MessageReceived += OnClientMessageReceived;
                
                // Initialize memory integration
                memoryIntegration = new KenshiMemoryIntegration(client);
                
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error initializing multiplayer controller: {ex.Message}");
                return false;
            }
        }
        
        private void OnClientMessageReceived(object sender, GameMessage message)
        {
            // Forward the message to subscribers
            MessageReceived?.Invoke(this, message);
            
            // Process message based on type
            ProcessGameMessage(message);
        }
        
        private void ProcessGameMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                    HandlePositionUpdate(message);
                    break;
                
                case MessageType.Combat:
                    HandleCombatAction(message);
                    break;
                
                case MessageType.Inventory:
                    HandleInventoryUpdate(message);
                    break;
                
                case MessageType.Health:
                    HandleHealthUpdate(message);
                    break;
                
                case MessageType.Chat:
                    // Just forward chat messages to subscribers
                    break;
                
                case MessageType.SystemMessage:
                    // Just forward system messages to subscribers
                    break;
            }
        }
        
        private void HandlePositionUpdate(GameMessage message)
        {
            if (message.PlayerId == null || message.PlayerId == username)
                return; // Ignore our own position updates
            
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
                    
                    // TODO: Update position of other player in the game
                    // This would require finding/spawning their character and updating position
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling position update: {ex.Message}");
            }
        }
        
        private void HandleCombatAction(GameMessage message)
        {
            if (message.PlayerId == null || message.PlayerId == username)
                return; // Ignore our own combat actions
            
            try
            {
                // Extract combat data
                if (message.Data.TryGetValue("TargetId", out object targetObj) && 
                    message.Data.TryGetValue("Action", out object actionObj))
                {
                    string targetId = targetObj.ToString();
                    string action = actionObj.ToString();
                    
                    // TODO: Apply combat action in game
                    // This would require finding the target and applying the action
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling combat action: {ex.Message}");
            }
        }
        
        private void HandleInventoryUpdate(GameMessage message)
        {
            if (message.PlayerId == null || message.PlayerId == username)
                return; // Ignore our own inventory updates
            
            try
            {
                // Extract inventory data
                if (message.Data.TryGetValue("ItemName", out object itemNameObj) && 
                    message.Data.TryGetValue("Quantity", out object quantityObj))
                {
                    string itemName = itemNameObj.ToString();
                    int quantity = Convert.ToInt32(quantityObj);
                    
                    // TODO: Update inventory of other player in the game
                    // This would require finding their character and updating inventory
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling inventory update: {ex.Message}");
            }
        }
        
        private void HandleHealthUpdate(GameMessage message)
        {
            if (message.PlayerId == null || message.PlayerId == username)
                return; // Ignore our own health updates
            
            try
            {
                // Extract health data
                if (message.Data.TryGetValue("CurrentHealth", out object currentHealthObj) && 
                    message.Data.TryGetValue("MaxHealth", out object maxHealthObj))
                {
                    int currentHealth = Convert.ToInt32(currentHealthObj);
                    int maxHealth = Convert.ToInt32(maxHealthObj);
                    
                    // TODO: Update health of other player in the game
                    // This would require finding their character and updating health
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error handling health update: {ex.Message}");
            }
        }
        
        public async Task<bool> Login(string user, string password)
        {
            if (isAuthenticated)
                return true;
            
            try
            {
                bool success = client.Login(serverAddress, serverPort, user, password);
                
                if (success)
                {
                    username = user;
                    isAuthenticated = true;
                    
                    // Connect to Kenshi process
                    if (!memoryIntegration.ConnectToKenshi())
                    {
                        Console.WriteLine("Failed to connect to Kenshi process");
                        return false;
                    }
                    
                    isConnected = true;
                }
                
                return success;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Login error: {ex.Message}");
                return false;
            }
        }
        
        public async Task<bool> Register(string user, string password, string email)
        {
            try
            {
                return client.Register(serverAddress, serverPort, user, password, email);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Registration error: {ex.Message}");
                return false;
            }
        }
        
        public void SendChatMessage(string message)
        {
            if (!isConnected || !isAuthenticated)
                return;
            
            try
            {
                // Create a chat message
                var chatMessage = new GameMessage
                {
                    Type = MessageType.Chat,
                    PlayerId = username,
                    Data = new System.Collections.Generic.Dictionary<string, object> 
                    { 
                        { "message", message }
                    }
                };
                
                // Send the message
                client.SendMessageToServer(chatMessage);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error sending chat message: {ex.Message}");
            }
        }
        
        public void Disconnect()
        {
            if (!isConnected)
                return;
            
            try
            {
                // Clean up resources
                memoryIntegration?.Dispose();
                client?.Disconnect();
                
                isConnected = false;
                isAuthenticated = false;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error disconnecting: {ex.Message}");
            }
        }
    }
}
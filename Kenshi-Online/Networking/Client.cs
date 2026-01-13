using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    public class EnhancedClient
    {
        private TcpClient client;
        private NetworkStream stream;
        private string authToken;
        private Dictionary<string, GameFileInfo> cachedFileInfo = new Dictionary<string, GameFileInfo>();
        private string localCachePath;
        private float lastX, lastY;
        private DateTime lastCombatTime = DateTime.MinValue;

        // New fields for features
        private FriendsManager friendsManager;
        private MarketplaceManager marketplaceManager;
        private TradeManager tradeManager;
        private STUNClient stunClient;
        private bool isWebInterfaceEnabled = false;
        private WebUIController webUI;

        // Player identification
        private string playerId;
        private string sessionId;

        // Client configuration
        public string ServerAddress { get; private set; }
        public int ServerPort { get; private set; }
        public string CurrentUsername { get; private set; }
        public string AuthToken => authToken;
        public string PlayerId => playerId;
        public string SessionId => sessionId;
        public bool IsLoggedIn => !string.IsNullOrEmpty(authToken);
        public bool IsWebInterfaceEnabled => isWebInterfaceEnabled;
        public bool IsConnected => client?.Connected ?? false;

        public event EventHandler<GameMessage> MessageReceived;

        public EnhancedClient(string cachePath)
        {
            localCachePath = cachePath;
            Directory.CreateDirectory(localCachePath);

            // Initialize feature managers
            friendsManager = new FriendsManager(this, cachePath);
            marketplaceManager = new MarketplaceManager(this, cachePath);
            tradeManager = new TradeManager(this, cachePath);
            stunClient = new STUNClient();
        }

        public bool Login(string serverAddress, int port, string username, string password)
        {
            try
            {
                // Store for reconnection
                ServerAddress = serverAddress;
                ServerPort = port;
                CurrentUsername = username;

                client = new TcpClient(serverAddress, port);
                stream = client.GetStream();

                // Create login message
                var loginMessage = new GameMessage
                {
                    Type = MessageType.Login,
                    Data = new Dictionary<string, object>
                    {
                        { "username", username },
                        { "password", password }
                    }
                };

                SendMessageToServer(loginMessage);

                // Wait for response
                GameMessage response = ReceiveMessageFromServer();

                if (response.Type == MessageType.Authentication &&
                    response.Data.ContainsKey("success") &&
                    (bool)response.Data["success"] &&
                    response.Data.ContainsKey("token"))
                {
                    authToken = response.Data["token"].ToString();
                    playerId = username;  // Use username as player ID
                    sessionId = authToken; // Use auth token as session ID

                    // Extract player ID from response if available
                    if (response.Data.ContainsKey("playerId"))
                    {
                        playerId = response.Data["playerId"].ToString();
                    }

                    // Start listener thread
                    Thread readThread = new Thread(ListenForServerMessages);
                    readThread.IsBackground = true;
                    readThread.Start();

                    return true;
                }

                return false;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Login failed: " + ex.Message);
                return false;
            }
        }

        public bool Register(string serverAddress, int port, string username, string password, string email)
        {
            try
            {
                client = new TcpClient(serverAddress, port);
                stream = client.GetStream();

                var registerMessage = new GameMessage
                {
                    Type = MessageType.Register,
                    Data = new Dictionary<string, object>
                    {
                        { "username", username },
                        { "password", password },
                        { "email", email }
                    }
                };

                SendMessageToServer(registerMessage);

                GameMessage response = ReceiveMessageFromServer();

                if (response.Type == MessageType.Authentication &&
                    response.Data.ContainsKey("success") &&
                    (bool)response.Data["success"])
                {
                    // Registration successful
                    return true;
                }

                return false;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Registration failed: " + ex.Message);
                return false;
            }
        }

        public void EnableWebInterface(int port = 8080)
        {
            if (!isWebInterfaceEnabled)
            {
                try
                {
                    string webRootPath = Path.Combine(localCachePath, "webui");
                    webUI = new WebUIController(webRootPath, port);
                    webUI.SetClient(this);
                    webUI.Start();

                    isWebInterfaceEnabled = true;
                    Console.WriteLine($"Web interface enabled at http://localhost:{port}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Failed to enable web interface: {ex.Message}");
                }
            }
        }

        public void DisableWebInterface()
        {
            if (isWebInterfaceEnabled && webUI != null)
            {
                webUI.Stop();
                isWebInterfaceEnabled = false;
                Console.WriteLine("Web interface disabled");
            }
        }

        // Friend System Methods
        public bool SendFriendRequest(string username) => friendsManager.SendFriendRequest(username);
        public bool AcceptFriendRequest(string username) => friendsManager.AcceptFriendRequest(username);
        public bool DeclineFriendRequest(string username) => friendsManager.DeclineFriendRequest(username);
        public bool RemoveFriend(string username) => friendsManager.RemoveFriend(username);
        public bool BlockUser(string username) => friendsManager.BlockUser(username);
        public List<FriendRelation> GetFriends() => friendsManager.GetFriends();
        public List<FriendRelation> GetBlockedUsers() => friendsManager.GetBlockedUsers();
        public List<string> GetIncomingFriendRequests() => friendsManager.GetIncomingRequests(CurrentUsername);
        public List<string> GetOutgoingFriendRequests() => friendsManager.GetOutgoingRequests(CurrentUsername);

        // Marketplace Methods
        public bool CreateMarketListing(string itemId, string itemName, int quantity, int price, float condition = 1.0f) =>
            marketplaceManager.CreateListing(itemId, itemName, quantity, price, condition);
        public bool PurchaseMarketListing(string listingId) => marketplaceManager.PurchaseListing(listingId);
        public bool CancelMarketListing(string listingId) => marketplaceManager.CancelListing(listingId);
        public List<MarketListing> GetActiveMarketListings() => marketplaceManager.GetActiveListings();
        public List<MarketListing> GetMyMarketListings() => marketplaceManager.GetMyListings();
        public List<MarketListing> GetMyPurchases() => marketplaceManager.GetMyPurchases();
        public List<MarketListing> SearchMarketListings(string searchTerm) => marketplaceManager.SearchListings(searchTerm);

        // Trading Methods
        public bool InitiateTrade(string targetUsername) => tradeManager.InitiateTrade(targetUsername);
        public bool AcceptTradeRequest(string tradeId) => tradeManager.AcceptTradeRequest(tradeId);
        public bool DeclineTradeRequest(string tradeId) => tradeManager.DeclineTradeRequest(tradeId);
        public bool AddItemToTrade(string itemId, string itemName, int quantity, float condition = 1.0f) =>
            tradeManager.AddItemToTrade(itemId, itemName, quantity, condition);
        public bool RemoveItemFromTrade(string itemId) => tradeManager.RemoveItemFromTrade(itemId);
        public bool UpdateItemQuantity(string itemId, int quantity) => tradeManager.UpdateItemQuantity(itemId, quantity);
        public bool ConfirmTradeOffer() => tradeManager.ConfirmTradeOffer();
        public bool CancelTrade() => tradeManager.CancelTrade();
        public List<TradeSession> GetIncomingTradeRequests() => tradeManager.GetIncomingTradeRequests();
        public List<TradeSession> GetOutgoingTradeRequests() => tradeManager.GetOutgoingTradeRequests();
        public List<TradeSession> GetTradeHistory(int limit = 10) => tradeManager.GetTradeHistory(limit);
        public TradeSession GetCurrentTrade() => tradeManager.GetCurrentTrade();

        // STUN related methods
        public async Task<IPEndPoint> GetPublicEndpoint(int localPort = 0) => await stunClient.GetPublicEndPointAsync(localPort);

        public byte[] RequestGameFile(string relativePath)
        {
            // Check if we already have this file cached
            string cachePath = Path.Combine(localCachePath, relativePath);

            if (File.Exists(cachePath) && cachedFileInfo.TryGetValue(relativePath, out var fileInfo))
            {
                // We have it cached, return the file
                return File.ReadAllBytes(cachePath);
            }

            // Request the file from server
            var fileRequest = new GameMessage
            {
                Type = "file_request",
                Data = new Dictionary<string, object>
                {
                    { "path", relativePath }
                },
                SessionId = authToken
            };

            SendMessageToServer(fileRequest);

            // Wait for response
            // In a real implementation, this would be asynchronous with callbacks
            GameMessage response = null;

            // Create a signal to wait for the response
            var responseSignal = new ManualResetEvent(false);

            // Event handler to capture the response
            EventHandler<GameMessage> responseHandler = null;
            responseHandler = (sender, msg) => {
                if (msg.Type == "file_data")
                {
                    response = msg;
                    responseSignal.Set();
                }
            };

            // Register the event handler
            MessageReceived += responseHandler;

            // Wait for the response with a timeout
            bool gotResponse = responseSignal.WaitOne(TimeSpan.FromSeconds(30));

            // Unregister the event handler
            MessageReceived -= responseHandler;

            if (!gotResponse || response == null)
            {
                throw new TimeoutException("Timeout waiting for file data");
            }

            if (response.Data.ContainsKey("data") && response.Data.ContainsKey("fileInfo"))
            {
                byte[] fileData = Convert.FromBase64String(response.Data["data"].ToString());
                GameFileInfo info = JsonSerializer.Deserialize<GameFileInfo>(
                    response.Data["fileInfo"].ToString());

                // Ensure directory exists
                Directory.CreateDirectory(Path.GetDirectoryName(cachePath));

                // Cache the file
                File.WriteAllBytes(cachePath, fileData);
                cachedFileInfo[relativePath] = info;

                return fileData;
            }

            throw new Exception("Failed to download file: " + relativePath);
        }

        public List<GameFileInfo> RequestFileList(string directory = "")
        {
            var fileListRequest = new GameMessage
            {
                Type = "file_list_request",
                Data = new Dictionary<string, object>
                {
                    { "directory", directory }
                },
                SessionId = authToken
            };

            SendMessageToServer(fileListRequest);

            // Wait for response
            GameMessage response = null;
            var responseSignal = new ManualResetEvent(false);

            EventHandler<GameMessage> responseHandler = null;
            responseHandler = (sender, msg) => {
                if (msg.Type == "file_list")
                {
                    response = msg;
                    responseSignal.Set();
                }
            };

            MessageReceived += responseHandler;
            bool gotResponse = responseSignal.WaitOne(TimeSpan.FromSeconds(30));
            MessageReceived -= responseHandler;

            if (!gotResponse || response == null)
            {
                throw new TimeoutException("Timeout waiting for file list");
            }

            if (response.Data.ContainsKey("files"))
            {
                return JsonSerializer.Deserialize<List<GameFileInfo>>(
                    response.Data["files"].ToString());
            }

            return new List<GameFileInfo>();
        }

        public void UpdatePosition(float newX, float newY)
        {
            float threshold = 0.5f;
            if (Math.Abs(lastX - newX) > threshold || Math.Abs(lastY - newY) > threshold)
            {
                var position = new Position { X = newX, Y = newY };
                var message = new GameMessage
                {
                    Type = MessageType.Position,
                    PlayerId = CurrentUsername,
                    Data = JsonSerializer.Deserialize<Dictionary<string, object>>(JsonSerializer.Serialize(position)),
                    SessionId = authToken
                };

                SendMessageToServer(message);
                lastX = newX;
                lastY = newY;
            }
        }

        public void PerformCombatAction(string targetId, string actionType)
        {
            TimeSpan cooldown = TimeSpan.FromSeconds(1);
            if (DateTime.Now - lastCombatTime < cooldown)
            {
                Console.WriteLine("Combat action rate limit reached.");
                return;
            }

            lastCombatTime = DateTime.Now;

            var combatAction = new CombatAction { TargetId = targetId, Action = actionType };
            var message = new GameMessage
            {
                Type = MessageType.Combat,
                PlayerId = CurrentUsername,
                Data = JsonSerializer.Deserialize<Dictionary<string, object>>(JsonSerializer.Serialize(combatAction)),
                SessionId = authToken
            };

            SendMessageToServer(message);
        }

        public void UpdateInventory(string itemName, int quantity)
        {
            var inventoryItem = new InventoryItem { ItemName = itemName, Quantity = quantity };
            var message = new GameMessage
            {
                Type = MessageType.Inventory,
                PlayerId = CurrentUsername,
                Data = JsonSerializer.Deserialize<Dictionary<string, object>>(JsonSerializer.Serialize(inventoryItem)),
                SessionId = authToken
            };

            SendMessageToServer(message);
        }

        public void UpdateHealth(int currentHealth, int maxHealth)
        {
            var healthStatus = new HealthStatus { CurrentHealth = currentHealth, MaxHealth = maxHealth };
            var message = new GameMessage
            {
                Type = MessageType.Health,
                PlayerId = CurrentUsername,
                Data = JsonSerializer.Deserialize<Dictionary<string, object>>(JsonSerializer.Serialize(healthStatus)),
                SessionId = authToken
            };

            SendMessageToServer(message);
        }

        private void ListenForServerMessages()
        {
            byte[] buffer = new byte[16384]; // Larger buffer for file transfers
            while (true)
            {
                try
                {
                    int bytesRead = stream.Read(buffer, 0, buffer.Length);
                    if (bytesRead > 0)
                    {
                        string encryptedMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                        string jsonMessage = EncryptionHelper.Decrypt(encryptedMessage);

                        GameMessage message = GameMessage.FromJson(jsonMessage);

                        // Raise the event with the received message
                        MessageReceived?.Invoke(this, message);

                        // Process based on message type
                        HandleGameMessage(message);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error in message listener: {ex.Message}");
                    break;
                }
            }
        }

        private void HandleGameMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                    var position = JsonSerializer.Deserialize<Position>(JsonSerializer.Serialize(message.Data));
                    // Handle position update
                    break;
                case MessageType.Inventory:
                    var item = JsonSerializer.Deserialize<InventoryItem>(JsonSerializer.Serialize(message.Data));
                    Console.WriteLine($"Player {message.PlayerId} has item: {item.ItemName} x{item.Quantity}");
                    break;
                case MessageType.Combat:
                    var combatAction = JsonSerializer.Deserialize<CombatAction>(JsonSerializer.Serialize(message.Data));
                    Console.WriteLine($"Player {message.PlayerId} performs {combatAction.Action} on {combatAction.TargetId}");
                    break;
                case MessageType.Health:
                    var health = JsonSerializer.Deserialize<HealthStatus>(JsonSerializer.Serialize(message.Data));
                    Console.WriteLine($"Player {message.PlayerId} health: {health.CurrentHealth}/{health.MaxHealth}");
                    break;
                    // Add other message types as needed
            }
        }

        private GameMessage ReceiveMessageFromServer()
        {
            byte[] buffer = new byte[8192];
            int bytesRead = stream.Read(buffer, 0, buffer.Length);
            string encryptedMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
            string jsonMessage = EncryptionHelper.Decrypt(encryptedMessage);
            return GameMessage.FromJson(jsonMessage);
        }

        /// <summary>
        /// Send a game message to the server
        /// </summary>
        public void SendMessage(GameMessage message)
        {
            SendMessageToServer(message);
        }

        /// <summary>
        /// Send a game message to the server (internal implementation)
        /// </summary>
        internal void SendMessageToServer(GameMessage message)
        {
            if (stream == null || !client.Connected)
            {
                throw new InvalidOperationException("Not connected to server");
            }

            string jsonMessage = message.ToJson();
            string encryptedMessage = EncryptionHelper.Encrypt(jsonMessage);
            byte[] messageBuffer = Encoding.ASCII.GetBytes(encryptedMessage);
            stream.Write(messageBuffer, 0, messageBuffer.Length);
        }

        #region Spawn Methods

        /// <summary>
        /// Request to spawn into the game at a specific location
        /// </summary>
        public void RequestSpawn(string location = "Hub")
        {
            if (!IsLoggedIn)
            {
                Console.WriteLine("Cannot request spawn: not logged in");
                return;
            }

            var spawnRequest = new GameMessage
            {
                Type = MessageType.SpawnRequest,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "location", location }
                }
            };

            SendMessageToServer(spawnRequest);
            Console.WriteLine($"Spawn request sent for location: {location}");
        }

        /// <summary>
        /// Request to spawn with friends at a specific location
        /// </summary>
        public void RequestGroupSpawn(List<string> playerIds, string location = "Hub")
        {
            if (!IsLoggedIn)
            {
                Console.WriteLine("Cannot request group spawn: not logged in");
                return;
            }

            var groupSpawnRequest = new GameMessage
            {
                Type = MessageType.GroupSpawnRequest,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "playerIds", playerIds },
                    { "location", location }
                }
            };

            SendMessageToServer(groupSpawnRequest);
            Console.WriteLine($"Group spawn request sent for {playerIds.Count} players at {location}");
        }

        /// <summary>
        /// Signal ready for group spawn
        /// </summary>
        public void ReadyForGroupSpawn(string groupId)
        {
            if (!IsLoggedIn || string.IsNullOrEmpty(groupId))
                return;

            var readyMessage = new GameMessage
            {
                Type = MessageType.GroupSpawnReady,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "groupId", groupId }
                }
            };

            SendMessageToServer(readyMessage);
            Console.WriteLine($"Ready for group spawn: {groupId}");
        }

        #endregion

        #region Game Commands

        /// <summary>
        /// Send move command to server
        /// </summary>
        public void SendMoveCommand(float x, float y, float z)
        {
            if (!IsLoggedIn)
                return;

            var moveMessage = new GameMessage
            {
                Type = MessageType.MoveCommand,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "x", x },
                    { "y", y },
                    { "z", z }
                }
            };

            SendMessageToServer(moveMessage);
        }

        /// <summary>
        /// Send attack command to server
        /// </summary>
        public void SendAttackCommand(string targetId)
        {
            if (!IsLoggedIn || string.IsNullOrEmpty(targetId))
                return;

            var attackMessage = new GameMessage
            {
                Type = MessageType.AttackCommand,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "targetId", targetId }
                }
            };

            SendMessageToServer(attackMessage);
        }

        /// <summary>
        /// Send follow command to server
        /// </summary>
        public void SendFollowCommand(string targetPlayerId)
        {
            if (!IsLoggedIn || string.IsNullOrEmpty(targetPlayerId))
                return;

            var followMessage = new GameMessage
            {
                Type = MessageType.FollowCommand,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "targetId", targetPlayerId }
                }
            };

            SendMessageToServer(followMessage);
        }

        /// <summary>
        /// Send position update to server
        /// </summary>
        public void SendPositionUpdate(float x, float y, float z, float rotationY = 0)
        {
            if (!IsLoggedIn)
                return;

            var positionMessage = new GameMessage
            {
                Type = MessageType.Position,
                PlayerId = playerId,
                SessionId = sessionId,
                Data = new Dictionary<string, object>
                {
                    { "x", x },
                    { "y", y },
                    { "z", z },
                    { "rotY", rotationY }
                }
            };

            SendMessageToServer(positionMessage);
        }

        #endregion

        public void Disconnect()
        {
            DisableWebInterface();

            if (client != null && client.Connected)
            {
                client.Close();
            }
        }
    }
}
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking.Authority;

namespace KenshiMultiplayer.Networking
{
    public class EnhancedServer
    {
        private TcpListener server;
        private List<TcpClient> connectedClients = new List<TcpClient>();
        private Dictionary<string, Lobby> lobbies = new Dictionary<string, Lobby>();
        private GameFileManager fileManager;
        private Dictionary<string, string> activeUserSessions = new Dictionary<string, string>(); // Maps authToken to username

        // Authority system integration
        private ServerContext serverContext;
        private readonly Dictionary<string, TcpClient> playerClients = new Dictionary<string, TcpClient>();
        private readonly Dictionary<TcpClient, string> clientPlayers = new Dictionary<TcpClient, string>();

        /// <summary>
        /// Get the server's authority context for save system integration
        /// </summary>
        public ServerContext Context => serverContext;

        public EnhancedServer(string kenshiRootPath)
        {
            fileManager = new GameFileManager(kenshiRootPath);

            // Initialize server context with authority system
            string savePath = System.IO.Path.Combine(kenshiRootPath, "multiplayer_data");
            serverContext = new ServerContext(savePath);

            // Subscribe to authority events
            serverContext.OnActionRejected += OnActionRejected;
            serverContext.OnPlayerSaveUpdated += OnPlayerSaveUpdated;
        }

        /// <summary>
        /// Handle authority rejection - notify client
        /// </summary>
        private void OnActionRejected(string playerId, AuthorityValidationResult result)
        {
            if (playerClients.TryGetValue(playerId, out var client))
            {
                var errorMessage = new GameMessage
                {
                    Type = MessageType.Error,
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "code", "ACTION_REJECTED" },
                        { "reason", result.RejectionReason }
                    }
                };
                SendMessageToClient(client, errorMessage.ToJson());
            }
        }

        /// <summary>
        /// Handle save updates - can notify client if needed
        /// </summary>
        private void OnPlayerSaveUpdated(string playerId, string version)
        {
            Logger.Log($"Player {playerId} save updated to {version}");
        }

        public void Start(int port = 5555)
        {
            server = new TcpListener(IPAddress.Any, port);
            server.Start();
            Logger.Log($"Enhanced server started on port {port}.");

            // Start admin console thread
            Thread adminThread = new Thread(StartCommandLoop);
            adminThread.IsBackground = true;
            adminThread.Start();

            // Main client acceptance loop
            while (true)
            {
                TcpClient client = server.AcceptTcpClient();
                connectedClients.Add(client);
                Logger.Log("Client connected.");

                Thread clientThread = new Thread(() => HandleClient(client));
                clientThread.IsBackground = true;
                clientThread.Start();
            }
        }

        private void HandleClient(TcpClient client)
        {
            NetworkStream stream = client.GetStream();
            byte[] buffer = new byte[16384]; // Larger buffer for file transfers
            string authenticatedUser = null;
            string authToken = null;

            try
            {
                while (true)
                {
                    int bytesRead = stream.Read(buffer, 0, buffer.Length);
                    if (bytesRead > 0)
                    {
                        string encryptedMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                        string jsonMessage = EncryptionHelper.Decrypt(encryptedMessage);

                        GameMessage message = GameMessage.FromJson(jsonMessage);

                        // Handle authentication
                        if (message.Type == MessageType.Login)
                        {
                            HandleLogin(message, client);
                            if (message.Data.ContainsKey("username"))
                            {
                                authenticatedUser = message.Data["username"].ToString();
                            }
                        }
                        else if (message.Type == MessageType.Register)
                        {
                            HandleRegistration(message, client);
                        }
                        // Handle file requests
                        else if (message.Type == "file_request")
                        {
                            if (ValidateAuthToken(message.SessionId, out string username))
                            {
                                HandleFileRequest(message, client);
                                authenticatedUser = username;
                                authToken = message.SessionId;
                            }
                            else
                            {
                                SendErrorToClient(client, "Authentication required");
                            }
                        }
                        else if (message.Type == "file_list_request")
                        {
                            if (ValidateAuthToken(message.SessionId, out string username))
                            {
                                HandleFileListRequest(message, client);
                                authenticatedUser = username;
                                authToken = message.SessionId;
                            }
                            else
                            {
                                SendErrorToClient(client, "Authentication required");
                            }
                        }
                        // Handle other message types
                        else if (ValidateAuthToken(message.SessionId, out string username))
                        {
                            authenticatedUser = username;
                            authToken = message.SessionId;

                            // Handle various message types
                            switch (message.Type)
                            {
                                case MessageType.Chat:
                                    HandleChatMessage(message, client);
                                    break;
                                case MessageType.Position:
                                case MessageType.Inventory:
                                case MessageType.Combat:
                                case MessageType.Health:
                                    // Broadcast to other clients
                                    if (ValidateMessage(message))
                                    {
                                        Logger.Log($"Received {message.Type} from {authenticatedUser}");
                                        BroadcastMessage(jsonMessage, client);
                                    }
                                    else
                                    {
                                        Logger.Log($"Invalid message from {authenticatedUser}: {message.Type}");
                                    }
                                    break;
                                // Spawn requests
                                case MessageType.SpawnRequest:
                                    this.HandleSpawnRequest(message);
                                    break;
                                case MessageType.GroupSpawnRequest:
                                    this.HandleGroupSpawnRequest(message);
                                    break;
                                case MessageType.GroupSpawnReady:
                                    this.HandleGroupSpawnReady(message);
                                    break;
                                // Game commands
                                case MessageType.MoveCommand:
                                    HandleMoveCommand(message, client);
                                    break;
                                case MessageType.AttackCommand:
                                    HandleAttackCommand(message, client);
                                    break;
                                case MessageType.FollowCommand:
                                    HandleFollowCommand(message, client);
                                    break;
                                case MessageType.PickupCommand:
                                    HandlePickupCommand(message, client);
                                    break;
                                // Squad commands
                                case MessageType.SquadCreate:
                                case MessageType.SquadDisband:
                                case MessageType.SquadJoin:
                                case MessageType.SquadLeave:
                                case MessageType.SquadCommand:
                                    HandleSquadCommand(message, client);
                                    break;
                                default:
                                    Logger.Log($"Unhandled message type: {message.Type}");
                                    break;
                            }
                        }
                        else
                        {
                            SendErrorToClient(client, "Authentication required");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Client disconnected: {ex.Message}");
            }
            finally
            {
                // Clean up
                if (authToken != null)
                {
                    activeUserSessions.Remove(authToken);
                }

                // Unregister from authority system
                if (clientPlayers.TryGetValue(client, out string playerId))
                {
                    _ = serverContext.UnregisterPlayer(playerId).ContinueWith(t =>
                    {
                        if (t.IsFaulted)
                            Logger.Log($"Error unregistering player {playerId}: {t.Exception?.InnerException?.Message}");
                    }, TaskContinuationOptions.OnlyOnFaulted);
                    playerClients.Remove(playerId);
                    clientPlayers.Remove(client);
                }

                connectedClients.Remove(client);
                client.Close();
                Logger.Log($"Client connection closed. {connectedClients.Count} clients remaining.");
            }
        }

        private void HandleLogin(GameMessage message, TcpClient client)
        {
            if (!message.Data.TryGetValue("username", out var usernameObj) ||
                !message.Data.TryGetValue("password", out var passwordObj))
            {
                SendErrorToClient(client, "Login failed: missing credentials");
                return;
            }

            string username = usernameObj?.ToString();
            string password = passwordObj?.ToString();

            if (string.IsNullOrEmpty(username) || string.IsNullOrEmpty(password))
            {
                SendErrorToClient(client, "Login failed: invalid credentials");
                return;
            }

            var (success, sessionId, errorMessage) = UserManager.Login(username, password);

            if (success)
            {
                // Generate JWT token
                string token = AuthManager.GenerateJWT(username);

                // Store active session
                activeUserSessions[token] = username;

                // Register player with authority system
                string playerId = username; // Use username as playerId for simplicity
                _ = serverContext.RegisterPlayer(playerId, username).ContinueWith(t =>
                {
                    if (t.IsFaulted)
                        Logger.Log($"Error registering player {playerId}: {t.Exception?.InnerException?.Message}");
                }, TaskContinuationOptions.OnlyOnFaulted);

                // Track client-player mapping
                playerClients[playerId] = client;
                clientPlayers[client] = playerId;

                var response = new GameMessage
                {
                    Type = MessageType.Authentication,
                    Data = new Dictionary<string, object>
                    {
                        { "success", true },
                        { "token", token },
                        { "username", username },
                        { "playerId", playerId }
                    }
                };

                SendMessageToClient(client, response.ToJson());
                Logger.Log($"User {username} authenticated and registered with authority system");
            }
            else
            {
                var response = new GameMessage
                {
                    Type = MessageType.Authentication,
                    Data = new Dictionary<string, object>
                    {
                        { "success", false },
                        { "error", errorMessage }
                    }
                };

                SendMessageToClient(client, response.ToJson());
                Logger.Log($"Authentication failed for {username}: {errorMessage}");
            }
        }

        private void HandleRegistration(GameMessage message, TcpClient client)
        {
            string username = message.Data["username"].ToString();
            string password = message.Data["password"].ToString();
            string email = message.Data["email"].ToString();

            var (success, errorMessage) = UserManager.RegisterUser(username, password, email);

            var response = new GameMessage
            {
                Type = MessageType.Authentication,
                Data = new Dictionary<string, object>
                {
                    { "success", success },
                    { "error", errorMessage ?? "" }
                }
            };

            SendMessageToClient(client, response.ToJson());

            if (success)
            {
                Logger.Log($"New user registered: {username}");
            }
            else
            {
                Logger.Log($"Registration failed for {username}: {errorMessage}");
            }
        }

        private void HandleFileRequest(GameMessage message, TcpClient client)
        {
            string relativePath = message.Data["path"].ToString();

            try
            {
                // Get file data
                byte[] fileData = fileManager.GetFileData(relativePath);
                GameFileInfo fileInfo = fileManager.GetFileInfo(relativePath);

                // Create response
                var response = new GameMessage
                {
                    Type = "file_data",
                    Data = new Dictionary<string, object>
                    {
                        { "data", Convert.ToBase64String(fileData) },
                        { "fileInfo", JsonSerializer.Serialize(fileInfo) }
                    }
                };

                SendMessageToClient(client, response.ToJson());
                Logger.Log($"Sent file {relativePath} ({fileData.Length} bytes)");
            }
            catch (Exception ex)
            {
                SendErrorToClient(client, $"File request failed: {ex.Message}");
            }
        }

        private void HandleFileListRequest(GameMessage message, TcpClient client)
        {
            string directory = message.Data.ContainsKey("directory")
                ? message.Data["directory"].ToString()
                : "";

            try
            {
                List<GameFileInfo> files = fileManager.GetDirectoryContents(directory);

                var response = new GameMessage
                {
                    Type = "file_list",
                    Data = new Dictionary<string, object>
                    {
                        { "files", JsonSerializer.Serialize(files) },
                        { "directory", directory }
                    }
                };

                SendMessageToClient(client, response.ToJson());
                Logger.Log($"Sent file list for {directory} ({files.Count} files)");
            }
            catch (Exception ex)
            {
                SendErrorToClient(client, $"File list request failed: {ex.Message}");
            }
        }

        private bool ValidateAuthToken(string token, out string username)
        {
            username = null;

            if (string.IsNullOrEmpty(token))
                return false;

            if (activeUserSessions.TryGetValue(token, out username))
                return true;

            if (AuthManager.ValidateJWT(token, out username))
            {
                // If valid but not in active sessions, add it
                activeUserSessions[token] = username;
                return true;
            }

            return false;
        }

        private void BroadcastMessage(string jsonMessage, TcpClient senderClient)
        {
            string encryptedMessage = EncryptionHelper.Encrypt(jsonMessage);
            byte[] messageBuffer = Encoding.ASCII.GetBytes(encryptedMessage);

            foreach (var client in connectedClients)
            {
                if (client != senderClient && client.Connected)
                {
                    try
                    {
                        NetworkStream stream = client.GetStream();
                        stream.Write(messageBuffer, 0, messageBuffer.Length);
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"Error broadcasting to client: {ex.Message}");
                    }
                }
            }
        }

        private bool ValidateMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                case MessageType.Inventory:
                case MessageType.Combat:
                case MessageType.Health:
                case MessageType.Chat:
                    return true;
                default:
                    return false;
            }
        }

        private void SendErrorToClient(TcpClient client, string errorMessage)
        {
            var response = new GameMessage
            {
                Type = MessageType.Error,
                Data = new Dictionary<string, object> { { "message", errorMessage } }
            };

            SendMessageToClient(client, response.ToJson());
        }

        private void SendMessageToClient(TcpClient client, string message)
        {
            if (client != null && client.Connected)
            {
                try
                {
                    string encryptedMessage = EncryptionHelper.Encrypt(message);
                    byte[] messageBuffer = Encoding.ASCII.GetBytes(encryptedMessage);
                    NetworkStream stream = client.GetStream();
                    stream.Write(messageBuffer, 0, messageBuffer.Length);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error sending message to client: {ex.Message}");
                }
            }
        }

        private void HandleChatMessage(GameMessage message, TcpClient senderClient)
        {
            // Extract user info from token
            string username = null;
            ValidateAuthToken(message.SessionId, out username);

            if (message.LobbyId != null && lobbies.TryGetValue(message.LobbyId, out var lobby))
            {
                string channel = message.Data.ContainsKey("channel") ? message.Data["channel"].ToString() : "general";
                string chatMessage = message.Data.ContainsKey("message") ? message.Data["message"].ToString() : string.Empty;
                lobby.BroadcastToChannel(channel, $"[{username}]: {chatMessage}", senderClient);
                Logger.Log($"Chat in lobby {message.LobbyId}, channel {channel}: {username}: {chatMessage}");
            }
            else
            {
                // Global chat
                string chatMessage = message.Data.ContainsKey("message") ? message.Data["message"].ToString() : string.Empty;

                var broadcastMessage = new GameMessage
                {
                    Type = MessageType.Chat,
                    PlayerId = username,
                    Data = new Dictionary<string, object>
                    {
                        { "message", chatMessage }
                    }
                };

                BroadcastMessage(broadcastMessage.ToJson(), senderClient);
                Logger.Log($"Global chat: {username}: {chatMessage}");
            }
        }

        private void StartCommandLoop()
        {
            while (true)
            {
                try
                {
                    string command = Console.ReadLine();
                    if (string.IsNullOrEmpty(command))
                        continue;

                    if (command.StartsWith("/help"))
                    {
                        Console.WriteLine("Available commands:");
                        Console.WriteLine("/list - List active players");
                        Console.WriteLine("/kick <username> - Kick a player");
                        Console.WriteLine("/ban <username> <hours> - Ban a player for specified hours");
                        Console.WriteLine("/create-lobby <id> <isPrivate> <password> <maxPlayers> - Create a new lobby");
                        Console.WriteLine("/list-lobbies - List all lobbies");
                        Console.WriteLine("/broadcast <message> - Broadcast a message to all clients");
                    }
                    else if (command.StartsWith("/list"))
                    {
                        ListActivePlayers();
                    }
                    else if (command.StartsWith("/kick "))
                    {
                        string username = command.Substring(6).Trim();
                        KickPlayer(username);
                    }
                    else if (command.StartsWith("/ban "))
                    {
                        string[] parts = command.Substring(5).Trim().Split(' ');
                        if (parts.Length >= 2 && int.TryParse(parts[1], out int hours))
                        {
                            BanPlayer(parts[0], hours);
                        }
                        else
                        {
                            Console.WriteLine("Invalid ban command. Use: /ban <username> <hours>");
                        }
                    }
                    else if (command.StartsWith("/create-lobby "))
                    {
                        string[] parts = command.Substring(14).Trim().Split(' ');
                        if (parts.Length >= 4)
                        {
                            string lobbyId = parts[0];
                            bool isPrivate = bool.Parse(parts[1]);
                            string password = parts[2];
                            int maxPlayers = int.Parse(parts[3]);

                            CreateLobby(lobbyId, isPrivate, password, maxPlayers);
                            Console.WriteLine($"Lobby {lobbyId} created");
                        }
                        else
                        {
                            Console.WriteLine("Invalid create-lobby command. Use: /create-lobby <id> <isPrivate> <password> <maxPlayers>");
                        }
                    }
                    else if (command.StartsWith("/list-lobbies"))
                    {
                        ListLobbies();
                    }
                    else if (command.StartsWith("/broadcast "))
                    {
                        string message = command.Substring(11).Trim();
                        BroadcastSystemMessage(message);
                        Console.WriteLine("Message broadcasted");
                    }
                    else
                    {
                        Console.WriteLine("Unknown command. Type /help for a list of commands");
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error executing command: {ex.Message}");
                }
            }
        }

        private void ListActivePlayers()
        {
            Console.WriteLine("Active players:");

            // List all players from active sessions
            foreach (var username in activeUserSessions.Values)
            {
                Console.WriteLine($"- {username}");
            }

            Console.WriteLine($"Total players: {activeUserSessions.Count}");
        }

        private void KickPlayer(string username)
        {
            // Find user's auth token
            string tokenToRemove = null;
            foreach (var kvp in activeUserSessions)
            {
                if (kvp.Value == username)
                {
                    tokenToRemove = kvp.Key;
                    break;
                }
            }

            if (tokenToRemove != null)
            {
                activeUserSessions.Remove(tokenToRemove);

                // Also check for user in lobbies
                foreach (var lobby in lobbies.Values)
                {
                    lobby.KickPlayer(username);
                }

                Logger.Log($"Player {username} was kicked");
                Console.WriteLine($"Player {username} was kicked");
            }
            else
            {
                Console.WriteLine($"Player {username} not found");
            }
        }

        private void BanPlayer(string username, int hours)
        {
            bool success = UserManager.BanUser("admin", username, TimeSpan.FromHours(hours), $"Banned by admin for {hours} hours");

            if (success)
            {
                // Also kick the player
                KickPlayer(username);
                Logger.Log($"Player {username} was banned for {hours} hours");
                Console.WriteLine($"Player {username} was banned for {hours} hours");
            }
            else
            {
                Console.WriteLine($"Failed to ban player {username}");
            }
        }

        private void ListLobbies()
        {
            Console.WriteLine("Active lobbies:");

            foreach (var kvp in lobbies)
            {
                string lobbyId = kvp.Key;
                Lobby lobby = kvp.Value;

                Console.WriteLine($"- {lobbyId} (Players: {lobby.Players.Count}/{lobby.MaxPlayers}, Private: {lobby.IsPrivate})");
            }

            Console.WriteLine($"Total lobbies: {lobbies.Count}");
        }

        private void BroadcastSystemMessage(string message)
        {
            var systemMessage = new GameMessage
            {
                Type = MessageType.SystemMessage,
                PlayerId = "SYSTEM",
                Data = new Dictionary<string, object> { { "message", message } }
            };

            foreach (var client in connectedClients)
            {
                SendMessageToClient(client, systemMessage.ToJson());
            }

            Logger.Log($"System broadcast: {message}");
        }

        public void CreateLobby(string lobbyId, bool isPrivate, string password, int maxPlayers)
        {
            if (!lobbies.ContainsKey(lobbyId))
            {
                lobbies[lobbyId] = new Lobby(lobbyId, isPrivate, password, maxPlayers);
                Logger.Log($"Lobby {lobbyId} created.");
            }
        }

        public bool JoinLobby(string lobbyId, TcpClient client, string password = "")
        {
            if (lobbies.ContainsKey(lobbyId) && lobbies[lobbyId].CanJoin(password))
            {
                lobbies[lobbyId].AddPlayer(client);
                Logger.Log($"Client joined lobby {lobbyId}.");
                return true;
            }
            return false;
        }

        #region Game Command Handlers

        private void HandleMoveCommand(GameMessage message, TcpClient senderClient)
        {
            try
            {
                string playerId = message.PlayerId;
                float x = message.Data.ContainsKey("x") ? Convert.ToSingle(message.Data["x"]) : 0;
                float y = message.Data.ContainsKey("y") ? Convert.ToSingle(message.Data["y"]) : 0;
                float z = message.Data.ContainsKey("z") ? Convert.ToSingle(message.Data["z"]) : 0;

                // Get previous position for validation
                var player = serverContext.GetPlayer(playerId);
                float prevX = player?.LastX ?? x;
                float prevY = player?.LastY ?? y;
                float prevZ = player?.LastZ ?? z;
                long prevTime = player?.LastUpdateTime ?? DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                float timeDelta = (DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - prevTime) / 1000f;

                // Validate position update through authority system
                var validationResult = serverContext.ValidatePositionUpdate(
                    playerId, x, y, z, prevX, prevY, prevZ, timeDelta);

                if (!validationResult.IsValid)
                {
                    Logger.Log($"Position rejected for {playerId}: {validationResult.RejectionReason}");
                    // Send correction to client
                    var correction = new GameMessage
                    {
                        Type = MessageType.Position,
                        PlayerId = playerId,
                        Data = new Dictionary<string, object>
                        {
                            { "x", prevX },
                            { "y", prevY },
                            { "z", prevZ },
                            { "correction", true }
                        }
                    };
                    SendMessageToClient(senderClient, correction.ToJson());
                    return;
                }

                // Update player's last known position
                if (player != null)
                {
                    player.LastX = x;
                    player.LastY = y;
                    player.LastZ = z;
                    player.LastUpdateTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                }

                Logger.Log($"Move command from {playerId}: ({x}, {y}, {z}) - validated");

                // Get game state manager and execute move
                var gsm = this.GetGameStateManager();
                if (gsm != null)
                {
                    gsm.MovePlayer(playerId, new Data.Position { X = x, Y = y, Z = z });
                }

                // Broadcast position update to other clients
                var positionUpdate = new GameMessage
                {
                    Type = MessageType.Position,
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "x", x },
                        { "y", y },
                        { "z", z }
                    }
                };
                BroadcastMessage(positionUpdate.ToJson(), senderClient);
            }
            catch (Exception ex)
            {
                Logger.Log($"ERROR handling move command: {ex.Message}");
            }
        }

        private void HandleAttackCommand(GameMessage message, TcpClient senderClient)
        {
            try
            {
                string playerId = message.PlayerId;
                string targetId = message.Data.ContainsKey("targetId") ? message.Data["targetId"].ToString() : "";
                string weaponId = message.Data.ContainsKey("weaponId") ? message.Data["weaponId"].ToString() : null;

                Logger.Log($"Attack command from {playerId} targeting {targetId}");

                // Validate combat action through authority system
                var validationResult = serverContext.ValidateCombatAction(
                    playerId, targetId, "attack", weaponId);

                if (!validationResult.IsValid)
                {
                    Logger.Log($"Combat rejected for {playerId}: {validationResult.RejectionReason}");
                    return;
                }

                // Get game state manager and execute attack
                var gsm = this.GetGameStateManager();
                if (gsm != null && !string.IsNullOrEmpty(targetId))
                {
                    gsm.AttackTarget(playerId, targetId);
                }

                // Broadcast combat action to other clients (server-validated)
                var combatUpdate = new GameMessage
                {
                    Type = MessageType.Combat,
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "targetId", targetId },
                        { "action", "attack" },
                        { "validated", true }
                    }
                };
                BroadcastMessage(combatUpdate.ToJson(), senderClient);
            }
            catch (Exception ex)
            {
                Logger.Log($"ERROR handling attack command: {ex.Message}");
            }
        }

        private void HandleFollowCommand(GameMessage message, TcpClient senderClient)
        {
            try
            {
                string playerId = message.PlayerId;
                string targetId = message.Data.ContainsKey("targetId") ? message.Data["targetId"].ToString() : "";

                Logger.Log($"Follow command from {playerId} targeting {targetId}");

                // Get game state manager and execute follow
                var gsm = this.GetGameStateManager();
                if (gsm != null && !string.IsNullOrEmpty(targetId))
                {
                    gsm.FollowPlayer(playerId, targetId);
                }

                // Notify clients
                var followUpdate = new GameMessage
                {
                    Type = MessageType.FollowCommand,
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "targetId", targetId }
                    }
                };
                BroadcastMessage(followUpdate.ToJson(), senderClient);
            }
            catch (Exception ex)
            {
                Logger.Log($"ERROR handling follow command: {ex.Message}");
            }
        }

        private void HandlePickupCommand(GameMessage message, TcpClient senderClient)
        {
            try
            {
                string playerId = message.PlayerId;
                string itemId = message.Data.ContainsKey("itemId") ? message.Data["itemId"].ToString() : "";
                int quantity = message.Data.ContainsKey("quantity") ? Convert.ToInt32(message.Data["quantity"]) : 1;

                Logger.Log($"Pickup command from {playerId} for item {itemId}");

                if (string.IsNullOrEmpty(itemId))
                    return;

                // Validate inventory change through authority system
                AuthorityValidationResult validationResult;
                try
                {
                    validationResult = serverContext.ValidateInventoryChange(playerId, itemId, quantity, "pickup").GetAwaiter().GetResult();
                }
                catch (Exception ex)
                {
                    Logger.Log($"Pickup validation error for {playerId}: {ex.Message}");
                    return;
                }

                if (!validationResult.IsValid)
                {
                    Logger.Log($"Pickup rejected for {playerId}: {validationResult.RejectionReason}");
                    return;
                }

                // Get game state manager and execute pickup
                var gsm = this.GetGameStateManager();
                if (gsm != null)
                {
                    gsm.PickupItem(playerId, itemId);
                }

                // Broadcast inventory update
                var inventoryUpdate = new GameMessage
                {
                    Type = MessageType.Inventory,
                    PlayerId = playerId,
                    Data = new Dictionary<string, object>
                    {
                        { "itemId", itemId },
                        { "change", quantity },
                        { "action", "pickup" }
                    }
                };
                BroadcastMessage(inventoryUpdate.ToJson(), senderClient);
            }
            catch (Exception ex)
            {
                Logger.Log($"ERROR handling pickup command: {ex.Message}");
            }
        }

        private void HandleSquadCommand(GameMessage message, TcpClient senderClient)
        {
            try
            {
                string playerId = message.PlayerId;
                string action = message.Type;

                Logger.Log($"Squad command '{action}' from {playerId}");

                var gsm = this.GetGameStateManager();
                if (gsm == null)
                    return;

                switch (action)
                {
                    case MessageType.SquadCreate:
                        var playerIds = message.Data.ContainsKey("playerIds")
                            ? JsonSerializer.Deserialize<List<string>>(message.Data["playerIds"].ToString())
                            : new List<string> { playerId };
                        string squadId = gsm.CreateSquad(playerIds);

                        // Send squad created notification
                        var createResponse = new GameMessage
                        {
                            Type = MessageType.SquadCreate,
                            PlayerId = playerId,
                            Data = new Dictionary<string, object>
                            {
                                { "squadId", squadId },
                                { "playerIds", playerIds }
                            }
                        };
                        BroadcastToAll(createResponse);
                        break;

                    case MessageType.SquadDisband:
                    case MessageType.SquadJoin:
                    case MessageType.SquadLeave:
                    case MessageType.SquadCommand:
                        // Broadcast the command to relevant clients
                        BroadcastMessage(message.ToJson(), senderClient);
                        break;
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"ERROR handling squad command: {ex.Message}");
            }
        }

        #endregion

        #region Broadcast Methods

        /// <summary>
        /// Broadcast a message to all connected clients
        /// </summary>
        public void BroadcastToAll(GameMessage message)
        {
            string jsonMessage = message.ToJson();
            string encryptedMessage = EncryptionHelper.Encrypt(jsonMessage);
            byte[] messageBuffer = Encoding.ASCII.GetBytes(encryptedMessage);

            foreach (var client in connectedClients.ToList())
            {
                if (client.Connected)
                {
                    try
                    {
                        NetworkStream stream = client.GetStream();
                        stream.Write(messageBuffer, 0, messageBuffer.Length);
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"Error broadcasting to client: {ex.Message}");
                    }
                }
            }
        }

        #endregion
    }
}
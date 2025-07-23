using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    public class WebUIController
    {
        private HttpListener httpListener;
        private WebSocketManager wsManager;
        private readonly string webRootPath;
        private readonly int port;
        private EnhancedClient client;
        private EnhancedServer server;
        private bool isRunning = false;
        private CancellationTokenSource cancellationTokenSource;

        // UI update timers
        private Timer statsUpdateTimer;
        private Timer playerListUpdateTimer;

        public WebUIController(string webRootPath, int port = 8080)
        {
            this.webRootPath = webRootPath;
            this.port = port;
            this.wsManager = new WebSocketManager();

            // Ensure web root directory exists
            Directory.CreateDirectory(webRootPath);

            // Create default index.html if it doesn't exist
            string indexPath = Path.Combine(webRootPath, "index.html");
            if (!File.Exists(indexPath))
            {
                CreateDefaultUI(indexPath);
            }
        }

        public void SetClient(EnhancedClient client)
        {
            this.client = client;

            // Subscribe to client events
            if (client != null)
            {
                client.MessageReceived += OnClientMessageReceived;
            }
        }

        public void SetServer(EnhancedServer server)
        {
            this.server = server;
        }

        public void Start()
        {
            if (isRunning) return;

            cancellationTokenSource = new CancellationTokenSource();
            httpListener = new HttpListener();
            httpListener.Prefixes.Add($"http://localhost:{port}/");
            httpListener.Prefixes.Add($"http://+:{port}/");

            try
            {
                httpListener.Start();
                isRunning = true;

                // Start listening for HTTP requests
                Task.Run(() => HandleHttpRequests(cancellationTokenSource.Token));

                // Start update timers
                statsUpdateTimer = new Timer(UpdateStats, null, TimeSpan.Zero, TimeSpan.FromSeconds(5));
                playerListUpdateTimer = new Timer(UpdatePlayerList, null, TimeSpan.Zero, TimeSpan.FromSeconds(2));

                Logger.Log($"WebUI started on port {port}");
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to start WebUI: {ex.Message}");
                throw;
            }
        }

        public void Stop()
        {
            if (!isRunning) return;

            isRunning = false;
            cancellationTokenSource?.Cancel();

            statsUpdateTimer?.Dispose();
            playerListUpdateTimer?.Dispose();

            httpListener?.Stop();
            httpListener?.Close();

            wsManager?.Shutdown();

            Logger.Log("WebUI stopped");
        }

        private async Task HandleHttpRequests(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested && isRunning)
            {
                try
                {
                    var context = await httpListener.GetContextAsync();
                    _ = Task.Run(() => ProcessRequest(context), cancellationToken);
                }
                catch (Exception ex)
                {
                    if (!cancellationToken.IsCancellationRequested)
                    {
                        Logger.Log($"HTTP listener error: {ex.Message}");
                    }
                }
            }
        }

        private async Task ProcessRequest(HttpListenerContext context)
        {
            try
            {
                var request = context.Request;
                var response = context.Response;

                // Handle WebSocket upgrade
                if (request.IsWebSocketRequest)
                {
                    await HandleWebSocketRequest(context);
                    return;
                }

                // Handle API requests
                if (request.Url.AbsolutePath.StartsWith("/api/"))
                {
                    await HandleApiRequest(context);
                    return;
                }

                // Serve static files
                await ServeStaticFile(context);
            }
            catch (Exception ex)
            {
                Logger.Log($"Request processing error: {ex.Message}");
                SendErrorResponse(context.Response, 500, "Internal Server Error");
            }
        }

        private async Task HandleWebSocketRequest(HttpListenerContext context)
        {
            try
            {
                var wsContext = await context.AcceptWebSocketAsync(null);
                var webSocket = wsContext.WebSocket;
                string clientId = Guid.NewGuid().ToString();

                // Get user ID from query string or headers
                string userId = context.Request.QueryString["userId"] ?? clientId;

                await wsManager.HandleWebSocketAsync(webSocket, clientId, userId);
            }
            catch (Exception ex)
            {
                Logger.Log($"WebSocket error: {ex.Message}");
            }
        }

        private async Task HandleApiRequest(HttpListenerContext context)
        {
            var request = context.Request;
            var response = context.Response;
            string path = request.Url.AbsolutePath;

            response.ContentType = "application/json";
            response.Headers.Add("Access-Control-Allow-Origin", "*");

            try
            {
                object responseData = null;

                switch (path)
                {
                    case "/api/status":
                        responseData = GetServerStatus();
                        break;

                    case "/api/player":
                        responseData = GetPlayerInfo();
                        break;

                    case "/api/party":
                        if (request.HttpMethod == "GET")
                            responseData = GetPartyInfo();
                        else if (request.HttpMethod == "POST")
                            responseData = await HandlePartyAction(request);
                        break;

                    case "/api/events":
                        responseData = GetEvents();
                        break;

                    case "/api/marketplace":
                        responseData = GetMarketplace();
                        break;

                    case "/api/friends":
                        responseData = GetFriends();
                        break;

                    case "/api/chat":
                        if (request.HttpMethod == "POST")
                            responseData = await HandleChatMessage(request);
                        break;

                    case "/api/command":
                        if (request.HttpMethod == "POST")
                            responseData = await HandleCommand(request);
                        break;

                    default:
                        SendErrorResponse(response, 404, "Not Found");
                        return;
                }

                if (responseData != null)
                {
                    string json = JsonSerializer.Serialize(responseData);
                    byte[] buffer = Encoding.UTF8.GetBytes(json);
                    response.ContentLength64 = buffer.Length;
                    await response.OutputStream.WriteAsync(buffer, 0, buffer.Length);
                }

                response.StatusCode = 200;
            }
            catch (Exception ex)
            {
                Logger.Log($"API error: {ex.Message}");
                SendErrorResponse(response, 500, ex.Message);
            }
            finally
            {
                response.Close();
            }
        }

        private async Task ServeStaticFile(HttpListenerContext context)
        {
            var request = context.Request;
            var response = context.Response;

            string filePath = request.Url.AbsolutePath;
            if (filePath == "/")
                filePath = "/index.html";

            filePath = Path.Combine(webRootPath, filePath.TrimStart('/'));

            if (!File.Exists(filePath))
            {
                SendErrorResponse(response, 404, "File Not Found");
                return;
            }

            try
            {
                byte[] buffer = File.ReadAllBytes(filePath);
                response.ContentType = GetContentType(filePath);
                response.ContentLength64 = buffer.Length;
                await response.OutputStream.WriteAsync(buffer, 0, buffer.Length);
                response.StatusCode = 200;
            }
            catch (Exception ex)
            {
                Logger.Log($"Static file error: {ex.Message}");
                SendErrorResponse(response, 500, "Error serving file");
            }
            finally
            {
                response.Close();
            }
        }

        private void SendErrorResponse(HttpListenerResponse response, int statusCode, string message)
        {
            response.StatusCode = statusCode;
            response.ContentType = "text/plain";
            byte[] buffer = Encoding.UTF8.GetBytes(message);
            response.ContentLength64 = buffer.Length;
            response.OutputStream.Write(buffer, 0, buffer.Length);
            response.Close();
        }

        private string GetContentType(string filePath)
        {
            string extension = Path.GetExtension(filePath).ToLower();
            return extension switch
            {
                ".html" => "text/html",
                ".css" => "text/css",
                ".js" => "application/javascript",
                ".json" => "application/json",
                ".png" => "image/png",
                ".jpg" => "image/jpeg",
                ".jpeg" => "image/jpeg",
                ".gif" => "image/gif",
                ".svg" => "image/svg+xml",
                _ => "application/octet-stream",
            };
        }

        // API Methods
        private object GetServerStatus()
        {
            if (client != null)
            {
                return new
                {
                    connected = client.IsLoggedIn,
                    username = client.CurrentUsername,
                    server = client.ServerAddress,
                    port = client.ServerPort,
                    webUIEnabled = client.IsWebInterfaceEnabled
                };
            }
            else if (server != null)
            {
                // Server status
                return new
                {
                    type = "server",
                    // Add server-specific status
                };
            }

            return new { error = "No client or server configured" };
        }

        private object GetPlayerInfo()
        {
            if (client == null) return null;

            var party = client.GetCurrentParty();
            var friends = client.GetFriends();

            return new
            {
                username = client.CurrentUsername,
                party = party,
                friendsOnline = friends.Where(f => f.IsOnline).Count(),
                level = 15, // This should come from player data
                health = 75,
                maxHealth = 100
            };
        }

        private object GetPartyInfo()
        {
            if (client == null) return null;

            var party = client.GetCurrentParty();
            var publicParties = client.GetPublicParties();

            return new
            {
                currentParty = party,
                publicParties = publicParties
            };
        }

        private object GetEvents()
        {
            if (client == null) return null;

            var activeEvents = client.GetActiveEvents();
            var myEvents = client.GetMyEvents();
            var inProgress = client.GetEventsInProgress();

            return new
            {
                active = activeEvents,
                myEvents = myEvents,
                inProgress = inProgress
            };
        }

        private object GetMarketplace()
        {
            if (client == null) return null;

            var listings = client.GetActiveMarketListings();
            var myListings = client.GetMyMarketListings();

            return new
            {
                listings = listings,
                myListings = myListings
            };
        }

        private object GetFriends()
        {
            if (client == null) return null;

            var friends = client.GetFriends();
            var requests = client.GetIncomingFriendRequests();

            return new
            {
                friends = friends,
                requests = requests
            };
        }

        private async Task<object> HandleChatMessage(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream))
            {
                string body = await reader.ReadToEndAsync();
                var data = JsonSerializer.Deserialize<Dictionary<string, object>>(body);

                if (data.TryGetValue("message", out var message))
                {
                    // Process the chat message
                    var chatMessage = new GameMessage
                    {
                        Type = MessageType.Chat,
                        PlayerId = client.CurrentUsername,
                        Data = new Dictionary<string, object> { { "message", message.ToString() } },
                        SessionId = client.AuthToken
                    };

                    client.SendMessageToServer(chatMessage);

                    return new { success = true };
                }
            }

            return new { success = false, error = "Invalid message" };
        }

        private async Task<object> HandleCommand(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream))
            {
                string body = await reader.ReadToEndAsync();
                var data = JsonSerializer.Deserialize<Dictionary<string, object>>(body);

                if (data.TryGetValue("command", out var command))
                {
                    // Process quick command
                    bool handled = client.ProcessQuickCommand(command.ToString());

                    return new { success = handled };
                }
            }

            return new { success = false, error = "Invalid command" };
        }

        private async Task<object> HandlePartyAction(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream))
            {
                string body = await reader.ReadToEndAsync();
                var data = JsonSerializer.Deserialize<Dictionary<string, object>>(body);

                if (data.TryGetValue("action", out var action))
                {
                    switch (action.ToString())
                    {
                        case "create":
                            if (data.TryGetValue("name", out var name))
                            {
                                var party = client.CreateParty(name.ToString());
                                return new { success = party != null, party = party };
                            }
                            break;

                        case "join":
                            if (data.TryGetValue("partyId", out var partyId))
                            {
                                bool joined = client.JoinParty(partyId.ToString());
                                return new { success = joined };
                            }
                            break;

                        case "leave":
                            bool left = client.LeaveParty();
                            return new { success = left };

                        case "invite":
                            if (data.TryGetValue("username", out var username))
                            {
                                bool invited = client.InviteToParty(username.ToString());
                                return new { success = invited };
                            }
                            break;
                    }
                }
            }

            return new { success = false, error = "Invalid action" };
        }

        // Timer callbacks
        private void UpdateStats(object state)
        {
            if (client == null || !client.IsLoggedIn) return;

            var stats = new
            {
                type = "stats_update",
                timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                playersOnline = 47, // This should come from server
                activeEvents = client.GetActiveEvents().Count,
                marketListings = client.GetActiveMarketListings().Count
            };

            _ = wsManager.BroadcastMessageAsync(new Dictionary<string, object>
            {
                { "type", stats.type },
                { "data", stats }
            });
        }

        private void UpdatePlayerList(object state)
        {
            if (client == null || !client.IsLoggedIn) return;

            var party = client.GetCurrentParty();
            if (party != null)
            {
                var update = new
                {
                    type = "party_update",
                    members = party.Members.Values.ToList()
                };

                _ = wsManager.BroadcastMessageAsync(new Dictionary<string, object>
                {
                    { "type", update.type },
                    { "data", update }
                });
            }
        }

        private void OnClientMessageReceived(object sender, GameMessage message)
        {
            // Forward relevant messages to WebSocket clients
            switch (message.Type)
            {
                case MessageType.Chat:
                    _ = wsManager.BroadcastMessageAsync(new Dictionary<string, object>
                    {
                        { "type", "chat" },
                        { "username", message.PlayerId },
                        { "message", message.Data.ContainsKey("message") ? message.Data["message"] : "" },
                        { "timestamp", message.Timestamp }
                    });
                    break;

                case MessageType.Position:
                    if (client.GetCurrentParty()?.Members.ContainsKey(message.PlayerId) == true)
                    {
                        _ = wsManager.BroadcastMessageAsync(new Dictionary<string, object>
                        {
                            { "type", "player_position" },
                            { "playerId", message.PlayerId },
                            { "position", message.Data }
                        });
                    }
                    break;
            }
        }

        private void CreateDefaultUI(string indexPath)
        {
            // Save the HTML content from the artifact as the default UI
            string htmlContent = @"<!-- Insert the full HTML content from the kenshi-ui artifact here -->";
            File.WriteAllText(indexPath, htmlContent);
        }
    }
}
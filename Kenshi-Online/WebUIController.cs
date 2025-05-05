using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Threading;
using System.Text.Json;
using System.Linq;
using System.Diagnostics;
using System.Security.Cryptography;

namespace KenshiMultiplayer
{
    public class WebUIController
    {
        private HttpListener listener;
        private Thread listenerThread;
        private EnhancedClient client;
        private EnhancedServer server;
        private string webRoot;
        private bool isRunning = false;
        private Dictionary<string, Func<HttpListenerRequest, Dictionary<string, object>>> apiEndpoints;
        private const string WebUIVersion = "1.0.1";
        private Dictionary<string, string> sessionTokens = new Dictionary<string, string>();

        public WebUIController(string webRootPath, int port = 8080)
        {
            webRoot = webRootPath;
            Directory.CreateDirectory(webRoot);

            // Initialize HTTP listener
            listener = new HttpListener();
            listener.Prefixes.Add($"http://localhost:{port}/");
            Logger.Log($"WebUI initializing on port {port}");

            // Setup API endpoints
            apiEndpoints = new Dictionary<string, Func<HttpListenerRequest, Dictionary<string, object>>>();
            InitializeApiEndpoints();

            // Extract embedded web files or create default ones
            ExtractEmbeddedWebUI();

            Logger.Log("WebUI extracted/created successfully");
        }

        public void SetClient(EnhancedClient clientInstance)
        {
            client = clientInstance;
            // Register client message handler for WebUI updates
            if (client != null)
            {
                client.MessageReceived += OnClientMessageReceived;
                Logger.Log("WebUI connected to client instance");
            }
        }

        public void SetServer(EnhancedServer serverInstance)
        {
            server = serverInstance;
            Logger.Log("WebUI connected to server instance");
        }

        private void InitializeApiEndpoints()
        {
            Logger.Log("Initializing WebUI API endpoints");

            // Auth endpoints
            apiEndpoints.Add("/api/login", HandleLogin);
            apiEndpoints.Add("/api/register", HandleRegister);
            apiEndpoints.Add("/api/logout", HandleLogout);
            apiEndpoints.Add("/api/status", HandleStatus);

            // Friends system endpoints
            apiEndpoints.Add("/api/friends/list", HandleFriendsList);
            apiEndpoints.Add("/api/friends/add", HandleFriendAdd);
            apiEndpoints.Add("/api/friends/remove", HandleFriendRemove);
            apiEndpoints.Add("/api/friends/accept", HandleFriendAccept);
            apiEndpoints.Add("/api/friends/decline", HandleFriendDecline);
            apiEndpoints.Add("/api/friends/block", HandleFriendBlock);

            // Marketplace endpoints
            apiEndpoints.Add("/api/marketplace/listings", HandleMarketplaceListings);
            apiEndpoints.Add("/api/marketplace/create", HandleMarketplaceCreate);
            apiEndpoints.Add("/api/marketplace/purchase", HandleMarketplacePurchase);
            apiEndpoints.Add("/api/marketplace/cancel", HandleMarketplaceCancel);
            apiEndpoints.Add("/api/marketplace/search", HandleMarketplaceSearch);

            // Trading endpoints
            apiEndpoints.Add("/api/trade/initiate", HandleTradeInitiate);
            apiEndpoints.Add("/api/trade/update", HandleTradeUpdate);
            apiEndpoints.Add("/api/trade/confirm", HandleTradeConfirm);
            apiEndpoints.Add("/api/trade/cancel", HandleTradeCancel);
            apiEndpoints.Add("/api/trade/items", HandleTradeItems);

            // Player endpoints
            apiEndpoints.Add("/api/player/inventory", HandlePlayerInventory);
            apiEndpoints.Add("/api/player/status", HandlePlayerStatus);
            apiEndpoints.Add("/api/player/position", HandlePlayerPosition);

            // Game files and mods endpoints
            apiEndpoints.Add("/api/game/status", HandleGameStatus);
            apiEndpoints.Add("/api/game/mods", HandleGameMods);
            apiEndpoints.Add("/api/game/files", HandleGameFiles);

            Logger.Log($"WebUI initialized {apiEndpoints.Count} API endpoints");
        }

        public void Start()
        {
            if (isRunning) return;

            try
            {
                listener.Start();
                isRunning = true;

                listenerThread = new Thread(ListenerLoop);
                listenerThread.IsBackground = true;
                listenerThread.Start();

                Logger.Log("WebUI started successfully");
                Console.WriteLine($"WebUI is running at http://localhost:{listener.Prefixes.First().Split(':')[2].TrimEnd('/')}");
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to start WebUI: {ex.Message}");
                Console.WriteLine($"Failed to start WebUI: {ex.Message}");
            }
        }

        public void Stop()
        {
            if (!isRunning) return;

            try
            {
                isRunning = false;
                listener.Stop();

                if (listenerThread != null && listenerThread.IsAlive)
                {
                    listenerThread.Join(1000);
                }

                Logger.Log("WebUI stopped");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error stopping WebUI: {ex.Message}");
            }
        }

        private void ListenerLoop()
        {
            while (isRunning)
            {
                try
                {
                    var context = listener.GetContext();
                    ThreadPool.QueueUserWorkItem((_) => ProcessRequest(context));
                }
                catch (Exception ex)
                {
                    if (isRunning)
                    {
                        Logger.Log($"Error in WebUI listener: {ex.Message}");
                    }
                }
            }
        }

        private void ProcessRequest(HttpListenerContext context)
        {
            string path = context.Request.Url.AbsolutePath;
            string query = context.Request.Url.Query;

            try
            {
                // Log requests (excluding frequent polling requests)
                if (!path.Contains("/api/status") && !path.Contains("/api/game/status"))
                {
                    Logger.Log($"WebUI request: {context.Request.HttpMethod} {path}{query}");
                }

                // Handle API requests
                if (path.StartsWith("/api/"))
                {
                    // Check authentication for protected endpoints
                    if (RequiresAuthentication(path) && !IsAuthenticated(context.Request))
                    {
                        SendJsonResponse(context, new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Authentication required" }
                        }, 401);
                        return;
                    }

                    HandleApiRequest(context);
                    return;
                }

                // Handle WebSocket connections if supported
                if (context.Request.IsWebSocketRequest)
                {
                    // Implement WebSocket handler here if needed
                    context.Response.StatusCode = 501; // Not Implemented
                    context.Response.Close();
                    return;
                }

                // Serve static files
                string filePath = Path.Combine(webRoot, path.TrimStart('/'));

                // Default to index.html for root path
                if (path == "/" || string.IsNullOrEmpty(path))
                {
                    filePath = Path.Combine(webRoot, "index.html");
                }

                if (File.Exists(filePath))
                {
                    ServeFile(context, filePath);
                }
                else
                {
                    // Try serving index.html for client-side routing
                    if (!path.Contains("."))
                    {
                        string indexPath = Path.Combine(webRoot, "index.html");
                        if (File.Exists(indexPath))
                        {
                            ServeFile(context, indexPath);
                            return;
                        }
                    }

                    // 404 if file not found
                    context.Response.StatusCode = 404;
                    context.Response.Close();
                    Logger.Log($"WebUI 404: {path}");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error processing request for {path}: {ex.Message}");
                try
                {
                    context.Response.StatusCode = 500;
                    context.Response.Close();
                }
                catch { /* Ignore if response already closed */ }
            }
        }

        private bool RequiresAuthentication(string path)
        {
            // List of endpoints that don't require authentication
            string[] publicEndpoints = new string[]
            {
                "/api/login",
                "/api/register",
                "/api/status",
                "/api/game/status"
            };

            return !publicEndpoints.Contains(path);
        }

        private bool IsAuthenticated(HttpListenerRequest request)
        {
            if (client == null) return false;

            // Check for auth token in headers
            string authToken = request.Headers["Authorization"];
            if (string.IsNullOrEmpty(authToken)) return false;

            // Simple validation - in a real app you'd validate against session store
            if (authToken.StartsWith("Bearer "))
            {
                authToken = authToken.Substring(7);
                return client.IsLoggedIn && client.AuthToken == authToken;
            }

            return false;
        }

        private void ServeFile(HttpListenerContext context, string filePath)
        {
            string contentType = GetContentType(filePath);
            byte[] buffer;

            try
            {
                buffer = File.ReadAllBytes(filePath);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error reading file {filePath}: {ex.Message}");
                context.Response.StatusCode = 500;
                context.Response.Close();
                return;
            }

            context.Response.ContentType = contentType;
            context.Response.ContentLength64 = buffer.Length;
            context.Response.StatusCode = 200;

            try
            {
                context.Response.OutputStream.Write(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error writing response: {ex.Message}");
            }
            finally
            {
                context.Response.Close();
            }
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
                ".jpg" or ".jpeg" => "image/jpeg",
                ".gif" => "image/gif",
                ".svg" => "image/svg+xml",
                ".ico" => "image/x-icon",
                ".woff" => "font/woff",
                ".woff2" => "font/woff2",
                ".ttf" => "font/ttf",
                ".eot" => "application/vnd.ms-fontobject",
                _ => "application/octet-stream"
            };
        }

        private void HandleApiRequest(HttpListenerContext context)
        {
            string path = context.Request.Url.AbsolutePath;

            if (apiEndpoints.ContainsKey(path))
            {
                try
                {
                    var result = apiEndpoints[path](context.Request);
                    SendJsonResponse(context, result);
                }
                catch (Exception ex)
                {
                    SendJsonResponse(context, new Dictionary<string, object>
                    {
                        { "success", false },
                        { "error", ex.Message }
                    }, 500);
                    Logger.Log($"API error for {path}: {ex.Message}");
                }
            }
            else
            {
                context.Response.StatusCode = 404;
                SendJsonResponse(context, new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", "API endpoint not found" }
                }, 404);
                Logger.Log($"API endpoint not found: {path}");
            }
        }

        private void SendJsonResponse(HttpListenerContext context, Dictionary<string, object> data, int statusCode = 200)
        {
            context.Response.ContentType = "application/json";
            context.Response.StatusCode = statusCode;

            // Add CORS headers for development
            context.Response.AddHeader("Access-Control-Allow-Origin", "*");
            context.Response.AddHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            context.Response.AddHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

            if (context.Request.HttpMethod == "OPTIONS")
            {
                // Handle preflight request
                context.Response.Close();
                return;
            }

            try
            {
                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions
                {
                    WriteIndented = true,
                    PropertyNamingPolicy = JsonNamingPolicy.CamelCase
                });

                byte[] buffer = Convert.FromBase64String(json);
                context.Response.ContentLength64 = buffer.Length;
                context.Response.OutputStream.Write(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error sending JSON response: {ex.Message}");
            }
            finally
            {
                context.Response.Close();
            }
        }

        private void OnClientMessageReceived(object sender, GameMessage message)
        {
            // Forward relevant messages to WebUI via WebSocket if implemented
            // For now, just log them
            if (message.Type != MessageType.Position &&
                message.Type != MessageType.Ping &&
                message.Type != MessageType.Pong)
            {
                Logger.Log($"WebUI received message: {message.Type}");
            }
        }

        // API Handler Methods
        private Dictionary<string, object> HandleLogin(HttpListenerRequest request)
        {
            if (client == null)
                return new Dictionary<string, object> { { "success", false }, { "error", "Client not initialized" } };

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var loginData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (loginData == null || !loginData.ContainsKey("username") || !loginData.ContainsKey("password"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Invalid login data. Username and password required." }
                        };
                    }

                    // Call client login method
                    bool success = client.Login(
                        client.ServerAddress ?? request.Url.Host,
                        client.ServerPort > 0 ? client.ServerPort : 5555,
                        loginData["username"],
                        loginData["password"]
                    );

                    if (success)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", true },
                            { "username", loginData["username"] },
                            { "token", client.AuthToken },
                            { "message", "Login successful" }
                        };
                    }
                    else
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Invalid username or password" }
                        };
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Login error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Login process error: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleRegister(HttpListenerRequest request)
        {
            if (client == null)
                return new Dictionary<string, object> { { "success", false }, { "error", "Client not initialized" } };

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var regData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (regData == null ||
                        !regData.ContainsKey("username") ||
                        !regData.ContainsKey("password") ||
                        !regData.ContainsKey("email"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Invalid registration data. Username, password, and email required." }
                        };
                    }

                    // Validate data
                    if (string.IsNullOrWhiteSpace(regData["username"]) || regData["username"].Length < 3)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username must be at least 3 characters" }
                        };
                    }

                    if (string.IsNullOrWhiteSpace(regData["password"]) || regData["password"].Length < 8)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Password must be at least 8 characters" }
                        };
                    }

                    if (string.IsNullOrWhiteSpace(regData["email"]) || !IsValidEmail(regData["email"]))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Invalid email address format" }
                        };
                    }

                    // Call client register method
                    bool success = client.Register(
                        client.ServerAddress ?? request.Url.Host,
                        client.ServerPort > 0 ? client.ServerPort : 5555,
                        regData["username"],
                        regData["password"],
                        regData["email"]
                    );

                    if (success)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", true },
                            { "message", "Registration successful! You can now log in." }
                        };
                    }
                    else
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Registration failed. Username or email might already be in use." }
                        };
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Registration error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Registration process error: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleLogout(HttpListenerRequest request)
        {
            if (client == null)
                return new Dictionary<string, object> { { "success", false }, { "error", "Client not initialized" } };

            try
            {
                string authHeader = request.Headers["Authorization"];
                if (!string.IsNullOrEmpty(authHeader) && authHeader.StartsWith("Bearer "))
                {
                    string token = authHeader.Substring(7);

                    // Implement logout logic - may need to add a Logout method to EnhancedClient
                    client.Disconnect();

                    return new Dictionary<string, object>
                    {
                        { "success", true },
                        { "message", "Logged out successfully" }
                    };
                }

                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", "Not logged in" }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Logout error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Logout error: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleStatus(HttpListenerRequest request)
        {
            bool isLoggedIn = client != null && client.IsLoggedIn;
            return new Dictionary<string, object>
            {
                { "success", true },
                { "loggedIn", isLoggedIn },
                { "username", isLoggedIn ? client.CurrentUsername : null },
                { "serverAddress", client?.ServerAddress ?? "Not connected" },
                { "serverPort", client?.ServerPort ?? 0 },
                { "webUiVersion", WebUIVersion }
            };
        }

        private Dictionary<string, object> HandleFriendsList(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                var friends = client.GetFriends();
                var incomingRequests = client.GetIncomingFriendRequests();
                var outgoingRequests = client.GetOutgoingFriendRequests();
                var blockedUsers = client.GetBlockedUsers();

                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "friends", friends },
                    { "incomingRequests", incomingRequests },
                    { "outgoingRequests", outgoingRequests },
                    { "blockedUsers", blockedUsers }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Friends list error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting friends list: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendAdd(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (friendData == null || !friendData.ContainsKey("username"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username required" }
                        };
                    }

                    bool success = client.SendFriendRequest(friendData["username"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Friend request sent" : "Failed to send friend request" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Add friend error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error adding friend: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendRemove(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (friendData == null || !friendData.ContainsKey("username"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username required" }
                        };
                    }

                    bool success = client.RemoveFriend(friendData["username"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Friend removed" : "Failed to remove friend" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Remove friend error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error removing friend: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendAccept(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (friendData == null || !friendData.ContainsKey("username"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username required" }
                        };
                    }

                    bool success = client.AcceptFriendRequest(friendData["username"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Friend request accepted" : "Failed to accept friend request" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Accept friend error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error accepting friend request: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendDecline(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (friendData == null || !friendData.ContainsKey("username"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username required" }
                        };
                    }

                    bool success = client.DeclineFriendRequest(friendData["username"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Friend request declined" : "Failed to decline friend request" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Decline friend error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error declining friend request: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendBlock(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (friendData == null || !friendData.ContainsKey("username"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Username required" }
                        };
                    }

                    bool success = client.BlockUser(friendData["username"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "User blocked" : "Failed to block user" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Block user error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error blocking user: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplaceListings(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                var activeListings = client.GetActiveMarketListings();
                var myListings = client.GetMyMarketListings();
                var myPurchases = client.GetMyPurchases();

                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "activeListings", activeListings },
                    { "myListings", myListings },
                    { "myPurchases", myPurchases }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Marketplace listings error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting marketplace listings: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplaceCreate(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var listingData = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (listingData == null ||
                        !listingData.ContainsKey("itemId") ||
                        !listingData.ContainsKey("itemName") ||
                        !listingData.ContainsKey("quantity") ||
                        !listingData.ContainsKey("price"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Item ID, name, quantity, and price are required" }
                        };
                    }

                    string itemId = listingData["itemId"].ToString();
                    string itemName = listingData["itemName"].ToString();

                    if (!int.TryParse(listingData["quantity"].ToString(), out int quantity) || quantity <= 0)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Quantity must be a positive number" }
                        };
                    }

                    if (!int.TryParse(listingData["price"].ToString(), out int price) || price <= 0)
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Price must be a positive number" }
                        };
                    }

                    float condition = 1.0f;
                    if (listingData.ContainsKey("condition") &&
                        float.TryParse(listingData["condition"].ToString(), out float parsedCondition))
                    {
                        condition = Math.Clamp(parsedCondition, 0.0f, 1.0f);
                    }

                    bool success = client.CreateMarketListing(itemId, itemName, quantity, price, condition);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Listing created successfully" : "Failed to create listing" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Create marketplace listing error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error creating marketplace listing: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplacePurchase(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var purchaseData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (purchaseData == null || !purchaseData.ContainsKey("listingId"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Listing ID required" }
                        };
                    }

                    bool success = client.PurchaseMarketListing(purchaseData["listingId"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Item purchased successfully" : "Failed to purchase item" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Purchase marketplace listing error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error purchasing marketplace listing: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplaceCancel(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var cancelData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (cancelData == null || !cancelData.ContainsKey("listingId"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Listing ID required" }
                        };
                    }

                    bool success = client.CancelMarketListing(cancelData["listingId"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Listing cancelled successfully" : "Failed to cancel listing" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Cancel marketplace listing error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error cancelling marketplace listing: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplaceSearch(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                // Get search term from query string
                string searchTerm = "";
                if (request.Url.Query.Length > 1)
                {
                    var queryParams = request.Url.Query.TrimStart('?').Split('&');
                    foreach (var param in queryParams)
                    {
                        var parts = param.Split('=');
                        if (parts.Length == 2 && parts[0] == "q")
                        {
                            searchTerm = WebUtility.UrlDecode(parts[1]);
                            break;
                        }
                    }
                }

                var searchResults = client.SearchMarketListings(searchTerm);

                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "searchTerm", searchTerm },
                    { "results", searchResults }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Search marketplace listings error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error searching marketplace listings: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleTradeInitiate(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var tradeData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (tradeData == null || !tradeData.ContainsKey("targetUsername"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Target username required" }
                        };
                    }

                    bool success = client.InitiateTrade(tradeData["targetUsername"]);

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Trade initiated successfully" : "Failed to initiate trade" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Initiate trade error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error initiating trade: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleTradeUpdate(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
                {
                    string jsonStr = reader.ReadToEnd();
                    var tradeData = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonStr,
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

                    if (tradeData == null ||
                        !tradeData.ContainsKey("action") ||
                        !tradeData.ContainsKey("itemId"))
                    {
                        return new Dictionary<string, object>
                        {
                            { "success", false },
                            { "error", "Action and itemId required" }
                        };
                    }

                    string action = tradeData["action"].ToString();
                    string itemId = tradeData["itemId"].ToString();
                    bool success = false;

                    switch (action.ToLower())
                    {
                        case "add":
                            if (!tradeData.ContainsKey("itemName") ||
                                !tradeData.ContainsKey("quantity"))
                            {
                                return new Dictionary<string, object>
                                {
                                    { "success", false },
                                    { "error", "Item name and quantity required for add action" }
                                };
                            }

                            string itemName = tradeData["itemName"].ToString();

                            if (!int.TryParse(tradeData["quantity"].ToString(), out int quantity) || quantity <= 0)
                            {
                                return new Dictionary<string, object>
                                {
                                    { "success", false },
                                    { "error", "Quantity must be a positive number" }
                                };
                            }

                            float condition = 1.0f;
                            if (tradeData.ContainsKey("condition") &&
                                float.TryParse(tradeData["condition"].ToString(), out float parsedCondition))
                            {
                                condition = Math.Clamp(parsedCondition, 0.0f, 1.0f);
                            }

                            success = client.AddItemToTrade(itemId, itemName, quantity, condition);
                            break;

                        case "remove":
                            success = client.RemoveItemFromTrade(itemId);
                            break;

                        case "update":
                            if (!tradeData.ContainsKey("quantity"))
                            {
                                return new Dictionary<string, object>
                                {
                                    { "success", false },
                                    { "error", "Quantity required for update action" }
                                };
                            }

                            if (!int.TryParse(tradeData["quantity"].ToString(), out int updateQuantity) || updateQuantity <= 0)
                            {
                                return new Dictionary<string, object>
                                {
                                    { "success", false },
                                    { "error", "Quantity must be a positive number" }
                                };
                            }

                            success = client.UpdateItemQuantity(itemId, updateQuantity);
                            break;

                        default:
                            return new Dictionary<string, object>
                            {
                                { "success", false },
                                { "error", $"Unknown action: {action}" }
                            };
                    }

                    return new Dictionary<string, object>
                    {
                        { "success", success },
                        { "message", success ? "Trade updated successfully" : "Failed to update trade" }
                    };
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Update trade error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error updating trade: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleTradeConfirm(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                bool success = client.ConfirmTradeOffer();

                return new Dictionary<string, object>
                {
                    { "success", success },
                    { "message", success ? "Trade offer confirmed" : "Failed to confirm trade offer" }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Confirm trade error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error confirming trade: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleTradeCancel(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                bool success = client.CancelTrade();

                return new Dictionary<string, object>
                {
                    { "success", success },
                    { "message", success ? "Trade cancelled" : "Failed to cancel trade" }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Cancel trade error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error cancelling trade: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleTradeItems(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                var currentTrade = client.GetCurrentTrade();

                if (currentTrade == null)
                {
                    return new Dictionary<string, object>
                    {
                        { "success", false },
                        { "error", "No active trade" }
                    };
                }

                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "trade", currentTrade }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Get trade items error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting trade items: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandlePlayerInventory(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                // This would need to be implemented in the EnhancedClient class
                // For now, return dummy data
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "items", new List<Dictionary<string, object>>
                        {
                            new Dictionary<string, object>
                            {
                                { "itemId", "item1" },
                                { "itemName", "Katana" },
                                { "quantity", 1 },
                                { "condition", 0.85 }
                            },
                            new Dictionary<string, object>
                            {
                                { "itemId", "item2" },
                                { "itemName", "Dried Meat" },
                                { "quantity", 5 },
                                { "condition", 1.0 }
                            },
                            new Dictionary<string, object>
                            {
                                { "itemId", "item3" },
                                { "itemName", "Iron Plates" },
                                { "quantity", 15 },
                                { "condition", 1.0 }
                            }
                        }
                    }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Player inventory error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting player inventory: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandlePlayerStatus(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                // This would need to be implemented in the EnhancedClient class
                // For now, return dummy data
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "player", new Dictionary<string, object>
                        {
                            { "displayName", client.CurrentUsername },
                            { "health", 85 },
                            { "maxHealth", 100 },
                            { "level", 12 },
                            { "hunger", 80 },
                            { "thirst", 75 }
                        }
                    }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Player status error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting player status: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandlePlayerPosition(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                // This would require adding position tracking to the EnhancedClient
                // For now, return dummy data
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "position", new Dictionary<string, object>
                        {
                            { "x", 123.45 },
                            { "y", 67.89 },
                            { "z", 0 }
                        }
                    }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Player position error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting player position: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleGameStatus(HttpListenerRequest request)
        {
            try
            {
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "online", client != null },
                    { "loggedIn", client != null && client.IsLoggedIn },
                    { "serverAddress", client?.ServerAddress ?? "Not connected" },
                    { "serverPort", client?.ServerPort ?? 0 },
                    { "webInterfaceEnabled", client?.IsWebInterfaceEnabled ?? false },
                    { "webUiVersion", WebUIVersion }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Game status error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting game status: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleGameMods(HttpListenerRequest request)
        {
            try
            {
                // Implement mod detection - this would need access to the Kenshi game directory
                // For now, return dummy data
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "mods", new List<Dictionary<string, object>>
                        {
                            new Dictionary<string, object>
                            {
                                { "name", "Reactive World" },
                                { "enabled", true },
                                { "path", "mods/reactive_world" }
                            },
                            new Dictionary<string, object>
                            {
                                { "name", "Kaizo" },
                                { "enabled", true },
                                { "path", "mods/kaizo" }
                            },
                            new Dictionary<string, object>
                            {
                                { "name", "Dark UI" },
                                { "enabled", true },
                                { "path", "mods/dark_ui" }
                            }
                        }
                    }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Game mods error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting game mods: {ex.Message}" }
                };
            }
        }

        private Dictionary<string, object> HandleGameFiles(HttpListenerRequest request)
        {
            if (client == null || !client.IsLoggedIn)
            {
                return new Dictionary<string, object> { { "success", false }, { "error", "Not logged in" } };
            }

            try
            {
                string directory = "";
                if (request.Url.Query.Length > 1)
                {
                    var queryParams = request.Url.Query.TrimStart('?').Split('&');
                    foreach (var param in queryParams)
                    {
                        var parts = param.Split('=');
                        if (parts.Length == 2 && parts[0] == "dir")
                        {
                            directory = WebUtility.UrlDecode(parts[1]);
                            break;
                        }
                    }
                }

                var files = client.RequestFileList(directory);

                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "directory", directory },
                    { "files", files }
                };
            }
            catch (Exception ex)
            {
                Logger.Log($"Game files error: {ex.Message}");
                return new Dictionary<string, object>
                {
                    { "success", false },
                    { "error", $"Error getting game files: {ex.Message}" }
                };
            }
        }

        private void ExtractEmbeddedWebUI()
        {
            // Create basic files if they don't exist
            string indexPath = Path.Combine(webRoot, "index.html");
            string stylesPath = Path.Combine(webRoot, "styles.css");
            string scriptsPath = Path.Combine(webRoot, "scripts");
            string scriptsMainPath = Path.Combine(scriptsPath, "main.js");

            // Create directories if they don't exist
            Directory.CreateDirectory(webRoot);
            Directory.CreateDirectory(scriptsPath);

            // Create index.html if it doesn't exist
            if (!File.Exists(indexPath))
            {
                File.WriteAllText(indexPath, CreateDefaultHtml());
            }

            // Create styles.css if it doesn't exist
            if (!File.Exists(stylesPath))
            {
                File.WriteAllText(stylesPath, CreateDefaultCss());
            }

            // Create main.js if it doesn't exist
            if (!File.Exists(scriptsMainPath))
            {
                File.WriteAllText(scriptsMainPath, CreateDefaultJs());
            }
        }

        private string CreateDefaultHtml()
        {
            // Create a more modern default HTML file
            return @"<!DOCTYPE html>
<html lang=""en"">
<head>
    <meta charset=""UTF-8"">
    <meta name=""viewport"" content=""width=device-width, initial-scale=1.0"">
    <title>Kenshi Online - Web Interface</title>
    <link rel=""stylesheet"" href=""/styles.css"">
</head>
<body>
    <header>
        <div class=""container"">
            <h1>Kenshi Online</h1>
            <div id=""user-status"">
                <span id=""status-message"">Connecting...</span>
                <button id=""login-btn"" style=""display: none;"">Login</button>
                <button id=""logout-btn"" style=""display: none;"">Logout</button>
            </div>
        </div>
    </header>

    <main>
        <div class=""container"">
            <!-- Login Form -->
            <div id=""login-form"" class=""card"" style=""display: none;"">
                <h2>Login</h2>
                <form id=""form-login"">
                    <div class=""form-group"">
                        <label for=""username"">Username</label>
                        <input type=""text"" id=""username"" name=""username"" required>
                    </div>
                    <div class=""form-group"">
                        <label for=""password"">Password</label>
                        <input type=""password"" id=""password"" name=""password"" required>
                    </div>
                    <div class=""form-actions"">
                        <button type=""submit"">Login</button>
                        <button type=""button"" id=""register-link"">Register</button>
                    </div>
                </form>
            </div>

            <!-- Registration Form -->
            <div id=""register-form"" class=""card"" style=""display: none;"">
                <h2>Register</h2>
                <form id=""form-register"">
                    <div class=""form-group"">
                        <label for=""reg-username"">Username</label>
                        <input type=""text"" id=""reg-username"" name=""username"" required>
                        <small>Must be at least 3 characters long</small>
                    </div>
                    <div class=""form-group"">
                        <label for=""reg-password"">Password</label>
                        <input type=""password"" id=""reg-password"" name=""password"" required>
                        <small>Must be at least 8 characters long</small>
                    </div>
                    <div class=""form-group"">
                        <label for=""reg-email"">Email</label>
                        <input type=""email"" id=""reg-email"" name=""email"" required>
                    </div>
                    <div class=""form-actions"">
                        <button type=""submit"">Register</button>
                        <button type=""button"" id=""login-link"">Back to Login</button>
                    </div>
                </form>
            </div>

            <!-- Dashboard -->
            <div id=""dashboard"" style=""display: none;"">
                <div class=""tabs"">
                    <button class=""tab-btn active"" data-tab=""status"">Status</button>
                    <button class=""tab-btn"" data-tab=""friends"">Friends</button>
                    <button class=""tab-btn"" data-tab=""marketplace"">Marketplace</button>
                    <button class=""tab-btn"" data-tab=""trade"">Trade</button>
                    <button class=""tab-btn"" data-tab=""inventory"">Inventory</button>
                    <button class=""tab-btn"" data-tab=""mods"">Mods</button>
                </div>

                <!-- Status Tab -->
                <div id=""tab-status"" class=""tab-content active"">
                    <div class=""card"">
                        <h2>Player Status</h2>
                        <div id=""player-status"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>Server Status</h2>
                        <div id=""server-status"">Loading...</div>
                    </div>
                </div>

                <!-- Friends Tab -->
                <div id=""tab-friends"" class=""tab-content"">
                    <div class=""card"">
                        <h2>Friends List</h2>
                        <div id=""friends-list"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>Friend Requests</h2>
                        <div id=""friend-requests"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>Add Friend</h2>
                        <form id=""form-add-friend"">
                            <div class=""form-group"">
                                <label for=""friend-username"">Username</label>
                                <input type=""text"" id=""friend-username"" name=""username"" required>
                            </div>
                            <div class=""form-actions"">
                                <button type=""submit"">Send Request</button>
                            </div>
                        </form>
                    </div>
                </div>

                <!-- Marketplace Tab -->
                <div id=""tab-marketplace"" class=""tab-content"">
                    <div class=""card"">
                        <h2>Marketplace Listings</h2>
                        <div id=""marketplace-listings"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>My Listings</h2>
                        <div id=""my-listings"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>Create Listing</h2>
                        <form id=""form-create-listing"">
                            <div class=""form-group"">
                                <label for=""listing-item-id"">Item ID</label>
                                <select id=""listing-item-id"" name=""itemId"" required></select>
                            </div>
                            <div class=""form-group"">
                                <label for=""listing-item-name"">Item Name</label>
                                <input type=""text"" id=""listing-item-name"" name=""itemName"" required>
                            </div>
                            <div class=""form-group"">
                                <label for=""listing-quantity"">Quantity</label>
                                <input type=""number"" id=""listing-quantity"" name=""quantity"" min=""1"" value=""1"" required>
                            </div>
                            <div class=""form-group"">
                                <label for=""listing-price"">Price (per item)</label>
                                <input type=""number"" id=""listing-price"" name=""price"" min=""1"" required>
                            </div>
                            <div class=""form-group"">
                                <label for=""listing-condition"">Condition</label>
                                <input type=""range"" id=""listing-condition"" name=""condition"" min=""0"" max=""1"" step=""0.01"" value=""1"">
                                <span id=""condition-value"">100%</span>
                            </div>
                            <div class=""form-actions"">
                                <button type=""submit"">Create Listing</button>
                            </div>
                        </form>
                    </div>
                </div>

                <!-- Trade Tab -->
                <div id=""tab-trade"" class=""tab-content"">
                    <div class=""card"">
                        <h2>Current Trade</h2>
                        <div id=""current-trade"">No active trade</div>
                    </div>
                    <div class=""card"">
                        <h2>Trade Requests</h2>
                        <div id=""trade-requests"">Loading...</div>
                    </div>
                    <div class=""card"">
                        <h2>Initiate Trade</h2>
                        <form id=""form-initiate-trade"">
                            <div class=""form-group"">
                                <label for=""trade-username"">Username</label>
                                <select id=""trade-username"" name=""targetUsername"" required></select>
                            </div>
                            <div class=""form-actions"">
                                <button type=""submit"">Initiate Trade</button>
                            </div>
                        </form>
                    </div>
                </div>

                <!-- Inventory Tab -->
                <div id=""tab-inventory"" class=""tab-content"">
                    <div class=""card"">
                        <h2>Inventory</h2>
                        <div id=""inventory-items"">Loading...</div>
                    </div>
                </div>

                <!-- Mods Tab -->
                <div id=""tab-mods"" class=""tab-content"">
                    <div class=""card"">
                        <h2>Installed Mods</h2>
                        <div id=""installed-mods"">Loading...</div>
                    </div>
                </div>
            </div>
        </div>
    </main>

    <footer>
        <div class=""container"">
            <p>Kenshi Online Web Interface v1.0.1</p>
        </div>
    </footer>

    <!-- Notification System -->
    <div id=""notification-container""></div>

    <script src=""/scripts/main.js""></script>
</body>
</html>";
        }

        private string CreateDefaultCss()
        {
            // Create modern CSS styles
            return @"/* Base Styles */
:root {
    --primary-color: #8a4e3c;
    --primary-dark: #6a3e30;
    --secondary-color: #3a3a3a;
    --accent-color: #d2a979;
    --text-color: #e0e0e0;
    --background-color: #1a1a1a;
    --card-background: #2a2a2a;
    --border-color: #3a3a3a;
    --success-color: #4CAF50;
    --error-color: #F44336;
    --warning-color: #FFC107;
    --info-color: #2196F3;
}

* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background-color: var(--background-color);
    color: var(--text-color);
    line-height: 1.6;
}

.container {
    width: 90%;
    max-width: 1200px;
    margin: 0 auto;
    padding: 15px;
}

/* Header */
header {
    background-color: var(--secondary-color);
    border-bottom: 1px solid var(--border-color);
    padding: 10px 0;
}

header .container {
    display: flex;
    justify-content: space-between;
    align-items: center;
}

header h1 {
    color: var(--accent-color);
    font-size: 1.8rem;
}

#user-status {
    display: flex;
    align-items: center;
    gap: 15px;
}

/* Card Component */
.card {
    background-color: var(--card-background);
    border: 1px solid var(--border-color);
    border-radius: 4px;
    padding: 20px;
    margin-bottom: 20px;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.card h2 {
    color: var(--accent-color);
    margin-bottom: 15px;
    font-size: 1.4rem;
    border-bottom: 1px solid var(--border-color);
    padding-bottom: 10px;
}

/* Forms */
.form-group {
    margin-bottom: 15px;
}

.form-group label {
    display: block;
    margin-bottom: 5px;
    font-weight: 500;
}

.form-group input, 
.form-group select {
    width: 100%;
    padding: 10px;
    border: 1px solid var(--border-color);
    border-radius: 4px;
    background-color: var(--secondary-color);
    color: var(--text-color);
    font-size: 1rem;
}

.form-group small {
    display: block;
    color: var(--accent-color);
    font-size: 0.8rem;
    margin-top: 5px;
}

.form-actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
    margin-top: 20px;
}

/* Buttons */
button {
    background-color: var(--primary-color);
    color: var(--text-color);
    border: none;
    border-radius: 4px;
    padding: 8px 16px;
    cursor: pointer;
    font-size: 0.9rem;
    transition: background-color 0.2s ease;
}

button:hover {
    background-color: var(--primary-dark);
}

button.secondary {
    background-color: var(--secondary-color);
}

button.secondary:hover {
    background-color: #4a4a4a;
}

/* Tabs */
.tabs {
    display: flex;
    border-bottom: 1px solid var(--border-color);
    margin-bottom: 20px;
    overflow-x: auto;
}

.tab-btn {
    padding: 10px 20px;
    background: none;
    border: none;
    border-bottom: 3px solid transparent;
    color: var(--text-color);
    cursor: pointer;
    font-size: 1rem;
    white-space: nowrap;
}

.tab-btn:hover {
    background-color: rgba(255, 255, 255, 0.05);
}

.tab-btn.active {
    border-bottom-color: var(--primary-color);
    color: var(--accent-color);
}

.tab-content {
    display: none;
}

.tab-content.active {
    display: block;
}

/* Lists */
.list-item {
    padding: 12px;
    border-bottom: 1px solid var(--border-color);
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.list-item:last-child {
    border-bottom: none;
}

.list-item .actions {
    display: flex;
    gap: 5px;
}

.list-item .status {
    font-size: 0.8rem;
    padding: 3px 8px;
    border-radius: 12px;
    background-color: var(--secondary-color);
}

.list-item .status.online {
    background-color: var(--success-color);
    color: white;
}

.list-item .status.offline {
    background-color: var(--error-color);
    color: white;
}

/* Notification System */
#notification-container {
    position: fixed;
    top: 20px;
    right: 20px;
    z-index: 1000;
}

.notification {
    background-color: var(--card-background);
    border-left: 4px solid var(--primary-color);
    border-radius: 4px;
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
    padding: 12px 20px;
    margin-bottom: 10px;
    width: 300px;
    animation: fadeIn 0.3s ease;
}

.notification.success {
    border-left-color: var(--success-color);
}

.notification.error {
    border-left-color: var(--error-color);
}

.notification.warning {
    border-left-color: var(--warning-color);
}

.notification.info {
    border-left-color: var(--info-color);
}

.notification h3 {
    margin-bottom: 5px;
    font-size: 1rem;
}

.notification p {
    font-size: 0.9rem;
}

@keyframes fadeIn {
    from { opacity: 0; transform: translateY(-10px); }
    to { opacity: 1; transform: translateY(0); }
}

@keyframes fadeOut {
    from { opacity: 1; transform: translateY(0); }
    to { opacity: 0; transform: translateY(-10px); }
}

/* Footer */
footer {
    text-align: center;
    padding: 20px 0;
    background-color: var(--secondary-color);
    border-top: 1px solid var(--border-color);
    margin-top: 40px;
}

footer p {
    font-size: 0.9rem;
    color: #888;
}

/* Responsive adjustments */
@media (max-width: 768px) {
    .container {
        width: 95%;
    }
    
    header .container {
        flex-direction: column;
        gap: 10px;
    }
    
    .tabs {
        flex-wrap: nowrap;
        overflow-x: auto;
    }
    
    .form-actions {
        flex-direction: column;
    }
    
    .form-actions button {
        width: 100%;
    }
}";
        }

        private string CreateDefaultJs()
        {
            // Create JavaScript for the interface
            return @"// Main JavaScript for Kenshi Online Web Interface
document.addEventListener('DOMContentLoaded', function() {
    // State management
    const state = {
        isLoggedIn: false,
        username: null,
        token: null,
        activeTab: 'status',
        inventoryItems: [],
        friendsList: [],
        marketplaceListings: [],
        currentTrade: null
    };

    // DOM elements
    const elements = {
        userStatus: document.getElementById('status-message'),
        loginBtn: document.getElementById('login-btn'),
        logoutBtn: document.getElementById('logout-btn'),
        loginForm: document.getElementById('login-form'),
        registerForm: document.getElementById('register-form'),
        dashboard: document.getElementById('dashboard'),
        registerLink: document.getElementById('register-link'),
        loginLink: document.getElementById('login-link'),
        tabButtons: document.querySelectorAll('.tab-btn'),
        tabContents: document.querySelectorAll('.tab-content')
    };

    // Initialize the app
    init();

    // Initialize the application
    function init() {
        // Set up event listeners
        setupEventListeners();
        
        // Check login status
        checkLoginStatus();
        
        // Periodically refresh status
        setInterval(checkLoginStatus, 10000);
    }

    // Set up all event listeners
    function setupEventListeners() {
        // Login/Logout buttons
        elements.loginBtn.addEventListener('click', () => {
            showLoginForm();
        });
        
        elements.logoutBtn.addEventListener('click', () => {
            logout();
        });
        
        // Register/Login form links
        elements.registerLink.addEventListener('click', () => {
            hideLoginForm();
            showRegisterForm();
        });
        
        elements.loginLink.addEventListener('click', () => {
            hideRegisterForm();
            showLoginForm();
        });
        
        // Login form submission
        document.getElementById('form-login').addEventListener('submit', function(e) {
            e.preventDefault();
            login();
        });
        
        // Register form submission
        document.getElementById('form-register').addEventListener('submit', function(e) {
            e.preventDefault();
            register();
        });
        
        // Tab switching
        elements.tabButtons.forEach(button => {
            button.addEventListener('click', () => {
                const tabName = button.getAttribute('data-tab');
                switchTab(tabName);
            });
        });
        
        // Add Friend form
        document.getElementById('form-add-friend').addEventListener('submit', function(e) {
            e.preventDefault();
            addFriend();
        });
        
        // Create Listing form
        document.getElementById('form-create-listing').addEventListener('submit', function(e) {
            e.preventDefault();
            createListing();
        });
        
        // Initiate Trade form
        document.getElementById('form-initiate-trade').addEventListener('submit', function(e) {
            e.preventDefault();
            initiateTrade();
        });
        
        // Condition slider
        const conditionSlider = document.getElementById('listing-condition');
        const conditionValue = document.getElementById('condition-value');
        
        if (conditionSlider && conditionValue) {
            conditionSlider.addEventListener('input', function() {
                const value = Math.round(this.value * 100);
                conditionValue.textContent = `${value}%`;
            });
        }
    }

    // Check if user is logged in
    function checkLoginStatus() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                if (data.success && data.loggedIn) {
                    state.isLoggedIn = true;
                    state.username = data.username;
                    
                    // Update UI for logged-in state
                    elements.userStatus.textContent = `Logged in as ${state.username}`;
                    elements.loginBtn.style.display = 'none';
                    elements.logoutBtn.style.display = 'inline-block';
                    
                    hideLoginForm();
                    hideRegisterForm();
                    showDashboard();
                    
                    // Load dashboard data
                    loadDashboardData();
                } else {
                    state.isLoggedIn = false;
                    state.username = null;
                    
                    // Update UI for logged-out state
                    elements.userStatus.textContent = 'Not logged in';
                    elements.loginBtn.style.display = 'inline-block';
                    elements.logoutBtn.style.display = 'none';
                    
                    hideDashboard();
                    showLoginForm();
                }
            })
            .catch(error => {
                console.error('Error checking login status:', error);
                showNotification('Error', 'Failed to connect to server', 'error');
            });
    }

    // Login functionality
    function login() {
        const username = document.getElementById('username').value;
        const password = document.getElementById('password').value;
        
        if (!username || !password) {
            showNotification('Error', 'Username and password are required', 'error');
            return;
        }
        
        fetch('/api/login', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                username,
                password
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                state.isLoggedIn = true;
                state.username = data.username;
                state.token = data.token;
                
                // Store token in localStorage for persistence
                localStorage.setItem('kenshiToken', data.token);
                
                showNotification('Success', 'Login successful!', 'success');
                checkLoginStatus();
            } else {
                showNotification('Error', data.error || 'Login failed', 'error');
            }
        })
        .catch(error => {
            console.error('Error during login:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Logout functionality
    function logout() {
        // Clear token from localStorage
        localStorage.removeItem('kenshiToken');
        
        // Call logout API
        fetch('/api/logout', {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            state.isLoggedIn = false;
            state.username = null;
            state.token = null;
            
            showNotification('Success', 'Logout successful', 'success');
            checkLoginStatus();
        })
        .catch(error => {
            console.error('Error during logout:', error);
            
            // Even if API fails, we still want to log out client-side
            state.isLoggedIn = false;
            state.username = null;
            state.token = null;
            
            checkLoginStatus();
        });
    }

    // Register functionality
    function register() {
        const username = document.getElementById('reg-username').value;
        const password = document.getElementById('reg-password').value;
        const email = document.getElementById('reg-email').value;
        
        if (!username || !password || !email) {
            showNotification('Error', 'All fields are required', 'error');
            return;
        }
        
        // Validate username length
        if (username.length < 3) {
            showNotification('Error', 'Username must be at least 3 characters', 'error');
            return;
        }
        
        // Validate password length
        if (password.length < 8) {
            showNotification('Error', 'Password must be at least 8 characters', 'error');
            return;
        }
        
        // Validate email format
        const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
        if (!emailRegex.test(email)) {
            showNotification('Error', 'Invalid email format', 'error');
            return;
        }
        
        fetch('/api/register', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                username,
                password,
                email
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Registration successful! You can now log in.', 'success');
                hideRegisterForm();
                showLoginForm();
            } else {
                showNotification('Error', data.error || 'Registration failed', 'error');
            }
        })
        .catch(error => {
            console.error('Error during registration:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Switch tabs in the dashboard
    function switchTab(tabName) {
        state.activeTab = tabName;
        
        // Update tab buttons
        elements.tabButtons.forEach(button => {
            if (button.getAttribute('data-tab') === tabName) {
                button.classList.add('active');
            } else {
                button.classList.remove('active');
            }
        });
        
        // Update tab contents
        elements.tabContents.forEach(content => {
            if (content.id === `tab-${tabName}`) {
                content.classList.add('active');
            } else {
                content.classList.remove('active');
            }
        });
        
        // Load tab-specific data
        loadTabData(tabName);
    }

    // Load data for the active tab
    function loadTabData(tabName) {
        switch (tabName) {
            case 'status':
                loadStatusData();
                break;
            case 'friends':
                loadFriendsData();
                break;
            case 'marketplace':
                loadMarketplaceData();
                break;
            case 'trade':
                loadTradeData();
                break;
            case 'inventory':
                loadInventoryData();
                break;
            case 'mods':
                loadModsData();
                break;
        }
    }

    // Load all dashboard data
    function loadDashboardData() {
        // Load data for the active tab
        loadTabData(state.activeTab);
    }

    // Load player and server status data
    function loadStatusData() {
        const playerStatusElement = document.getElementById('player-status');
        const serverStatusElement = document.getElementById('server-status');
        
        // Load player status
        fetch('/api/player/status', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                const player = data.player;
                let html = `
                    <div class=""status-grid"">
                        <div class=""status-item"">
                            <span class=""status-label"">Name:</span>
                            <span class=""status-value"">${player.displayName}</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">Health:</span>
                            <span class=""status-value"">${player.health}/${player.maxHealth}</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">Level:</span>
                            <span class=""status-value"">${player.level}</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">Hunger:</span>
                            <span class=""status-value"">${player.hunger}%</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">Thirst:</span>
                            <span class=""status-value"">${player.thirst}%</span>
                        </div>
                    </div>
                `;
                playerStatusElement.innerHTML = html;
            } else {
                playerStatusElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load player status'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading player status:', error);
            playerStatusElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
        
        // Load server status
        fetch('/api/game/status')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                let html = `
                    <div class=""status-grid"">
                        <div class=""status-item"">
                            <span class=""status-label"">Server Address:</span>
                            <span class=""status-value"">${data.serverAddress}:${data.serverPort}</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">Web Interface:</span>
                            <span class=""status-value"">${data.webInterfaceEnabled ? 'Enabled' : 'Disabled'}</span>
                        </div>
                        <div class=""status-item"">
                            <span class=""status-label"">WebUI Version:</span>
                            <span class=""status-value"">${data.webUiVersion}</span>
                        </div>
                    </div>
                `;
                serverStatusElement.innerHTML = html;
            } else {
                serverStatusElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load server status'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading server status:', error);
            serverStatusElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
    }

    // Load friends data
    function loadFriendsData() {
        const friendsListElement = document.getElementById('friends-list');
        const friendRequestsElement = document.getElementById('friend-requests');
        
        // Show loading state
        friendsListElement.innerHTML = '<p>Loading friends...</p>';
        friendRequestsElement.innerHTML = '<p>Loading friend requests...</p>';
        
        fetch('/api/friends/list', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.friendsList = data.friends;
                
                // Render friends list
                if (data.friends.length === 0) {
                    friendsListElement.innerHTML = '<p>You have no friends yet.</p>';
                } else {
                    let html = '';
                    data.friends.forEach(friend => {
                        const status = friend.isOnline ? 
                            '<span class=""status online"">Online</span>' : 
                            '<span class=""status offline"">Offline</span>';
                        
                        const lastSeen = friend.lastSeen ? 
                            new Date(friend.lastSeen).toLocaleString() : 
                            'Never';
                        
                        html += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${friend.username}</strong>
                                    ${status}
                                </div>
                                <div class=""actions"">
                                    <button onclick=""initiateTrade('${friend.username}')"">Trade</button>
                                    <button class=""secondary"" onclick=""removeFriend('${friend.username}')"">Remove</button>
                                </div>
                            </div>
                        `;
                    });
                    friendsListElement.innerHTML = html;
                }
                
                // Render friend requests
                let incomingHtml = '';
                let outgoingHtml = '';
                
                if (data.incomingRequests.length === 0) {
                    incomingHtml = '<p>No incoming friend requests.</p>';
                } else {
                    data.incomingRequests.forEach(request => {
                        incomingHtml += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${request}</strong>
                                </div>
                                <div class=""actions"">
                                    <button onclick=""acceptFriendRequest('${request}')"">Accept</button>
                                    <button class=""secondary"" onclick=""declineFriendRequest('${request}')"">Decline</button>
                                </div>
                            </div>
                        `;
                    });
                }
                
                if (data.outgoingRequests.length === 0) {
                    outgoingHtml = '<p>No outgoing friend requests.</p>';
                } else {
                    data.outgoingRequests.forEach(request => {
                        outgoingHtml += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${request}</strong>
                                    <span class=""status"">Pending</span>
                                </div>
                                <div class=""actions"">
                                    <button class=""secondary"" onclick=""cancelFriendRequest('${request}')"">Cancel</button>
                                </div>
                            </div>
                        `;
                    });
                }
                
                friendRequestsElement.innerHTML = `
                    <h3>Incoming Requests</h3>
                    ${incomingHtml}
                    <h3>Outgoing Requests</h3>
                    ${outgoingHtml}
                `;
                
                // Update trade dropdown with friends
                updateTradeDropdown(data.friends);
            } else {
                friendsListElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load friends'}</p>`;
                friendRequestsElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load friend requests'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading friends data:', error);
            friendsListElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
            friendRequestsElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
    }

    // Load marketplace data
    function loadMarketplaceData() {
        const marketplaceListingsElement = document.getElementById('marketplace-listings');
        const myListingsElement = document.getElementById('my-listings');
        
        // Show loading state
        marketplaceListingsElement.innerHTML = '<p>Loading marketplace listings...</p>';
        myListingsElement.innerHTML = '<p>Loading your listings...</p>';
        
        fetch('/api/marketplace/listings', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.marketplaceListings = data.activeListings;
                
                // Render marketplace listings
                if (data.activeListings.length === 0) {
                    marketplaceListingsElement.innerHTML = '<p>No items available for purchase.</p>';
                } else {
                    let html = '';
                    data.activeListings.forEach(listing => {
                        const condition = Math.round(listing.itemCondition * 100);
                        
                        html += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${listing.itemName}</strong> x${listing.quantity}
                                    <span class=""status"">${condition}% Condition</span>
                                </div>
                                <div>
                                    <span>${listing.price} cats each (${listing.price * listing.quantity} total)</span>
                                    <div class=""actions"">
                                        <button onclick=""purchaseListing('${listing.id}')"">Purchase</button>
                                    </div>
                                </div>
                            </div>
                        `;
                    });
                    marketplaceListingsElement.innerHTML = html;
                }
                
                // Render my listings
                if (data.myListings.length === 0) {
                    myListingsElement.innerHTML = '<p>You have no active listings.</p>';
                } else {
                    let html = '';
                    data.myListings.forEach(listing => {
                        const condition = Math.round(listing.itemCondition * 100);
                        const listedDate = new Date(listing.listedAt).toLocaleString();
                        
                        html += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${listing.itemName}</strong> x${listing.quantity}
                                    <span class=""status"">${condition}% Condition</span>
                                </div>
                                <div>
                                    <span>${listing.price} cats each (${listing.price * listing.quantity} total)</span>
                                    <small>Listed: ${listedDate}</small>
                                    <div class=""actions"">
                                        <button class=""secondary"" onclick=""cancelListing('${listing.id}')"">Cancel</button>
                                    </div>
                                </div>
                            </div>
                        `;
                    });
                    myListingsElement.innerHTML = html;
                }
                
                // Update item dropdown for creating listings
                loadInventoryForListings();
            } else {
                marketplaceListingsElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load marketplace listings'}</p>`;
                myListingsElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load your listings'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading marketplace data:', error);
            marketplaceListingsElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
            myListingsElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
    }

    // Load trade data
    function loadTradeData() {
        const currentTradeElement = document.getElementById('current-trade');
        const tradeRequestsElement = document.getElementById('trade-requests');
        
        // Show loading state
        currentTradeElement.innerHTML = '<p>Loading current trade...</p>';
        tradeRequestsElement.innerHTML = '<p>Loading trade requests...</p>';
        
        // Get current trade
        fetch('/api/trade/items', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                if (data.trade) {
                    state.currentTrade = data.trade;
                    
                    // Determine trade partner
                    const isInitiator = data.trade.initiatorId === state.username;
                    const partner = isInitiator ? data.trade.targetId : data.trade.initiatorId;
                    
                    // Get offers
                    const yourOffer = isInitiator ? data.trade.initiatorOffer : data.trade.targetOffer;
                    const theirOffer = isInitiator ? data.trade.targetOffer : data.trade.initiatorOffer;
                    
                    // Render trade UI
                    let html = `
                        <div class=""trade-container"">
                            <h3>Trading with ${partner}</h3>
                            <div class=""trade-offers"">
                                <div class=""trade-offer"">
                                    <h4>Your Offer</h4>
                                    ${renderTradeItems(yourOffer.items, true)}
                                    <div class=""trade-actions"">
                                        <button id=""add-item-btn"">Add Item</button>
                                        <button class=""${yourOffer.isConfirmed ? 'secondary' : ''}"" 
                                                onclick=""confirmTradeOffer()"" 
                                                ${yourOffer.isConfirmed ? 'disabled' : ''}>
                                            ${yourOffer.isConfirmed ? 'Confirmed' : 'Confirm Offer'}
                                        </button>
                                    </div>
                                </div>
                                <div class=""trade-offer"">
                                    <h4>${partner}'s Offer</h4>
                                    ${renderTradeItems(theirOffer.items, false)}
                                    <div class=""trade-status"">
                                        ${theirOffer.isConfirmed ? 
                                            '<span class=""status success"">Offer Confirmed</span>' : 
                                            '<span class=""status"">Waiting for confirmation</span>'}
                                    </div>
                                </div>
                            </div>
                            <div class=""trade-footer"">
                                <button class=""secondary"" onclick=""cancelTrade()"">Cancel Trade</button>
                            </div>
                        </div>
                    `;
                    currentTradeElement.innerHTML = html;
                    
                    // Add event listener for the add item button
                    const addItemBtn = document.getElementById('add-item-btn');
                    if (addItemBtn) {
                        addItemBtn.addEventListener('click', () => {
                            showAddItemToTradeDialog();
                        });
                    }
                } else {
                    currentTradeElement.innerHTML = '<p>No active trade</p>';
                }
            } else {
                currentTradeElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load current trade'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading current trade:', error);
            currentTradeElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
        
        // Get trade requests
        // Implementation would be similar to above
    }

    // Helper function to render trade items
    function renderTradeItems(items, isYourItems) {
        if (!items || items.length === 0) {
            return '<p>No items added yet</p>';
        }
        
        let html = '<div class=""trade-items"">';
        items.forEach(item => {
            const condition = Math.round(item.condition * 100);
            
            html += `
                <div class=""trade-item"">
                    <div class=""item-info"">
                        <strong>${item.itemName}</strong> x${item.quantity}
                        <span class=""status"">${condition}% Condition</span>
                    </div>
                    ${isYourItems ? 
                        `<button class=""secondary small"" onclick=""removeTradeItem('${item.itemId}')"">Remove</button>` : 
                        ''}
                </div>
            `;
        });
        html += '</div>';
        
        return html;
    }

    // Load inventory data
    function loadInventoryData() {
        const inventoryItemsElement = document.getElementById('inventory-items');
        
        // Show loading state
        inventoryItemsElement.innerHTML = '<p>Loading inventory...</p>';
        
        fetch('/api/player/inventory', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.inventoryItems = data.items;
                
                // Render inventory
                if (data.items.length === 0) {
                    inventoryItemsElement.innerHTML = '<p>Your inventory is empty.</p>';
                } else {
                    let html = '';
                    data.items.forEach(item => {
                        const condition = Math.round(item.condition * 100);
                        
                        html += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${item.itemName}</strong> x${item.quantity}
                                    <span class=""status"">${condition}% Condition</span>
                                </div>
                                <div class=""actions"">
                                    <button onclick=""sellItem('${item.itemId}', '${item.itemName}')"">Sell</button>
                                </div>
                            </div>
                        `;
                    });
                    inventoryItemsElement.innerHTML = html;
                }
            } else {
                inventoryItemsElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load inventory'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading inventory data:', error);
            inventoryItemsElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
    }

    // Load mods data
    function loadModsData() {
        const modsElement = document.getElementById('installed-mods');
        
        // Show loading state
        modsElement.innerHTML = '<p>Loading mods...</p>';
        
        fetch('/api/game/mods', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Render mods list
                if (data.mods.length === 0) {
                    modsElement.innerHTML = '<p>No mods installed.</p>';
                } else {
                    let html = '';
                    data.mods.forEach(mod => {
                        const status = mod.enabled ? 
                            '<span class=""status online"">Enabled</span>' : 
                            '<span class=""status offline"">Disabled</span>';
                        
                        html += `
                            <div class=""list-item"">
                                <div>
                                    <strong>${mod.name}</strong>
                                    ${status}
                                </div>
                                <div>
                                    <small>${mod.path}</small>
                                </div>
                            </div>
                        `;
                    });
                    modsElement.innerHTML = html;
                }
            } else {
                modsElement.innerHTML = `<p class=""error"">${data.error || 'Failed to load mods'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading mods data:', error);
            modsElement.innerHTML = `<p class=""error"">Failed to connect to server</p>`;
        });
    }

    // Load inventory for marketplace listings
    function loadInventoryForListings() {
        // This would be populated with actual inventory data
        const itemIdSelect = document.getElementById('listing-item-id');
        const itemNameInput = document.getElementById('listing-item-name');
        
        if (itemIdSelect && itemNameInput) {
            fetch('/api/player/inventory', {
                headers: {
                    'Authorization': `Bearer ${state.token}`
                }
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    // Clear existing options
                    itemIdSelect.innerHTML = '';
                    
                    // Add options for each inventory item
                    data.items.forEach(item => {
                        const option = document.createElement('option');
                        option.value = item.itemId;
                        option.textContent = `${item.itemName} x${item.quantity}`;
                        option.dataset.name = item.itemName;
                        option.dataset.condition = item.condition;
                        itemIdSelect.appendChild(option);
                    });
                    
                    // Handle selection change
                    itemIdSelect.addEventListener('change', () => {
                        const selected = itemIdSelect.options[itemIdSelect.selectedIndex];
                        if (selected) {
                            itemNameInput.value = selected.dataset.name || '';
                            
                            // Update condition slider if present
                            const conditionSlider = document.getElementById('listing-condition');
                            const conditionValue = document.getElementById('condition-value');
                            
                            if (conditionSlider && conditionValue && selected.dataset.condition) {
                                const condition = parseFloat(selected.dataset.condition);
                                conditionSlider.value = condition;
                                conditionValue.textContent = `${Math.round(condition * 100)}%`;
                            }
                        }
                    });
                    
                    // Trigger change event for the first item
                    if (itemIdSelect.options.length > 0) {
                        itemIdSelect.selectedIndex = 0;
                        itemIdSelect.dispatchEvent(new Event('change'));
                    }
                }
            })
            .catch(error => {
                console.error('Error loading inventory for listings:', error);
            });
        }
    }

    // Update trade dropdown with friends
    function updateTradeDropdown(friends) {
        const tradeUsernameSelect = document.getElementById('trade-username');
        
        if (tradeUsernameSelect) {
            // Clear existing options
            tradeUsernameSelect.innerHTML = '';
            
            // Add options for each online friend
            const onlineFriends = friends.filter(friend => friend.isOnline);
            
            if (onlineFriends.length === 0) {
                const option = document.createElement('option');
                option.disabled = true;
                option.selected = true;
                option.textContent = 'No online friends';
                tradeUsernameSelect.appendChild(option);
            } else {
                onlineFriends.forEach(friend => {
                    const option = document.createElement('option');
                    option.value = friend.username;
                    option.textContent = friend.username;
                    tradeUsernameSelect.appendChild(option);
                });
            }
        }
    }

    // Add friend functionality
    function addFriend() {
        const username = document.getElementById('friend-username').value;
        
        if (!username) {
            showNotification('Error', 'Username is required', 'error');
            return;
        }
        
        fetch('/api/friends/add', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request sent!', 'success');
                document.getElementById('friend-username').value = '';
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to send friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error sending friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Create marketplace listing
    function createListing() {
        const itemId = document.getElementById('listing-item-id').value;
        const itemName = document.getElementById('listing-item-name').value;
        const quantity = parseInt(document.getElementById('listing-quantity').value);
        const price = parseInt(document.getElementById('listing-price').value);
        const condition = parseFloat(document.getElementById('listing-condition').value);
        
        if (!itemId || !itemName || isNaN(quantity) || isNaN(price)) {
            showNotification('Error', 'All fields are required', 'error');
            return;
        }
        
        if (quantity <= 0) {
            showNotification('Error', 'Quantity must be a positive number', 'error');
            return;
        }
        
        if (price <= 0) {
            showNotification('Error', 'Price must be a positive number', 'error');
            return;
        }
        
        fetch('/api/marketplace/create', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                itemId,
                itemName,
                quantity,
                price,
                condition
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Listing created successfully!', 'success');
                loadMarketplaceData();
            } else {
                showNotification('Error', data.error || 'Failed to create listing', 'error');
            }
        })
        .catch(error => {
            console.error('Error creating listing:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Initiate trade
    function initiateTrade(username) {
        let targetUsername = username;
        
        if (!targetUsername) {
            const select = document.getElementById('trade-username');
            if (select) {
                targetUsername = select.value;
            }
        }
        
        if (!targetUsername) {
            showNotification('Error', 'Username is required', 'error');
            return;
        }
        
        fetch('/api/trade/initiate', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                targetUsername
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade initiated!', 'success');
                switchTab('trade');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to initiate trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error initiating trade:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Show notification
    function showNotification(title, message, type = 'info') {
        const container = document.getElementById('notification-container');
        
        // Create notification element
        const notification = document.createElement('div');
        notification.className = `notification ${type}`;
        notification.innerHTML = `
            <h3>${title}</h3>
            <p>${message}</p>
        `;
        
        // Add to container
        container.appendChild(notification);
        
        // Auto-remove after 5 seconds
        setTimeout(() => {
            notification.style.animation = 'fadeOut 0.3s ease forwards';
            setTimeout(() => {
                notification.remove();
            }, 300);
        }, 5000);
    }

    // UI visibility functions
    function showLoginForm() {
        elements.loginForm.style.display = 'block';
    }
    
    function hideLoginForm() {
        elements.loginForm.style.display = 'none';
    }
    
    function showRegisterForm() {
        elements.registerForm.style.display = 'block';
    }
    
    function hideRegisterForm() {
        elements.registerForm.style.display = 'none';
    }
    
    function showDashboard() {
        elements.dashboard.style.display = 'block';
    }
    
    function hideDashboard() {
        elements.dashboard.style.display = 'none';
    }

    // Global functions - these need to be accessible from HTML
    window.acceptFriendRequest = function(username) {
        fetch('/api/friends/accept', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request accepted!', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to accept friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error accepting friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.declineFriendRequest = function(username) {
        fetch('/api/friends/decline', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request declined', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to decline friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error declining friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.removeFriend = function(username) {
        fetch('/api/friends/remove', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend removed', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to remove friend', 'error');
            }
        })
        .catch(error => {
            console.error('Error removing friend:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.purchaseListing = function(listingId) {
        fetch('/api/marketplace/purchase', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                listingId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Item purchased successfully!', 'success');
                loadMarketplaceData();
                loadInventoryData();
            } else {
                showNotification('Error', data.error || 'Failed to purchase item', 'error');
            }
        })
        .catch(error => {
            console.error('Error purchasing item:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.cancelListing = function(listingId) {
        fetch('/api/marketplace/cancel', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                listingId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Listing cancelled successfully', 'success');
                loadMarketplaceData();
            } else {
                showNotification('Error', data.error || 'Failed to cancel listing', 'error');
            }
        })
        .catch(error => {
            console.error('Error cancelling listing:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.confirmTradeOffer = function() {
        fetch('/api/trade/confirm', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade offer confirmed', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to confirm trade offer', 'error');
            }
        })
        .catch(error => {
            console.error('Error confirming trade offer:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.cancelTrade = function() {
        fetch('/api/trade/cancel', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade cancelled', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to cancel trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error cancelling trade:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.removeTradeItem = function(itemId) {
        fetch('/api/trade/update', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                action: 'remove',
                itemId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Item removed from trade', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to remove item from trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error removing trade item:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.sellItem = function(itemId, itemName) {
        // Open dialog to create listing
        const listingItemIdSelect = document.getElementById('listing-item-id');
        const listingItemNameInput = document.getElementById('listing-item-name');
        
        if (listingItemIdSelect && listingItemNameInput) {
            // Find the option with this item ID
            for (let i = 0; i < listingItemIdSelect.options.length; i++) {
                if (listingItemIdSelect.options[i].value === itemId) {
                    listingItemIdSelect.selectedIndex = i;
                    listingItemIdSelect.dispatchEvent(new Event('change'));
                    break;
                }
            }
            
            // Switch to marketplace tab
            switchTab('marketplace');
            
            // Scroll to create listing section
            document.querySelector('#tab-marketplace .card:nth-child(3)').scrollIntoView({
                behavior: 'smooth'
            });
            
            // Focus on quantity field
            document.getElementById('listing-quantity').focus();
        }
    };
    
    // Try to restore session from localStorage
    const savedToken = localStorage.getItem('kenshiToken');
    if (savedToken) {
        state.token = savedToken;
    }
});";
        }

        private bool IsValidEmail(string email)
        {
            try
            {
                var addr = new System.Net.Mail.MailAddress(email);
                return addr.Address == email;
            }
            catch
            {
                return false;
            }
        }
        }
}
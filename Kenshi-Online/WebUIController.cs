using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Threading;
using System.Text.Json;
using System.Linq;
using System.Diagnostics;

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

        public WebUIController(string webRootPath, int port = 8080)
        {
            webRoot = webRootPath;
            Directory.CreateDirectory(webRoot);

            // Initialize HTTP listener
            listener = new HttpListener();
            listener.Prefixes.Add($"http://localhost:{port}/");

            // Setup API endpoints
            apiEndpoints = new Dictionary<string, Func<HttpListenerRequest, Dictionary<string, object>>>();
            InitializeApiEndpoints();

            // Extract embedded web files or create default ones
            ExtractEmbeddedWebUI();
        }

        public void SetClient(EnhancedClient clientInstance)
        {
            client = clientInstance;
            // Register client message handler for WebUI updates
            if (client != null)
            {
                client.MessageReceived += OnClientMessageReceived;
            }
        }

        public void SetServer(EnhancedServer serverInstance)
        {
            server = serverInstance;
        }

        private void InitializeApiEndpoints()
        {
            // Auth endpoints
            apiEndpoints.Add("/api/login", HandleLogin);
            apiEndpoints.Add("/api/register", HandleRegister);

            // Friends system endpoints
            apiEndpoints.Add("/api/friends/list", HandleFriendsList);
            apiEndpoints.Add("/api/friends/add", HandleFriendAdd);
            apiEndpoints.Add("/api/friends/remove", HandleFriendRemove);
            apiEndpoints.Add("/api/friends/accept", HandleFriendAccept);

            // Marketplace endpoints
            apiEndpoints.Add("/api/marketplace/listings", HandleMarketplaceListings);
            apiEndpoints.Add("/api/marketplace/create", HandleMarketplaceCreate);
            apiEndpoints.Add("/api/marketplace/purchase", HandleMarketplacePurchase);

            // Trading endpoints
            apiEndpoints.Add("/api/trade/initiate", HandleTradeInitiate);
            apiEndpoints.Add("/api/trade/update", HandleTradeUpdate);
            apiEndpoints.Add("/api/trade/confirm", HandleTradeConfirm);
            apiEndpoints.Add("/api/trade/cancel", HandleTradeCancel);

            // Player endpoints
            apiEndpoints.Add("/api/player/inventory", HandlePlayerInventory);
            apiEndpoints.Add("/api/player/status", HandlePlayerStatus);
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

            try
            {
                // Handle API requests
                if (path.StartsWith("/api/"))
                {
                    HandleApiRequest(context);
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
                    // 404 if file not found
                    context.Response.StatusCode = 404;
                    context.Response.Close();
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

        private void ServeFile(HttpListenerContext context, string filePath)
        {
            context.Response.ContentType = GetContentType(filePath);
            context.Response.StatusCode = 200;

            using (FileStream fs = File.OpenRead(filePath))
            {
                fs.CopyTo(context.Response.OutputStream);
            }

            context.Response.Close();
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
                    });
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
                });
            }
        }

        private void SendJsonResponse(HttpListenerContext context, Dictionary<string, object> data)
        {
            context.Response.ContentType = "application/json";
            string json = JsonSerializer.Serialize(data);
            byte[] buffer = Encoding.UTF8.GetBytes(json);

            context.Response.ContentLength64 = buffer.Length;
            context.Response.OutputStream.Write(buffer, 0, buffer.Length);
            context.Response.Close();
        }

        private void OnClientMessageReceived(object sender, GameMessage message)
        {
            // Forward relevant messages to WebUI via WebSocket (implementation needed)
        }

        // API Handler Methods
        private Dictionary<string, object> HandleLogin(HttpListenerRequest request)
        {
            if (client == null)
                return new Dictionary<string, object> { { "success", false }, { "error", "Client not initialized" } };

            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var loginData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call client login method
                bool success = client.Login(
                    client.ServerAddress,
                    client.ServerPort,
                    loginData["username"],
                    loginData["password"]
                );

                return new Dictionary<string, object>
                {
                    { "success", success },
                    { "error", success ? null : "Invalid username or password" }
                };
            }
        }

        private Dictionary<string, object> HandleRegister(HttpListenerRequest request)
        {
            if (client == null)
                return new Dictionary<string, object> { { "success", false }, { "error", "Client not initialized" } };

            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var regData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call client register method
                bool success = client.Register(
                    client.ServerAddress,
                    client.ServerPort,
                    regData["username"],
                    regData["password"],
                    regData["email"]
                );

                return new Dictionary<string, object>
                {
                    { "success", success },
                    { "error", success ? null : "Registration failed" }
                };
            }
        }

        private Dictionary<string, object> HandleFriendsList(HttpListenerRequest request)
        {
            // This would call the FriendsManager to get the friends list
            // For now, return a sample response
            return new Dictionary<string, object>
            {
                { "success", true },
                { "friends", new List<Dictionary<string, object>>
                    {
                        new Dictionary<string, object> { { "username", "Player1" }, { "status", "Online" } },
                        new Dictionary<string, object> { { "username", "Player2" }, { "status", "Offline" } }
                    }
                }
            };
        }

        private Dictionary<string, object> HandleFriendAdd(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call FriendsManager to add friend
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleFriendRemove(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call FriendsManager to remove friend
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleFriendAccept(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var friendData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call FriendsManager to accept friend request
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplaceListings(HttpListenerRequest request)
        {
            // This would call MarketplaceManager to get listings
            // For now, return a sample response
            return new Dictionary<string, object>
            {
                { "success", true },
                { "listings", new List<Dictionary<string, object>>
                    {
                        new Dictionary<string, object>
                        {
                            { "id", 1 },
                            { "itemName", "Iron Plate" },
                            { "sellerName", "Trader" },
                            { "price", 50 },
                            { "quantity", 10 }
                        },
                        new Dictionary<string, object>
                        {
                            { "id", 2 },
                            { "itemName", "Katana" },
                            { "sellerName", "Weaponsmith" },
                            { "price", 1000 },
                            { "quantity", 1 }
                        }
                    }
                }
            };
        }

        private Dictionary<string, object> HandleMarketplaceCreate(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var listingData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call MarketplaceManager to create listing
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "listingId", 3 }
                };
            }
        }

        private Dictionary<string, object> HandleMarketplacePurchase(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var purchaseData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call MarketplaceManager to process purchase
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleTradeInitiate(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var tradeData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call TradeManager to initiate trade
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true },
                    { "tradeId", Guid.NewGuid().ToString() }
                };
            }
        }

        private Dictionary<string, object> HandleTradeUpdate(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var tradeData = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonStr);

                // Call TradeManager to update trade offer
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleTradeConfirm(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var tradeData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call TradeManager to confirm trade
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandleTradeCancel(HttpListenerRequest request)
        {
            using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
            {
                string jsonStr = reader.ReadToEnd();
                var tradeData = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonStr);

                // Call TradeManager to cancel trade
                // This is a placeholder implementation
                return new Dictionary<string, object>
                {
                    { "success", true }
                };
            }
        }

        private Dictionary<string, object> HandlePlayerInventory(HttpListenerRequest request)
        {
            // This would call PlayerManager or directly client to get inventory
            // For now, return a sample response
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

        private Dictionary<string, object> HandlePlayerStatus(HttpListenerRequest request)
        {
            // This would call PlayerManager or directly client to get player status
            // For now, return a sample response
            return new Dictionary<string, object>
            {
                { "success", true },
                { "player", new Dictionary<string, object>
                    {
                        { "displayName", "Player1" },
                        { "health", 85 },
                        { "maxHealth", 100 },
                        { "level", 12 },
                        { "hunger", 80 },
                        { "thirst", 75 }
                    }
                }
            };
        }

        private void ExtractEmbeddedWebUI()
        {
            // Extract embedded web files to the web root directory
            // For now, we'll create a basic index.html if it doesn't exist
            string indexPath = Path.Combine(webRoot, "index.html");
            if (!File.Exists(indexPath))
            {
                File.WriteAllText(indexPath, CreateDefaultHtml());
            }
        }

        private string CreateDefaultHtml()
        {
            // Create a simpler default HTML file to avoid string formatting issues
            string html = "<!DOCTYPE html>\n";
            html += "<html lang='en'>\n";
            html += "<head>\n";
            html += "    <meta charset='UTF-8'>\n";
            html += "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
            html += "    <title>Kenshi Online - WebUI</title>\n";
            html += "    <style>\n";
            html += "        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f0f0f0; }\n";
            html += "        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 5px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
            html += "        h1 { color: #333; }\n";
            html += "        .tabs { display: flex; margin-bottom: 20px; border-bottom: 1px solid #ddd; }\n";
            html += "        .tab { padding: 10px 20px; cursor: pointer; border: 1px solid transparent; border-bottom: none; }\n";
            html += "        .tab.active { background: white; border-color: #ddd; border-radius: 5px 5px 0 0; }\n";
            html += "        .tab-content { display: none; }\n";
            html += "        .tab-content.active { display: block; }\n";
            html += "    </style>\n";
            html += "</head>\n";
            html += "<body>\n";
            html += "    <div class='container'>\n";
            html += "        <h1>Kenshi Online</h1>\n";
            html += "        <div class='tabs'>\n";
            html += "            <div class='tab active' data-tab='status'>Status</div>\n";
            html += "            <div class='tab' data-tab='friends'>Friends</div>\n";
            html += "            <div class='tab' data-tab='marketplace'>Marketplace</div>\n";
            html += "            <div class='tab' data-tab='trade'>Trade</div>\n";
            html += "            <div class='tab' data-tab='inventory'>Inventory</div>\n";
            html += "        </div>\n";
            html += "        <div id='status' class='tab-content active'>\n";
            html += "            <h2>Player Status</h2>\n";
            html += "            <div id='status-content'>Loading...</div>\n";
            html += "        </div>\n";
            html += "        <div id='friends' class='tab-content'>\n";
            html += "            <h2>Friends</h2>\n";
            html += "            <div id='friends-list'>Loading...</div>\n";
            html += "            <h3>Add Friend</h3>\n";
            html += "            <input type='text' id='friend-username' placeholder='Username'>\n";
            html += "            <button id='add-friend-btn'>Add Friend</button>\n";
            html += "        </div>\n";
            html += "        <div id='marketplace' class='tab-content'>\n";
            html += "            <h2>Marketplace</h2>\n";
            html += "            <div id='marketplace-listings'>Loading...</div>\n";
            html += "            <h3>Create Listing</h3>\n";
            html += "            <select id='listing-item'></select>\n";
            html += "            <input type='number' id='listing-price' placeholder='Price'>\n";
            html += "            <input type='number' id='listing-quantity' placeholder='Quantity'>\n";
            html += "            <button id='create-listing-btn'>Create Listing</button>\n";
            html += "        </div>\n";
            html += "        <div id='trade' class='tab-content'>\n";
            html += "            <h2>Trade</h2>\n";
            html += "            <select id='trade-player'></select>\n";
            html += "            <button id='initiate-trade-btn'>Initiate Trade</button>\n";
            html += "            <div id='trade-panel' style='display:none;'>\n";
            html += "                <h3>Trade with <span id='trade-partner-name'></span></h3>\n";
            html += "                <div class='trade-offers'>\n";
            html += "                    <div class='your-offer'>\n";
            html += "                        <h4>Your Offer</h4>\n";
            html += "                        <div id='your-items'></div>\n";
            html += "                        <select id='your-item-add'></select>\n";
            html += "                        <input type='number' id='your-item-quantity' value='1' min='1'>\n";
            html += "                        <button id='add-your-item'>Add</button>\n";
            html += "                    </div>\n";
            html += "                    <div class='their-offer'>\n";
            html += "                        <h4>Their Offer</h4>\n";
            html += "                        <div id='their-items'></div>\n";
            html += "                    </div>\n";
            html += "                </div>\n";
            html += "                <button id='confirm-trade-btn'>Confirm Trade</button>\n";
            html += "                <button id='cancel-trade-btn'>Cancel Trade</button>\n";
            html += "            </div>\n";
            html += "        </div>\n";
            html += "        <div id='inventory' class='tab-content'>\n";
            html += "            <h2>Inventory</h2>\n";
            html += "            <div id='inventory-items'>Loading...</div>\n";
            html += "        </div>\n";
            html += "    </div>\n";
            html += "    <script src='/scripts/main.js'></script>\n";
            html += "</body>\n";
            html += "</html>";

            return html;
        }
    }
}
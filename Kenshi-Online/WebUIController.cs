using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Threading;
using System.Text.Json;
using static System.Runtime.InteropServices.JavaScript.JSType;
using System.Diagnostics;
using System.Reflection.Metadata;

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
        private Dictionary<string, Func<HttpListenerRequest, Dictionary<string, object>>> apiEndpoints = new Dictionary<string, Func<HttpListenerRequest, Dictionary<string, object>>>();

        public WebUIController(string webRootPath, int port = 8080)
        {
            webRoot = webRootPath;
            Directory.CreateDirectory(webRoot);
            ExtractEmbeddedWebUI();

            // Initialize HTTP listener
            listener = new HttpListener();
            listener.Prefixes.Add($"http://localhost:{port}/");

            // Setup API endpoints
            InitializeApiEndpoints();
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

        private void ExtractEmbeddedWebUI()
        {
            // Extract embedded web files to the web root directory
            // This will be implemented to pull resources from embedded resources

            // For now, we'll create a basic index.html if it doesn't exist
            string indexPath = Path.Combine(webRoot, "index.html");
            if (!File.Exists(indexPath))
            {
                File.WriteAllText(indexPath, GetDefaultIndexHtml());
            }
        }

        private string GetDefaultIndexHtml()
        {
            return @"<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Kenshi Online - WebUI</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f0f0f0; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 5px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; }
        .tabs { display: flex; margin-bottom: 20px; border-bottom: 1px solid #ddd; }
        .tab { padding: 10px 20px; cursor: pointer; border: 1px solid transparent; border-bottom: none; }
        .tab.active { background: white; border-color: #ddd; border-radius: 5px 5px 0 0; }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>Kenshi Online</h1>
        <div class='tabs'>
            <div class='tab active' data-tab='status'>Status</div>
            <div class='tab' data-tab='friends'>Friends</div>
            <div class='tab' data-tab='marketplace'>Marketplace</div>
            <div class='tab' data-tab='trade'>Trade</div>
            <div class='tab' data-tab='inventory'>Inventory</div>
        </div>

        <div id='status' class='tab-content active'>
            <h2>Player Status</h2>
            <div id='status-content'>Loading...</div>
        </div>

        <div id='friends' class='tab-content'>
            <h2>Friends</h2>
            <div id='friends-list'>Loading...</div>
            <h3>Add Friend</h3>
            <input type='text' id='friend-username' placeholder='Username'>
            <button id='add-friend-btn'>Add Friend</button>
        </div>

        <div id='marketplace' class='tab-content'>
            <h2>Marketplace</h2>
            <div id='marketplace-listings'>Loading...</div>
            <h3>Create Listing</h3>
            <select id='listing-item'></select>
            <input type='number' id='listing-price' placeholder='Price'>
            <input type='number' id='listing-quantity' placeholder='Quantity'>
            <button id='create-listing-btn'>Create Listing</button>
        </div>

        <div id='trade' class='tab-content'>
            <h2>Trade</h2>
            <select id='trade-player'></select>
            <button id='initiate-trade-btn'>Initiate Trade</button>
            <div id='trade-panel' style='display:none;'>
                <h3>Trade with <span id='trade-partner-name'></span></h3>
                <div class='trade-offers'>
                    <div class='your-offer'>
                        <h4>Your Offer</h4>
                        <div id='your-items'></div>
                        <select id='your-item-add'></select>
                        <input type='number' id='your-item-quantity' value='1' min='1'>
                        <button id='add-your-item'>Add</button>
                    </div>
                    <div class='their-offer'>
                        <h4>Their Offer</h4>
                        <div id='their-items'></div>
                    </div>
                </div>
                <button id='confirm-trade-btn'>Confirm Trade</button>
                <button id='cancel-trade-btn'>Cancel Trade</button>
            </div>
        </div>

        <div id='inventory' class='tab-content'>
            <h2>Inventory</h2>
            <div id='inventory-items'>Loading...</div>
        </div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', function() {
            // Tab switching
            document.querySelectorAll('.tab').forEach(tab => {
                tab.addEventListener('click', function() {
                    const tabId = this.getAttribute('data-tab');
                    
                    // Update active tab
                    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
                    this.classList.add('active');
                    
                    // Update active content
                    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
                    document.getElementById(tabId).classList.add('active');
                });
            });

            // Load initial data
            fetchPlayerStatus();
            fetchFriendsList();
            fetchMarketplaceListings();
            fetchInventory();
        });

        // API Calls
        async function fetchPlayerStatus() {
            try {
                const response = await fetch('/api/player/status');
                const data = await response.json();
                
                if (data.success) {
                    let html = `
                        <p><strong>Name:</strong> ${data.player.displayName}</p>
                        <p><strong>Health:</strong> ${data.player.health}/${data.player.maxHealth}</p>
                        <p><strong>Level:</strong> ${data.player.level}</p>
                    `;
                    document.getElementById('status-content').innerHTML = html;
                } else {
                    document.getElementById('status-content').innerHTML = 'Error loading status: ' + data.error;
                }
            } catch (error) {
                document.getElementById('status-content').innerHTML = 'Error connecting to server';
            }
        }

        async function fetchFriendsList() {
            try {
                const response = await fetch('/api/friends/list');
                const data = await response.json();
                
                if (data.success) {
                    let html = '<ul>';
                    data.friends.forEach(friend => {
                        html += `<li>${friend.username} (${friend.status}) <button onclick='removeFriend(\"${ friend.username}\")'>Remove</button></li>`;
                    });
                    html += '</ul>';
                    
                    document.getElementById('friends-list').innerHTML = html;
                } else {
                    document.getElementById('friends-list').innerHTML = 'Error loading friends: ' + data.error;
                }
            } catch (error) {
    document.getElementById('friends-list').innerHTML = 'Error connecting to server';
}
        }

        async function fetchMarketplaceListings()
{
    try
    {
        const response = await fetch('/api/marketplace/listings');
        const data = await response.json();

        if (data.success)
        {
            let html = '<table style=\"width:100%\"><tr><th>Item</th><th>Seller</th><th>Price</th><th>Quantity</th><th>Action</th></tr>';
            data.listings.forEach(listing => {
                html += `< tr >

                    < td >${ listing.itemName}</ td >

                    < td >${ listing.sellerName}</ td >

                    < td >${ listing.price}</ td >

                    < td >${ listing.quantity}</ td >

                    < td >< button onclick = 'purchaseItem(${listing.id})' > Buy </ button ></ td >

                </ tr >`;
            });
            html += '</table>';

            document.getElementById('marketplace-listings').innerHTML = html;
        }
        else
        {
            document.getElementById('marketplace-listings').innerHTML = 'Error loading marketplace: ' + data.error;
        }
    }
    catch (error)
    {
        document.getElementById('marketplace-listings').innerHTML = 'Error connecting to server';
    }
}

async function fetchInventory()
{
    try
    {
        const response = await fetch('/api/player/inventory');
        const data = await response.json();

        if (data.success)
        {
            let html = '<table style=\"width:100%\"><tr><th>Item</th><th>Quantity</th><th>Condition</th></tr>';
            data.items.forEach(item => {
                html += `< tr >

                    < td >${ item.itemName}</ td >

                    < td >${ item.quantity}</ td >

                    < td >${ item.condition.toFixed(2)}</ td >

                </ tr >`;
            });
            html += '</table>';

            document.getElementById('inventory-items').innerHTML = html;

            // Also update the item selectors for marketplace and trade
            let selectOptions = '<option value=\"\">Select an item</option>';
            data.items.forEach(item => {
                selectOptions += `< option value =\"${item.itemId}\">${item.itemName} (${item.quantity})</option>`;
                    });

            document.getElementById('listing-item').innerHTML = selectOptions;
            document.getElementById('your-item-add').innerHTML = selectOptions;

        }
        else
        {
            document.getElementById('inventory-items').innerHTML = 'Error loading inventory: ' + data.error;
        }
    }
    catch (error)
    {
        document.getElementById('inventory-items').innerHTML = 'Error connecting to server';
    }
}

// Functions for user interactions
async function addFriend()
{
    const username = document.getElementById('friend-username').value.trim();
    if (!username) return;

    try
    {
        const response = await fetch('/api/friends/add', {
        method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ username })
                });
const data = await response.json();

if (data.success)
{
    alert('Friend request sent!');
    fetchFriendsList();
}
else
{
    alert('Error: ' + data.error);
}
            } catch (error) {
    alert('Error connecting to server');
}
        }

        async function removeFriend(username)
{
    try
    {
        const response = await fetch('/api/friends/remove', {
        method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ username })
                });
const data = await response.json();

if (data.success)
{
    alert('Friend removed!');
    fetchFriendsList();
}
else
{
    alert('Error: ' + data.error);
}
            } catch (error) {
    alert('Error connecting to server');
}
        }

        async function createListing()
{
    const itemId = document.getElementById('listing-item').value;
    const price = document.getElementById('listing-price').value;
    const quantity = document.getElementById('listing-quantity').value;

    if (!itemId || !price || !quantity)
    {
        alert('Please fill all fields');
        return;
    }

    try
    {
        const response = await fetch('/api/marketplace/create', {
        method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ itemId, price, quantity })
                });
const data = await response.json();

if (data.success)
{
    alert('Listing created!');
    fetchMarketplaceListings();
    fetchInventory();
}
else
{
    alert('Error: ' + data.error);
}
            } catch (error) {
    alert('Error connecting to server');
}
        }

        async function purchaseItem(listingId)
{
    try
    {
        const response = await fetch('/api/marketplace/purchase', {
        method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ listingId })
                });
const data = await response.json();

if (data.success)
{
    alert('Item purchased!');
    fetchMarketplaceListings();
    fetchInventory();
}
else
{
    alert('Error: ' + data.error);
}
            } catch (error) {
    alert('Error connecting to server');
}
        }

        // Set up event listeners
        document.addEventListener('DOMContentLoaded', function() {
    document.getElementById('add-friend-btn').addEventListener('click', addFriend);
    document.getElementById('create-listing-btn').addEventListener('click', createListing);

    // Add more event listeners for other functions
});
    </ script >
</ body >
</ html > ";
        }

        #region API Handlers
        
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
        
        #endregion
    }
}
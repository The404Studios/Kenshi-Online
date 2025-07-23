using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace KenshiMultiplayer
{
    class EnhancedProgram
    {
        static void Main(string[] args)
        {
            DisplayHeader();

            Console.WriteLine("Kenshi Multiplayer Server/Client");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("1. Start Server");
            Console.WriteLine("2. Start Client");
            Console.Write("Select option: ");

            string input = Console.ReadLine()?.Trim();

            if (input == "1")
            {
                StartServer();
            }
            else if (input == "2")
            {
                StartClient();
            }
            else
            {
                Console.WriteLine("Invalid option.");
            }
        }

        static void DisplayHeader()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
  _  __                _     _   ____        _ _            
 | |/ /   _ _ __  ___| |__ (_) / ___| _ __ | (_)_ __   ___ 
 | ' / | | | '_ \/ __| '_ \| | \___ \| '_ \| | | '_ \ / _ \
 | . \ |_| | | | \__ \ | | | |  ___) | | | | | | | | |  __/
 |_|\_\__,_|_| |_|___/_| |_|_| |____/|_| |_|_|_|_| |_|\___|
                                                            
");
            Console.ResetColor();
        }

        static void StartServer()
        {
            string kenshiPath = FindKenshiInstallationPath();

            if (string.IsNullOrEmpty(kenshiPath))
            {
                Console.WriteLine("Cannot start server without a valid Kenshi installation path.");
                return;
            }

            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            Console.Write("Enable WebUI? (y/n) [y]: ");
            string webUIInput = Console.ReadLine()?.Trim().ToLower();
            bool enableWebUI = string.IsNullOrEmpty(webUIInput) || webUIInput == "y" || webUIInput == "yes";

            int webUIPort = 8080;
            if (enableWebUI)
            {
                Console.Write("WebUI port [8080]: ");
                string webUIPortInput = Console.ReadLine()?.Trim();
                webUIPort = string.IsNullOrEmpty(webUIPortInput) ? 8080 : int.Parse(webUIPortInput);
            }

            try
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("Starting Kenshi Multiplayer server...");
                Console.WriteLine($"Using Kenshi installation at: {kenshiPath}");
                Console.WriteLine($"Server will listen on port: {port}");
                if (enableWebUI)
                {
                    Console.WriteLine($"WebUI will be available at: http://localhost:{webUIPort}");
                }
                Console.ResetColor();

                // Load mods
                GameModManager modManager = new GameModManager(kenshiPath);
                var activeMods = modManager.GetActiveMods();

                Console.WriteLine($"Detected {modManager.GetAllMods().Count} mods, {activeMods.Count} active");
                foreach (var mod in activeMods)
                {
                    Console.WriteLine($"  - {mod.Name} {(string.IsNullOrEmpty(mod.Version) ? "" : $"v{mod.Version}")}");
                }

                var server = new EnhancedServer(kenshiPath);

                if (enableWebUI)
                {
                    // Initialize WebUI
                    string webRootPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "webui");
                    var webUI = new WebUIController(webRootPath, webUIPort);
                    webUI.SetServer(server);
                    webUI.Start();
                }

                server.Start(port);

                // Display admin commands
                Console.WriteLine("\nServer started! Available commands:");
                Console.WriteLine("/help - Display available commands");
                Console.WriteLine("/list - List connected players");
                Console.WriteLine("/kick <username> - Kick a player");
                Console.WriteLine("/ban <username> <hours> - Ban a player");
                Console.WriteLine("/create-lobby <id> <isPrivate> <password> <maxPlayers> - Create a lobby");
                Console.WriteLine("/list-lobbies - List all lobbies");
                Console.WriteLine("/broadcast <message> - Send message to all players");
                Console.WriteLine("/list-mods - List all detected mods");
                if (enableWebUI)
                {
                    Console.WriteLine("/webui-disable - Disable WebUI");
                    Console.WriteLine("/webui-enable - Enable WebUI");
                }
                Console.WriteLine("/shutdown - Stop the server");
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Failed to start server: {ex.Message}");
                Console.ResetColor();
            }
        }


        static string FindKenshiInstallationPath()
        {
            Console.WriteLine("Detecting Kenshi installation...");

            string kenshiPath = GameModManager.FindKenshiInstallation();

            if (kenshiPath != null)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"Kenshi installation detected at: {kenshiPath}");
                Console.ResetColor();
                return kenshiPath;
            }

            // If automatic detection failed, ask user
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("Could not automatically detect Kenshi installation.");
            Console.ResetColor();

            Console.Write("Enter Kenshi installation path (e.g. C:\\Steam\\steamapps\\common\\Kenshi): ");
            string userPath = Console.ReadLine()?.Trim();

            if (string.IsNullOrEmpty(userPath) || !Directory.Exists(userPath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Invalid path. Kenshi installation not found.");
                Console.ResetColor();
                return null;
            }

            // Validate the path by checking for kenshi_x64.exe
            string exePath = Path.Combine(userPath, "kenshi_x64.exe");
            if (!File.Exists(exePath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Kenshi executable not found at the specified path.");
                Console.ResetColor();
                return null;
            }

            return userPath;
        }

        static void StartClient()
        {
            Console.Write("Enter cache directory path [./cache]: ");
            string cachePath = Console.ReadLine()?.Trim();

            if (string.IsNullOrEmpty(cachePath))
            {
                cachePath = "./cache";
            }

            Directory.CreateDirectory(cachePath);

            Console.Write("Server address [localhost]: ");
            string serverAddress = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(serverAddress))
            {
                serverAddress = "localhost";
            }

            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            Console.Write("Enable WebUI? (y/n) [y]: ");
            string webUIInput = Console.ReadLine()?.Trim().ToLower();
            bool enableWebUI = string.IsNullOrEmpty(webUIInput) || webUIInput == "y" || webUIInput == "yes";

            int webUIPort = 8080;
            if (enableWebUI)
            {
                Console.Write("WebUI port [8080]: ");
                string webUIPortInput = Console.ReadLine()?.Trim();
                webUIPort = string.IsNullOrEmpty(webUIPortInput) ? 8080 : int.Parse(webUIPortInput);
            }

            // Initialize client
            var client = new EnhancedClient(cachePath);

            if (enableWebUI)
            {
                client.EnableWebInterface(webUIPort);
            }

            // Register message handler
            client.MessageReceived += (sender, msg) => {
                if (msg.Type == MessageType.Chat || msg.Type == MessageType.SystemMessage)
                {
                    string chatMessage = msg.Data.ContainsKey("message") ? msg.Data["message"].ToString() : "";
                    Console.WriteLine($"[{msg.PlayerId}]: {chatMessage}");
                }
            };

            while (true)
            {
                Console.Clear();
                DisplayHeader();
                Console.WriteLine("Kenshi Multiplayer Client");
                Console.WriteLine("---------------------------------");
                Console.WriteLine("1. Login");
                Console.WriteLine("2. Register");
                Console.WriteLine("3. Exit");
                Console.Write("Select option: ");

                string input = Console.ReadLine()?.Trim();

                if (input == "1")
                {
                    // Login
                    Console.Write("Username: ");
                    string username = Console.ReadLine();

                    Console.Write("Password: ");
                    string password = ReadPassword();

                    Console.WriteLine("\nLogging in...");

                    if (client.Login(serverAddress, port, username, password))
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("Login successful!");
                        Console.ResetColor();
                        ClientMainMenu(client);
                        break;
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("Login failed.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                }
                else if (input == "2")
                {
                    // Register
                    Console.Write("Username: ");
                    string username = Console.ReadLine();

                    Console.Write("Password: ");
                    string password = ReadPassword();

                    Console.Write("\nEmail: ");
                    string email = Console.ReadLine();

                    Console.WriteLine("Registering...");

                    if (client.Register(serverAddress, port, username, password, email))
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("Registration successful! You can now login.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("Registration failed.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                }
                else if (input == "3")
                {
                    client.Disconnect();
                    break;
                }
            }
        }

        static void ClientMainMenu(EnhancedClient client)
        {
            while (true)
            {
                Console.Clear();
                DisplayHeader();
                Console.WriteLine("Kenshi Multiplayer Client - Connected");
                Console.WriteLine("---------------------------------");
                Console.WriteLine("1. Download Game Files");
                Console.WriteLine("2. Chat");
                Console.WriteLine("3. Friends");
                Console.WriteLine("4. Marketplace");
                Console.WriteLine("5. Trade");
                if (client.IsWebInterfaceEnabled)
                {
                    Console.WriteLine("6. Disable WebUI");
                }
                else
                {
                    Console.WriteLine("6. Enable WebUI");
                }
                Console.WriteLine("7. Disconnect");
                Console.Write("Select option: ");

                string input = Console.ReadLine()?.Trim();

                if (input == "1")
                {
                    DownloadFilesMenu(client);
                }
                else if (input == "2")
                {
                    ChatMenu(client);
                }
                else if (input == "3")
                {
                    FriendsMenu(client);
                }
                else if (input == "4")
                {
                    MarketplaceMenu(client);
                }
                else if (input == "5")
                {
                    TradeMenu(client);
                }
                else if (input == "6")
                {
                    if (client.IsWebInterfaceEnabled)
                    {
                        client.DisableWebInterface();
                        Console.WriteLine("WebUI disabled");
                    }
                    else
                    {
                        Console.Write("WebUI port [8080]: ");
                        string portInput = Console.ReadLine()?.Trim();
                        int port = string.IsNullOrEmpty(portInput) ? 8080 : int.Parse(portInput);

                        client.EnableWebInterface(port);
                        Console.WriteLine($"WebUI enabled at http://localhost:{port}");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "7")
                {
                    client.Disconnect();
                    break;
                }
            }
        }

        static void FriendsMenu(EnhancedClient client)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("Friends");
                Console.WriteLine("---------------------------------");

                var friends = client.GetFriends();
                var incomingRequests = client.GetIncomingFriendRequests();
                var outgoingRequests = client.GetOutgoingFriendRequests();

                // Display friends list
                Console.WriteLine("Friends List:");
                if (friends.Count == 0)
                {
                    Console.WriteLine("  No friends yet.");
                }
                else
                {
                    int index = 1;
                    foreach (var friend in friends)
                    {
                        string status = friend.IsOnline ? "Online" : "Offline";
                        string lastSeen = friend.LastSeen.HasValue ?
                            $", Last seen: {friend.LastSeen.Value.ToString("g")}" : "";

                        Console.WriteLine($"  {index}. {friend.Username} ({status}{lastSeen})");
                        index++;
                    }
                }

                Console.WriteLine("\nIncoming Friend Requests:");
                if (incomingRequests.Count == 0)
                {
                    Console.WriteLine("  No pending requests.");
                }
                else
                {
                    int index = 1;
                    foreach (var request in incomingRequests)
                    {
                        Console.WriteLine($"  {index}. {request}");
                        index++;
                    }
                }

                Console.WriteLine("\nOutgoing Friend Requests:");
                if (outgoingRequests.Count == 0)
                {
                    Console.WriteLine("  No pending requests.");
                }
                else
                {
                    int index = 1;
                    foreach (var request in outgoingRequests)
                    {
                        Console.WriteLine($"  {index}. {request}");
                        index++;
                    }
                }

                // Display options
                Console.WriteLine("\nOptions:");
                Console.WriteLine("1. Add Friend");
                Console.WriteLine("2. Accept Friend Request");
                Console.WriteLine("3. Decline Friend Request");
                Console.WriteLine("4. Remove Friend");
                Console.WriteLine("5. Back to Main Menu");
                Console.Write("Select option: ");

                string input = Console.ReadLine()?.Trim();

                if (input == "1")
                {
                    Console.Write("Enter username to add: ");
                    string username = Console.ReadLine()?.Trim();

                    if (!string.IsNullOrEmpty(username))
                    {
                        if (client.SendFriendRequest(username))
                        {
                            Console.WriteLine("Friend request sent successfully!");
                        }
                        else
                        {
                            Console.WriteLine("Failed to send friend request.");
                        }
                        Thread.Sleep(2000);
                    }
                }
                else if (input == "2")
                {
                    if (incomingRequests.Count == 0)
                    {
                        Console.WriteLine("No pending friend requests to accept.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Enter request number to accept: ");
                    if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                        index > 0 && index <= incomingRequests.Count)
                    {
                        string username = incomingRequests[index - 1];
                        if (client.AcceptFriendRequest(username))
                        {
                            Console.WriteLine($"Accepted friend request from {username}!");
                        }
                        else
                        {
                            Console.WriteLine("Failed to accept friend request.");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Invalid selection.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "3")
                {
                    if (incomingRequests.Count == 0)
                    {
                        Console.WriteLine("No pending friend requests to decline.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Enter request number to decline: ");
                    if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                        index > 0 && index <= incomingRequests.Count)
                    {
                        string username = incomingRequests[index - 1];
                        if (client.DeclineFriendRequest(username))
                        {
                            Console.WriteLine($"Declined friend request from {username}.");
                        }
                        else
                        {
                            Console.WriteLine("Failed to decline friend request.");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Invalid selection.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "4")
                {
                    if (friends.Count == 0)
                    {
                        Console.WriteLine("No friends to remove.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Enter friend number to remove: ");
                    if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                        index > 0 && index <= friends.Count)
                    {
                        string username = friends[index - 1].Username;
                        if (client.RemoveFriend(username))
                        {
                            Console.WriteLine($"Removed {username} from friends list.");
                        }
                        else
                        {
                            Console.WriteLine("Failed to remove friend.");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Invalid selection.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "5")
                {
                    break;
                }
            }
        }

        static void MarketplaceMenu(EnhancedClient client)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("Marketplace");
                Console.WriteLine("---------------------------------");

                var listings = client.GetActiveMarketListings();
                var myListings = client.GetMyMarketListings();

                // Display active listings
                Console.WriteLine("Available Items:");
                if (listings.Count == 0)
                {
                    Console.WriteLine("  No items available for purchase.");
                }
                else
                {
                    int index = 1;
                    Console.WriteLine("  ID | Item Name | Seller | Price | Quantity | Condition");
                    Console.WriteLine("  ------------------------------------------------------");
                    foreach (var listing in listings)
                    {
                        Console.WriteLine($"  {index}. {listing.ItemName} | {listing.SellerName} | " +
                                         $"{listing.Price} | {listing.Quantity} | {listing.ItemCondition:F2}");
                        index++;
                    }
                }

                Console.WriteLine("\nYour Listings:");
                if (myListings.Count == 0)
                {
                    Console.WriteLine("  You have no active listings.");
                }
                else
                {
                    int index = 1;
                    Console.WriteLine("  ID | Item Name | Price | Quantity | Listed At");
                    Console.WriteLine("  -------------------------------------------");
                    foreach (var listing in myListings)
                    {
                        Console.WriteLine($"  {index}. {listing.ItemName} | {listing.Price} | " +
                                         $"{listing.Quantity} | {listing.ListedAt:g}");
                        index++;
                    }
                }

                // Display options
                Console.WriteLine("\nOptions:");
                Console.WriteLine("1. Create New Listing");
                Console.WriteLine("2. Purchase Item");
                Console.WriteLine("3. Cancel My Listing");
                Console.WriteLine("4. Search Listings");
                Console.WriteLine("5. Back to Main Menu");
                Console.Write("Select option: ");

                string input = Console.ReadLine()?.Trim();

                if (input == "1")
                {
                    // In a real implementation, we would list the player's inventory here
                    // For now, we'll just ask for the item details

                    Console.Write("Item ID: ");
                    string itemId = Console.ReadLine()?.Trim();

                    Console.Write("Item Name: ");
                    string itemName = Console.ReadLine()?.Trim();

                    Console.Write("Quantity: ");
                    if (!int.TryParse(Console.ReadLine()?.Trim(), out int quantity) || quantity <= 0)
                    {
                        Console.WriteLine("Invalid quantity.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Price (per item): ");
                    if (!int.TryParse(Console.ReadLine()?.Trim(), out int price) || price <= 0)
                    {
                        Console.WriteLine("Invalid price.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Condition (0.0-1.0) [1.0]: ");
                    string condInput = Console.ReadLine()?.Trim();
                    float condition = string.IsNullOrEmpty(condInput) ? 1.0f :
                        float.TryParse(condInput, out float c) ? Math.Clamp(c, 0.0f, 1.0f) : 1.0f;

                    if (client.CreateMarketListing(itemId, itemName, quantity, price, condition))
                    {
                        Console.WriteLine("Listing created successfully!");
                    }
                    else
                    {
                        Console.WriteLine("Failed to create listing.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "2")
                {
                    if (listings.Count == 0)
                    {
                        Console.WriteLine("No items available for purchase.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Enter listing number to purchase: ");
                    if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                        index > 0 && index <= listings.Count)
                    {
                        string listingId = listings[index - 1].Id;
                        Console.Write($"Buy {listings[index - 1].ItemName} x{listings[index - 1].Quantity} " +
                                    $"for {listings[index - 1].Price * listings[index - 1].Quantity} cats? (y/n): ");

                        string confirm = Console.ReadLine()?.Trim().ToLower();
                        if (confirm == "y" || confirm == "yes")
                        {
                            if (client.PurchaseMarketListing(listingId))
                            {
                                Console.WriteLine("Purchase successful!");
                            }
                            else
                            {
                                Console.WriteLine("Failed to purchase item.");
                            }
                        }
                    }
                    else
                    {
                        Console.WriteLine("Invalid selection.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "3")
                {
                    if (myListings.Count == 0)
                    {
                        Console.WriteLine("You have no active listings to cancel.");
                        Thread.Sleep(2000);
                        continue;
                    }

                    Console.Write("Enter listing number to cancel: ");
                    if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                        index > 0 && index <= myListings.Count)
                    {
                        string listingId = myListings[index - 1].Id;
                        if (client.CancelMarketListing(listingId))
                        {
                            Console.WriteLine("Listing cancelled successfully!");
                        }
                        else
                        {
                            Console.WriteLine("Failed to cancel listing.");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Invalid selection.");
                    }
                    Thread.Sleep(2000);
                }
                else if (input == "4")
                {
                    Console.Write("Enter search term: ");
                    string searchTerm = Console.ReadLine()?.Trim();

                    var searchResults = client.SearchMarketListings(searchTerm);

                    Console.WriteLine($"\nSearch Results for '{searchTerm}':");
                    if (searchResults.Count == 0)
                    {
                        Console.WriteLine("  No matching items found.");
                    }
                    else
                    {
                        int index = 1;
                        Console.WriteLine("  ID | Item Name | Seller | Price | Quantity | Condition");
                        Console.WriteLine("  ------------------------------------------------------");
                        foreach (var listing in searchResults)
                        {
                            Console.WriteLine($"  {index}. {listing.ItemName} | {listing.SellerName} | " +
                                             $"{listing.Price} | {listing.Quantity} | {listing.ItemCondition:F2}");
                            index++;
                        }
                    }

                    Console.WriteLine("\nPress any key to continue...");
                    Console.ReadKey();
                }
                else if (input == "5")
                {
                    break;
                }
            }
        }

        static void TradeMenu(EnhancedClient client)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("Trading");
                Console.WriteLine("---------------------------------");

                var currentTrade = client.GetCurrentTrade();
                var incomingRequests = client.GetIncomingTradeRequests();
                var outgoingRequests = client.GetOutgoingTradeRequests();

                // Display current trade if any
                if (currentTrade != null)
                {
                    string partnerName = currentTrade.InitiatorId == client.CurrentUsername ?
                        currentTrade.TargetId : currentTrade.InitiatorId;

                    Console.WriteLine($"Current Trade with {partnerName}:");

                    var yourOffer = currentTrade.InitiatorId == client.CurrentUsername ?
                        currentTrade.InitiatorOffer : currentTrade.TargetOffer;

                    var theirOffer = currentTrade.InitiatorId == client.CurrentUsername ?
                        currentTrade.TargetOffer : currentTrade.InitiatorOffer;

                    Console.WriteLine("\nYour offer:");
                    if (yourOffer.Items.Count == 0)
                    {
                        Console.WriteLine("  Nothing offered yet.");
                    }
                    else
                    {
                        int index = 1;
                        foreach (var item in yourOffer.Items)
                        {
                            Console.WriteLine($"  {index}. {item.ItemName} x{item.Quantity} (Condition: {item.Condition:F2})");
                            index++;
                        }
                    }

                    Console.WriteLine($"Your offer status: {(yourOffer.IsConfirmed ? "Confirmed" : "Not Confirmed")}");

                    Console.WriteLine("\nTheir offer:");
                    if (theirOffer.Items.Count == 0)
                    {
                        Console.WriteLine("  Nothing offered yet.");
                    }
                    else
                    {
                        int index = 1;
                        foreach (var item in theirOffer.Items)
                        {
                            Console.WriteLine($"  {index}. {item.ItemName} x{item.Quantity} (Condition: {item.Condition:F2})");
                            index++;
                        }
                    }

                    Console.WriteLine($"Their offer status: {(theirOffer.IsConfirmed ? "Confirmed" : "Not Confirmed")}");

                    Console.WriteLine("\nCurrent Trade Options:");
                    Console.WriteLine("1. Add Item to Trade");
                    Console.WriteLine("2. Remove Item from Trade");
                    Console.WriteLine("3. Confirm Trade");
                    Console.WriteLine("4. Cancel Trade");
                }
                else
                {
                    // Display incoming trade requests
                    Console.WriteLine("Incoming Trade Requests:");
                    if (incomingRequests.Count == 0)
                    {
                        Console.WriteLine("  No pending trade requests.");
                    }
                    else
                    {
                        int index = 1;
                        foreach (var request in incomingRequests)
                        {
                            Console.WriteLine($"  {index}. From {request.InitiatorId} (Created: {request.CreatedAt:g})");
                            index++;
                        }
                    }

                    Console.WriteLine("\nOutgoing Trade Requests:");
                    if (outgoingRequests.Count == 0)
                    {
                        Console.WriteLine("  No pending trade requests.");
                    }
                    else
                    {
                        int index = 1;
                        foreach (var request in outgoingRequests)
                        {
                            Console.WriteLine($"  {index}. To {request.TargetId} (Created: {request.CreatedAt:g})");
                            index++;
                        }
                    }

                    Console.WriteLine("\nTrade Options:");
                    Console.WriteLine("1. Initiate Trade");
                    Console.WriteLine("2. Accept Trade Request");
                    Console.WriteLine("3. Decline Trade Request");
                }

                Console.WriteLine("5. Back to Main Menu");
                Console.Write("Select option: ");

                string input = Console.ReadLine()?.Trim();

                if (currentTrade != null)
                {
                    // Options for active trade
                    if (input == "1")
                    {
                        // In a real implementation, we would list the player's inventory here
                        Console.Write("Item ID: ");
                        string itemId = Console.ReadLine()?.Trim();

                        Console.Write("Item Name: ");
                        string itemName = Console.ReadLine()?.Trim();

                        Console.Write("Quantity: ");
                        if (!int.TryParse(Console.ReadLine()?.Trim(), out int quantity) || quantity <= 0)
                        {
                            Console.WriteLine("Invalid quantity.");
                            Thread.Sleep(2000);
                            continue;
                        }

                        Console.Write("Condition (0.0-1.0) [1.0]: ");
                        string condInput = Console.ReadLine()?.Trim();
                        float condition = string.IsNullOrEmpty(condInput) ? 1.0f :
                            float.TryParse(condInput, out float c) ? Math.Clamp(c, 0.0f, 1.0f) : 1.0f;

                        if (client.AddItemToTrade(itemId, itemName, quantity, condition))
                        {
                            Console.WriteLine("Item added to trade!");
                        }
                        else
                        {
                            Console.WriteLine("Failed to add item to trade.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "2")
                    {
                        var yourOffer = currentTrade.InitiatorId == client.CurrentUsername ?
                            currentTrade.InitiatorOffer : currentTrade.TargetOffer;

                        if (yourOffer.Items.Count == 0)
                        {
                            Console.WriteLine("No items to remove.");
                            Thread.Sleep(2000);
                            continue;
                        }

                        Console.Write("Enter item number to remove: ");
                        if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                            index > 0 && index <= yourOffer.Items.Count)
                        {
                            string itemId = yourOffer.Items[index - 1].ItemId;
                            if (client.RemoveItemFromTrade(itemId))
                            {
                                Console.WriteLine("Item removed from trade.");
                            }
                            else
                            {
                                Console.WriteLine("Failed to remove item from trade.");
                            }
                        }
                        else
                        {
                            Console.WriteLine("Invalid selection.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "3")
                    {
                        if (client.ConfirmTradeOffer())
                        {
                            Console.WriteLine("Trade offer confirmed!");
                        }
                        else
                        {
                            Console.WriteLine("Failed to confirm trade offer.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "4")
                    {
                        if (client.CancelTrade())
                        {
                            Console.WriteLine("Trade cancelled.");
                        }
                        else
                        {
                            Console.WriteLine("Failed to cancel trade.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "5")
                    {
                        break;
                    }
                }
                else
                {
                    // Options when no active trade
                    if (input == "1")
                    {
                        Console.Write("Enter username to trade with: ");
                        string username = Console.ReadLine()?.Trim();

                        if (!string.IsNullOrEmpty(username))
                        {
                            if (client.InitiateTrade(username))
                            {
                                Console.WriteLine("Trade request sent successfully!");
                            }
                            else
                            {
                                Console.WriteLine("Failed to send trade request.");
                            }
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "2")
                    {
                        if (incomingRequests.Count == 0)
                        {
                            Console.WriteLine("No pending trade requests to accept.");
                            Thread.Sleep(2000);
                            continue;
                        }

                        Console.Write("Enter request number to accept: ");
                        if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                            index > 0 && index <= incomingRequests.Count)
                        {
                            string tradeId = incomingRequests[index - 1].Id;
                            if (client.AcceptTradeRequest(tradeId))
                            {
                                Console.WriteLine("Trade request accepted!");
                            }
                            else
                            {
                                Console.WriteLine("Failed to accept trade request.");
                            }
                        }
                        else
                        {
                            Console.WriteLine("Invalid selection.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "3")
                    {
                        if (incomingRequests.Count == 0)
                        {
                            Console.WriteLine("No pending trade requests to decline.");
                            Thread.Sleep(2000);
                            continue;
                        }

                        Console.Write("Enter request number to decline: ");
                        if (int.TryParse(Console.ReadLine()?.Trim(), out int index) &&
                            index > 0 && index <= incomingRequests.Count)
                        {
                            string tradeId = incomingRequests[index - 1].Id;
                            if (client.DeclineTradeRequest(tradeId))
                            {
                                Console.WriteLine("Trade request declined.");
                            }
                            else
                            {
                                Console.WriteLine("Failed to decline trade request.");
                            }
                        }
                        else
                        {
                            Console.WriteLine("Invalid selection.");
                        }
                        Thread.Sleep(2000);
                    }
                    else if (input == "5")
                    {
                        break;
                    }
                }
            }
        }

        static void DownloadFilesMenu(EnhancedClient client)
        {
            try
            {
                Console.Clear();
                Console.WriteLine("Downloading file list...");

                var files = client.RequestFileList();

                Console.Clear();
                Console.WriteLine("Game Files");
                Console.WriteLine("---------------------------------");

                // Group files by directory
                var directories = files
                    .Select(f => Path.GetDirectoryName(f.RelativePath).Replace('\\', '/'))
                    .Distinct()
                    .OrderBy(d => d)
                    .ToList();

                int index = 1;
                foreach (var dir in directories)
                {
                    Console.WriteLine($"{index}. {dir}/");
                    index++;
                }

                Console.WriteLine($"{index}. Back to main menu");
                Console.Write("Select directory to browse or download: ");

                string input = Console.ReadLine()?.Trim();
                if (int.TryParse(input, out int selection) && selection >= 1 && selection <= directories.Count)
                {
                    string selectedDir = directories[selection - 1];
                    BrowseDirectory(client, selectedDir);
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }

        static void BrowseDirectory(EnhancedClient client, string directory)
        {
            try
            {
                Console.Clear();
                Console.WriteLine($"Browsing: {directory}");
                Console.WriteLine("---------------------------------");

                var files = client.RequestFileList(directory)
                    .OrderBy(f => Path.GetFileName(f.RelativePath))
                    .ToList();

                int index = 1;
                foreach (var file in files)
                {
                    string fileName = Path.GetFileName(file.RelativePath);
                    Console.WriteLine($"{index}. {fileName} ({GetFileSizeString(file.Size)})");
                    index++;
                }

                Console.WriteLine($"{index}. Download all files");
                Console.WriteLine($"{index + 1}. Back");
                Console.Write("Select file to download or action: ");

                string input = Console.ReadLine()?.Trim();
                if (int.TryParse(input, out int selection))
                {
                    if (selection >= 1 && selection <= files.Count)
                    {
                        // Download specific file
                        var fileToDownload = files[selection - 1];
                        DownloadFile(client, fileToDownload.RelativePath);
                    }
                    else if (selection == files.Count + 1)
                    {
                        // Download all files
                        DownloadMultipleFiles(client, files);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }

        static void DownloadFile(EnhancedClient client, string relativePath)
        {
            try
            {
                Console.Clear();
                Console.WriteLine($"Downloading: {relativePath}");
                Console.WriteLine("Please wait...");

                byte[] fileData = client.RequestGameFile(relativePath);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"Downloaded {GetFileSizeString(fileData.Length)} successfully!");
                Console.ResetColor();

                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Download error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }

        static void DownloadMultipleFiles(EnhancedClient client, List<GameFileInfo> files)
        {
            Console.Clear();
            Console.WriteLine($"Downloading {files.Count} files");
            Console.WriteLine("Progress: 0%");

            int totalFiles = files.Count;
            int filesDownloaded = 0;

            foreach (var file in files)
            {
                try
                {
                    int percent = (int)((double)filesDownloaded / totalFiles * 100);
                    Console.SetCursorPosition(10, 1);
                    Console.Write($"{percent}%");

                    Console.SetCursorPosition(0, 2);
                    Console.Write($"Current: {file.RelativePath.PadRight(40)}");

                    byte[] fileData = client.RequestGameFile(file.RelativePath);
                    filesDownloaded++;
                }
                catch (Exception ex)
                {
                    Console.SetCursorPosition(0, 3);
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"Error: {ex.Message}".PadRight(50));
                    Console.ResetColor();
                }
            }

            Console.SetCursorPosition(10, 1);
            Console.Write("100%");

            Console.SetCursorPosition(0, 4);
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Downloaded {filesDownloaded} of {totalFiles} files");
            Console.ResetColor();

            Console.WriteLine("Press any key to continue...");
            Console.ReadKey();
        }

        static void ChatMenu(EnhancedClient client)
        {
            Console.Clear();
            Console.WriteLine("Chat");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("Type your messages (type /exit to return to menu)");
            Console.WriteLine("---------------------------------");

            while (true)
            {
                string message = Console.ReadLine()?.Trim();

                if (message == "/exit")
                {
                    break;
                }

                if (!string.IsNullOrEmpty(message))
                {
                    // Create a chat message
                    var chatMessage = new GameMessage
                    {
                        Type = MessageType.Chat,
                        Data = new Dictionary<string, object>
                        {
                            { "message", message }
                        }
                    };

                    try
                    {
                        client.SendMessageToServer(chatMessage);
                    }
                    catch (Exception ex)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine($"Error sending message: {ex.Message}");
                        Console.ResetColor();
                    }
                }
            }
        }

        // Helper method to read password without showing characters
        static string ReadPassword()
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

            return password;
        }

        // Helper method to format file size
        static string GetFileSizeString(long byteCount)
        {
            string[] suf = { "B", "KB", "MB", "GB" };
            if (byteCount == 0)
                return "0" + suf[0];

            long bytes = Math.Abs(byteCount);
            int place = Convert.ToInt32(Math.Floor(Math.Log(bytes, 1024)));
            double num = Math.Round(bytes / Math.Pow(1024, place), 1);
            return $"{(Math.Sign(byteCount) * num).ToString("0.##")} {suf[place]}";
        }
    }
}
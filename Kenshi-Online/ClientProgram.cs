using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Client-side entry point for Kenshi Online multiplayer.
    ///
    /// This program:
    /// 1. Connects to Kenshi game (via memory bridge)
    /// 2. Connects to multiplayer server
    /// 3. Handles login and spawn
    /// 4. Synchronizes player positions
    /// 5. Renders other players in the game world
    /// </summary>
    public class ClientProgram
    {
        private static KenshiGameBridge gameBridge;
        private static EnhancedClient networkClient;
        private static MultiplayerSync multiplayerSync;
        private static ModInjector modInjector;
        private static bool isRunning;

        public static async Task Main(string[] args)
        {
            Console.Title = "Kenshi Online - Client";
            DisplayBanner();

            try
            {
                // Initialize components
                await InitializeClient();

                // Main menu loop
                await RunMainMenu();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"\nFatal error: {ex.Message}");
                Console.ResetColor();
            }
            finally
            {
                Cleanup();
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }

        private static void DisplayBanner()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
╔═══════════════════════════════════════════════╗
║         KENSHI ONLINE - MULTIPLAYER           ║
║              Client Application               ║
╚═══════════════════════════════════════════════╝
");
            Console.ResetColor();
        }

        private static async Task InitializeClient()
        {
            Console.WriteLine("Initializing Kenshi Online Client...\n");

            // Step 1: Initialize cache path
            string cachePath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "KenshiOnline", "client_cache");
            Directory.CreateDirectory(cachePath);

            // Step 2: Initialize network client
            Console.Write("Creating network client... ");
            networkClient = new EnhancedClient(cachePath);
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("OK");
            Console.ResetColor();

            // Step 3: Initialize game bridge
            Console.Write("Initializing game bridge... ");
            gameBridge = new KenshiGameBridge();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("OK");
            Console.ResetColor();

            // Step 4: Initialize mod injector
            Console.Write("Initializing mod injector... ");
            modInjector = new ModInjector();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("OK");
            Console.ResetColor();

            Console.WriteLine();
        }

        private static async Task RunMainMenu()
        {
            while (true)
            {
                DisplayMenu();

                Console.Write("\nChoice: ");
                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        await ConnectToGame();
                        break;
                    case "2":
                        await ConnectToServer();
                        break;
                    case "3":
                        await Login();
                        break;
                    case "4":
                        await SpawnPlayer();
                        break;
                    case "5":
                        DisplayStatus();
                        break;
                    case "6":
                        DisplayOtherPlayers();
                        break;
                    case "7":
                        await FriendsMenu();
                        break;
                    case "8":
                        await TrainerMenu();
                        break;
                    case "9":
                        await Disconnect();
                        return;
                    case "0":
                        await Disconnect();
                        return;
                    case "8":
                        return;
                    default:
                        Console.WriteLine("Invalid choice.");
                        break;
                }

                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
                Console.Clear();
                DisplayBanner();
            }
        }

        private static void DisplayMenu()
        {
            Console.WriteLine("=== MAIN MENU ===\n");

            // Show status
            Console.Write("Game: ");
            Console.ForegroundColor = gameBridge?.IsConnected == true ? ConsoleColor.Green : ConsoleColor.Red;
            Console.WriteLine(gameBridge?.IsConnected == true ? "CONNECTED" : "DISCONNECTED");
            Console.ResetColor();

            Console.Write("Server: ");
            Console.ForegroundColor = networkClient?.IsConnected == true ? ConsoleColor.Green : ConsoleColor.Red;
            Console.WriteLine(networkClient?.IsConnected == true ? "CONNECTED" : "DISCONNECTED");
            Console.ResetColor();

            Console.Write("Logged in: ");
            Console.ForegroundColor = networkClient?.IsLoggedIn == true ? ConsoleColor.Green : ConsoleColor.Yellow;
            Console.WriteLine(networkClient?.IsLoggedIn == true ? $"YES ({networkClient.CurrentUsername})" : "NO");
            Console.ResetColor();

            Console.Write("Sync: ");
            Console.ForegroundColor = multiplayerSync?.IsRunning == true ? ConsoleColor.Green : ConsoleColor.Yellow;
            Console.WriteLine(multiplayerSync?.IsRunning == true ? "RUNNING" : "STOPPED");
            Console.ResetColor();

            Console.WriteLine("\n--- Options ---");
            Console.WriteLine("1. Connect to Kenshi");
            Console.WriteLine("2. Connect to Server");
            Console.WriteLine("3. Login");
            Console.WriteLine("4. Spawn / Join Game");
            Console.WriteLine("5. Status");
            Console.WriteLine("6. Other Players");
            Console.WriteLine("7. Friends");
            Console.WriteLine("8. Trainer (Debug)");
            Console.WriteLine("9. Disconnect & Quit");
            Console.WriteLine("0. Exit");
            Console.WriteLine("7. Disconnect & Quit");
            Console.WriteLine("8. Exit");
        }

        private static async Task ConnectToGame()
        {
            Console.WriteLine("\n=== Connect to Kenshi ===\n");

            // Check if Kenshi is running
            var process = ModInjector.FindKenshiProcess();
            if (process == null)
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Kenshi is not running.");
                Console.ResetColor();
                Console.WriteLine("\nPlease start Kenshi first, then try again.");
                Console.WriteLine("(Load or create a game save before connecting)");
                return;
            }

            Console.WriteLine($"Found Kenshi process (PID: {process.Id})");

            // Connect game bridge
            Console.Write("Connecting to game memory... ");
            if (gameBridge.ConnectToKenshi())
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("SUCCESS!");
                Console.ResetColor();

                // Check if mod is loaded
                if (ModInjector.IsModLoaded(process))
                {
                    Console.WriteLine("Mod is already loaded.");
                }
                else
                {
                    Console.WriteLine("\nNote: For full functionality, inject the mod DLL.");
                }
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("FAILED!");
                Console.ResetColor();
                Console.WriteLine("Try running as administrator.");
            }
        }

        private static async Task ConnectToServer()
        {
            Console.WriteLine("\n=== Connect to Server ===\n");

            Console.Write("Server address [localhost]: ");
            string address = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(address))
                address = "localhost";

            Console.Write("Server port [7777]: ");
            string portStr = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portStr) ? 7777 : int.Parse(portStr);

            Console.Write("\nConnecting to {0}:{1}... ", address, port);

            try
            {
                // Note: EnhancedClient doesn't have a separate Connect method
                // Connection happens during Login
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Ready (will connect on login)");
                Console.ResetColor();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"FAILED: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static async Task Login()
        {
            Console.WriteLine("\n=== Login ===\n");

            Console.Write("Server address [localhost]: ");
            string address = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(address))
                address = "localhost";

            Console.Write("Server port [7777]: ");
            string portStr = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portStr) ? 7777 : int.Parse(portStr);

            Console.Write("Username: ");
            string username = Console.ReadLine()?.Trim();

            Console.Write("Password: ");
            string password = ReadPassword();

            if (string.IsNullOrEmpty(username) || string.IsNullOrEmpty(password))
            {
                Console.WriteLine("Username and password are required.");
                return;
            }

            Console.Write($"\nLogging in as {username}... ");

            try
            {
                bool success = networkClient.Login(address, port, username, password);

                if (success)
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("SUCCESS!");
                    Console.ResetColor();

                    Console.WriteLine($"Player ID: {networkClient.PlayerId}");
                    Console.WriteLine($"Session ID: {networkClient.SessionId?.Substring(0, Math.Min(10, networkClient.SessionId.Length))}...");

                    // Initialize multiplayer sync
                    if (multiplayerSync == null)
                    {
                        multiplayerSync = new MultiplayerSync(gameBridge, networkClient);
                        multiplayerSync.OnPlayerJoined += (id) => Console.WriteLine($"[SYNC] Player joined: {id}");
                        multiplayerSync.OnPlayerLeft += (id) => Console.WriteLine($"[SYNC] Player left: {id}");
                    }
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("FAILED!");
                    Console.ResetColor();
                    Console.WriteLine("Check username/password and try again.");
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static async Task SpawnPlayer()
        {
            Console.WriteLine("\n=== Spawn Player ===\n");

            if (!networkClient.IsLoggedIn)
            {
                Console.WriteLine("Please login first.");
                return;
            }

            if (!gameBridge.IsConnected)
            {
                Console.WriteLine("Please connect to Kenshi first.");
                return;
            }

            // Ask about group spawn
            Console.WriteLine("Spawn options:");
            Console.WriteLine("  1. Solo spawn");
            Console.WriteLine("  2. Spawn with friends (group spawn)");
            Console.Write("\nChoice [1]: ");
            string spawnChoice = Console.ReadLine()?.Trim();

            // Show available spawn locations
            Console.WriteLine("\nAvailable spawn locations:");
            // Show available spawn locations
            Console.WriteLine("Available spawn locations:");
            Console.WriteLine("  Hub, Squin, Sho-Battai, Heng, Stack, Admag");
            Console.WriteLine("  BadTeeth, Bark, Stoat, WorldsEnd, FlatsLagoon");
            Console.WriteLine("  Shark, MudTown, Mongrel, Catun, Spring, Random");
            Console.WriteLine();

            Console.Write("Spawn location [Hub]: ");
            string location = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(location))
                location = "Hub";

            try
            {
                if (spawnChoice == "2")
                {
                    // Group spawn with friends
                    var friends = networkClient.GetFriends();
                    if (friends.Count == 0)
                    {
                        Console.WriteLine("\nYou have no friends added. Add friends first from the Friends menu.");
                        Console.WriteLine("Falling back to solo spawn...\n");
                    }
                    else
                    {
                        Console.WriteLine($"\nOnline friends ({friends.Count}):");
                        for (int i = 0; i < friends.Count; i++)
                        {
                            var friend = friends[i];
                            string status = friend.IsOnline ? "(online)" : "(offline)";
                            Console.WriteLine($"  {i + 1}. {friend.Username} {status}");
                        }

                        Console.WriteLine("\nEnter friend numbers to invite (comma-separated), or 'all' for all online:");
                        Console.Write("Selection: ");
                        string selection = Console.ReadLine()?.Trim();

                        var selectedFriends = new List<string> { networkClient.PlayerId };

                        if (selection?.ToLower() == "all")
                        {
                            foreach (var f in friends.Where(f => f.IsOnline))
                                selectedFriends.Add(f.Username);
                        }
                        else if (!string.IsNullOrEmpty(selection))
                        {
                            var indices = selection.Split(',').Select(s => s.Trim());
                            foreach (var idx in indices)
                            {
                                if (int.TryParse(idx, out int num) && num > 0 && num <= friends.Count)
                                {
                                    selectedFriends.Add(friends[num - 1].Username);
                                }
                            }
                        }

                        if (selectedFriends.Count > 1)
                        {
                            Console.Write($"\nRequesting group spawn for {selectedFriends.Count} players at {location}... ");
                            networkClient.RequestGroupSpawn(selectedFriends, location);
                            Console.ForegroundColor = ConsoleColor.Green;
                            Console.WriteLine("REQUEST SENT!");
                            Console.ResetColor();
                            Console.WriteLine("Waiting for all players to be ready...");
                            StartMultiplayerSync();
                            return;
                        }
                    }
                }

                // Solo spawn
                Console.Write($"\nRequesting spawn at {location}... ");
                networkClient.RequestSpawn(location);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("REQUEST SENT!");
                Console.ResetColor();

                StartMultiplayerSync();

            Console.Write($"\nRequesting spawn at {location}... ");

            try
            {
                // Request spawn from server
                networkClient.RequestSpawn(location);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("REQUEST SENT!");
                Console.ResetColor();

                // Start multiplayer sync
                if (multiplayerSync != null && !multiplayerSync.IsRunning)
                {
                    Console.Write("Starting multiplayer sync... ");
                    if (multiplayerSync.Start(networkClient.PlayerId))
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("OK");
                        Console.ResetColor();
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        Console.WriteLine("FAILED (will retry)");
                        Console.ResetColor();
                    }
                }

                Console.WriteLine("\nYou should now be spawned in the game world!");
                Console.WriteLine("Other players will appear as you explore.");
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static void StartMultiplayerSync()
        {
            if (multiplayerSync != null && !multiplayerSync.IsRunning)
            {
                Console.Write("Starting multiplayer sync... ");
                if (multiplayerSync.Start(networkClient.PlayerId))
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("OK");
                    Console.ResetColor();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("FAILED (will retry)");
                    Console.ResetColor();
                }
            }
        }

        private static void DisplayStatus()
        {
            Console.WriteLine("\n=== Status ===\n");

            Console.WriteLine("--- Game ---");
            Console.WriteLine($"  Connected: {gameBridge?.IsConnected}");
            if (gameBridge?.KenshiProcess != null)
            {
                Console.WriteLine($"  Process ID: {gameBridge.KenshiProcess.Id}");
                Console.WriteLine($"  Process Name: {gameBridge.KenshiProcess.ProcessName}");
            }

            Console.WriteLine("\n--- Network ---");
            Console.WriteLine($"  Connected: {networkClient?.IsConnected}");
            Console.WriteLine($"  Logged In: {networkClient?.IsLoggedIn}");
            Console.WriteLine($"  Username: {networkClient?.CurrentUsername ?? "(none)"}");
            Console.WriteLine($"  Player ID: {networkClient?.PlayerId ?? "(none)"}");

            Console.WriteLine("\n--- Multiplayer Sync ---");
            Console.WriteLine($"  Running: {multiplayerSync?.IsRunning}");
            Console.WriteLine($"  Other Players: {multiplayerSync?.OtherPlayerCount ?? 0}");
        }

        private static void DisplayOtherPlayers()
        {
            Console.WriteLine("\n=== Other Players ===\n");

            if (multiplayerSync == null)
            {
                Console.WriteLine("Multiplayer sync not initialized.");
                return;
            }

            var players = multiplayerSync.GetOtherPlayers();

            if (players.Count == 0)
            {
                Console.WriteLine("No other players nearby.");
                return;
            }

            Console.WriteLine($"Found {players.Count} other player(s):\n");

            foreach (var player in players)
            {
                Console.WriteLine($"  {player.DisplayName ?? player.PlayerId}");
                if (player.Position != null)
                {
                    Console.WriteLine($"    Position: ({player.Position.X:F1}, {player.Position.Y:F1}, {player.Position.Z:F1})");
                }
                Console.WriteLine($"    Health: {player.Health:F0}");
                Console.WriteLine($"    State: {player.State}");
                Console.WriteLine($"    Spawned: {player.IsSpawned}");
                Console.WriteLine();
            }
        }

        private static async Task FriendsMenu()
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("\n=== Friends Menu ===\n");

                if (!networkClient?.IsLoggedIn == true)
                {
                    Console.WriteLine("Please login first to manage friends.");
                    return;
                }

                // Display current friends
                var friends = networkClient.GetFriends();
                var incoming = networkClient.GetIncomingFriendRequests();
                var outgoing = networkClient.GetOutgoingFriendRequests();

                Console.WriteLine($"Friends ({friends.Count}):");
                if (friends.Count == 0)
                {
                    Console.WriteLine("  (no friends yet)");
                }
                else
                {
                    foreach (var friend in friends)
                    {
                        string status = friend.IsOnline ? "[ONLINE]" : "[offline]";
                        Console.ForegroundColor = friend.IsOnline ? ConsoleColor.Green : ConsoleColor.Gray;
                        Console.WriteLine($"  - {friend.Username} {status}");
                        Console.ResetColor();
                    }
                }

                if (incoming.Count > 0)
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine($"\nPending friend requests ({incoming.Count}):");
                    foreach (var req in incoming)
                    {
                        Console.WriteLine($"  - {req}");
                    }
                    Console.ResetColor();
                }

                if (outgoing.Count > 0)
                {
                    Console.ForegroundColor = ConsoleColor.Cyan;
                    Console.WriteLine($"\nOutgoing requests ({outgoing.Count}):");
                    foreach (var req in outgoing)
                    {
                        Console.WriteLine($"  - {req}");
                    }
                    Console.ResetColor();
                }

                Console.WriteLine("\n--- Options ---");
                Console.WriteLine("1. Add friend");
                Console.WriteLine("2. Accept friend request");
                Console.WriteLine("3. Decline friend request");
                Console.WriteLine("4. Remove friend");
                Console.WriteLine("5. Block user");
                Console.WriteLine("6. Back to main menu");

                Console.Write("\nChoice: ");
                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        Console.Write("\nEnter username to add: ");
                        string addUser = Console.ReadLine()?.Trim();
                        if (!string.IsNullOrEmpty(addUser))
                        {
                            if (networkClient.SendFriendRequest(addUser))
                            {
                                Console.ForegroundColor = ConsoleColor.Green;
                                Console.WriteLine($"Friend request sent to {addUser}!");
                            }
                            else
                            {
                                Console.ForegroundColor = ConsoleColor.Red;
                                Console.WriteLine("Failed to send friend request.");
                            }
                            Console.ResetColor();
                        }
                        break;

                    case "2":
                        if (incoming.Count == 0)
                        {
                            Console.WriteLine("No pending friend requests.");
                        }
                        else
                        {
                            Console.Write("\nEnter username to accept: ");
                            string acceptUser = Console.ReadLine()?.Trim();
                            if (!string.IsNullOrEmpty(acceptUser))
                            {
                                if (networkClient.AcceptFriendRequest(acceptUser))
                                {
                                    Console.ForegroundColor = ConsoleColor.Green;
                                    Console.WriteLine($"You are now friends with {acceptUser}!");
                                }
                                else
                                {
                                    Console.ForegroundColor = ConsoleColor.Red;
                                    Console.WriteLine("Failed to accept friend request.");
                                }
                                Console.ResetColor();
                            }
                        }
                        break;

                    case "3":
                        if (incoming.Count == 0)
                        {
                            Console.WriteLine("No pending friend requests.");
                        }
                        else
                        {
                            Console.Write("\nEnter username to decline: ");
                            string declineUser = Console.ReadLine()?.Trim();
                            if (!string.IsNullOrEmpty(declineUser))
                            {
                                if (networkClient.DeclineFriendRequest(declineUser))
                                {
                                    Console.WriteLine($"Declined friend request from {declineUser}.");
                                }
                                else
                                {
                                    Console.ForegroundColor = ConsoleColor.Red;
                                    Console.WriteLine("Failed to decline friend request.");
                                    Console.ResetColor();
                                }
                            }
                        }
                        break;

                    case "4":
                        Console.Write("\nEnter username to remove: ");
                        string removeUser = Console.ReadLine()?.Trim();
                        if (!string.IsNullOrEmpty(removeUser))
                        {
                            if (networkClient.RemoveFriend(removeUser))
                            {
                                Console.WriteLine($"Removed {removeUser} from friends.");
                            }
                            else
                            {
                                Console.ForegroundColor = ConsoleColor.Red;
                                Console.WriteLine("Failed to remove friend.");
                                Console.ResetColor();
                            }
                        }
                        break;

                    case "5":
                        Console.Write("\nEnter username to block: ");
                        string blockUser = Console.ReadLine()?.Trim();
                        if (!string.IsNullOrEmpty(blockUser))
                        {
                            if (networkClient.BlockUser(blockUser))
                            {
                                Console.ForegroundColor = ConsoleColor.Yellow;
                                Console.WriteLine($"Blocked {blockUser}.");
                                Console.ResetColor();
                            }
                            else
                            {
                                Console.ForegroundColor = ConsoleColor.Red;
                                Console.WriteLine("Failed to block user.");
                                Console.ResetColor();
                            }
                        }
                        break;

                    case "6":
                        return;

                    default:
                        Console.WriteLine("Invalid choice.");
                        break;
                }

                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        private static async Task TrainerMenu()
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("\n=== Trainer / Debug Menu ===\n");

                if (!gameBridge?.IsConnected == true)
                {
                    Console.WriteLine("Please connect to Kenshi first.");
                    Console.WriteLine("\nPress any key to go back...");
                    Console.ReadKey();
                    return;
                }

                Console.WriteLine("WARNING: These options are for debugging and testing only!");
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Using these in multiplayer may cause desync or be flagged by anti-cheat.\n");
                Console.ResetColor();

                Console.WriteLine("--- Options ---");
                Console.WriteLine("1. Teleport to location");
                Console.WriteLine("2. Spawn test character");
                Console.WriteLine("3. Set player health");
                Console.WriteLine("4. View memory offsets");
                Console.WriteLine("5. Force position sync");
                Console.WriteLine("6. Dump player data");
                Console.WriteLine("7. Back to main menu");

                Console.Write("\nChoice: ");
                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        await TrainerTeleport();
                        break;

                    case "2":
                        await TrainerSpawnCharacter();
                        break;

                    case "3":
                        await TrainerSetHealth();
                        break;

                    case "4":
                        TrainerViewOffsets();
                        break;

                    case "5":
                        TrainerForceSync();
                        break;

                    case "6":
                        TrainerDumpData();
                        break;

                    case "7":
                        return;

                    default:
                        Console.WriteLine("Invalid choice.");
                        break;
                }

                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        private static async Task TrainerTeleport()
        {
            Console.WriteLine("\n=== Teleport ===\n");

            Console.WriteLine("Preset locations:");
            Console.WriteLine("  1. Hub (-5140, 0, 21200)");
            Console.WriteLine("  2. Squin (-8450, 30, 18200)");
            Console.WriteLine("  3. Stack (5750, 50, 11450)");
            Console.WriteLine("  4. Custom coordinates");

            Console.Write("\nChoice: ");
            string choice = Console.ReadLine()?.Trim();

            float x = 0, y = 0, z = 0;

            switch (choice)
            {
                case "1":
                    x = -5140; y = 0; z = 21200;
                    break;
                case "2":
                    x = -8450; y = 30; z = 18200;
                    break;
                case "3":
                    x = 5750; y = 50; z = 11450;
                    break;
                case "4":
                    Console.Write("X coordinate: ");
                    float.TryParse(Console.ReadLine(), out x);
                    Console.Write("Y coordinate: ");
                    float.TryParse(Console.ReadLine(), out y);
                    Console.Write("Z coordinate: ");
                    float.TryParse(Console.ReadLine(), out z);
                    break;
                default:
                    Console.WriteLine("Invalid choice.");
                    return;
            }

            Console.Write($"\nTeleporting to ({x}, {y}, {z})... ");

            try
            {
                var position = new Networking.Position(x, y, z);
                if (gameBridge.SetLocalPlayerPosition(position))
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("SUCCESS!");
                    Console.ResetColor();

                    // Notify server of position change
                    if (networkClient?.IsConnected == true)
                    {
                        networkClient.SendPositionUpdate(x, y, z, 0);
                    }
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("FAILED!");
                    Console.ResetColor();
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static async Task TrainerSpawnCharacter()
        {
            Console.WriteLine("\n=== Spawn Test Character ===\n");

            Console.Write("Character name [TestNPC]: ");
            string name = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(name))
                name = "TestNPC";

            Console.Write("Spawn near player? (y/n) [y]: ");
            string nearPlayer = Console.ReadLine()?.Trim().ToLower();

            try
            {
                var playerPos = gameBridge.GetLocalPlayerPosition();
                float x = playerPos?.X ?? 0;
                float y = playerPos?.Y ?? 0;
                float z = playerPos?.Z ?? 0;

                if (nearPlayer != "n")
                {
                    // Spawn 5 units away
                    x += 5;
                    z += 5;
                }
                else
                {
                    Console.Write("X coordinate: ");
                    float.TryParse(Console.ReadLine(), out x);
                    Console.Write("Y coordinate: ");
                    float.TryParse(Console.ReadLine(), out y);
                    Console.Write("Z coordinate: ");
                    float.TryParse(Console.ReadLine(), out z);
                }

                Console.Write($"\nSpawning '{name}' at ({x:F1}, {y:F1}, {z:F1})... ");

                var playerData = new Data.PlayerData
                {
                    PlayerId = $"test_{Guid.NewGuid():N}".Substring(0, 16),
                    DisplayName = name,
                    Health = 100,
                    MaxHealth = 100
                };

                var spawnPos = new Networking.Position(x, y, z);
                if (gameBridge.SpawnPlayerCharacter(playerData.PlayerId, playerData, spawnPos))
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("SUCCESS!");
                    Console.ResetColor();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("FAILED!");
                    Console.ResetColor();
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static async Task TrainerSetHealth()
        {
            Console.WriteLine("\n=== Set Player Health ===\n");

            var currentPos = gameBridge.GetLocalPlayerPosition();
            Console.WriteLine($"Current position: {currentPos?.X:F1}, {currentPos?.Y:F1}, {currentPos?.Z:F1}");

            Console.Write("\nNew health value (0-100) [100]: ");
            string healthStr = Console.ReadLine()?.Trim();
            float health = string.IsNullOrEmpty(healthStr) ? 100f : float.Parse(healthStr);

            Console.Write($"Setting health to {health}... ");

            // Note: This requires implementing SetLocalPlayerHealth in KenshiGameBridge
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("NOT IMPLEMENTED YET");
            Console.ResetColor();
            Console.WriteLine("Health modification requires additional memory offsets.");
        }

        private static void TrainerViewOffsets()
        {
            Console.WriteLine("\n=== Memory Offsets ===\n");

            try
            {
                if (!Game.RuntimeOffsets.IsInitialized)
                {
                    Console.WriteLine("RuntimeOffsets not initialized. Initializing...");
                    Game.RuntimeOffsets.Initialize();
                }

                Console.WriteLine("Current offsets loaded:");
                Console.WriteLine($"  Base Address: 0x{Game.RuntimeOffsets.BaseAddress:X}");
                Console.WriteLine($"  World Instance: 0x{Game.RuntimeOffsets.WorldInstance:X}");
                Console.WriteLine($"  Selected Character: 0x{Game.RuntimeOffsets.SelectedCharacter:X}");
                Console.WriteLine($"  Player Squad List: 0x{Game.RuntimeOffsets.PlayerSquadList:X}");
                Console.WriteLine($"  All Characters List: 0x{Game.RuntimeOffsets.AllCharactersList:X}");

                Console.WriteLine("\nCharacter Structure Offsets:");
                Console.WriteLine($"  Position: 0x{Game.RuntimeOffsets.Character.Position:X}");
                Console.WriteLine($"  Rotation: 0x{Game.RuntimeOffsets.Character.Rotation:X}");
                Console.WriteLine($"  Health: 0x{Game.RuntimeOffsets.Character.Health:X}");
                Console.WriteLine($"  Max Health: 0x{Game.RuntimeOffsets.Character.MaxHealth:X}");
                Console.WriteLine($"  Inventory: 0x{Game.RuntimeOffsets.Character.Inventory:X}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error reading offsets: {ex.Message}");
            }
        }

        private static void TrainerForceSync()
        {
            Console.WriteLine("\n=== Force Position Sync ===\n");

            if (multiplayerSync?.IsRunning != true)
            {
                Console.WriteLine("Multiplayer sync is not running.");
                return;
            }

            if (networkClient?.IsConnected != true)
            {
                Console.WriteLine("Not connected to server.");
                return;
            }

            try
            {
                var pos = gameBridge.GetLocalPlayerPosition();
                if (pos != null)
                {
                    Console.Write($"Sending position ({pos.X:F1}, {pos.Y:F1}, {pos.Z:F1})... ");
                    networkClient.SendPositionUpdate(pos.X, pos.Y, pos.Z, pos.RotY);
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("SENT!");
                    Console.ResetColor();
                }
                else
                {
                    Console.WriteLine("Could not read player position.");
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static void TrainerDumpData()
        {
            Console.WriteLine("\n=== Player Data Dump ===\n");

            try
            {
                var pos = gameBridge.GetLocalPlayerPosition();
                Console.WriteLine("Local Player:");
                Console.WriteLine($"  Position: ({pos?.X:F2}, {pos?.Y:F2}, {pos?.Z:F2})");
                Console.WriteLine($"  Rotation: ({pos?.RotX:F2}, {pos?.RotY:F2}, {pos?.RotZ:F2})");

                Console.WriteLine($"\nGame Bridge:");
                Console.WriteLine($"  Connected: {gameBridge?.IsConnected}");
                Console.WriteLine($"  Process: {gameBridge?.KenshiProcess?.ProcessName ?? "N/A"}");

                Console.WriteLine($"\nNetwork Client:");
                Console.WriteLine($"  Connected: {networkClient?.IsConnected}");
                Console.WriteLine($"  Logged In: {networkClient?.IsLoggedIn}");
                Console.WriteLine($"  Player ID: {networkClient?.PlayerId ?? "N/A"}");

                Console.WriteLine($"\nMultiplayer Sync:");
                Console.WriteLine($"  Running: {multiplayerSync?.IsRunning}");
                Console.WriteLine($"  Other Players: {multiplayerSync?.OtherPlayerCount ?? 0}");

                if (multiplayerSync?.OtherPlayerCount > 0)
                {
                    Console.WriteLine("\n  Other Player Details:");
                    foreach (var p in multiplayerSync.GetOtherPlayers())
                    {
                        Console.WriteLine($"    - {p.PlayerId}: ({p.Position?.X:F1}, {p.Position?.Y:F1}, {p.Position?.Z:F1})");
                    }
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
            }
        }

        private static async Task Disconnect()
        {
            Console.WriteLine("\nDisconnecting...");

            multiplayerSync?.Stop();
            networkClient?.Disconnect();

            Console.WriteLine("Disconnected.");
        }

        private static void Cleanup()
        {
            multiplayerSync?.Dispose();
            networkClient?.Disconnect();
            gameBridge?.Dispose();
        }

        private static string ReadPassword()
        {
            string password = "";
            ConsoleKeyInfo key;

            do
            {
                key = Console.ReadKey(true);

                if (key.Key != ConsoleKey.Backspace && key.Key != ConsoleKey.Enter)
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

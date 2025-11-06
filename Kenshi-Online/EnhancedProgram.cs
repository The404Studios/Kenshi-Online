using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Enhanced startup program with full game integration
    /// </summary>
    class EnhancedProgram
    {
        private static KenshiGameBridge gameBridge;
        private static GameStateManager gameStateManager;
        private static EnhancedServer server;
        private static EnhancedClient client;

        static void Main(string[] args)
        {
            DisplayHeader();

            Console.WriteLine("===========================================");
            Console.WriteLine("    KENSHI ONLINE - Multiplayer Edition    ");
            Console.WriteLine("===========================================");
            Console.WriteLine();
            Console.WriteLine("Select Mode:");
            Console.WriteLine("1. Start Server (Host a game)");
            Console.WriteLine("2. Start Client (Join a game)");
            Console.WriteLine("3. Start Server + Client (Solo/Testing)");
            Console.WriteLine("4. Exit");
            Console.Write("\nEnter choice: ");

            string input = Console.ReadLine()?.Trim();

            switch (input)
            {
                case "1":
                    StartServer();
                    break;
                case "2":
                    StartClient();
                    break;
                case "3":
                    StartServerAndClient();
                    break;
                case "4":
                    Environment.Exit(0);
                    break;
                default:
                    Console.WriteLine("Invalid option.");
                    break;
            }
        }

        static void DisplayHeader()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
╔═══════════════════════════════════════════════════════════════╗
║                                                               ║
║   ██╗  ██╗███████╗███╗   ██╗███████╗██╗  ██╗██╗             ║
║   ██║ ██╔╝██╔════╝████╗  ██║██╔════╝██║  ██║██║             ║
║   █████╔╝ █████╗  ██╔██╗ ██║███████╗███████║██║             ║
║   ██╔═██╗ ██╔══╝  ██║╚██╗██║╚════██║██╔══██║██║             ║
║   ██║  ██╗███████╗██║ ╚████║███████║██║  ██║██║             ║
║   ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝             ║
║                                                               ║
║            ██████╗ ███╗   ██╗██╗     ██╗███╗   ██╗███████╗  ║
║           ██╔═══██╗████╗  ██║██║     ██║████╗  ██║██╔════╝  ║
║           ██║   ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗    ║
║           ██║   ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝    ║
║           ╚██████╔╝██║ ╚████║███████╗██║██║ ╚████║███████╗  ║
║            ╚═════╝ ╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝  ║
║                                                               ║
║              Play Kenshi with your friends!                   ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
");
            Console.ResetColor();
        }

        static void StartServer()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[SERVER MODE]");
            Console.WriteLine("=============\n");

            // Step 1: Find Kenshi installation
            Console.WriteLine("STEP 1: Detecting Kenshi...");
            string kenshiPath = FindKenshiInstallationPath();

            if (string.IsNullOrEmpty(kenshiPath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Cannot start server without Kenshi!");
                Console.WriteLine("Please make sure Kenshi is installed and the game is running.");
                Console.ResetColor();
                Console.ReadKey();
                return;
            }

            // Step 2: Check if Kenshi is running
            Console.WriteLine("\nSTEP 2: Checking if Kenshi is running...");
            if (!IsKenshiRunning())
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("WARNING: Kenshi process not detected!");
                Console.WriteLine("Please start Kenshi and load a save game first.");
                Console.WriteLine("Press any key after Kenshi is running...");
                Console.ResetColor();
                Console.ReadKey();

                if (!IsKenshiRunning())
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Still can't find Kenshi process!");
                    Console.ResetColor();
                    Console.ReadKey();
                    return;
                }
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Kenshi is running!");
            Console.ResetColor();

            // Step 3: Initialize Game Bridge
            Console.WriteLine("\nSTEP 3: Connecting to Kenshi game engine...");
            gameBridge = new KenshiGameBridge();

            if (!gameBridge.ConnectToKenshi())
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Failed to connect to Kenshi!");
                Console.WriteLine("Try running as Administrator.");
                Console.ResetColor();
                Console.ReadKey();
                return;
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Connected to Kenshi game engine!");
            Console.ResetColor();

            // Step 4: Initialize Game State Manager
            Console.WriteLine("\nSTEP 4: Initializing game systems...");
            var stateSynchronizer = new StateSynchronizer();
            gameStateManager = new GameStateManager(gameBridge, stateSynchronizer);

            if (!gameStateManager.Start())
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Failed to start game state manager!");
                Console.ResetColor();
                Console.ReadKey();
                return;
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Game systems initialized!");
            Console.ResetColor();

            // Step 5: Configure Server
            Console.WriteLine("\nSTEP 5: Server configuration");
            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            Console.Write("Max players [16]: ");
            string maxPlayersInput = Console.ReadLine()?.Trim();
            int maxPlayers = string.IsNullOrEmpty(maxPlayersInput) ? 16 : int.Parse(maxPlayersInput);

            Console.Write("Server password (leave empty for public): ");
            string password = Console.ReadLine()?.Trim();

            // Step 6: Start Server
            Console.WriteLine("\nSTEP 6: Starting multiplayer server...");
            try
            {
                server = new EnhancedServer(kenshiPath);
                server.SetGameStateManager(gameStateManager);
                server.Start(port);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"\n✓ SERVER ONLINE!");
                Console.WriteLine($"✓ Listening on port: {port}");
                Console.WriteLine($"✓ Max players: {maxPlayers}");
                Console.WriteLine($"✓ Active players: 0/{maxPlayers}");
                Console.ResetColor();

                DisplayServerCommands();

                // Server command loop
                RunServerCommandLoop();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR starting server: {ex.Message}");
                Console.ResetColor();
                Console.ReadKey();
            }
        }

        static void StartClient()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[CLIENT MODE]");
            Console.WriteLine("=============\n");

            // Step 1: Check if Kenshi is running
            Console.WriteLine("STEP 1: Checking if Kenshi is running...");
            if (!IsKenshiRunning())
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("WARNING: Kenshi process not detected!");
                Console.WriteLine("For best experience, start Kenshi first.");
                Console.WriteLine("\nContinue anyway? (y/n): ");
                string continueInput = Console.ReadLine()?.Trim().ToLower();

                if (continueInput != "y" && continueInput != "yes")
                {
                    return;
                }
                Console.ResetColor();
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("✓ Kenshi is running!");
                Console.ResetColor();
            }

            // Step 2: Initialize Game Bridge (if Kenshi is running)
            if (IsKenshiRunning())
            {
                Console.WriteLine("\nSTEP 2: Connecting to Kenshi game engine...");
                gameBridge = new KenshiGameBridge();

                if (gameBridge.ConnectToKenshi())
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("✓ Connected to Kenshi game engine!");
                    Console.ResetColor();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("⚠ Could not connect to Kenshi (running as admin may help)");
                    Console.ResetColor();
                }
            }

            // Step 3: Server connection settings
            Console.WriteLine("\nSTEP 3: Server connection");
            Console.Write("Server address [localhost]: ");
            string serverAddress = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(serverAddress))
                serverAddress = "localhost";

            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            // Step 4: Initialize client
            Console.WriteLine("\nSTEP 4: Initializing client...");
            string cachePath = "./cache";
            Directory.CreateDirectory(cachePath);
            client = new EnhancedClient(cachePath);

            // Step 5: Login
            Console.WriteLine("\nSTEP 5: Account");
            Console.WriteLine("1. Login");
            Console.WriteLine("2. Register");
            Console.Write("Choice: ");
            string choice = Console.ReadLine()?.Trim();

            Console.Write("Username: ");
            string username = Console.ReadLine()?.Trim();

            Console.Write("Password: ");
            string password = ReadPassword();

            try
            {
                bool loginSuccess = false;

                if (choice == "1")
                {
                    loginSuccess = client.Login(serverAddress, port, username, password);
                }
                else if (choice == "2")
                {
                    loginSuccess = client.Register(serverAddress, port, username, password, "");
                    if (loginSuccess)
                    {
                        Thread.Sleep(500);
                        loginSuccess = client.Login(serverAddress, port, username, password);
                    }
                }

                if (!loginSuccess)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Login failed!");
                    Console.ResetColor();
                    Console.ReadKey();
                    return;
                }

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"\n✓ CONNECTED TO SERVER!");
                Console.WriteLine($"✓ Logged in as: {username}");
                Console.ResetColor();

                // Step 6: Spawn selection
                RunClientSpawnMenu(username);
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
                Console.ReadKey();
            }
        }

        static void RunClientSpawnMenu(string playerId)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine("\n=== SPAWN MENU ===");
                Console.WriteLine("1. Spawn Solo");
                Console.WriteLine("2. Spawn with Friends (Group Spawn)");
                Console.WriteLine("3. Select Spawn Location");
                Console.WriteLine("4. Disconnect");
                Console.Write("\nChoice: ");

                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        SpawnPlayerSolo(playerId);
                        break;
                    case "2":
                        SpawnWithFriends(playerId);
                        break;
                    case "3":
                        SelectSpawnLocation(playerId);
                        break;
                    case "4":
                        return;
                }
            }
        }

        static async void SpawnPlayerSolo(string playerId)
        {
            Console.WriteLine("\nSpawning player...");

            var playerData = new PlayerData
            {
                PlayerId = playerId,
                DisplayName = playerId,
                Health = 100,
                MaxHealth = 100,
                Position = new Position(),
                CurrentState = PlayerState.Idle
            };

            if (gameStateManager != null)
            {
                bool success = await gameStateManager.AddPlayer(playerId, playerData, "Hub");

                if (success)
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("✓ Successfully spawned at The Hub!");
                    Console.ResetColor();
                    RunInGameMenu(playerId);
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Failed to spawn!");
                    Console.ResetColor();
                }
            }
            else
            {
                // Send spawn request to server
                client.SendSpawnRequest("Hub");
                Console.WriteLine("Spawn request sent to server...");
                Thread.Sleep(2000);
                RunInGameMenu(playerId);
            }
        }

        static void SpawnWithFriends(string playerId)
        {
            Console.WriteLine("\n=== GROUP SPAWN ===");
            Console.WriteLine("Enter friend usernames (comma separated):");
            Console.Write("> ");
            string friendsInput = Console.ReadLine()?.Trim();

            var playerIds = new List<string> { playerId };
            if (!string.IsNullOrEmpty(friendsInput))
            {
                playerIds.AddRange(friendsInput.Split(',').Select(s => s.Trim()));
            }

            Console.WriteLine($"\nCreating group spawn for {playerIds.Count} players...");

            if (gameStateManager != null)
            {
                string groupId = gameStateManager.RequestGroupSpawn(playerIds, "Hub");
                Console.WriteLine($"Group ID: {groupId}");
                Console.WriteLine("Waiting for all players to ready up...");

                // Signal ready
                gameStateManager.PlayerReadyForGroupSpawn(groupId, playerId);

                Console.WriteLine("Press any key when all players are ready...");
                Console.ReadKey();
            }
            else
            {
                // Send group spawn request to server
                client.SendGroupSpawnRequest(playerIds, "Hub");
                Console.WriteLine("Group spawn request sent to server...");
                Thread.Sleep(2000);
            }

            RunInGameMenu(playerId);
        }

        static void SelectSpawnLocation(string playerId)
        {
            Console.WriteLine("\n=== SPAWN LOCATIONS ===");

            var locations = new List<string>
            {
                "Hub", "Squin", "Sho-Battai", "Heng", "Stack",
                "Admag", "BadTeeth", "Bark", "Stoat", "WorldsEnd",
                "FlatsLagoon", "Shark", "MudTown", "Mongrel", "Catun", "Spring"
            };

            for (int i = 0; i < locations.Count; i++)
            {
                Console.WriteLine($"{i + 1}. {locations[i]}");
            }

            Console.Write("\nSelect location: ");
            string input = Console.ReadLine()?.Trim();

            if (int.TryParse(input, out int index) && index >= 1 && index <= locations.Count)
            {
                string location = locations[index - 1];
                Console.WriteLine($"Selected: {location}");

                if (gameStateManager != null)
                {
                    _ = gameStateManager.SpawnPlayer(playerId, location);
                }
                else
                {
                    client.SendSpawnRequest(location);
                }

                Thread.Sleep(2000);
                RunInGameMenu(playerId);
            }
        }

        static void RunInGameMenu(string playerId)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine($"\n=== IN GAME - {playerId} ===");
                Console.WriteLine("1. View Player Status");
                Console.WriteLine("2. View Active Players");
                Console.WriteLine("3. Send Chat Message");
                Console.WriteLine("4. Move to Location");
                Console.WriteLine("5. Follow Player");
                Console.WriteLine("6. Respawn");
                Console.WriteLine("7. Disconnect");
                Console.Write("\nChoice: ");

                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        ViewPlayerStatus(playerId);
                        break;
                    case "2":
                        ViewActivePlayers();
                        break;
                    case "3":
                        SendChatMessage();
                        break;
                    case "7":
                        return;
                }

                if (choice != "7")
                {
                    Console.WriteLine("\nPress any key to continue...");
                    Console.ReadKey();
                }
            }
        }

        static void ViewPlayerStatus(string playerId)
        {
            if (gameStateManager != null)
            {
                var playerData = gameStateManager.GetPlayerData(playerId);
                if (playerData != null)
                {
                    Console.WriteLine($"\n--- {playerData.DisplayName} ---");
                    Console.WriteLine($"Health: {playerData.Health}/{playerData.MaxHealth}");
                    if (playerData.Position != null)
                    {
                        Console.WriteLine($"Position: ({playerData.Position.X:F1}, {playerData.Position.Y:F1}, {playerData.Position.Z:F1})");
                    }
                    Console.WriteLine($"State: {playerData.CurrentState}");
                }
            }
        }

        static void ViewActivePlayers()
        {
            if (gameStateManager != null)
            {
                var players = gameStateManager.GetAllPlayers();
                Console.WriteLine($"\n=== Active Players ({players.Count}) ===");
                foreach (var player in players)
                {
                    Console.WriteLine($"- {player.DisplayName} (HP: {player.Health}/{player.MaxHealth})");
                }
            }
        }

        static void SendChatMessage()
        {
            Console.Write("\nMessage: ");
            string message = Console.ReadLine();
            if (!string.IsNullOrEmpty(message) && client != null)
            {
                client.SendChatMessage(message);
                Console.WriteLine("Message sent!");
            }
        }

        static void RunServerCommandLoop()
        {
            while (true)
            {
                Console.Write("\nServer> ");
                string command = Console.ReadLine()?.Trim().ToLower();

                if (string.IsNullOrEmpty(command))
                    continue;

                var parts = command.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                string cmd = parts[0];

                switch (cmd)
                {
                    case "/help":
                        DisplayServerCommands();
                        break;

                    case "/status":
                        DisplayServerStatus();
                        break;

                    case "/players":
                        DisplayActivePlayers();
                        break;

                    case "/shutdown":
                    case "/stop":
                        Console.WriteLine("Shutting down server...");
                        gameStateManager?.Stop();
                        server?.Stop();
                        Environment.Exit(0);
                        break;

                    default:
                        Console.WriteLine("Unknown command. Type /help for available commands.");
                        break;
                }
            }
        }

        static void DisplayServerCommands()
        {
            Console.WriteLine("\n=== Server Commands ===");
            Console.WriteLine("/help       - Show this help");
            Console.WriteLine("/status     - Show server status");
            Console.WriteLine("/players    - List active players");
            Console.WriteLine("/shutdown   - Stop the server");
        }

        static void DisplayServerStatus()
        {
            Console.WriteLine("\n=== Server Status ===");
            Console.WriteLine($"Game State Manager: {(gameStateManager?.IsRunning == true ? "RUNNING" : "STOPPED")}");
            Console.WriteLine($"Active Players: {gameStateManager?.ActivePlayerCount ?? 0}");
            Console.WriteLine($"Game Bridge: {(gameBridge?.IsConnected == true ? "CONNECTED" : "DISCONNECTED")}");
        }

        static void DisplayActivePlayers()
        {
            if (gameStateManager != null)
            {
                var players = gameStateManager.GetAllPlayers();
                Console.WriteLine($"\n=== Active Players ({players.Count}) ===");
                foreach (var player in players)
                {
                    Console.WriteLine($"- {player.DisplayName}");
                    Console.WriteLine($"  Health: {player.Health}/{player.MaxHealth}");
                    Console.WriteLine($"  State: {player.CurrentState}");
                    if (player.Position != null)
                    {
                        Console.WriteLine($"  Position: ({player.Position.X:F1}, {player.Position.Y:F1}, {player.Position.Z:F1})");
                    }
                    Console.WriteLine();
                }
            }
        }

        static void StartServerAndClient()
        {
            Console.WriteLine("Starting server and client...");
            // Start server in background thread
            var serverThread = new Thread(() => StartServer());
            serverThread.IsBackground = true;
            serverThread.Start();

            Thread.Sleep(2000);

            // Start client
            StartClient();
        }

        #region Helpers

        static string FindKenshiInstallationPath()
        {
            string kenshiPath = GameModManager.FindKenshiInstallation();

            if (kenshiPath != null)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"✓ Found Kenshi at: {kenshiPath}");
                Console.ResetColor();
                return kenshiPath;
            }

            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("⚠ Could not auto-detect Kenshi installation.");
            Console.ResetColor();

            Console.Write("Enter Kenshi path: ");
            string userPath = Console.ReadLine()?.Trim();

            if (!string.IsNullOrEmpty(userPath) && Directory.Exists(userPath))
            {
                return userPath;
            }

            return null;
        }

        static bool IsKenshiRunning()
        {
            var processes = System.Diagnostics.Process.GetProcessesByName("kenshi_x64");
            if (processes.Length == 0)
            {
                processes = System.Diagnostics.Process.GetProcessesByName("kenshi");
            }
            return processes.Length > 0;
        }

        static string ReadPassword()
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

        #endregion
    }
}

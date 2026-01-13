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

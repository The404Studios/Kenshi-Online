using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Networking.Authority;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Enhanced startup program with game launcher and auto-injection
    /// </summary>
    class EnhancedProgram
    {
        private static GameLauncher gameLauncher;
        private static KenshiGameBridge gameBridge;
        private static GameStateManager gameStateManager;
        private static EnhancedServer server;
        private static EnhancedClient client;

        static async Task Main(string[] args)
        {
            DisplayHeader();

            // Handle command line arguments
            if (args.Length > 0)
            {
                HandleCommandLineArgs(args);
                return;
            }

            while (true)
            {
                Console.WriteLine("\n===========================================");
                Console.WriteLine("    KENSHI ONLINE - Multiplayer Edition    ");
                Console.WriteLine("===========================================");
                Console.WriteLine();
                Console.WriteLine("Select Mode:");
                Console.WriteLine("1. Launch Kenshi + Connect (Recommended)");
                Console.WriteLine("2. Start Server (Host a game)");
                Console.WriteLine("3. Start Client (Join a game)");
                Console.WriteLine("4. Inject into Running Kenshi");
                Console.WriteLine("5. Start Server + Client (Solo/Testing)");
                Console.WriteLine("6. Build Mod DLL");
                Console.WriteLine("7. Exit");
                Console.Write("\nEnter choice: ");

                string input = Console.ReadLine()?.Trim();

                switch (input)
                {
                    case "1":
                        await LaunchAndConnect();
                        break;
                    case "2":
                        await StartServer();
                        break;
                    case "3":
                        await StartClient();
                        break;
                    case "4":
                        InjectIntoRunning();
                        break;
                    case "5":
                        await StartServerAndClient();
                        break;
                    case "6":
                        BuildModDll();
                        break;
                    case "7":
                        Environment.Exit(0);
                        break;
                    default:
                        Console.WriteLine("Invalid option.");
                        break;
                }
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
║           Press INSERT in-game to open the overlay            ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
");
            Console.ResetColor();
        }

        static void HandleCommandLineArgs(string[] args)
        {
            string command = args[0].ToLower();

            switch (command)
            {
                case "--launch":
                case "-l":
                    LaunchAndConnect().GetAwaiter().GetResult();
                    break;
                case "--server":
                case "-s":
                    StartServer().GetAwaiter().GetResult();
                    break;
                case "--client":
                case "-c":
                    StartClient().GetAwaiter().GetResult();
                    break;
                case "--inject":
                case "-i":
                    InjectIntoRunning();
                    break;
                case "--build":
                case "-b":
                    BuildModDll();
                    break;
                case "--help":
                case "-h":
                    PrintHelp();
                    break;
                default:
                    Console.WriteLine($"Unknown command: {command}");
                    PrintHelp();
                    break;
            }
        }

        static void PrintHelp()
        {
            Console.WriteLine("Kenshi Online - Command Line Usage:");
            Console.WriteLine("  --launch, -l    Launch Kenshi with mod injection");
            Console.WriteLine("  --server, -s    Start as server");
            Console.WriteLine("  --client, -c    Start as client");
            Console.WriteLine("  --inject, -i    Inject into running Kenshi");
            Console.WriteLine("  --build, -b     Build the mod DLL");
            Console.WriteLine("  --help, -h      Show this help");
        }

        static async Task LaunchAndConnect()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[LAUNCH AND CONNECT MODE]");
            Console.WriteLine("=========================\n");

            // Step 1: Find Kenshi
            Console.WriteLine("STEP 1: Locating Kenshi...");
            string kenshiExe = GameLauncher.FindKenshiExecutable();

            if (string.IsNullOrEmpty(kenshiExe))
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Could not auto-detect Kenshi installation.");
                Console.ResetColor();
                Console.Write("Enter path to Kenshi executable: ");
                kenshiExe = Console.ReadLine()?.Trim();

                if (string.IsNullOrEmpty(kenshiExe) || !File.Exists(kenshiExe))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Invalid Kenshi path!");
                    Console.ResetColor();
                    Console.ReadKey();
                    return;
                }
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Found Kenshi: {kenshiExe}");
            Console.ResetColor();

            // Step 2: Find mod DLL
            Console.WriteLine("\nSTEP 2: Locating mod DLL...");
            string modDll = GameLauncher.GetDefaultModDllPath();

            if (!File.Exists(modDll))
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Mod DLL not found. Would you like to build it? (y/n): ");
                Console.ResetColor();

                if (Console.ReadLine()?.Trim().ToLower() == "y")
                {
                    if (!BuildModDllInternal())
                    {
                        Console.ReadKey();
                        return;
                    }
                    modDll = GameLauncher.GetDefaultModDllPath();
                }

                if (!File.Exists(modDll))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Mod DLL not found!");
                    Console.ResetColor();
                    Console.ReadKey();
                    return;
                }
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Found mod DLL: {modDll}");
            Console.ResetColor();

            // Step 3: Launch Kenshi
            Console.WriteLine("\nSTEP 3: Launching Kenshi with mod injection...");

            gameLauncher = new GameLauncher();
            gameLauncher.OnLogMessage += msg => Console.WriteLine(msg);
            gameLauncher.OnStateChanged += state =>
            {
                switch (state)
                {
                    case GameLauncher.LaunchState.CreatingProcess:
                        Console.WriteLine("Creating Kenshi process...");
                        break;
                    case GameLauncher.LaunchState.InjectingMod:
                        Console.WriteLine("Injecting multiplayer mod...");
                        break;
                    case GameLauncher.LaunchState.ResumingProcess:
                        Console.WriteLine("Starting game...");
                        break;
                    case GameLauncher.LaunchState.WaitingForGame:
                        Console.WriteLine("Waiting for game window...");
                        break;
                    case GameLauncher.LaunchState.Running:
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("\nKenshi launched successfully!");
                        Console.ResetColor();
                        break;
                    case GameLauncher.LaunchState.Error:
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("\nFailed to launch Kenshi!");
                        Console.ResetColor();
                        break;
                }
            };

            bool success = await gameLauncher.LaunchGameAsync(kenshiExe, modDll);

            if (success)
            {
                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine("\n========================================");
                Console.WriteLine("  KENSHI ONLINE IS NOW RUNNING!");
                Console.WriteLine("========================================");
                Console.WriteLine("\n  Press INSERT in-game to open the");
                Console.WriteLine("  multiplayer overlay menu.");
                Console.WriteLine("\n  From there you can:");
                Console.WriteLine("  - Connect to a server");
                Console.WriteLine("  - View online players");
                Console.WriteLine("  - Chat with other players");
                Console.WriteLine("========================================");
                Console.ResetColor();

                Console.WriteLine("\nPress any key to return to main menu...");
                Console.WriteLine("(Kenshi will keep running)");
                Console.ReadKey();
            }
            else
            {
                Console.WriteLine("\nPress any key to return to main menu...");
                Console.ReadKey();
            }
        }

        static void InjectIntoRunning()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[INJECT INTO RUNNING KENSHI]");
            Console.WriteLine("============================\n");

            if (!GameLauncher.IsKenshiRunning())
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Kenshi is not running!");
                Console.WriteLine("Please start Kenshi first, then try again.");
                Console.ResetColor();
                Console.ReadKey();
                return;
            }

            Console.WriteLine("Found running Kenshi process.");

            string modDll = GameLauncher.GetDefaultModDllPath();

            if (!File.Exists(modDll))
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Mod DLL not found.");
                Console.ResetColor();
                Console.Write("Enter path to mod DLL: ");
                modDll = Console.ReadLine()?.Trim();

                if (string.IsNullOrEmpty(modDll) || !File.Exists(modDll))
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("ERROR: Invalid DLL path!");
                    Console.ResetColor();
                    Console.ReadKey();
                    return;
                }
            }

            Console.WriteLine($"Mod DLL: {modDll}");
            Console.WriteLine("\nInjecting mod...");

            gameLauncher = new GameLauncher();
            gameLauncher.OnLogMessage += msg => Console.WriteLine(msg);

            bool success = gameLauncher.AttachAndInject(modDll);

            if (success)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("\nMod injected successfully!");
                Console.WriteLine("Press INSERT in-game to open the overlay.");
                Console.ResetColor();
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nFailed to inject mod!");
                Console.WriteLine("Make sure you're running as Administrator.");
                Console.ResetColor();
            }

            Console.WriteLine("\nPress any key to continue...");
            Console.ReadKey();
        }

        static async Task StartServer()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[SERVER MODE]");
            Console.WriteLine("=============\n");

            // Find Kenshi installation
            Console.WriteLine("STEP 1: Detecting Kenshi...");
            string kenshiPath = FindKenshiInstallationPath();

            if (string.IsNullOrEmpty(kenshiPath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Cannot start server without Kenshi installation path!");
                Console.ResetColor();
                Console.ReadKey();
                return;
            }

            // Check if Kenshi is running
            Console.WriteLine("\nSTEP 2: Checking if Kenshi is running...");
            if (!GameLauncher.IsKenshiRunning())
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Kenshi is not running.");
                Console.WriteLine("Would you like to launch Kenshi with the mod? (y/n): ");
                Console.ResetColor();

                if (Console.ReadLine()?.Trim().ToLower() == "y")
                {
                    string kenshiExe = GameLauncher.FindKenshiExecutable();
                    string modDll = GameLauncher.GetDefaultModDllPath();

                    if (!string.IsNullOrEmpty(kenshiExe) && File.Exists(modDll))
                    {
                        gameLauncher = new GameLauncher();
                        await gameLauncher.LaunchGameAsync(kenshiExe, modDll);
                    }
                }
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Proceeding with server setup...");
            Console.ResetColor();

            // Initialize Game Bridge if Kenshi is running
            if (GameLauncher.IsKenshiRunning())
            {
                Console.WriteLine("\nSTEP 3: Connecting to Kenshi game engine...");
                gameBridge = new KenshiGameBridge();

                if (gameBridge.ConnectToKenshi())
                {
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("Connected to Kenshi!");
                    Console.ResetColor();

                    // Initialize Game State Manager
                    var stateSynchronizer = new StateSynchronizer();
                    gameStateManager = new GameStateManager(gameBridge, stateSynchronizer);
                    gameStateManager.Start();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("Could not connect to Kenshi (running as admin may help)");
                    Console.ResetColor();
                }
            }

            // Server configuration
            Console.WriteLine("\nSTEP 4: Server configuration");
            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            Console.Write("Max players [16]: ");
            string maxPlayersInput = Console.ReadLine()?.Trim();
            int maxPlayers = string.IsNullOrEmpty(maxPlayersInput) ? 16 : int.Parse(maxPlayersInput);

            Console.Write("Server password (leave empty for public): ");
            string password = Console.ReadLine()?.Trim();

            // World configuration
            Console.Write("World name [default]: ");
            string worldName = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(worldName))
                worldName = "default";

            // Start Server
            Console.WriteLine("\nSTEP 5: Starting multiplayer server...");
            try
            {
                server = new EnhancedServer(kenshiPath);

                // Initialize game state manager with save system integration
                if (gameStateManager != null)
                {
                    // Use the server's ServerContext for save system integration
                    // This ensures the authority system and save system share state
                    server.SetGameStateManager(gameStateManager, server.Context, worldName);

                    Console.ForegroundColor = ConsoleColor.Cyan;
                    Console.WriteLine($"Save system initialized for world: {worldName}");
                    Console.ResetColor();
                }

                server.Start(port);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"\nSERVER ONLINE!");
                Console.WriteLine($"Port: {port}");
                Console.WriteLine($"Max players: {maxPlayers}");
                Console.WriteLine($"World: {worldName}");
                Console.ResetColor();

                DisplayServerCommands();
                await RunServerCommandLoop();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR starting server: {ex.Message}");
                Console.ResetColor();
                Console.ReadKey();
            }
        }

        static async Task StartClient()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[CLIENT MODE]");
            Console.WriteLine("=============\n");

            // Check if Kenshi is running
            Console.WriteLine("STEP 1: Checking if Kenshi is running...");
            if (!GameLauncher.IsKenshiRunning())
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Kenshi is not running.");
                Console.WriteLine("For the best experience, use 'Launch Kenshi + Connect' option instead.");
                Console.WriteLine("\nContinue anyway? (y/n): ");
                Console.ResetColor();

                if (Console.ReadLine()?.Trim().ToLower() != "y")
                    return;
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("Kenshi is running!");
                Console.ResetColor();

                // Initialize Game Bridge
                gameBridge = new KenshiGameBridge();
                if (gameBridge.ConnectToKenshi())
                {
                    Console.WriteLine("Connected to Kenshi game engine.");
                }
            }

            // Server connection settings
            Console.WriteLine("\nSTEP 2: Server connection");
            Console.Write("Server address [localhost]: ");
            string serverAddress = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(serverAddress))
                serverAddress = "localhost";

            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);

            // Initialize client
            Console.WriteLine("\nSTEP 3: Initializing client...");
            string cachePath = "./cache";
            Directory.CreateDirectory(cachePath);
            client = new EnhancedClient(cachePath);

            // Login
            Console.WriteLine("\nSTEP 4: Account");
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
                        await Task.Delay(500);
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
                Console.WriteLine($"\nCONNECTED TO SERVER!");
                Console.WriteLine($"Logged in as: {username}");
                Console.ResetColor();

                // Run client menu
                await RunClientMenu(username);
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"ERROR: {ex.Message}");
                Console.ResetColor();
                Console.ReadKey();
            }
        }

        static void BuildModDll()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("\n[BUILD MOD DLL]");
            Console.WriteLine("===============\n");

            BuildModDllInternal();

            Console.WriteLine("\nPress any key to continue...");
            Console.ReadKey();
        }

        static bool BuildModDllInternal()
        {
            string modSourcePath = Path.Combine(Directory.GetCurrentDirectory(), "..", "KenshiOnlineMod");
            if (!Directory.Exists(modSourcePath))
            {
                modSourcePath = Path.Combine(Directory.GetCurrentDirectory(), "KenshiOnlineMod");
            }

            if (!Directory.Exists(modSourcePath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Could not find KenshiOnlineMod source directory!");
                Console.ResetColor();
                return false;
            }

            string buildPath = Path.Combine(modSourcePath, "build");
            Console.WriteLine($"Source: {modSourcePath}");
            Console.WriteLine($"Build: {buildPath}");

            var injector = new ModInjector();
            injector.OnLogMessage += msg => Console.WriteLine(msg);

            bool success = injector.BuildMod(modSourcePath, buildPath);

            if (success)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("\nMod DLL built successfully!");
                Console.WriteLine($"Output: {Path.Combine(buildPath, "bin", "KenshiOnlineMod.dll")}");
                Console.ResetColor();
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nFailed to build mod DLL!");
                Console.WriteLine("Make sure CMake and a C++ compiler are installed.");
                Console.ResetColor();
            }

            return success;
        }

        static async Task RunClientMenu(string username)
        {
            while (true)
            {
                Console.Clear();
                Console.WriteLine($"\n=== KENSHI ONLINE - {username} ===");
                Console.WriteLine("1. View Status");
                Console.WriteLine("2. View Players");
                Console.WriteLine("3. Send Chat");
                Console.WriteLine("4. Disconnect");
                Console.Write("\nChoice: ");

                string choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        Console.WriteLine("\nClient connected. Use in-game overlay for full functionality.");
                        Console.WriteLine("Press INSERT in-game to open overlay.");
                        break;
                    case "2":
                        if (gameStateManager != null)
                        {
                            var players = gameStateManager.GetAllPlayers();
                            Console.WriteLine($"\nActive Players ({players.Count}):");
                            foreach (var player in players)
                            {
                                Console.WriteLine($"- {player.DisplayName}");
                            }
                        }
                        else
                        {
                            Console.WriteLine("\nUse in-game overlay to view players.");
                        }
                        break;
                    case "3":
                        Console.Write("Message: ");
                        string message = Console.ReadLine();
                        if (!string.IsNullOrEmpty(message))
                        {
                            client.SendChatMessage(message);
                            Console.WriteLine("Message sent!");
                        }
                        break;
                    case "4":
                        return;
                }

                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        static async Task RunServerCommandLoop()
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
                    case "/save":
                        await ForceSaveWorld();
                        break;
                    case "/saves":
                        DisplaySaveStatus();
                        break;
                    case "/shutdown":
                    case "/stop":
                        Console.WriteLine("Saving world state...");
                        await ForceSaveWorld();
                        Console.WriteLine("Shutting down server...");
                        gameStateManager?.Stop();
                        server?.Stop();
                        return;
                    case "/clear":
                        Console.Clear();
                        break;
                    default:
                        Console.WriteLine("Unknown command. Type /help for available commands.");
                        break;
                }
            }
        }

        static async Task ForceSaveWorld()
        {
            Console.WriteLine("Forcing world save...");
            try
            {
                if (gameStateManager != null)
                {
                    await gameStateManager.ForceSaveAsync();
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine("World saved successfully!");
                    Console.ResetColor();
                }
                else
                {
                    Console.ForegroundColor = ConsoleColor.Yellow;
                    Console.WriteLine("Game state manager not initialized.");
                    Console.ResetColor();
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Save error: {ex.Message}");
                Console.ResetColor();
            }
        }

        static void DisplaySaveStatus()
        {
            Console.WriteLine("\n=== Save System Status ===");
            if (gameStateManager?.WorldSave != null)
            {
                var worldSave = gameStateManager.WorldSave;
                Console.WriteLine($"World Loaded: {worldSave.IsLoaded}");
                if (worldSave.WorldSave != null)
                {
                    Console.WriteLine($"World ID: {worldSave.WorldSave.WorldId}");
                    Console.WriteLine($"Save Version: {worldSave.WorldSave.SaveVersion}");
                    Console.WriteLine($"Created: {DateTimeOffset.FromUnixTimeMilliseconds(worldSave.WorldSave.CreatedAt):g}");
                    Console.WriteLine($"NPCs Tracked: {worldSave.WorldSave.NPCStates?.Count ?? 0}");
                    Console.WriteLine($"Buildings: {worldSave.WorldSave.Buildings?.Count ?? 0}");
                    Console.WriteLine($"World Events: {worldSave.WorldSave.WorldEvents?.Count ?? 0}");
                }
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("Save system not initialized.");
                Console.ResetColor();
            }
        }

        static void DisplayServerCommands()
        {
            Console.WriteLine("\n=== Server Commands ===");
            Console.WriteLine("/help       - Show this help");
            Console.WriteLine("/status     - Show server status");
            Console.WriteLine("/players    - List active players");
            Console.WriteLine("/save       - Force save world state");
            Console.WriteLine("/saves      - Show save system status");
            Console.WriteLine("/clear      - Clear console");
            Console.WriteLine("/shutdown   - Save and stop the server");
        }

        static void DisplayServerStatus()
        {
            Console.WriteLine("\n=== Server Status ===");
            Console.WriteLine($"Game State Manager: {(gameStateManager?.IsRunning == true ? "RUNNING" : "STOPPED")}");
            Console.WriteLine($"Active Players: {gameStateManager?.ActivePlayerCount ?? 0}");
            Console.WriteLine($"Game Bridge: {(gameBridge?.IsConnected == true ? "CONNECTED" : "DISCONNECTED")}");
            Console.WriteLine($"Save System: {(gameStateManager?.WorldSave?.IsLoaded == true ? "LOADED" : "NOT LOADED")}");
            if (gameStateManager?.WorldSave?.WorldSave != null)
            {
                Console.WriteLine($"World: {gameStateManager.WorldSave.WorldSave.WorldId}");
            }
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
                }
            }
            else
            {
                Console.WriteLine("Game state manager not initialized.");
            }
        }

        static async Task StartServerAndClient()
        {
            Console.WriteLine("Starting server and client...");

            // Start server in background
            var serverTask = Task.Run(() => StartServer());
            await Task.Delay(2000);

            // Start client
            await StartClient();
        }

        #region Helpers

        static string FindKenshiInstallationPath()
        {
            string kenshiPath = GameModManager.FindKenshiInstallation();

            if (kenshiPath != null)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"Found Kenshi at: {kenshiPath}");
                Console.ResetColor();
                return kenshiPath;
            }

            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("Could not auto-detect Kenshi installation.");
            Console.ResetColor();

            Console.Write("Enter Kenshi path: ");
            string userPath = Console.ReadLine()?.Trim();

            if (!string.IsNullOrEmpty(userPath) && Directory.Exists(userPath))
            {
                return userPath;
            }

            return null;
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

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiOnline.Core.Synchronization;
using KenshiOnline.Core.Session;
using KenshiOnline.Core.Admin;
using KenshiOnline.Core.Chat;
using KenshiOnline.Core.Squad;
using KenshiOnline.Core.Social;
using KenshiOnline.Core.Trading;

namespace KenshiOnline.Launcher
{
    class Program
    {
        private static Config _config = new Config();
        private static readonly string ConfigPath = "kenshi_online.json";
        private static readonly string LogPath = "kenshi_online.log";
        private static CancellationTokenSource? _serverCts;
        private static CancellationTokenSource? _clientCts;
        private static Task? _serverTask;
        private static Task? _clientTask;
        private static readonly object _logLock = new object();

        static async Task Main(string[] args)
        {
            try
            {
                Console.OutputEncoding = System.Text.Encoding.UTF8;

                // Initialize system
                InitializeSystem();
                LoadConfig();

                // Check for command line mode
                if (args.Length > 0)
                {
                    await HandleCommandLine(args);
                    return;
                }

                // Interactive mode
                await RunInteractiveMode();
            }
            catch (Exception ex)
            {
                LogError($"Fatal error: {ex.Message}");
                LogError(ex.StackTrace ?? "No stack trace");
                Console.WriteLine("\nPress any key to exit...");
                Console.ReadKey();
            }
        }

        static void InitializeSystem()
        {
            try
            {
                // Get the executable directory
                var exeDir = AppDomain.CurrentDomain.BaseDirectory;
                Directory.SetCurrentDirectory(exeDir);

                LogInfo($"Initializing Kenshi Online v2.0");
                LogInfo($"Working directory: {Directory.GetCurrentDirectory()}");

                // Create necessary directories
                EnsureDirectoryExists("logs");
                EnsureDirectoryExists("saves");
                EnsureDirectoryExists("config");

                LogInfo("System initialized successfully");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Failed to initialize system: {ex.Message}");
                throw;
            }
        }

        static void EnsureDirectoryExists(string path)
        {
            try
            {
                if (!Directory.Exists(path))
                {
                    Directory.CreateDirectory(path);
                    LogInfo($"Created directory: {path}");
                }
            }
            catch (Exception ex)
            {
                LogError($"Failed to create directory '{path}': {ex.Message}");
            }
        }

        static void LogInfo(string message)
        {
            lock (_logLock)
            {
                var timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
                var logMessage = $"[{timestamp}] [INFO] {message}";
                Console.WriteLine(logMessage);

                try
                {
                    File.AppendAllText(LogPath, logMessage + Environment.NewLine);
                }
                catch { }
            }
        }

        static void LogError(string message)
        {
            lock (_logLock)
            {
                var timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
                var logMessage = $"[{timestamp}] [ERROR] {message}";
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine(logMessage);
                Console.ResetColor();

                try
                {
                    File.AppendAllText(LogPath, logMessage + Environment.NewLine);
                }
                catch { }
            }
        }

        static async Task RunInteractiveMode()
        {
            while (true)
            {
                try
                {
                    Console.Clear();
                    ShowBanner();
                    ShowMainMenu();

                    var key = Console.ReadKey(true);
                    Console.Clear();

                    switch (key.KeyChar)
                    {
                        case '1':
                            await RunSoloMode();
                            break;
                        case '2':
                            await HostServer();
                            break;
                        case '3':
                            await JoinServer();
                            break;
                        case '4':
                            ShowSettings();
                            break;
                        case '5':
                            ShowServerHistory();
                            break;
                        case '6':
                            ShowQuickStart();
                            break;
                        case '7':
                            LogInfo("Application exiting");
                            Console.WriteLine("Thank you for playing Kenshi Online!");
                            return;
                        default:
                            break;
                    }
                }
                catch (Exception ex)
                {
                    LogError($"Menu error: {ex.Message}");
                    Console.WriteLine("\nPress any key to continue...");
                    Console.ReadKey();
                }
            }
        }

        static void ShowBanner()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                                                              ║");
            Console.WriteLine("║          KENSHI ONLINE - Unified Launcher v2.0               ║");
            Console.WriteLine("║                                                              ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.ResetColor();
            Console.WriteLine();
        }

        static void ShowMainMenu()
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("  PLAY MODES:");
            Console.ResetColor();
            Console.WriteLine("    [1] 🎮 Solo Mode         - Play alone (local server)");
            Console.WriteLine("    [2] 🌐 Host Server       - Host for friends");
            Console.WriteLine("    [3] 🤝 Join Server       - Join a friend");
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("  OPTIONS:");
            Console.ResetColor();
            Console.WriteLine("    [4] ⚙️  Settings          - Configure launcher");
            Console.WriteLine("    [5] 📜 Server History    - Recent servers");
            Console.WriteLine("    [6] 📖 Quick Start       - How to play");
            Console.WriteLine("    [7] 🚪 Exit");
            Console.WriteLine();

            if (_serverTask != null || _clientTask != null)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                if (_serverTask != null && !_serverTask.IsCompleted)
                    Console.WriteLine("  ⚡ Server: RUNNING");
                if (_clientTask != null && !_clientTask.IsCompleted)
                    Console.WriteLine("  ⚡ Client: RUNNING");
                Console.ResetColor();
                Console.WriteLine();
            }

            Console.Write("Select option: ");
        }

        static async Task RunSoloMode()
        {
            try
            {
                Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
                Console.WriteLine("║                    SOLO MODE                                 ║");
                Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
                Console.WriteLine();

                LogInfo("Starting Solo Mode");
                Console.WriteLine("Starting local server and client...");
                Console.WriteLine();

                // Start local server on 127.0.0.1
                _serverCts = new CancellationTokenSource();
                _serverTask = Task.Run(() => RunServer("127.0.0.1", 7777, _serverCts.Token));

                // Wait for server to start
                await Task.Delay(2000);

                // Start client
                _clientCts = new CancellationTokenSource();
                _clientTask = Task.Run(() => RunClient("127.0.0.1", 7777, _clientCts.Token));

                // Wait for client to connect
                await Task.Delay(1000);

                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("✓ Solo mode active!");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  Server Address: 127.0.0.1:7777");
                Console.WriteLine();
                Console.WriteLine("You can now:");
                Console.WriteLine("  1. Start Kenshi game");
                Console.WriteLine("  2. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
                Console.WriteLine("  3. The plugin will automatically connect to 127.0.0.1:7777");
                Console.WriteLine("  4. Play!");
                Console.WriteLine();
                Console.WriteLine("Press any key to stop solo mode...");
                Console.ReadKey(true);

                LogInfo("Stopping Solo Mode");

                // Cleanup
                _clientCts?.Cancel();
                _serverCts?.Cancel();

                try
                {
                    await Task.WhenAll(_serverTask ?? Task.CompletedTask, _clientTask ?? Task.CompletedTask);
                }
                catch (OperationCanceledException)
                {
                    // Expected when canceling
                }
                catch (Exception ex)
                {
                    LogError($"Error stopping solo mode: {ex.Message}");
                }

                _serverTask = null;
                _clientTask = null;

                LogInfo("Solo Mode stopped");
            }
            catch (Exception ex)
            {
                LogError($"Solo mode error: {ex.Message}");
                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        static async Task HostServer()
        {
            try
            {
                Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
                Console.WriteLine("║                    HOST SERVER                               ║");
                Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
                Console.WriteLine();

                // Auto-detect local IP
                var localIP = GetLocalIPAddress();
                Console.WriteLine($"Your local IP: {localIP}");
                Console.WriteLine();

                Console.Write($"Port (default {_config.DefaultPort}): ");
                var portInput = Console.ReadLine();
                var port = string.IsNullOrWhiteSpace(portInput) ? _config.DefaultPort : int.Parse(portInput);

                Console.Write($"Max players (default {_config.MaxPlayers}): ");
                var maxInput = Console.ReadLine();
                var maxPlayers = string.IsNullOrWhiteSpace(maxInput) ? _config.MaxPlayers : int.Parse(maxInput);

                Console.Write("Server name: ");
                var serverName = Console.ReadLine();
                if (string.IsNullOrWhiteSpace(serverName))
                    serverName = "Kenshi Online Server";

                Console.WriteLine();
                LogInfo($"Starting server: {serverName} on port {port}");
                Console.WriteLine("Starting server...");
                Console.WriteLine();

                _serverCts = new CancellationTokenSource();
                _serverTask = Task.Run(() => RunServer("0.0.0.0", port, _serverCts.Token, serverName, maxPlayers));

                await Task.Delay(1500);

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("✓ Server is running!");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("═══════════════════════════════════════════════════════════════");
                Console.WriteLine("  SHARE THIS WITH FRIENDS:");
                Console.WriteLine("═══════════════════════════════════════════════════════════════");
                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine($"  Local Network: {localIP}:{port}");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  Connection string:");
                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine($"  kenshi://{localIP}:{port}/{serverName.Replace(" ", "%20")}");
                Console.ResetColor();
                Console.WriteLine("═══════════════════════════════════════════════════════════════");
                Console.WriteLine();
                Console.WriteLine($"ℹ️  Make sure to forward port {port} in your router!");
                Console.WriteLine("ℹ️  Get your public IP from: https://whatismyip.com");
                Console.WriteLine();
                Console.WriteLine("Server will run in the background.");
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey(true);
            }
            catch (Exception ex)
            {
                LogError($"Host server error: {ex.Message}");
                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        static async Task JoinServer()
        {
            try
            {
                Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
                Console.WriteLine("║                    JOIN SERVER                               ║");
                Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
                Console.WriteLine();

                if (_config.ServerHistory.Any())
                {
                    Console.WriteLine("Recent servers:");
                    for (int i = 0; i < Math.Min(5, _config.ServerHistory.Count); i++)
                    {
                        Console.WriteLine($"  [{i + 1}] {_config.ServerHistory[i]}");
                    }
                    Console.WriteLine();
                }

                Console.Write("Server IP:Port (or paste connection string): ");
                var input = Console.ReadLine() ?? "";

                string ip;
                int port;

                // Parse connection string
                if (input.StartsWith("kenshi://"))
                {
                    var uri = new Uri(input);
                    ip = uri.Host;
                    port = uri.Port > 0 ? uri.Port : _config.DefaultPort;
                    Console.WriteLine($"Connecting to: {ip}:{port}");
                }
                else if (int.TryParse(input, out int historyIndex) && historyIndex > 0 && historyIndex <= _config.ServerHistory.Count)
                {
                    var server = _config.ServerHistory[historyIndex - 1];
                    var parts = server.Split(':');
                    ip = parts[0];
                    port = int.Parse(parts[1]);
                }
                else
                {
                    var parts = input.Split(':');
                    ip = parts[0];
                    port = parts.Length > 1 ? int.Parse(parts[1]) : _config.DefaultPort;
                }

                Console.WriteLine();
                LogInfo($"Connecting to {ip}:{port}");
                Console.WriteLine($"Connecting to {ip}:{port}...");
                Console.WriteLine();

                _clientCts = new CancellationTokenSource();
                _clientTask = Task.Run(() => RunClient(ip, port, _clientCts.Token));

                await Task.Delay(1500);

                // Add to history
                var serverAddress = $"{ip}:{port}";
                _config.ServerHistory.Remove(serverAddress);
                _config.ServerHistory.Insert(0, serverAddress);
                if (_config.ServerHistory.Count > 10)
                    _config.ServerHistory.RemoveAt(_config.ServerHistory.Count - 1);
                SaveConfig();

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("✓ Connected to server!");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("You can now:");
                Console.WriteLine("  1. Start Kenshi game");
                Console.WriteLine("  2. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
                Console.WriteLine("  3. Play!");
                Console.WriteLine();
                Console.WriteLine("Client will run in the background.");
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey(true);
            }
            catch (Exception ex)
            {
                LogError($"Join server error: {ex.Message}");
                Console.WriteLine("\nPress any key to continue...");
                Console.ReadKey();
            }
        }

        static void ShowSettings()
        {
            while (true)
            {
                try
                {
                    Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
                    Console.WriteLine("║                      SETTINGS                                ║");
                    Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
                    Console.WriteLine();
                    Console.WriteLine($"  [1] Default Port: {_config.DefaultPort}");
                    Console.WriteLine($"  [2] Max Players: {_config.MaxPlayers}");
                    Console.WriteLine($"  [3] Player Name: {_config.PlayerName}");
                    Console.WriteLine($"  [4] Auto-connect on startup: {_config.AutoConnect}");
                    Console.WriteLine($"  [5] Show FPS overlay: {_config.ShowFPS}");
                    Console.WriteLine();
                    Console.WriteLine("  [S] Save settings");
                    Console.WriteLine("  [B] Back to main menu");
                    Console.WriteLine();
                    Console.Write("Select option: ");

                    var key = Console.ReadKey(true);
                    Console.Clear();

                    switch (key.KeyChar)
                    {
                        case '1':
                            Console.Write("New default port: ");
                            if (int.TryParse(Console.ReadLine(), out int port))
                                _config.DefaultPort = port;
                            break;
                        case '2':
                            Console.Write("Max players (1-32): ");
                            if (int.TryParse(Console.ReadLine(), out int max))
                                _config.MaxPlayers = Math.Clamp(max, 1, 32);
                            break;
                        case '3':
                            Console.Write("Your player name: ");
                            _config.PlayerName = Console.ReadLine() ?? "Player";
                            break;
                        case '4':
                            _config.AutoConnect = !_config.AutoConnect;
                            break;
                        case '5':
                            _config.ShowFPS = !_config.ShowFPS;
                            break;
                        case 's':
                        case 'S':
                            SaveConfig();
                            Console.WriteLine("Settings saved!");
                            Thread.Sleep(1000);
                            break;
                        case 'b':
                        case 'B':
                            return;
                    }
                }
                catch (Exception ex)
                {
                    LogError($"Settings error: {ex.Message}");
                    Thread.Sleep(2000);
                }
            }
        }

        static void ShowServerHistory()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                   SERVER HISTORY                             ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            if (!_config.ServerHistory.Any())
            {
                Console.WriteLine("  No server history yet.");
            }
            else
            {
                Console.WriteLine("  Recent servers:");
                Console.WriteLine();
                for (int i = 0; i < _config.ServerHistory.Count; i++)
                {
                    Console.WriteLine($"    {i + 1}. {_config.ServerHistory[i]}");
                }
            }

            Console.WriteLine();
            Console.WriteLine("Press any key to return...");
            Console.ReadKey(true);
        }

        static void ShowQuickStart()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                   QUICK START GUIDE                          ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();
            Console.WriteLine("  HOW TO PLAY:");
            Console.WriteLine();
            Console.WriteLine("  Option 1: SOLO MODE (easiest)");
            Console.WriteLine("    1. Select 'Solo Mode' from main menu");
            Console.WriteLine("    2. Start Kenshi game");
            Console.WriteLine("    3. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
            Console.WriteLine("       (Use Process Hacker or similar DLL injector)");
            Console.WriteLine("    4. Plugin connects to 127.0.0.1:7777 automatically");
            Console.WriteLine("    5. Play!");
            Console.WriteLine();
            Console.WriteLine("  Option 2: PLAY WITH FRIENDS");
            Console.WriteLine("    Host:");
            Console.WriteLine("      1. Select 'Host Server'");
            Console.WriteLine("      2. Forward the port in your router");
            Console.WriteLine("      3. Share connection string with friends");
            Console.WriteLine("      4. Start Kenshi and inject plugin");
            Console.WriteLine();
            Console.WriteLine("    Join:");
            Console.WriteLine("      1. Select 'Join Server'");
            Console.WriteLine("      2. Paste connection string from friend");
            Console.WriteLine("      3. Start Kenshi and inject plugin");
            Console.WriteLine();
            Console.WriteLine("  RECOMMENDED DLL INJECTOR:");
            Console.WriteLine("    - Process Hacker (https://processhacker.sourceforge.io/)");
            Console.WriteLine("    - Extreme Injector (https://github.com/master131/ExtremeInjector)");
            Console.WriteLine();
            Console.WriteLine("  PLUGIN LOCATION:");
            Console.WriteLine("    bin/Release/Plugin/Re_Kenshi_Plugin.dll");
            Console.WriteLine();
            Console.WriteLine("Press any key to return...");
            Console.ReadKey(true);
        }

        static async Task RunServer(string bindAddress, int port, CancellationToken ct, string serverName = "Kenshi Online Server", int maxPlayers = 32)
        {
            try
            {
                LogInfo($"Server starting on {bindAddress}:{port}");
                LogInfo($"Working directory: {Directory.GetCurrentDirectory()}");

                // Initialize game systems
                var entityManager = new EntityManager();
                var sessionManager = new SessionManager(maxPlayers);
                var combatSync = new CombatSync(entityManager);
                var inventorySync = new InventorySync(entityManager);
                var worldStateManager = new WorldStateManager();
                var chatSystem = new ChatSystem();
                var squadSystem = new SquadSystem();
                var friendSystem = new FriendSystem();
                var tradingSystem = new TradingSystem();
                var adminCommands = new AdminCommands(sessionManager, entityManager, worldStateManager);

                // Parse bind address
                IPAddress listenAddress;
                if (bindAddress == "0.0.0.0")
                {
                    listenAddress = IPAddress.Any;
                }
                else if (bindAddress == "127.0.0.1" || bindAddress == "localhost")
                {
                    listenAddress = IPAddress.Loopback;
                }
                else
                {
                    listenAddress = IPAddress.Parse(bindAddress);
                }

                // Start TCP listener
                var listener = new TcpListener(listenAddress, port);
                listener.Start();

                LogInfo($"Server listening on {listenAddress}:{port}");
                LogInfo($"Server name: {serverName}");
                LogInfo($"Max players: {maxPlayers}");
                Console.WriteLine($"[SERVER] Ready for connections on {bindAddress}:{port}");

                var clients = new List<TcpClient>();
                var lastUpdate = DateTime.UtcNow;
                const float updateRate = 20f;

                while (!ct.IsCancellationRequested)
                {
                    try
                    {
                        // Accept new connections
                        if (listener.Pending())
                        {
                            var client = await listener.AcceptTcpClientAsync();
                            clients.Add(client);
                            LogInfo($"Client connected from {client.Client.RemoteEndPoint}");
                            _ = Task.Run(() => HandleClient(client, sessionManager, ct), ct);
                        }

                        // Update game state
                        var now = DateTime.UtcNow;
                        var deltaTime = (float)(now - lastUpdate).TotalSeconds;

                        if (deltaTime >= 1.0f / updateRate)
                        {
                            entityManager.Update(deltaTime);
                            worldStateManager.Update(deltaTime);
                            combatSync.Update(deltaTime);
                            inventorySync.Update(deltaTime);
                            sessionManager.Update();

                            lastUpdate = now;
                        }

                        await Task.Delay(10, ct);
                    }
                    catch (Exception ex) when (ct.IsCancellationRequested)
                    {
                        break;
                    }
                    catch (Exception ex)
                    {
                        LogError($"Server loop error: {ex.Message}");
                    }
                }

                // Cleanup
                listener.Stop();
                foreach (var client in clients)
                {
                    try { client.Close(); } catch { }
                }

                LogInfo("Server stopped");
            }
            catch (SocketException ex)
            {
                LogError($"Socket error: {ex.Message} (Code: {ex.ErrorCode})");
                LogError("This usually means the port is already in use or access is denied.");
            }
            catch (Exception ex)
            {
                LogError($"Server error: {ex.Message}");
                LogError(ex.StackTrace ?? "No stack trace");
            }
        }

        static async Task HandleClient(TcpClient client, SessionManager sessionManager, CancellationToken ct)
        {
            try
            {
                using var stream = client.GetStream();
                var buffer = new byte[8192];

                while (!ct.IsCancellationRequested && client.Connected)
                {
                    var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0) break;

                    var data = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    LogInfo($"Received {bytesRead} bytes from client");

                    // Process messages
                    // (Add message handling logic here)
                }
            }
            catch (Exception ex) when (ct.IsCancellationRequested)
            {
                // Expected when canceling
            }
            catch (Exception ex)
            {
                LogError($"Client handler error: {ex.Message}");
            }
            finally
            {
                try { client.Close(); } catch { }
            }
        }

        static async Task RunClient(string serverIP, int port, CancellationToken ct)
        {
            try
            {
                LogInfo($"Client connecting to {serverIP}:{port}");

                // Connect to game server
                var tcpClient = new TcpClient();
                await tcpClient.ConnectAsync(serverIP, port);

                LogInfo($"Client connected to {serverIP}:{port}");
                Console.WriteLine($"[CLIENT] Connected to {serverIP}:{port}");

                // Start IPC server for plugin
                _ = Task.Run(() => RunIPCServer(tcpClient, ct), ct);

                var stream = tcpClient.GetStream();
                var buffer = new byte[8192];

                while (!ct.IsCancellationRequested && tcpClient.Connected)
                {
                    try
                    {
                        if (stream.DataAvailable)
                        {
                            var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                            if (bytesRead == 0) break;

                            LogInfo($"Client received {bytesRead} bytes");
                            // Forward to plugin via IPC
                        }

                        await Task.Delay(10, ct);
                    }
                    catch (Exception ex) when (ct.IsCancellationRequested)
                    {
                        break;
                    }
                }

                tcpClient.Close();
                LogInfo("Client disconnected");
            }
            catch (SocketException ex)
            {
                LogError($"Client connection error: {ex.Message}");
                Console.WriteLine($"[CLIENT] Failed to connect: {ex.Message}");
            }
            catch (Exception ex) when (!ct.IsCancellationRequested)
            {
                LogError($"Client error: {ex.Message}");
            }
        }

        static async Task RunIPCServer(TcpClient gameServerConnection, CancellationToken ct)
        {
            try
            {
                LogInfo("IPC server waiting for game plugin to connect...");
                Console.WriteLine("[IPC] Waiting for game plugin to connect...");

                // Named pipe IPC server for plugin communication
                // (Add IPC logic here when implementing plugin communication)

                while (!ct.IsCancellationRequested)
                {
                    await Task.Delay(100, ct);
                }
            }
            catch (Exception ex) when (!ct.IsCancellationRequested)
            {
                LogError($"IPC error: {ex.Message}");
            }
        }

        static string GetLocalIPAddress()
        {
            try
            {
                var host = Dns.GetHostEntry(Dns.GetHostName());
                foreach (var ip in host.AddressList)
                {
                    if (ip.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(ip))
                    {
                        return ip.ToString();
                    }
                }
            }
            catch (Exception ex)
            {
                LogError($"Failed to get local IP: {ex.Message}");
            }

            return "127.0.0.1";
        }

        static void LoadConfig()
        {
            try
            {
                if (File.Exists(ConfigPath))
                {
                    var json = File.ReadAllText(ConfigPath);
                    _config = JsonSerializer.Deserialize<Config>(json) ?? new Config();
                    LogInfo("Configuration loaded");
                }
                else
                {
                    SaveConfig(); // Create default config
                    LogInfo("Created default configuration");
                }
            }
            catch (Exception ex)
            {
                LogError($"Failed to load config: {ex.Message}");
                _config = new Config();
            }
        }

        static void SaveConfig()
        {
            try
            {
                var options = new JsonSerializerOptions { WriteIndented = true };
                var json = JsonSerializer.Serialize(_config, options);
                File.WriteAllText(ConfigPath, json);
                LogInfo("Configuration saved");
            }
            catch (Exception ex)
            {
                LogError($"Failed to save config: {ex.Message}");
            }
        }

        static async Task HandleCommandLine(string[] args)
        {
            var command = args[0].ToLower();

            try
            {
                switch (command)
                {
                    case "solo":
                    case "-solo":
                    case "--solo":
                        LogInfo("Starting in solo mode (command line)");
                        _serverCts = new CancellationTokenSource();
                        _clientCts = new CancellationTokenSource();

                        var serverTask = Task.Run(() => RunServer("127.0.0.1", 7777, _serverCts.Token));
                        await Task.Delay(2000);
                        var clientTask = Task.Run(() => RunClient("127.0.0.1", 7777, _clientCts.Token));

                        Console.WriteLine("Press Ctrl+C to stop...");
                        Console.CancelKeyPress += (s, e) =>
                        {
                            e.Cancel = true;
                            _serverCts?.Cancel();
                            _clientCts?.Cancel();
                        };

                        await Task.WhenAll(serverTask, clientTask);
                        break;

                    case "host":
                    case "-host":
                    case "--host":
                        var port = args.Length > 1 ? int.Parse(args[1]) : 7777;
                        LogInfo($"Starting server on port {port} (command line)");
                        _serverCts = new CancellationTokenSource();

                        Console.CancelKeyPress += (s, e) =>
                        {
                            e.Cancel = true;
                            _serverCts?.Cancel();
                        };

                        await RunServer("0.0.0.0", port, _serverCts.Token);
                        break;

                    case "join":
                    case "-join":
                    case "--join":
                        if (args.Length < 2)
                        {
                            Console.WriteLine("Usage: KenshiOnline join <ip:port>");
                            return;
                        }

                        var parts = args[1].Split(':');
                        var ip = parts[0];
                        var joinPort = parts.Length > 1 ? int.Parse(parts[1]) : 7777;

                        LogInfo($"Joining server {ip}:{joinPort} (command line)");
                        _clientCts = new CancellationTokenSource();

                        Console.CancelKeyPress += (s, e) =>
                        {
                            e.Cancel = true;
                            _clientCts?.Cancel();
                        };

                        await RunClient(ip, joinPort, _clientCts.Token);
                        break;

                    case "help":
                    case "-help":
                    case "--help":
                    case "-h":
                        ShowCommandLineHelp();
                        break;

                    default:
                        Console.WriteLine($"Unknown command: {command}");
                        Console.WriteLine("Use 'KenshiOnline help' for usage information");
                        break;
                }
            }
            catch (Exception ex)
            {
                LogError($"Command line error: {ex.Message}");
            }
        }

        static void ShowCommandLineHelp()
        {
            Console.WriteLine("Kenshi Online - Command Line Usage");
            Console.WriteLine();
            Console.WriteLine("Usage:");
            Console.WriteLine("  KenshiOnline                    - Start interactive mode");
            Console.WriteLine("  KenshiOnline solo               - Start solo mode (local server + client)");
            Console.WriteLine("  KenshiOnline host [port]        - Host server (default port: 7777)");
            Console.WriteLine("  KenshiOnline join <ip:port>     - Join server");
            Console.WriteLine("  KenshiOnline help               - Show this help");
            Console.WriteLine();
            Console.WriteLine("Examples:");
            Console.WriteLine("  KenshiOnline solo");
            Console.WriteLine("  KenshiOnline host 7777");
            Console.WriteLine("  KenshiOnline join 192.168.1.100:7777");
        }
    }

    class Config
    {
        public int DefaultPort { get; set; } = 7777;
        public int MaxPlayers { get; set; } = 32;
        public string PlayerName { get; set; } = "Player";
        public bool AutoConnect { get; set; } = false;
        public bool ShowFPS { get; set; } = true;
        public List<string> ServerHistory { get; set; } = new List<string>();
    }
}
